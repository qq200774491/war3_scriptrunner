local cj = require 'jass.common'
local japi = require 'jass.japi'
local runtime = require 'jass.runtime'
local errors = require 'room_errors'
local profile = require 'room_profile'
local shared_room = require 'shared_room'

local local_player = cj.GetPlayerId(cj.GetLocalPlayer())
local log_path = profile.logs .. '/player-' .. local_player .. '.log'
local log = io.open(log_path, 'w') or errors.fail('log_open', log_path)

local function record(text)
    log:write(text, '\n')
    log:flush()
    cj.DisplayTimedTextToPlayer(cj.GetLocalPlayer(), 0, 0, 30, text)
end

runtime.error_handle = function(message)
    record('LUA_ERROR ' .. tostring(message))
end

record('BOOT player=' .. local_player)

local loader, reason = package.loadlib(profile.dll, 'luaopen_shared_room_native')
if not loader then
    errors.fail('load_native', reason)
end
local native = loader()

local room
local counter_events = 0
---@type number
local counter = 0

room = shared_room.start(
    profile,
    {
        on_ready = function()
            record('SYNC_READY')
            room.send(0, 'add-zero', 'increment', '7')
        end,
        on_event = function(event, payload, target)
            record(('SYNC_EVENT %s %s target=%d'):format(event, payload, target))
            if event ~= 'counter' then
                return
            end
            counter_events = counter_events + 1
            counter = tonumber(payload) or errors.fail('demo_counter', payload)
            cj.SetPlayerState(cj.Player(0), cj.PLAYER_STATE_RESOURCE_GOLD, counter)
            if counter_events == 1 then
                room.send(1, 'add-one', 'increment', '5')
            elseif counter_events == 2 then
                room.send(0, 'check', 'query', '')
            elseif counter_events == 3 then
                local gold = cj.GetPlayerState(cj.Player(0), cj.PLAYER_STATE_RESOURCE_GOLD)
                if counter == 12 then
                    record('PASS shared_counter=12 gold=' .. gold)
                else
                    record('FAIL shared_counter=' .. counter .. ' gold=' .. gold)
                end
            end
        end,
        on_failure = function(failure)
            record('SYNC_FAILED ' .. tostring(failure))
        end,
        on_local_status = function(kind, detail)
            if kind == 'failed' then
                record('LOCAL_FAILED ' .. tostring(detail))
            end
        end,
    },
    {common = cj, japi = japi, native = native}
)
