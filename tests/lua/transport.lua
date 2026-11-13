local errors = require 'room_errors'
local json = require 'room_json'
local transport = require 'room_transport'
local value = json.decode('{"text":"中文\\n\\uD83D\\uDE00","arr":[true,false,null,1.25e2]}')
assert(value.text == '中文\n😀' and value.arr[2] == false and value.arr[3] == json.null and value.arr[4] == 125)
for _, invalid in ipairs({'{"x":1,"x":2}', '[1,]', '01', '1.', '1.e2', '+1', '.3', '1e', '{', '"\\uD800"', 'true false'}) do
    assert(not pcall(json.decode, invalid), invalid)
end
local packets, received = {}, {{}, {}}
local writer = transport.new('test-room', 0, function() end, error)
local readers = {}
for index = 1, 2 do
    readers[index] = transport.new('test-room', 0, function(raw) table.insert(received[index], raw) end, error)
end
local payload = ('中文\n|😀'):rep(700)
writer:enqueue(payload)
writer:enqueue('second')
while writer.queued > 0 do writer:flush(function(packet) packets[#packets + 1] = packet; assert(#packet <= 240) end) end
for _, packet in ipairs(packets) do
    for _, reader in ipairs(readers) do
        reader:receive(1, packet) -- 非中继无效。
        reader:receive(0, packet)
        reader:receive(0, packet) -- 同片、整条重复不得二次执行。
    end
end
for _, values in ipairs(received) do assert(#values == 2 and values[1] == payload and values[2] == 'second') end
assert(not pcall(writer.enqueue, writer, ('x'):rep(16385)))
local failure
local broken = transport.new('test-room', 0, error, function(reason) failure = reason end)
broken:receive(0, packets[2])
assert(failure == errors.text('transport_sequence_gap'))
local conflict = transport.new('test-room', 0, error, function(reason) failure = reason end)
conflict:receive(0, packets[1])
conflict:receive(0, (packets[1]:gsub('%x%x$', 'ff')))
assert(failure == errors.text('transport_fragment_conflict'))
-- map已有同步消息上限为 800，350 字节原文加十六进制与头部须保持在该限制以内。
local fast_packets, fast_received = {}, {}
local fast = transport.new('12345678-1234-1234-1234-123456789012', 0, function() end, error, 350)
local fast_reader = transport.new('12345678-1234-1234-1234-123456789012', 0,
    function(raw) fast_received[#fast_received + 1] = raw end, error, 350)
fast:enqueue(payload)
while fast.queued > 0 do
    fast:flush(function(packet)
        assert(#packet < 800)
        fast_packets[#fast_packets + 1] = packet
        fast_reader:receive(0, packet)
    end)
end
assert(fast_received[1] == payload and #fast_packets < #packets / 4)
print('TRANSPORT_OK')
