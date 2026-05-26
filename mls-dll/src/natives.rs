//! Registers the JASS native surface that lua can call directly.
//!
//! 全部走 cdecl ABI，全部包 `catch_unwind`——任何一条 native 让 Rust panic 穿过
//! jass 调用栈 = war3 直接崩。
//!
//! 当前注册的 7 个 native：
//!   `MlsNextEvent()           : string`  —— 从 RECV_QUEUE 出队一条 s2c event，JSON 序列化返回；空 → ""
//!   `MsSendMlEvent(pi,en,ev)  : boolean` —— 业务方便的 c2s 入口（=RequestExtraBooleanData(1009,…) 的等价封装）
//!   `MsGetRoomStartTs()       : integer` —— 房间开始时间戳（atomic 缓存）
//!   `MsGetRoomLoadedTs()      : integer`
//!   `MsGetRoomGameTime()      : integer`
//!   `MsGetRoomPlayerCount()   : integer`
//!   `MsGetRoomModeId()        : integer`
//!
//! `RegisterEvent`/`UnregisterEvent` **不在这里注册**——按 plan 由 lua glue 实现
//! （DLL 完全不碰 jass code handle），见 `D:\war3Project\KKYDWE\plugin\mls-sim.lua`。

#![allow(non_snake_case)]

use crate::dispatch;
use crate::japi;
use crate::log::{log_err, log_inf, log_wrn};
use crate::pipe_client;
use crate::protocol::{EventPayload, Frame};

const MAX_ENAME_BYTES: usize = 32;
const MAX_EVALUE_BYTES: usize = 900;

// ── native impls (cdecl, all args u32 per jass ABI) ─────────────────────

unsafe extern "C" fn Native_MlsNextEvent() -> u32 {
    catch_or_zero(|| {
        let p = match dispatch::pop_recv() {
            Some(p) => p,
            None => return japi::create_jstring(""),
        };
        let json = build_event_json(&p);
        japi::create_jstring(&json)
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

use std::sync::atomic::{AtomicBool, Ordering};
static REGISTERED: AtomicBool = AtomicBool::new(false);

pub fn register_all() -> bool {
    if REGISTERED.swap(true, Ordering::AcqRel) {
        log_inf("natives: already registered, skipping");
        return true;
    }

    let entries: &[(&str, &str, *const ())] = &[
        ("MlsNextEvent",         "()S",     Native_MlsNextEvent         as *const ()),
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
