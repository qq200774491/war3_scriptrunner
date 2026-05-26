//! S2C event 接收侧的 main-thread 派发面：
//!
//!   pipe worker  ──push_recv──▶  RECV_QUEUE  ──pop_recv──▶  MlsNextEvent native (main thread, lua poll)
//!
//! `push_recv` 在 pipe worker 线程被调，已经做过长度检查；overlength 在这里 drop+log
//! （32/900B 平台真实限制，**不分块**）。
//!
//! 房间元信息（start_ts、loaded_ts 等）用 `AtomicI32` 缓存，对应 native 读 atomic 即可。
//! 目前不写入——TODO 在 push_recv 里识别特殊 ename（`_room_meta_*`）后填充。
//! P0 验证不依赖这些值；先返回 0 是合规默认。

use std::collections::VecDeque;
use std::sync::atomic::{AtomicI32, Ordering};
use std::sync::{Mutex, OnceLock};

use crate::log::log_wrn;
use crate::protocol::EventPayload;

const MAX_ENAME_BYTES: usize = 32;
const MAX_EVALUE_BYTES: usize = 900;

static RECV_QUEUE: OnceLock<Mutex<VecDeque<EventPayload>>> = OnceLock::new();

fn recv_queue() -> &'static Mutex<VecDeque<EventPayload>> {
    RECV_QUEUE.get_or_init(|| Mutex::new(VecDeque::new()))
}

/// 入队 s2c event。任何线程可调（实际只在 pipe worker）。
pub fn push_recv(p: EventPayload) {
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
    if let Ok(mut q) = recv_queue().lock() {
        q.push_back(p);
    }
}

/// 出队一条 event；main thread（Native_MlsNextEvent）调。空队列返回 None。
pub fn pop_recv() -> Option<EventPayload> {
    recv_queue().lock().ok()?.pop_front()
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
