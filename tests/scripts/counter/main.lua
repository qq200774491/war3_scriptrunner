local counter = 0
local loaded = false

RegisterEvent("_roomloaded", function(_, _, payload, player)
    local room = json.decode(payload)
    assert(player == -1)
    assert(#room.players == MsGetRoomPlayerCount())
    loaded = true
    Log.Info("room loaded with %d players", #room.players)
end)

RegisterEvent("increment", function(_, _, payload, player)
    assert(loaded)
    counter = counter + assert(tonumber(payload))
    MsSendMlEvent(player, "counter", tostring(counter))
end)

RegisterEvent("query", function(_, _, _, player)
    assert(loaded)
    MsSendMlEvent(player, "counter", tostring(counter))
end)

RegisterEvent("fail", function()
    error("intentional test failure")
end)

RegisterEvent("later", function(_, _, payload, player)
    Timer.After(0.02, function()
        MsSendMlEvent(player, "later", payload)
    end)
end)

RegisterEvent("broadcast", function(_, _, payload)
    MsSendMlEvent(-1, "broadcast", payload)
end)
