use std::io::Write;

use windows_sys::Win32::System::Diagnostics::Debug::OutputDebugStringA;

pub fn log_inf(msg: &str) { log_with("INF", msg); }
pub fn log_wrn(msg: &str) { log_with("WRN", msg); }
pub fn log_err(msg: &str) { log_with("ERR", msg); }

fn log_with(level: &str, msg: &str) {
    let line = format!("[mls/{}] {}", level, msg);
    let _ = writeln!(std::io::stderr(), "{}", line);
    let mut dbg = Vec::with_capacity(line.len() + 2);
    dbg.extend_from_slice(line.as_bytes());
    dbg.push(b'\n');
    dbg.push(0);
    unsafe { OutputDebugStringA(dbg.as_ptr()); }
}
