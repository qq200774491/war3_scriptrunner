-- 只在同步触发器内调用 receive；网络到达时间仅决定中继何时发送。
local M = {}
local MAX_BYTES, MAX_QUEUE = 16384, 262144
local function hex(value)
    return (value:gsub('.', function(char) return ('%02x'):format(char:byte()) end))
end
local function unhex(value)
    return (value:gsub('%x%x', function(pair) return string.char(tonumber(pair, 16)) end))
end
function M.new(room, relay, deliver, failed, chunk)
    assert(type(room) == 'string' and room:match('^[%w%-]+$') and #room <= 36, 'invalid room')
    chunk = chunk or 80
    assert(chunk == 80 or chunk == 350, 'unsupported sync chunk profile')
    local self = {room = room, relay = relay, deliver = deliver, failed = failed, chunk = chunk,
        queue = {}, head = 1, tail = 0, queued = 0, sending = 0, expected = 1, index = 1, parts = {}}
    return setmetatable(self, {__index = M})
end
function M:enqueue(value)
    assert(#value > 0 and #value <= MAX_BYTES, 'sync response exceeds 16 KiB limit')
    assert(self.queued + #value <= MAX_QUEUE, 'sync queue full')
    self.sending = self.sending + 1
    local count = math.ceil(#value / self.chunk)
    for index = 1, count do
        self.tail = self.tail + 1
        local part = value:sub((index - 1) * self.chunk + 1, index * self.chunk)
        self.queue[self.tail] = {('%s|%d|%d|%d|%s'):format(self.room, self.sending, index, count, hex(part)), #part}
    end
    self.queued = self.queued + #value
end
function M:flush(send)
    for _ = 1, 4 do
        local part = self.queue[self.head]
        if not part then return end
        send(part[1])
        self.queued = self.queued - part[2]
        self.queue[self.head] = nil
        self.head = self.head + 1
    end
end
function M:stop(reason)
    if self.stopped then return end
    self.stopped = true
    self.parts = {}
    self.failed(reason)
end
function M:receive(sender, packet)
    if self.stopped or sender ~= self.relay then return end
    local packet_limit = self.chunk == 350 and 799 or 240
    if type(packet) ~= 'string' or #packet > packet_limit then return self:stop('invalid sync packet size') end
    local room, sequence, index, count, data = packet:match('^([%w%-]+)|(%d+)|(%d+)|(%d+)|(%x+)$')
    if room ~= self.room then return end
    sequence, index, count = tonumber(sequence), tonumber(index), tonumber(count)
    if not sequence or not index or not count or #data % 2 ~= 0 or #data > self.chunk * 2 or
        count < 1 or count > math.ceil(MAX_BYTES / self.chunk) or index < 1 or index > count then
        return self:stop('invalid sync fragment')
    end
    if sequence < self.expected then return end
    if sequence ~= self.expected or index > self.index or (self.count and self.count ~= count) then
        return self:stop('sync sequence gap')
    end
    local part = unhex(data)
    if index < self.index then
        if self.parts[index] ~= part then self:stop('conflicting sync fragment') end
        return
    end
    self.parts[index], self.count, self.index = part, count, index + 1
    if index == count then
        local value = table.concat(self.parts)
        if #value > MAX_BYTES then return self:stop('sync response too large') end
        self.expected, self.index, self.parts, self.count = self.expected + 1, 1, {}, nil
        self.deliver(value)
    end
end
return M
