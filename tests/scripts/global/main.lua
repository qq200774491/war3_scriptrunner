---@diagnostic disable: undefined-global, missing-fields, redundant-parameter
RegisterEvent('_msbroadcast', function(_, _, payload)
    local message = json.decode(payload)
    if message.key ~= 'notice' or message.value ~= '完成' then
        error('广播内容不符', 0)
    end
    MsSendMlEvent(0, 'passed', 'global native chain')
end)

local returned = false

local function on_released(released)
    if released.errnu ~= 0 or not released.data.ok then
        error('释放锁失败', 0)
    end
    MsBroadcast('notice', '完成')
end

local function on_deleted(deleted)
    if deleted.errnu ~= 0 or deleted.data.value ~= 1 then
        error('删除字段的结果不符', 0)
    end
    MsGALockRelease('lock', on_released)
end

local function on_all_read(all)
    if all.errnu ~= 0 or all.data.value.other ~= '2' then
        error('读取全部字段的结果不符', 0)
    end
    MsGAHDel('hash', {'other'}, on_deleted)
end

local function on_fields_read(read)
    if read.errnu ~= 0 then
        error('读取指定字段失败', 0)
    end
    local value = read.data.value
    if value.owner ~= '玩家一' or value.absent then
        error('读取指定字段的结果不符', 0)
    end
    MsGAHGetAll('hash', on_all_read)
end

local function on_saved(saved)
    if saved.errnu ~= 0 or saved.data.value ~= 2 then
        error('写入字段的结果不符', 0)
    end
    MsGAHmGet('hash', {'owner', 'absent'}, on_fields_read)
end

local function on_blocked(blocked)
    if blocked.errnu ~= 0 or blocked.data.ok then
        error('锁被占用时再次获取应当失败', 0)
    end
    MsGAHmSet('hash', {owner = '玩家一', other = '2'}, on_saved)
end

local function on_acquired(reply)
    if not returned or reply.errnu ~= 0 or not reply.data.ok then
        error('首次获取锁失败', 0)
    end
    MsGALockAcquire('lock', 1000, on_blocked)
end

local transaction = MsGALockAcquire('lock', 1000, on_acquired)
if type(transaction) ~= 'number' then
    error('获取锁应返回事务号', 0)
end
returned = true
