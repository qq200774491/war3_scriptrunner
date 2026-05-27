//! S2C event 主线程派发队列：
//!
//!   pipe worker  ──push_sync_out──▶  SYNC_OUT_QUEUE  ──drain_sync_out──▶  Native_MsPump (main thread)
//!
//! v0.6 起 s2c 不再由 lua 拉取，而是 dll 主线程在 `MsPump` native 里 drain
//! 后转调 `DzSyncData("mls", json)`，让 war3 帧同步层把消息**在同一帧**广播
//! 到所有客户端的业务 lua（业务侧用 `DzTriggerRegisterSyncEvent` 接）。
//!
//! `push_sync_out` 在 pipe worker 线程被调用，做 32B ename / 900B evalue 上限
//! 校验（**保持与 mls.exe `validate_user_event` 上限一致**，便于业务源头不动）；
//! pump 出队时再做一次 880B JSON 体二次校验，超限 drop + WRN，**不分块**。

use std::collections::VecDeque;
use std::sync::atomic::{AtomicI32, Ordering};
use std::sync::{Mutex, OnceLock};

use crate::log::log_wrn;
use crate::protocol::EventPayload;

const MAX_ENAME_BYTES: usize = 32;
const MAX_EVALUE_BYTES: usize = 900;

static SYNC_OUT_QUEUE: OnceLock<Mutex<VecDeque<EventPayload>>> = OnceLock::new();

fn queue() -> &'static Mutex<VecDeque<EventPayload>> {
    SYNC_OUT_QUEUE.get_or_init(|| Mutex::new(VecDeque::new()))
}

/// 入队一条 s2c event；任何线程可调（实际只在 pipe worker）。
/// 超限直接 drop + log，不分块、不缓存。
pub fn push_sync_out(p: EventPayload) {
    if p.ename.len() > MAX_ENAME_BYTES {
        log_wrn(&format!(
            "dispatch: s2c DROP ename {}B > {}B (prefix={:?})",
            p.ename.len(),
            MAX_ENAME_BYTES,
            p.ename.get(..p.ename.len().min(20)).unwrap_or("")
        ));
        return;
    }
    if p.evalue.len() > MAX_EVALUE_BYTES {
        log_wrn(&format!(
            "dispatch: s2c DROP evalue {}B > {}B (ename={:?})",
            p.evalue.len(),
            MAX_EVALUE_BYTES,
            p.ename
        ));
        return;
    }
    if let Ok(mut q) = queue().lock() {
        q.push_back(p);
    }
}

/// 主线程 pump 入口：一次性把队列搬走，批处理。空队列返回空 Vec。
pub fn drain_sync_out() -> Vec<EventPayload> {
    queue()
        .lock()
        .ok()
        .map(|mut q| q.drain(..).collect())
        .unwrap_or_default()
}

// ── 房间元信息缓存 ──────────────────────────────────────────────────

static ROOM_START_TS: AtomicI32 = AtomicI32::new(0);
static ROOM_LOADED_TS: AtomicI32 = AtomicI32::new(0);
static ROOM_GAME_TIME: AtomicI32 = AtomicI32::new(0);
static ROOM_PLAYER_COUNT: AtomicI32 = AtomicI32::new(0);
static ROOM_MODE_ID: AtomicI32 = AtomicI32::new(0);

pub fn room_start_ts() -> i32 {
    ROOM_START_TS.load(Ordering::Acquire)
}
pub fn room_loaded_ts() -> i32 {
    ROOM_LOADED_TS.load(Ordering::Acquire)
}
pub fn room_game_time() -> i32 {
    ROOM_GAME_TIME.load(Ordering::Acquire)
}
pub fn room_player_count() -> i32 {
    ROOM_PLAYER_COUNT.load(Ordering::Acquire)
}
pub fn room_mode_id() -> i32 {
    ROOM_MODE_ID.load(Ordering::Acquire)
}
