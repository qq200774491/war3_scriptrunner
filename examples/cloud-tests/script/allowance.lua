-- 虚构的每日额度服务；数值、字段和事件名均为示例自定义。
local allowance = {}

function allowance.today()
    return os.date('%Y%m%d')
end

---@param playerIndex integer
---@return table
function allowance.read(playerIndex)
    local raw = MsGetScriptArchive(playerIndex)
    return raw == '' and {balance = 0, claimedDay = ''} or json.decode(raw)
end

RegisterEvent('example_allowance_claim', function(eventId, eventName, eventData, playerIndex)
    local state = allowance.read(playerIndex)
    local day = allowance.today()
    local granted = state.claimedDay ~= day
    if granted then
        state.balance = state.balance + 7
        state.claimedDay = day
        assert(MsSaveScriptArchive(playerIndex, json.encode(state)) == 0)
    end
    MsSendMlEvent(playerIndex, 'example_allowance_result', json.encode {
        granted = granted, balance = state.balance, claimedDay = state.claimedDay,
    })
end)

return allowance
