# mls-dll

Rust replacement for the legacy C++ MLS pipe-client DLL
(`D:\Code\japi`). Pure named-pipe IPC against
[`mls-sim`](../mls-sim-rs) — no Game.dll hooking, no inline asm, no
hard-coded VM addresses.

## Output

- crate type: `cdylib`
- rustc output: `target\i686-pc-windows-msvc\release\mls_sim.dll`
- deployment target: `D:\war3Project\KKYDWE\plugin\mls-sim.dll`
  (renamed; KKYDWE's `mls-sim.lua` + `.plcfg` already in place there)

War3 / KKWE are 32-bit processes, so the DLL **must** be built with
`i686-pc-windows-msvc`.

## One-time setup

```powershell
rustup target add i686-pc-windows-msvc
```

## Build + deploy

```powershell
cd D:\code2\mls\mls-dll
.\build.ps1                     # release + copy to KKYDWE plugin dir
.\build.ps1 -NoCopy             # build only
.\build.ps1 -DebugBuild         # debug build with symbols
.\build.ps1 -Deploy <dir>       # custom deploy dir
```

Equivalent raw cargo:

```powershell
cargo build --release --target i686-pc-windows-msvc
Copy-Item target\i686-pc-windows-msvc\release\mls_sim.dll `
    D:\war3Project\KKYDWE\plugin\mls-sim.dll -Force
```

## Runtime behaviour

`DllMain(DLL_PROCESS_ATTACH)`:

1. Install panic hook (logs to stderr + `OutputDebugString`).
2. Acquire a console (3-stage fallback):
   - `GetConsoleWindow()` non-null → inherited
   - `AttachConsole(ATTACH_PARENT_PROCESS)` → parent
   - `AllocConsole()` → spawn new
3. Check auto-test trigger (either is enough):
   - env var `MLS_AUTO_TEST=1`, **or**
   - `mls_auto_test.txt` exists next to the host `.exe`
4. If triggered, spawn worker thread:
   - connect `\\.\pipe\mls-sim-bridge` (with retry/backoff)
   - send `Hello { room_id="room-001", player_index=0, name="AutoTest", version="mls-dll-rs/0.1" }`
   - await `HelloAck { ok: true }`
   - loop forever: every 3 s send `Event { dir:"c2s", ename:"auto_test", evalue:'{"counter":N,"src":"dll-rs"}' }`
   - read concurrent s2c events / Pong / Bye / Error from server
   - on disconnect: reconnect with `1s → 2s → 5s → 10s` backoff

All logs use the `[mls/INF|WRN|ERR]` prefix, written to both stderr and
`OutputDebugStringA` (visible in DebugView / Sysinternals).

The DLL never opens a JASS native, never reads VM addresses, and never
calls into Game.dll. It is safe to load into war3.exe, kkwe.exe, or any
other host.

## Wire protocol

Frame format (matches `mls-sim-rs/src/pipe/protocol.rs`):

```
[u32 LE total_len] [u8 type] [payload (total_len - 1 bytes JSON or empty)]
```

| type | name     | direction | payload                                                  |
|------|----------|-----------|----------------------------------------------------------|
| 0x01 | Hello    | dll→sim   | `{room_id, player_index, name, version}`                 |
| 0x02 | HelloAck | sim→dll   | `{ok, errnu, message}`                                   |
| 0x03 | Event    | both      | `{dir, ename, evalue, ts, player_index, room_id}`        |
| 0x04 | Bye      | both      | `{reason}`                                               |
| 0x05 | Ping     | dll→sim   | empty                                                    |
| 0x06 | Pong     | sim→dll   | empty                                                    |
| 0x0F | Error    | sim→dll   | `{errnu, message}`                                       |

Max body 64 KiB. Server and client share the same enum / JSON layout,
just transport differs (tokio async vs. Win32 OVERLAPPED).

## Triggering an end-to-end smoke test

```powershell
# 1. start mls-sim (if not running)
Start-Process "D:\code2\mls\mls-sim-rs\target\release\mls-sim.exe" `
    -ArgumentList "-s","C:\Users\admin\Desktop\script","--console-notwrte" `
    -RedirectStandardOutput "D:\code2\mls\mls-sim-stdout.log" `
    -RedirectStandardError "D:\code2\mls\mls-sim-stdout.log.err"

# 2. drop the sentinel next to war3.exe
"x" > D:\war3Project\war3\mls_auto_test.txt

# 3. launch war3 via KKWE (right-click in editor)
#    expected: new console pops up next to war3.exe with
#       [mls/INF] dllmain: DLL_PROCESS_ATTACH (host=war3.exe)
#       [mls/INF] auto_test: connected & ack ok
#       [mls/INF] auto_test: send #0 ok=1
#       ...
#    and mls-sim-stdout.log shows
#       pipe: client connected room=room-001 player=0
#       pipe: c2s event room=room-001 player=0 ename="auto_test" ...
```

The Python test client (`D:\code2\mls\tools\pipe_test.py --auto`) keeps
working alongside this DLL — they're independent pipe connections.
