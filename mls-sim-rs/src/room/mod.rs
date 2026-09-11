pub mod global_archive;
pub mod json_lua;
pub mod lua_api;
mod profiler;

use crate::player::Player;
use crate::simulation::RoomSimulationData;
use global_archive::{GlobalArchiveStore, SharedGlobalArchive};
use mlua::prelude::*;
use serde::Serialize;
use std::collections::{HashMap, VecDeque};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicI32, Ordering};
use std::sync::{Arc, Condvar, Mutex, RwLock};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};
use tokio::sync::{broadcast, mpsc};

const MAX_EVENT_NAME_LEN: usize = 32;
const MAX_EVENT_DATA_LEN: usize = 900;
const MAX_SCRIPT_ARCHIVE_LEN: usize = 1024 * 1024;
const MAX_LOG_LEN: usize = 2000;
const MAX_BUFFER: usize = 500;

pub const ERR_OK: i32 = 0;
pub const ERR_UNKNOWN: i32 = 1;
pub const ERR_ROOM_NOT_EXIST: i32 = 2;
pub const ERR_PLAYER_NOT_EXIST: i32 = 3;
pub const ERR_EVENT_KEY_LEN: i32 = 4;
pub const ERR_EVENT_KEY_INVALID: i32 = 5;
pub const ERR_EVENT_VALUE_LEN: i32 = 6;
pub const ERR_EVENT_VALUE_INVALID: i32 = 7;
pub const ERR_ARCHIVE_KEY_LEN: i32 = 8;
pub const ERR_ARCHIVE_VALUE_LEN: i32 = 9;
pub const ERR_TEXT_TOO_LONG: i32 = 10;
pub const ERR_SCRIPT_ARCHIVE_TOO_LONG: i32 = 11;
pub const ERR_ITEM_NOT_ENOUGH: i32 = 1259;
pub const ERR_ITEM_NOT_FOUND: i32 = 10133;

const EXECUTION_HOOK_INTERVAL: u32 = 10_000;
const EXECUTION_CANCELLED_MESSAGE: &str = "MLS test execution cancelled";
const EXECUTION_DEADLINE_MESSAGE: &str = "MLS test execution deadline exceeded";

#[derive(Debug, Default)]
pub struct RoomExecutionControl {
    deadline: Mutex<Option<Instant>>,
    cancelled: AtomicBool,
}

impl RoomExecutionControl {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn set_deadline(&self, deadline: Instant) {
        *self.deadline.lock().unwrap() = Some(deadline);
    }

    pub fn clear_deadline(&self) {
        *self.deadline.lock().unwrap() = None;
    }

    pub fn cancel(&self) {
        self.cancelled.store(true, Ordering::Relaxed);
    }

    pub fn is_cancelled(&self) -> bool {
        self.cancelled.load(Ordering::Relaxed)
    }

    pub fn is_expired(&self) -> bool {
        self.deadline
            .lock()
            .unwrap()
            .is_some_and(|deadline| Instant::now() >= deadline)
    }

    fn error_message(&self) -> Option<&'static str> {
        if self.is_cancelled() {
            Some(EXECUTION_CANCELLED_MESSAGE)
        } else if self.is_expired() {
            Some(EXECUTION_DEADLINE_MESSAGE)
        } else {
            None
        }
    }
}

#[derive(Debug, Default)]
pub(super) struct TimerThreadControl {
    cancelled: AtomicBool,
    wake_mutex: Mutex<()>,
    wake_condvar: Condvar,
}

impl TimerThreadControl {
    fn cancel(&self) {
        let _guard = self.wake_mutex.lock().unwrap();
        self.cancelled.store(true, Ordering::Release);
        self.wake_condvar.notify_all();
    }

    pub(super) fn notify_waiters(&self) {
        let _guard = self.wake_mutex.lock().unwrap();
        self.wake_condvar.notify_all();
    }

    pub(super) fn wait_timeout(
        &self,
        timeout: Duration,
        local_cancelled: Option<&AtomicBool>,
    ) -> bool {
        let is_cancelled = || {
            self.cancelled.load(Ordering::Acquire)
                || local_cancelled.is_some_and(|flag| flag.load(Ordering::Acquire))
        };
        if is_cancelled() {
            return false;
        }

        let guard = self.wake_mutex.lock().unwrap();
        let (_guard, result) = self
            .wake_condvar
            .wait_timeout_while(guard, timeout, |_| !is_cancelled())
            .unwrap();
        result.timed_out() && !is_cancelled()
    }
}

/// 房间事件循环的唤醒信号：命令入队后立即唤醒空闲中的循环，避免固定轮询间隔带来的延迟。
#[derive(Debug, Default)]
pub struct WakeSignal {
    pending: Mutex<bool>,
    condvar: Condvar,
}

impl WakeSignal {
    pub fn notify(&self) {
        let mut pending = self.pending.lock().unwrap();
        *pending = true;
        self.condvar.notify_all();
    }

    /// 等待唤醒或超时。调用前已有尚未消费的唤醒时立即返回，因此通知不会在两次检查之间丢失。
    pub fn wait(&self, timeout: Duration) {
        let guard = self.pending.lock().unwrap();
        let (mut guard, _) = self
            .condvar
            .wait_timeout_while(guard, timeout, |pending| !*pending)
            .unwrap();
        *guard = false;
    }
}

/// 带唤醒的房间命令发送端。所有发往房间线程的命令都经由此处，保证事件循环被立即唤醒。
#[derive(Clone)]
pub struct CommandSender {
    tx: mpsc::UnboundedSender<RoomCommand>,
    wake: Arc<WakeSignal>,
}

impl CommandSender {
    pub fn new(tx: mpsc::UnboundedSender<RoomCommand>) -> Self {
        Self {
            tx,
            wake: Arc::new(WakeSignal::default()),
        }
    }

    pub fn send(&self, command: RoomCommand) -> Result<(), mpsc::error::SendError<RoomCommand>> {
        let result = self.tx.send(command);
        self.wake.notify();
        result
    }

    /// 房间线程在命令队列为空时调用：阻塞到有新命令入队或超时。
    pub fn wait_for_command(&self, timeout: Duration) {
        self.wake.wait(timeout);
    }
}

struct RoomThreadLifecycle {
    running: Arc<AtomicBool>,
    timer_control: Arc<TimerThreadControl>,
}

impl Drop for RoomThreadLifecycle {
    fn drop(&mut self) {
        self.running.store(false, Ordering::Release);
        self.timer_control.cancel();
    }
}

#[derive(Clone, Debug)]
pub struct RoomLaunchOptions {
    pub bootstrap_files: Vec<PathBuf>,
    pub execution_control: Option<Arc<RoomExecutionControl>>,
    pub archive_guard: Option<Arc<tempfile::TempDir>>,
    /// 每次 MsSaveScriptArchive / MsSet*Archive、玩家退出与房间关闭时是否把存档同步写入 archive_dir。
    /// GUI 与桥接房间依赖磁盘存档跨房间重启，保持 true；测试房间只读内存中的存档，
    /// 关闭 false 可免去每个用例数十次同步小文件写入，也让房间关闭不再经过磁盘。
    pub persist_archives_to_disk: bool,
    /// 房间要接入的全局存档。为 None 且 `simulation.global_archive_api` 为真时，
    /// 由 RoomManager 按 script_dir 分配共享实例；最终仍为 None 时不安装 MsGA* / MsBroadcast，
    /// 即模拟"旧平台"，云脚本探测到全局函数缺失后应自行降级。
    pub global_archive: Option<SharedGlobalArchive>,
}

impl Default for RoomLaunchOptions {
    fn default() -> Self {
        Self {
            bootstrap_files: Vec::new(),
            execution_control: None,
            archive_guard: None,
            persist_archives_to_disk: true,
            global_archive: None,
        }
    }
}

fn now_ts() -> f64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs_f64()
}

fn now_secs() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs() as i64
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub enum RoomStatus {
    Created,
    Running,
    Stopped,
    Error,
}

impl std::fmt::Display for RoomStatus {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            RoomStatus::Created => write!(f, "created"),
            RoomStatus::Running => write!(f, "running"),
            RoomStatus::Stopped => write!(f, "stopped"),
            RoomStatus::Error => write!(f, "error"),
        }
    }
}

#[derive(Debug, Clone, Serialize)]
pub struct LogEntry {
    pub timestamp: f64,
    pub level: String,
    pub source: String,
    pub message: String,
    pub room_id: String,
    pub player_index: i32,
}

#[derive(Debug, Clone, Serialize)]
pub struct OutEvent {
    pub timestamp: f64,
    pub player_index: i32,
    pub ename: String,
    pub evalue: String,
    pub room_id: String,
}

#[derive(Debug)]
struct PendingServerResponse {
    ready_at: Instant,
    player_index: i32,
    ename: String,
    evalue: String,
}

#[derive(Debug, Default)]
pub(super) struct ServerResponseDelivery {
    delay: Duration,
    pending: VecDeque<PendingServerResponse>,
}

impl ServerResponseDelivery {
    fn set_delay(&mut self, delay: Duration) {
        self.delay = delay;
    }

    pub(super) fn schedule(
        &mut self,
        player_index: i32,
        ename: String,
        evalue: String,
    ) -> Option<(i32, String, String)> {
        if self.delay.is_zero() && self.pending.is_empty() {
            return Some((player_index, ename, evalue));
        }

        let now = Instant::now();
        let delayed_until = now
            .checked_add(self.delay)
            .expect("validated server response delay overflowed Instant");
        let ready_at = self
            .pending
            .back()
            .map(|event| event.ready_at.max(delayed_until))
            .unwrap_or(delayed_until);
        self.pending.push_back(PendingServerResponse {
            ready_at,
            player_index,
            ename,
            evalue,
        });
        None
    }

    fn take_due(&mut self, now: Instant) -> Vec<PendingServerResponse> {
        let mut due = Vec::new();
        while self
            .pending
            .front()
            .is_some_and(|event| event.ready_at <= now)
        {
            due.push(self.pending.pop_front().unwrap());
        }
        due
    }

    fn next_wait(&self, now: Instant) -> Option<Duration> {
        self.pending
            .front()
            .map(|event| event.ready_at.saturating_duration_since(now))
    }
}

#[derive(Debug, Clone, Serialize, Default)]
pub struct ProfileNode {
    pub id: String,
    pub name: String,
    pub count: u64,
    pub self_count: u64,
    pub children: Vec<ProfileNode>,
}

#[derive(Debug, Clone, Serialize, Default)]
pub struct ProfileData {
    pub root: ProfileNode,
    pub total_samples: u64,
    pub window: u64,
    pub bucket_count: u64,
    pub running: bool,
}

pub enum RoomCommand {
    DispatchEvent {
        ename: String,
        evalue: String,
        player_index: i32,
    },
    TimerCallback {
        func_key: mlua::RegistryKey,
    },
    TickerFire {
        ticker_id: i32,
    },
    PlayerJoin {
        player_index: i32,
        name: String,
        reason: String,
    },
    PlayerLeave {
        player_index: i32,
        reason: String,
    },
    PlayerExit {
        player_index: i32,
        reason: String,
    },
    Stop {
        reason: String,
    },
    Destroy,
    ProfilerStart {
        hook_count: i32,
        window_seconds: i32,
    },
    ProfilerStop,
    ProfilerReset,
    Barrier {
        ack: std::sync::mpsc::SyncSender<()>,
    },
}

pub fn validate_user_event(ename: &str, evalue: &str) -> i32 {
    if ename.is_empty() || ename.as_bytes().len() > MAX_EVENT_NAME_LEN {
        return ERR_EVENT_KEY_LEN;
    }
    if evalue.as_bytes().len() > MAX_EVENT_DATA_LEN {
        return ERR_EVENT_VALUE_LEN;
    }
    if ename.starts_with('_')
        || !ename
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || b == b':' || b == b'_' || !b.is_ascii())
    {
        return ERR_EVENT_KEY_INVALID;
    }
    ERR_OK
}

pub struct RoomSharedState {
    pub id: String,
    pub status: RoomStatus,
    pub error_message: String,
    pub script_dir: PathBuf,
    pub mode_id: i32,
    pub start_ts: i64,
    pub loaded_ts: i64,
    pub simulation: RoomSimulationData,
    pub players: HashMap<i32, Player>,
    pub out_queues: HashMap<i32, VecDeque<OutEvent>>,
    pub log_buffer: VecDeque<LogEntry>,
    pub event_buffer: VecDeque<OutEvent>,
    pub profile_data: Option<ProfileData>,
    pub profiler_available: bool,
    pub profiler_running: bool,
}

impl RoomSharedState {
    pub fn to_json(&self) -> serde_json::Value {
        let game_time = if self.loaded_ts > 0 {
            now_secs() - self.loaded_ts
        } else {
            0
        };
        let players: serde_json::Map<String, serde_json::Value> = self
            .players
            .iter()
            .map(|(k, v)| (k.to_string(), v.to_json()))
            .collect();
        serde_json::json!({
            "id": self.id,
            "script_dir": self.script_dir.to_string_lossy(),
            "mode_id": self.mode_id,
            "status": self.status.to_string(),
            "error_message": self.error_message,
            "start_ts": self.start_ts,
            "loaded_ts": self.loaded_ts,
            "game_time": game_time,
            "simulation": self.simulation,
            "player_count": self.players.values().filter(|p| p.is_connected).count(),
            "players": players,
        })
    }
}

pub struct Room {
    pub shared: Arc<RwLock<RoomSharedState>>,
    pub command_tx: CommandSender,
    pub log_tx: broadcast::Sender<LogEntry>,
    pub out_event_tx: broadcast::Sender<OutEvent>,
    pub join_handle: Option<std::thread::JoinHandle<()>>,
    pub execution_control: Option<Arc<RoomExecutionControl>>,
    /// 房间接入的全局存档；None 表示该房间以"旧平台"方式运行，没有 MsGA* / MsBroadcast。
    pub global_archive: Option<SharedGlobalArchive>,
    timer_control: Arc<TimerThreadControl>,
    server_response_delivery: Arc<Mutex<ServerResponseDelivery>>,
}

impl Drop for Room {
    fn drop(&mut self) {
        self.destroy();
        let Some(handle) = self.join_handle.take() else {
            return;
        };
        if handle.is_finished() {
            let _ = handle.join();
            return;
        }
        let _ = std::thread::Builder::new()
            .name("mls-room-reaper".to_string())
            .spawn(move || {
                let _ = handle.join();
            });
    }
}

impl Room {
    pub fn send_event(&self, ename: String, evalue: String, player_index: i32) -> i32 {
        let err = validate_user_event(&ename, &evalue);
        if err != ERR_OK {
            return err;
        }
        if player_index >= 0
            && !self
                .shared
                .read()
                .unwrap()
                .players
                .contains_key(&player_index)
        {
            return ERR_PLAYER_NOT_EXIST;
        }
        let _ = self.command_tx.send(RoomCommand::DispatchEvent {
            ename,
            evalue,
            player_index,
        });
        ERR_OK
    }

    pub fn stop(&self, reason: String) {
        let _ = self.command_tx.send(RoomCommand::Stop { reason });
    }

    pub fn destroy(&self) {
        self.timer_control.cancel();
        if let Some(control) = &self.execution_control {
            control.cancel();
        }
        let _ = self.command_tx.send(RoomCommand::Destroy);
    }

    pub fn barrier(&self, timeout: Duration) -> bool {
        let (ack, receiver) = std::sync::mpsc::sync_channel(1);
        if self.command_tx.send(RoomCommand::Barrier { ack }).is_err() {
            return false;
        }
        receiver.recv_timeout(timeout).is_ok()
    }

    pub fn set_server_response_delay(&self, seconds: f64) -> Result<(), String> {
        if !seconds.is_finite() || seconds < 0.0 {
            return Err("server response delay must be a non-negative finite number".into());
        }
        let delay = Duration::try_from_secs_f64(seconds)
            .map_err(|_| "server response delay is too large".to_string())?;
        if Instant::now().checked_add(delay).is_none() {
            return Err("server response delay is too large".into());
        }
        self.server_response_delivery
            .lock()
            .unwrap()
            .set_delay(delay);
        Ok(())
    }

    fn wait_for_shutdown(&mut self, timeout: Duration) -> bool {
        let Some(handle) = self.join_handle.as_ref() else {
            return true;
        };
        let start = Instant::now();
        while !handle.is_finished() {
            if start.elapsed() >= timeout {
                return false;
            }
            std::thread::sleep(Duration::from_millis(10));
        }
        self.join_handle.take().unwrap().join().is_ok()
    }

    pub fn join_player(&self, player_index: i32, name: String, reason: String) -> i32 {
        let _ = self.command_tx.send(RoomCommand::PlayerJoin {
            player_index,
            name,
            reason,
        });
        ERR_OK
    }

    pub fn leave_player(&self, player_index: i32, reason: String) -> i32 {
        if !self
            .shared
            .read()
            .unwrap()
            .players
            .contains_key(&player_index)
        {
            return ERR_PLAYER_NOT_EXIST;
        }
        let _ = self.command_tx.send(RoomCommand::PlayerLeave {
            player_index,
            reason,
        });
        ERR_OK
    }

    pub fn exit_player(&self, player_index: i32, reason: String) -> i32 {
        if !self
            .shared
            .read()
            .unwrap()
            .players
            .contains_key(&player_index)
        {
            return ERR_PLAYER_NOT_EXIST;
        }
        let _ = self.command_tx.send(RoomCommand::PlayerExit {
            player_index,
            reason,
        });
        ERR_OK
    }

    pub fn has_player(&self, player_index: i32) -> bool {
        self.shared
            .read()
            .unwrap()
            .players
            .contains_key(&player_index)
    }

    pub fn profiler_start(&self, hook_count: i32, window_seconds: i32) {
        let _ = self.command_tx.send(RoomCommand::ProfilerStart {
            hook_count,
            window_seconds,
        });
    }

    pub fn profiler_stop(&self) {
        let _ = self.command_tx.send(RoomCommand::ProfilerStop);
    }

    pub fn profiler_reset(&self) {
        let _ = self.command_tx.send(RoomCommand::ProfilerReset);
    }

    pub fn drain_events(&self, player_index: i32) -> Vec<OutEvent> {
        let mut shared = self.shared.write().unwrap();
        if let Some(q) = shared.out_queues.get_mut(&player_index) {
            q.drain(..).collect()
        } else {
            Vec::new()
        }
    }

    pub fn poll_events(&self, player_index: i32) -> Vec<serde_json::Value> {
        self.drain_events(player_index)
            .into_iter()
            .map(|e| {
                serde_json::json!({
                    "timestamp": e.timestamp,
                    "player_index": e.player_index,
                    "ename": e.ename,
                    "evalue": e.evalue,
                    "room_id": e.room_id,
                })
            })
            .collect()
    }
}

pub struct RoomManager {
    pub rooms: HashMap<String, Room>,
    next_id: u32,
    /// 按 script_dir 共享的全局存档：同一 manager 内同一地图的房间读写同一份数据、广播互达。
    /// 测试运行器为每个用例建立独立 manager，用例之间因此天然隔离。
    global_archives: HashMap<PathBuf, SharedGlobalArchive>,
}

impl RoomManager {
    pub fn new() -> Self {
        Self {
            rooms: HashMap::new(),
            next_id: 0,
            global_archives: HashMap::new(),
        }
    }

    /// 取得或创建 script_dir 对应的共享全局存档。
    pub fn global_archive_for_script(&mut self, script_dir: &Path) -> SharedGlobalArchive {
        let key = script_dir
            .canonicalize()
            .unwrap_or_else(|_| script_dir.to_path_buf());
        self.global_archives
            .entry(key)
            .or_insert_with(|| Arc::new(Mutex::new(GlobalArchiveStore::default())))
            .clone()
    }

    /// 房间接入的全局存档；房间不存在或以旧平台方式运行时为 None。
    pub fn global_archive_for_room(&self, room_id: &str) -> Option<SharedGlobalArchive> {
        self.rooms
            .get(room_id)
            .and_then(|room| room.global_archive.clone())
    }

    pub fn create_room(
        &mut self,
        script_dir: PathBuf,
        mode_id: i32,
        players: HashMap<i32, Player>,
        archive_dir: String,
    ) -> String {
        let simulation = RoomSimulationData::default_for_players(&players);
        self.create_room_with_simulation(script_dir, mode_id, players, simulation, archive_dir)
    }

    pub fn create_room_with_simulation(
        &mut self,
        script_dir: PathBuf,
        mode_id: i32,
        players: HashMap<i32, Player>,
        simulation: RoomSimulationData,
        archive_dir: String,
    ) -> String {
        self.create_room_with_options(
            script_dir,
            mode_id,
            players,
            simulation,
            archive_dir,
            RoomLaunchOptions::default(),
        )
    }

    pub fn create_room_with_options(
        &mut self,
        script_dir: PathBuf,
        mode_id: i32,
        players: HashMap<i32, Player>,
        simulation: RoomSimulationData,
        archive_dir: String,
        mut options: RoomLaunchOptions,
    ) -> String {
        self.next_id += 1;
        let id = format!("room-{:03}", self.next_id);
        let (raw_command_tx, command_rx) = mpsc::unbounded_channel();
        let command_tx = CommandSender::new(raw_command_tx);
        let (log_tx, _) = broadcast::channel(256);
        let (out_event_tx, _) = broadcast::channel(256);

        // 全局存档按 script_dir 共享；房间线程结束时退订，广播不再投递给已关闭房间。
        if options.global_archive.is_none() && simulation.global_archive_api {
            options.global_archive = Some(self.global_archive_for_script(&script_dir));
        }
        if let Some(store) = &options.global_archive {
            store
                .lock()
                .unwrap()
                .subscribe(id.clone(), command_tx.clone());
        }
        let global_archive_for_room = options.global_archive.clone();

        let out_queues = players
            .keys()
            .map(|idx| (*idx, VecDeque::with_capacity(MAX_BUFFER)))
            .collect();

        let shared = Arc::new(RwLock::new(RoomSharedState {
            id: id.clone(),
            status: RoomStatus::Created,
            error_message: String::new(),
            script_dir: script_dir.clone(),
            mode_id,
            start_ts: now_secs(),
            loaded_ts: 0,
            simulation,
            players,
            out_queues,
            log_buffer: VecDeque::new(),
            event_buffer: VecDeque::new(),
            profile_data: None,
            profiler_available: false,
            profiler_running: false,
        }));

        let shared_for_room = shared.clone();
        let command_tx_for_room = command_tx.clone();
        let log_tx_for_room = log_tx.clone();
        let out_event_tx_for_room = out_event_tx.clone();
        let execution_control_for_room = options.execution_control.clone();
        let timer_control = Arc::new(TimerThreadControl::default());
        let timer_control_for_room = timer_control.clone();
        let server_response_delivery = Arc::new(Mutex::new(ServerResponseDelivery::default()));
        let server_response_delivery_for_room = server_response_delivery.clone();

        // Spawn room thread
        let room_id = id.clone();
        let handle = std::thread::Builder::new()
            .name(format!("room-{}", id))
            .spawn(move || {
                room_thread(
                    room_id,
                    script_dir,
                    shared,
                    command_tx,
                    command_rx,
                    log_tx,
                    out_event_tx,
                    archive_dir,
                    options,
                    timer_control,
                    server_response_delivery,
                );
            })
            .expect("failed to spawn room thread");

        let room = Room {
            shared: shared_for_room,
            command_tx: command_tx_for_room,
            log_tx: log_tx_for_room,
            out_event_tx: out_event_tx_for_room,
            join_handle: Some(handle),
            execution_control: execution_control_for_room,
            global_archive: global_archive_for_room,
            timer_control: timer_control_for_room,
            server_response_delivery: server_response_delivery_for_room,
        };

        self.rooms.insert(id.clone(), room);
        id
    }

    pub fn get_room(&self, id: &str) -> Option<&Room> {
        self.rooms.get(id)
    }

    pub fn list_rooms(&self) -> Vec<serde_json::Value> {
        self.rooms
            .values()
            .map(|r| r.shared.read().unwrap().to_json())
            .collect()
    }

    pub fn destroy_room(&mut self, id: &str) -> bool {
        if let Some(room) = self.rooms.remove(id) {
            room.destroy();
            true
        } else {
            false
        }
    }

    pub fn destroy_room_and_wait(&mut self, id: &str, timeout: Duration) -> bool {
        let Some(mut room) = self.rooms.remove(id) else {
            return false;
        };
        room.destroy();
        if room.wait_for_shutdown(timeout) {
            true
        } else {
            if room.join_handle.is_some() {
                self.rooms.insert(id.to_string(), room);
            }
            false
        }
    }

    pub fn restart_room(
        &mut self,
        id: &str,
        archive_dir: String,
        reason: String,
    ) -> Option<String> {
        let room = self.rooms.remove(id)?;
        let (script_dir, mode_id, players, simulation) = {
            let shared = room.shared.read().unwrap();
            (
                shared.script_dir.clone(),
                shared.mode_id,
                shared.players.clone(),
                shared.simulation.clone(),
            )
        };
        room.stop(reason);
        Some(self.create_room_with_simulation(
            script_dir,
            mode_id,
            players,
            simulation,
            archive_dir,
        ))
    }

    pub fn shutdown_all(&mut self) {
        for room in self.rooms.values() {
            if room.execution_control.is_some() {
                room.destroy();
            } else {
                let _ = room.command_tx.send(RoomCommand::Stop {
                    reason: "Shutdown".into(),
                });
            }
        }
        for room in self.rooms.values_mut() {
            room.wait_for_shutdown(Duration::from_secs(3));
        }
    }
}

impl Drop for RoomManager {
    fn drop(&mut self) {
        self.shutdown_all();
    }
}

struct EventHandler {
    id: i32,
    func_key: mlua::RegistryKey,
}

fn execute_bootstrap_file(lua: &Lua, script_dir: &Path, bootstrap_file: &Path) -> LuaResult<()> {
    let path = if bootstrap_file.is_absolute() {
        bootstrap_file.to_path_buf()
    } else {
        script_dir.join(bootstrap_file)
    };
    if !path.is_file() {
        return Err(mlua::Error::RuntimeError(format!(
            "bootstrap file not found: {}",
            path.display()
        )));
    }
    let path = path.canonicalize().map_err(|error| {
        mlua::Error::RuntimeError(format!(
            "failed to resolve bootstrap file {}: {}",
            path.display(),
            error
        ))
    })?;
    if !path
        .extension()
        .and_then(|extension| extension.to_str())
        .is_some_and(|extension| extension.eq_ignore_ascii_case("lua"))
    {
        return Err(mlua::Error::RuntimeError(format!(
            "bootstrap file must have a .lua extension: {}",
            path.display()
        )));
    }

    // The simulator's require implementation tracks the loaded file's directory,
    // so bootstrap files can require sibling modules just like regular scripts.
    let module_path = path.with_extension("");
    let require: LuaFunction = lua.globals().get("require")?;
    require.call::<LuaValue>(module_path.to_string_lossy().into_owned())?;
    Ok(())
}

fn room_thread(
    room_id: String,
    script_dir: PathBuf,
    shared: Arc<RwLock<RoomSharedState>>,
    command_tx: CommandSender,
    mut command_rx: mpsc::UnboundedReceiver<RoomCommand>,
    log_tx: broadcast::Sender<LogEntry>,
    out_event_tx: broadcast::Sender<OutEvent>,
    archive_dir: String,
    options: RoomLaunchOptions,
    timer_control: Arc<TimerThreadControl>,
    server_response_delivery: Arc<Mutex<ServerResponseDelivery>>,
) {
    let _archive_guard = options.archive_guard.clone();
    let running = Arc::new(AtomicBool::new(true));
    let next_event_id = Arc::new(AtomicI32::new(1));
    let trans_id_counter = Arc::new(AtomicI32::new(0));
    let log_count = Arc::new(AtomicI32::new(0));
    let log_window_start = Arc::new(Mutex::new(now_ts()));
    let log_fused = Arc::new(AtomicBool::new(false));

    let event_handlers: Arc<Mutex<HashMap<String, Vec<EventHandler>>>> =
        Arc::new(Mutex::new(HashMap::new()));

    let emit_log = {
        let log_tx = log_tx.clone();
        let shared = shared.clone();
        let room_id = room_id.clone();
        let log_count = log_count.clone();
        let log_window_start = log_window_start.clone();
        let log_fused = log_fused.clone();
        move |level: &str, source: &str, message: String| {
            let now = now_ts();
            {
                let mut ws = log_window_start.lock().unwrap();
                if now - *ws > 100.0 {
                    *ws = now;
                    log_count.store(0, Ordering::Relaxed);
                    log_fused.store(false, Ordering::Relaxed);
                }
            }
            if log_fused.load(Ordering::Relaxed) {
                return;
            }
            let count = log_count.fetch_add(1, Ordering::Relaxed);
            let message = if count >= 1000 {
                log_fused.store(true, Ordering::Relaxed);
                "[LOG FUSE] Log rate exceeded 1000/100s".to_string()
            } else {
                message
            };

            let entry = LogEntry {
                timestamp: now,
                level: level.to_string(),
                source: source.to_string(),
                message: message.clone(),
                room_id: room_id.clone(),
                player_index: -1,
            };
            {
                let mut s = shared.write().unwrap();
                s.log_buffer.push_back(entry.clone());
                if s.log_buffer.len() > MAX_BUFFER {
                    s.log_buffer.pop_front();
                }
            }
            let _ = log_tx.send(entry);
            tracing::info!("[{}] [{}] {} {}", room_id, level, source, message);
        }
    };

    let emit_out_event = {
        let out_event_tx = out_event_tx.clone();
        let shared = shared.clone();
        let room_id = room_id.clone();
        move |player_index: i32, ename: String, evalue: String| {
            let ev = OutEvent {
                timestamp: now_ts(),
                player_index,
                ename,
                evalue,
                room_id: room_id.clone(),
            };
            {
                let mut s = shared.write().unwrap();
                s.event_buffer.push_back(ev.clone());
                if s.event_buffer.len() > MAX_BUFFER {
                    s.event_buffer.pop_front();
                }
                if player_index >= 0 {
                    s.out_queues
                        .entry(player_index)
                        .or_insert_with(|| VecDeque::with_capacity(MAX_BUFFER))
                        .push_back(ev.clone());
                } else {
                    let indices: Vec<i32> = s.players.keys().cloned().collect();
                    for idx in indices {
                        s.out_queues
                            .entry(idx)
                            .or_insert_with(|| VecDeque::with_capacity(MAX_BUFFER))
                            .push_back(ev.clone());
                    }
                }
            }
            let _ = out_event_tx.send(ev);
        }
    };

    // Initialize Lua VM (exclude IO; DEBUG is already excluded by ALL_SAFE)
    let lua = Lua::new_with(LuaStdLib::ALL_SAFE ^ LuaStdLib::IO, LuaOptions::default()).unwrap();
    // Declared after Lua so timer cancellation runs before the VM is dropped.
    let _lifecycle = RoomThreadLifecycle {
        running: running.clone(),
        timer_control: timer_control.clone(),
    };
    lua.set_memory_limit(10 * 1024 * 1024).ok();

    if let Some(control) = options.execution_control.clone() {
        lua.set_hook(
            mlua::HookTriggers::new().every_nth_instruction(EXECUTION_HOOK_INTERVAL),
            move |_lua_ctx, _debug| {
                if let Some(message) = control.error_message() {
                    Err(mlua::Error::RuntimeError(message.to_string()))
                } else {
                    Ok(mlua::VmState::Continue)
                }
            },
        );
    }

    // Sandbox: restrict stdlib to match MLS online environment
    {
        let globals = lua.globals();
        let os_table: mlua::Table = globals.get("os").unwrap();
        for name in [
            "execute",
            "exit",
            "getenv",
            "remove",
            "rename",
            "setlocale",
            "tmpname",
        ] {
            os_table.set(name, LuaNil).unwrap();
        }
        globals.set("io", LuaNil).unwrap();
        globals.set("debug", LuaNil).unwrap();
    }

    // Ticker callbacks stored by ID (since RegistryKey is not Clone)
    let ticker_callbacks: Arc<Mutex<HashMap<i32, mlua::RegistryKey>>> =
        Arc::new(Mutex::new(HashMap::new()));
    let next_ticker_id = Arc::new(AtomicI32::new(1));

    if let Err(e) = json_lua::install_json_lib(&lua).and_then(|_| {
        lua_api::install_lua_apis(
            &lua,
            lua_api::InstallLuaApiContext {
                shared: shared.clone(),
                command_tx: command_tx.clone(),
                running: running.clone(),
                timer_control,
                event_handlers: event_handlers.clone(),
                ticker_callbacks: ticker_callbacks.clone(),
                next_event_id: next_event_id.clone(),
                next_ticker_id: next_ticker_id.clone(),
                trans_id_counter: trans_id_counter.clone(),
                emit_log: emit_log.clone(),
                emit_out_event: emit_out_event.clone(),
                server_response_delivery: server_response_delivery.clone(),
                archive_dir: archive_dir.clone(),
                persist_archives_to_disk: options.persist_archives_to_disk,
                script_dir: script_dir.clone(),
                global_archive: options.global_archive.clone(),
            },
        )
    }) {
        emit_log("ERR", "System", format!("Lua API install failed: {}", e));
        let mut s = shared.write().unwrap();
        s.status = RoomStatus::Error;
        s.error_message = e.to_string();
        return;
    }

    // Load and run main.lua
    let main_lua = script_dir.join("main.lua");
    if !main_lua.exists() {
        let msg = format!("main.lua not found in {}", script_dir.display());
        emit_log("ERR", "System", msg.clone());
        let emit_out = emit_out_event.clone();
        emit_out(-1, "_mlroomfail".to_string(), msg.clone());
        let mut s = shared.write().unwrap();
        s.status = RoomStatus::Error;
        s.error_message = msg;
        return;
    }

    let code = match std::fs::read_to_string(&main_lua) {
        Ok(c) => c,
        Err(e) => {
            let msg = format!("Failed to read main.lua: {}", e);
            emit_log("ERR", "System", msg.clone());
            let emit_out = emit_out_event.clone();
            emit_out(-1, "_mlroomfail".to_string(), msg.clone());
            let mut s = shared.write().unwrap();
            s.status = RoomStatus::Error;
            s.error_message = msg;
            return;
        }
    };

    if let Err(e) = lua.load(&code).set_name("@main.lua").exec() {
        let msg = format!("Failed to execute main.lua: {}", e);
        emit_log("ERR", "System", msg.clone());
        let emit_out = emit_out_event.clone();
        emit_out(-1, "_mlroomfail".to_string(), msg.clone());
        let mut s = shared.write().unwrap();
        s.status = RoomStatus::Error;
        s.error_message = msg;
        return;
    }

    for bootstrap_file in &options.bootstrap_files {
        if let Err(e) = execute_bootstrap_file(&lua, &script_dir, bootstrap_file) {
            let msg = format!(
                "Failed to execute bootstrap {}: {}",
                bootstrap_file.display(),
                e
            );
            emit_log("ERR", "System", msg.clone());
            let emit_out = emit_out_event.clone();
            emit_out(-1, "_mlroomfail".to_string(), msg.clone());
            let mut s = shared.write().unwrap();
            s.status = RoomStatus::Error;
            s.error_message = msg;
            return;
        }
    }

    // Room loaded
    {
        let mut s = shared.write().unwrap();
        s.status = RoomStatus::Running;
        s.loaded_ts = now_secs();
    }
    emit_log("INF", "System", "Room started successfully".to_string());

    // Profiler (Rust-native)
    let profiler_state = Arc::new(Mutex::new(profiler::ProfilerState::new()));
    {
        shared.write().unwrap().profiler_available = true;
    }
    let mut last_profile_extract = std::time::Instant::now();

    // Fire _roomloaded
    {
        let player_indices: Vec<i32> = shared.read().unwrap().players.keys().cloned().collect();
        let data = serde_json::json!({"players": player_indices}).to_string();
        dispatch_lua_event(&lua, &event_handlers, "_roomloaded", &data, -1, &emit_log);
    }

    // Event loop
    while running.load(Ordering::Relaxed) {
        let due_responses = server_response_delivery
            .lock()
            .unwrap()
            .take_due(Instant::now());
        for response in due_responses {
            let can_deliver = response.player_index < 0
                || shared
                    .read()
                    .unwrap()
                    .players
                    .contains_key(&response.player_index);
            if can_deliver {
                emit_out_event(response.player_index, response.ename, response.evalue);
            }
        }

        match command_rx.try_recv() {
            Ok(cmd) => match cmd {
                RoomCommand::DispatchEvent {
                    ename,
                    evalue,
                    player_index,
                } => {
                    dispatch_lua_event(
                        &lua,
                        &event_handlers,
                        &ename,
                        &evalue,
                        player_index,
                        &emit_log,
                    );
                }
                RoomCommand::PlayerJoin {
                    player_index,
                    name,
                    reason,
                } => {
                    let should_dispatch = {
                        let mut s = shared.write().unwrap();
                        let existed = s.players.contains_key(&player_index);
                        let player = s
                            .players
                            .entry(player_index)
                            .or_insert_with(|| Player::new(player_index, name));
                        let was_connected = player.is_connected;
                        player.is_connected = true;
                        s.out_queues
                            .entry(player_index)
                            .or_insert_with(|| VecDeque::with_capacity(MAX_BUFFER));
                        !existed || !was_connected
                    };
                    if should_dispatch {
                        let data = serde_json::json!({"reason": reason}).to_string();
                        dispatch_lua_event(
                            &lua,
                            &event_handlers,
                            "_playerjoin",
                            &data,
                            player_index,
                            &emit_log,
                        );
                    }
                }
                RoomCommand::PlayerLeave {
                    player_index,
                    reason,
                } => {
                    let exists = {
                        let mut s = shared.write().unwrap();
                        if let Some(player) = s.players.get_mut(&player_index) {
                            player.is_connected = false;
                            true
                        } else {
                            false
                        }
                    };
                    if exists {
                        let data = serde_json::json!({"reason": reason}).to_string();
                        dispatch_lua_event(
                            &lua,
                            &event_handlers,
                            "_playerleave",
                            &data,
                            player_index,
                            &emit_log,
                        );
                    }
                }
                RoomCommand::PlayerExit {
                    player_index,
                    reason,
                } => {
                    let exists = shared.read().unwrap().players.contains_key(&player_index);
                    if exists {
                        let data = serde_json::json!({"reason": reason}).to_string();
                        dispatch_lua_event(
                            &lua,
                            &event_handlers,
                            "_playerexit",
                            &data,
                            player_index,
                            &emit_log,
                        );
                        if options.persist_archives_to_disk {
                            let s = shared.read().unwrap();
                            let _ = crate::storage::save_room_archives(
                                &archive_dir,
                                &s.script_dir.to_string_lossy(),
                                &s.players,
                            );
                        }
                        let mut s = shared.write().unwrap();
                        s.players.remove(&player_index);
                        s.out_queues.remove(&player_index);
                    }
                }
                RoomCommand::TimerCallback { func_key } => {
                    if let Ok(func) = lua.registry_value::<LuaFunction>(&func_key) {
                        if let Err(e) = func.call::<()>(()) {
                            emit_log("ERR", "Timer", format!("Timer callback error: {}", e));
                        }
                    }
                }
                RoomCommand::TickerFire { ticker_id } => {
                    let func_opt = {
                        let cbs = ticker_callbacks.lock().unwrap();
                        cbs.get(&ticker_id)
                            .and_then(|key| lua.registry_value::<LuaFunction>(key).ok())
                    };
                    if let Some(func) = func_opt {
                        if let Err(e) = func.call::<()>(()) {
                            emit_log("ERR", "Timer", format!("Ticker callback error: {}", e));
                        }
                    }
                }
                RoomCommand::ProfilerStart {
                    hook_count,
                    window_seconds,
                } => {
                    if options.execution_control.is_some() {
                        emit_log(
                            "WRN",
                            "Profiler",
                            "Profiler is unavailable while test execution limits are active"
                                .to_string(),
                        );
                    } else {
                        {
                            let mut state = profiler_state.lock().unwrap();
                            state.configure(window_seconds, 32);
                        }
                        let state_for_hook = profiler_state.clone();
                        lua.set_hook(
                            mlua::HookTriggers::new().every_nth_instruction(hook_count as u32),
                            move |lua_ctx, _debug| {
                                state_for_hook.lock().unwrap().record_sample(lua_ctx);
                                Ok(mlua::VmState::Continue)
                            },
                        );
                        shared.write().unwrap().profiler_running = true;
                    }
                }
                RoomCommand::ProfilerStop => {
                    if options.execution_control.is_none() {
                        lua.remove_hook();
                    }
                    shared.write().unwrap().profiler_running = false;
                }
                RoomCommand::ProfilerReset => {
                    if options.execution_control.is_none() {
                        lua.remove_hook();
                    }
                    profiler_state.lock().unwrap().reset();
                    let mut s = shared.write().unwrap();
                    s.profiler_running = false;
                    s.profile_data = None;
                }
                RoomCommand::Barrier { ack } => {
                    let _ = ack.send(());
                }
                RoomCommand::Stop { reason } => {
                    let data = serde_json::json!({"reason": reason}).to_string();
                    dispatch_lua_event(&lua, &event_handlers, "_roomover", &data, -1, &emit_log);
                    break;
                }
                RoomCommand::Destroy => {
                    break;
                }
            },
            Err(mpsc::error::TryRecvError::Empty) => {
                if last_profile_extract.elapsed() > std::time::Duration::from_millis(500) {
                    let is_running = shared.read().unwrap().profiler_running;
                    if is_running {
                        let mut state = profiler_state.lock().unwrap();
                        let data = state.to_profile_data(true);
                        shared.write().unwrap().profile_data = Some(data);
                    }
                    last_profile_extract = std::time::Instant::now();
                }
                // 空闲时阻塞到下一条命令入队或下一次延迟投递到期；50ms 只是兜底上限，
                // 命令到达会立即唤醒，避免每次请求都要等一个轮询周期。
                let wait_for = server_response_delivery
                    .lock()
                    .unwrap()
                    .next_wait(Instant::now())
                    .unwrap_or_else(|| Duration::from_millis(50))
                    .min(Duration::from_millis(50));
                if !wait_for.is_zero() {
                    command_tx.wait_for_command(wait_for);
                }
            }
            Err(mpsc::error::TryRecvError::Disconnected) => {
                break;
            }
        }
    }

    // Save archives
    if options.persist_archives_to_disk {
        let s = shared.read().unwrap();
        if let Err(e) = crate::storage::save_room_archives(
            &archive_dir,
            &s.script_dir.to_string_lossy(),
            &s.players,
        ) {
            emit_log("ERR", "System", format!("Archive save failed: {}", e));
        }
    }

    // 退订全局存档广播；共享数据本身留在 manager 中供同地图的其他房间继续使用。
    if let Some(store) = &options.global_archive {
        store.lock().unwrap().unsubscribe(&room_id);
    }

    {
        let mut s = shared.write().unwrap();
        if s.status == RoomStatus::Running {
            s.status = RoomStatus::Stopped;
        }
    }
    emit_log("INF", "System", "Room stopped".to_string());
}

fn dispatch_lua_event<F>(
    lua: &Lua,
    event_handlers: &Arc<Mutex<HashMap<String, Vec<EventHandler>>>>,
    ename: &str,
    evalue: &str,
    player_index: i32,
    emit_log: &F,
) where
    F: Fn(&str, &str, String),
{
    // Collect registry keys we need to call, then drop the lock
    let keys: Vec<i32> = {
        let handlers = event_handlers.lock().unwrap();
        match handlers.get(ename) {
            Some(handler_list) => handler_list.iter().map(|h| h.id).collect(),
            None => return,
        }
    };

    for id in keys {
        // Re-acquire lock briefly to get the registry key reference
        let func_result = {
            let handlers = event_handlers.lock().unwrap();
            if let Some(handler_list) = handlers.get(ename) {
                handler_list
                    .iter()
                    .find(|h| h.id == id)
                    .and_then(|h| lua.registry_value::<LuaFunction>(&h.func_key).ok())
            } else {
                None
            }
        };

        if let Some(func) = func_result {
            // RegisterEvent 统一投递四参数；2026-09-12 真机 GA 回包确认首参为注册编号、正文在第三参。
            let call_result =
                func.call::<()>((id, ename.to_string(), evalue.to_string(), player_index));
            if let Err(e) = call_result {
                emit_log(
                    "ERR",
                    "Event",
                    format!("Handler error for '{}': {}", ename, e),
                );
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn registered_events_deliver_id_name_payload_and_player() {
        let lua = Lua::new();
        for (id, name, data, player_index) in [
            (
                4,
                "_msga_reply",
                r#"{"trans_id":1,"op":"MsGAHGetAll","errnu":0,"data":{"value": {}}}"#,
                -1,
            ),
            (
                5,
                "_msbroadcast",
                r#"{"key":"ga:permissions:changed","value":"{}"}"#,
                -1,
            ),
            (6, "ping", "hello", 0),
        ] {
            let callback: LuaFunction = lua
                .load("return function(...) ga_test_args = table.pack(...) end")
                .eval()
                .unwrap();
            let handlers = Arc::new(Mutex::new(HashMap::from([(
                name.to_string(),
                vec![EventHandler {
                    id,
                    func_key: lua.create_registry_value(callback).unwrap(),
                }],
            )])));
            dispatch_lua_event(
                &lua,
                &handlers,
                name,
                data,
                player_index,
                &|_, _, message| {
                    panic!("unexpected dispatch error: {message}");
                },
            );
            let args: LuaTable = lua.globals().get("ga_test_args").unwrap();
            assert_eq!(args.get::<usize>("n").unwrap(), 4);
            assert_eq!(args.get::<i32>(1).unwrap(), id);
            assert_eq!(args.get::<String>(2).unwrap(), name);
            assert_eq!(args.get::<String>(3).unwrap(), data);
            assert_eq!(args.get::<i32>(4).unwrap(), player_index);
        }
    }

    struct TestDirectory(PathBuf);

    impl TestDirectory {
        fn new(name: &str) -> Self {
            static NEXT_ID: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(1);
            let id = NEXT_ID.fetch_add(1, Ordering::Relaxed);
            let path = std::env::temp_dir()
                .join(format!("mls-sim-room-{name}-{}-{id}", std::process::id()));
            std::fs::create_dir_all(&path).unwrap();
            Self(path)
        }

        fn path(&self) -> &Path {
            &self.0
        }
    }

    impl Drop for TestDirectory {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.0);
        }
    }

    fn test_players() -> HashMap<i32, Player> {
        HashMap::from([(0, Player::new(0, "Player_0".to_string()))])
    }

    fn create_test_room(
        directory: &TestDirectory,
        options: RoomLaunchOptions,
    ) -> (RoomManager, String) {
        let players = test_players();
        let simulation = RoomSimulationData::default_for_players(&players);
        let archive_dir = directory.path().join("archives");
        let mut manager = RoomManager::new();
        let room_id = manager.create_room_with_options(
            directory.path().to_path_buf(),
            0,
            players,
            simulation,
            archive_dir.to_string_lossy().into_owned(),
            options,
        );
        (manager, room_id)
    }

    fn wait_for_status(
        manager: &RoomManager,
        room_id: &str,
        expected: RoomStatus,
        timeout: Duration,
    ) -> bool {
        let start = Instant::now();
        while start.elapsed() < timeout {
            let status = manager
                .get_room(room_id)
                .unwrap()
                .shared
                .read()
                .unwrap()
                .status
                .clone();
            if status == expected {
                return true;
            }
            std::thread::sleep(Duration::from_millis(10));
        }
        false
    }

    #[test]
    fn timer_waits_wake_on_room_and_ticker_cancellation() {
        let room_control = Arc::new(TimerThreadControl::default());
        let (room_ready_tx, room_ready_rx) = std::sync::mpsc::sync_channel(1);
        let (room_done_tx, room_done_rx) = std::sync::mpsc::sync_channel(1);
        let room_control_for_thread = room_control.clone();
        let room_waiter = std::thread::spawn(move || {
            room_ready_tx.send(()).unwrap();
            let elapsed = room_control_for_thread.wait_timeout(Duration::from_secs(30), None);
            room_done_tx.send(elapsed).unwrap();
        });
        room_ready_rx.recv().unwrap();
        room_control.cancel();
        assert!(!room_done_rx
            .recv_timeout(Duration::from_secs(1))
            .expect("room cancellation did not wake the timer waiter"));
        room_waiter.join().unwrap();

        let ticker_control = Arc::new(TimerThreadControl::default());
        let ticker_cancelled = Arc::new(AtomicBool::new(false));
        let (ticker_ready_tx, ticker_ready_rx) = std::sync::mpsc::sync_channel(1);
        let (ticker_done_tx, ticker_done_rx) = std::sync::mpsc::sync_channel(1);
        let ticker_control_for_thread = ticker_control.clone();
        let ticker_cancelled_for_thread = ticker_cancelled.clone();
        let ticker_waiter = std::thread::spawn(move || {
            ticker_ready_tx.send(()).unwrap();
            let elapsed = ticker_control_for_thread
                .wait_timeout(Duration::from_secs(30), Some(&ticker_cancelled_for_thread));
            ticker_done_tx.send(elapsed).unwrap();
        });
        ticker_ready_rx.recv().unwrap();
        ticker_cancelled.store(true, Ordering::Release);
        ticker_control.notify_waiters();
        assert!(!ticker_done_rx
            .recv_timeout(Duration::from_secs(1))
            .expect("ticker cancellation did not wake the timer waiter"));
        ticker_waiter.join().unwrap();
    }

    #[test]
    fn shutdown_timeout_keeps_room_and_join_handle_owned() {
        let directory = TestDirectory::new("shutdown-timeout");
        let players = test_players();
        let out_queues = players
            .keys()
            .map(|index| (*index, VecDeque::new()))
            .collect();
        let shared = Arc::new(RwLock::new(RoomSharedState {
            id: "blocked".to_string(),
            status: RoomStatus::Running,
            error_message: String::new(),
            script_dir: directory.path().to_path_buf(),
            mode_id: 0,
            start_ts: now_secs(),
            loaded_ts: now_secs(),
            simulation: RoomSimulationData::default_for_players(&players),
            players,
            out_queues,
            log_buffer: VecDeque::new(),
            event_buffer: VecDeque::new(),
            profile_data: None,
            profiler_available: false,
            profiler_running: false,
        }));
        let (raw_command_tx, command_rx) = mpsc::unbounded_channel();
        let command_tx = CommandSender::new(raw_command_tx);
        let (log_tx, _) = broadcast::channel(1);
        let (out_event_tx, _) = broadcast::channel(1);
        let (release_tx, release_rx) = std::sync::mpsc::sync_channel(1);
        let handle = std::thread::spawn(move || {
            let _command_rx = command_rx;
            release_rx.recv().unwrap();
        });
        let room = Room {
            shared,
            command_tx,
            log_tx,
            out_event_tx,
            join_handle: Some(handle),
            execution_control: None,
            global_archive: None,
            timer_control: Arc::new(TimerThreadControl::default()),
            server_response_delivery: Arc::new(Mutex::new(ServerResponseDelivery::default())),
        };
        let mut manager = RoomManager::new();
        manager.rooms.insert("blocked".to_string(), room);

        assert!(!manager.destroy_room_and_wait("blocked", Duration::ZERO));
        assert!(manager
            .get_room("blocked")
            .and_then(|room| room.join_handle.as_ref())
            .is_some());

        release_tx.send(()).unwrap();
        assert!(manager.destroy_room_and_wait("blocked", Duration::from_secs(1)));
        assert!(!manager.rooms.contains_key("blocked"));
    }

    #[test]
    fn room_thread_keeps_temporary_archive_guard_until_shutdown() {
        let directory = TestDirectory::new("archive-guard");
        std::fs::write(directory.path().join("main.lua"), "-- test room\n").unwrap();
        let archive_guard = Arc::new(tempfile::tempdir().unwrap());
        let archive_path = archive_guard.path().to_path_buf();
        let options = RoomLaunchOptions {
            archive_guard: Some(archive_guard.clone()),
            ..RoomLaunchOptions::default()
        };
        let (mut manager, room_id) = create_test_room(&directory, options);
        assert!(manager
            .get_room(&room_id)
            .unwrap()
            .barrier(Duration::from_secs(2)));

        drop(archive_guard);
        assert!(archive_path.is_dir());
        assert!(manager.destroy_room_and_wait(&room_id, Duration::from_secs(2)));
        assert!(!archive_path.exists());
    }

    #[test]
    fn validate_user_event_accepts_documented_boundaries() {
        assert_eq!(validate_user_event("A123:boss", ""), ERR_OK);
        assert_eq!(validate_user_event("war3_data", ""), ERR_OK);
        assert_eq!(
            validate_user_event(&"a".repeat(32), &"b".repeat(900)),
            ERR_OK
        );
    }

    #[test]
    fn validate_user_event_rejects_invalid_key_and_value() {
        assert_eq!(validate_user_event("", ""), ERR_EVENT_KEY_LEN);
        assert_eq!(validate_user_event(&"a".repeat(33), ""), ERR_EVENT_KEY_LEN);
        assert_eq!(
            validate_user_event("_roomloaded", ""),
            ERR_EVENT_KEY_INVALID
        );
        assert_eq!(validate_user_event("bad-key", ""), ERR_EVENT_KEY_INVALID);
        assert_eq!(
            validate_user_event("ok", &"v".repeat(901)),
            ERR_EVENT_VALUE_LEN
        );
    }

    #[test]
    fn server_response_delay_is_non_blocking_and_preserves_send_order() {
        let directory = TestDirectory::new("response-delay");
        std::fs::write(
            directory.path().join("main.lua"),
            r#"
RegisterEvent("emit", function(_, _, eventData, playerIndex)
    MsSendMlEvent(playerIndex, "delayed", eventData .. ":1")
    MsSendMlEvent(playerIndex, "delayed", eventData .. ":2")
end)
"#,
        )
        .unwrap();
        let (mut manager, room_id) = create_test_room(&directory, RoomLaunchOptions::default());
        let room = manager.get_room(&room_id).unwrap();
        assert!(room.barrier(Duration::from_secs(2)));
        room.set_server_response_delay(0.4).unwrap();

        let handler_start = Instant::now();
        assert_eq!(
            room.send_event("emit".to_string(), "first".to_string(), 0),
            ERR_OK
        );
        assert!(room.barrier(Duration::from_millis(250)));
        assert!(handler_start.elapsed() < Duration::from_millis(300));

        room.set_server_response_delay(0.0).unwrap();
        assert_eq!(
            room.send_event("emit".to_string(), "second".to_string(), 0),
            ERR_OK
        );
        assert!(room.barrier(Duration::from_millis(250)));
        assert!(room.drain_events(0).is_empty());

        let deadline = Instant::now() + Duration::from_secs(1);
        let events = loop {
            let events = room.drain_events(0);
            if !events.is_empty() || Instant::now() >= deadline {
                break events;
            }
            std::thread::sleep(Duration::from_millis(5));
        };
        assert_eq!(
            events
                .iter()
                .map(|event| event.evalue.as_str())
                .collect::<Vec<_>>(),
            ["first:1", "first:2", "second:1", "second:2"]
        );
        assert!(handler_start.elapsed() >= Duration::from_millis(300));
        assert!(manager.destroy_room_and_wait(&room_id, Duration::from_secs(2)));
    }

    #[test]
    fn bootstrap_runs_before_room_loaded_and_can_require_siblings() {
        let directory = TestDirectory::new("bootstrap-order");
        std::fs::write(
            directory.path().join("main.lua"),
            r#"
RegisterEvent("_roomloaded", function()
    MsSendMlEvent(0, "bootstrap_result", BOOTSTRAP_VALUE or "missing")
end)
"#,
        )
        .unwrap();
        std::fs::write(
            directory.path().join("bootstrap.lua"),
            "BOOTSTRAP_VALUE = require('bootstrap_helper')\n",
        )
        .unwrap();
        std::fs::write(
            directory.path().join("bootstrap_helper.lua"),
            "return 'ready'\n",
        )
        .unwrap();

        let options = RoomLaunchOptions {
            bootstrap_files: vec![directory.path().join("bootstrap.lua")],
            execution_control: None,
            archive_guard: None,
            ..RoomLaunchOptions::default()
        };
        let (mut manager, room_id) = create_test_room(&directory, options);
        let room = manager.get_room(&room_id).unwrap();
        assert!(room.barrier(Duration::from_secs(2)));
        let events = room.drain_events(0);
        assert_eq!(events.len(), 1);
        assert_eq!(events[0].ename, "bootstrap_result");
        assert_eq!(events[0].evalue, "ready");
        assert!(manager.destroy_room_and_wait(&room_id, Duration::from_secs(2)));
    }

    #[test]
    fn execution_deadline_interrupts_main_and_bootstrap() {
        for (name, main, bootstrap) in [
            ("main", "while true do end\n", None),
            (
                "bootstrap",
                "-- main completes\n",
                Some("while true do end\n"),
            ),
        ] {
            let directory = TestDirectory::new(name);
            std::fs::write(directory.path().join("main.lua"), main).unwrap();
            let bootstrap_files = if let Some(code) = bootstrap {
                let path = directory.path().join("bootstrap.lua");
                std::fs::write(&path, code).unwrap();
                vec![path]
            } else {
                Vec::new()
            };
            let control = Arc::new(RoomExecutionControl::new());
            control.set_deadline(Instant::now() + Duration::from_millis(100));
            let options = RoomLaunchOptions {
                bootstrap_files,
                execution_control: Some(control),
                archive_guard: None,
                ..RoomLaunchOptions::default()
            };
            let (mut manager, room_id) = create_test_room(&directory, options);

            assert!(wait_for_status(
                &manager,
                &room_id,
                RoomStatus::Error,
                Duration::from_secs(2)
            ));
            let error_message = manager
                .get_room(&room_id)
                .unwrap()
                .shared
                .read()
                .unwrap()
                .error_message
                .clone();
            assert!(error_message.contains(EXECUTION_DEADLINE_MESSAGE));
            assert!(manager.destroy_room_and_wait(&room_id, Duration::from_secs(2)));
        }
    }

    #[test]
    fn execution_deadline_interrupts_timer_callbacks() {
        let directory = TestDirectory::new("timer-deadline");
        std::fs::write(
            directory.path().join("main.lua"),
            "Timer.After(0.01, function() while true do end end)\n",
        )
        .unwrap();
        let control = Arc::new(RoomExecutionControl::new());
        control.set_deadline(Instant::now() + Duration::from_millis(200));
        let options = RoomLaunchOptions {
            bootstrap_files: Vec::new(),
            execution_control: Some(control),
            archive_guard: None,
            ..RoomLaunchOptions::default()
        };
        let (mut manager, room_id) = create_test_room(&directory, options);
        assert!(manager
            .get_room(&room_id)
            .unwrap()
            .barrier(Duration::from_secs(2)));

        let start = Instant::now();
        let saw_timeout = loop {
            let saw_timeout = manager
                .get_room(&room_id)
                .unwrap()
                .shared
                .read()
                .unwrap()
                .log_buffer
                .iter()
                .any(|entry| {
                    entry.source == "Timer" && entry.message.contains(EXECUTION_DEADLINE_MESSAGE)
                });
            if saw_timeout || start.elapsed() >= Duration::from_secs(2) {
                break saw_timeout;
            }
            std::thread::sleep(Duration::from_millis(10));
        };
        assert!(saw_timeout);
        assert!(manager.destroy_room_and_wait(&room_id, Duration::from_secs(2)));
    }

    #[test]
    fn profiler_cannot_replace_test_execution_hook() {
        let directory = TestDirectory::new("profiler-hook");
        std::fs::write(
            directory.path().join("main.lua"),
            r#"
RegisterEvent("hang", function()
    while true do end
end)
"#,
        )
        .unwrap();
        let control = Arc::new(RoomExecutionControl::new());
        control.set_deadline(Instant::now() + Duration::from_secs(5));
        let options = RoomLaunchOptions {
            bootstrap_files: Vec::new(),
            execution_control: Some(control.clone()),
            archive_guard: None,
            ..RoomLaunchOptions::default()
        };
        let (mut manager, room_id) = create_test_room(&directory, options);
        let room = manager.get_room(&room_id).unwrap();
        assert!(room.barrier(Duration::from_secs(2)));
        room.profiler_start(1_000, 1);
        assert!(room.barrier(Duration::from_secs(2)));
        assert_eq!(
            room.send_event("hang".to_string(), String::new(), 0),
            ERR_OK
        );
        std::thread::sleep(Duration::from_millis(100));

        let start = Instant::now();
        assert!(manager.destroy_room_and_wait(&room_id, Duration::from_secs(2)));
        assert!(start.elapsed() < Duration::from_secs(2));
        assert!(control.is_cancelled());
    }
}
