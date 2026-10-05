import asyncio
from pathlib import Path
import tempfile
import unittest

from shared_room.client import Client
from shared_room.native import NativeHost, HostError
from shared_room.server import RoomServer, encode

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'build/Release/script_host.exe'
DLL = ROOT / 'scriptrunner.dll'
COUNTER = ROOT / 'tests/scripts/counter'


class RoomTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.room = RoomServer(EXE, DLL, COUNTER)
        await self.room.start()
        self.clients = [Client(), Client()]
        for player, client in enumerate(self.clients):
            await client.connect(self.room.connection_info(player))
        for client in self.clients:
            await client.expect('ready')

    async def asyncTearDown(self):
        await self.room.close()
        for client in self.clients:
            await client.close()

    async def exchange(self, player, request, event, payload):
        client = self.clients[player]
        await client.send(request, event, payload)
        result = await client.expect('event')
        ack = await client.expect('ack')
        self.assertEqual(result['player'], player)
        return result, ack

    async def test_real_dll_shared_state_and_identity(self):
        first, ack1 = await self.exchange(0, 'same-id', 'increment', '7')
        second, ack2 = await self.exchange(1, 'same-id', 'increment', '5')
        third, _ = await self.exchange(0, 'query', 'query', '')
        self.assertEqual([first['payload'], second['payload'], third['payload']], ['7', '12', '12'])
        self.assertLess(ack1['sequence'], ack2['sequence'])
        self.assertTrue(any('2 players' in entry['message'] for entry in self.room.host.logs))

    async def test_duplicate_and_conflicting_retry(self):
        _, original = await self.exchange(0, 'add', 'increment', '7')
        await self.clients[0].send('add', 'increment', '7')
        repeat = await self.clients[0].expect('ack')
        self.assertTrue(repeat['duplicate'])
        self.assertEqual(repeat['sequence'], original['sequence'])
        await self.clients[0].send('add', 'increment', '100')
        error = await self.clients[0].expect('error')
        self.assertIn('different content', error['reason'])
        result, _ = await self.exchange(1, 'check', 'query', '')
        self.assertEqual(result['payload'], '7')

    async def test_concurrent_clients(self):
        for number in range(20):
            await asyncio.gather(*(client.send(str(number), 'increment', '1') for client in self.clients))
            await asyncio.gather(*(client.expect('ack') for client in self.clients))
        # 清除增量回包，单独读取新查询结果。
        for client in self.clients:
            client.inbox.clear()
        result, _ = await self.exchange(1, 'final', 'query', '')
        self.assertEqual(result['payload'], '40')

    async def test_identity_cannot_be_overridden(self):
        writer = self.clients[0].writer
        writer.write(encode({'id': 'spoof', 'event': 'increment', 'payload': '9', 'player': 1}))
        await writer.drain()
        self.assertIn('bound to connection', (await self.clients[0].expect('error'))['reason'])
        result, _ = await self.exchange(1, 'check', 'query', '')
        self.assertEqual(result['payload'], '0')

    async def test_script_error_fails_room(self):
        await self.clients[0].send('fail', 'fail', '')
        result = await self.clients[0].expect('room_failed')
        self.assertIn('Lua callback', result['reason'])
        self.assertTrue(any('intentional test failure' in e['message'] for e in self.room.host.logs))

    async def test_disconnect_invalidates_room(self):
        await self.clients[0].close()
        result = await self.clients[1].expect('room_failed')
        self.assertIn('disconnected', result['reason'])

    async def test_timer_and_broadcast(self):
        await self.clients[0].send('later', 'later', 'deferred')
        await self.clients[0].expect('ack')
        self.assertEqual((await self.clients[0].expect('event'))['payload'], 'deferred')
        await self.clients[1].send('broadcast', 'broadcast', 'both')
        replies = await asyncio.gather(*(client.expect('event') for client in self.clients))
        self.assertEqual([r['payload'] for r in replies], ['both', 'both'])
        await self.clients[1].expect('ack')

    async def test_embedded_nul_and_size_limit(self):
        for payload in ['a\0b', 'x' * (128 * 1024 + 1)]:
            await self.clients[0].send('bad', 'increment', payload)
            self.assertIn('invalid request', (await self.clients[0].expect('error'))['reason'])
        result, _ = await self.exchange(1, 'read', 'query', '')
        self.assertEqual(result['payload'], '0')

    async def test_duplicate_concurrent_same_client(self):
        await self.clients[0].send('once', 'increment', '3')
        await self.clients[0].send('once', 'increment', '3')
        first = await self.clients[0].expect('ack')
        second = await self.clients[0].expect('ack')
        self.assertEqual(first['sequence'], second['sequence'])
        result, _ = await self.exchange(1, 'read', 'query', '')
        self.assertEqual(result['payload'], '3')

    async def test_reserved_event_and_bad_credentials(self):
        await self.clients[0].send('x', '_roomloaded', '{}')
        self.assertIn('invalid request', (await self.clients[0].expect('error'))['reason'])
        intruder = Client()
        with self.assertRaisesRegex(RuntimeError, 'credentials'):
            await intruder.connect({**self.room.connection_info(0), 'token': 'invalid'})


class HostTests(unittest.IsolatedAsyncioTestCase):
    async def test_cancelled_close_caller_still_reaps_host(self):
        host = NativeHost(EXE, DLL, COUNTER, [0, 1])
        await host.start()
        closing = asyncio.create_task(host.close())
        await asyncio.sleep(0)
        closing.cancel()
        await asyncio.gather(closing, return_exceptions=True)
        await host.close()
        self.assertEqual(host.process.returncode, 0)

    async def test_unicode_script_path_and_reopen(self):
        with tempfile.TemporaryDirectory(prefix='路径测试-', dir=ROOT / 'build') as folder:
            scripts = Path(folder)
            (scripts / 'main.lua').write_text('Log.Info("fresh room")', encoding='utf-8')
            for _ in range(2):
                host = NativeHost(EXE, DLL, scripts, [0, 1])
                try:
                    await host.start()
                    self.assertTrue(any('fresh room' in e['message'] for e in host.logs))
                finally:
                    await host.close()
                self.assertEqual(host.process.returncode, 0)

    async def test_unknown_dll_rejected(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
            candidate = Path(folder) / 'candidate.dll'
            candidate.write_bytes(DLL.read_bytes() + b'changed')
            host = NativeHost(EXE, candidate, COUNTER, [0, 1])
            with self.assertRaisesRegex(HostError, 'unverified DLL'):
                await host.start()
            self.assertIsNotNone(host.process.returncode)

    async def test_load_error_and_unsupported_api(self):
        for code, expected in [('error("load failed")', 'main.lua load failed'),
                               ('MsGetPlayerMapLevel(0)', 'unsupported NativeInterface slot 3')]:
            with self.subTest(code=code), tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
                scripts = Path(folder)
                (scripts / 'main.lua').write_text(code, encoding='utf-8')
                host = NativeHost(EXE, DLL, scripts, [0, 1])
                with self.assertRaisesRegex(HostError, expected):
                    await host.start()

    async def test_hung_script_is_killed(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
            scripts = Path(folder)
            (scripts / 'main.lua').write_text('while true do end', encoding='utf-8')
            host = NativeHost(EXE, DLL, scripts, [0, 1], timeout=0.3)
            with self.assertRaises((asyncio.TimeoutError, HostError)):
                await host.start()
            self.assertIsNotNone(host.process.returncode)


if __name__ == '__main__':
    unittest.main(verbosity=2)
