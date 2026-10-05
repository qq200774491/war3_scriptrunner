# ScriptRoom 共享云脚本房间

用于在同一台 Windows 电脑上启动两个 Warcraft III 客户端，共用一个云脚本房间，并分别保存玩家的本地存档。提供图形界面和命令行入口。

本仓库的默认分支 `scriptrunner` 使用原版 `scriptrunner.dll`；`main`、`v0.6` 分支保留 MLS 模拟器。

## 运行环境

- Windows。
- KKWE 插件目录及工具支持的 Warcraft III 1.27 游戏文件。
- 已生成的 `.w3x` 地图、客户端 Lua 入口和云脚本目录。
- 云脚本目录必须直接包含 `main.lua`。

地图、云脚本、KKWE 和魔兽游戏资源需要自行准备。便携版须保留整个文件夹，不能只复制 EXE。

## 准备程序

已有便携版时，直接打开程序所在目录，继续配置路径。

从源码构建需要稳定版 Rust、CMake、Visual Studio 2022 C++ 工具链（含 Win32）：

```powershell
git clone --branch scriptrunner https://github.com/qq200774491/war3_mls.git
cd war3_mls
Copy-Item 本地测试配置.example.json 本地测试配置.json
```

编辑 `本地测试配置.json`，填写本机实际路径，然后构建：

```powershell
.\tools\package_rust.ps1
```

输出目录为 `build/package/ScriptRoom-Rust`，包含 `ScriptRoom.exe` 和 `ScriptRoomCLI.exe`。运行便携版无需安装 Rust、Python 或编译工具。

## 配置路径

编辑程序旁的 `本地测试配置.json`，或在 GUI 的“路径与配置”中填写：

| 字段 | 填写内容 |
|---|---|
| `map` | 已生成的 `.w3x` 地图文件 |
| `local_entry` | 地图客户端的本地 Lua 入口 |
| `cloud_scripts` | 直接包含 `main.lua` 的云脚本目录 |
| `archives` | 当前地图独立的存档目录，如 `build/state/map` |
| `editor` | KKWE 根目录，包含 `bin` 和 `plugin/warcraft3` |
| `source_game` | 原始魔兽游戏目录 |
| `game` | 共用的测试游戏目录，如 `build/warcraft-demo`，不能与 `source_game` 相同 |
| `seconds` | 本轮服务运行时长，单位秒，默认 3600 |
| `startup_timeout` | 等待建房、加入和开局的超时秒数，默认 90 |

示例中的 `map` 是通用项目名，请替换为自己的实际路径。JSON 路径建议使用 `/`；相对路径以配置文件所在目录为基准。换地图时另存配置并选择独立存档目录。

工具使用已有地图，不负责打包地图或编译云脚本。地图需具备兼容的 MLS 接口与本地脚本入口，不能仅替换文件路径就适配所有地图。

## 图形界面启动

1. 双击 `ScriptRoom.exe`。
2. 打开“路径与配置”，填写路径并保存配置。
3. 点击“检查”，按提示处理路径、插件或游戏版本问题。
4. 退出上一轮游戏，点击“启动”，等待两个客户端建房、加入并开局。
5. 在控制台查看日志，可按来源筛选、搜索或查看单行详情。

测试期间保持启动器运行。结束时先在游戏内保存并退出两个客户端，再停止云服务。任一客户端断开后，需要重新启动整场测试。

“停止云服务”不会关闭游戏窗口。“关闭全部魔兽”会请求本机所有魔兽窗口退出；仍未退出时可确认强制结束，未保存进度会丢失。

## 命令行启动

在便携版目录打开 PowerShell：

```powershell
# 检查配置，不启动游戏
.\ScriptRoomCLI.exe --check

# 使用默认配置启动
.\ScriptRoomCLI.exe

# 指定配置并运行两小时
.\ScriptRoomCLI.exe --config .\本地测试配置.json --seconds 7200

# 查看全部参数
.\ScriptRoomCLI.exe --help
```

按 `Ctrl+C` 停止云服务，游戏窗口需自行退出。默认端口 TCP 6112 必须空闲；同一游戏目录和存档目录不要同时运行多场。

## 日志与存档

日志位于 `build/game`。启动失败时查看 GUI 控制台，以及 `launch-0.stderr.log`、`launch-1.stderr.log`、`quicktest-0.log`、`quicktest-1.log`。

存档位于配置的 `archives` 目录：

- `clients/player-0.ini`、`clients/player-1.ini`：两名玩家的客户端存档。
- `slot-0.archive`、`slot-1.archive`：两名玩家的云脚本存档。
- `global.sqlite3`：当前存档目录内的共享持久化数据。

备份时先退出游戏和云服务，再复制整个存档目录。玩家身份按本地槽位区分，不是平台账号；本工具不导入线上存档，也不提供跨电脑共享房间或断线重连。

## 更多说明

- [Rust 启动器使用说明](Rust启动器.md)：界面操作、命令行与构建选项。
- [配置、控制台与存档](云脚本控制台与配置.md)：完整配置示例、Python 启动入口与存档维护。
- [旧 Python 版使用说明](GUI与命令行启动.md)：旧版 GUI、CLI 与打包方法。
