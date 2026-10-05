-- GA 平台探针：仅转发真实原生调用与异步结果，不伪造回包或存储。
RegisterEvent('example_store_request', function(eventId, eventName, eventData, playerIndex)
    local request = json.decode(eventData)
    if type(MsGAHmSet) ~= 'function' then
        MsSendMlEvent(playerIndex, 'example_store_result', json.encode({unsupported = true}))
        return
    end
    local function reply(result)
        MsSendMlEvent(playerIndex, 'example_store_result', json.encode(result))
    end
    if request.op == 'write' then
        MsGAHmSet('example:records', {[request.field] = request.value}, reply)
    elseif request.op == 'read' then
        MsGAHGetAll('example:records', reply)
    else
        error('unknown example store operation')
    end
end)
