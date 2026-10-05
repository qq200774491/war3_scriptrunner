-- 关闭请求由持有创建句柄的助手执行；事件句柄只属于这一代游戏进程。
local ffi = require 'ffi'
ffi.cdef [[
    void* __stdcall CreateEventA(void*, int, int, const char*);
    unsigned long __stdcall GetLastError(void);
    unsigned long __stdcall WaitForSingleObject(void*, unsigned long);
    int __stdcall SetEvent(void*);
    int __stdcall CloseHandle(void*);
]]

local function create_event(name, manual)
    local handle = ffi.C.CreateEventA(nil, manual, 0, name)
    local code = ffi.C.GetLastError()
    if handle == nil then
        error('cannot create game control event: ' .. tostring(code))
    end
    if code == 183 then
        ffi.C.CloseHandle(handle)
        error('game control event already exists')
    end
    return handle
end

return function(process)
    local prefix = 'Local\\ScriptRoomGame-' .. tostring(process:id())
    local request = create_event(prefix .. '-terminate', 0)
    local opened, exited = pcall(create_event, prefix .. '-exited', 1)
    if not opened then
        ffi.C.CloseHandle(request)
        error(exited)
    end
    local ok, code = pcall(function()
        -- 先发布控制通道，再通知父启动器 PID，避免刚启动就点击关闭的竞态。
        print('GAME_PID ' .. tostring(process:id()))
        io.stdout:flush()
        while process:is_running() do
            local result = ffi.C.WaitForSingleObject(request, 100)
            if result == 0 then
                process:kill()
            elseif result ~= 258 then
                error('game control wait failed: ' .. tostring(ffi.C.GetLastError()))
            end
        end
        local exit_code = process:wait()
        assert(ffi.C.SetEvent(exited) ~= 0, 'cannot signal game exit')
        return exit_code
    end)
    ffi.C.CloseHandle(request)
    ffi.C.CloseHandle(exited)
    if not ok then
        error(code)
    end
    return code
end
