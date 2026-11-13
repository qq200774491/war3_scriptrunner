---@diagnostic disable: undefined-global, missing-fields, redundant-parameter
local check = require 'check'
local errors = require 'room_errors'
local json = require 'room_json'
local transport = require 'room_transport'

local ROOM = 'test-room'
local FAST_ROOM = '12345678-1234-1234-1234-123456789012'

local function ignore()
end

local value = json.decode('{"text":"中文\\n\\uD83D\\uDE00","arr":[true,false,null,1.25e2]}')
check.equal(value.text, '中文\n😀')
check.equal(value.arr[2], false)
check.equal(value.arr[3], json.null)
check.equal(value.arr[4], 125)

local invalid_documents = {
    '{"x":1,"x":2}', '[1,]', '01', '1.', '1.e2', '+1', '.3', '1e', '{', '"\\uD800"', 'true false',
}
for _, document in ipairs(invalid_documents) do
    check.fails(
        function()
            json.decode(document)
        end,
        document
    )
end

local packets = {}
local received = {{}, {}}
local writer = transport.new(ROOM, 0, ignore, error)
local readers = {}
for index = 1, 2 do
    readers[index] = transport.new(
        ROOM,
        0,
        function(raw)
            table.insert(received[index], raw)
        end,
        error
    )
end

local payload = ('中文\n|😀'):rep(700)
writer:enqueue(payload)
writer:enqueue('second')
while writer.queued > 0 do
    writer:flush(function(packet)
        packets[#packets + 1] = packet
        check.is_true(#packet <= 240)
    end)
end

for _, packet in ipairs(packets) do
    for _, reader in ipairs(readers) do
        reader:receive(1, packet)
        reader:receive(0, packet)
        reader:receive(0, packet)
    end
end
for _, values in ipairs(received) do
    check.equal(#values, 2)
    check.equal(values[1], payload)
    check.equal(values[2], 'second')
end

check.fails(
    function()
        writer:enqueue(('x'):rep(16385))
    end,
    '超过 16 KiB 的响应'
)

local failure
local function remember(reason)
    failure = reason
end

local broken = transport.new(ROOM, 0, error, remember)
broken:receive(0, packets[2])
check.equal(failure, errors.text('transport_sequence_gap'))

local conflict = transport.new(ROOM, 0, error, remember)
conflict:receive(0, packets[1])
local tampered = packets[1]:gsub('%x%x$', 'ff')
conflict:receive(0, tampered)
check.equal(failure, errors.text('transport_fragment_conflict'))

local fast_packets = {}
local fast_received = {}
local fast = transport.new(FAST_ROOM, 0, ignore, error, 350)
local fast_reader = transport.new(
    FAST_ROOM,
    0,
    function(raw)
        fast_received[#fast_received + 1] = raw
    end,
    error,
    350
)
fast:enqueue(payload)
while fast.queued > 0 do
    fast:flush(function(packet)
        check.is_true(#packet < 800)
        fast_packets[#fast_packets + 1] = packet
        fast_reader:receive(0, packet)
    end)
end
check.equal(fast_received[1], payload)
check.is_true(#fast_packets < #packets / 4)

print('TRANSPORT_OK')
