-- ============================================================================
-- 业务侧最小接收样例（v0.6 DzSync 帧同步路径）
-- ----------------------------------------------------------------------------
-- mls-dll 的 MsPump native 把所有 s2c event 通过 DzSyncData("mls", json) 广播；
-- war3 帧同步层会在同一帧把同一份 json 派发给所有客户端的业务 lua。
--
-- 业务 lua 自己写 trigger + DzTriggerRegisterSyncEvent("mls", false) 接，
-- 在 action 里 DzGetTriggerSyncData() 拿 json，自己 decode。
--
-- 注意：
--   * prefix 固定 "mls"；所有 mls s2c event 走这一个 prefix
--   * json schema = { ename: string, evalue: string, player_index: int }
--   * evalue 是字符串，业务自己再 decode 一次（如果原本是 json）
--   * 这个回调在所有客户端的同一帧执行——任何"按客户端不同处理"的逻辑
--     必须靠 GetLocalPlayer() 之类区分，不能靠机器 ip / 时钟
-- ============================================================================

local japi = japi or _G.japi or require("jass.japi")
local cjson = require("json")

local function on_mls_sync()
    local raw = japi.DzGetTriggerSyncData()
    if not raw or raw == "" then return end
    local ok, ev = pcall(cjson.decode, raw)
    if not ok or type(ev) ~= "table" or not ev.ename then
        print("[biz] bad mls sync payload:", tostring(raw))
        return
    end
    print("[biz] sync:", ev.ename, ev.evalue, "from pi=", ev.player_index)
    -- TODO: 按 ev.ename 分发到具体业务处理
end

local function install()
    local t = japi.CreateTrigger()
    japi.DzTriggerRegisterSyncEvent(t, "mls", false)
    japi.TriggerAddAction(t, on_mls_sync)
end

install()
