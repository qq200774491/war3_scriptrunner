import asyncio
from pathlib import Path
import tempfile
import unittest

from shared_room.global_archive import GlobalArchive
from shared_room.native import NativeHost

ROOT = Path(__file__).resolve().parents[1]


class GlobalArchiveTests(unittest.TestCase):
    def test_hash_persistence_atomic_rejection_and_delete(self):
        with tempfile.TemporaryDirectory() as folder:
            archive = GlobalArchive(folder)
            def call(op, value=''):
                return archive.request(7, op, 'market', value)[1]
            try:
                self.assertEqual(call('MsGAHmSet', '{"a":"原值","b":"2"}')['errnu'], 0)
                self.assertEqual(call('MsGAHmSet', '{"a":"不应写入","b":123}')['errnu'], -1)
                self.assertEqual(call('MsGAHmGet', '["a","missing"]')['data']['value'], {'a': '原值'})
                self.assertEqual(call('MsGAHDel', '["b","b"]')['data']['value'], 1)
            finally:
                archive.close()
            archive = GlobalArchive(folder)
            try:
                self.assertEqual(call('MsGAHGetAll')['data']['value'], {'a': '原值'})
                self.assertEqual(call('MsGAHDel', '["a"]')['data']['value'], 1)
                self.assertTrue(call('MsGALockAcquire', '1000')['data']['ok'])
            finally:
                archive.close()

    def test_lock_contention_expiry_type_conflict_and_invalid_arguments(self):
        now = [100.0]
        with tempfile.TemporaryDirectory() as folder:
            archive = GlobalArchive(folder, lambda: now[0])
            def call(op, arg='', key='lease'):
                return archive.request(1, op, key, arg)[1]
            try:
                first = call('MsGALockAcquire', '-1')['data']
                self.assertEqual(first['ttl_ms'], 1000)
                self.assertTrue(first['ok'])
                self.assertFalse(call('MsGALockAcquire', '1000')['data']['ok'])
                self.assertEqual(call('MsGAHmSet', '{"a":"b"}')['errnu'], -1)
                now[0] += 1.1
                self.assertTrue(call('MsGALockAcquire', '1000')['data']['ok'])
                call('MsGALockRelease')
                self.assertTrue(call('MsGALockAcquire', '1000')['data']['ok'])
                for op, value in [('MsGAHmGet', '[]'), ('MsGAHDel', '{}'), ('MsGAHmSet', 'null')]:
                    self.assertEqual(call(op, value, 'other')['errnu'], -1)
            finally:
                archive.close()


class GlobalNativeTests(unittest.IsolatedAsyncioTestCase):
    async def test_original_dll_async_hash_lock_and_broadcast(self):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            (folder / 'main.lua').write_text('''
local finished = false
RegisterEvent('_msbroadcast', function(_, _, payload)
    local message = json.decode(payload)
    assert(message.key == 'notice' and message.value == '完成')
    finished = true
    MsSendMlEvent(0, 'passed', 'global native chain')
end)
local returned = false
local tid = MsGALockAcquire('lock', 1000, function(reply)
    assert(returned and reply.errnu == 0 and reply.data.ok)
    MsGALockAcquire('lock', 1000, function(blocked)
        assert(blocked.errnu == 0 and not blocked.data.ok)
        MsGAHmSet('hash', {owner='玩家一', other='2'}, function(saved)
            assert(saved.errnu == 0 and saved.data.value == 2)
            MsGAHmGet('hash', {'owner', 'absent'}, function(read)
                assert(read.errnu == 0 and read.data.value.owner == '玩家一' and not read.data.value.absent)
                MsGAHGetAll('hash', function(all)
                    assert(all.errnu == 0 and all.data.value.other == '2')
                    MsGAHDel('hash', {'other'}, function(deleted)
                        assert(deleted.errnu == 0 and deleted.data.value == 1)
                        MsGALockRelease('lock', function(released)
                            assert(released.errnu == 0 and released.data.ok)
                            MsBroadcast('notice', '完成')
                        end)
                    end)
                end)
            end)
        end)
    end)
end)
assert(type(tid) == 'number')
returned = true
''', encoding='utf-8')
            events = []
            host = NativeHost(ROOT / 'build/Release/script_host.exe', ROOT / 'scriptrunner.dll',
                              folder, (0, 1), events.append, archives=folder / 'state')
            try:
                await host.start()
                for _ in range(200):
                    if events or host.failure:
                        break
                    await asyncio.sleep(.01)
                self.assertIsNone(host.failure)
                self.assertEqual(events[0]['payload'], 'global native chain')
            finally:
                await host.close()
