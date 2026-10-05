-- 两种宿主复用同一异步流程；每个具名阶段验证当前结果后推进下一步。
RegisterEvent('_msbroadcast', function(_, _, payload)
    local message = json.decode(payload)
    assert(message.key == 'notice' and message.value == '完成')
    MsSendMlEvent(0, 'passed', 'global native chain')
end)

local returned = false
local function on_released(released)
    assert(released.errnu == 0 and released.data.ok)
    MsBroadcast('notice', '完成')
end

local function on_deleted(deleted)
    assert(deleted.errnu == 0 and deleted.data.value == 1)
    MsGALockRelease('lock', on_released)
end

local function on_all_read(all)
    assert(all.errnu == 0 and all.data.value.other == '2')
    MsGAHDel('hash', {'other'}, on_deleted)
end

local function on_fields_read(read)
    assert(read.errnu == 0 and read.data.value.owner == '玩家一' and not read.data.value.absent)
    MsGAHGetAll('hash', on_all_read)
end

local function on_saved(saved)
    assert(saved.errnu == 0 and saved.data.value == 2)
    MsGAHmGet('hash', {'owner', 'absent'}, on_fields_read)
end

local function on_blocked(blocked)
    assert(blocked.errnu == 0 and not blocked.data.ok)
    MsGAHmSet('hash', {owner='玩家一', other='2'}, on_saved)
end

local function on_acquired(reply)
    assert(returned and reply.errnu == 0 and reply.data.ok)
    MsGALockAcquire('lock', 1000, on_blocked)
end

local tid = MsGALockAcquire('lock', 1000, on_acquired)
assert(type(tid) == 'number')
returned = true
