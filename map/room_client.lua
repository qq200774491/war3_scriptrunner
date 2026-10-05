local json = require 'room_json'
local M = {}
M.__index = M

-- 只拥有本机连接与请求队列；不创建或销毁全端同步句柄。
function M.new(profile, native, common, callbacks)
    return setmetatable({
        _profile = profile, _native = native, _common = common,
        _player = common.GetPlayerId(common.GetLocalPlayer()),
        _status = callbacks.on_status or function() end,
        _relay_message = callbacks.on_relay_message,
        _state = 'waiting', _pending = {},
    }, M)
end

function M:close()
    self._state = 'closed'
    local connection = self._connection
    self._connection = nil
    self._pending = {}
    if connection then connection:close() end
end

function M:failure(reason)
    if self._state == 'closed' then return end
    self:close()
    self._status('failed', tostring(reason))
    if self._player == self._profile.relay then
        -- 固定控制消息避免队列溢出后再次拼接不可控异常文本。
        pcall(self._relay_message, '{"kind":"room_failed","room":"' .. self._profile.room ..
            '","reason":"relay transport failed"}')
    end
end

function M:send(player, request_id, event, payload)
    if self._player ~= player then return end
    if self._state == 'closed' then self._status('request_rejected', 'connection closed'); return end
    if #self._pending >= 128 then self:failure('pending request queue full'); return end
    self._pending[#self._pending + 1] = {request_id, event, payload}
end

function M:poll()
    if self._state == 'closed' then return end
    for player = 0, 15 do
        if self._profile.players[player] and
            self._common.GetPlayerSlotState(self._common.Player(player)) ~= self._common.PLAYER_SLOT_STATE_PLAYING then
            self:failure('game player ' .. player .. ' left; restart requires a new room')
            return
        end
    end
    if not self._connection then
        local player = self._profile.players[self._player]
        if not player then self:close(); return end
        self._connection = self._native.open(player.port)
        self._connection:join(self._profile.room, self._player, player.token)
    end
    for _ = 1, 16 do
        local raw = self._connection:poll()
        if not raw then break end
        local message = json.decode(raw)
        if message.kind == 'ready' then self._state = 'ready' end
        if message.kind == 'room_failed' then self._state = 'waiting' end
        self._status(message.kind, message)
        if self._player == self._profile.relay and
            (message.kind == 'event' or message.kind == 'ready' or message.kind == 'room_failed') then
            self._relay_message(raw)
        end
    end
    if self._state == 'ready' then
        for _, request in ipairs(self._pending) do self._connection:send(table.unpack(request)) end
        self._pending = {}
    end
end

return M
