//! Dynamic loader + safe wrappers for `yd_jass_api.dll` and `ydbase.dll`.
//!
//! See `docs/yd_jass_api-abi.md` for the ABI investigation (Track 0 product).
//! Key facts: every jass primitive is **cdecl**; `ydbase.dll` is at
//! `D:\war3Project\KKYDWE\bin\ydbase.dll` (KKYDWE puts it in `bin/`, not in
//! `plugin/warcraft3/`, but it's already loaded into the process by
//! `yd_jass_api.dll` so plain `LoadLibraryA("ydbase.dll")` works — just
//! a refcount bump).
//!
//! We bypass `yd_jass_api!AddJApi` (its arg order is opaque from disassembly)
//! and call `ydbase!japi_add` directly via its mangled name — the mangled
//! name encodes the exact signature so there's no guesswork.

#![allow(non_snake_case)]

use std::os::raw::c_char;
use std::sync::OnceLock;

use windows_sys::Win32::Foundation::HMODULE;
use windows_sys::Win32::System::LibraryLoader::{GetProcAddress, LoadLibraryA};

use crate::log::{log_err, log_inf};

// ── function-pointer types (every one is cdecl = extern "C" on x86 Windows) ──

pub type JapiAdd = unsafe extern "C" fn(
    func_addr: u32,
    name: *const c_char,
    params: *const c_char,
) -> bool;

pub type JapiHook = unsafe extern "C" fn(
    name: *const c_char,
    out_old: *mut u32,
    new_proc: u32,
) -> bool;

// async_hook 与 japi_hook 签名完全相同，区别是 async_hook **把 hook 任务排队**——
// Initialize() 时 jass native 表可能还没 populate（`RequestExtraBooleanData` 这条没注册），
// 同步 japi_hook 就会返回 false；async_hook 等 native 表加载完后再装，并把
// 原 native 地址写入 out_old 指向的存储。**out_old 必须是 static / 全局**，
// 不能是栈变量——异步写入时栈早没了。
pub type AsyncHook = unsafe extern "C" fn(
    name: *const c_char,
    out_old: *mut u32,
    new_proc: u32,
) -> bool;

pub type CreateString = unsafe extern "C" fn(*const c_char) -> u32;
pub type FromString   = unsafe extern "C" fn(u32) -> *const c_char;

pub struct Japi {
    pub japi_add:      JapiAdd,
    pub japi_hook:     JapiHook,
    pub async_hook:    AsyncHook,
    pub create_string: CreateString,
    pub from_string:   FromString,
}

static JAPI: OnceLock<Japi> = OnceLock::new();

pub fn get() -> Option<&'static Japi> {
    JAPI.get()
}

// ── one-shot init: LoadLibrary + GetProcAddress everything we need ──

const SYM_JAPI_ADD:      &[u8] = b"?japi_add@jass@warcraft3@base@@YA_NIPBD0@Z\0";
const SYM_JAPI_HOOK:     &[u8] = b"?japi_hook@jass@warcraft3@base@@YA_NPBDPAII@Z\0";
const SYM_ASYNC_HOOK:    &[u8] = b"?async_hook@jass@warcraft3@base@@YA_NPBDPAII@Z\0";
const SYM_CREATE_STRING: &[u8] = b"?create_string@jass@warcraft3@base@@YAIPBD@Z\0";
const SYM_FROM_STRING:   &[u8] = b"?from_string@jass@warcraft3@base@@YAPBDI@Z\0";

pub fn init() -> bool {
    if JAPI.get().is_some() {
        return true;
    }
    match try_init() {
        Some(japi) => {
            let _ = JAPI.set(japi);
            log_inf("japi: yd_jass_api.dll + ydbase.dll loaded, 4 symbols resolved");
            true
        }
        None => false,
    }
}

fn try_init() -> Option<Japi> {
    let h_yd_jass = unsafe { LoadLibraryA(b"yd_jass_api.dll\0".as_ptr() as _) };
    if h_yd_jass.is_null() {
        log_err("japi: LoadLibrary yd_jass_api.dll failed");
        return None;
    }
    let h_ydbase = unsafe { LoadLibraryA(b"ydbase.dll\0".as_ptr() as _) };
    if h_ydbase.is_null() {
        log_err("japi: LoadLibrary ydbase.dll failed");
        return None;
    }
    let _ = h_yd_jass;

    let resolve = |h: HMODULE, sym: &[u8], label: &str| -> Option<usize> {
        let f = unsafe { GetProcAddress(h, sym.as_ptr() as _) };
        match f {
            Some(p) => Some(p as usize),
            None => {
                log_err(&format!("japi: GetProcAddress {} failed", label));
                None
            }
        }
    };

    let p_japi_add      = resolve(h_ydbase, SYM_JAPI_ADD,      "japi_add")?;
    let p_japi_hook     = resolve(h_ydbase, SYM_JAPI_HOOK,     "japi_hook")?;
    let p_async_hook    = resolve(h_ydbase, SYM_ASYNC_HOOK,    "async_hook")?;
    let p_create_string = resolve(h_ydbase, SYM_CREATE_STRING, "create_string")?;
    let p_from_string   = resolve(h_ydbase, SYM_FROM_STRING,   "from_string")?;

    Some(Japi {
        japi_add:      unsafe { std::mem::transmute::<usize, JapiAdd>(p_japi_add) },
        japi_hook:     unsafe { std::mem::transmute::<usize, JapiHook>(p_japi_hook) },
        async_hook:    unsafe { std::mem::transmute::<usize, AsyncHook>(p_async_hook) },
        create_string: unsafe { std::mem::transmute::<usize, CreateString>(p_create_string) },
        from_string:   unsafe { std::mem::transmute::<usize, FromString>(p_from_string) },
    })
}

// ── ergonomic wrappers used by other modules ────────────────────────────

/// Register a jass native. Returns true on success.
///
/// `params` is the jass-signature mini-string (e.g. `"()S"`, `"(ISS)B"`).
/// See `docs/yd_jass_api-abi.md` for the letter alphabet.
pub fn add_native(name: &str, params: &str, fn_addr: usize) -> bool {
    let Some(japi) = JAPI.get() else { return false; };
    let Ok(cname) = std::ffi::CString::new(name) else { return false; };
    let Ok(cparams) = std::ffi::CString::new(params) else { return false; };
    let ok = unsafe { (japi.japi_add)(fn_addr as u32, cname.as_ptr(), cparams.as_ptr()) };
    if !ok {
        log_err(&format!("japi: japi_add({}, {}) returned false", name, params));
    }
    ok
}

/// Install a hook on an existing native. Returns the original native address
/// on success (caller stores it to pass-through on the non-match path).
///
/// **Requires** the target native to already be in the jass function table.
/// During `Initialize()` that's typically NOT the case—use `async_hook` instead.
pub fn hook(name: &str, new_proc: usize) -> Option<u32> {
    let japi = JAPI.get()?;
    let cname = std::ffi::CString::new(name).ok()?;
    let mut old_proc: u32 = 0;
    let ok = unsafe { (japi.japi_hook)(cname.as_ptr(), &mut old_proc, new_proc as u32) };
    if ok { Some(old_proc) } else {
        log_err(&format!("japi: japi_hook({}) returned false", name));
        None
    }
}

/// Queue a hook installation; ydbase runs it after the jass native table is
/// populated. **`out_old` MUST point at static / 'static-lived storage**—the
/// callback writes there at an unspecified later time, so stack pointers are
/// instant UB.
///
/// Returns `true` if the queue accepted the request (not whether the hook
/// was actually installed—that's still pending when this returns).
pub fn async_hook(name: &str, out_old: *mut u32, new_proc: usize) -> bool {
    let Some(japi) = JAPI.get() else { return false; };
    let Ok(cname) = std::ffi::CString::new(name) else { return false; };
    let ok = unsafe { (japi.async_hook)(cname.as_ptr(), out_old, new_proc as u32) };
    if !ok {
        log_err(&format!("japi: async_hook({}) returned false", name));
    }
    ok
}

/// Copy a jass string handle into a Rust `String`. Returns empty on null/0.
///
/// The pointer returned by `from_string` is only valid for the duration of
/// the current native frame, so we copy immediately.
pub fn read_jstring(j: u32) -> String {
    if j == 0 { return String::new(); }
    let Some(japi) = JAPI.get() else { return String::new(); };
    let p = unsafe { (japi.from_string)(j) };
    if p.is_null() { return String::new(); }
    unsafe { std::ffi::CStr::from_ptr(p).to_string_lossy().into_owned() }
}

/// Allocate a new jass string for `s` and return the handle.
pub fn create_jstring(s: &str) -> u32 {
    let Some(japi) = JAPI.get() else { return 0; };
    let Ok(c) = std::ffi::CString::new(s) else { return 0; };
    unsafe { (japi.create_string)(c.as_ptr()) }
}
