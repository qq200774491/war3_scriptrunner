# ScriptRoom Rust 版

首次克隆后，复制 `本地测试配置.example.json` 为 `本地测试配置.json`，再填写实际地图、脚本和存档路径。示例中的 `map` 是通用项目名；本机配置不纳入版本控制。

双击 `ScriptRoom.exe`。在“路径与配置”指定地图、客户端 Lua 入口、云脚本目录（直接含 main.lua）和存档目录，点击“保存配置”，再用顶部“检查”“启动”。控制台支持日志搜索、来源筛选和单行详情。黑白灰、深灰蓝、浅色主题在右上角切换，保存在程序旁的 `界面设置.json`。

左侧是控制台、配置和玩家存档导航，右侧详情栏显示运行路径与选中日志，可通过顶栏按钮收起。窗口宽度不足 1080 逻辑像素时自动隐藏详情栏。顶栏空白可拖动窗口、双击最大化，四边四角可调整大小。

“关闭全部魔兽”会向本机所有 `war3.exe` / `Warcraft III.exe` 的可见窗口请求正常关闭。三秒后仍未退出的进程可另行确认强制结束。新版启动的游戏由启动助手使用创建时保留的句柄结束，即使插件随后限制外部进程权限，也不需要重新申请结束权限；取消确认不会结束游戏。旧版或其他工具启动的游戏仍使用系统权限，拒绝访问时明确记录失败，不能冒充成功。升级前已运行的游戏需正常退出后用新版重新启动。强制结束会丢失未保存的进度。

“停止云服务”只停止本轮共享脚本宿主。游戏窗口继续存在。关闭正在运行的 GUI 会先询问是否停止服务。

启动助手保留游戏创建时的进程句柄，开局前可报告房主或加入端的退出码，例如 `0xC0000409`，避免游戏已经退出却一直显示“等待连接”。`build/game/launch-0.stderr.log`、`launch-1.stderr.log` 记录助手错误；`quicktest-0.log`、`quicktest-1.log` 记录建房与加入阶段。监测助手的故障与游戏退出分别报告，停止服务时保留独立助手及游戏控制通道，不强制结束游戏；助手在游戏退出后自行释放句柄并结束。

## 命令行

```powershell
.\ScriptRoomCLI.exe --check
.\ScriptRoomCLI.exe --config .\本地测试配置.json
.\ScriptRoomCLI.exe --config .\本地测试配置.json --seconds 3600 --stop-file .\stop.flag
```

Ctrl+C 或创建指定的 stop 文件会停止服务。重新运行前删除旧 stop 文件。

## 配置

沿用旧版 JSON 的九个字段：map、local_entry、cloud_scripts、archives、editor、source_game、game、seconds、startup_timeout。相对路径相对于配置文件。GUI 载入后使用绝对路径保存，便携打包也会转换路径，升级不覆盖已有配置。

两个玩家共用 `game` 指定的一套魔兽，不能指向原始 `source_game`。存档仍为 `archives/clients/player-0.ini`、`player-1.ini`，云档为 `slot-0.archive`、`slot-1.archive`，全局库为 `global.sqlite3`。开局汇总读档快照，各端写回自己的 INI。旧版 v1 存档会留备份后迁移；已迁移的文件缺失时拒绝启动，不用队友旧副本恢复。

## 开发构建

需要 Windows、MSVC、稳定版 Rust、CMake。无需 Python 运行时。原版 DLL 与 32 位原生组件仍保留。

```powershell
cargo +stable test --locked --all-targets
cargo +stable clippy --locked --all-targets -- -D warnings
.\tools\package_rust.ps1
```

默认输出 `build/package/ScriptRoom-Rust`，整个目录一起分发。已构建原生组件时可传 `-SkipNative`。原 Python 入口保留作回归对照。

开发时可导出 GUI 自身的渲染帧：`ScriptRoom.exe --capture C:\Temp\scriptroom.png --theme light --page settings`。主题可选 `light`、`monochrome`，默认深灰蓝；页面可选 `settings`、`archives`，默认控制台。可用 `--width 940 --height 580` 检查窄窗口，`--capture-log <现有日志文件>` 预览历史日志。此模式不自动启动游戏或服务，导出后退出。图标来自 Tabler Icons，授权文件随便携包附带。

Rust 模块分工：config 配置；platform Windows 句柄/进程；archives INI；wire/host 原生宿主；global_archive SQLite；room TCP 权威房间；launch 启动编排；game_monitor 游戏退出监测；gui 原生 egui 界面。所有原生宿主访问独占串行化，等待 ACK 时持续处理心跳和回包。

协议集成测试加载真实 scriptrunner.dll。带本机云脚本路径的集成案例依赖开发机 map 脚本。实机双端开局与游戏内验收须单独记录，编译通过不等于游戏测试通过。
