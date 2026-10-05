use crate::{
    archives::{self, Ini},
    config::Config,
    game_monitor::GameMonitor,
    host::HostConfig,
    platform,
    room::Room,
};
use anyhow::{Context, Result};
use sha2::{Digest, Sha256};
use std::{
    fs,
    io::{Read, Write},
    path::{Path, PathBuf},
    process::Stdio,
    time::{Duration, SystemTime, UNIX_EPOCH},
};
use tokio::{
    process::Command,
    sync::{mpsc, watch},
    task::JoinHandle,
    time::Instant,
};

const GAME_HASH: &str = "e04d1716603c075eb0c8e1e21cf1093a664adc5249efab396bfa08d7b09d0c3a";
const GAME_FILES: [&str; 11] = [
    "War3.exe",
    "Game.dll",
    "Storm.dll",
    "Mss32.dll",
    "ijl15.dll",
    "msvcr120.dll",
    "msvcp120.dll",
    "war3.mpq",
    "War3x.mpq",
    "War3xLocal.mpq",
    "War3Patch.mpq",
];
const PLUGINS: [&str; 4] = [
    "yd_mutil_warcraft3.dll",
    "yd_lua_engine.dll",
    "kkapi.dll",
    "kkapi_local_plugin.dll",
];

fn digest(path: &Path) -> Result<Vec<u8>> {
    let mut input = fs::File::open(path)?;
    let mut hash = Sha256::new();
    let mut buffer = [0; 65536];
    loop {
        let count = input.read(&mut buffer)?;
        if count == 0 {
            break;
        }
        hash.update(&buffer[..count]);
    }
    Ok(hash.finalize().to_vec())
}
fn copy(source: &Path, destination: &Path) -> Result<()> {
    if destination.exists()
        && fs::metadata(source)?.len() == fs::metadata(destination)?.len()
        && digest(source)? == digest(destination)?
    {
        return Ok(());
    }
    fs::create_dir_all(destination.parent().context("missing destination parent")?)?;
    fs::copy(source, destination)
        .with_context(|| format!("复制 {} 到 {}", source.display(), destination.display()))?;
    Ok(())
}
fn absolute(path: &Path) -> Result<PathBuf> {
    Ok(std::path::absolute(path)?)
}

pub fn check(root: &Path, config: &Config) -> Result<()> {
    config.validate_numbers()?;
    for path in [
        &config.map,
        &config.local_entry,
        &config.cloud_scripts.join("main.lua"),
        &config.editor.join("bin/luacore.dll"),
        &root.join("scriptrunner.dll"),
        &root.join("map/legacy_mls.lua"),
        &root.join("tools/launch_game.lua"),
        &root.join("tools/game_control.lua"),
    ] {
        anyhow::ensure!(path.is_file(), "缺少文件：{}", path.display());
    }
    for name in [
        "script_host.exe",
        "lua_probe.exe",
        "shared_room_native.dll",
        "zz_shared_quicktest.dll",
    ] {
        anyhow::ensure!(
            root.join("build/Release").join(name).is_file(),
            "缺少原生组件：{name}"
        );
    }
    for name in GAME_FILES {
        anyhow::ensure!(
            config.source_game.join(name).is_file(),
            "缺少魔兽文件：{name}"
        );
    }
    let plugins = config.editor.join("plugin/warcraft3");
    let ini = Ini::parse_text(
        fs::read_to_string(plugins.join("config.cfg"))?.trim_start_matches('\u{feff}'),
    )?;
    for name in PLUGINS {
        anyhow::ensure!(
            plugins.join(name).is_file() && ini.get("Enable", name) == Some("1"),
            "编辑器插件缺失或未启用：{name}"
        );
    }
    anyhow::ensure!(
        hex::encode(digest(&config.source_game.join("Game.dll"))?) == GAME_HASH,
        "Game.dll 不是已验证的 1.27 版本，停止启动"
    );
    let source = fs::canonicalize(&config.source_game)?;
    let game = if config.game.exists() {
        fs::canonicalize(&config.game)?
    } else {
        absolute(&config.game)?
    };
    anyhow::ensure!(
        source
            .to_string_lossy()
            .trim_start_matches(r"\?\")
            .to_lowercase()
            != game
                .to_string_lossy()
                .trim_start_matches(r"\?\")
                .to_lowercase(),
        "测试目录不能覆盖原魔兽目录"
    );
    Ok(())
}

fn prepare(root: &Path, config: &Config) -> Result<([PathBuf; 2], PathBuf)> {
    check(root, config)?;
    let probe = std::net::TcpListener::bind("0.0.0.0:6112")
        .context("6112 端口被占用，请先退出上一轮魔兽")?;
    drop(probe);
    for path in [
        config.game.join("war3.exe"),
        config.archives.join("clients/player-0/war3.exe"),
        config.archives.join("clients/player-1/war3.exe"),
    ] {
        if path.exists() {
            fs::OpenOptions::new()
                .read(true)
                .write(true)
                .open(&path)
                .with_context(|| format!("请关闭旧测试客户端：{}", path.display()))?;
        }
    }
    for name in GAME_FILES {
        copy(&config.source_game.join(name), &config.game.join(name))?;
    }
    let has_previous = if config.archives.exists() {
        fs::read_dir(&config.archives)?
            .filter_map(|e| e.ok())
            .any(|entry| entry.file_name().to_string_lossy().ends_with(".archive"))
    } else {
        false
    };
    let legacy = config.game.join("dz_w3_plugin.ini");
    let inis = archives::prepare(
        &config.archives.join("clients"),
        has_previous.then_some(legacy.as_path()),
    )?;
    copy(&config.map, &config.game.join("Maps/Test/Project.w3x"))?;
    let folder = root.join("build/game");
    let staged = folder.join("editor");
    let plugins = staged.join("plugin/warcraft3");
    for entry in fs::read_dir(config.editor.join("plugin/warcraft3"))? {
        let entry = entry?;
        if entry
            .path()
            .extension()
            .is_some_and(|e| e.eq_ignore_ascii_case("dll"))
        {
            copy(&entry.path(), &plugins.join(entry.file_name()))?;
        }
    }
    copy(
        &root.join("build/Release/zz_shared_quicktest.dll"),
        &plugins.join("zz_shared_quicktest.dll"),
    )?;
    let mut ini = Ini::parse_text(
        fs::read_to_string(config.editor.join("plugin/warcraft3/config.cfg"))?
            .trim_start_matches('\u{feff}'),
    )?;
    let enable = ini.section_mut("Enable");
    enable.retain(|key, _| !key.eq_ignore_ascii_case("zz_shared_quicktest.dll"));
    enable.insert("zz_shared_quicktest.dll".into(), "1".into());
    archives::atomic_write(&plugins.join("config.cfg"), ini.text().as_bytes())?;
    copy(
        &config.editor.join("bin/EverConfig.cfg"),
        &staged.join("bin/EverConfig.cfg"),
    )?;
    copy(
        &config.editor.join("plugin/warcraft3/yd_loader.dll"),
        &folder.join("yd_loader.dll"),
    )?;
    Ok((inis, staged))
}

fn lua(value: impl AsRef<str>) -> String {
    // Lua 5.1 支持十进制字节转义，不使用 JSON 的 \u 转义。
    let mut result = String::from("\"");
    for byte in value.as_ref().as_bytes() {
        match byte {
            b'"' => result.push_str("\\\""),
            b'\\' => result.push_str("\\\\"),
            0..=31 | 127 => result.push_str(&format!("\\{byte:03}")),
            _ => result.push(*byte as char),
        }
    }
    // 上面按字节构建仅用于 ASCII；非 ASCII 路径通过 UTF-8 字节十进制形式传给 Lua。
    if !value.as_ref().is_ascii() {
        result = String::from("\"");
        for byte in value.as_ref().as_bytes() {
            result.push_str(&format!("\\{byte:03}"));
        }
    }
    result.push('"');
    result
}

fn install_entry(root: &Path, config: &Config, room: &Room) -> Result<()> {
    let folder = root.join("build/game");
    let players = room
        .profiles
        .iter()
        .map(|profile| {
            format!(
                "[{}]={{port={},token={}}}",
                profile.player,
                profile.port,
                lua(&profile.token)
            )
        })
        .collect::<Vec<_>>()
        .join(",");
    let source = config.local_entry.parent().context("无客户端脚本目录")?;
    let paths = [
        root.join("map/?.lua"),
        source.join("?.lua"),
        source.join("?/init.lua"),
    ]
    .map(|p| p.to_string_lossy().replace('\\', "/"))
    .join(";");
    let entry=format!("package.path = {} .. ';' .. package.path\nrequire('legacy_mls').install({{room={},relay=0,sync_chunk=350,sync_immediate=true,logs={},dll={},players={{{}}}}})\n{}",lua(paths),lua(&room.profiles[0].room),lua(folder.to_string_lossy()),lua(root.join("build/Release/shared_room_native.dll").to_string_lossy()),players,fs::read_to_string(&config.local_entry)?);
    archives::atomic_write(
        &config
            .game
            .join(config.local_entry.file_name().context("无入口文件名")?),
        entry.as_bytes(),
    )?;
    for player in 0..2 {
        fs::write(folder.join(format!("mls-player-{player}.log")), "")?;
    }
    Ok(())
}

fn windows_argument(value: &str) -> String {
    if !value.is_empty() && !value.contains([' ', '\t', '"']) {
        return value.into();
    }
    let mut result = String::from("\"");
    let mut slashes = 0;
    for c in value.chars() {
        if c == '\\' {
            slashes += 1;
            continue;
        }
        if c == '"' {
            result.push_str(&"\\".repeat(slashes * 2 + 1));
        } else {
            result.push_str(&"\\".repeat(slashes));
        }
        slashes = 0;
        result.push(c);
    }
    result.push_str(&"\\".repeat(slashes * 2));
    result.push('"');
    result
}
async fn launch_client(
    root: &Path,
    config: &Config,
    staged: &Path,
    ini: &Path,
    room: &str,
    player: usize,
) -> Result<GameMonitor> {
    let folder = root.join("build/game");
    let log = folder.join(format!("quicktest-{player}.log"));
    fs::write(&log, "")?;
    let mut args = vec![
        config.game.join("war3.exe").to_string_lossy().into_owned(),
        "-window".into(),
        "-opengl".into(),
        "-kkwe".into(),
        staged.to_string_lossy().into_owned(),
    ];
    if player == 0 {
        args.extend(["-host".into(), "2".into(), "Maps\\Test\\Project.w3x".into()]);
    } else {
        args.push("-join".into());
    }
    args.extend([
        "--qt-room".into(),
        room.into(),
        "--qt-player".into(),
        format!("SRPlayer{player}"),
        "--qt-log".into(),
        log.to_string_lossy().into_owned(),
        "--qt-ini".into(),
        ini.to_string_lossy().into_owned(),
    ]);
    let command = args
        .iter()
        .map(|a| windows_argument(a))
        .collect::<Vec<_>>()
        .join(" ");
    let fields = [
        ("editor", config.editor.to_string_lossy().into_owned()),
        ("game", config.game.to_string_lossy().into_owned()),
        ("files", folder.to_string_lossy().into_owned()),
        ("command", command.clone()),
    ];
    let table = format!(
        "return {{{}}}\n",
        fields
            .iter()
            .map(|(key, value)| format!("{key}={}", lua(value)))
            .collect::<Vec<_>>()
            .join(",")
    );
    archives::atomic_write(&folder.join("launch_config.lua"), table.as_bytes())?;
    archives::atomic_write(
        &folder.join(format!("launch-{player}.json")),
        &serde_json::to_vec_pretty(
            &serde_json::json!({"command":command,"game":config.game,"editor":staged}),
        )?,
    )?;
    let paths = std::iter::once(config.editor.join("bin")).chain(
        std::env::split_paths(&std::env::var_os("PATH").unwrap_or_default()).collect::<Vec<_>>(),
    );
    let errors = folder.join(format!("launch-{player}.stderr.log"));
    let mut helper = Command::new(root.join("build/Release/lua_probe.exe"));
    helper
        .arg(config.editor.join("bin/luacore.dll"))
        .arg(root.join("tools/launch_game.lua"))
        .current_dir(root)
        .env("PATH", std::env::join_paths(paths)?)
        .env("SR_MONITOR_GAME", "1")
        .creation_flags(0x08000000)
        .kill_on_drop(false)
        .stdout(Stdio::piped())
        .stderr(fs::File::create(&errors)?);
    GameMonitor::start(&mut helper, errors).await
}

#[derive(Clone, Copy)]
enum QuicktestSignal {
    Ready,
    Started,
    Failed,
}
impl QuicktestSignal {
    fn signaled(self, room: &str) -> Result<bool> {
        let suffix = match self {
            Self::Ready => "ready",
            Self::Started => "started",
            Self::Failed => "failed",
        };
        platform::event_signaled(&format!("Local\\SharedScriptQuicktest-{room}-{suffix}"))
    }
}

async fn wait_host(room: &str, seconds: f64, host: &mut GameMonitor) -> Result<()> {
    let start = Instant::now();
    loop {
        ensure_game_running(host, 0).await?;
        anyhow::ensure!(
            !QuicktestSignal::Failed.signaled(room)?,
            "房主报告启动失败，请检查 quicktest-0.log"
        );
        if QuicktestSignal::Ready.signaled(room)? {
            return Ok(());
        }
        anyhow::ensure!(start.elapsed().as_secs_f64() < seconds, "房主启动超时");
        tokio::time::sleep(Duration::from_millis(50)).await;
    }
}

async fn ensure_game_running(client: &mut GameMonitor, player: usize) -> Result<()> {
    if let Some(code) = client.exit_code().await? {
        let role = if player == 0 { "房主" } else { "加入端" };
        anyhow::bail!("{role}（玩家 {player}，PID {}）在双人开局前已退出，退出码 0x{code:08X}；请检查地图加载及 quicktest-{player}.log，云端读档尚未开始", client.pid);
    }
    Ok(())
}

struct RunLog {
    path: PathBuf,
    sender: mpsc::Sender<String>,
    task: JoinHandle<std::io::Result<()>>,
}
impl RunLog {
    fn start(folder: &Path, output: mpsc::Sender<String>) -> Result<Self> {
        let timestamp = SystemTime::now().duration_since(UNIX_EPOCH)?.as_secs();
        let path = folder.join(format!("cloud-rust-{timestamp}.log"));
        let mut file = fs::File::create(&path)?;
        let (sender, mut receive) = mpsc::channel::<String>(2048);
        let task = tokio::spawn(async move {
            while let Some(line) = receive.recv().await {
                writeln!(file, "{line}")?;
                file.flush()?;
                let _ = output.try_send(line);
            }
            Ok(())
        });
        Ok(Self { path, sender, task })
    }
    async fn finish(self) -> Result<()> {
        drop(self.sender);
        self.task.await??;
        Ok(())
    }
}

#[derive(Clone, Copy, PartialEq, Debug, serde::Serialize)]
#[serde(rename_all = "snake_case")]
enum LaunchPhase {
    WaitingForLan,
    LanStarted,
}
impl LaunchPhase {
    fn observe(&mut self, started: bool) {
        if started {
            *self = Self::LanStarted;
        }
    }
}

// 这一轮客户端监测与启动阶段由同一所有者维护；服务停止后游戏继续运行。
struct GameSession<'a> {
    root: &'a Path,
    config: &'a Config,
    room: &'a Room,
    clients: Vec<GameMonitor>,
    phase: LaunchPhase,
}
impl<'a> GameSession<'a> {
    fn new(root: &'a Path, config: &'a Config, room: &'a Room) -> Self {
        Self {
            root,
            config,
            room,
            clients: Vec::new(),
            phase: LaunchPhase::WaitingForLan,
        }
    }

    async fn start(
        &mut self,
        inis: &[PathBuf; 2],
        staged: &Path,
        stop: &mut watch::Receiver<bool>,
        logs: &mpsc::Sender<String>,
    ) -> Result<()> {
        install_entry(self.root, self.config, self.room)?;
        let room_id = &self.room.profiles[0].room;
        for (player, ini) in inis.iter().enumerate() {
            if *stop.borrow() {
                return Ok(());
            }
            if player == 1 {
                tokio::select! {
                    result = wait_host(room_id, self.config.startup_timeout, &mut self.clients[0]) => result?,
                    _ = stop.changed() => return Ok(()),
                }
            }
            if *stop.borrow() {
                return Ok(());
            }
            let client =
                launch_client(self.root, self.config, staged, ini, room_id, player).await?;
            let pid = client.pid;
            self.clients.push(client);
            logs.send(format!(
                "[启动] 玩家 {player} PID {pid}；INI {}",
                ini.display()
            ))
            .await?;
        }
        Ok(())
    }

    async fn monitor(
        &mut self,
        stop: &mut watch::Receiver<bool>,
        logs: &mpsc::Sender<String>,
    ) -> Result<()> {
        let folder = self.root.join("build/game");
        let room_id = &self.room.profiles[0].room;
        let start = Instant::now();
        let mut tick = tokio::time::interval(Duration::from_secs(1));
        let mut previous = [String::new(), String::new()];
        let mut report = Instant::now();
        while !*stop.borrow() {
            tokio::select! { _ = stop.changed() => break, _ = tick.tick() => {} }
            if *stop.borrow() || start.elapsed().as_secs() >= self.config.seconds {
                break;
            }
            let status = self.room.status.borrow().clone();
            if let Some(reason) = &status.failure {
                anyhow::bail!("{reason}");
            }
            anyhow::ensure!(
                !QuicktestSignal::Failed.signaled(room_id)?,
                "房主报告启动失败，请检查 quicktest-0.log"
            );
            self.phase
                .observe(QuicktestSignal::Started.signaled(room_id)?);
            if self.phase == LaunchPhase::WaitingForLan {
                for (player, client) in self.clients.iter_mut().enumerate() {
                    ensure_game_running(client, player).await?;
                }
                anyhow::ensure!(
                    start.elapsed().as_secs_f64() < self.config.startup_timeout,
                    "双人开局超时，请检查 quicktest 日志"
                );
            }
            forward_game_logs(&folder, &mut previous, logs).await?;
            if report.elapsed() >= Duration::from_secs(10) {
                logs.send(format!(
                    "[运行] 在线 {}/2；回包 {}；云端 {}",
                    status.connected,
                    status.delivery,
                    if status.active {
                        "运行中"
                    } else {
                        "等待连接"
                    }
                ))
                .await?;
                report = Instant::now();
            }
        }
        Ok(())
    }

    fn finish(self) -> Vec<u32> {
        self.clients
            .into_iter()
            .map(|client| {
                let pid = client.pid;
                client.detach();
                pid
            })
            .collect()
    }
}

async fn forward_game_logs(
    folder: &Path,
    previous: &mut [String; 2],
    logs: &mpsc::Sender<String>,
) -> Result<()> {
    for (player, old) in previous.iter_mut().enumerate() {
        let bytes = fs::read(folder.join(format!("quicktest-{player}.log")))?;
        let text = String::from_utf8_lossy(&bytes).into_owned();
        if text != *old {
            let new = text.strip_prefix(old.as_str()).unwrap_or(&text);
            logs.send(new.trim_end().into()).await?;
            *old = text;
        }
    }
    Ok(())
}

#[derive(serde::Serialize)]
struct RunReport<'a> {
    runtime: &'static str,
    room: &'a str,
    game_pids: &'a [u32],
    phase: LaunchPhase,
    cloud_callbacks: u64,
    cloud_failure: Option<String>,
    error: Option<String>,
    client_archives: &'a [PathBuf; 2],
    game: &'a Path,
    console_log: &'a Path,
}
impl RunReport<'_> {
    fn save(&self, folder: &Path) -> Result<()> {
        archives::atomic_write(
            &folder.join("project-result-rust.json"),
            &serde_json::to_vec_pretty(self)?,
        )
    }
}

pub async fn run(
    root: PathBuf,
    config: Config,
    mut stop: watch::Receiver<bool>,
    output: mpsc::Sender<String>,
) -> Result<()> {
    let folder = root.join("build/game");
    fs::create_dir_all(&folder)?;
    let _lock = platform::launch_lock(&folder.join(".project-launch.lock"))?;
    let roots = root.clone();
    let settings = config.clone();
    let (inis, staged) = tokio::task::spawn_blocking(move || prepare(&roots, &settings)).await??;
    if *stop.borrow() {
        return Ok(());
    }
    let log = RunLog::start(&folder, output)?;
    // 房间建立失败也收尾日志任务，避免 early return 遗留后台工作。
    let room = match Room::start(
        HostConfig {
            executable: root.join("build/Release/script_host.exe"),
            dll: root.join("scriptrunner.dll"),
            scripts: config.cloud_scripts.clone(),
            archives: config.archives.clone(),
        },
        Some(0),
        log.sender.clone(),
    )
    .await
    {
        Ok(room) => room,
        Err(error) => {
            log.finish().await?;
            return Err(error);
        }
    };
    let room_id = room.profiles[0].room.clone();
    let _ = log
        .sender
        .send(format!(
            "[房间] {room_id}\n[云脚本] {}\n[存档] {}\n[共用魔兽] {}\n[控制台] {}",
            config.cloud_scripts.display(),
            config.archives.display(),
            config.game.display(),
            log.path.display()
        ))
        .await;
    let mut session = GameSession::new(&root, &config, &room);
    let result = async {
        session
            .start(&inis, &staged, &mut stop, &log.sender)
            .await?;
        session.monitor(&mut stop, &log.sender).await
    }
    .await;
    let phase = session.phase;
    let pids = session.finish();
    let status = room.status.borrow().clone();
    room.close().await;
    let save = RunReport {
        runtime: "rust",
        room: &room_id,
        game_pids: &pids,
        phase,
        cloud_callbacks: status.delivery,
        cloud_failure: status.failure,
        error: result.as_ref().err().map(|error| format!("{error:#}")),
        client_archives: &inis,
        game: &config.game,
        console_log: &log.path,
    }
    .save(&folder);
    let _ = log
        .sender
        .send(format!(
            "[停止] {}",
            result
                .as_ref()
                .err()
                .map(|e| format!("{e:#}"))
                .unwrap_or_else(|| "云服务已停止".into())
        ))
        .await;
    let logged = log.finish().await;
    save?;
    logged?;
    result
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn command_quoting_preserves_trailing_slashes() {
        assert_eq!(windows_argument("C:\\two words\\"), "\"C:\\two words\\\\\"");
        assert_eq!(windows_argument("simple"), "simple");
        assert_eq!(windows_argument("a\"b"), "\"a\\\"b\"");
    }
    #[test]
    fn lua_paths_are_byte_exact() {
        assert_eq!(lua("a\\b"), "\"a\\\\b\"");
        assert_eq!(lua("中"), "\"\\228\\184\\173\"");
    }
    #[test]
    fn game_started_phase_does_not_depend_on_log_text_or_signal_lifetime() {
        let mut phase = LaunchPhase::WaitingForLan;
        phase.observe(false);
        assert_eq!(phase, LaunchPhase::WaitingForLan);
        phase.observe(true);
        phase.observe(false);
        assert_eq!(phase, LaunchPhase::LanStarted);
    }
}
