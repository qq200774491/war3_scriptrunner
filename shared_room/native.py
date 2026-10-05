"""原 DLL 的隔离进程控制；内部使用长度前缀协议，不与地图线程共享 Lua 状态。"""
import asyncio
import json
from pathlib import Path
import struct
from .global_archive import GlobalArchive

MAX_FRAME = 1024 * 1024


class HostError(RuntimeError):
    pass


def pack_text(value: str) -> bytes:
    encoded = value.encode('utf-8')
    if b'\0' in encoded or len(encoded) > MAX_FRAME:
        raise ValueError('invalid text')
    return struct.pack('<I', len(encoded)) + encoded


class Frame:
    def __init__(self, data):
        self.data = data
        self.position = 0

    def unpack(self, format):
        size = struct.calcsize(format)
        if self.position + size > len(self.data):
            raise HostError('truncated host frame')
        value, = struct.unpack_from(format, self.data, self.position)
        self.position += size
        return value

    def blob(self):
        size = self.unpack('<I')
        if size > len(self.data) - self.position:
            raise HostError('truncated host text')
        value = self.data[self.position:self.position + size]
        self.position += size
        return value

    def text(self):
        return self.blob().decode('utf-8')

    def finish(self):
        if self.position != len(self.data):
            raise HostError('trailing host bytes')


class NativeHost:
    def __init__(self, executable: Path, dll: Path, scripts: Path, players,
                 on_message=lambda message: None, on_failure=lambda message: None,
                 timeout=5.0, archives=None, on_log=None):
        self.command = [str(executable.resolve()), str(dll.resolve()), str(scripts.resolve()),
                        ','.join(map(str, players))]
        if archives is not None:
            self.command.append(str(Path(archives).resolve()))
        self.on_message = on_message
        self.on_failure = on_failure
        self.timeout = timeout
        self.logs = []
        self.on_log = on_log
        self.process = None
        self.reader_task = None
        self.watchdog_task = None
        self.last_heartbeat = 0.0
        self.ready = None
        self.pending = {}
        self.failure = None
        self.closing = False
        self.lock = asyncio.Lock()
        self.sequence = 0
        self.archives = archives
        self.global_queue = asyncio.Queue(maxsize=256)
        self.global_task = None
        self.close_task = None

    async def start(self):
        if self.closing or self.process is not None:
            raise HostError('host cannot be restarted')
        self.ready = asyncio.get_running_loop().create_future()
        self.process = await asyncio.create_subprocess_exec(
            *self.command, stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL, creationflags=0x08000000)
        self.reader_task = asyncio.create_task(self._read())
        try:
            await asyncio.wait_for(asyncio.shield(self.ready), self.timeout)
            self.last_heartbeat = asyncio.get_running_loop().time()
            self.watchdog_task = asyncio.create_task(self._watchdog())
            self.global_task = asyncio.create_task(self._global_replies())
        except BaseException:
            await self.close()
            raise

    async def _read(self):
        try:
            while True:
                header = await self.process.stdout.readexactly(4)
                size, = struct.unpack('<I', header)
                if not 0 < size <= MAX_FRAME:
                    raise HostError('invalid host frame length')
                frame = Frame(await self.process.stdout.readexactly(size))
                kind = frame.unpack('<B')
                if kind == 1:
                    entry = {'level': frame.unpack('<I'), 'message': frame.blob().decode('utf-8', errors='replace')}
                    self.logs.append(entry)
                    self.logs = self.logs[-100:]
                    if self.on_log:
                        self.on_log(entry)
                elif kind == 2:
                    message = {'kind': 'event', 'player': frame.unpack('<i'),
                               'event': frame.text()}
                    payload = frame.blob()
                    try:
                        message['payload'] = payload.decode('utf-8')
                    except UnicodeDecodeError:
                        # 原地图按字节分页，中文可能在页边界被切开；业务字节必须无损送回 Lua。
                        message['payload'] = payload.hex()
                        message['payload_encoding'] = 'hex'
                    self.on_message(message)
                elif kind == 3:
                    if frame.unpack('<I') != 1 or self.ready.done():
                        raise HostError('invalid host readiness')
                    self.ready.set_result(None)
                elif kind == 4:
                    sequence = frame.unpack('<Q')
                    future = self.pending.pop(sequence, None)
                    if future is None:
                        raise HostError('unexpected host acknowledgement')
                    future.set_result(sequence)
                elif kind == 5:
                    raise HostError(frame.text())
                elif kind == 6:
                    self.last_heartbeat = asyncio.get_running_loop().time()
                elif kind == 7:
                    self.global_queue.put_nowait((frame.unpack('<I'), frame.text(), frame.text(), frame.text()))
                else:
                    raise HostError('unknown host frame type')
                frame.finish()
        except (Exception, asyncio.CancelledError) as error:
            if not self.closing:
                self.failure = str(error) or 'host exited'
                self.on_failure(self.failure)
            for future in [self.ready, *self.pending.values()]:
                if future and not future.done():
                    future.set_exception(HostError(self.failure or 'host closed'))

    async def _watchdog(self):
        while not self.closing:
            await asyncio.sleep(0.5)
            if asyncio.get_running_loop().time() - self.last_heartbeat > max(2.0, self.timeout):
                self.failure = 'host heartbeat timed out'
                self.on_failure(self.failure)
                await self.close()
                return

    async def _write(self, body):
        if self.failure:
            raise HostError(self.failure)
        self.process.stdin.write(struct.pack('<I', len(body)) + body)
        await self.process.stdin.drain()

    async def _global_replies(self):
        archive = None
        try:
            while True:
                request = await self.global_queue.get()
                if archive is None:
                    archive = GlobalArchive(self.archives)
                name, result = archive.request(*request)
                await self._event(-1, name, json.dumps(result, ensure_ascii=False, separators=(',', ':')), 0x12)
                self.global_queue.task_done()
        except asyncio.CancelledError:
            pass
        except Exception as error:
            if not self.closing:
                self.failure = 'local global archive failed: ' + str(error)
                self.on_failure(self.failure)
        finally:
            if archive:
                archive.close()

    async def event(self, player, name, payload):
        return await self._event(player, name, payload, 0x10)

    async def _event(self, player, name, payload, kind):
        async with self.lock:
            if self.failure or self.closing:
                raise HostError(self.failure or 'host closed')
            self.sequence += 1
            sequence = self.sequence
            body = bytes([kind]) + struct.pack('<Qi', sequence, player) + pack_text(name) + pack_text(payload)
            future = asyncio.get_running_loop().create_future()
            self.pending[sequence] = future
            try:
                await self._write(body)
                return await asyncio.wait_for(future, self.timeout)
            except asyncio.TimeoutError as error:
                self.failure = 'host request timed out'
                self.on_failure(self.failure)
                await self.close()
                raise HostError(self.failure) from error
            finally:
                self.pending.pop(sequence, None)

    async def close(self):
        if self.close_task is None:
            self.closing = True
            self.close_task = asyncio.create_task(self._close(asyncio.current_task()))
        await asyncio.shield(self.close_task)

    async def _close(self, initiator):
        # 连接处理器取消不能中断宿主清理；并发 close 共同等待同一关闭任务。
        if self.global_task and self.global_task is not initiator:
            self.global_task.cancel()
            await asyncio.gather(self.global_task, return_exceptions=True)
        if self.watchdog_task and self.watchdog_task is not initiator:
            self.watchdog_task.cancel()
            await asyncio.gather(self.watchdog_task, return_exceptions=True)
        if self.process and self.process.returncode is None:
            try:
                self.process.stdin.write(struct.pack('<I', 1) + b'\x11')
                await self.process.stdin.drain()
                await asyncio.wait_for(self.process.wait(), 1.0)
            except (OSError, asyncio.TimeoutError, ConnectionError):
                if self.process.returncode is None:
                    self.process.kill()
                await self.process.wait()
        if self.reader_task:
            self.reader_task.cancel()
            await asyncio.gather(self.reader_task, return_exceptions=True)
        if self.ready and self.ready.done() and not self.ready.cancelled():
            self.ready.exception()
