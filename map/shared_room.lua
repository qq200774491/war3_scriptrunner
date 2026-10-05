local json = require 'room_json'
local transport = require 'room_transport'
local M = {}

-- 所有玩家按相同顺序 start。profile 提供同一房间和每个槽位的本机连接凭据。
-- on_event/on_ready/on_failure 在同步上下文执行；on_local_status 仅用于日志或本地 UI。
function M.start(profile, callbacks, injected)
    local cj = injected and injected.common or require 'jass.common'
    local japi = injected and injected.japi or require 'jass.japi'
    local native = injected and injected.native or assert(package.loadlib(profile.dll, 'luaopen_shared_room_native'))()
    local prefix = 'SRROOM1'
    -- 已验证的本地插件可主动提交同步命令；回包仍只在全端同步触发器内执行。
    local sync_send = japi.DzSyncData
    if profile.sync_immediate then
        sync_send = assert(japi.DzSyncDataImmediately, 'immediate sync native unavailable')
    end
    local local_player = cj.GetPlayerId(cj.GetLocalPlayer())
    local connection, local_ready, local_closed = nil, false, false
    local pending = {}
    local delivery = 0
    local function status(kind, detail)
        if callbacks.on_local_status then callbacks.on_local_status(kind, detail) end
    end
    local stream
    local function synchronized(raw)
        local message = json.decode(raw)
        assert(message.room == profile.room, 'wrong synchronized room')
        if message.kind == 'event' then
            assert(message.delivery == delivery + 1, 'cloud delivery gap')
            assert(message.player == -1 or profile.players[message.player], 'invalid cloud target')
            assert(type(message.event) == 'string' and type(message.payload) == 'string', 'invalid cloud event')
            delivery = message.delivery
            local payload = message.payload
            if message.payload_encoding then
                assert(message.payload_encoding == 'hex' and #payload % 2 == 0 and not payload:find('[^%x]'),
                    'invalid binary cloud payload')
                payload = payload:gsub('%x%x', function(pair)
                    return string.char(tonumber(pair, 16))
                end)
            end
            -- 定向回包也在全部客户端执行，player 是结果的归属玩家，不是本地过滤条件。
            callbacks.on_event(message.event, payload, message.player)
        elseif message.kind == 'ready' then
            if callbacks.on_ready then callbacks.on_ready() end
        elseif message.kind == 'room_failed' then
            stream:stop(message.reason)
        else
            error('unknown synchronized message')
        end
    end
    stream = transport.new(profile.room, profile.relay, function(raw)
        local ok, reason = pcall(synchronized, raw)
        if not ok then stream:stop(tostring(reason)) end
    end, function(reason)
        if callbacks.on_failure then callbacks.on_failure(reason) end
    end, profile.sync_chunk)
    local trigger = cj.CreateTrigger()
    japi.DzTriggerRegisterSyncData(trigger, prefix, false)
    cj.TriggerAddAction(trigger, function()
        stream:receive(cj.GetPlayerId(japi.DzGetTriggerSyncPlayer()), japi.DzGetTriggerSyncData())
    end)
    local function local_failure(reason)
        if local_closed then return end
        local_closed = true
        if connection then connection:close() end
        status('failed', tostring(reason))
        if local_player == profile.relay then
            -- 固定控制消息不携带不可控异常文本，避免队列溢出时再次产生巨型回包。
            pcall(stream.enqueue, stream, '{"kind":"room_failed","room":"' .. profile.room .. '","reason":"relay transport failed"}')
        end
    end
    local function poll()
        if local_closed then return end
        if not connection then
            local player = profile.players[local_player]
            if not player then local_closed = true; return end
            connection = native.open(player.port)
            connection:join(profile.room, local_player, player.token)
        end
        for _ = 1, 16 do
            local raw = connection:poll()
            if not raw then break end
            local message = json.decode(raw)
            if message.kind == 'ready' then local_ready = true end
            if message.kind == 'room_failed' then local_ready = false end
            status(message.kind, message)
            if local_player == profile.relay and
                (message.kind == 'event' or message.kind == 'ready' or message.kind == 'room_failed') then
                stream:enqueue(raw)
            end
        end
        if local_ready then
            for _, request in ipairs(pending) do connection:send(table.unpack(request)) end
            pending = {}
        end
    end
    local timer = cj.CreateTimer()
    cj.TimerStart(timer, 0.05, true, function()
        local ok, reason = pcall(poll)
        if not ok then local_failure(reason) end
        if local_player == profile.relay then stream:flush(function(packet) sync_send(prefix, packet) end) end
    end)
    local api = {}
    function api.send(player, request_id, event, payload)
        if local_player ~= player then return end
        if local_closed then status('request_rejected', 'connection closed'); return end
        if #pending >= 128 then local_failure('pending request queue full'); return end
        pending[#pending + 1] = {request_id, event, payload}
    end
    -- 所有玩家在同一同步事件中 close；本地断线处理不销毁同步句柄。
    function api.close()
        local_closed = true
        if connection then connection:close() end
        cj.DestroyTimer(timer)
        cj.DestroyTrigger(trigger)
    end
    return api
end
return M
