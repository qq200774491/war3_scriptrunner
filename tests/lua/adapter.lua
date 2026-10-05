local native
if profile.ffi then
    native = require('room_ffi').load(profile.dll)
else
    native = assert(package.loadlib(profile.dll, 'luaopen_shared_room_native'))()
end
local adapter = require 'shared_room'
local clients, packets, context = {}, {}, false
local function create(player)
    local state = {gold = 0, events = {}, ready = 0, player = player, statuses = {}}
    local cj = {
        GetPlayerId = function(value) return value end,
        GetLocalPlayer = function() return player end,
        CreateTrigger = function() return {} end,
        TriggerAddAction = function(_, action) state.action = action end,
        CreateTimer = function() return {} end,
        TimerStart = function(_, _, _, callback) state.tick = callback end,
        DestroyTimer = function() state.timer_closed = true end,
        DestroyTrigger = function() state.trigger_closed = true end,
    }
    local japi = {
        DzTriggerRegisterSyncData = function(_, prefix, server) assert(prefix == 'SRROOM1' and server == false) end,
        DzGetTriggerSyncPlayer = function() return 0 end,
        DzGetTriggerSyncData = function() return state.packet end,
        DzSyncData = function(_, packet)
            assert(player == 0 and not profile.sync_immediate)
            packets[#packets + 1] = packet
        end,
        DzSyncDataImmediately = function(_, packet)
            assert(player == 0 and profile.sync_immediate)
            packets[#packets + 1] = packet
        end,
    }
    local room
    room = adapter.start(profile, {
        on_ready = function()
            assert(context, 'ready escaped synchronized context')
            state.ready = state.ready + 1
            room.send(0, 'first', 'increment', '7')
        end,
        on_event = function(event, payload, target)
            assert(context, 'business event escaped synchronized context')
            assert(event == 'counter')
            state.events[#state.events + 1] = target .. ':' .. payload
            state.gold = tonumber(payload)
            if #state.events == 1 then room.send(1, 'second', 'increment', '5') end
            if #state.events == 2 then
                room.send(0, 'first', 'increment', '7') -- 幂等重试不能再次增加。
                room.send(0, 'check', 'query', '')
            end
        end,
        on_failure = function(reason) error(reason) end,
        on_local_status = function(kind) assert(not context); state.statuses[kind] = true end,
    }, {common = cj, japi = japi, native = native})
    state.room = room
    return state
end
clients[1], clients[2] = create(0), create(1)
local deadline, tick = os.clock() + 8, 0
while os.clock() < deadline do
    tick = tick + 1
    clients[1].tick()
    -- 刻意让第二端轮询更慢，检验不同本地到达时间不会改变业务结果。
    if tick % 17 == 0 then clients[2].tick() end
    local ready = packets
    packets = {}
    context = true
    for _, packet in ipairs(ready) do
        for _, client in ipairs(clients) do client.packet = packet; client.action() end
    end
    context = false
    if #clients[1].events == 3 and #clients[2].events == 3 then break end
end
for _, client in ipairs(clients) do
    assert(client.ready == 1 and client.gold == 12)
    assert(table.concat(client.events, ',') == '0:7,1:12,0:12')
    client.room.close()
    assert(client.timer_closed and client.trigger_closed)
end
print('ADAPTER_OK 0:7,1:12,0:12 gold=12 both-clients')
