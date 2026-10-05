# ScriptRoom Rust 版

从源码使用时，先复制 `本地测试配置.example.json` 为 `本地测试配置.json` 并填写实际路径。使用便携版时，编辑程序旁已有的 `本地测试配置.json`，或在界面中修改配置。

双击 `ScriptRoom.exe`。在“路径与配置”指定地图、客户端 Lua 入口、云脚本目录（直接含 main.lua）和存档目录，点击“保存配置”，再用顶部“检查”“启动”。控制台支持日志搜索、来源筛选和单行详情。黑白灰、深灰蓝、浅色主题在右上角切换，保存在程序旁的 `界面设置.json`。

左侧是控制台、配置和玩家存档导航，右侧详情栏显示运行路径与选中日志，可通过顶栏按钮收起。窗口宽度不足 1080 逻辑像素时自动隐藏详情栏。顶栏空白可拖动窗口、双击最大化，四边四角可调整大小。

“关闭全部魔兽”会请求本机所有 `war3.exe` / `Warcraft III.exe` 窗口正常退出。三秒后仍未退出的进程可另行确认强制结束；强制结束会丢失未保存进度。提示拒绝访问时，请从游戏内正常退出。

“停止云服务”只停止本轮共享脚本宿主，游戏窗口继续存在。结束一场测试时，先在两端保存并退出游戏，再停止服务。关闭正在运行的 GUI 时，按提示确认是否停止服务。

启动失败时查看控制台错误，以及 `build/game/launch-0.stderr.log`、`launch-1.stderr.log`。`quicktest-0.log`、`quicktest-1.log` 记录建房与加入阶段。修正问题后，退出上一轮游戏再启动。

## 命令行

```powershell
.\ScriptRoomCLI.exe --check
.\ScriptRoomCLI.exe --config .\本地测试配置.json
.\ScriptRoomCLI.exe --config .\本地测试配置.json --seconds 3600 --stop-file .\stop.flag
```

Ctrl+C 或创建指定的 stop 文件会停止服务。重新运行前删除旧 stop 文件。

## 配置

配置包含九个字段：map、local_entry、cloud_scripts、archives、editor、source_game、game、seconds、startup_timeout。相对路径相对于配置文件。GUI 载入后使用绝对路径保存，便携打包也会转换路径，升级不覆盖已有配置。

两个玩家共用 `game` 指定的一套魔兽，不能指向原始 `source_game`。存档仍为 `archives/clients/player-0.ini`、`player-1.ini`，云档为 `slot-0.archive`、`slot-1.archive`，全局库为 `global.sqlite3`。开局汇总读档快照，各端写回自己的 INI。旧版 v1 存档会留备份后迁移；已迁移的文件缺失时拒绝启动，不用队友旧副本恢复。

## 开发构建

需要 Windows、MSVC、稳定版 Rust、CMake。无需 Python 运行时。原版 DLL 与 32 位原生组件仍保留。

CAME 保护地图使用独立的启动适配，避免平台预初始化与地图 VM 重建暴雪全局时重复分配对象，造成加入端首帧失步、存档一直停在 0%。适配只支持已验证的 KKWE 平台插件 ABI，版本不匹配会报告 `unsupported kkapi protected map bootstrap ABI`；普通 MPQ 地图保持原有初始化路径。建房菜单预热为 3 秒，之后还需等待地图实际加载和房间就绪。

```powershell
.\tools\package_rust.ps1
```

默认输出 `build/package/ScriptRoom-Rust`，整个目录一起分发。已构建原生组件时可传 `-SkipNative`。

开发时可导出 GUI 自身的渲染帧：`ScriptRoom.exe --capture C:\Temp\scriptroom.png --theme light --page settings`。主题可选 `light`、`monochrome`，默认深灰蓝；页面可选 `settings`、`archives`，默认控制台。可用 `--width 940 --height 580` 检查窄窗口，`--capture-log <现有日志文件>` 预览历史日志。此模式不自动启动游戏或服务，导出后退出。图标来自 Tabler Icons，授权文件随便携包附带。
