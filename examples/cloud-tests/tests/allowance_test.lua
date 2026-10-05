local events = require 'helpers.events'

---@param client MlsTestClient
---@param day string
---@param claimedDay string
local function seed(client, day, claimedDay)
    REQUIRE_EQ(events.request(client, 'example_fixture_seed', 'example_fixture_seeded', {
        day = day, claimedDay = claimedDay, balance = 20,
    }).errnum, 0)
end

---@param client MlsTestClient
---@return table
local function claim(client)
    return events.request(client, 'example_allowance_claim', 'example_allowance_result')
end

TEST_CASE('每日额度同日重复领取只记账一次', '[example][allowance][idempotency]', function(ctx)
    local client = ctx:client(0)
    seed(client, '20300102', '20300101')
    CHECK_EQ(claim(client), {granted = true, balance = 27, claimedDay = '20300102'})
    CHECK_EQ(claim(client), {granted = false, balance = 27, claimedDay = '20300102'})
    -- 同时检查真实存档，避免仅检查回包而漏掉重复写入。
    CHECK_EQ(events.request(client, 'example_fixture_archive', 'example_fixture_archive_result'), {
        balance = 27, claimedDay = '20300102',
    })
end)

TEST_CASE('每日额度日期分支从相同前置状态重跑', '[example][allowance][section]', function(ctx)
    local client = ctx:client(0)
    SECTION('当天已领取', function()
        seed(client, '20300102', '20300102')
        CHECK_EQ(claim(client).balance, 20)
    end)
    SECTION('跨日后可领取', function()
        seed(client, '20300103', '20300102')
        CHECK_EQ(claim(client), {granted = true, balance = 27, claimedDay = '20300103'})
    end)
end)

TEST_CASE('每日额度临时离线后仍保留领取记录', '[example][allowance][lifecycle]', function(ctx)
    local client = ctx:client(0)
    seed(client, '20300102', '')
    REQUIRE_EQ(claim(client).granted, true)
    REQUIRE_EQ(ctx:playerLeave({index = 0, reason = 'example-reconnect'}), 0)
    REQUIRE_EQ(ctx:playerJoin({index = 0, name = 'ExamplePlayer'}), 0)
    CHECK_EQ(claim(client), {granted = false, balance = 27, claimedDay = '20300102'})
end)
