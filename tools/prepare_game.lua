-- 仅操作工作目录内的测试地图副本，沿用编辑器的 MPQ 和 mapdump 实现。
local config = assert(loadfile('build/game/prepare_config.lua'))()
package.cpath = config.editor .. '/bin/modules/?.dll;' .. package.cpath
package.path = config.editor .. '/share/script/?.lua;' .. config.editor .. '/plugin/w3x2lni/script/?.lua;' .. package.path
local ffi = require 'ffi'
local storm_module = ffi.load(config.editor .. '/bin/StormLib.dll')
require 'filesystem'
local storm = require 'ffi.stormlib'
local map = assert(storm.open(fs.path(config.map), false))
for _, name in ipairs({'main.lua', 'room_profile.lua', 'shared_room.lua', 'room_client.lua', 'room_events.lua', 'room_json.lua', 'room_transport.lua'}) do
    assert(map:add_file(name, fs.path(config.files .. '/' .. name)), name)
end
map:close()
local output = assert(io.open(config.files .. '/map.cfg', 'wb'))
require 'mapdump'(fs.path(config.map), fs.path(config.editor .. '/jass/system/ht'), function(line)
    output:write(line, '\n')
end)
output:close()
print('MAP_READY')
