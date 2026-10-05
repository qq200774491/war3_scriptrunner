"""便携启动器的取消清理、存档路径导出与真实 Tk/CLI 接线。"""
import asyncio
import json
from pathlib import Path
import tempfile
import time
import tkinter as tk
import unittest

from tools.launcher import ROOT, supervise
from tools.launcher_gui import LauncherWindow
from tools.package_launcher import export_config


class SupervisorTests(unittest.IsolatedAsyncioTestCase):
    async def test_stop_waits_for_backend_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            stop = Path(directory) / 'stop'
            started = asyncio.Event()
            cleaned = asyncio.Event()
            async def operation():
                try:
                    started.set()
                    await asyncio.Future()
                finally:
                    await asyncio.sleep(.01)
                    cleaned.set()
            running = asyncio.create_task(supervise(operation(), stop))
            await started.wait()
            stop.touch()
            await asyncio.wait_for(running, 2)
            self.assertTrue(cleaned.is_set())

    async def test_backend_errors_propagate(self):
        with tempfile.TemporaryDirectory() as directory:
            async def failure():
                raise RuntimeError('host failed')
            with self.assertRaisesRegex(RuntimeError, 'host failed'):
                await supervise(failure(), Path(directory) / 'stop')


class PackageConfigTests(unittest.TestCase):
    def test_export_keeps_archive_location_and_preserves_user_edits(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source.json'
            source.write_text(json.dumps({'map': 'map.w3x', 'archives': 'state/map中文存档'}), encoding='utf-8')
            target = root / 'package/config.json'
            target.parent.mkdir()
            export_config(source, target)
            data = json.loads(target.read_text(encoding='utf-8'))
            self.assertEqual(Path(data['archives']), root / 'state/map中文存档')
            target.write_text('USER CONFIG', encoding='utf-8')
            export_config(source, target)
            self.assertEqual(target.read_text(encoding='utf-8'), 'USER CONFIG')


class GuiWiringTests(unittest.TestCase):
    def test_real_check_worker_and_invalid_config_recovery(self):
        window = tk.Tk()
        window.withdraw()
        try:
            ui = LauncherWindow(window, ROOT / '本地测试配置.json')
            ui.start(check=True)
            deadline = time.monotonic() + 20
            while ui.process is not None and time.monotonic() < deadline:
                window.update()
                time.sleep(.02)
            self.assertIsNone(ui.process, 'configuration worker timed out')
            self.assertIn('"ok": true', ui.log.get('1.0', 'end'))
            self.assertNotIn('disabled', ui.start_button.state())
            with tempfile.TemporaryDirectory() as directory:
                invalid = Path(directory) / 'invalid.json'
                invalid.write_text('{broken', encoding='utf-8')
                second = tk.Toplevel(window)
                broken = LauncherWindow(second, invalid)
                self.assertIn('配置无法读取', broken.log.get('1.0', 'end'))
                second.destroy()
        finally:
            window.destroy()

class ThemeTests(unittest.TestCase):
    def test_live_switch_preserves_worker_edits_logs_and_restart_preference(self):
        from tools.launcher_theme import load_theme, THEMES
        with tempfile.TemporaryDirectory() as directory:
            preference = Path(directory) / 'appearance.json'
            window = tk.Tk()
            window.withdraw()
            try:
                ui = LauncherWindow(window, ROOT / '本地测试配置.json', preference)
                ui.start(check=True)
                worker = ui.process
                ui.values['seconds'].set('7200')
                ui.append('ERROR 保留错误记录\n')
                ui.view.show('settings')
                ui.theme_label.set(THEMES['shadowworks'])
                ui.change_theme()
                self.assertIs(ui.process, worker)
                self.assertEqual(ui.values['seconds'].get(), '7200')
                self.assertEqual(ui.view.page, 'settings')
                self.assertIn('保留错误记录', ui.log.get('1.0', 'end'))
                self.assertTrue(ui.log.tag_ranges('error'))
                self.assertEqual(ui.view.nav_buttons['overview'].cget('bg'), '#124493')
                self.assertIn('disabled', ui.start_button.state())
                self.assertEqual(load_theme(preference), 'shadowworks')
                deadline = time.monotonic() + 20
                while ui.process is not None and time.monotonic() < deadline:
                    window.update()
                    time.sleep(.02)
                self.assertIsNone(ui.process)
                self.assertEqual(ui.view.phase.get(), '检查通过')
                # 正在运行时停止按钮的启用状态也必须跨主题保留。
                ui.stop_button.state(('!disabled',))
                ui.theme_label.set(THEMES['monochrome'])
                ui.change_theme()
                self.assertNotIn('disabled', ui.stop_button.state())
                self.assertEqual(ui.view.nav_buttons['overview'].cget('bg'), '#181818')
                second = tk.Toplevel(window)
                restored = LauncherWindow(second, ROOT / '本地测试配置.json', preference)
                self.assertEqual(restored.theme_label.get(), '黑白灰')
                second.destroy()
            finally:
                window.destroy()

    def test_invalid_preference_falls_back_without_touching_project(self):
        from tools.launcher_theme import load_theme
        with tempfile.TemporaryDirectory() as directory:
            preference = Path(directory) / 'appearance.json'
            for data in ('{bad', '[]', '{"theme": []}', '{"theme": "unknown"}'):
                preference.write_text(data, encoding='utf-8')
                self.assertEqual(load_theme(preference), 'monochrome')
