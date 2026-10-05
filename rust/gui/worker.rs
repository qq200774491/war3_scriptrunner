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
        self.finished.try_recv().ok()
    }
}
impl Drop for Worker {
    fn drop(&mut self) {
        self.stop();
    }
}
