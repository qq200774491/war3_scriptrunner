use scriptroom::{config::Config, launch};
use std::{path::PathBuf, sync::mpsc, time::Duration};
use tokio::sync::{mpsc as async_channel, watch};

pub struct Worker {
    pub logs: async_channel::Receiver<String>,
    finished: mpsc::Receiver<Result<(), String>>,
    stop: watch::Sender<bool>,
}
impl Worker {
    pub fn start(root: PathBuf, config: Config, check: bool) -> Self {
        let (output, logs) = async_channel::channel(2048);
        let (stop, stopping) = watch::channel(false);
        let (complete, finished) = mpsc::channel();
        std::thread::spawn(move || {
            let result = (|| -> anyhow::Result<()> {
                if check {
                    launch::check(&root, &config)?;
                    let _ = output
                        .blocking_send("[检查] 配置及组件通过；启动时检查端口与存档锁。".into());
                    return Ok(());
                }
                let runtime = tokio::runtime::Builder::new_multi_thread()
                    .enable_all()
                    .build()?;
                let result = runtime.block_on(launch::run(root, config, stopping, output));
                runtime.shutdown_timeout(Duration::from_secs(3));
                result
            })();
            let _ = complete.send(result.map_err(|error| format!("{error:#}")));
        });
        Self {
            logs,
            finished,
            stop,
        }
    }
    pub fn stop(&self) {
        let _ = self.stop.send(true);
    }
    pub fn completed(&self) -> Option<Result<(), String>> {
        match self.finished.try_recv() {
            Ok(result) => Some(result),
            Err(mpsc::TryRecvError::Empty) => None,
            Err(mpsc::TryRecvError::Disconnected) => {
                Some(Err("后台线程异常退出，未返回执行结果".into()))
            }
        }
    }
}
impl Drop for Worker {
    fn drop(&mut self) {
        self.stop();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn disconnected_worker_reports_failure_instead_of_remaining_busy() {
        let (complete, finished) = mpsc::channel();
        let (_, logs) = async_channel::channel(1);
        let (stop, _) = watch::channel(false);
        let worker = Worker {
            logs,
            finished,
            stop,
        };
        assert!(worker.completed().is_none());
        drop(complete);
        assert!(worker
            .completed()
            .unwrap()
            .unwrap_err()
            .contains("异常退出"));
    }
}
