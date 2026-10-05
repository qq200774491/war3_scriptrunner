import configparser
import json
from pathlib import Path
import tempfile
import unittest

from tools.game_room import game_command, quote, validate_editor


class LauncherTests(unittest.TestCase):
    def test_command_serialization_preserves_windows_map_separators(self):
        command = r'F:\game\war3.exe -host 2 Maps\Test\Project.w3x'
        self.assertEqual(json.loads(quote(command)), command)
        self.assertIn(r'Maps\\Test\\Project.w3x', quote(command))

    def test_editor_root_and_required_plugins(self):
        with tempfile.TemporaryDirectory() as directory:
            editor = Path(directory) / 'editor'
            plugins = editor / 'plugin/warcraft3'
            plugins.mkdir(parents=True)
            names = ('yd_mutil_warcraft3.dll', 'yd_lua_engine.dll', 'kkapi.dll', 'kkapi_local_plugin.dll')
            config = configparser.ConfigParser()
            config['Enable'] = {name: '1' for name in names}
            for name in names:
                (plugins / name).touch()
            with (plugins / 'config.cfg').open('w') as file:
                config.write(file)
            self.assertEqual(validate_editor(editor), editor.resolve())
            with self.assertRaisesRegex(ValueError, 'Editor root'):
                validate_editor(editor.parent)
            (plugins / 'yd_mutil_warcraft3.dll').unlink()
            with self.assertRaisesRegex(ValueError, 'yd_mutil_warcraft3'):
                validate_editor(editor)

    def test_lan_prompts_for_identity_by_default(self):
        command = game_command(Path('F:/game'), Path('D:/editor'), Path('F:/map.w3x'), True)
        self.assertIn('-kkwe D:\\editor', command.replace('/', '\\'))
        self.assertNotIn('-auto', command)
        self.assertNotIn('-loadfile', command)
        auto = game_command(Path('F:/game'), Path('D:/editor'), Path('F:/map.w3x'), True, True)
        self.assertTrue(auto.endswith('-auto'))
