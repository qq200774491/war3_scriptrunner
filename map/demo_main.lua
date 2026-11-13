local cj = require 'jass.common'
local runtime = require 'jass.runtime'
local profile = require 'room_profile'

local local_player = cj.GetPlayerId(cj.GetLocalPlayer())
local log_path = profile.logs .. '/player-' .. local_player .. '.log'
local log = io.open(log_path, 'w') or error('cannot open ' .. log_path, 0)

local function record(text)
    log:write(text, '\n')
    log:flush()
    cj.DisplayTimedTextToPlayer(cj.GetLocalPlayer(), 0, 0, 30, text)
end

runtime.error_handle = function(message)
    record('LUA_ERROR ' .. tostring(message))
end

record('BOOT player=' .. local_player)

local room
local counter_events = 0
local counter = 0

room = require('shared_room').start(profile, {
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
        counter = tonumber(payload) or error('invalid counter ' .. payload, 0)
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
    on_failure = function(reason)
        record('SYNC_FAILED ' .. tostring(reason))
    end,
    on_local_status = function(kind, detail)
        if kind == 'failed' then
            record('LOCAL_FAILED ' .. tostring(detail))
        end
    end,
})
