//! Hook `RequestExtraBooleanData(1009)` — the platform's player→cloud upstream.
//!
//! 仙途客户端的 `kkapi.lua:181-185` 用 `japi.RequestExtraBooleanData(1009, who,
//! label, data, false, 0, 0, 0)` 向云脚本发消息。我们替换这个 native，
//! `dataType == 1009` 时把 (label, data) 引流到 pipe；其他 dataType 透传原 native。
//!
//! ABI（参考 `D:\Code\japi\src\mls\mls_module.cpp:65-95`）：
//!   `extern "cdecl" uint32_t RequestExtraBooleanData(
//!         int dataType, jhandle_t whichPlayer,
//!         const char* param1, const char* param2,
//!         int param3, int param4, int param5, int param6)`
//!
//! 注意 `param1`/`param2` 在 hook 上下文里是 **已解析的 C 字符串指针**，不是 jass
//! string handle —— 老 C++ 直接当 `const char*` 用并工作正常，我们照搬。
//!
//! 限长：`ename ≤ 32B`、`evalue ≤ 900B`（平台真实云端限制）。超长 = drop + WRN，
//! 不入 pipe，**不分块**。

#![allow(non_snake_case)]

use std::os::raw::c_char;
use std::sync::atomic::{AtomicU32, Ordering};

use crate::japi;
use crate::log::{log_err, log_inf, log_wrn};
use crate::pipe_client;
use crate::protocol::{EventPayload, Frame};

type OrigReb = unsafe extern "C" fn(
    data_type: i32,
    which_player: u32,
    param1: *const c_char,
    param2: *const c_char,
    param3: i32,
    param4: i32,
    param5: i32,
    param6: i32,
) -> u32;

// async_hook 在 native 表加载完后异步写入这里。AtomicU32 给我们一个稳定的 *mut u32
// （as_ptr()），同时读侧用 atomic load 保证不读到撕裂值。
// **注意**：被 ydbase 用栈外指针写入 → 必须是 'static 存储。
static ORIG_REB: AtomicU32 = AtomicU32::new(0);

const OPCODE_PLAYER_TO_CLOUD: i32 = 1009;
const MAX_ENAME_BYTES: usize = 32;
const MAX_EVALUE_BYTES: usize = 900;

unsafe extern "C" fn Fake_REB(
    data_type: i32,
    which_player: u32,
    param1: *const c_char,
    param2: *const c_char,
    param3: i32,
    param4: i32,
    param5: i32,
    param6: i32,
) -> u32 {
    let result = std::panic::catch_unwind(|| {
        if data_type == OPCODE_PLAYER_TO_CLOUD {
            return route_c2s(which_player, param1, param2);
        }
        let orig_addr = ORIG_REB.load(Ordering::Acquire);
        if orig_addr == 0 {
            log_err("hook_reb: dt!=1009 but ORIG_REB unset, dropping");
            return 0;
        }
        let orig: OrigReb = std::mem::transmute(orig_addr as usize);
        orig(
            data_type, which_player, param1, param2, param3, param4, param5, param6,
        )
    });
    match result {
        Ok(v) => v,
        Err(_) => {
            log_err("hook_reb: PANIC caught in Fake_REB");
            0
        }
    }
}

unsafe fn route_c2s(_which_player: u32, p_label: *const c_char, p_data: *const c_char) -> u32 {
    let label = cstr_to_string(p_label);
    let data = cstr_to_string(p_data);

    if label.len() > MAX_ENAME_BYTES {
        log_wrn(&format!(
            "hook_reb: c2s DROP ename={}B > {}B (prefix={:?})",
            label.len(),
            MAX_ENAME_BYTES,
            label.get(..label.len().min(20)).unwrap_or("")
        ));
        return 0;
    }
    if data.len() > MAX_EVALUE_BYTES {
        log_wrn(&format!(
            "hook_reb: c2s DROP evalue={}B > {}B (ename={:?})",
            data.len(),
            MAX_EVALUE_BYTES,
            label
        ));
        return 0;
    }

    let frame = Frame::Event(EventPayload {
        dir: "c2s".to_string(),
        ename: label,
        evalue: data,
        ts: 0.0,
        player_index: 0, // pipe_client::push_send 用握手时的默认值兜底
        room_id: String::new(),
    });
    pipe_client::push_send(frame);
    1
}

unsafe fn cstr_to_string(p: *const c_char) -> String {
    if p.is_null() {
        return String::new();
    }
    // 注意：这个指针只在当前 native 帧有效，所以我们立刻 copy。
    std::ffi::CStr::from_ptr(p)
        .to_string_lossy()
        .into_owned()
}

// idempotent 守卫：Initialize() 可能被编辑器侧 + war3 侧各调一次；
// 用 ASYNC_HOOK_QUEUED 标记是否已 enqueue，避免重复 enqueue。ORIG_REB 异步
// 才写入，不能用它判 "已安装"——enqueue 后到真正写入之间它还是 0。
use std::sync::atomic::AtomicBool;
static ASYNC_HOOK_QUEUED: AtomicBool = AtomicBool::new(false);

pub fn install() -> bool {
    if ASYNC_HOOK_QUEUED.swap(true, Ordering::AcqRel) {
        log_inf("hook_reb: async_hook already queued, skipping");
        return true;
    }
    // ORIG_REB.as_ptr() 是 'static 存储，async_hook 把原 native 地址异步写到这里。
    let out_ptr: *mut u32 = ORIG_REB.as_ptr();
    let our_addr = Fake_REB as *const () as usize;
    if japi::async_hook("RequestExtraBooleanData", out_ptr, our_addr) {
        log_inf("hook_reb: async_hook queued for RequestExtraBooleanData (orig will be filled in once native table is populated)");
        true
    } else {
        ASYNC_HOOK_QUEUED.store(false, Ordering::Release);
        false
    }
}
