from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tools.client_archives import prepare_snapshots, read_ini

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'build/Release/ini_api_fixture.dll'


class IniRedirectTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='INI重定向-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / 'game'
        self.game.mkdir()
        self.exe = self.game / 'war3.exe'
        shutil.copyfile(ROOT / 'build/Release/ini_redirect_probe.exe', self.exe)
        self.source = self.game / 'dz_w3_plugin.ini'
        self.source.write_bytes(b'[DzAPI]\nSSV-0-probe=legacy-0\nSSV-1-probe=legacy-1\nlevel=3\n')
        self.original = self.source.read_bytes()
        self.targets = prepare_snapshots(self.root / 'clients')
        for player, path in self.targets.items():
            path.write_text(f'[DzAPI]\nSSV-{player}-probe=old-{player}\nlevel={42+player}\n', encoding='utf-16')

    def start(self, player, value, requested=None, hold=False, target=None, fixture=FIXTURE):
        argv = [str(self.exe), str(fixture), str(target or self.targets[player]),
                str(requested or self.source), f'SSV-{player}-probe', value]
        if hold:
            argv.append('hold')
        process = subprocess.Popen(argv, cwd=self.game, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.addCleanup(self.stop, process)
        return process

    @staticmethod
    def stop(process):
        if process.poll() is None:
            process.kill()
        process.communicate()

    def finish(self, process, expected=0):
        output, error = process.communicate(timeout=5)
        self.assertEqual(process.returncode, expected, error)
        return output, error

    def test_string_integer_write_and_cross_dll_scope(self):
        output, _ = self.finish(self.start(0, 'new-0'))
        self.assertIn(b'BEFORE old-0', output)
        self.assertIn(b'INT 42', output)
        self.assertIn(b'AFTER new-0', output)
        self.assertIn(b'OUTSIDE legacy-0', output)
        self.assertEqual(self.source.read_bytes(), self.original)
        audit = Path(str(self.targets[0]) + '.audit').read_text()
        self.assertIn('INI_READ_ROUTED', audit)
        self.assertIn('INI_WRITE_ROUTED', audit)
        output, _ = self.finish(self.start(0, '--read-only', requested='dz_w3_plugin.ini'))
        self.assertIn(b'BEFORE new-0', output)

    def test_same_exe_two_processes_separate_targets_and_live_locks(self):
        processes = [self.start(p, f'new-{p}', hold=True) for p in (0, 1)]
        for process in processes:
            lines = [process.stdout.readline() for _ in range(5)]
            self.assertEqual(lines[-1].strip(), b'READY', lines)
        _, error = self.finish(self.start(0, 'must-not-write'), expected=1)
        self.assertIn(b'lock player INI', error)
        with self.assertRaisesRegex(RuntimeError, '占用'):
            prepare_snapshots(self.root / 'clients')
        for process in processes:
            output, error = process.communicate(b'\n', timeout=5)
            self.assertEqual(process.returncode, 0, (output, error))
        prepare_snapshots(self.root / 'clients')
        for player in (0, 1):
            output, _ = self.finish(self.start(player, '--read-only'))
            self.assertIn(f'BEFORE new-{player}'.encode(), output)
        self.assertEqual(self.source.read_bytes(), self.original)

    def test_same_name_elsewhere_and_other_filename_are_not_redirected(self):
        for relative in ('other/dz_w3_plugin.ini', 'unrelated.ini'):
            path = self.game / relative
            path.parent.mkdir(exist_ok=True)
            path.write_text('[DzAPI]\nSSV-0-probe=unrelated\n', encoding='ascii')
            output, _ = self.finish(self.start(0, 'updated-unrelated', requested=path))
            self.assertIn(b'BEFORE unrelated', output)
            self.assertIn(b'OUTSIDE updated-unrelated', output)
            config, section = read_ini(self.targets[0].read_bytes())
            self.assertEqual(config[section]['SSV-0-probe'], 'old-0')

    def test_missing_target_shared_target_and_unknown_api_fail_closed(self):
        self.finish(self.start(0, 'bad', target=self.root / 'missing.ini'), expected=1)
        self.finish(self.start(0, 'bad', target=self.source), expected=1)
        self.finish(self.start(0, 'bad', fixture=ROOT / 'build/Release/shared_room_native.dll'), expected=1)
        _, error = self.finish(self.start(0, 'bad', fixture=ROOT / 'build/Release/ini_extra_api_fixture.dll'), expected=1)
        self.assertIn(b'unsupported platform profile API', error)
        self.assertEqual(self.source.read_bytes(), self.original)

    def test_delete_and_empty_roundtrip(self):
        output, _ = self.finish(self.start(0, ''))
        self.assertIn(b'AFTER \r\n', output)
        output, _ = self.finish(self.start(0, '--read-only'))
        self.assertIn(b'BEFORE \r\n', output)
        self.finish(self.start(0, '--delete'))
        output, _ = self.finish(self.start(0, '--read-only'))
        self.assertIn(b'BEFORE missing', output)
