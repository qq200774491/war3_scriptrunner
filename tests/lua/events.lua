local check = require 'check'
local room_events = require 'room_events'
local room_transport = require 'room_transport'
local shared_room = require 'shared_room'

local function ignore()
end

local ready = 0
local received = {}
local callbacks = {
    on_ready = function()
        ready = ready + 1
    end,
    on_event = function(name, payload, target)
        received[#received + 1] = {name, payload, target}
    end,
}

local function create()
    return room_events.new({room = 'events', players = {[0] = {}, [1] = {}}}, callbacks)
end

local READY = '{"room":"events","kind":"ready"}'
local BINARY_EVENT = '{"room":"events","kind":"event","delivery":1,"player":1,' ..
    '"event":"binary","payload":"00ff","payload_encoding":"hex"}'
local GAP_EVENT = '{"room":"events","kind":"event","delivery":3,"player":0,' ..
    '"event":"gap","payload":""}'
local FAILED_WITH_REASON = '{"room":"events","kind":"room_failed","reason":"stopped"}'
local FAILED_WITHOUT_REASON = '{"room":"events","kind":"room_failed"}'
local BROADCAST_EVENT = '{"room":"events","kind":"event","delivery":1,"player":-1,' ..
    '"event":"all","payload":"ok"}'
local OTHER_ROOM = '{"room":"another","kind":"ready"}'
local BAD_HEX_EVENT = '{"room":"events","kind":"event","delivery":2,"player":0,' ..
    '"event":"binary","payload":"gg","payload_encoding":"hex"}'

local stream = create()
check.is_true(stream:receive(READY))
check.equal(ready, 1)
check.is_true(stream:receive(BINARY_EVENT))
check.equal(received[1][1], 'binary')
check.equal(received[1][2], string.char(0, 255))
check.equal(received[1][3], 1)
check.fails(
    function()
        stream:receive(GAP_EVENT)
    end,
    '序号跳跃的事件'
)
check.equal(#received, 1)

local accepted, reason = stream:receive(FAILED_WITH_REASON)
check.equal(accepted, false)
check.equal(reason, 'stopped')
accepted, reason = stream:receive(FAILED_WITHOUT_REASON)
check.equal(accepted, false)
check.equal(reason, nil)

local other = create()
check.is_true(other:receive(BROADCAST_EVENT))
check.fails(
    function()
        other:receive(OTHER_ROOM)
    end,
    '其他房间的消息'
)
check.fails(
    function()
        other:receive(BAD_HEX_EVENT)
    end,
    '非法的十六进制载荷'
)
print('EVENTS_OK delivery, binary payload, failure and independent streams')

local action
local packet
local failures = 0
local timers = 0
local triggers = 0

local profile = {room = 'failure', relay = 0, players = {}}
local failing_callbacks = {
    on_failure = function()
        failures = failures + 1
        error('失败回调夹具', 0)
    end,
}
local common = {
    GetLocalPlayer = function()
        return 0
    end,
    GetPlayerId = function(value)
        return value
    end,
    CreateTrigger = function()
        return {}
    end,
    TriggerAddAction = function(_, callback)
        action = callback
    end,
    CreateTimer = function()
        return {}
    end,
    TimerStart = ignore,
    DestroyTimer = function()
        timers = timers + 1
    end,
    DestroyTrigger = function()
        triggers = triggers + 1
    end,
}
local japi = {
    DzTriggerRegisterSyncData = ignore,
    DzGetTriggerSyncPlayer = function()
        return 0
    end,
    DzGetTriggerSyncData = function()
        return packet
    end,
}
local api = shared_room.start(profile, failing_callbacks, {common = common, japi = japi, native = {}})

local relay = room_transport.new('failure', 0, ignore, ignore)
relay:enqueue('{"kind":"room_failed","room":"failure"}')
relay:flush(function(value)
    packet = value
    check.is_true(pcall(action))
end)
relay:enqueue('{"kind":"ready","room":"failure"}')
relay:flush(function(value)
    packet = value
    action()
end)
check.equal(failures, 1)

api.close()
api.close()
check.equal(timers, 1)
check.equal(triggers, 1)
