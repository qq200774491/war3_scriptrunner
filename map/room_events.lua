local json = require 'room_json'
local M = {}
M.__index = M

-- 每个同步流独立维护投递序号；二进制与消息形态只在这个边界归一化。
function M.new(profile, callbacks)
    return setmetatable({_profile = profile, _callbacks = callbacks, _delivery = 0}, M)
end

function M:receive(raw)
    local message = json.decode(raw)
    assert(message.room == self._profile.room, 'wrong synchronized room')
    if message.kind == 'ready' then
        if self._callbacks.on_ready then self._callbacks.on_ready() end
        return true
    end
    if message.kind == 'room_failed' then
        return false, message.reason
    end
    assert(message.kind == 'event', 'unknown synchronized message')
    assert(message.delivery == self._delivery + 1, 'cloud delivery gap')
    assert(message.player == -1 or self._profile.players[message.player], 'invalid cloud target')
    assert(type(message.event) == 'string' and type(message.payload) == 'string', 'invalid cloud event')
    local payload = message.payload
    if message.payload_encoding then
        assert(message.payload_encoding == 'hex' and #payload % 2 == 0 and not payload:find('[^%x]'),
            'invalid binary cloud payload')
        payload = payload:gsub('%x%x', function(pair) return string.char(tonumber(pair, 16)) end)
    end
    self._delivery = message.delivery
    -- 归属玩家不用于本机过滤，定向事件仍由全部客户端在锁步上下文执行。
    self._callbacks.on_event(message.event, payload, message.player)
    return true
end

return M
