use clap::Parser;
use mls_sim::config::{AppConfig, Cli};
use mls_sim::room::RoomManager;
use mls_sim::{bridge, config, gui, pipe, storage};
use std::path::PathBuf;
use std::sync::{Arc, RwLock};

fn main() {
    #[cfg(windows)]
    {
        use std::os::windows::io::AsRawHandle;
        unsafe {
            let handle = std::io::stdout().as_raw_handle();
            let mut mode: u32 = 0;
            windows_sys::Win32::System::Console::GetConsoleMode(handle, &mut mode);
            windows_sys::Win32::System::Console::SetConsoleMode(
                handle,
                mode | windows_sys::Win32::System::Console::ENABLE_VIRTUAL_TERMINAL_PROCESSING,
            );
        }
    }

    // 当 stdout 是终端时打彩色；重定向到文件时关掉 ANSI 转义码（避免 [2m[32m 这种"乱码"）
    let use_ansi = std::io::IsTerminal::is_terminal(&std::io::stdout());
    tracing_subscriber::fmt()
        .with_ansi(use_ansi)
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env().unwrap_or_else(|_| "info".into()),
        )
        .init();

    let cli = Cli::parse();

    #[cfg(windows)]
    if cli.console_notwrte {
        unsafe {
            windows_sys::Win32::System::Console::FreeConsole();
        }
    }

    let config_path = cli.config.clone();
    let config = AppConfig::load(&cli);

    let host = config.host.clone();
    let port = config.port;
    let auto_room = config.auto_room.clone();

    let manager = Arc::new(RwLock::new(RoomManager::new()));
    let config = Arc::new(RwLock::new(config));

    if let Some(ref room_cfg) = auto_room {
        let script_dir = PathBuf::from(&room_cfg.script_dir);
        if script_dir.is_dir() {
            let mut players = config::build_players_from_config(&room_cfg.players);
            let archive_dir = config.read().unwrap().archive_dir.clone();
            storage::apply_saved_archives(&archive_dir, &room_cfg.script_dir, &mut players);
            let simulation = config::build_room_simulation_from_config(Some(room_cfg), &players);
            let mut mgr = manager.write().unwrap();
            let room_id = mgr.create_room_with_simulation(
                script_dir.clone(),
                room_cfg.mode_id,
                players,
                simulation,
                archive_dir,
            );
            tracing::info!(
                "Auto-created room {} with script_dir: {}",
                room_id,
                script_dir.display()
            );
        } else {
            tracing::warn!("Auto-room script_dir not found: {}", room_cfg.script_dir);
        }
    }

    let rt = tokio::runtime::Runtime::new().unwrap();
    {
        let manager = manager.clone();
        let config = config.clone();
        rt.spawn(async move {
            bridge::run_bridge_server(host, port, manager, config).await;
        });
    }

    {
        let (pipe_enable, pipe_name) = {
            let c = config.read().unwrap();
            (c.pipe_enable, c.pipe_name.clone())
        };
        if pipe_enable {
            let manager = manager.clone();
            rt.spawn(async move {
                pipe::run_pipe_server(pipe_name, manager).await;
            });
        } else {
            tracing::info!("Pipe bridge disabled by config");
        }
    }

    println!();
    println!(
        "  MLS Simulator v{} (Native GUI)",
        env!("CARGO_PKG_VERSION")
    );
    println!();

    // 让主线程进入 tokio runtime，否则 dioxus webview 创建时
    // 调 tokio::runtime::Handle::current() 会 panic
    let _enter = rt.enter();
    gui::launch_app(manager, config, config_path);
}
