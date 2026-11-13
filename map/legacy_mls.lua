local ffi = require 'ffi'
local cj = require 'jass.common'
local japi = require 'jass.japi'
local errors = require 'room_errors'
local json = require 'room_json'
local room_ffi = require 'room_ffi'
local shared_room = require 'shared_room'

local M = {}

local CLOUD_REQUEST_KIND = 1009

---@class MlsDelivery
---@field payload string
---@field player integer

---@param profile RoomProfile
---@param local_player integer
---@return fun(message: string)
local function create_logger(profile, local_player)
    ffi.cdef 'unsigned long __stdcall GetTickCount(void);'
    local clock = ffi.load('kernel32')
    local started = clock.GetTickCount()
    local path = profile.logs .. '/mls-player-' .. local_player .. '.log'
    return function(message)
        local file = io.open(path, 'a') or errors.fail('log_open', path)
        file:write(tostring(clock.GetTickCount() - started), 'ms ', message, '\n')
        file:close()
    end
end

---@param log fun(message: string)
---@return fun(reason: any)
local function create_failure_reporter(log)
    local display_text = cj.DisplayTimedTextToPlayer
    local shown = false
    return function(reason)
        log('FAILED ' .. tostring(reason))
        if shown then
            return
        end
        shown = true
        display_text(cj.GetLocalPlayer(), 0, 0, 120, errors.text('connection_lost_notice'))
    end
end

---@param listeners table<string, table[]>
local function hook_trigger_registration(listeners)
    local register = japi.DzTriggerRegisterSyncData

    function japi.DzTriggerRegisterSyncData(trigger, name, server)
        if server and listeners[name] then
            listeners[name][#listeners[name] + 1] = trigger
            return
        end
        return register(trigger, name, server)
    end
end

---@param delivery {current: MlsDelivery|nil}
local function hook_sync_readers(delivery)
    local sync_data = japi.DzGetTriggerSyncData
    local sync_player = japi.DzGetTriggerSyncPlayer

    function japi.DzGetTriggerSyncData()
        if delivery.current then
            return delivery.current.payload
        end
        return sync_data()
    end

    function japi.DzGetTriggerSyncPlayer()
        if delivery.current then
            return cj.Player(delivery.current.player)
        end
        return sync_player()
    end
end

---@param payload string
---@param player integer
---@return string|nil
local function page_summary(payload, player)
    local parsed, metadata = pcall(json.decode, payload)
    if not parsed or type(metadata) ~= 'table' or not metadata.page then
        return nil
    end
    return ('PAGE target=%d id=%s label=%s page=%s total=%s'):format(
        player,
        tostring(metadata.id),
        tostring(metadata.label),
        tostring(metadata.page),
        tostring(metadata.total))
end

---@param targets table[]
---@param delivery {current: MlsDelivery|nil}
---@param payload string
---@param player integer
local function execute_triggers(targets, delivery, payload, player)
    delivery.current = {payload = payload, player = player}
    local ok, reason = xpcall(
        function()
            for _, trigger in ipairs(targets) do
                cj.TriggerExecute(trigger)
            end
        end,
        debug.traceback
    )
    delivery.current = nil
    if not ok then
        errors.rethrow(tostring(reason))
    end
end

---@param profile RoomProfile
---@return RoomHandle
function M.install(profile)
    local local_player = cj.GetPlayerId(cj.GetLocalPlayer())
    local log = create_logger(profile, local_player)
    local report_failure = create_failure_reporter(log)
    local listeners = {war3_data = {}, war3_data_page = {}}
    ---@type {current: MlsDelivery|nil}
    local delivery = {}
    local received = 0
    local sequence = 0

    hook_trigger_registration(listeners)
    hook_sync_readers(delivery)

    local native = room_ffi.load(profile.dll)
    local room = shared_room.start(
        profile,
        {
            on_ready = function()
                log('READY shared cloud room')
            end,
            on_event = function(name, payload, player)
                local targets = listeners[name] or errors.fail('event_unhandled_channel', name)
                execute_triggers(targets, delivery, payload, player)
                received = received + 1
                log(('EVENT seq=%d target=%d channel=%s bytes=%d'):format( received, player, name, #payload))
                local summary = page_summary(payload, player)
                if summary then
                    log(summary)
                end
            end,
            on_failure = report_failure,
            on_local_status = function(kind, detail)
                if kind == 'failed' then
                    report_failure(detail)
                elseif kind == 'room_failed' then
                    report_failure(detail.reason)
                end
            end,
        },
        {common = cj, japi = japi, native = native}
    )

    local request = japi.RequestExtraBooleanData

    function japi.RequestExtraBooleanData(kind, player, label, data, ...)
        if kind ~= CLOUD_REQUEST_KIND then
            return request(kind, player, label, data, ...)
        end
        local sender = cj.GetPlayerId(player)
        if sender ~= local_player then
            return false
        end
        sequence = sequence + 1
        room.send(sender, 'mls-' .. sequence, label, data)
        log(('SEND player=%d channel=%s bytes=%d'):format(sender, label, #data))
        return true
    end

    log('INSTALLED local MLS bridge')
    return room
end

return M
