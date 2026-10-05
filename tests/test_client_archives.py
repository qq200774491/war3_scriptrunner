import ctypes
import json
from ctypes import wintypes
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from tools.client_archives import INI_NAME, launch_lock, prepare_snapshots, read_ini
from tools.project_quicktest import prepare_project_clients


class ClientArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.legacy = self.root / 'old.ini'
        self.original = (b'[DzAPI]\r\nSSV-0-platform_uid=12345678\r\n'
                         b'SSV-1-platform_uid=12345679\r\nSSV-0-save=old-A\r\n'
                         b'SSV-1-save=old-B\r\nRED-0-3---0-0-0-0=25\r\n'
                         b'[Other]\r\nuntouched=100%\r\n')
        self.legacy.write_bytes(self.original)
        self.folder = self.root / 'clients'
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.kernel.GetPrivateProfileStringA.argtypes = [wintypes.LPCSTR, wintypes.LPCSTR,
            wintypes.LPCSTR, wintypes.LPSTR, wintypes.DWORD, wintypes.LPCSTR]
        self.kernel.WritePrivateProfileStringA.argtypes = [wintypes.LPCSTR, wintypes.LPCSTR,
            wintypes.LPCSTR, wintypes.LPCSTR]

    def read(self, path, key):
        buffer = ctypes.create_string_buffer(8192)
        self.kernel.GetPrivateProfileStringA(b'DzAPI', key.encode('ascii'), b'<missing>',
            buffer, len(buffer), str(path).encode('mbcs'))
        return buffer.value.decode('mbcs')

    def write(self, path, key, value):
        result = self.kernel.WritePrivateProfileStringA(b'DzAPI', key.encode('ascii'),
            value.encode('mbcs') if value is not None else None, str(path).encode('mbcs'))
        self.assertTrue(result, ctypes.get_last_error())

    def test_migration_snapshot_is_identical_and_original_is_preserved(self):
        games = prepare_snapshots(self.folder, self.legacy)
        self.assertNotEqual(games[0], games[1])
        self.assertEqual(self.legacy.read_bytes(), self.original)
        self.assertEqual((self.folder / 'legacy-dz_w3_plugin.ini.bak').read_bytes(), self.original)
        for game in games.values():
            self.assertEqual(self.read(game, 'SSV-0-save'), 'old-A')
            self.assertEqual(self.read(game, 'SSV-1-save'), 'old-B')
            self.assertEqual(self.read(game, 'RED-0-3---0-0-0-0'), '25')
            config, _ = read_ini((game).read_bytes())
            self.assertEqual(config['Other']['untouched'], '100%')
        self.assertEqual((games[0]).read_bytes(), (games[1]).read_bytes())

    def test_two_processes_save_reload_and_ignore_remote_stale_copies(self):
        games = prepare_snapshots(self.folder, self.legacy)
        worker = '''
import ctypes, sys
from pathlib import Path
kernel = ctypes.WinDLL('kernel32', use_last_error=True)
path = str(Path(sys.argv[1]).resolve()).encode('mbcs')
player = int(sys.argv[2])
for index in range(100):
    key = f'SSV-{player}-key-{index}'.encode()
    assert kernel.WritePrivateProfileStringA(b'DzAPI', key, f'owner-{player}-{index}'.encode(), path)
assert kernel.WritePrivateProfileStringA(b'DzAPI', f'SSV-{player}-save'.encode(), f'new-{player}'.encode(), path)
assert kernel.WritePrivateProfileStringA(b'DzAPI', f'SSV-{1-player}-save'.encode(), b'wrong-remote-copy', path)
'''
        processes = [subprocess.Popen([sys.executable, '-c', worker, str(games[p]), str(p)],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE) for p in (0, 1)]
        for process in processes:
            output, error = process.communicate(timeout=10)
            self.assertEqual(process.returncode, 0, (output, error))
        # 重启之前两端文件内容独立；下一次开局只采信各自的权威槽位。
        self.assertEqual(self.read(games[0], 'SSV-1-save'), 'wrong-remote-copy')
        self.assertEqual(self.read(games[1], 'SSV-1-save'), 'new-1')
        games = prepare_snapshots(self.folder, self.legacy)
        for game in games.values():
            for player in (0, 1):
                self.assertEqual(self.read(game, f'SSV-{player}-save'), f'new-{player}')
                for index in range(100):
                    self.assertEqual(self.read(game, f'SSV-{player}-key-{index}'),
                                     f'owner-{player}-{index}')
        self.assertEqual(self.legacy.read_bytes(), self.original)

    def test_deleted_and_empty_values_do_not_resurrect_from_peer_or_legacy(self):
        games = prepare_snapshots(self.folder, self.legacy)
        self.write(games[0], 'SSV-0-save', None)
        self.write(games[1], 'SSV-1-save', '')
        prepare_snapshots(self.folder, self.legacy)
        for game in games.values():
            self.assertEqual(self.read(game, 'SSV-0-save'), '<missing>')
            self.assertEqual(self.read(game, 'SSV-1-save'), '')

    def test_missing_or_invalid_owner_stops_before_overwriting_peer(self):
        games = prepare_snapshots(self.folder, self.legacy)
        first = (games[0]).read_bytes()
        (games[1]).unlink()
        with self.assertRaises(FileNotFoundError):
            prepare_snapshots(self.folder, self.legacy)
        self.assertEqual((games[0]).read_bytes(), first)
        (games[1]).write_bytes(b'[DzAPI]\nSSV-1-save=a\nssv-1-save=b\n')
        with self.assertRaisesRegex(ValueError, '重复键'):
            prepare_snapshots(self.folder, self.legacy)
        self.assertEqual((games[0]).read_bytes(), first)

    def test_interrupted_publish_can_retry_without_losing_owned_data(self):
        games = prepare_snapshots(self.folder, self.legacy)
        self.write(games[0], 'SSV-0-save', 'new-A')
        self.write(games[1], 'SSV-1-save', 'new-B')
        from tools.client_archives import atomic_write
        def fail_second(path, data):
            if path.name == 'player-1.ini':
                raise OSError('simulated replace failure')
            atomic_write(path, data)
        with patch('tools.client_archives.atomic_write', side_effect=fail_second):
            with self.assertRaises(OSError):
                prepare_snapshots(self.folder, self.legacy)
        prepare_snapshots(self.folder, self.legacy)
        for game in games.values():
            self.assertEqual(self.read(game, 'SSV-0-save'), 'new-A')
            self.assertEqual(self.read(game, 'SSV-1-save'), 'new-B')

    def test_launch_lock_rejects_another_writer_and_releases(self):
        with launch_lock(self.root):
            with self.assertRaisesRegex(RuntimeError, '启动器'):
                with launch_lock(self.root):
                    self.fail('second writer was allowed')
        with launch_lock(self.root):
            pass

    def test_project_paths_entry_map_and_new_project_isolation(self):
        args = SimpleNamespace(game=self.root / 'base', source_game=self.root / 'source',
                               archives=self.root / 'project-A', local_entry=self.root / 'entry.lua')
        args.game.mkdir()
        args.archives.mkdir()
        (args.game / INI_NAME).write_bytes(self.original)
        (args.archives / 'slot-0.archive').write_text('cloud archive', encoding='utf-8')
        source_map = self.root / 'source.w3x'
        source_map.write_bytes(b'map')
        args.local_entry.write_text('-- local entry', encoding='utf-8')
        def stage(source, destination):
            destination.mkdir(parents=True, exist_ok=True)
            (destination / 'war3.exe').write_bytes(b'fake executable for staging test')
        with patch('tools.project_quicktest.prepare_game_directory', side_effect=stage):
            games, relative = prepare_project_clients(args, source_map)
            for game in games.values():
                self.assertEqual((args.game / relative).read_bytes(), b'map')
                self.assertFalse((game.parent / 'player-0/war3.exe').exists())
                self.assertFalse((game.parent / 'player-1/war3.exe').exists())
                self.assertEqual((args.game / args.local_entry.name).read_text(), '-- local entry')
                self.assertEqual(self.read(game, 'SSV-0-save'), 'old-A')
            self.write(games[0], 'SSV-0-save', 'persisted')
            prepare_project_clients(args, source_map)
            self.assertEqual(self.read(games[1], 'SSV-0-save'), 'persisted')
            args.archives = self.root / 'project-B'
            new_games, _ = prepare_project_clients(args, source_map)
            self.assertNotEqual(new_games, games)
            self.assertEqual(self.read(new_games[0], 'SSV-0-save'), '<missing>')
            self.assertEqual((self.root / 'project-A/slot-0.archive').read_text(), 'cloud archive')

    def test_v1_directory_migration_preserves_each_owner_and_does_not_reimport(self):
        self.folder.mkdir()
        (self.folder / 'layout.json').write_text(json.dumps({'version': 1, 'players': [0, 1]}))
        originals = {}
        for player in (0, 1):
            old = self.folder / f'player-{player}' / INI_NAME
            old.parent.mkdir()
            old.write_bytes(self.original)
            self.write(old, f'SSV-{player}-save', f'owner-{player}')
            self.write(old, f'SSV-{1-player}-save', 'stale')
            originals[player] = old.read_bytes()
        paths = prepare_snapshots(self.folder)
        for path in paths.values():
            self.assertEqual(self.read(path, 'SSV-0-save'), 'owner-0')
            self.assertEqual(self.read(path, 'SSV-1-save'), 'owner-1')
        for player in (0, 1):
            self.assertEqual((self.folder / 'backups' / f'v1-player-{player}.ini').read_bytes(), originals[player])
            self.assertEqual((self.folder / f'player-{player}' / INI_NAME).read_bytes(), originals[player])
        self.write(paths[0], 'SSV-0-save', 'latest')
        prepare_snapshots(self.folder)
        self.assertEqual(self.read(paths[1], 'SSV-0-save'), 'latest')
        self.assertEqual(json.loads((self.folder / 'layout.json').read_text())['version'], 2)
