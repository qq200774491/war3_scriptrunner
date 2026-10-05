import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest

from shared_room.native import NativeHost, HostError
from tools.cloud_console import CloudConsole, follow
from tools.project_quicktest import parse_args

ROOT = Path(__file__).resolve().parents[1]


class ConfigTests(unittest.TestCase):
    def test_relative_paths_follow_config_and_cli_overrides_seconds(self):
        with tempfile.TemporaryDirectory(prefix='云脚本配置-') as directory:
            config = Path(directory) / 'test.json'
            config.write_text(json.dumps({'map': '地图/test.w3x', 'cloud_scripts': '云脚本', 'seconds': 3600}), encoding='utf-8-sig')
            args = parse_args(['--config', str(config), '--seconds', '20', '--no-console'])
            self.assertEqual(args.map, config.parent / '地图/test.w3x')
            self.assertEqual(args.cloud_scripts, config.parent / '云脚本')
            self.assertEqual(args.seconds, 20)
            self.assertTrue(args.no_console)

    def test_invalid_settings_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'test.json'
            for settings in ({'cloud_script': 'typo'}, {'map': 123}, {'seconds': True},
                             {'seconds': 1.5}, {'startup_timeout': float('nan')}):
                config.write_text(json.dumps(settings), encoding='utf-8')
                with self.subTest(settings=settings), contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit):
                        parse_args(['--config', str(config), '--map', 'test.w3x'])


class CloudConsoleTests(unittest.IsolatedAsyncioTestCase):
    async def test_original_dll_streams_all_logs_and_failure_before_shutdown(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / 'main.lua').write_text('''
for i = 1, 125 do Log.Info('中文云日志 %d', i) end
Log.Warning('warning sample')
error('console failure sample')
''', encoding='utf-8')
            console = CloudConsole(folder, show=False)
            host = NativeHost(ROOT / 'build/Release/script_host.exe', ROOT / 'scriptrunner.dll',
                              folder, (0, 1), on_log=console.cloud_log)
            try:
                with self.assertRaises(HostError):
                    await host.start()
                # 读取时尚未关闭 console，证明是实时输出，不是结束时才保存。
                live = console.path.read_text(encoding='utf-8')
                self.assertEqual(live.count('中文云日志'), 125)
                self.assertIn('云脚本/WARN', live)
                self.assertIn('console failure sample', live)
                self.assertEqual(len(host.logs), 100)
            finally:
                await host.close()
                console.close()
            self.assertIn('本轮房间服务已结束', '\n'.join(follow(console.path)))
            self.assertEqual((folder / 'cloud-console-latest.txt').read_text(encoding='utf-8'), str(console.path))
