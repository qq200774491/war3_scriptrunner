local events = require 'helpers.events'

---@param ctx MlsTestContext
---@param data table
---@return table
local function request(ctx, data)
    return events.request(ctx:client(0), 'example_store_request', 'example_store_result', data)
end

---@param ctx MlsTestContext
local function startWrite(ctx)
    REQUIRE_EQ(ctx:client(0):send('example_store_request', json.encode {
        op = 'write', field = 'record-a', value = 'sample-a',
    }), 0)
    events.waitUntil(ctx, function() return ctx:gaReplyState().held == 1 end)
end

TEST_CASE('共享记录可播种并由云端异步读取', '[example][ga]', function(ctx)
    REQUIRE(ctx:gaHmSet('example:records', {['record-a'] = 'seed-value'}))
    local result = request(ctx, {op = 'read'})
    CHECK_EQ(result.errnu, 0)
    CHECK_EQ(result.data.value, {['record-a'] = 'seed-value'})
end)

TEST_CASE('共享记录写入故障不改变原数据且可重试', '[example][ga][fault]', function(ctx)
    REQUIRE(ctx:gaHmSet('example:records', {['record-a'] = 'original'}))
    ctx:setGaFault({op = 'MsGAHmSet', errnu = 2, count = 1})
    local data = {op = 'write', field = 'record-a', value = 'updated'}
    CHECK_EQ(request(ctx, data).errnu, 2)
    CHECK_EQ(ctx:gaHGetAll('example:records'), {['record-a'] = 'original'})
    CHECK_EQ(request(ctx, data).errnu, 0)
    CHECK_EQ(ctx:gaHGetAll('example:records'), {['record-a'] = 'updated'})
end)

TEST_CASE('共享记录暂扣回包期间其他房间仍能读写', '[example][ga][multi-room]', function(ctx)
    local other = ctx:createRoom({players = {{index = 0, name = 'ExamplePeer', platform = {guid = 9002}}}})
    CHECK_NE(ctx.roomId, other.roomId)
    ctx:holdGaReplies('MsGAHmSet')
    startWrite(ctx)
    CHECK_EQ(request(other, {op = 'read'}).data.value, {['record-a'] = 'sample-a'})
    CHECK_EQ(request(other, {op = 'write', field = 'record-b', value = 'sample-b'}).errnu, 0)
    CHECK_EQ(other:gaReplyState().held, 0)
    REQUIRE_EQ(ctx:releaseGaReplies(), 1)
    local response = ctx:client(0):waitEvent({name = 'example_store_result'})
    REQUIRE(response)
    CHECK_EQ(json.decode(assert(response).data).errnu, 0)
    CHECK_EQ(ctx:gaHGetAll('example:records'), {['record-a'] = 'sample-a', ['record-b'] = 'sample-b'})
    CHECK_EQ(ctx:gaReplyState().calls.MsGAHmSet, 1) -- 释放回包没有重做写入。
    REQUIRE(other:close())
    CHECK_EQ(other:close(), false)
    CHECK_EQ(request(ctx, {op = 'read'}).data.value['record-b'], 'sample-b')
end)

TEST_CASE('共享记录丢弃写入回包不会撤销已经发生的写入', '[example][ga][multi-room][lost-reply]', function(ctx)
    local other = ctx:createRoom()
    ctx:holdGaReplies('MsGAHmSet')
    startWrite(ctx)
    REQUIRE_EQ(ctx:dropGaReplies(), 1)
    CHECK_EQ(ctx:gaReplyState().held, 0)
    CHECK_EQ(request(other, {op = 'read'}).data.value, {['record-a'] = 'sample-a'})
    CHECK_EQ(ctx:client(0):waitEvent({name = 'example_store_result', timeout = 0.05}), nil)
    -- 本例验证“已写入但无回包”，业务如何恢复需要由业务套件另行验证。
end)

TEST_CASE('共享记录在新用例中从空存档开始', '[example][ga][isolation]', function(ctx)
    CHECK_EQ(ctx:gaHGetAll('example:records'), {})
    CHECK_EQ(request(ctx, {op = 'read'}).data.value, {})
end)

TEST_CASE('旧平台缺少GA时走能力探测分支', '[example][ga][no-ga]', function(ctx)
    CHECK_EQ(request(ctx, {op = 'read'}), {unsupported = true})
    CHECK_THROWS(function() ctx:gaHGetAll('example:records') end)
    local other = ctx:createRoom()
    CHECK_EQ(request(other, {op = 'read'}), {unsupported = true})
end)
