local transport = require 'room_transport'
local room_events = require 'room_events'
local room_client = require 'room_client'
local errors = require 'room_errors'

local M = {}

local SYNC_PREFIX = 'SRROOM1'
local POLL_INTERVAL = 0.05

---@param profile RoomProfile
---@param callbacks RoomCallbacks
---@param dependencies RoomDependencies
---@return RoomHandle
function M.start(profile, callbacks, dependencies)
    local cj = dependencies.common
    local japi = dependencies.japi
    local native = dependencies.native
    local sync_send = japi.DzSyncData
    if profile.sync_immediate then
        sync_send = japi.DzSyncDataImmediately
        if not sync_send then
            errors.fail('sync_unavailable')
        end
    end

    local events = room_events.new(profile, callbacks)
    ---@type RoomTransport
    local stream

    local function synchronized(raw)
        local delivered, reason = events:receive(raw)
        if delivered == false then
            stream:stop(reason)
        end
    end

    stream = transport.new(
        profile.room,
        profile.relay,
        function(raw)
            local ok, reason = pcall(synchronized, raw)
            if not ok then
                stream:stop(tostring(reason))
            end
        end,
        function(reason)
            if callbacks.on_failure then
                callbacks.on_failure(reason)
            end
        end,
        profile.sync_chunk
    )

    local client = room_client.new(
        profile,
        native,
        cj,
        {
            on_status = callbacks.on_local_status,
            on_relay_message = function(raw)
                stream:enqueue(raw)
            end,
        }
    )

    local trigger = cj.CreateTrigger()
    japi.DzTriggerRegisterSyncData(trigger, SYNC_PREFIX, false)
    cj.TriggerAddAction(
        trigger,
        function()
            local sender = cj.GetPlayerId(japi.DzGetTriggerSyncPlayer())
            stream:receive(sender, japi.DzGetTriggerSyncData())
        end
    )

    local timer = cj.CreateTimer()
    local local_player = cj.GetPlayerId(cj.GetLocalPlayer())
    cj.TimerStart(
        timer,
        POLL_INTERVAL,
        true,
        function()
            local ok, reason = pcall(client.poll, client)
            if not ok then
                client:failure(reason)
            end
            if local_player == profile.relay then
                stream:flush(function(packet)
                    sync_send(SYNC_PREFIX, packet)
                end)
            end
        end
    )

    local closed = false
    local handle = {}

    function handle.send(player, request_id, event, payload)
        client:send(player, request_id, event, payload)
    end

    function handle.close()
        if closed then
            return
        end
        closed = true
        client:close()
        cj.DestroyTimer(timer)
        cj.DestroyTrigger(trigger)
    end

    return handle
end

return M
