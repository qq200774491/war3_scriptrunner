local json = require 'room_json'

---@class RoomEvents
---@field _profile RoomProfile
---@field _callbacks RoomCallbacks
---@field _delivery integer
local M = {}
M.__index = M

---@param profile RoomProfile
---@param callbacks RoomCallbacks
---@return RoomEvents
function M.new(profile, callbacks)
    return setmetatable({
        _profile = profile,
        _callbacks = callbacks,
        _delivery = 0,
    }, M)
end

local function unhex(text)
    return (text:gsub('%x%x', function(pair)
        return string.char(tonumber(pair, 16))
    end))
end

---@param message RoomMessage
---@return string payload
local function event_payload(message)
    local event, payload = message.event, message.payload
    if type(event) ~= 'string' or type(payload) ~= 'string' then
        error('invalid cloud event', 0)
    end
    if not message.payload_encoding then
        return payload
    end
    if message.payload_encoding ~= 'hex' or #payload % 2 ~= 0 or payload:find('[^%x]') then
        error('invalid binary cloud payload', 0)
    end
    return unhex(payload)
end

---@param raw string
---@return boolean accepted
---@return string|nil reason
function M:receive(raw)
    local message = json.decode(raw)
    if message.room ~= self._profile.room then
        error('wrong synchronized room', 0)
    end
    if message.kind == 'ready' then
        if self._callbacks.on_ready then
            self._callbacks.on_ready()
        end
        return true
    end
    if message.kind == 'room_failed' then
        return false, message.reason
    end
    if message.kind ~= 'event' then
        error('unknown synchronized message', 0)
    end
    if message.delivery ~= self._delivery + 1 then
        error('cloud delivery gap', 0)
    end
    if message.player ~= -1 and not self._profile.players[message.player] then
        error('invalid cloud target', 0)
    end
    local payload = event_payload(message)
    self._delivery = message.delivery
    self._callbacks.on_event(message.event, payload, message.player)
    return true
end

return M
