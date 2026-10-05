//! Windows 边界集中在这里；句柄拥有者负责释放，业务层不持有裸句柄。
use anyhow::{bail, Context, Result};
use std::{
    ffi::OsStr,
    fs::{File, OpenOptions},
    os::windows::{ffi::OsStrExt, fs::OpenOptionsExt, io::AsRawHandle},
    path::Path,
};
use windows_sys::Win32::{
    Foundation::*,
    Globalization::*,
    Storage::FileSystem::*,
    System::{Diagnostics::ToolHelp::*, Threading::*, IO::OVERLAPPED},
    UI::WindowsAndMessaging::*,
};

fn wide(value: impl AsRef<OsStr>) -> Vec<u16> {
    value.as_ref().encode_wide().chain(Some(0)).collect()
}

struct Handle(HANDLE);
impl Drop for Handle {
    fn drop(&mut self) {
        unsafe {
            CloseHandle(self.0);
        }
    }
}

pub fn exclusive_file(path: &Path) -> Result<File> {
    OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .truncate(false)
        .share_mode(0)
        .open(path)
        .with_context(|| format!("文件被占用或无法写入：{}", path.display()))
}

pub fn launch_lock(path: &Path) -> Result<File> {
    let file = OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .truncate(false)
        .open(path)?;
    let mut overlap: OVERLAPPED = unsafe { std::mem::zeroed() };
    // 与 Python msvcrt.locking 锁住同一字节，防止旧入口和新入口同时启动。
    if unsafe {
        LockFileEx(
            file.as_raw_handle() as HANDLE,
            LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
            0,
            1,
            0,
            &mut overlap,
        )
    } == 0
    {
        bail!(
            "已有启动器运行或启动锁不可用：{}",
            std::io::Error::last_os_error()
        );
    }
    Ok(file)
}

pub fn decode_ansi(bytes: &[u8]) -> Result<String> {
    if bytes.is_empty() {
        return Ok(String::new());
    }
    let length = i32::try_from(bytes.len())?;
    let count = unsafe {
        MultiByteToWideChar(
            CP_ACP,
            MB_ERR_INVALID_CHARS,
            bytes.as_ptr(),
            length,
            std::ptr::null_mut(),
            0,
        )
    };
    anyhow::ensure!(count > 0, "INI 不是有效的系统代码页文本");
    let mut output = vec![0; count as usize];
    unsafe {
        MultiByteToWideChar(
            CP_ACP,
            MB_ERR_INVALID_CHARS,
            bytes.as_ptr(),
            length,
            output.as_mut_ptr(),
            count,
        );
    }
    Ok(String::from_utf16(&output)?)
}

pub fn event_signaled(name: &str) -> Result<bool> {
    let handle = unsafe { OpenEventW(SYNCHRONIZE, 0, wide(name).as_ptr()) };
    if handle.is_null() {
        let error = std::io::Error::last_os_error();
        if error.raw_os_error() == Some(ERROR_FILE_NOT_FOUND as i32) {
            return Ok(false);
        }
        return Err(error.into());
    }
    let handle = Handle(handle);
    match unsafe { WaitForSingleObject(handle.0, 0) } {
        WAIT_OBJECT_0 => Ok(true),
        WAIT_TIMEOUT => Ok(false),
        _ => Err(std::io::Error::last_os_error().into()),
    }
}

#[derive(Debug, Clone)]
pub struct GameProcess {
    pub pid: u32,
    pub name: String,
}

pub fn games() -> Result<Vec<GameProcess>> {
    let handle = unsafe { CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
    anyhow::ensure!(
        handle != INVALID_HANDLE_VALUE,
        "无法枚举魔兽进程：{}",
        std::io::Error::last_os_error()
    );
    let snapshot = Handle(handle);
    let mut entry: PROCESSENTRY32W = unsafe { std::mem::zeroed() };
    entry.dwSize = std::mem::size_of_val(&entry) as u32;
    let mut result = Vec::new();
    let mut found = unsafe { Process32FirstW(snapshot.0, &mut entry) };
    while found != 0 {
        let end = entry
            .szExeFile
            .iter()
            .position(|&c| c == 0)
            .unwrap_or(entry.szExeFile.len());
        let name = String::from_utf16_lossy(&entry.szExeFile[..end]);
        if name.eq_ignore_ascii_case("war3.exe") || name.eq_ignore_ascii_case("warcraft iii.exe") {
            result.push(GameProcess {
                pid: entry.th32ProcessID,
                name,
            });
        }
        found = unsafe { Process32NextW(snapshot.0, &mut entry) };
    }
    let error = unsafe { GetLastError() };
    anyhow::ensure!(error == ERROR_NO_MORE_FILES, "枚举进程失败：{error}");
    Ok(result)
}

struct CloseWindows {
    pid: u32,
    sent: usize,
    failed: usize,
}
unsafe extern "system" fn close_window(window: HWND, context: LPARAM) -> BOOL {
    let state = &mut *(context as *mut CloseWindows);
    let mut pid = 0;
    GetWindowThreadProcessId(window, &mut pid);
    if pid == state.pid && IsWindowVisible(window) != 0 {
        if PostMessageW(window, WM_CLOSE, 0, 0) != 0 {
            state.sent += 1;
        } else {
            state.failed += 1;
        }
    }
    1
}

pub fn request_close(game: &GameProcess) -> Result<String> {
    let mut state = CloseWindows {
        pid: game.pid,
        sent: 0,
        failed: 0,
    };
    let success = unsafe { EnumWindows(Some(close_window), &mut state as *mut _ as LPARAM) };
    anyhow::ensure!(success != 0, "无法枚举游戏窗口");
    Ok(format!(
        "PID {}：已发送 {} 个正常关闭请求，{} 个失败{}",
        game.pid,
        state.sent,
        state.failed,
        if state.sent == 0 {
            "（窗口不可见或已退出）"
        } else {
            "；等待游戏退出"
        }
    ))
}

enum ForceHandle {
    Process(Handle),
    Owner { request: Handle, exited: Handle },
}

/// 优先使用启动助手持有的创建句柄；确认前捕获事件/进程句柄，不按 PID 重新定位。
pub struct ForceTarget {
    pub pid: u32,
    handle: ForceHandle,
}
// Windows 进程和事件句柄允许跨线程使用；这里仅转移独占所有权，不提供 Sync。
unsafe impl Send for ForceTarget {}

impl ForceTarget {
    pub fn capture(game: &GameProcess) -> Result<Self> {
        anyhow::ensure!(
            game.name.eq_ignore_ascii_case("war3.exe")
                || game.name.eq_ignore_ascii_case("warcraft iii.exe"),
            "不是魔兽进程"
        );
        let prefix = format!("Local\\ScriptRoomGame-{}", game.pid);
        let request = unsafe {
            OpenEventW(
                EVENT_MODIFY_STATE,
                0,
                wide(format!("{prefix}-terminate")).as_ptr(),
            )
        };
        if !request.is_null() {
            let request = Handle(request);
            let exited =
                unsafe { OpenEventW(SYNCHRONIZE, 0, wide(format!("{prefix}-exited")).as_ptr()) };
            anyhow::ensure!(
                !exited.is_null(),
                "PID {} 的启动助手控制通道不完整：{}",
                game.pid,
                std::io::Error::last_os_error()
            );
            return Ok(Self {
                pid: game.pid,
                handle: ForceHandle::Owner {
                    request,
                    exited: Handle(exited),
                },
            });
        }
        let handle = unsafe {
            OpenProcess(
                PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                0,
                game.pid,
            )
        };
        anyhow::ensure!(
            !handle.is_null(),
            "PID {} 无法取得结束权限：{}",
            game.pid,
            std::io::Error::last_os_error()
        );
        let handle = Handle(handle);
        let mut buffer = vec![0u16; 32768];
        let mut size = buffer.len() as u32;
        anyhow::ensure!(
            unsafe { QueryFullProcessImageNameW(handle.0, 0, buffer.as_mut_ptr(), &mut size) } != 0,
            "无法核实进程身份"
        );
        let path = String::from_utf16_lossy(&buffer[..size as usize]);
        let name = Path::new(&path)
            .file_name()
            .context("无进程名称")?
            .to_string_lossy();
        anyhow::ensure!(name.eq_ignore_ascii_case(&game.name), "进程身份已改变");
        Ok(Self {
            pid: game.pid,
            handle: ForceHandle::Process(handle),
        })
    }
    pub fn terminate(self) -> Result<String> {
        let wait = match &self.handle {
            ForceHandle::Process(handle) => handle.0,
            ForceHandle::Owner { exited, .. } => exited.0,
        };
        if unsafe { WaitForSingleObject(wait, 0) } == WAIT_OBJECT_0 {
            return Ok(format!("PID {} 已退出", self.pid));
        }
        let success = match &self.handle {
            ForceHandle::Process(handle) => unsafe { TerminateProcess(handle.0, 1) },
            ForceHandle::Owner { request, .. } => unsafe { SetEvent(request.0) },
        };
        anyhow::ensure!(
            success != 0,
            "PID {} 结束请求失败：{}",
            self.pid,
            std::io::Error::last_os_error()
        );
        anyhow::ensure!(
            unsafe { WaitForSingleObject(wait, 3000) } == WAIT_OBJECT_0,
            "PID {} 尚未退出，未报告关闭成功",
            self.pid
        );
        Ok(format!("PID {} 已强制结束", self.pid))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::{os::windows::process::CommandExt, time::Duration};

    #[test]
    #[ignore = "只由 process_identity_and_force_close 启动为独立测试进程"]
    fn fixture_wait() {
        if let Some(ready) = std::env::var_os("SCRIPTROOM_FIXTURE_READY") {
            use windows_sys::Win32::Security::*;
            unsafe {
                let mut acl: ACL = std::mem::zeroed();
                let mut descriptor: SECURITY_DESCRIPTOR = std::mem::zeroed();
                let descriptor_ptr = (&mut descriptor as *mut SECURITY_DESCRIPTOR).cast();
                assert_ne!(
                    InitializeAcl(&mut acl, std::mem::size_of::<ACL>() as u32, 2),
                    0
                );
                assert_ne!(InitializeSecurityDescriptor(descriptor_ptr, 1), 0);
                assert_ne!(SetSecurityDescriptorDacl(descriptor_ptr, 1, &acl, 0), 0);
                assert_ne!(
                    SetKernelObjectSecurity(
                        GetCurrentProcess(),
                        DACL_SECURITY_INFORMATION,
                        descriptor_ptr
                    ),
                    0
                );
            }
            std::fs::write(ready, std::process::id().to_string()).unwrap();
        }
        std::thread::sleep(Duration::from_secs(60));
    }

    #[test]
    fn owner_control_closes_process_after_external_access_is_denied() {
        let root = Path::new(env!("CARGO_MANIFEST_DIR"));
        let config = crate::config::Config::load(&root.join("本地测试配置.json")).unwrap();
        let directory = tempfile::tempdir().unwrap();
        let executable = directory.path().join("war3.exe");
        let ready = directory.path().join("ready.txt");
        let script = directory.path().join("control.lua");
        std::fs::copy(std::env::current_exe().unwrap(), &executable).unwrap();
        let command = format!(
            "\"{}\" --ignored --exact platform::tests::fixture_wait",
            executable.display()
        );
        std::fs::write(
            &script,
            format!(
                r#"
package.cpath = [==[{modules}/?.dll]==] .. ';' .. package.cpath
require 'filesystem'
require 'sys'
local process = sys.process()
assert(process:create(nil, [==[{command}]==], fs.path([==[{directory}]==])))
local monitor = assert(loadfile([==[{monitor}]==]))()
local code = monitor(process)
process:close()
print('GAME_EXIT ' .. tostring(code))
"#,
                modules = config.editor.join("bin/modules").display(),
                directory = directory.path().display(),
                monitor = root.join("tools/game_control.lua").display()
            ),
        )
        .unwrap();
        let paths = std::iter::once(config.editor.join("bin")).chain(
            std::env::split_paths(&std::env::var_os("PATH").unwrap_or_default())
                .collect::<Vec<_>>(),
        );
        let mut helper = std::process::Command::new(root.join("build/Release/lua_probe.exe"))
            .arg(config.editor.join("bin/luacore.dll"))
            .arg(script)
            .env("PATH", std::env::join_paths(paths).unwrap())
            .env("SCRIPTROOM_FIXTURE_READY", &ready)
            .creation_flags(0x08000000)
            .stdout(std::process::Stdio::piped())
            .stderr(std::process::Stdio::piped())
            .spawn()
            .unwrap();
        let result = (|| -> Result<()> {
            let start = std::time::Instant::now();
            while !ready.is_file() {
                anyhow::ensure!(
                    start.elapsed() < Duration::from_secs(10),
                    "fixture ready timeout"
                );
                anyhow::ensure!(
                    helper.try_wait()?.is_none(),
                    "control helper exited before fixture readiness"
                );
                std::thread::sleep(Duration::from_millis(20));
            }
            let game = GameProcess {
                pid: std::fs::read_to_string(&ready)?.parse()?,
                name: "war3.exe".into(),
            };
            let direct = unsafe {
                OpenProcess(
                    PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                    0,
                    game.pid,
                )
            };
            if !direct.is_null() {
                drop(Handle(direct));
                anyhow::bail!("fixture must reject direct access to reproduce error 5");
            }
            anyhow::ensure!(
                std::io::Error::last_os_error().raw_os_error() == Some(5),
                "expected access denied"
            );
            let cancelled = ForceTarget::capture(&game)?;
            anyhow::ensure!(
                matches!(cancelled.handle, ForceHandle::Owner { .. }),
                "expected owner control path"
            );
            drop(cancelled);
            std::thread::sleep(Duration::from_millis(150));
            anyhow::ensure!(
                helper.try_wait()?.is_none(),
                "capture/cancel must not terminate the game"
            );
            let target = ForceTarget::capture(&game)?;
            let stale_confirmation = ForceTarget::capture(&game)?;
            anyhow::ensure!(
                std::thread::spawn(move || target.terminate())
                    .join()
                    .expect("关闭线程异常退出")?
                    .contains("已强制结束"),
                "owner close failed"
            );
            anyhow::ensure!(
                stale_confirmation.terminate()?.contains("已退出"),
                "old confirmation must stay bound to exited process"
            );
            Ok(())
        })();
        if result.is_err() {
            let _ = helper.kill();
        }
        let output = helper.wait_with_output().unwrap();
        assert!(
            result.is_ok(),
            "{result:?}; helper stderr={}",
            String::from_utf8_lossy(&output.stderr)
        );
        assert!(
            output.status.success(),
            "{}",
            String::from_utf8_lossy(&output.stderr)
        );
        assert!(String::from_utf8_lossy(&output.stdout).contains("GAME_EXIT "));
    }

    #[test]
    fn process_identity_and_force_close() {
        let directory = tempfile::tempdir().unwrap();
        let executable = directory.path().join("war3.exe");
        std::fs::copy(std::env::current_exe().unwrap(), &executable).unwrap();
        let mut child = std::process::Command::new(executable)
            .args(["--ignored", "--exact", "platform::tests::fixture_wait"])
            .creation_flags(0x08000000)
            .spawn()
            .unwrap();
        let game = GameProcess {
            pid: child.id(),
            name: "war3.exe".into(),
        };
        let result = (|| -> Result<()> {
            anyhow::ensure!(
                games()?.iter().any(|p| p.pid == game.pid),
                "fixture missing"
            );
            let wrong = GameProcess {
                pid: game.pid,
                name: "other.exe".into(),
            };
            anyhow::ensure!(
                ForceTarget::capture(&wrong).is_err(),
                "identity mismatch must be rejected"
            );
            let target = ForceTarget::capture(&game)?;
            let notice = request_close(&game)?;
            anyhow::ensure!(
                notice.contains("0 个正常关闭"),
                "headless fixture should have no window"
            );
            target.terminate()?;
            Ok(())
        })();
        let _ = child.kill();
        let _ = child.wait();
        result.unwrap();
    }
}
