local cj = require 'jass.common'
local runtime = require 'jass.runtime'
local profile = require 'room_profile'
local local_player = cj.GetPlayerId(cj.GetLocalPlayer())
local log = assert(io.open(profile.logs .. '/player-' .. local_player .. '.log', 'w'))
local function record(text)
    log:write(text, '\n'); log:flush()
    cj.DisplayTimedTextToPlayer(cj.GetLocalPlayer(), 0, 0, 30, text)
end
runtime.error_handle = function(error) record('LUA_ERROR ' .. tostring(error)) end
record('BOOT player=' .. local_player)
local room, events, final = nil, 0, 0
room = require('shared_room').start(profile, {
    on_ready = function()
        record('SYNC_READY')
        room.send(0, 'add-zero', 'increment', '7')
    end,
    on_event = function(event, payload, target)
        record(('SYNC_EVENT %s %s target=%d'):format(event, payload, target))
        if event ~= 'counter' then return end
        events = events + 1
        final = assert(tonumber(payload))
        -- 修改可参与锁步的实际游戏状态，所有客户端执行相同调用。
        cj.SetPlayerState(cj.Player(0), cj.PLAYER_STATE_RESOURCE_GOLD, final)
        if events == 1 then
            room.send(1, 'add-one', 'increment', '5')
        elseif events == 2 then
            room.send(0, 'check', 'query', '')
        elseif events == 3 then
            assert(final == 12)
            record('PASS shared_counter=12 gold=' .. cj.GetPlayerState(cj.Player(0), cj.PLAYER_STATE_RESOURCE_GOLD))
        end
    end,
    on_failure = function(reason) record('SYNC_FAILED ' .. reason) end,
    on_local_status = function(kind, detail)
        if kind == 'failed' then record('LOCAL_FAILED ' .. tostring(detail)) end
    end,
})
