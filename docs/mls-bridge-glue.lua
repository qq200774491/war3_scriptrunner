-- ============================================================================
-- mls-dll war3 客户端 lua glue（path-A 设计）
-- ----------------------------------------------------------------------------
-- 这个文件不是 plugin loader 那侧的（mls-sim.lua），是 **war3 客户端 in-game**
-- lua 的桥。它把 DLL 注册的 4 个核心 jass native（MlsNextEvent / MsSendMlEvent /
-- MsGetRoom*）封装成与 mls-sim 服务端对称的 RegisterEvent / MsSendMlEvent API，
-- 完成 path-A 闭环：
--
--     war3 lua RegisterEvent("xx", cb)
--          ↓
--     _mls_registry[xx] = { cb, ... }
--          ↓
--     50ms pump 调 japi.MlsNextEvent()
--          ↓
--     DLL pop RECV_QUEUE 一条 → 返回 JSON 字符串
--          ↓
--     lua JSON.decode + 对 _mls_registry[ename] 里每个 cb 调用
--
-- 集成位置：放在 war3 map 的 lua 入口附近（war3map.lua / 仙途的 main 等），
-- 在 map 启动后被 require 一次即可。
--
-- 依赖：
--   * japi.MlsNextEvent / japi.MsSendMlEvent / japi.MsGetRoom*（由 mls-sim.dll 注册）
--   * 一个能跑 50ms 周期回调的 timer，下面给两种实现（jass timer / centertimer）
--   * 一个 JSON 库（仙途用 cjson；war3 自带 lua 也可手写）
-- ============================================================================

local M = {}

-- ── 1. 依赖装配（按你的 map/项目改）──────────────────────────────────

-- jass native 调用入口。仙途用 `local japi = require "api.japi"`。
-- 通用 war3 lua（yd_lua_engine）下应该是全局 japi 表或类似。
local japi = japi or _G.japi or require("api.japi")  -- 按需调整

-- JSON 解码。多数项目带；如果没有用 dkjson/cjson/luajson 任意。
local json_decode = (function()
    local ok, cjson = pcall(require, "cjson"); if ok then return cjson.decode end
    local ok2, dkjson = pcall(require, "dkjson"); if ok2 then return dkjson.decode end
    local ok3, j = pcall(require, "json"); if ok3 then return j.decode end
    error("[mls-glue] 找不到 JSON 库：cjson / dkjson / json 至少要有一个")
end)()

-- ── 2. 内部注册表 ────────────────────────────────────────────────────

local _registry = {}     -- ename → { [id] = lua_callback }
local _next_id  = 1

-- ── 3. 公开 API（与 mls-sim 服务端 api/event.lua 同名同语义）─────────

--- 注册事件回调
--- @param ename string 事件名（≤32 字节）
--- @param cb function(id:int, ename:string, evalue:string, player_index:int)
--- @return integer id 用于 UnregisterEvent
function M.RegisterEvent(ename, cb)
    assert(type(ename) == "string", "ename must be string")
    assert(type(cb) == "function",  "cb must be function")
    local bucket = _registry[ename]
    if not bucket then
        bucket = {}
        _registry[ename] = bucket
    end
    local id = _next_id; _next_id = _next_id + 1
    bucket[id] = cb
    return id
end

--- 注销回调
function M.UnregisterEvent(id)
    for _, bucket in pairs(_registry) do
        if bucket[id] then bucket[id] = nil; return end
    end
end

--- 直接发 c2s（不经 RequestExtraBooleanData(1009)；与 DLL 的 MsSendMlEvent native 等价）
--- @param player_index integer
--- @param ename string
--- @param evalue string
--- @return boolean
function M.MsSendMlEvent(player_index, ename, evalue)
    return japi.MsSendMlEvent(player_index, ename, evalue) ~= 0
end

--- 房间元信息（DLL 当前返回 0，TODO 后续从特殊 s2c 事件填充）
function M.MsGetRoomStartTs()    return japi.MsGetRoomStartTs()    end
function M.MsGetRoomLoadedTs()   return japi.MsGetRoomLoadedTs()   end
function M.MsGetRoomGameTime()   return japi.MsGetRoomGameTime()   end
function M.MsGetRoomPlayerCount() return japi.MsGetRoomPlayerCount() end
function M.MsGetRoomModeId()     return japi.MsGetRoomModeId()     end

-- ── 4. Pump：每 ~50ms drain DLL 的 RECV_QUEUE ────────────────────────

-- 单次 pump：把 DLL 队列里的 s2c 事件全部派发出去
function M.pump()
    while true do
        local s = japi.MlsNextEvent()
        if not s or s == "" then return end
        local ok, ev = pcall(json_decode, s)
        if not ok or type(ev) ~= "table" or not ev.ename then
            -- 错误日志 —— DLL 给的 JSON 一般不会坏，坏就是版本错配
            if Log and Log.Error then Log.Error("[mls-glue] bad event JSON: %s", tostring(s)) end
        else
            local bucket = _registry[ev.ename]
            if bucket then
                for id, cb in pairs(bucket) do
                    local pok, perr = pcall(cb, id, ev.ename, ev.evalue or "", ev.player_index or 0)
                    if not pok and Log and Log.Error then
                        Log.Error("[mls-glue] event %q cb crashed: %s", ev.ename, tostring(perr))
                    end
                end
            end
        end
    end
end

-- ── 5. 安装周期触发器 ────────────────────────────────────────────────
-- 两种实现选其一：
--
-- 5a. 仙途 / kkapi 项目（首选）：用 centertimer
-- ```lua
-- local centertimer = require "api.centertimer"
-- centertimer.loop(0.05, M.pump)
-- ```
--
-- 5b. 通用 jass timer：直接 CreateTimer + TimerStart + TriggerAddAction
-- ```lua
-- local trig = japi.CreateTrigger()
-- japi.TriggerRegisterTimerEvent(trig, 0.05, true)
-- japi.TriggerAddAction(trig, function() M.pump() end)
-- -- 注：lua function 能否直接作为 jass code handle 取决于 yd_lua_engine 的桥实现。
-- -- 如果不行，需要在 jass 里写 stub："function mls_pump_cb takes nothing returns nothing
-- --                                       call ExecuteFunc(\"mls_pump_lua\")
-- --                                     endfunction"
-- ```
--
-- 出于"DLL 不碰 jass code handle"原则，5a 路径优先；5b 仅在你的项目没有
-- centertimer 时退路。

-- 默认不自动 install——交给 caller 决定。要 install 直接调：
function M.install_default_pump_centertimer()
    local centertimer = require "api.centertimer"
    centertimer.loop(0.05, M.pump)
    if Log and Log.Info then Log.Info("[mls-glue] pump installed via centertimer @ 50ms") end
end

return M
