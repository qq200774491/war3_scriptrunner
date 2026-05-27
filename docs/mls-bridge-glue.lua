-- ============================================================================
-- mls-dll war3 客户端 lua glue（v0.6 DzSync 帧同步版本）
-- ----------------------------------------------------------------------------
-- 这个文件是 war3 客户端 in-game lua 的桥；它做两件事：
--
--   1. 50ms 周期调 DLL 的 MsPump() native。
--      MsPump 在 **war3 主线程** drain SYNC_OUT_QUEUE，对每条 event 调
--      DzSyncData("mls", json) —— 由 war3 帧同步层在同一帧把消息广播到
--      所有客户端的业务 lua（lockstep，绝不 desync）。
--
--   2. 提供 MsSendMlEvent() c2s 上行的便捷封装。
--
-- **业务接收 *不* 在这里写**：业务侧用 DzTriggerRegisterSyncEvent("mls", ...)
-- 直接挂回调，参考 docs/examples/dzsync-receiver.lua。dll 不维护回调表。
--
-- 集成位置：war3 map 的 lua 入口（war3map.lua / 仙途的 main 等），map 启动后
-- require 一次并调 start()。
--
-- 依赖：japi.MsPump / japi.MsSendMlEvent（由 mls-sim.dll 注册）+ 全局 jass
-- native CreateTimer / TimerStart（war3 自带）。
-- ============================================================================

local M = {}

-- 调整入口：仙途用 require "api.japi"；通用 yd_lua_engine 下用全局 japi 表。
local japi = japi or _G.japi or require("api.japi")

--- 启动 50ms 周期 pump。**必须在 jass VM 就绪后调**（map 的 main 之后）。
function M.start()
    TimerStart(CreateTimer(), 0.05, true, function()
        japi.MsPump()
    end)
end

--- c2s 上行（等价于 RequestExtraBooleanData(1009, ...)）。
--- @param player_index integer  必须是 host pi（约定 = 0）；其他客户端调将无效
--- @param ename string  ≤32 字节
--- @param evalue string ≤900 字节
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

return M
