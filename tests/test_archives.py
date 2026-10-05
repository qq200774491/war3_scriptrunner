import asyncio
from pathlib import Path
import tempfile
import unittest

from shared_room.native import NativeHost, HostError

ROOT = Path(__file__).resolve().parents[1]


class ArchiveTests(unittest.IsolatedAsyncioTestCase):
    async def run_script(self, directory, archives, source, wait=0):
        script = Path(directory) / 'scripts'
        script.mkdir(exist_ok=True)
        (script / 'main.lua').write_text(source, encoding='utf-8')
        events = []
        host = NativeHost(ROOT / 'build/Release/script_host.exe', ROOT / 'scriptrunner.dll',
            script, (0, 1), events.append, archives=archives)
        try:
            await host.start()
            if wait:
                await asyncio.sleep(wait)
            self.assertIsNone(host.failure)
            return events
        finally:
            await host.close()

    async def test_two_player_archives_persist_separately_and_replace(self):
        with tempfile.TemporaryDirectory() as folder:
            archives = Path(folder) / 'archives'
            await self.run_script(folder, archives, '''
assert(MsGetPlayerName(-1) == 'LocalSystem')
assert(MsGetPlayerName(0) == 'SRPlayer0' and MsGetPlayerName(1) == 'SRPlayer1')
assert(MsGetRoomPlayerCount() == 2 and MsGetRoomModeId() == 0)
assert(MsGetPlayedCount(0) == 1 and MsGetPlayedCount(1) == 1)
assert(MsGetRoomStartTs() > 1700000000 and MsGetRoomGameTime() >= 0)
assert(MsGetScriptArchive(0) == nil and MsGetScriptArchive(1) == nil)
assert(MsSaveScriptArchive(0, '玩家一旧档') == 0)
assert(MsSaveScriptArchive(1, '玩家二独立存档') == 0)
assert(MsSaveScriptArchive(0, '玩家一新档') == 0)
assert(MsGetScriptArchive(0) == '玩家一新档')
''')
            await self.run_script(folder, archives, '''
assert(MsGetScriptArchive(0) == '玩家一新档')
assert(MsGetScriptArchive(1) == '玩家二独立存档')
assert(MsGetPlayedCount(0) == 2 and MsGetPlayedCount(1) == 2)
''')
            self.assertEqual((archives / 'slot-0.archive').read_text(encoding='utf-8'), '玩家一新档')
            self.assertEqual((archives / 'slot-1.archive').read_text(encoding='utf-8'), '玩家二独立存档')

    async def test_archive_directory_is_required_and_roster_is_enforced(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaisesRegex(HostError, 'not configured'):
                await self.run_script(folder, None, 'MsGetScriptArchive(0)')
            with self.assertRaises(HostError):
                await self.run_script(folder, Path(folder) / 'archives', "MsSaveScriptArchive(2, 'wrong player')")

    async def test_directory_lock_rejects_second_writer(self):
        with tempfile.TemporaryDirectory() as folder:
            script = Path(folder) / 'scripts'
            script.mkdir()
            (script / 'main.lua').write_text('-- empty\n', encoding='utf-8')
            archives = Path(folder) / 'archives'
            first = NativeHost(ROOT / 'build/Release/script_host.exe', ROOT / 'scriptrunner.dll', script, (0, 1), archives=archives)
            try:
                await first.start()
                with self.assertRaisesRegex(HostError, 'archive file open failed'):
                    await self.run_script(folder, archives, '-- empty\n')
            finally:
                await first.close()

    async def test_failed_replace_retains_old_archive(self):
        with tempfile.TemporaryDirectory() as folder:
            archives = Path(folder) / 'archives'
            await self.run_script(folder, archives, "MsSaveScriptArchive(0, 'old')")
            # 用目录占住临时文件名，确定性触发写入失败，验证不会清空旧档。
            (archives / 'slot-0.archive.pending').mkdir()
            with self.assertRaisesRegex(HostError, 'archive file open failed'):
                await self.run_script(folder, archives, "MsSaveScriptArchive(0, 'new')")
            self.assertEqual((archives / 'slot-0.archive').read_text(), 'old')

    async def test_original_engine_binary_page_is_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            events = await self.run_script(folder, None,
                "MsSendMlEvent(0, 'page', string.char(0xe4, 0xb8))")
            self.assertEqual(events[0]['payload_encoding'], 'hex')
            self.assertEqual(bytes.fromhex(events[0]['payload']), b'\xe4\xb8')
