-- 测试 VM 独立编解码原始 JSON 事件；这不是 MLS 可靠消息协议。
local events = {}

---@param client MlsTestClient
---@param requestName string
---@param responseName string
---@param data? table
---@return table
function events.request(client, requestName, responseName, data)
    REQUIRE_EQ(client:send(requestName, json.encode(data or {})), 0)
    local response = client:waitEvent({name = responseName})
    REQUIRE(response, 'missing response: ' .. responseName)
    return json.decode(assert(response).data)
end

---@param ctx MlsTestContext
---@param predicate fun():boolean
function events.waitUntil(ctx, predicate)
    local client = ctx:client(0)
    local deadline = client:now() + client.eventTimeout
    while not predicate() do
        local remaining = deadline - client:now()
        if remaining <= 0 then
            FAIL('condition did not become true before deadline')
        end
        -- 让房间线程继续处理；waitEvent 保留其他名称的业务回包。
        client:waitEvent({name = '__example_wait__', timeout = math.min(0.01, remaining)})
    end
end

return events
