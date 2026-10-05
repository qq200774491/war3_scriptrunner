import asyncio
import json
from pathlib import Path
import tempfile
import unittest

from shared_room.server import RoomServer

ROOT = Path(__file__).resolve().parents[1]
CORE = Path('D:/war3Project/KKWE插件/bin/luacore.dll')
PROBE = ROOT / 'build/Release/lua_probe.exe'


def lua_string(value):
    # Lua 与 JSON 对 ASCII 路径、令牌使用相同转义；Unicode 保持 UTF-8 原文。
    return json.dumps(str(value).replace('\\', '/'), ensure_ascii=False)


class BridgeTests(unittest.IsolatedAsyncioTestCase):
    async def test_synchronized_event_boundary_failure_and_binary_payload(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
            source = (f'package.path = {lua_string(ROOT / "map/?.lua")} .. ";" .. package.path\n'
                f'dofile({lua_string(ROOT / "tests/lua/events.lua")})')
            self.assertIn('EVENTS_OK', await self.run_lua(source, Path(folder), 'test.lua'))

    async def test_game_disconnect_closes_cloud_connection(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
            source = (f'package.path = {lua_string(ROOT / "map/?.lua")} .. ";" .. package.path\n'
                f'dofile({lua_string(ROOT / "tests/lua/roster.lua")})')
            self.assertIn('ROSTER_OK', await self.run_lua(source, Path(folder), 'test.lua'))

    async def test_legacy_mls_request_and_synchronized_callback_boundary(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
            source = (f'package.path = {lua_string(ROOT / "map/?.lua")} .. ";" .. package.path\n'
                f'package.cpath = {lua_string(CORE.parent / "modules/?.dll")} .. ";" .. package.cpath\n'
                f'profile={{logs={lua_string(folder)},dll="unused"}}\n'
                f'dofile({lua_string(ROOT / "tests/lua/legacy.lua")})')
            self.assertIn('LEGACY_OK', await self.run_lua(source, Path(folder), 'test.lua'))

    async def test_game_adapter_with_real_engine_and_skewed_polling(self):
        await self.run_game_adapter(False)

    async def test_ffi_adapter_with_real_engine_and_skewed_polling(self):
        await self.run_game_adapter(True)

    async def run_game_adapter(self, use_ffi):
        room = RoomServer(ROOT / 'build/Release/script_host.exe', ROOT / 'scriptrunner.dll',
                          ROOT / 'tests/scripts/counter', relay_player=0)
        await room.start()
        try:
            profiles = []
            for player in room.players:
                info = room.connection_info(player)
                profiles.append(f'[{player}]={{port={info["port"]},token={lua_string(info["token"])}}}')
            source = (f'package.path = {lua_string(ROOT / "map/?.lua")} .. ";" .. package.path\n'
                      f'package.cpath = {lua_string(CORE.parent / "modules/?.dll")} .. ";" .. package.cpath\n'
                      f'profile={{room={lua_string(room.room_id)},relay=0,ffi={str(use_ffi).lower()},'
                      f'sync_immediate={str(use_ffi).lower()},'
                      f'dll={lua_string(ROOT / "build/Release/shared_room_native.dll")},players={{' +
                      ','.join(profiles) + '}}\n' +
                      f'dofile({lua_string(ROOT / "tests/lua/adapter.lua")})')
            with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
                output = await self.run_lua(source, Path(folder), 'adapter.lua')
                self.assertIn('ADAPTER_OK', output)
            self.assertEqual(room.delivery, 3)
        finally:
            await room.close()

    async def run_lua(self, source, folder, name):
        path = folder / name
        path.write_text(source, encoding='utf-8')
        process = await asyncio.create_subprocess_exec(str(PROBE), str(CORE), str(path),
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        try:
            stdout, stderr = await asyncio.wait_for(process.communicate(), 20)
        except BaseException:
            if process.returncode is None:
                process.kill()
            await process.communicate()
            raise
        self.assertEqual(process.returncode, 0, stderr.decode('utf-8', errors='replace'))
        return stdout.decode('utf-8')

    async def test_lua_json_and_sync_transport(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
            output = await self.run_lua(
                f'package.path = {lua_string(ROOT / "map/?.lua")} .. ";" .. package.path\n'
                f'dofile({lua_string(ROOT / "tests/lua/transport.lua")})', Path(folder), 'test.lua')
            self.assertIn('TRANSPORT_OK', output)

    async def test_actual_luacore_and_native_connections(self):
        room = RoomServer(ROOT / 'build/Release/script_host.exe', ROOT / 'scriptrunner.dll',
                          ROOT / 'tests/scripts/counter', relay_player=0)
        await room.start()
        try:
            with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
                scripts = []
                for player in (0, 1):
                    info = room.connection_info(player)
                    scripts.append(f'''
package.path = {lua_string(ROOT / 'map/?.lua')} .. ';' .. package.path
local json = require 'room_json'
local native = assert(package.loadlib({lua_string(ROOT / 'build/Release/shared_room_native.dll')}, 'luaopen_shared_room_native'))()
assert(type(native) == 'table')
local connection = native.open({info['port']})
connection:join({lua_string(info['room'])}, {player}, {lua_string(info['token'])})
local deadline, sent, events, ack = os.clock() + 8, false, 0, false
while os.clock() < deadline do
    local raw = connection:poll()
    if raw then
        local message = json.decode(raw)
        assert(message.kind ~= 'error' and message.kind ~= 'room_failed', raw)
        if message.kind == 'ready' and not sent then
            connection:send('native-test', 'increment', '{7 if player == 0 else 5}')
            sent = true
        elseif message.kind == 'ack' then ack = true
        elseif message.kind == 'event' then
            assert({player} == 0, 'non-relay received cloud callback')
            events = events + 1
            assert(message.delivery == events)
            if events == 2 then assert(message.payload == '12') end
        end
    end
    -- 非中继保持连接，避免另一端尚未收完事件时提前拆房。
    if {player} == 0 and events == 2 and ack then
        local done = assert(io.open({lua_string(Path(folder) / 'done')}, 'w')); done:write('ok'); done:close()
        break
    end
    if {player} == 1 and ack then
        local done = io.open({lua_string(Path(folder) / 'done')}, 'r')
        if done then done:close(); break end
    end
end
assert(sent and ack)
if {player} == 0 then assert(events == 2) end
connection:close(); connection:close(); connection = nil; collectgarbage('collect')
print('NATIVE_OK')
''')
                results = await asyncio.gather(*(self.run_lua(source, Path(folder), f'client{index}.lua')
                    for index, source in enumerate(scripts)))
                self.assertTrue(all('NATIVE_OK' in result for result in results))
        finally:
            await room.close()
