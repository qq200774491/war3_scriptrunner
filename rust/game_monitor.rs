//! 启动助手保留创建时的进程句柄，避免游戏收紧权限后无法读取退出码。
use anyhow::{Context, Result};
use std::{path::PathBuf, time::Duration};
use tokio::{
    io::{AsyncBufReadExt, BufReader, Lines},
    process::{Child, ChildStdout, Command},
    time::timeout,
};

pub(crate) struct GameMonitor {
    pub pid: u32,
    helper: Child,
    output: Lines<BufReader<ChildStdout>>,
    errors: PathBuf,
    exit: Option<u32>,
}

impl GameMonitor {
    pub async fn start(command: &mut Command, errors: PathBuf) -> Result<Self> {
        let mut helper = command.spawn().context("创建游戏启动助手")?;
        let output = BufReader::new(helper.stdout.take().context("启动助手缺少标准输出")?).lines();
        let mut monitor = Self {
            pid: 0,
            helper,
            output,
            errors,
            exit: None,
        };
        let result = timeout(Duration::from_secs(15), async {
            while let Some(line) = monitor.output.next_line().await? {
                if let Some(value) = line.strip_prefix("GAME_PID ") {
                    return value.trim().parse::<u32>().context("非法游戏 PID");
                }
            }
            anyhow::bail!(
                "游戏启动失败：{}",
                std::fs::read_to_string(&monitor.errors).unwrap_or_default()
            );
        })
        .await
        .context("游戏启动助手超时")
        .and_then(|result| result);
        match result {
            Ok(pid) => monitor.pid = pid,
            Err(error) => {
                let _ = monitor.helper.kill().await;
                return Err(error);
            }
        }
        Ok(monitor)
    }

    pub async fn exit_code(&mut self) -> Result<Option<u32>> {
        if self.exit.is_some() {
            return Ok(self.exit);
        }
        let Some(status) = self.helper.try_wait()? else {
            return Ok(None);
        };
        while let Some(line) = self.output.next_line().await? {
            if let Some(value) = line.strip_prefix("GAME_EXIT ") {
                self.exit = Some(value.trim().parse().context("非法游戏退出码")?);
            }
        }
        anyhow::ensure!(
            status.success() && self.exit.is_some(),
            "游戏 PID {} 的监测助手异常结束（无法确认游戏退出码）：{}",
            self.pid,
            std::fs::read_to_string(&self.errors).unwrap_or_default()
        );
        Ok(self.exit)
    }

    pub fn detach(self) {
        // 游戏仍运行时保留独立助手及创建句柄，停止云服务后依然能通过 GUI 关闭。
        // 助手在游戏退出时自行释放资源；其 Child 设置 kill_on_drop(false)。
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::process::Stdio;

    #[tokio::test]
    async fn captures_game_exit_even_when_helper_succeeds() {
        let directory = tempfile::tempdir().unwrap();
        let errors = directory.path().join("helper.log");
        let mut command = Command::new("cmd.exe");
        command
            .args(["/D", "/C", "echo GAME_PID 123&echo GAME_EXIT 3221226505"])
            .creation_flags(0x08000000)
            .kill_on_drop(true)
            .stdout(Stdio::piped())
            .stderr(std::fs::File::create(&errors).unwrap());
        let mut monitor = GameMonitor::start(&mut command, errors).await.unwrap();
        assert_eq!(monitor.pid, 123);
        monitor.helper.wait().await.unwrap();
        assert_eq!(monitor.exit_code().await.unwrap(), Some(0xc0000409));
        assert_eq!(monitor.exit_code().await.unwrap(), Some(0xc0000409));
        monitor.detach();
    }

    #[tokio::test]
    async fn startup_failure_reports_helper_error_without_room_timeout() {
        let directory = tempfile::tempdir().unwrap();
        let errors = directory.path().join("helper.log");
        let mut command = Command::new("cmd.exe");
        command
            .args(["/D", "/C", "echo expected launch failure 1>&2&exit /b 9"])
            .creation_flags(0x08000000)
            .kill_on_drop(true)
            .stdout(Stdio::piped())
            .stderr(std::fs::File::create(&errors).unwrap());
        let result = GameMonitor::start(&mut command, errors).await;
        let error = match result {
            Err(error) => error,
            Ok(_) => panic!("failed helper must not launch a room"),
        };
        assert!(error.to_string().contains("expected launch failure"));
    }

    #[tokio::test]
    async fn missing_exit_report_is_not_claimed_as_game_exit() {
        let directory = tempfile::tempdir().unwrap();
        let errors = directory.path().join("helper.log");
        let mut command = Command::new("cmd.exe");
        command
            .args(["/D", "/C", "echo GAME_PID 456"])
            .creation_flags(0x08000000)
            .kill_on_drop(true)
            .stdout(Stdio::piped())
            .stderr(std::fs::File::create(&errors).unwrap());
        let mut monitor = GameMonitor::start(&mut command, errors).await.unwrap();
        monitor.helper.wait().await.unwrap();
        assert!(monitor
            .exit_code()
            .await
            .unwrap_err()
            .to_string()
            .contains("无法确认游戏退出码"));
        monitor.detach();
    }
}
