---@diagnostic disable: undefined-global


local load_config, config_error = loadfile('build/game/launch_config.lua')
if not load_config then
    error(config_error, 0)
end
local config = load_config()

package.cpath = config.editor .. '/bin/modules/?.dll;' .. package.cpath
require 'filesystem'
require 'sys'

local process = sys.process()
process:inject(fs.path(config.files .. '/yd_loader.dll'))
if not process:create(nil, config.command, fs.path(config.game)) then
    error('魔兽启动失败', 0)
end

if os.getenv('SR_MONITOR_GAME') == '1' then
    local load_monitor, monitor_error = loadfile('tools/game_control.lua')
    if not load_monitor then
        error(monitor_error, 0)
    end
    local monitor = load_monitor()
    local code = monitor(process)
    print('GAME_EXIT ' .. tostring(code))
    io.stdout:flush()
else
    print('GAME_PID ' .. process:id())
    io.stdout:flush()
end

process:close()
print('GAME_LAUNCHED')
