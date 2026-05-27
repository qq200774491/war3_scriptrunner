#![cfg(windows)]
#![allow(non_snake_case)]

mod dispatch;
mod hook_reb;
mod japi;
mod log;
mod natives;
mod pipe_client;
mod protocol;

use std::ffi::c_void;
use std::os::raw::c_char;
use std::path::PathBuf;
use std::ptr;

use windows_sys::Win32::Foundation::{CloseHandle, HMODULE};
use windows_sys::Win32::System::Console::{
    AllocConsole, AttachConsole, GetConsoleWindow, ATTACH_PARENT_PROCESS,
};
use windows_sys::Win32::System::LibraryLoader::{
    DisableThreadLibraryCalls, GetModuleFileNameA,
};
use windows_sys::Win32::System::Threading::CreateThread;

use crate::log::{log_err, log_inf, log_wrn};

const DLL_PROCESS_ATTACH: u32 = 1;

/// KKYDWE 插件加载器会 GetProcAddress("PluginName") 并校验返回字符串等于 DLL 文件名
/// （不含扩展名），不匹配就 FreeLibrary。部署文件名是 `mls-sim.dll` → 这里返回 "mls-sim"。
#[no_mangle]
pub extern "C" fn PluginName() -> *const c_char {
    static NAME: &[u8] = b"mls-sim\0";
    NAME.as_ptr() as *const c_char
}

/// KKYDWE 调用的初始化入口；编辑器侧和 war3 侧的 plugin loader 都会调。
/// DllMain 已经把 pipe worker 线程拉起来了，这里负责"jass 桥就绪"：
///   1. `japi::init()` —— 加载 yd_jass_api.dll + ydbase.dll，解析 cdecl 符号
///   2. `hook_reb::install()` —— hook RequestExtraBooleanData，c2s 入口（dt=1009）
///   3. `natives::register_all()` —— 注册 7 个 native（MsPump + MsSendMlEvent + MsGetRoom*）
///
/// 整段必须包 `catch_unwind`——jass 调用栈不能让 Rust panic 穿透。
/// 不能放 DllMain 里做：DllMain 在 LoadLibrary 时触发，jass VM 尚未就绪。
/// hook_reb/natives 都做了 idempotent 守卫，重复调（编辑器+war3 各一次）安全。
#[no_mangle]
pub extern "C" fn Initialize() {
    log_inf("plugin: Initialize() invoked by host");
    let res = std::panic::catch_unwind(|| {
        if !japi::init() {
            log_err("plugin: japi::init() failed, jass bridge unavailable");
            return;
        }
        // 这三步互相不阻断：hook 失败也要尝试 natives，反之亦然——
        // 任何一条 native 通了 lua 那边都能用，hook 与否决定的只是 c2s 路径
        let hook_ok = hook_reb::install();
        if !hook_ok {
            log_err("plugin: hook_reb::install() failed, c2s 1009 path unavailable");
        }
        let nat_ok = natives::register_all();
        if !nat_ok {
            log_wrn("plugin: natives::register_all() reported partial failure");
        }
        log_inf(&format!(
            "plugin: jass bridge init done (japi=ok hook={} natives={})",
            if hook_ok { "queued" } else { "FAILED" },
            if nat_ok { "ok" } else { "partial" },
        ));
    });
    if let Err(e) = res {
        log_err(&format!("plugin: Initialize() PANIC: {:?}", e));
    }
}

/// MSVC's `_DllMainCRTStartup` calls this user-defined `DllMain` after CRT init.
///
/// This is the **war3-direct-load entry**: when the DLL is dropped into
/// `D:\war3Project\KKYDWE\plugin\warcraft3\`, KKYDWE's yd_loader simply
/// LoadLibrary's it and our DllMain does the rest — no PluginName/Initialize
/// call needed from the host.
///
/// Keep work here minimal — schedule a worker thread for blocking I/O.
#[no_mangle]
pub extern "system" fn DllMain(
    hinst: *mut c_void,
    fdw_reason: u32,
    _lpv: *mut c_void,
) -> i32 {
    if fdw_reason == DLL_PROCESS_ATTACH {
        // Skip DLL_THREAD_ATTACH/DETACH callbacks — we don't need per-thread
        // notifications and they only slow down war3 thread spawns.
        unsafe { DisableThreadLibraryCalls(hinst as HMODULE); }

        install_panic_hook();
        ensure_console();
        log_inf(&format!(
            "dllmain: DLL_PROCESS_ATTACH (host={})",
            exe_basename()
        ));
        if should_auto_test() {
            log_inf("dllmain: auto-test enabled, spawning worker thread");
            unsafe {
                let h = CreateThread(
                    ptr::null(),
                    0,
                    Some(auto_test_thread),
                    ptr::null(),
                    0,
                    ptr::null_mut(),
                );
                if h.is_null() {
                    log_err("dllmain: CreateThread failed");
                } else {
                    CloseHandle(h);
                }
            }
        } else {
            log_inf("dllmain: auto-test NOT triggered (no env, no sentinel file)");
        }
    }
    1
}

unsafe extern "system" fn auto_test_thread(_lp: *mut c_void) -> u32 {
    log_inf("auto_test: thread starting");
    pipe_client::run_forever();
    0
}

fn install_panic_hook() {
    std::panic::set_hook(Box::new(|info| {
        let msg = format!("PANIC in mls-sim DLL: {}", info);
        log_err(&msg);
    }));
}

fn ensure_console() {
    unsafe {
        if !GetConsoleWindow().is_null() {
            log_inf("console acquired (source=inherited)");
            return;
        }
        if AttachConsole(ATTACH_PARENT_PROCESS) != 0 {
            log_inf("console acquired (source=parent)");
            return;
        }
        if AllocConsole() != 0 {
            log_inf("console acquired (source=alloc)");
            return;
        }
    }
    log_wrn("console acquire FAILED (only OutputDebugString will be visible)");
}

fn exe_path() -> PathBuf {
    let mut buf = [0u8; 1024];
    let n = unsafe {
        GetModuleFileNameA(ptr::null_mut(), buf.as_mut_ptr(), buf.len() as u32)
    };
    if n == 0 {
        return PathBuf::new();
    }
    let s = String::from_utf8_lossy(&buf[..n as usize]).into_owned();
    PathBuf::from(s)
}

fn exe_basename() -> String {
    exe_path()
        .file_name()
        .map(|s| s.to_string_lossy().into_owned())
        .unwrap_or_default()
}

fn should_auto_test() -> bool {
    if std::env::var("MLS_AUTO_TEST").map(|v| v == "1").unwrap_or(false) {
        log_inf("auto_test trigger: MLS_AUTO_TEST=1");
        return true;
    }
    let exe = exe_path();
    if let Some(dir) = exe.parent() {
        let sentinel = dir.join("mls_auto_test.txt");
        if sentinel.exists() {
            log_inf(&format!(
                "auto_test trigger: sentinel file found at {}",
                sentinel.display()
            ));
            return true;
        }
    }
    false
}
