-- 使用编辑器原有 process:inject/create 接口，且不带 -scriptrunnerfolder。
local config = assert(loadfile('build/game/launch_config.lua'))()
package.cpath = config.editor .. '/bin/modules/?.dll;' .. package.cpath
require 'filesystem'
require 'sys'
local process = sys.process()
process:inject(fs.path(config.files .. '/yd_loader.dll'))
assert(process:create(nil, config.command, fs.path(config.game)), 'Warcraft launch failed')
if os.getenv('SR_MONITOR_GAME') == '1' then
    local monitor = assert(loadfile('tools/game_control.lua'))()
    local code = monitor(process)
    print('GAME_EXIT ' .. tostring(code))
    io.stdout:flush()
else
    print('GAME_PID ' .. process:id())
    io.stdout:flush()
end
process:close()
print('GAME_LAUNCHED')
