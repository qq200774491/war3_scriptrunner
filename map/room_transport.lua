local MAX_BYTES = 16384
local MAX_QUEUE = 262144
local FLUSH_PER_TICK = 4

local PACKET_LIMITS = {[80] = 240, [350] = 799}

local function hex(value)
    return (value:gsub('.', function(char)
        return ('%02x'):format(char:byte())
    end))
end

local function unhex(value)
    return (value:gsub('%x%x', function(pair)
        return string.char(tonumber(pair, 16))
    end))
end

---@class SyncFragment
---@field sequence integer
---@field index integer
---@field count integer
---@field data string

---@param packet string
---@param room string
---@param chunk integer
---@return SyncFragment|nil fragment
---@return string|nil reason
local function parse_fragment(packet, room, chunk)
    local packet_room, sequence, index, count, data =
        packet:match('^([%w%-]+)|(%d+)|(%d+)|(%d+)|(%x+)$')
    if packet_room ~= room then
        return nil
    end
    sequence, index, count = tonumber(sequence), tonumber(index), tonumber(count)
    local valid = #data % 2 == 0
        and #data <= chunk * 2
        and count >= 1
        and count <= math.ceil(MAX_BYTES / chunk)
        and index >= 1
        and index <= count
    if not valid then
        return nil, 'invalid sync fragment'
    end
    return {sequence = sequence, index = index, count = count, data = data}
end

---@class RoomTransport
---@field queued integer
---@field _room string
---@field _relay integer
---@field _deliver fun(value: string)
---@field _failed fun(reason: string|nil)
---@field _chunk integer
---@field _packet_limit integer
---@field _queue table<integer, {packet: string, size: integer}>
---@field _head integer
---@field _tail integer
---@field _sending integer
---@field _expected integer
---@field _index integer
---@field _count integer|nil
---@field _parts table<integer, string>
---@field _stopped boolean
local M = {}
M.__index = M

---@param room string
---@param relay integer
---@param deliver fun(value: string)
---@param failed fun(reason: string|nil)
---@param chunk 80|350|nil
---@return RoomTransport
function M.new(room, relay, deliver, failed, chunk)
    if not room:match('^[%w%-]+$') or #room > 36 then
        error('invalid room', 0)
    end
    chunk = chunk or 80
    local packet_limit = PACKET_LIMITS[chunk]
    if not packet_limit then
        error('unsupported sync chunk profile', 0)
    end
    return setmetatable({
        queued = 0,
        _room = room,
        _relay = relay,
        _deliver = deliver,
        _failed = failed,
        _chunk = chunk,
        _packet_limit = packet_limit,
        _queue = {},
        _head = 1,
        _tail = 0,
        _sending = 0,
        _expected = 1,
        _index = 1,
        _parts = {},
    }, M)
end

---@param value string
function M:enqueue(value)
    if #value == 0 or #value > MAX_BYTES then
        error('sync response exceeds 16 KiB limit', 0)
    end
    if self.queued + #value > MAX_QUEUE then
        error('sync queue full', 0)
    end
    self._sending = self._sending + 1
    local count = math.ceil(#value / self._chunk)
    for index = 1, count do
        local part = value:sub((index - 1) * self._chunk + 1, index * self._chunk)
        self._tail = self._tail + 1
        self._queue[self._tail] = {
            packet = ('%s|%d|%d|%d|%s'):format(self._room, self._sending, index, count, hex(part)),
            size = #part,
        }
    end
    self.queued = self.queued + #value
end

---@param send fun(packet: string)
function M:flush(send)
    for _ = 1, FLUSH_PER_TICK do
        local entry = self._queue[self._head]
        if not entry then
            return
        end
        send(entry.packet)
        self.queued = self.queued - entry.size
        self._queue[self._head] = nil
        self._head = self._head + 1
    end
end

---@param reason string|nil
function M:stop(reason)
    if self._stopped then
        return
    end
    self._stopped = true
    self._parts = {}
    self._failed(reason)
end

function M:finish_response()
    local value = table.concat(self._parts)
    if #value > MAX_BYTES then
        return self:stop('sync response too large')
    end
    self._expected = self._expected + 1
    self._index = 1
    self._parts = {}
    self._count = nil
    self._deliver(value)
end

---@param fragment SyncFragment
function M:accept_fragment(fragment)
    local index, count = fragment.index, fragment.count
    if fragment.sequence < self._expected then
        return
    end
    if fragment.sequence ~= self._expected
        or index > self._index
        or (self._count and self._count ~= count)
    then
        return self:stop('sync sequence gap')
    end
    local part = unhex(fragment.data)
    if index < self._index then
        if self._parts[index] ~= part then
            self:stop('conflicting sync fragment')
        end
        return
    end
    self._parts[index] = part
    self._count = count
    self._index = index + 1
    if index == count then
        self:finish_response()
    end
end

---@param sender integer
---@param packet string
function M:receive(sender, packet)
    if self._stopped or sender ~= self._relay then
        return
    end
    if type(packet) ~= 'string' or #packet > self._packet_limit then
        return self:stop('invalid sync packet size')
    end
    local fragment, reason = parse_fragment(packet, self._room, self._chunk)
    if reason then
        return self:stop(reason)
    end
    if fragment then
        self:accept_fragment(fragment)
    end
end

return M
