local counter = 0
local loaded = false

RegisterEvent("_roomloaded", function(_, _, payload, player)
    local room = json.decode(payload)
    if player ~= -1 then
        error("房间就绪事件的目标应为所有玩家", 0)
    end
    if #room.players ~= MsGetRoomPlayerCount() then
        error("房间玩家数不一致", 0)
    end
    loaded = true
    Log.Info("房间已加载，玩家数 %d", #room.players)
end)

RegisterEvent("increment", function(_, _, payload, player)
    if not loaded then
        error("房间尚未加载就收到 increment", 0)
    end
    local amount = tonumber(payload)
    if not amount then
        error("increment 的载荷不是数字：" .. payload, 0)
    end
    counter = counter + amount
    MsSendMlEvent(player, "counter", tostring(counter))
end)

RegisterEvent("query", function(_, _, _, player)
    if not loaded then
        error("房间尚未加载就收到 query", 0)
    end
    MsSendMlEvent(player, "counter", tostring(counter))
end)

RegisterEvent("fail", function()
    error("故意触发的失败", 0)
end)

RegisterEvent("later", function(_, _, payload, player)
    Timer.After(0.02, function()
        MsSendMlEvent(player, "later", payload)
    end)
end)

RegisterEvent("broadcast", function(_, _, payload)
    MsSendMlEvent(-1, "broadcast", payload)
end)
