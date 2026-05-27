use std::collections::VecDeque;
use std::ffi::CString;
use std::ptr;
use std::sync::{Mutex, OnceLock};
use std::time::{Duration, Instant};

use windows_sys::Win32::Foundation::{
    CloseHandle, GetLastError, HANDLE, INVALID_HANDLE_VALUE,
};
use windows_sys::Win32::Storage::FileSystem::{
    CreateFileA, ReadFile, WriteFile, FILE_FLAG_OVERLAPPED, OPEN_EXISTING,
};
use windows_sys::Win32::System::Pipes::{
    SetNamedPipeHandleState, WaitNamedPipeA, PIPE_READMODE_BYTE,
};
use windows_sys::Win32::System::IO::{CancelIoEx, GetOverlappedResult, OVERLAPPED};
use windows_sys::Win32::System::Threading::{
    CreateEventA, ResetEvent, WaitForSingleObject,
};

use crate::log::{log_err, log_inf, log_wrn};
use crate::protocol::{decode_frame, encode_frame, EventPayload, Frame, HelloPayload};

// ── public producer queue ───────────────────────────────────────────────
//
// Any thread (notably the jass-main thread, via hook_reb::Fake_REB) can call
// `push_send`. The session_loop drains the queue on each iteration. We keep
// it as Mutex<VecDeque> rather than mpsc because we don't need cross-thread
// blocking wakeup — session_loop polls every 100ms anyway.

static SEND_QUEUE: OnceLock<Mutex<VecDeque<Frame>>> = OnceLock::new();

fn send_queue() -> &'static Mutex<VecDeque<Frame>> {
    SEND_QUEUE.get_or_init(|| Mutex::new(VecDeque::new()))
}

/// Push a frame onto the outbound queue. For `Frame::Event`, empty
/// `room_id` is filled in with the handshake default so producers (hooks,
/// natives) don't need to know it.
pub fn push_send(mut frame: Frame) {
    if let Frame::Event(ref mut p) = frame {
        if p.room_id.is_empty() {
            p.room_id = ROOM_ID.to_string();
        }
    }
    if let Ok(mut q) = send_queue().lock() {
        q.push_back(frame);
    }
}

const PIPE_NAME: &str = r"\\.\pipe\mls-sim-bridge";
const ROOM_ID: &str = "room-001";
const PLAYER_INDEX: i32 = 0;
const CLIENT_NAME: &str = "AutoTest";
const CLIENT_VERSION: &str = "mls-dll-rs/0.1";

const EVENT_INTERVAL: Duration = Duration::from_secs(3);

const GENERIC_READ: u32 = 0x80000000;
const GENERIC_WRITE: u32 = 0x40000000;

const WAIT_OBJECT_0: u32 = 0;
const WAIT_TIMEOUT: u32 = 0x0000_0102;

const ERROR_IO_PENDING: u32 = 997;
const ERROR_OPERATION_ABORTED: u32 = 995;
const ERROR_PIPE_BUSY: u32 = 231;
const ERROR_FILE_NOT_FOUND: u32 = 2;

const BACKOFF_SCHEDULE_SECS: [u64; 4] = [1, 2, 5, 10];

pub fn run_forever() {
    let mut backoff_idx: usize = 0;
    let mut counter: u32 = 0;
    loop {
        match connect_and_handshake() {
            Some(h) => {
                backoff_idx = 0;
                log_inf("auto_test: connected & ack ok");
                counter = session_loop(h, counter);
                unsafe { CloseHandle(h); }
                log_inf("auto_test: session ended, will reconnect");
            }
            None => {
                // already logged inside connect_and_handshake
            }
        }
        let wait = Duration::from_secs(BACKOFF_SCHEDULE_SECS[backoff_idx]);
        log_inf(&format!(
            "auto_test: backoff {} s before reconnect",
            wait.as_secs()
        ));
        std::thread::sleep(wait);
        if backoff_idx + 1 < BACKOFF_SCHEDULE_SECS.len() {
            backoff_idx += 1;
        }
    }
}

fn connect_and_handshake() -> Option<HANDLE> {
    let pipe = match CString::new(PIPE_NAME) {
        Ok(s) => s,
        Err(_) => return None,
    };

    let mut h: HANDLE = INVALID_HANDLE_VALUE;
    for attempt in 0..3 {
        h = unsafe {
            CreateFileA(
                pipe.as_ptr() as _,
                GENERIC_READ | GENERIC_WRITE,
                0,
                ptr::null(),
                OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED,
                ptr::null_mut(),
            )
        };
        if h != INVALID_HANDLE_VALUE {
            break;
        }
        let err = unsafe { GetLastError() };
        match err {
            ERROR_PIPE_BUSY => {
                log_inf(&format!(
                    "auto_test: pipe busy, WaitNamedPipe (attempt {})",
                    attempt + 1
                ));
                unsafe {
                    WaitNamedPipeA(pipe.as_ptr() as _, 2000);
                }
                continue;
            }
            ERROR_FILE_NOT_FOUND => {
                log_inf("auto_test: pipe not found (mls-sim not running?)");
                return None;
            }
            other => {
                log_wrn(&format!(
                    "auto_test: CreateFile failed err={} (attempt {})",
                    other,
                    attempt + 1
                ));
                return None;
            }
        }
    }
    if h == INVALID_HANDLE_VALUE {
        return None;
    }

    // server defaults to byte mode; this is a no-op but mirrors C++ client
    let mode: u32 = PIPE_READMODE_BYTE;
    unsafe {
        SetNamedPipeHandleState(h, &mode, ptr::null(), ptr::null());
    }

    let hello = Frame::Hello(HelloPayload {
        room_id: ROOM_ID.into(),
        player_index: PLAYER_INDEX,
        name: CLIENT_NAME.into(),
        version: CLIENT_VERSION.into(),
    });
    let hello_bytes = match encode_frame(&hello) {
        Ok(b) => b,
        Err(_) => {
            unsafe { CloseHandle(h); }
            return None;
        }
    };
    if !overlapped_write(h, &hello_bytes, 5000) {
        log_wrn("auto_test: write Hello failed");
        unsafe { CloseHandle(h); }
        return None;
    }
    log_inf("auto_test: Hello sent, awaiting HelloAck");

    let mut accum: Vec<u8> = Vec::new();
    match read_one_frame_blocking(h, &mut accum, 10000) {
        Some(Frame::HelloAck(ack)) => {
            if ack.ok {
                log_inf(&format!(
                    "auto_test: HelloAck ok=true errnu={}",
                    ack.errnu
                ));
                if !accum.is_empty() {
                    log_inf(&format!(
                        "auto_test: residual after HelloAck: {} bytes",
                        accum.len()
                    ));
                }
                Some(h)
            } else {
                log_err(&format!(
                    "auto_test: HelloAck NOT ok, errnu={} msg={}",
                    ack.errnu, ack.message
                ));
                unsafe { CloseHandle(h); }
                None
            }
        }
        Some(other) => {
            log_wrn(&format!(
                "auto_test: expected HelloAck, got {:?}",
                frame_label(&other)
            ));
            unsafe { CloseHandle(h); }
            None
        }
        None => {
            log_wrn("auto_test: HelloAck read failed/timeout");
            unsafe { CloseHandle(h); }
            None
        }
    }
}

fn session_loop(h: HANDLE, start_counter: u32) -> u32 {
    let mut counter = start_counter;
    let mut next_event_at = Instant::now() + EVENT_INTERVAL;
    let mut read_buf = vec![0u8; 4096];
    let mut accum: Vec<u8> = Vec::new();

    let read_event = unsafe { CreateEventA(ptr::null(), 1, 0, ptr::null()) };
    if read_event.is_null() {
        log_err("session: CreateEventA failed");
        return counter;
    }

    // 100ms 是 ReadFile 等待的硬上限——保证 send_queue 入队的 c2s 帧最多
    // 等 100ms 就能进入 overlapped_write。心跳仍按 EVENT_INTERVAL 触发。
    const READ_WAIT_CAP_MS: u32 = 100;

    loop {
        let now = Instant::now();

        // ── 1. Drain send queue（来自 hook_reb / 未来的 natives）─────
        let drained: Vec<Frame> = {
            match send_queue().lock() {
                Ok(mut q) => q.drain(..).collect(),
                Err(_) => Vec::new(),
            }
        };
        let mut hard_break = false;
        for f in drained {
            let bytes = match encode_frame(&f) {
                Ok(b) => b,
                Err(e) => {
                    log_err(&format!("session: encode_frame error: {:?}", e));
                    continue;
                }
            };
            let ok = overlapped_write(h, &bytes, 5000);
            if let Frame::Event(ref p) = f {
                log_inf(&format!(
                    "send: c2s ename={:?} evalue_len={} ok={}",
                    p.ename,
                    p.evalue.len(),
                    ok as u32
                ));
            }
            if !ok {
                hard_break = true;
                break;
            }
        }
        if hard_break {
            break;
        }

        // ── 2. Heartbeat（保留作为链路诊断/服务端 keep-alive）────────
        if now >= next_event_at {
            let frame = Frame::Event(EventPayload {
                dir: "c2s".into(),
                ename: "auto_test".into(),
                evalue: format!(r#"{{"counter":{},"src":"dll-rs"}}"#, counter),
                ts: 0.0,
                player_index: PLAYER_INDEX,
                room_id: ROOM_ID.into(),
            });
            let bytes = match encode_frame(&frame) {
                Ok(b) => b,
                Err(_) => break,
            };
            let ok = overlapped_write(h, &bytes, 5000);
            log_inf(&format!("auto_test: send #{} ok={}", counter, ok as u32));
            counter += 1;
            if !ok {
                break;
            }
            next_event_at = Instant::now() + EVENT_INTERVAL;
            continue;
        }

        let timeout_ms = {
            let d = next_event_at.duration_since(now);
            let ms = d.as_millis();
            let ms = if ms > u32::MAX as u128 { u32::MAX } else { ms as u32 };
            ms.min(READ_WAIT_CAP_MS)
        };

        let mut ov: OVERLAPPED = unsafe { std::mem::zeroed() };
        unsafe { ResetEvent(read_event); }
        ov.hEvent = read_event;
        let mut n: u32 = 0;
        let ok = unsafe {
            ReadFile(
                h,
                read_buf.as_mut_ptr() as _,
                read_buf.len() as u32,
                &mut n,
                &mut ov,
            )
        };

        let mut timed_out = false;
        if ok == 0 {
            let err = unsafe { GetLastError() };
            if err == ERROR_IO_PENDING {
                let r = unsafe { WaitForSingleObject(read_event, timeout_ms) };
                if r == WAIT_TIMEOUT {
                    unsafe { CancelIoEx(h, &mut ov); }
                    timed_out = true;
                } else if r != WAIT_OBJECT_0 {
                    log_err(&format!(
                        "session: WaitForSingleObject failed r={} err={}",
                        r,
                        unsafe { GetLastError() }
                    ));
                    break;
                }
            } else {
                log_wrn(&format!("session: ReadFile init err={}", err));
                break;
            }
        }

        let got = unsafe { GetOverlappedResult(h, &ov, &mut n, 1) };
        if got != 0 && n > 0 {
            accum.extend_from_slice(&read_buf[..n as usize]);
        }
        if got == 0 {
            let err = unsafe { GetLastError() };
            if !(timed_out && err == ERROR_OPERATION_ABORTED) {
                log_inf(&format!("session: read end err={}", err));
                break;
            }
        }

        if parse_and_dispatch(&mut accum) {
            break;
        }
    }

    unsafe { CloseHandle(read_event); }
    counter
}

fn parse_and_dispatch(accum: &mut Vec<u8>) -> bool {
    loop {
        if accum.len() < 5 {
            return false;
        }
        let total_len = u32::from_le_bytes([accum[0], accum[1], accum[2], accum[3]]);
        let typ = accum[4];
        if total_len == 0 || total_len as u64 > (crate::protocol::MAX_FRAME_BODY as u64 + 1) {
            log_err(&format!("session: bad frame total_len={}", total_len));
            return true;
        }
        let need = 4 + total_len as usize;
        if accum.len() < need {
            return false;
        }
        let body: Vec<u8> = accum[5..need].to_vec();
        accum.drain(0..need);
        match decode_frame(typ, &body) {
            Ok(Frame::Bye(p)) => {
                log_inf(&format!("session: server Bye reason={:?}", p.reason));
                return true;
            }
            Ok(Frame::Pong) => {}
            Ok(Frame::Event(p)) => {
                log_inf(&format!(
                    "session: recv s2c ename={:?} player={} evalue_len={}",
                    p.ename,
                    p.player_index,
                    p.evalue.len()
                ));
                // 进主线程 pump 队列；dispatch::push_sync_out 内部做 32/900 限长 drop+log
                // （主线程 MsPump 取走后再做一次 880B JSON 体二次校验）
                crate::dispatch::push_sync_out(p);
            }
            Ok(Frame::Error(e)) => {
                log_wrn(&format!(
                    "session: server Error errnu={} msg={}",
                    e.errnu, e.message
                ));
            }
            Ok(_) => {}
            Err(e) => {
                log_err(&format!("session: decode_frame error: {:?}", e));
                return true;
            }
        }
    }
}

fn read_one_frame_blocking(
    h: HANDLE,
    accum: &mut Vec<u8>,
    timeout_ms: u32,
) -> Option<Frame> {
    let deadline = Instant::now() + Duration::from_millis(timeout_ms as u64);
    let event = unsafe { CreateEventA(ptr::null(), 1, 0, ptr::null()) };
    if event.is_null() {
        return None;
    }
    let mut buf = vec![0u8; 1024];

    let result = loop {
        if let Some(frame) = try_parse_one(accum) {
            break Some(frame);
        }
        let now = Instant::now();
        if now >= deadline {
            break None;
        }
        let left = {
            let d = deadline.duration_since(now);
            let ms = d.as_millis();
            if ms > u32::MAX as u128 { u32::MAX } else { ms as u32 }
        };

        let mut ov: OVERLAPPED = unsafe { std::mem::zeroed() };
        unsafe { ResetEvent(event); }
        ov.hEvent = event;
        let mut n: u32 = 0;
        let ok = unsafe {
            ReadFile(
                h,
                buf.as_mut_ptr() as _,
                buf.len() as u32,
                &mut n,
                &mut ov,
            )
        };
        let mut timed_out = false;
        if ok == 0 {
            let err = unsafe { GetLastError() };
            if err == ERROR_IO_PENDING {
                let r = unsafe { WaitForSingleObject(event, left) };
                if r == WAIT_TIMEOUT {
                    unsafe { CancelIoEx(h, &mut ov); }
                    timed_out = true;
                } else if r != WAIT_OBJECT_0 {
                    break None;
                }
            } else {
                break None;
            }
        }
        let got = unsafe { GetOverlappedResult(h, &ov, &mut n, 1) };
        if got != 0 && n > 0 {
            accum.extend_from_slice(&buf[..n as usize]);
        }
        if got == 0 {
            let err = unsafe { GetLastError() };
            if !(timed_out && err == ERROR_OPERATION_ABORTED) {
                break None;
            }
            // pure timeout — loop will check deadline next iter
        }
    };

    unsafe { CloseHandle(event); }
    result
}

fn try_parse_one(accum: &mut Vec<u8>) -> Option<Frame> {
    if accum.len() < 5 {
        return None;
    }
    let total_len = u32::from_le_bytes([accum[0], accum[1], accum[2], accum[3]]);
    let typ = accum[4];
    if total_len == 0 || total_len as u64 > (crate::protocol::MAX_FRAME_BODY as u64 + 1) {
        return None;
    }
    let need = 4 + total_len as usize;
    if accum.len() < need {
        return None;
    }
    let body: Vec<u8> = accum[5..need].to_vec();
    accum.drain(0..need);
    decode_frame(typ, &body).ok()
}

fn overlapped_write(h: HANDLE, bytes: &[u8], timeout_ms: u32) -> bool {
    let event = unsafe { CreateEventA(ptr::null(), 1, 0, ptr::null()) };
    if event.is_null() {
        return false;
    }
    let mut ov: OVERLAPPED = unsafe { std::mem::zeroed() };
    ov.hEvent = event;
    let mut written: u32 = 0;
    let ok = unsafe {
        WriteFile(
            h,
            bytes.as_ptr() as _,
            bytes.len() as u32,
            &mut written,
            &mut ov,
        )
    };
    let mut timed_out = false;
    let mut hard_err = false;
    if ok == 0 {
        let err = unsafe { GetLastError() };
        if err == ERROR_IO_PENDING {
            let r = unsafe { WaitForSingleObject(event, timeout_ms) };
            if r == WAIT_TIMEOUT {
                unsafe { CancelIoEx(h, &mut ov); }
                timed_out = true;
            } else if r != WAIT_OBJECT_0 {
                hard_err = true;
            }
        } else {
            hard_err = true;
        }
    }
    let got = unsafe { GetOverlappedResult(h, &ov, &mut written, 1) };
    unsafe { CloseHandle(event); }
    if hard_err || timed_out {
        return false;
    }
    got != 0 && written as usize == bytes.len()
}

fn frame_label(f: &Frame) -> &'static str {
    match f {
        Frame::Hello(_) => "Hello",
        Frame::HelloAck(_) => "HelloAck",
        Frame::Event(_) => "Event",
        Frame::Bye(_) => "Bye",
        Frame::Ping => "Ping",
        Frame::Pong => "Pong",
        Frame::Error(_) => "Error",
    }
}
