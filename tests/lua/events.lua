local events = require 'room_events'
local ready, received = 0, {}
local callbacks = {
    on_ready = function() ready = ready + 1 end,
    on_event = function(name, payload, target)
        received[#received + 1] = {name, payload, target}
    end,
}
local function create()
    return events.new({room = 'events', players = {[0] = {}, [1] = {}}}, callbacks)
end
local stream = create()
assert(stream:receive('{"room":"events","kind":"ready"}'))
assert(ready == 1)
assert(stream:receive('{"room":"events","kind":"event","delivery":1,"player":1,"event":"binary","payload":"00ff","payload_encoding":"hex"}'))
assert(received[1][1] == 'binary' and received[1][2] == string.char(0, 255) and received[1][3] == 1)
assert(not pcall(stream.receive, stream, '{"room":"events","kind":"event","delivery":3,"player":0,"event":"gap","payload":""}'))
assert(#received == 1)
local accepted, reason = stream:receive('{"room":"events","kind":"room_failed","reason":"stopped"}')
assert(accepted == false and reason == 'stopped')
accepted, reason = stream:receive('{"room":"events","kind":"room_failed"}')
assert(accepted == false and reason == nil)
local other = create()
assert(other:receive('{"room":"events","kind":"event","delivery":1,"player":-1,"event":"all","payload":"ok"}'))
assert(not pcall(other.receive, other, '{"room":"another","kind":"ready"}'))
assert(not pcall(other.receive, other, '{"room":"events","kind":"event","delivery":2,"player":0,"event":"binary","payload":"gg","payload_encoding":"hex"}'))
print('EVENTS_OK delivery, binary payload, failure and independent streams')

-- 显式失败回调维持原有保护边界，即使回调报错也不能重复结束同步流。
local action, packet, failures, timers, triggers = nil, nil, 0, 0, 0
local api = require('shared_room').start({room = 'failure', relay = 0, players = {}}, {
    on_failure = function() failures = failures + 1; error('failure callback fixture') end,
}, {common = {
    GetLocalPlayer = function() return 0 end,
    GetPlayerId = function(value) return value end,
    CreateTrigger = function() return {} end,
    TriggerAddAction = function(_, callback) action = callback end,
    CreateTimer = function() return {} end,
    TimerStart = function() end,
    DestroyTimer = function() timers = timers + 1 end,
    DestroyTrigger = function() triggers = triggers + 1 end,
}, japi = {
    DzTriggerRegisterSyncData = function() end,
    DzGetTriggerSyncPlayer = function() return 0 end,
    DzGetTriggerSyncData = function() return packet end,
}, native = {}})
local relay = require('room_transport').new('failure', 0, function() end, function() end)
relay:enqueue('{"kind":"room_failed","room":"failure"}')
relay:flush(function(value) packet = value; assert(pcall(action)) end)
relay:enqueue('{"kind":"ready","room":"failure"}')
relay:flush(function(value) packet = value; action() end)
assert(failures == 1)
api.close(); api.close()
assert(timers == 1 and triggers == 1)
