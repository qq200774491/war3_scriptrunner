# mls-dll JASS 桥端到端验证步骤

接 `docs/rs-dll-prompt-v2-jass-bridge.md` + `docs/yd_jass_api-abi.md`。本次实现走 **路径 A**：DLL 暴露队列 API（`MlsNextEvent`），`RegisterEvent` 在 lua 实现，DLL 完全不碰 jass code handle。

## 当前状态

| 项 | 状态 |
|---|---|
| Rust DLL 代码 | ✅ 全部写完，editor side 已部署 200,704 字节 |
| war3 side DLL 部署 | ⚠️ 被 war3.exe 锁住——**用户需关 war3 后跑 `build.ps1` 才能覆盖** |
| mls-sim.lua loader 接线 | ✅ `loader.mss(self)` 调 `self.dll.Initialize()` |
| lua glue（RegisterEvent + pump）| ✅ 参考实现 `docs/mls-bridge-glue.lua`，需用户按自己 map/项目集成 |
| mls-sim 运行 | ⚠️ 未运行（最后日志 17:29），需用户用真业务脚本重启 |

## 验证 5 跳

```
仙途 lua: japi.RequestExtraBooleanData(1009, p, "ename", "data", false,0,0,0)
                                                  ↓ (DLL hook_reb::Fake_REB)
DLL pipe send queue → mls-sim:
    pipe: c2s event room=room-001 player=0 ename="ename" evalue_len=4
                                                  ↓
mls-sim 服务端 lua handler → MsSendMlEvent(0, "ack", "ok")
                                                  ↓
DLL: session: recv s2c ename="ack" player=0 evalue_len=2
                                                  ↓ (dispatch::push_recv → RECV_QUEUE)
war3 lua: japi.MlsNextEvent() 返回 {"ename":"ack","evalue":"ok","player_index":0}
                                                  ↓
lua glue: pump → _registry["ack"] → cb(id, "ack", "ok", 0)  ←  此处业务日志输出
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

### 3. 在 war3 客户端 lua 集成 glue（一次性）

把 `D:\code2\mls\docs\mls-bridge-glue.lua` 抄到 war3 客户端工程里（仙途 / 测试 map 的 lua 入口附近），按里头注释装好依赖（japi、json、centertimer）并调一次 `M.install_default_pump_centertimer()`，再加一两个测试回调：

```lua
local mls = require "mls-bridge-glue"
mls.install_default_pump_centertimer()

mls.RegisterEvent("ack", function(id, ename, evalue, pi)
    print(string.format("[map] ack received: ename=%s evalue=%s pi=%d", ename, evalue, pi))
end)

-- 主动发 c2s（任意按键 / 进游戏后调）
mls.MsSendMlEvent(0, "buy_tower", "1")
-- 或继续走仙途现有路径：japi.RequestExtraBooleanData(1009, who, "buy_tower", "1", false, 0,0,0)
```

> Note：如果客户端 lua 没有 centertimer，走 5b 路径用 jass timer + ExecuteFunc 自己接，参考 `docs/mls-bridge-glue.lua` 注释。

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
[mls/INF] plugin: jass bridge ready (japi loaded, REB(1009) hooked, natives registered)
[mls/INF] hook_reb: RequestExtraBooleanData hooked, orig=0x...
[mls/INF] send: c2s ename="buy_tower" evalue_len=1 ok=1
[mls/INF] session: recv s2c ename="ack" player=0 evalue_len=2
```

**war3 客户端 lua 控制台**（仙途的 / 你的 log.txt）应该有：
```
[map] ack received: ename=ack evalue=ok pi=0
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
| 5. lua cb 没执行 | war3 lua 无 print | glue 没集成 / `japi.MlsNextEvent` 解析不出（natives 没注册？查 `natives: register_all` 日志）/ pump 没装到 timer |

## 已知限制 / TODO

- **`MsGetRoom*` natives 都返回 0** —— 完整功能要在 dispatch 里识别特殊 ename（`_room_meta_*`）然后写入 atomic，本期 P0 验收不阻塞
- **player_index 在 c2s hook 里固定 0** —— 当前所有玩家从仙途客户端发的 c2s 都被记为 player 0；多人房间需要从 `whichPlayer` handle 反查 → `call_native("GetPlayerId", h)`，留 TODO
- **lua glue 还需用户按项目集成** —— 我们没法替仙途 / 测试 map 改 lua 入口；提供了完整可读的参考文件
