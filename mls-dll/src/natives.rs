//! Registers the JASS native surface that lua can call directly.
//!
//! 全部走 cdecl ABI，全部包 `catch_unwind`——任何一条 native 让 Rust panic 穿过
//! jass 调用栈 = war3 直接崩。
//!
//! 当前注册的 7 个 native：
//!   `MsPump()                 : integer` —— **主线程**入口；drain SYNC_OUT_QUEUE 并
//!                                          对每条 event 调 `DzSyncData("mls", json)`，
//!                                          由 war3 帧同步层广播到所有客户端的业务 lua；
//!                                          返回本次成功发出的条数（drop 不计）
//!   `MsSendMlEvent(pi,en,ev)  : boolean` —— c2s 上行的便捷入口（=RequestExtraBooleanData(1009,…)）
//!   `MsGetRoomStartTs()       : integer` —— 房间元信息原子缓存
//!   `MsGetRoomLoadedTs()      : integer`
//!   `MsGetRoomGameTime()      : integer`
//!   `MsGetRoomPlayerCount()   : integer`
//!   `MsGetRoomModeId()        : integer`
//!
//! 业务接收：在 war3 业务 lua 里
//!   `DzTriggerRegisterSyncEvent(trig, "mls", false)` + `DzGetTriggerSyncData()` 解 JSON。
//!
//! v0.6 之前的 `MlsNextEvent`（lua 拉队列）已删除——帧同步环境下 polling 会 desync。

#![allow(non_snake_case)]

use std::sync::OnceLock;
use std::sync::atomic::{AtomicBool, Ordering};

use crate::dispatch;
use crate::japi;
use crate::log::{log_err, log_inf, log_wrn};
use crate::pipe_client;
use crate::protocol::{EventPayload, Frame};

const MAX_ENAME_BYTES: usize = 32;
const MAX_EVALUE_BYTES: usize = 900;
/// `DzSyncData` 在 KKYDWE 的 sync payload 单帧硬上限 = 1024B（含框架元字段）。
/// JSON 体 ≤880B 给框架字节留 144B 余量。**超限 drop + WRN，不分块**。
const MAX_SYNC_JSON_BYTES: usize = 880;

const DZSYNC_PREFIX: &str = "mls";

// ── DzSyncData 探活（一次性）─────────────────────────────────────────────

static DZSYNC_AVAILABLE: OnceLock<bool> = OnceLock::new();
static DZSYNC_ERR_LOGGED: AtomicBool = AtomicBool::new(false);

fn ensure_dzsync_available() -> bool {
    *DZSYNC_AVAILABLE.get_or_init(|| {
        let ok = japi::japi_func_exists("DzSyncData");
        if !ok {
            log_err(
                "natives: DzSyncData not found in jass japi_function table; \
                 MsPump will abort on every tick until restart",
            );
        } else {
            log_inf("natives: DzSyncData probed OK");
        }
        ok
    })
}

// ── native impls (cdecl, all args u32 per jass ABI) ─────────────────────

unsafe extern "C" fn Native_MsPump() -> u32 {
    catch_or_zero(|| {
        if !ensure_dzsync_available() {
            // 已在探活时打 ERR，这里只在第一次 abort 时再补一句 pump 侧上下文；
            // 后续每 50ms 进来都静默，避免刷屏。
            if !DZSYNC_ERR_LOGGED.swap(true, Ordering::AcqRel) {
                log_err("MsPump: aborting — DzSyncData unavailable (jass bridge not ready?)");
            }
            return 0;
        }

        let items = dispatch::drain_sync_out();
        if items.is_empty() {
            return 0;
        }
        let mut sent: u32 = 0;
        let mut dropped: u32 = 0;
        for p in items {
            let json = build_event_json(&p);
            if json.len() > MAX_SYNC_JSON_BYTES {
                log_wrn(&format!(
                    "MsPump: DROP json {}B > {}B ename={:?}",
                    json.len(),
                    MAX_SYNC_JSON_BYTES,
                    p.ename
                ));
                dropped += 1;
                continue;
            }
            let h_prefix = japi::create_jstring(DZSYNC_PREFIX);
            let h_payload = japi::create_jstring(&json);
            if h_prefix == 0 || h_payload == 0 {
                log_err(&format!(
                    "MsPump: create_jstring returned 0 (prefix={} payload={}); skipping ename={:?}",
                    h_prefix, h_payload, p.ename
                ));
                dropped += 1;
                continue;
            }
            let _ = japi::call_by_name("DzSyncData", &[h_prefix, h_payload]);
            sent += 1;
        }
        if sent > 0 || dropped > 0 {
            log_inf(&format!("MsPump: sent {} drop {}", sent, dropped));
        }
        sent
    })
}

unsafe extern "C" fn Native_MsSendMlEvent(pi_raw: u32, j_ename: u32, j_evalue: u32) -> u32 {
    catch_or_zero(|| {
        let ename = japi::read_jstring(j_ename);
        let evalue = japi::read_jstring(j_evalue);
        if ename.len() > MAX_ENAME_BYTES {
            log_wrn(&format!(
                "MsSendMlEvent: DROP ename {}B > {}B",
                ename.len(),
                MAX_ENAME_BYTES
            ));
            return 0;
        }
        if evalue.len() > MAX_EVALUE_BYTES {
            log_wrn(&format!(
                "MsSendMlEvent: DROP evalue {}B > {}B (ename={:?})",
                evalue.len(),
                MAX_EVALUE_BYTES,
                ename
            ));
            return 0;
        }
        let pi = pi_raw as i32;
        let frame = Frame::Event(EventPayload {
            dir: "c2s".into(),
            ename,
            evalue,
            ts: 0.0,
            player_index: pi,
            room_id: String::new(), // pipe_client::push_send 兜底填握手默认 ROOM_ID
        });
        pipe_client::push_send(frame);
        1
    })
}

unsafe extern "C" fn Native_MsGetRoomStartTs() -> u32 {
    catch_or_zero(|| dispatch::room_start_ts() as u32)
}
unsafe extern "C" fn Native_MsGetRoomLoadedTs() -> u32 {
    catch_or_zero(|| dispatch::room_loaded_ts() as u32)
}
unsafe extern "C" fn Native_MsGetRoomGameTime() -> u32 {
    catch_or_zero(|| dispatch::room_game_time() as u32)
}
unsafe extern "C" fn Native_MsGetRoomPlayerCount() -> u32 {
    catch_or_zero(|| dispatch::room_player_count() as u32)
}
unsafe extern "C" fn Native_MsGetRoomModeId() -> u32 {
    catch_or_zero(|| dispatch::room_mode_id() as u32)
}

// ── helpers ─────────────────────────────────────────────────────────────

fn catch_or_zero<F: FnOnce() -> u32 + std::panic::UnwindSafe>(f: F) -> u32 {
    match std::panic::catch_unwind(f) {
        Ok(v) => v,
        Err(_) => {
            log_err("natives: PANIC caught in native body");
            0
        }
    }
}

fn build_event_json(p: &EventPayload) -> String {
    serde_json::to_string(&serde_json::json!({
        "ename": p.ename,
        "evalue": p.evalue,
        "player_index": p.player_index,
    }))
    .unwrap_or_else(|_| String::from("{}"))
}

// ── registration ────────────────────────────────────────────────────────

static REGISTERED: AtomicBool = AtomicBool::new(false);

pub fn register_all() -> bool {
    if REGISTERED.swap(true, Ordering::AcqRel) {
        log_inf("natives: already registered, skipping");
        return true;
    }

    let entries: &[(&str, &str, *const ())] = &[
        ("MsPump",               "()I",     Native_MsPump               as *const ()),
        ("MsSendMlEvent",        "(ISS)B",  Native_MsSendMlEvent        as *const ()),
        ("MsGetRoomStartTs",     "()I",     Native_MsGetRoomStartTs     as *const ()),
        ("MsGetRoomLoadedTs",    "()I",     Native_MsGetRoomLoadedTs    as *const ()),
        ("MsGetRoomGameTime",    "()I",     Native_MsGetRoomGameTime    as *const ()),
        ("MsGetRoomPlayerCount", "()I",     Native_MsGetRoomPlayerCount as *const ()),
        ("MsGetRoomModeId",      "()I",     Native_MsGetRoomModeId      as *const ()),
    ];
    let mut all_ok = true;
    let mut registered = 0;
    for (name, sig, addr) in entries {
        if japi::add_native(name, sig, *addr as usize) {
            registered += 1;
        } else {
            all_ok = false;
        }
    }
    log_inf(&format!(
        "natives: register_all done, {}/{} ok",
        registered,
        entries.len()
    ));
    all_ok
}
