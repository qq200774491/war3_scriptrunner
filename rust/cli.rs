use anyhow::Result;
use clap::Parser;
use scriptroom::{config, host::HostConfig, launch, room::Room};
use std::{path::PathBuf, time::Duration};
use tokio::sync::{mpsc, watch};

#[derive(Parser)]
#[command(version, about = "ScriptRoom：原生双人启动与共享云脚本")]
struct Args {
    #[arg(long)]
    config: Option<PathBuf>,
    #[arg(long)]
    check: bool,
    #[arg(long)]
    seconds: Option<u64>,
    #[arg(long)]
    stop_file: Option<PathBuf>,
    /// 仅启动云服务，将两名玩家的连接参数写入指定 JSON（集成测试使用）。
    #[arg(long)]
    serve: Option<PathBuf>,
    #[arg(long)]
    scripts: Option<PathBuf>,
    #[arg(long)]
    archives: Option<PathBuf>,
    #[arg(long)]
    direct: bool,
    #[arg(long)]
    map: Option<PathBuf>,
    #[arg(long)]
    local_entry: Option<PathBuf>,
    #[arg(long)]
    cloud_scripts: Option<PathBuf>,
    #[arg(long)]
    editor: Option<PathBuf>,
    #[arg(long)]
    source_game: Option<PathBuf>,
    #[arg(long)]
    game: Option<PathBuf>,
    #[arg(long)]
    startup_timeout: Option<f64>,
}
#[tokio::main]
async fn main() -> Result<()> {
    let args = Args::parse();
    let root = config::root()?;
    let (stop, stopping) = watch::channel(false);
    let (logs, mut messages) = mpsc::channel(2048);
    let printer = tokio::spawn(async move {
        while let Some(line) = messages.recv().await {
            println!("{line}");
        }
    });
    let stop_file = args.stop_file.clone();
    let monitor = tokio::spawn(async move {
        loop {
            tokio::select! {
                _ = tokio::signal::ctrl_c() => break,
                _ = tokio::time::sleep(Duration::from_millis(150)) => {
                    if stop_file.as_ref().is_some_and(|p| p.is_file()) { break; }
                }
            }
        }
        let _ = stop.send(true);
    });
    let result = if let Some(profiles) = args.serve {
        let scripts = args
            .scripts
            .ok_or_else(|| anyhow::anyhow!("--serve requires --scripts"))?;
        let archives = args
            .archives
            .ok_or_else(|| anyhow::anyhow!("--serve requires --archives"))?;
        std::fs::create_dir_all(&archives)?;
        let room = Room::start(
            HostConfig {
                executable: root.join("build/Release/script_host.exe"),
                dll: root.join("scriptrunner.dll"),
                scripts: std::path::absolute(scripts)?,
                archives: std::path::absolute(archives)?,
            },
            if args.direct { None } else { Some(0) },
            logs.clone(),
        )
        .await?;
        scriptroom::archives::atomic_write(&profiles, &serde_json::to_vec_pretty(&room.profiles)?)?;
        println!("ROOM_READY {}", room.profiles[0].room);
        let mut stopping = stopping;
        let mut status = room.status.clone();
        let result = loop {
            tokio::select! {
                _ = stopping.changed() => break Ok(()),
                changed = status.changed() => {
                    if let Some(reason) = status.borrow().failure.clone() { break Err(anyhow::anyhow!(reason)); }
                    if changed.is_err() { break Ok(()); }
                }
            }
        };
        room.close().await;
        result
    } else {
        let path = args.config.unwrap_or_else(|| config::default_config(&root));
        let mut config = config::Config::load(&path)?;
        for (field, value) in [
            (&mut config.map, args.map),
            (&mut config.local_entry, args.local_entry),
            (&mut config.cloud_scripts, args.cloud_scripts),
            (&mut config.archives, args.archives),
            (&mut config.editor, args.editor),
            (&mut config.source_game, args.source_game),
            (&mut config.game, args.game),
        ] {
            if let Some(value) = value {
                *field = std::path::absolute(value)?;
            }
        }
        if let Some(seconds) = args.startup_timeout {
            config.startup_timeout = seconds;
        }
        if let Some(seconds) = args.seconds {
            config.seconds = seconds;
        }
        if args.check {
            launch::check(&root, &config)?;
            println!("配置检查通过；启动时还会检查端口和存档锁。");
            Ok(())
        } else {
            launch::run(root, config, stopping, logs.clone()).await
        }
    };
    drop(logs);
    let _ = printer.await;
    monitor.abort();
    result
}
