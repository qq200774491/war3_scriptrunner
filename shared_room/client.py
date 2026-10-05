"""测试客户端 SDK；不是已经接入魔兽的插件。"""
import asyncio
import json
from .server import encode, MAX_LINE


class Client:
    def __init__(self):
        self.reader = None
        self.writer = None
        self.inbox = []

    async def connect(self, info):
        self.reader, self.writer = await asyncio.open_connection(info['host'], info['port'], limit=MAX_LINE)
        self.writer.write(encode({k: info[k] for k in ('version', 'room', 'player', 'token')}))
        await self.writer.drain()
        response = await self.receive()
        if response.get('kind') != 'joined':
            await self.close()
            raise RuntimeError(response.get('reason', 'join failed'))
        return response

    async def receive(self, timeout=5):
        line = await asyncio.wait_for(self.reader.readline(), timeout)
        if not line:
            raise EOFError('room connection closed')
        return json.loads(line)

    async def expect(self, kind, timeout=5):
        for index, message in enumerate(self.inbox):
            if message['kind'] == kind:
                return self.inbox.pop(index)
        async with asyncio.timeout(timeout):
            while True:
                message = await self.receive(timeout)
                if message['kind'] == kind:
                    return message
                if message['kind'] == 'room_failed':
                    raise RuntimeError(message['reason'])
                self.inbox.append(message)

    async def send(self, request_id, event, payload):
        self.writer.write(encode({'id': request_id, 'event': event, 'payload': payload}))
        await self.writer.drain()

    async def close(self):
        if self.writer:
            self.writer.close()
            await self.writer.wait_closed()
