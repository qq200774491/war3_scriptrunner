---@diagnostic disable: undefined-global
local check = require 'check'
local room_ffi = require 'room_ffi'
local shared_room = require 'shared_room'

local native
if profile.ffi then
    native = room_ffi.load(profile.dll)
else
    local loader, reason = package.loadlib(profile.dll, 'luaopen_shared_room_native')
    if not loader then
        error(reason, 0)
    end
    native = loader()
end

local clients = {}
local packets = {}
local in_sync_context = false

local function create(player)
    local state = {gold = 0, events = {}, ready = 0, player = player, statuses = {}}

    local cj = {
        GetPlayerId = function(value)
            return value
        end,
        GetLocalPlayer = function()
            return player
        end,
        Player = function(value)
            return value
        end,
        GetPlayerSlotState = function()
            return 1
        end,
        PLAYER_SLOT_STATE_PLAYING = 1,
        CreateTrigger = function()
            return {}
        end,
        TriggerAddAction = function(_, action)
            state.action = action
        end,
        CreateTimer = function()
            return {}
        end,
        TimerStart = function(_, _, _, callback)
            state.tick = callback
        end,
        DestroyTimer = function()
            state.timer_closed = (state.timer_closed or 0) + 1
        end,
        DestroyTrigger = function()
            state.trigger_closed = (state.trigger_closed or 0) + 1
        end,
    }

    local japi = {
        DzTriggerRegisterSyncData = function(_, prefix, server)
            check.equal(prefix, 'SRROOM1')
            check.equal(server, false)
        end,
        DzGetTriggerSyncPlayer = function()
            return 0
        end,
        DzGetTriggerSyncData = function()
            return state.packet
        end,
        DzSyncData = function(_, packet)
            check.equal(player, 0)
            check.is_true(not profile.sync_immediate)
            packets[#packets + 1] = packet
        end,
        DzSyncDataImmediately = function(_, packet)
            check.equal(player, 0)
            check.is_true(profile.sync_immediate)
            packets[#packets + 1] = packet
        end,
    }

    local room
    local callbacks = {
        on_ready = function()
            check.is_true(in_sync_context, '就绪回调跑到了同步上下文之外')
            state.ready = state.ready + 1
            room.send(0, 'first', 'increment', '7')
        end,
        on_event = function(event, payload, target)
            check.is_true(in_sync_context, '业务事件跑到了同步上下文之外')
            check.equal(event, 'counter')
            state.events[#state.events + 1] = target .. ':' .. payload
            state.gold = tonumber(payload)
            if #state.events == 1 then
                room.send(1, 'second', 'increment', '5')
            end
            if #state.events == 2 then
                room.send(0, 'first', 'increment', '7')
                room.send(0, 'check', 'query', '')
            end
        end,
        on_failure = function(reason)
            error(reason, 0)
        end,
        on_local_status = function(kind)
            check.is_true(not in_sync_context)
            state.statuses[kind] = true
        end,
    }
    room = shared_room.start(profile, callbacks, {common = cj, japi = japi, native = native})
    state.room = room
    return state
end

clients[1] = create(0)
clients[2] = create(1)

local deadline = os.clock() + 8
local tick = 0
while os.clock() < deadline do
    tick = tick + 1
    clients[1].tick()
    if tick % 17 == 0 then
        clients[2].tick()
    end
    local ready = packets
    packets = {}
    in_sync_context = true
    for _, packet in ipairs(ready) do
        for _, client in ipairs(clients) do
            client.packet = packet
            client.action()
        end
    end
    in_sync_context = false
    if #clients[1].events == 3 and #clients[2].events == 3 then
        break
    end
end

for _, client in ipairs(clients) do
    check.equal(client.ready, 1)
    check.equal(client.gold, 12)
    check.equal(table.concat(client.events, ','), '0:7,1:12,0:12')
    client.room.close()
    client.room.close()
    check.equal(client.timer_closed, 1)
    check.equal(client.trigger_closed, 1)
end

print('ADAPTER_OK 0:7,1:12,0:12 gold=12 both-clients')
