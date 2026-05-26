#!/usr/bin/env python3
"""
mls-sim 命名管道测试客户端

模拟 DLL/客户端那一侧，通过 \\.\pipe\mls-sim-bridge 连接 mls-sim，
完成 Hello 握手后进入 REPL：
    send <ename> <evalue>   上行事件
    ping                    主动发 Ping
    bye [reason]            主动断开
    status                  显示状态
    quit                    退出（不发 Bye）
后台线程持续 print 收到的 s2c 事件 / HelloAck / Pong / Error。

用 stdlib + pywin32：
    pip install pywin32
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import threading
import time
from dataclasses import dataclass
from typing import Optional

import win32file
import win32pipe
import pywintypes
import winerror


PIPE_NAME = r"\\.\pipe\mls-sim-bridge"

# 帧类型
T_HELLO     = 0x01
T_HELLO_ACK = 0x02
T_EVENT     = 0x03
T_BYE       = 0x04
T_PING      = 0x05
T_PONG      = 0x06
T_ERROR     = 0x0F

TYPE_NAME = {
    T_HELLO:     "Hello",
    T_HELLO_ACK: "HelloAck",
    T_EVENT:     "Event",
    T_BYE:       "Bye",
    T_PING:      "Ping",
    T_PONG:      "Pong",
    T_ERROR:     "Error",
}


# ---------- 帧编解码 ----------

def encode_frame(typ: int, payload: dict | None) -> bytes:
    body = b"" if payload is None else json.dumps(payload, ensure_ascii=False).encode("utf-8")
    total_len = 1 + len(body)  # 1 byte type + payload
    return struct.pack("<I", total_len) + bytes([typ]) + body


def read_exact(handle, n: int) -> bytes:
    """重叠 I/O 不用，简化为同步读：win32file.ReadFile 在同步管道句柄上会阻塞到拿到数据。"""
    buf = bytearray()
    while len(buf) < n:
        hr, data = win32file.ReadFile(handle, n - len(buf))
        if hr != 0 and hr != winerror.ERROR_MORE_DATA:
            raise IOError(f"ReadFile hr={hr}")
        if not data:
            raise EOFError("pipe closed")
        buf.extend(data)
    return bytes(buf)


def decode_frame(handle) -> tuple[int, dict | None]:
    """读一帧。返回 (type, payload_dict_or_None)。EOF / 错误抛异常。"""
    hdr = read_exact(handle, 5)
    total_len = struct.unpack_from("<I", hdr, 0)[0]
    typ = hdr[4]
    body_len = total_len - 1
    if body_len > 64 * 1024:
        raise IOError(f"frame body too large: {body_len}")
    body = read_exact(handle, body_len) if body_len > 0 else b""
    if typ in (T_PING, T_PONG) or body_len == 0:
        return typ, None
    try:
        return typ, json.loads(body.decode("utf-8"))
    except json.JSONDecodeError as e:
        return typ, {"__bad_json__": body.decode("utf-8", "replace"), "__err__": str(e)}


# ---------- 状态 ----------

@dataclass
class State:
    handle: object = None
    connected: bool = False         # pipe 是否已 CreateFile 成功
    ack_ok: bool = False            # HelloAck 是否成功
    running: bool = True            # 主循环退出标志
    last_pong: float = 0.0
    ev_count: int = 0


# ---------- 后台读线程 ----------

def reader_loop(state: State):
    while state.running:
        try:
            typ, payload = decode_frame(state.handle)
        except EOFError:
            print("\n[READER] pipe closed by server (EOF)")
            state.connected = False
            state.running = False
            return
        except pywintypes.error as e:
            print(f"\n[READER] pipe error: {e}")
            state.connected = False
            state.running = False
            return
        except Exception as e:
            print(f"\n[READER] decode error: {e}")
            state.connected = False
            state.running = False
            return

        name = TYPE_NAME.get(typ, f"0x{typ:02x}")
        if typ == T_HELLO_ACK:
            ok = bool(payload and payload.get("ok"))
            errnu = payload.get("errnu") if payload else None
            msg = payload.get("message", "") if payload else ""
            state.ack_ok = ok
            print(f"\n[<- HelloAck] ok={ok} errnu={errnu} msg={msg!r}")
        elif typ == T_EVENT:
            state.ev_count += 1
            d   = payload.get("dir") if payload else "?"
            en  = payload.get("ename") if payload else "?"
            ev  = payload.get("evalue") if payload else ""
            pi  = payload.get("player_index") if payload else "?"
            ts  = payload.get("ts") if payload else 0.0
            print(f"\n[<- Event #{state.ev_count}] dir={d} ename={en!r} pi={pi} evalue={ev!r}")
        elif typ == T_PONG:
            state.last_pong = time.monotonic()
            print(f"\n[<- Pong]")
        elif typ == T_ERROR:
            print(f"\n[<- Error] {payload}")
        elif typ == T_BYE:
            print(f"\n[<- Bye] {payload}")
            state.running = False
            return
        else:
            print(f"\n[<- {name}] {payload}")
        # 重新打印 prompt
        print("> ", end="", flush=True)


# ---------- 主循环 ----------

def connect(pipe_name: str) -> object:
    last_err = None
    for attempt in range(5):
        try:
            h = win32file.CreateFile(
                pipe_name,
                win32file.GENERIC_READ | win32file.GENERIC_WRITE,
                0,
                None,
                win32file.OPEN_EXISTING,
                0,  # 同步 I/O，无 FILE_FLAG_OVERLAPPED
                None,
            )
            # 不调 SetNamedPipeHandleState；server 端默认就是 byte mode
            print(f"[CONNECT] CreateFile OK, handle={h}")
            return h
        except pywintypes.error as e:
            last_err = e
            if e.winerror == winerror.ERROR_PIPE_BUSY:
                print(f"[CONNECT] pipe busy, retry {attempt + 1}/5...")
                if not win32pipe.WaitNamedPipe(pipe_name, 2000):
                    print(f"[CONNECT] WaitNamedPipe timed out")
            elif e.winerror == winerror.ERROR_FILE_NOT_FOUND:
                print(f"[CONNECT] pipe not found ({pipe_name}). mls-sim 起没起？")
                time.sleep(1)
            else:
                print(f"[CONNECT] CreateFile failed: {e}")
                time.sleep(1)
    raise last_err


def send_hello(state: State, room_id: str, player_index: int, name: str):
    payload = {
        "room_id": room_id,
        "player_index": player_index,
        "name": name,
        "version": "pipe-test-py/0.1",
    }
    buf = encode_frame(T_HELLO, payload)
    print(f"[-> Hello] {payload}")
    print(f"           wire bytes ({len(buf)} total): {buf.hex()}")
    err_code, n_written = win32file.WriteFile(state.handle, buf)
    print(f"           WriteFile: err={err_code} written={n_written}")
    try:
        win32file.FlushFileBuffers(state.handle)
        print(f"           FlushFileBuffers OK")
    except Exception as e:
        print(f"           FlushFileBuffers err: {e}")


def send_event_up(state: State, ename: str, evalue: str, player_index: int, room_id: str):
    payload = {
        "dir": "c2s",
        "ename": ename,
        "evalue": evalue,
        "ts": 0.0,
        "player_index": player_index,
        "room_id": room_id,
    }
    print(f"[-> Event c2s] ename={ename!r} evalue={evalue!r}")
    win32file.WriteFile(state.handle, encode_frame(T_EVENT, payload))


def send_ping(state: State):
    print("[-> Ping]")
    win32file.WriteFile(state.handle, encode_frame(T_PING, None))


def send_bye(state: State, reason: str):
    print(f"[-> Bye] reason={reason!r}")
    win32file.WriteFile(state.handle, encode_frame(T_BYE, {"reason": reason}))


def repl(state: State, room_id: str, player_index: int):
    HELP = (
        "命令：\n"
        "  send <ename> <evalue>   上行一条 Event\n"
        "  buy                     send buy_tower 1\n"
        "  cheat                   send money_666 1\n"
        "  ping                    主动发 Ping\n"
        "  status                  显示连接状态 & 事件计数\n"
        "  bye [reason]            主动断开\n"
        "  help                    显示此帮助\n"
        "  quit                    立刻退出\n"
    )
    print(HELP)
    while state.running:
        try:
            line = input("> ").strip()
        except EOFError:
            break
        except KeyboardInterrupt:
            print("\nCtrl+C: bye")
            break
        if not line:
            continue
        cmd, _, rest = line.partition(" ")
        cmd = cmd.lower()
        try:
            if cmd in ("q", "quit", "exit"):
                state.running = False
                break
            elif cmd == "help":
                print(HELP)
            elif cmd == "status":
                print(f"  connected={state.connected} ack_ok={state.ack_ok}")
                print(f"  events received={state.ev_count}  last_pong={state.last_pong:.1f}")
            elif cmd == "send":
                en, _, ev = rest.partition(" ")
                if not en:
                    print("usage: send <ename> <evalue>")
                    continue
                send_event_up(state, en, ev, player_index, room_id)
            elif cmd == "buy":
                send_event_up(state, "buy_tower", "1", player_index, room_id)
            elif cmd == "cheat":
                send_event_up(state, "money_666", "1", player_index, room_id)
            elif cmd == "ping":
                send_ping(state)
            elif cmd == "bye":
                send_bye(state, rest if rest else "client quit")
                time.sleep(0.5)
                state.running = False
                break
            else:
                print(f"unknown: {cmd}")
        except pywintypes.error as e:
            print(f"[WRITE] pipe error: {e}")
            state.running = False
            break


def auto_run(state: State, room_id: str, player_index: int):
    """非交互模式：连 + Hello + 几个预设事件 + 等响应 + 退出。"""
    print("[AUTO] waiting for HelloAck ...")
    for _ in range(50):
        if state.ack_ok or not state.running:
            break
        time.sleep(0.1)
    if not state.ack_ok:
        print("[AUTO][FAIL] HelloAck not ok")
        return 1

    print("\n[AUTO] step 1: send buy_tower")
    send_event_up(state, "buy_tower", "1", player_index, room_id)
    time.sleep(1.0)

    print("\n[AUTO] step 2: send money_666")
    send_event_up(state, "money_666", "1", player_index, room_id)
    time.sleep(1.0)

    print("\n[AUTO] step 3: send buy_tower again (should be 2 of 5)")
    send_event_up(state, "buy_tower", "1", player_index, room_id)
    time.sleep(1.0)

    print("\n[AUTO] step 4: ping")
    send_ping(state)
    time.sleep(0.5)

    print("\n[AUTO] step 5: kill_unit 3 (mob round 3)")
    send_event_up(state, "kill_unit", "3", player_index, room_id)
    time.sleep(1.0)

    print(f"\n[AUTO] done. total events received={state.ev_count}")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--room", default="room-001")
    parser.add_argument("--player", type=int, default=0)
    parser.add_argument("--name", default="PyTester")
    parser.add_argument("--pipe", default=PIPE_NAME)
    parser.add_argument("--auto", action="store_true",
                        help="non-interactive smoke test: hello + 几个事件 + 退出")
    args = parser.parse_args()

    # 一些 Windows 控制台编码补丁，避免中文事件挂掉打印
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if hasattr(sys.stderr, "reconfigure"):
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")

    state = State()
    print(f"connecting to {args.pipe} ...")
    try:
        state.handle = connect(args.pipe)
    except pywintypes.error as e:
        print(f"FATAL: connect failed: {e}")
        return 1
    state.connected = True
    print(f"connected. sending Hello FIRST (before starting reader)...")

    # 先发 Hello 再起 reader（消除 reader/write 之间的潜在竞争）
    send_hello(state, args.room, args.player, args.name)

    print(f"starting reader thread...")
    t = threading.Thread(target=reader_loop, args=(state,), daemon=True)
    t.start()

    if args.auto:
        rc = auto_run(state, args.room, args.player)
    else:
        # 等 HelloAck
        for _ in range(50):
            if state.ack_ok or not state.running:
                break
            time.sleep(0.1)
        if not state.ack_ok:
            print("WARN: HelloAck not received or not ok. 继续 REPL，你可以观察 mls-sim 日志。")
        repl(state, args.room, args.player)
        rc = 0

    state.running = False
    try:
        win32file.CloseHandle(state.handle)
    except Exception:
        pass
    print("bye.")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
