local json = require 'room_json'

local M = {}

local CLOUD_REQUEST_KIND = 1009

local FAILURE_NOTICE = '|cffff5555本地联机或云脚本连接已中断，存档加载不能继续。'
    .. '请退出两端后重新启动测试。详情见 mls-player 日志。|r'

---@class MlsDelivery
---@field payload string
---@field player integer

---@param profile RoomProfile
---@param local_player integer
---@return fun(message: string)
local function create_logger(profile, local_player)
    local ffi = require 'ffi'
    ffi.cdef 'unsigned long __stdcall GetTickCount(void);'
    local clock = ffi.load('kernel32')
    local started = clock.GetTickCount()
    local path = profile.logs .. '/mls-player-' .. local_player .. '.log'
    return function(message)
        local file = io.open(path, 'a') or error('cannot open log ' .. path, 0)
        file:write(tostring(clock.GetTickCount() - started), 'ms ', message, '\n')
        file:close()
    end
end

---@param cj table
---@param log fun(message: string)
---@return fun(reason: any)
local function create_failure_reporter(cj, log)
    local display_text = cj.DisplayTimedTextToPlayer
    local shown = false
    return function(reason)
        log('FAILED ' .. tostring(reason))
        if shown then
            return
        end
        shown = true
        display_text(cj.GetLocalPlayer(), 0, 0, 120, FAILURE_NOTICE)
    end
end

---@param japi table
---@param listeners table<string, table[]>
local function hook_trigger_registration(japi, listeners)
    local register = japi.DzTriggerRegisterSyncData
    function japi.DzTriggerRegisterSyncData(trigger, name, server)
        if server and listeners[name] then
            listeners[name][#listeners[name] + 1] = trigger
            return
        end
        return register(trigger, name, server)
    end
end

---@param japi table
---@param cj table
---@param delivery {current: MlsDelivery|nil}
local function hook_sync_readers(japi, cj, delivery)
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

---@param cj table
---@param targets table[]
---@param delivery {current: MlsDelivery|nil}
---@param payload string
---@param player integer
local function execute_triggers(cj, targets, delivery, payload, player)
    delivery.current = {payload = payload, player = player}
    local ok, reason = xpcall(function()
        for _, trigger in ipairs(targets) do
            cj.TriggerExecute(trigger)
        end
    end, debug.traceback)
    delivery.current = nil
    if not ok then
        error(reason, 0)
    end
end

---@param profile RoomProfile
---@return RoomHandle
function M.install(profile)
    local cj = require 'jass.common'
    local japi = require 'jass.japi'
    local local_player = cj.GetPlayerId(cj.GetLocalPlayer())
    local log = create_logger(profile, local_player)
    local report_failure = create_failure_reporter(cj, log)
    local listeners = {war3_data = {}, war3_data_page = {}}
    ---@type {current: MlsDelivery|nil}
    local delivery = {}
    local received = 0
    local sequence = 0

    hook_trigger_registration(japi, listeners)
    hook_sync_readers(japi, cj, delivery)

    local room = require('shared_room').start(profile, {
        on_ready = function()
            log('READY shared cloud room')
        end,
        on_event = function(name, payload, player)
            local targets = listeners[name] or error('unhandled cloud channel: ' .. name, 0)
            execute_triggers(cj, targets, delivery, payload, player)
            received = received + 1
            log(('EVENT seq=%d target=%d channel=%s bytes=%d'):format(
                received, player, name, #payload))
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
    }, {common = cj, japi = japi, native = require('room_ffi').load(profile.dll)})

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
