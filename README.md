# ScriptRoom 共享云脚本房间

首次克隆后，复制 `本地测试配置.example.json` 为 `本地测试配置.json`，再填写实际地图、脚本和存档路径。示例中的 `map` 是通用项目名；本机配置不纳入版本控制。

本项目统一维护于 [war3_mls](https://github.com/qq200774491/war3_mls)，`scriptrunner` 为默认分支，提供原版 DLL 驱动的共享房间与桌面启动器；`main`、`v0.6` 分支保留原 MLS 模拟器。

这是使用原版 `scriptrunner.dll` 的本机测试原型。已实现一个独立 x86 脚本进程、两个或更多 TCP 测试连接共享同一 Lua 状态、请求去重、定向回包和广播。

**map 已接入共享云脚本宿主与独立本地测试存档。** 真实双端已通过读档、两名玩家创建英雄和初始任务奖励回包；当前验证不等于全部玩法与平台接口验收。未实现的 NativeInterface 接口明确报错并终止隔离进程。

## GUI 与命令行 EXE

新版使用 Rust/egui，便携版位于 `build/package/ScriptRoom-Rust`：双击 `ScriptRoom.exe` 配置路径、启动并查看云脚本日志；`ScriptRoomCLI.exe --check` 检查配置，`ScriptRoomCLI.exe` 启动测试。GUI、启动管理、共享房间服务均为 Rust，不依赖 Python / `_internal`。原版 DLL 与 32 位原生组件保留，须分发整个文件夹。

首次打包导出当前 map 配置的绝对路径，继续使用同一套测试魔兽与已有玩家独立 INI。界面可另存配置，支持黑白灰/深灰蓝主题，以及先正常退出、再确认强制结束的“关闭全部魔兽”。使用与构建见 [Rust启动器.md](Rust启动器.md)。开发者执行 `tools/package_rust.ps1` 构建。旧 Python 入口及 [旧版说明](GUI与命令行启动.md) 保留作回归参考。

## 使用 map 现有地图

```powershell
./启动map双人测试.ps1
```

启动参数读取根目录 `本地测试配置.json`。其中 `cloud_scripts` 是云脚本入口目录，必须直接包含 `main.lua`；示例填写 `D:/Code/cloud_scripts/script_compile`。`local_entry` 是地图客户端脚本入口，二者用途不同。JSON 路径建议使用 `/`，使用反斜杠时必须写成 `\\`。相对路径以配置文件所在目录为基准；完整字段说明见 `云脚本控制台与配置.md`。

启动时自动打开独立的**云脚本控制台**，实时显示原版 runner 的 DEBUG/INFO/WARN/ERROR 日志、云脚本目录、连接人数、回包计数、剩余运行时间和宿主故障原因。它不替换或接管客户端控制台。关闭查看窗口不结束服务；双击 `打开云脚本控制台.cmd` 可重开最近一轮日志。日志按场次保存为 `build/game/cloud-日期-编号.log`，结束后窗口保留供查看。

此入口复用 `D:\Code\map\test.w3x`，复制到隔离游戏目录的 `Maps\Test\Project.w3x`；不打包地图，不改动map 项目，沿用地图既有的脚本加载设置。启动两个不同名字的客户端，第一端建房，第二端等主机准备完成后加入，凑齐两人自动开局。请先退出上一场测试，TCP 6112 必须空闲。

2026-09-23 实测：PID 58008/25064，主机记录 `PLAYER_JOINED count=2` 与 `START_GAME players=2`，两端地图日志分别记录 SRPlayer0/SRPlayer1，用户确认两个都进图。此前的地图路径报错及加入端 C++ 崩溃未在本次重现。

入口每次将 `D:\Code\map\script\lightning_main.lua` 更新至隔离游戏目录，恢复测试模式及 `D:\Code\map\script` 本地脚本路径。初次遗漏此入口造成 UID 失败；补齐后两端分别获得项目原有的测试 UID 12345678/12345679，并完成两人 UUID 同步收集。

入口启动唯一的原版 runner，加载配置指定的云脚本目录。隔离目录中的入口副本安装 `map/legacy_mls.lua`，把现有 MLS 请求接入共享房间，把回包通过 Warcraft 同步触发器交给原地图业务。源地图、开发项目和编辑器文件不变，不启用每进程独立的 `-scriptrunnerfolder`。

两名固定测试玩家的存档位于 `build/state/map/slot-0.archive` 和 `slot-1.archive`，重启后保留。它们按测试槽位区分，不是平台账号存档；不导入真实平台数据。同一存档目录只允许一个宿主写入；保存使用临时文件、刷新及替换，失败保留旧存档。`slot-N.sessions` 记录启用此能力后的本地宿主启动次数（包括本轮），供 `MsGetPlayedCount` 使用，不代表平台历史局数。

角色初始化的摆摊模块依赖全局接口，因此本地宿主另实现 Hash 批量读写、删除、读取全表、TTL 锁与广播。Hash 保存至同目录的 `global.sqlite3`，使用事务；锁不重入、按单调时间到期，在关闭宿主后释放；广播仅投递本房间。这是单宿主测试实现，不提供跨房间/跨电脑全局服务，也未实现 KV、全局整数增减、商城或平台账号数据。

配置默认保持服务 3600 秒，测试期间须保持启动工具运行；到期或关闭启动工具会关闭云端服务，游戏窗口需自行退出。`-Seconds 7200` 可覆盖配置中的时长；`-NoConsole` 仅禁用独立查看窗口，仍写云端日志文件。断开后需退出两端并重新启动整场测试。

## 快速运行

在 PowerShell 中：

```powershell
Set-Location F:\code\scriptrunner
python tools\build.py
python -m unittest discover -s tests -v
python tools\demo.py
```

依赖：Windows x86 MSVC 工具链、CMake、Python 3.11+。离线 PE 检查另用本机已有的 `pefile` 和 `capstone`。构建脚本针对 VS 2022 Win32 Release，解决当前环境的 Path/PATH 重复问题，不修改全局环境变量。

演示预期：客户端 0 加 7，客户端 1 再加 5，客户端 0 查询得到 12；重发第一次请求不再执行。过程使用真实原版 DLL，结果保存在 `analysis/demo-result.json`，结束后关闭房间、进程和连接。

## 运行服务

```powershell
python -m shared_room.server --scripts tests\scripts\counter --players 0,1
```

服务监听 `127.0.0.1`，将端口、房间 ID、玩家身份和临时连接凭据写入 `connections.json`。当前只支持同机多进程实验，不向局域网开放。所有配置玩家完成连接握手后，才启动唯一的脚本执行器并发送 `_roomloaded`。

客户端 SDK 是 `shared_room/client.py`。连接成功依次收到 `joined` 和 `ready`；业务请求只接受：

```json
{"id":"request-001","event":"increment","payload":"7"}
```

- 玩家号由连接凭据绑定，业务请求不能另传或覆盖。
- 同一玩家、同一请求 ID、相同内容重发：返回原 ACK，不重复执行脚本，不重复广播回包。
- 同一 ID 更换内容：拒绝。不同玩家可以使用相同 ID。
- 请求 ACK 表示一次事件及随后的推进完成；不代表后续 Timer 异步回包已经全部产生。
- 任何已加入玩家断开都会使整个实验房间失效；第一版不支持断线重连，必须创建新房间。
- 每房间最多 10000 个唯一请求，避免无界缓存；达到上限后拒绝新请求，不淘汰旧 ID 导致重复执行。
- `MsSendMlEvent(player, event, payload)` 在本原型中按玩家定向；`player = -1` 广播给所有已连接玩家。这是本原型明确采用并测试的路由约定，不代表已完整恢复平台所有发送语义。
- 保留 `_` 开头的内部事件，不接受客户端伪造 `_roomloaded`。
- 脚本异常、未知接口、执行超时或宿主失联会使房间失败；不承诺失败请求的状态回滚，失败房间禁止继续使用。

## 更新后检查

```powershell
python tools\check_compatibility.py "D:\war3Project\KKWE插件\bin\scriptrunner.dll" --output analysis\compatibility-candidate.json
```

该命令只读候选 DLL，检查 x86、工厂导出、SHA256，并沿工厂调用与构造函数写虚表的模式寻找 runner 虚表。它不加载候选 DLL，不依赖当前版本的固定 RVA，也不自动批准未知版本。

`compatibility/engine-v1.json` 是当前已验证版本的唯一配置来源，CMake 将其生成到执行器。执行器按导出名创建/销毁 runner、按配置虚函数槽位调用接口。没有把本次反汇编地址写进运行代码。

若仅编辑器更新而 DLL 哈希未变，可以继续运行当前测试；若 DLL 改变，即使导出和虚表看起来相同，执行器也会拒绝。确认回调 ABI、返回值、线程和生命周期之后，才能更新配置、重新编译并执行完整隔离测试。工具目前不自动恢复所有 NativeInterface 函数签名。

## 魔兽地图接入与双人测试

```powershell
python tools/game_room.py --lan --launch --seconds 900
```

也可执行根目录 `启动双人测试.ps1`，先编译再运行。默认从 `D:\war3Project\war3` 复制约 0.98 GB 的基础游戏文件至 `build/warcraft-demo`，不复制其额外插件。工具使用编辑器演示地图生成独立副本，启动隐藏的 ydhost 房主和一个共享脚本服务，按原编辑器 `process:inject/create` 入口启动两份游戏，不带 `-scriptrunnerfolder`。房主生成配置及监听已验证，客户端自动进房尚未验证。默认原游戏版本为实查的 1.27；换游戏版本需指定 `--war3-version`。

只准备地图并运行房主，不启动游戏：`python tools/game_room.py --lan --seconds 900`。默认在两个游戏窗口中手动进入局域网，分别输入 `SRPlayer0` / `SRPlayer1`，加入 `SharedRoomTest`。`--auto-join` 可选；旧的强制自动入房曾因空玩家名被房主拒绝。单个 `--launch` 不加 `--lan` 仅用于检查单进程加载，固定两人房间会继续等第二人。请等待上一场工具退出后再运行；同一个 `build/game` 不支持并行生成多场。

2026-09-23 修正启动参数：`-kkwe` 必须指向实际包含 `plugin/warcraft3/config.cfg` 的 `D:\war3Project\KKWE插件`，此前误传父目录。启动前现检查多开、Lua 和 KKAPI 插件是否存在并启用；21 项自动测试通过。该修正仍需重测用户报告的初始化失败弹窗，不能据静态修正宣布实机恢复。

测试目标：两个 `build/game/player-0.log`、`player-1.log` 都出现 `PASS shared_counter=12 gold=12`；还需观察两端继续运行没有断线/异步。没有这两份实机日志不能判为通过。`ydhost.log` 是真实房主日志；`host-result.json` 是脚本宿主结果。Ctrl+C 或到期关闭服务和房主，游戏窗口需自行退出。

地图 API 见 `map/shared_room.lua`、示例 `map/demo_main.lua`：所有玩家共同 `start`，通过 `room.send(playerId, requestId, event, payload)` 提交请求。引擎回包只发给中继玩家，再经普通 `DzSyncData` 的 `SRROOM1` 前缀进入所有客户端的同步回调。定向回包在所有客户端执行，target 参数表示归属玩家；不能据本地玩家过滤业务状态变更。日志回调 `on_local_status` 只允许本地 UI/诊断。

通用 API 保持显式接入。map 专用 `legacy_mls.lua` 在隔离入口中适配 `RequestExtraBooleanData(1009)`、`war3_data` 与 `war3_data_page` 注册及触发上下文，不是所有旧地图的平台接口替代品。每条同步回包上限 16 KiB、排队原文上限 256 KiB，每 0.05 秒最多 4 片，不提供断线恢复或中继迁移；凭据仅用于本机调试，不是平台账号认证。

通用配置默认 80 字节分片和 `DzSyncData`；map 已验证配置采用 350 字节原文分片（编码后消息小于 800 字节）与 `DzSyncDataImmediately`。实测普通发送队列导致整份分页耗时约 69 秒，超过地图 30 秒接收缓存期；主动提交同步命令后，同样两位玩家各 16 页约 1.6 秒收齐。回包仍只在全端同步事件中执行业务，不从本地 TCP 回调直接读档。

原 Lua C 模块适用于编辑器 `luacore.dll`。实际地图的 `jass.dll` 使用另一 Lua 状态，因此通过 `map/room_ffi.lua` 调用纯 C 连接接口，避免跨 Lua 运行库操作栈；连接由原生句柄管理，异常在 C 边界内转换。完整测试需本机编辑器运行库及其依赖。

## 代码边界

| 路径 | 职责 |
|---|---|
| `native/runner.cpp` | 哈希门禁、DLL 生命周期、已验证 ABI 和 NativeInterface 回调 |
| `native/local_platform.cpp` | 本地测试玩家、房间时间、独立存档及排他写锁 |
| `native/wire.cpp` | 有界二进制管道协议 |
| `native/main.cpp` | 单线程执行与事件推进、心跳、关闭 |
| `shared_room/native.py` | 隔离进程、超时、退出和输出解析 |
| `shared_room/server.py` | 房间成员、连接身份、事件序列和去重 |
| `shared_room/client.py` | TCP 测试客户端 SDK |
| `bridge/` | 游戏 Lua 5.3 C 模块、非阻塞本机连接 |
| `map/shared_room.lua` | 玩家请求与游戏同步回调接线 |
| `map/legacy_mls.lua` | 现有 MLS 请求和同步回包适配、分页诊断 |
| `map/room_ffi.lua` | 游戏 Lua 运行库的纯 C 连接绑定 |
| `map/room_transport.lua` | 分片、中继身份、顺序、去重与重组 |
| `tools/game_room.py` | 独立地图、隔离游戏目录、双人房主与启动入口 |
| `tools/check_compatibility.py` | 更新后的离线结构检查 |
| `tests/` | 原 DLL 和多连接回归测试 |

## 验证边界

已经测试：原 DLL 实际加载、Lua 实际执行、共享状态、两个 TCP 客户端并发、重复 ID、玩家身份、错误、超时、断开、定时器、广播、中文路径和未知版本拒绝。

新增验证：两个独立 Lua 进程加载真实游戏 Lua 运行库及新连接 DLL；原 DLL 实际执行；完整地图适配器在模拟的 JAPI 同步总线上运行，两端轮询速度不同仍得到 `0:7,1:12,0:12`，重复请求不重新执行；分片、重复、错误顺序、Unicode 和 JSON 边界通过。

40 项自动测试通过，覆盖独立存档重开、互斥、失败保留旧档、UTF-8 字符跨页字节保持、FFI、旧 MLS 适配、原 DLL 全局异步回包、锁竞争/过期、Hash 持久化/拒绝无效写入、关闭任务取消后的进程清理，以及实时云日志和配置校验。

仍未完成：完整保存/重进及长期玩法锁步验收、真实平台账号/跨房间全局存档/商城、其他旧地图通用兼容、跨电脑与断线重连、候选新版本动态自动迁移。已验证的初始读档及角色创建不能替代这些验收。

原编辑器目录、DLL 和配置均未修改。

## 本地研究资料

研究分析、实现思路、迁移计划与内部验收记录仅在本地保留，不纳入版本控制。工具生成的 `analysis/` 输出也不随仓库分发；使用说明、构建配置与第三方授权文件继续保留。
