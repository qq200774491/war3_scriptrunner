import asyncio
from pathlib import Path
import tempfile
import unittest
import uuid

from tools.quicktest_backend import HostSignals, verify_game, command

ROOT = Path(__file__).resolve().parents[1]
PROBE = ROOT / 'build/Release/quicktest_probe.exe'


class QuicktestTests(unittest.IsolatedAsyncioTestCase):
    async def test_native_host_join_signals_and_unique_sessions(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
            room = str(uuid.uuid4())
            relative = Path('Maps/Test/map.w3x')
            (Path(folder) / relative).parent.mkdir(parents=True)
            (Path(folder) / relative).touch()
            common = ['--qt-room', room, '--qt-player', 'SRPlayer0', '--qt-log', str(Path(folder) / 'host.log')]
            host = await asyncio.create_subprocess_exec(str(PROBE), '-host', '2', str(relative), *common, cwd=folder,
                stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
            signals = HostSignals(room)
            unrelated = HostSignals(str(uuid.uuid4()))
            try:
                await signals.wait(3)
                self.assertEqual(await host.stdout.readline(), b'READY\r\n')
                join = await asyncio.create_subprocess_exec(str(PROBE), '-join', '--qt-room', room,
                    '--qt-player', 'SRPlayer1', '--qt-log', str(Path(folder) / 'join.log'),
                    stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
                stdout, stderr = await asyncio.wait_for(join.communicate(), 3)
                self.assertEqual(join.returncode, 0, stderr)
                self.assertIn(b'JOIN_READY', stdout)
                with self.assertRaises(TimeoutError):
                    await unrelated.wait(0.1)
            finally:
                host.stdin.close()
                await asyncio.wait_for(host.wait(), 3)
                signals.close()
                unrelated.close()

    async def test_join_without_host_fails_instead_of_waiting(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
            process = await asyncio.create_subprocess_exec(str(PROBE), '-join', '--qt-room', str(uuid.uuid4()),
                '--qt-player', 'SRPlayer1', '--qt-log', str(Path(folder) / 'join.log'),
                stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
            _, stderr = await asyncio.wait_for(process.communicate(), 3)
            self.assertEqual(process.returncode, 1)
            self.assertIn(b'host not ready', stderr)

    async def test_host_rejects_absolute_map_before_creating_room(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
            process = await asyncio.create_subprocess_exec(str(PROBE), '-host', '2', str(Path(__file__).resolve()),
                '--qt-room', str(uuid.uuid4()), '--qt-player', 'SRPlayer0', '--qt-log', str(Path(folder) / 'host.log'),
                stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
            _, stderr = await asyncio.wait_for(process.communicate(), 3)
            self.assertEqual(process.returncode, 1)
            self.assertIn(b'relative Maps directory', stderr)

    async def test_game_hash_gate_and_separate_player_names(self):
        source = ROOT / 'build/warcraft-demo/Game.dll'
        verify_game(source)
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
            changed = Path(folder) / 'Game.dll'
            data = bytearray(source.read_bytes())
            data[-1] ^= 1
            changed.write_bytes(data)
            with self.assertRaisesRegex(ValueError, 'unknown hash'):
                verify_game(changed)
        commands = [command(ROOT, ROOT, ROOT / 'map.w3x', 'example-room', p, ROOT / f'player-{p}.log') for p in (0, 1)]
        self.assertIn('-host 2', commands[0])
        self.assertIn('-join', commands[1])
        self.assertIn('--qt-player SRPlayer0', commands[0])
        self.assertIn('--qt-player SRPlayer1', commands[1])

    async def test_one_game_executable_with_distinct_ini_arguments(self):
        game = ROOT / 'build/warcraft-demo'
        files = [ROOT / f'build/state/example/clients/player-{p}.ini' for p in (0, 1)]
        commands = [command(game, ROOT, Path('Maps/Test/Project.w3x'), 'room', p,
                            ROOT / f'player-{p}.log', ini=files[p]) for p in (0, 1)]
        for player, line in enumerate(commands):
            self.assertIn(str(game / 'war3.exe'), line)
            self.assertIn('--qt-ini ' + str(files[player].resolve()), line)
            self.assertNotIn(str(files[1-player].resolve()), line)
