import configparser
import json
from pathlib import Path
import tempfile
import os
import shutil
import subprocess
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

    def test_demo_map_contains_and_loads_all_room_dependencies(self):
        root = Path(__file__).resolve().parents[1]
        editor = Path('D:/war3Project/KKWE插件')
        with tempfile.TemporaryDirectory(dir=root / 'build') as directory:
            work = Path(directory)
            files = work / 'build/game'
            files.mkdir(parents=True)
            map_file = files / 'demo.w3x'
            shutil.copyfile(editor / 'example(演示地图)/JAPI/Lua-计时器-圣光.w3x', map_file)
            for source in (root / 'map').glob('*.lua'):
                shutil.copyfile(source, files / source.name)
            shutil.copyfile(root / 'map/demo_main.lua', files / 'main.lua')
            (files / 'room_profile.lua').write_text('return {}', encoding='utf-8')
            settings = dict(editor=editor, map=map_file, files=files)
            def lua_path(value):
                return json.dumps(str(value).replace('\\', '/'), ensure_ascii=False)
            (files / 'prepare_config.lua').write_text(
                'return {' + ','.join(f'{key}={lua_path(value)}' for key, value in settings.items()) + '}',
                encoding='utf-8')
            check = work / 'check.lua'
            check.write_text(f"""
dofile({lua_path(root / 'tools/prepare_game.lua')})
local ffi = require 'ffi'
local library = ffi.load({lua_path(editor / 'bin/StormLib.dll')})
local archive = assert(require('ffi.stormlib').open(fs.path({lua_path(map_file)}), true))
table.insert(package.searchers, 1, function(name)
    local source = archive:load_file(name .. '.lua')
    if source then return assert(load(source, '@' .. name)) end
end)
assert(require 'shared_room')
for _, name in ipairs({{'room_client.lua', 'room_events.lua'}}) do
    local source = assert(io.open(fs.path({lua_path(files)} .. '/' .. name), 'rb'))
    assert(archive:load_file(name) == source:read('a'))
    source:close()
end
archive:close()
print('MAP_MODULES_OK')
""", encoding='utf-8')
            environment = os.environ.copy()
            environment['PATH'] = str(editor / 'bin') + os.pathsep + environment.get('PATH', '')
            process = subprocess.run([str(root / 'build/Release/lua_probe.exe'),
                str(editor / 'bin/luacore.dll'), str(check)], cwd=work, env=environment,
                capture_output=True, timeout=20)
            self.assertEqual(process.returncode, 0, process.stderr.decode('utf-8', errors='replace'))
            self.assertIn(b'MAP_MODULES_OK', process.stdout)
