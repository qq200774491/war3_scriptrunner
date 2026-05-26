pub mod protocol;
pub mod session;

use std::sync::{Arc, RwLock};

use crate::room::RoomManager;

#[cfg(windows)]
pub async fn run_pipe_server(pipe_name: String, manager: Arc<RwLock<RoomManager>>) {
    use tokio::net::windows::named_pipe::ServerOptions;

    let full_name = normalize_pipe_name(&pipe_name);

    tracing::info!("Pipe server listening on {}", full_name);

    let mut server = match ServerOptions::new()
        .first_pipe_instance(true)
        .create(&full_name)
    {
        Ok(s) => s,
        Err(e) => {
            tracing::error!("Pipe server failed to create {}: {}", full_name, e);
            return;
        }
    };

    loop {
        if let Err(e) = server.connect().await {
            tracing::error!("Pipe server connect failed: {}", e);
            tokio::time::sleep(std::time::Duration::from_secs(1)).await;
            continue;
        }

        let connected = server;
        // 重试直到成功创建下一个 instance，否则 server 会无法再 accept
        server = loop {
            match ServerOptions::new().create(&full_name) {
                Ok(s) => break s,
                Err(e) => {
                    tracing::error!("Pipe server next instance failed, retry in 1s: {}", e);
                    tokio::time::sleep(std::time::Duration::from_secs(1)).await;
                }
            }
        };

        let manager_for_session = manager.clone();
        tokio::spawn(async move {
            session::run_session(connected, manager_for_session).await;
        });
    }
}

#[cfg(not(windows))]
pub async fn run_pipe_server(_pipe_name: String, _manager: Arc<RwLock<RoomManager>>) {
    tracing::warn!("Named pipe bridge is Windows-only; this build is not Windows");
}

fn normalize_pipe_name(name: &str) -> String {
    if name.starts_with(r"\\") {
        name.to_string()
    } else {
        format!(r"\\.\pipe\{}", name)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn normalize_relative_name() {
        assert_eq!(normalize_pipe_name("foo"), r"\\.\pipe\foo");
    }

    #[test]
    fn normalize_full_name_passthrough() {
        assert_eq!(
            normalize_pipe_name(r"\\.\pipe\mls-sim-bridge"),
            r"\\.\pipe\mls-sim-bridge"
        );
    }
}
