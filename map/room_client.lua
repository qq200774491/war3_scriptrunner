local json = require 'room_json'
local errors = require 'room_errors'

local MAX_PENDING = 128
local MAX_MESSAGES_PER_POLL = 16
local MAX_PLAYER_SLOT = 15
local RELAYED_KINDS = {event = true, ready = true, room_failed = true}
local FAILURE_MESSAGE = '{"kind":"room_failed","room":"%s","reason":"%s"}'

---@class RoomClient
---@field _profile RoomProfile
---@field _native NativeRoom
---@field _common table
---@field _player integer
---@field _on_status fun(kind: string, detail: any)|nil
---@field _relay_message fun(raw: string)
---@field _state 'waiting'|'ready'|'closed'
---@field _pending table[]
---@field _connection NativeConnection|nil
local M = {}
M.__index = M

---@param profile RoomProfile
---@param native NativeRoom
---@param common table
---@param callbacks ClientCallbacks
---@return RoomClient
function M.new(profile, native, common, callbacks)
    local o = {
        _profile = profile,
        _native = native,
        _common = common,
        _player = common.GetPlayerId(common.GetLocalPlayer()),
        _on_status = callbacks.on_status,
        _relay_message = callbacks.on_relay_message,
        _state = 'waiting',
        _pending = {},
    }
    setmetatable(o, M)
    return o
end

---@param kind string
---@param detail any
function M:report_status(kind, detail)
    if self._on_status then
        self._on_status(kind, detail)
    end
end

function M:close()
    self._state = 'closed'
    local connection = self._connection
    self._connection = nil
    self._pending = {}
    if connection then
        connection:close()
    end
end

---@param reason any
function M:failure(reason)
    if self._state == 'closed' then
        return
    end
    self:close()
    self:report_status('failed', tostring(reason))
    if self._player == self._profile.relay then
        local notice = FAILURE_MESSAGE:format(self._profile.room, errors.text('client_relay_failed'))
        pcall(self._relay_message, notice)
    end
end

---@param player integer
---@param request_id string
---@param event string
---@param payload string
function M:send(player, request_id, event, payload)
    if self._player ~= player then
        return
    end
    if self._state == 'closed' then
        self:report_status('request_rejected', errors.text('client_closed'))
        return
    end
    if #self._pending >= MAX_PENDING then
        self:failure(errors.text('client_queue_full'))
        return
    end
    self._pending[#self._pending + 1] = {request_id, event, payload}
end

---@return integer|nil
function M:departed_player()
    local common = self._common
    for player = 0, MAX_PLAYER_SLOT do
        if self._profile.players[player] then
            local state = common.GetPlayerSlotState(common.Player(player))
            if state ~= common.PLAYER_SLOT_STATE_PLAYING then
                return player
            end
        end
    end
end

---@return boolean connected
function M:ensure_connection()
    if self._connection then
        return true
    end
    local player = self._profile.players[self._player]
    if not player then
        self:close()
        return false
    end
    self._connection = self._native.open(player.port)
    self._connection:join(self._profile.room, self._player, player.token)
    return true
end

---@param raw string
function M:handle_message(raw)
    ---@type RoomMessage
    local message = json.decode(raw)
    if message.kind == 'ready' then
        self._state = 'ready'
    elseif message.kind == 'room_failed' then
        self._state = 'waiting'
    end
    self:report_status(message.kind, message)
    if self._player == self._profile.relay and RELAYED_KINDS[message.kind] then
        self._relay_message(raw)
    end
end

function M:flush_pending()
    for _, request in ipairs(self._pending) do
        self._connection:send(table.unpack(request))
    end
    self._pending = {}
end

function M:poll()
    if self._state == 'closed' then
        return
    end
    local departed = self:departed_player()
    if departed then
        self:failure(errors.text('client_player_left', departed))
        return
    end
    if not self:ensure_connection() then
        return
    end
    for _ = 1, MAX_MESSAGES_PER_POLL do
        local raw = self._connection:poll()
        if not raw then
            break
        end
        self:handle_message(raw)
    end
    if self._state == 'ready' then
        self:flush_pending()
    end
end

return M
