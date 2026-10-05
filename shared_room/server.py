"""固定玩家名单的本机房间服务；连接身份由预分配凭据绑定。"""
import argparse
import asyncio
import hashlib
import json
from pathlib import Path
import secrets
import uuid

from .native import NativeHost, HostError

MAX_LINE = 256 * 1024
MAX_REQUESTS = 10000


def encode(message):
    return (json.dumps(message, ensure_ascii=False, separators=(',', ':')) + '\n').encode('utf-8')


async def read_json(reader, timeout=30):
    line = await asyncio.wait_for(reader.readline(), timeout)
    if not line:
        raise EOFError('client disconnected')
    message = json.loads(line)
    if not isinstance(message, dict):
        raise ValueError('expected JSON object')
    return message


class Session:
    def __init__(self, player, writer):
        self.player = player
        self.writer = writer
        self.queue = asyncio.Queue(maxsize=256)

    def send(self, message):
        self.queue.put_nowait(message)

    async def pump(self):
        while True:
            message = await self.queue.get()
            try:
                self.writer.write(encode(message))
                await asyncio.wait_for(self.writer.drain(), 5)
            finally:
                self.queue.task_done()


class RoomServer:
    def __init__(self, executable, dll, scripts, players=(0, 1), timeout=5.0, relay_player=None, archives=None, on_log=None):
        if not players or any(type(p) is not int or not 0 <= p < 24 for p in players) or len(set(players)) != len(players):
            raise ValueError('invalid player roster')
        self.players = tuple(players)
        if relay_player is not None and relay_player not in players:
            raise ValueError('relay must belong to player roster')
        self.relay_player = relay_player
        self.delivery = 0
        self.room_id = str(uuid.uuid4())
        self.tokens = {player: secrets.token_urlsafe(32) for player in players}
        self.sessions = {}
        self.server = None
        self.start_task = None
        self.stop_task = None
        self.handlers = set()
        self.failed = None
        self.closed = False
        self.active = False
        self.dedup = {}
        self.request_lock = asyncio.Lock()
        self.host = NativeHost(executable, dll, scripts, players, self._route, self.fail, timeout,
                               archives=archives, on_log=on_log)

    async def start(self, port=0):
        self.server = await asyncio.start_server(self._client, '127.0.0.1', port, limit=MAX_LINE)
        return self.server.sockets[0].getsockname()[1]

    def connection_info(self, player):
        return {'version': 1, 'room': self.room_id, 'player': player, 'token': self.tokens[player],
                'host': '127.0.0.1', 'port': self.server.sockets[0].getsockname()[1]}

    def fail(self, reason):
        if self.failed or self.closed:
            return
        self.failed = reason
        self.active = False
        self.stop_task = asyncio.create_task(self.host.close())
        for session in self.sessions.values():
            try:
                session.send({'kind': 'room_failed', 'room': self.room_id, 'reason': reason})
            except asyncio.QueueFull:
                session.writer.close()

    def _route(self, message):
        if len(encode(message)) > MAX_LINE - 256:
            raise HostError('callback exceeds client frame limit')
        target = message['player']
        if target != -1 and target not in self.sessions:
            raise HostError('callback target not in connected room')
        if self.relay_player is not None:
            recipients = [self.sessions[self.relay_player]]
        elif target == -1:
            recipients = list(self.sessions.values())
        elif target in self.sessions:
            recipients = [self.sessions[target]]
        else:
            raise HostError('callback target not in connected room')
        self.delivery += 1
        for session in recipients:
            session.send({**message, 'room': self.room_id, 'delivery': self.delivery})

    async def _activate(self):
        try:
            await self.host.start()
            if self.failed or self.closed:
                await self.host.close()
                return
            self.active = True
            for session in self.sessions.values():
                session.send({'kind': 'ready', 'room': self.room_id, 'players': self.players})
        except Exception as error:
            self.fail(str(error))

    def _authenticate(self, hello):
        player = hello.get('player')
        if (self.closed or self.failed or hello.get('version') != 1 or
                hello.get('room') != self.room_id or type(player) is not int or
                player not in self.tokens or not isinstance(hello.get('token'), str) or
                not secrets.compare_digest(hello['token'], self.tokens[player])):
            raise ValueError('invalid room credentials or room unavailable')
        if player in self.sessions:
            raise ValueError('player already connected')
        return player

    async def _client(self, reader, writer):
        task = asyncio.current_task()
        self.handlers.add(task)
        session = None
        pump = None
        try:
            player = self._authenticate(await read_json(reader, 5))
            session = Session(player, writer)
            self.sessions[player] = session
            pump = asyncio.create_task(session.pump())
            session.send({'kind': 'joined', 'room': self.room_id, 'player': player})
            if len(self.sessions) == len(self.players) and self.start_task is None:
                self.start_task = asyncio.create_task(self._activate())
            while not self.closed:
                message = await read_json(reader, 3600)
                try:
                    response = await self.request(player, message)
                except (ValueError, HostError) as error:
                    response = {'kind': 'error', 'id': message.get('id'), 'reason': str(error)}
                session.send(response)
                if pump.done():
                    pump.result()
        except (Exception, asyncio.CancelledError) as error:
            if session is None and not self.closed:
                writer.write(encode({'kind': 'error', 'reason': str(error)}))
                try:
                    await asyncio.wait_for(writer.drain(), 1)
                except (OSError, asyncio.TimeoutError):
                    pass
        finally:
            if session:
                self.sessions.pop(session.player, None)
                if not self.closed:
                    self.fail(f'player {session.player} disconnected; reconnect requires a new room')
                    await self.host.close()
            if pump:
                pump.cancel()
                await asyncio.gather(pump, return_exceptions=True)
            writer.close()
            try:
                await writer.wait_closed()
            except OSError:
                pass  # 对端已退出时 Windows 可在 wait_closed 再次报告连接重置。
            finally:
                self.handlers.discard(task)

    async def request(self, player, request):
        if set(request) != {'id', 'event', 'payload'}:
            raise ValueError('request fields must be id, event, payload; player is bound to connection')
        request_id, event, payload = request['id'], request['event'], request['payload']
        if (not isinstance(request_id, str) or not 1 <= len(request_id) <= 64 or
                not isinstance(event, str) or not 1 <= len(event.encode('utf-8')) <= 64 or
                event.startswith('_') or not isinstance(payload, str) or
                len(payload.encode('utf-8')) > 128 * 1024 or '\0' in event or '\0' in payload):
            raise ValueError('invalid request content')
        fingerprint = hashlib.sha256(encode({'event': event, 'payload': payload})).digest()
        key = (player, request_id)
        async with self.request_lock:
            if not self.active or self.failed:
                raise HostError(self.failed or 'room not ready')
            previous = self.dedup.get(key)
            if previous:
                digest, ack = previous
                if digest != fingerprint:
                    raise ValueError('request id reused with different content')
                return {**ack, 'duplicate': True}
            if len(self.dedup) >= MAX_REQUESTS:
                raise ValueError('room request limit reached; open a new room')
            sequence = await self.host.event(player, event, payload)
            ack = {'kind': 'ack', 'id': request_id, 'sequence': sequence, 'duplicate': False}
            self.dedup[key] = (fingerprint, ack)
            return ack

    async def close(self):
        if self.closed:
            return
        self.closed = True
        if self.server:
            self.server.close()
        if self.start_task:
            await asyncio.gather(self.start_task, return_exceptions=True)
        await self.host.close()
        if self.stop_task:
            await asyncio.gather(self.stop_task, return_exceptions=True)
        for session in list(self.sessions.values()):
            session.writer.close()
        tasks = list(self.handlers)
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        if self.server:
            await self.server.wait_closed()


async def main(args):
    server = RoomServer(args.host_exe, args.dll, args.scripts, tuple(map(int, args.players.split(','))),
                        relay_player=args.relay)
    await server.start(args.port)
    try:
        connections = [server.connection_info(player) for player in server.players]
        args.connections.write_text(json.dumps(connections, indent=2), encoding='utf-8')
        print(f'Room {server.room_id}; connection profiles saved to {args.connections}', flush=True)
        print('Waiting for every configured player. Ctrl+C closes the room.', flush=True)
        await asyncio.Future()
    finally:
        await server.close()


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host-exe', type=Path, default=root / 'build/Release/script_host.exe')
    parser.add_argument('--dll', type=Path, default=root / 'scriptrunner.dll')
    parser.add_argument('--scripts', type=Path, required=True)
    parser.add_argument('--players', default='0,1')
    parser.add_argument('--port', type=int, default=0)
    parser.add_argument('--relay', type=int, help='Warcraft mode: send all callbacks only to this sync relay')
    parser.add_argument('--connections', type=Path, default=root / 'connections.json')
    try:
        asyncio.run(main(parser.parse_args()))
    except KeyboardInterrupt:
        pass
