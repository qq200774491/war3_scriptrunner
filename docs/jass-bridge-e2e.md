# mls-dll JASS 桥端到端验证步骤

接 `docs/rs-dll-prompt-v2-jass-bridge.md` + `docs/yd_jass_api-abi.md` + `docs/prompts/s2c-dzsync-relay.md`。

> **v0.6 路径变更**：原"路径 A 队列 polling + lua RegisterEvent 表"在 war3 多客户端帧同步下会 desync，已废弃。**当前路径**：DLL 主线程 `MsPump()` native drain SYNC_OUT_QUEUE 后调 `DzSyncData("mls", json)`，由 war3 帧同步层在同一帧广播给所有客户端的业务 lua；业务侧用 `DzTriggerRegisterSyncEvent(t, "mls", false)` + `DzGetTriggerSyncData()` 接。下文操作步骤已按 v0.6 链路更新；`MlsNextEvent` 已从 dll 移除。

## 当前状态

| 项 | 状态 |
|---|---|
| Rust DLL 代码 | ✅ 全部写完，editor side 已部署 200,704 字节 |
| war3 side DLL 部署 | ⚠️ 被 war3.exe 锁住——**用户需关 war3 后跑 `build.ps1` 才能覆盖** |
| mls-sim.lua loader 接线 | ✅ `loader.mss(self)` 调 `self.dll.Initialize()` |
| lua glue（RegisterEvent + pump）| ✅ 参考实现 `docs/mls-bridge-glue.lua`，需用户按自己 map/项目集成 |
| mls-sim 运行 | ⚠️ 未运行（最后日志 17:29），需用户用真业务脚本重启 |

## 验证 5 跳（v0.6 DzSync 链路）

```
仙途 lua: japi.RequestExtraBooleanData(1009, p, "ename", "data", false,0,0,0)
                                                  ↓ (DLL hook_reb::Fake_REB)
DLL pipe send queue → mls-sim:
    pipe: c2s event room=room-001 player=0 ename="ename" evalue_len=4
                                                  ↓
mls-sim 服务端 lua handler → MsSendMlEvent(0, "ack", "ok")
                                                  ↓ (s2c 只投到 host pi=0 的 dll pipe)
DLL pipe worker: session: recv s2c ename="ack" player=0 evalue_len=2
                                                  ↓ (dispatch::push_sync_out → SYNC_OUT_QUEUE)
DLL 主线程 (lua 50ms timer 触发):
    Native_MsPump → drain → DzSyncData("mls", {"ename":"ack","evalue":"ok","player_index":0})
    DLL log: MsPump: sent 1 drop 0
                                                  ↓ (war3 帧同步层广播 — 所有客户端同帧)
war3 业务 lua: DzTriggerRegisterSyncEvent action 触发 → DzGetTriggerSyncData() →
    print("[biz] sync: ack ok from pi= 0")        ←  此处业务日志输出
```

## 操作步骤（按顺序）

### 1. 关 war3，部署 DLL

```powershell
# 用户 PID 49012 那个 war3 进程现在压着 dll
Stop-Process -Id 49012 -Force   # 或手动关掉 war3
cd D:\code2\mls\mls-dll
.\build.ps1                      # 这次会成功 deploy 到 plugin\warcraft3\
```

预期：两个 deploy 行都 Green：
```
[mls-dll] deployed -> D:\war3Project\KKYDWE\plugin\mls-sim.dll
[mls-dll] deployed -> D:\war3Project\KKYDWE\plugin\warcraft3\mls-sim.dll
```

### 2. 起 mls-sim（真业务脚本）

```powershell
Stop-Process -Name mls-sim -ErrorAction SilentlyContinue
Start-Process "D:\code2\mls\mls-sim-rs\target\release\mls-sim.exe" `
    -ArgumentList "-s","D:\Code\MlScriptFolder\script","--console-notwrte" `
    -RedirectStandardOutput "D:\code2\mls\mls-sim-stdout.log" `
    -RedirectStandardError  "D:\code2\mls\mls-sim-stdout.log.err"
```

预期 log 首尾：
- 起头 `Auto-created room room-001 with script_dir: D:\Code\MlScriptFolder\script`
- 业务侧 lua init 完成、无 lua error

### 3. 在 war3 客户端 lua 集成 glue 与业务接收（一次性）

**Glue（启动 pump）**：把 `D:\code2\mls\docs\mls-bridge-glue.lua` 抄到 war3 客户端工程里（仙途 / 测试 map 的 lua 入口附近），map 启动后调一次：

```lua
local mls = require "mls-bridge-glue"
mls.start()              -- 内部 TimerStart(CreateTimer(), 0.05, true, japi.MsPump)
```

**业务接收**：参考 `docs/examples/dzsync-receiver.lua`，自己写 trigger：

```lua
local t = japi.CreateTrigger()
japi.DzTriggerRegisterSyncEvent(t, "mls", false)
japi.TriggerAddAction(t, function()
    local raw = japi.DzGetTriggerSyncData()
    local ev  = cjson.decode(raw)   -- {ename, evalue, player_index}
    if ev.ename == "ack" then
        print(string.format("[biz] sync: %s %s from pi= %d", ev.ename, ev.evalue, ev.player_index))
    end
end)
```

**主动发 c2s**（任意按键 / 进游戏后调，idx 必须是 host pi=0）：

```lua
mls.MsSendMlEvent(0, "buy_tower", "1")
-- 或继续走仙途现有路径：japi.RequestExtraBooleanData(1009, who, "buy_tower", "1", false, 0,0,0)
```

> Note：DzSyncData 必须在主线程调，所以 pump 必须挂 jass timer（`CreateTimer` + `TimerStart`），**不能**放后台线程。`mls-bridge-glue.lua` 已经按这个语义实现，业务只需 `mls.start()`。

### 4. 启动 war3，加载带 glue 的 map

```powershell
& "D:\war3Project\KKYDWE\KKWE.exe"  # 或你常用的 war3 启动方式
```

进入测试 map，触发任意 `MsSendMlEvent` / `RequestExtraBooleanData(1009)` 调用。

### 5. 看日志

**`D:\code2\mls\mls-sim-stdout.log` 应该有：**
```
pipe: c2s event room=room-001 player=0 ename="buy_tower" evalue_len=1
```

**war3 进程（DllMain 拉的控制台）应该有：**
```
[mls/INF] plugin: jass bridge init done (japi=ok hook=queued natives=ok)
[mls/INF] japi: yd_jass_api.dll + ydbase.dll loaded, 7 symbols resolved
[mls/INF] natives: register_all done, 7/7 ok
[mls/INF] natives: DzSyncData probed OK                ← 第一次 MsPump 进队时打
[mls/INF] send: c2s ename="buy_tower" evalue_len=1 ok=1
[mls/INF] session: recv s2c ename="ack" player=0 evalue_len=2
[mls/INF] MsPump: sent 1 drop 0
```

**war3 客户端 lua 控制台**（仙途的 / 你的 log.txt）应该有：
```
[biz] sync: ack ok from pi= 0
```

任一条缺失 = 那一跳没通，对照排查。

## 排查矩阵

| 缺哪一跳 | 看哪 | 可能原因 |
|---|---|---|
| 1. mls-sim 起不来 | stderr.log | script_dir 不存在 / 端口被占 / 配置错 |
| 2. war3 DLL 没加载 | war3 控制台无 `[mls/INF] dllmain: DLL_PROCESS_ATTACH` | DLL 没部署到 warcraft3/、或 PluginName 不匹配 |
| 2'. jass 桥未就绪 | 看到 dllmain 但没 `plugin: jass bridge ready` | `japi::init()` 失败 → 看 GetProcAddress 哪个符号缺；或 `hook_reb::install()` 失败 → japi_hook 拒绝 |
| 3. c2s 没到 mls-sim | mls-sim log 无 c2s event | pipe 没连上（mls-sim 未运行）/ hook 没拦住 1009（仙途 lua 没真调 REB？） |
| 4. s2c 没回来 | DLL log 无 recv s2c | mls-sim 业务 handler 没回 MsSendMlEvent / 服务端 lua 有错误 |
| 5a. MsPump 不被调 | DLL 无 `MsPump: sent` 行 | glue 没 `mls.start()` / TimerStart 没生效 / natives 没注册（查 `natives: register_all` 日志）|
| 5b. DzSyncData 不可用 | DLL 有 `MsPump: aborting — DzSyncData unavailable` ERR（只打一次）| KKYDWE 未注册该 native → 检查 `yd_jass_api` / `ydbase` 加载顺序，或换 host 客户端 |
| 5c. 业务 cb 没执行 | DLL 有 `MsPump: sent N` 但 lua 无 `[biz] sync:` | 业务侧 `DzTriggerRegisterSyncEvent` 没挂 / prefix 拼错（必须 `"mls"`）/ `DzGetTriggerSyncData` 返空 → 检查 cjson 是否能 decode |

## >880B drop 验收（v0.6 必跑）

DzSyncData 单帧硬上限 ≈ 1024B，dll 收紧到 **JSON 体 880B**（给框架字节留 144B 余量）。
超限路径不分块、直接 drop。验证：

1. 在 mls-sim 业务脚本里塞一条故意超限的 s2c：

   ```lua
   -- 业务侧 lua handler 里
   MsSendMlEvent(0, "bigblob", string.rep("x", 950))   -- 950B evalue → json 约 1000B
   ```

2. 触发该业务 handler（按 `验证 5 跳` 流程触发任意 c2s 让 server 调到这条）。

3. 预期 DLL 控制台**有且仅有一条 WRN**，不出现 `sent`：

   ```
   [mls/WRN] MsPump: DROP json 999B > 880B ename="bigblob"
   [mls/INF] MsPump: sent 0 drop 1
   ```

4. 预期 war3 业务侧 `[biz] sync:` **没有任何输出**（因为根本没调 DzSyncData）。

5. 再发一条正常长度的（如 `MsSendMlEvent(0, "ack", "ok")`），确认通道恢复正常——drop 只影响该条，**不污染**后续。

> 也可以在 evalue 入队前就触发 dll 的 900B 上限（pipe worker 路径）：
> `MsSendMlEvent(0, "huge", string.rep("x", 1000))`（1000B > 900B）→ DLL log 应该是
> `dispatch: s2c DROP evalue 1000B > 900B ename="huge"`，**不会**进队列，因此 pump
> 也不会看到这条。900/880 双重防线各管一段。

## 已知限制 / TODO

- **`MsGetRoom*` natives 都返回 0** —— 完整功能要在 dispatch 里识别特殊 ename（`_room_meta_*`）然后写入 atomic，本期 P0 验收不阻塞
- **player_index 在 c2s hook 里固定 0** —— 当前所有玩家从仙途客户端发的 c2s 都被记为 player 0；多人房间需要从 `whichPlayer` handle 反查 → `call_native("GetPlayerId", h)`，留 TODO
- **lua glue 还需用户按项目集成** —— 我们没法替仙途 / 测试 map 改 lua 入口；提供了完整可读的参考文件
