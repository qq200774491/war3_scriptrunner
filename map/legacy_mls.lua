local M = {}
local json = require 'room_json'

-- 仅本地测试入口显式安装；原地图 MLS 回调经 SRROOM1 锁步触发后执行。
function M.install(profile)
    local cj = require 'jass.common'
    local japi = require 'jass.japi'
    local register = japi.DzTriggerRegisterSyncData
    local sync_data = japi.DzGetTriggerSyncData
    local sync_player = japi.DzGetTriggerSyncPlayer
    local request = japi.RequestExtraBooleanData
    local display_text = cj.DisplayTimedTextToPlayer
    local listeners = {war3_data = {}, war3_data_page = {}}
    local current
    local sequence = 0
    local received = 0
    local ffi = require 'ffi'
    ffi.cdef 'unsigned long __stdcall GetTickCount(void);'
    local clock = ffi.load('kernel32')
    local started = clock.GetTickCount()
    local local_player = cj.GetPlayerId(cj.GetLocalPlayer())
    local function log(message)
        local file = assert(io.open(profile.logs .. '/mls-player-' .. local_player .. '.log', 'a'))
        file:write(tostring(clock.GetTickCount() - started), 'ms ', message, '\n')
        file:close()
    end
    local failure_shown = false
    local function report_failure(reason)
        log('FAILED ' .. tostring(reason))
        if failure_shown then return end
        failure_shown = true
        display_text(cj.GetLocalPlayer(), 0, 0, 120,
            '|cffff5555本地联机或云脚本连接已中断，存档加载不能继续。请退出两端后重新启动测试。详情见 mls-player 日志。|r')
    end
    rawset(japi, 'DzTriggerRegisterSyncData', function(trigger, name, server)
        if server and listeners[name] then
            listeners[name][#listeners[name] + 1] = trigger
            return
        end
        return register(trigger, name, server)
    end)
    rawset(japi, 'DzGetTriggerSyncData', function()
        if current then
            return current.payload
        end
        return sync_data()
    end)
    rawset(japi, 'DzGetTriggerSyncPlayer', function()
        if current then
            return cj.Player(current.player)
        end
        return sync_player()
    end)
    local room = require('shared_room').start(profile, {
        on_ready = function()
            log('READY shared cloud room')
        end,
        on_event = function(name, payload, player)
            local targets = assert(listeners[name], 'unhandled cloud channel: ' .. name)
            current = {payload = payload, player = player}
            local ok, reason = xpcall(function()
                for _, trigger in ipairs(targets) do
                    cj.TriggerExecute(trigger)
                end
            end, debug.traceback)
            current = nil
            assert(ok, reason)
            received = received + 1
            log(('EVENT seq=%d target=%d channel=%s bytes=%d'):format(received, player, name, #payload))
            -- 只记录分页元信息，不把存档内容写入诊断日志。
            local parsed, metadata = pcall(json.decode, payload)
            if parsed and type(metadata) == 'table' and metadata.page then
                log(('PAGE target=%d id=%s label=%s page=%s total=%s'):format(player,
                    tostring(metadata.id), tostring(metadata.label), tostring(metadata.page), tostring(metadata.total)))
            end
        end,
        on_failure = report_failure,
        on_local_status = function(kind, detail)
            if kind == 'failed' or kind == 'room_failed' then
                report_failure(type(detail) == 'table' and detail.reason or detail)
            end
        end,
    }, {common = cj, japi = japi, native = require('room_ffi').load(profile.dll)})
    rawset(japi, 'RequestExtraBooleanData', function(kind, player, label, data, ...)
        if kind ~= 1009 then
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
    end)
    log('INSTALLED local MLS bridge')
    return room
end

return M
