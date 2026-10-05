use scriptroom::platform::{self, ForceTarget};
use std::{
    sync::mpsc,
    time::{Duration, Instant},
};

pub enum CloseUpdate {
    Message(String),
    Finished,
}

// 关闭阶段互斥；确认持有捕获的句柄，后台任务直接取得所有权，不再次按 PID 查询。
pub enum GameClose {
    Idle,
    Waiting(Instant),
    Confirm(Vec<ForceTarget>),
    Running(mpsc::Receiver<CloseUpdate>),
}
impl GameClose {
    pub fn busy(&self) -> bool {
        !matches!(self, Self::Idle)
    }

    pub fn request(&mut self) -> anyhow::Result<Vec<String>> {
        let games = platform::games()?;
        if games.is_empty() {
            return Ok(vec!["没有运行中的魔兽".into()]);
        }
        let messages = games
            .iter()
            .map(|game| platform::request_close(game).unwrap_or_else(|error| error.to_string()))
            .collect();
        *self = Self::Waiting(Instant::now());
        Ok(messages)
    }

    pub fn poll(&mut self) -> Vec<String> {
        match self {
            Self::Waiting(time) if time.elapsed() >= Duration::from_secs(3) => {
                *self = Self::Idle;
                let games = match platform::games() {
                    Ok(games) => games,
                    Err(error) => return vec![error.to_string()],
                };
                let remaining = games.len();
                let mut messages = Vec::new();
                let mut targets = Vec::new();
                for game in games {
                    match ForceTarget::capture(&game) {
                        Ok(target) => targets.push(target),
                        Err(error) => messages.push(error.to_string()),
                    }
                }
                if remaining == 0 {
                    messages.push("全部魔兽已退出".into());
                } else {
                    messages.push(format!(
                        "仍有 {remaining} 个魔兽进程；可结束 {} 个，权限失败详情见控制台",
                        targets.len()
                    ));
                }
                if !targets.is_empty() {
                    *self = Self::Confirm(targets);
                }
                messages
            }
            Self::Running(receive) => {
                let mut messages = Vec::new();
                loop {
                    match receive.try_recv() {
                        Ok(CloseUpdate::Message(message)) => messages.push(message),
                        Ok(CloseUpdate::Finished) => {
                            *self = Self::Idle;
                            messages.push("结束操作已完成，逐个进程结果见控制台".into());
                            break;
                        }
                        Err(mpsc::TryRecvError::Empty) => break,
                        Err(mpsc::TryRecvError::Disconnected) => {
                            *self = Self::Idle;
                            messages.push("关闭任务意外中断，请检查逐个进程结果".into());
                            break;
                        }
                    }
                }
                messages
            }
            _ => Vec::new(),
        }
    }

    pub fn cancel(&mut self) {
        *self = Self::Idle;
    }

    pub fn terminate(&mut self) -> anyhow::Result<()> {
        let Self::Confirm(targets) = std::mem::replace(self, Self::Idle) else {
            anyhow::bail!("没有待确认的关闭目标");
        };
        let (output, receive) = mpsc::channel();
        std::thread::Builder::new()
            .name("game-close".into())
            .spawn(move || {
                for target in targets {
                    let message = target.terminate().unwrap_or_else(|error| error.to_string());
                    let _ = output.send(CloseUpdate::Message(message));
                }
                let _ = output.send(CloseUpdate::Finished);
            })?;
        *self = Self::Running(receive);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pending_close_is_nonblocking_and_disconnection_is_an_error() {
        let (output, receive) = mpsc::channel();
        let mut state = GameClose::Running(receive);
        assert!(state.poll().is_empty());
        assert!(state.busy());
        drop(output);
        assert!(state.poll()[0].contains("意外中断"));
        assert!(!state.busy());
    }

    #[test]
    fn completion_retains_individual_results() {
        let (output, receive) = mpsc::channel();
        let mut state = GameClose::Running(receive);
        output
            .send(CloseUpdate::Message("进程退出失败".into()))
            .unwrap();
        output.send(CloseUpdate::Finished).unwrap();
        drop(output);
        let messages = state.poll();
        assert_eq!(messages[0], "进程退出失败");
        assert!(messages[1].contains("已完成"));
        assert!(!state.busy());
    }
}
