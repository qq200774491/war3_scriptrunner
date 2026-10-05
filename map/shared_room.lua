local transport = require 'room_transport'
local room_events = require 'room_events'
local room_client = require 'room_client'
local M = {}

-- 所有玩家按相同顺序 start/close；业务回调只在同步触发器内执行。
-- on_local_status 属于本机连接状态，只能用于日志或本地 UI。
function M.start(profile, callbacks, injected)
    local cj = injected and injected.common or require 'jass.common'
    local japi = injected and injected.japi or require 'jass.japi'
    local native = injected and injected.native or assert(package.loadlib(profile.dll, 'luaopen_shared_room_native'))()
    local prefix = 'SRROOM1'
    local sync_send = japi.DzSyncData
    if profile.sync_immediate then
        sync_send = assert(japi.DzSyncDataImmediately, 'immediate sync native unavailable')
    end
    local events = room_events.new(profile, callbacks)
    local stream
    local function synchronized(raw)
        local delivered, reason = events:receive(raw)
        if delivered == false then stream:stop(reason) end
    end
    stream = transport.new(profile.room, profile.relay, function(raw)
        local ok, reason = pcall(synchronized, raw)
        if not ok then stream:stop(tostring(reason)) end
    end, function(reason)
        if callbacks.on_failure then callbacks.on_failure(reason) end
    end, profile.sync_chunk)
    local client = room_client.new(profile, native, cj, {
        on_status = callbacks.on_local_status,
        on_relay_message = function(raw) stream:enqueue(raw) end,
    })
    local trigger = cj.CreateTrigger()
    japi.DzTriggerRegisterSyncData(trigger, prefix, false)
    cj.TriggerAddAction(trigger, function()
        stream:receive(cj.GetPlayerId(japi.DzGetTriggerSyncPlayer()), japi.DzGetTriggerSyncData())
    end)
    local timer = cj.CreateTimer()
    local local_player = cj.GetPlayerId(cj.GetLocalPlayer())
    cj.TimerStart(timer, 0.05, true, function()
        local ok, reason = pcall(client.poll, client)
        if not ok then client:failure(reason) end
        if local_player == profile.relay then stream:flush(function(packet) sync_send(prefix, packet) end) end
    end)
    local closed = false
    local api = {}
    function api.send(player, request_id, event, payload)
        client:send(player, request_id, event, payload)
    end
    function api.close()
        if closed then return end
        -- 先标记同步资源终态，避免重复或重入关闭再次释放原生句柄。
        closed = true
        client:close()
        cj.DestroyTimer(timer)
        cj.DestroyTrigger(trigger)
    end
    return api
end

return M
