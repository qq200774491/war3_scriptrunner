---@diagnostic disable: undefined-global, missing-fields, redundant-parameter
local ffi = require 'ffi'

ffi.cdef [[
void* __stdcall CreateEventA(void*, int, int, const char*);
unsigned long __stdcall GetLastError(void);
unsigned long __stdcall WaitForSingleObject(void*, unsigned long);
int __stdcall SetEvent(void*);
int __stdcall CloseHandle(void*);
]]

local ERROR_ALREADY_EXISTS = 183
local WAIT_OBJECT_0 = 0
local WAIT_TIMEOUT = 258
local POLL_MILLISECONDS = 100

local function create_event(name, manual)
    local handle = ffi.C.CreateEventA(nil, manual, 0, name)
    local code = ffi.C.GetLastError()
    if handle == nil then
        error('无法创建游戏控制事件：' .. tostring(code), 0)
    end
    if code == ERROR_ALREADY_EXISTS then
        ffi.C.CloseHandle(handle)
        error('游戏控制事件已存在', 0)
    end
    return handle
end

return function(process)
    local prefix = 'Local\\ScriptRoomGame-' .. tostring(process:id())
    local request = create_event(prefix .. '-terminate', 0)
    local opened, exited = pcall(create_event, prefix .. '-exited', 1)
    if not opened then
        ffi.C.CloseHandle(request)
        error(exited, 0)
    end

    local ok, code = pcall(function()
        print('GAME_PID ' .. tostring(process:id()))
        io.stdout:flush()
        while process:is_running() do
            local result = ffi.C.WaitForSingleObject(request, POLL_MILLISECONDS)
            if result == WAIT_OBJECT_0 then
                process:kill()
            elseif result ~= WAIT_TIMEOUT then
                error('等待游戏控制事件失败：' .. tostring(ffi.C.GetLastError()), 0)
            end
        end
        local exit_code = process:wait()
        if ffi.C.SetEvent(exited) == 0 then
            error('无法通知游戏已退出', 0)
        end
        return exit_code
    end)
    ffi.C.CloseHandle(request)
    ffi.C.CloseHandle(exited)
    if not ok then
        error(code, 0)
    end
    return code
end
