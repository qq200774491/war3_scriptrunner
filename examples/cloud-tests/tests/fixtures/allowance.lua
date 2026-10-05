-- 只在 cloud_bootstrap 加载：设置前置存档与业务日期，读取原始保存结果。
-- 不把待验证的领取逻辑搬进夹具，也不改动平台的单调时钟。
local allowance = require 'allowance'

RegisterEvent('example_fixture_seed', function(eventId, eventName, eventData, playerIndex)
    local data = json.decode(eventData)
    -- 测试夹具有意替换日期来源，生产实现仍使用真实日期。
    ---@diagnostic disable-next-line: duplicate-set-field
    allowance.today = function() return data.day end
    local result = MsSaveScriptArchive(playerIndex, json.encode {
        balance = data.balance, claimedDay = data.claimedDay,
    })
    MsSendMlEvent(playerIndex, 'example_fixture_seeded', json.encode({errnum = result}))
end)

RegisterEvent('example_fixture_archive', function(eventId, eventName, eventData, playerIndex)
    MsSendMlEvent(playerIndex, 'example_fixture_archive_result', MsGetScriptArchive(playerIndex))
end)
