<p align="center"><img src="快拿掉.svg" width="240" alt="快拿掉快拿掉"></p>

# 云脚本Test启动器

英文名 ScriptRoom。在一台电脑上开几个魔兽（1 到 4 个，默认两个），连到本机的同一个云脚本房间，用来测要几个玩家一起才跑得起来的云脚本逻辑。每个玩家的存档分开存。

默认分支 `scriptrunner` 用原版 `scriptrunner.dll`，MLS 模拟器那套在 `main` 和 `v0.6`。

## 限制

- 只支持同一台电脑上的玩家，一次最多 4 个。没有跨机，也没有断线重连，一端掉线这场就结束，得重开。
- 只支持魔兽 1.27。启动时会校验 `Game.dll` 的哈希，不对就不让启动。
- 不打包地图，也不编译云脚本，这两样要先自己弄好。
- 地图本身得有兼容的 MLS 接口和本地脚本入口，只改路径不保证能用。

## 需要什么

- Windows
- KKWE 目录（有 `bin` 和 `plugin/warcraft3`）
- 1.27 的魔兽游戏文件
- 生成好的 `.w3x` 地图、地图的客户端 Lua 入口、云脚本目录（`main.lua` 直接放在目录里）

## 使用

双击 `ScriptRoom.exe`。

1. “路径与配置”里填好路径，保存。
2. 点“检查”。缺文件、插件没启用、游戏版本不对都会在这里报出来。
3. 上一轮的魔兽要先关掉，在左栏“启动”下面选好开几个客户端，然后点“启动”。玩家 0 建房，其余依次加入，人齐了自动开局。
4. 云脚本的输出在控制台里，能搜索，能按来源筛选，点一行在右边看全文。

测完先在每个游戏里存档、退出，再点“停止”。反过来的话，没存的就丢了。

“停止”只停云服务，游戏窗口不会关。“关闭全部魔兽”是请本机所有魔兽窗口正常退出，三秒后还没退的会问你要不要强制结束，强制结束会丢未保存的进度。

命令行：

```powershell
.\ScriptRoomCLI.exe --check                                      # 只检查，不启动
.\ScriptRoomCLI.exe                                              # 用默认配置启动
.\ScriptRoomCLI.exe --config .\本地测试配置.json --seconds 7200    # 指定配置，跑两小时
.\ScriptRoomCLI.exe --help                                       # 全部参数
```

`Ctrl+C` 停云服务，游戏窗口要自己关。TCP 6112 端口必须空闲，同一个游戏目录和存档目录不要同时跑两场。

## 配置

程序旁边的 `本地测试配置.json`，也可以在界面里改。第一次用就复制 `本地测试配置.example.json`。这个文件是本机路径，不进版本库。

| 字段 | 内容 |
|---|---|
| `map` | 生成好的 `.w3x` |
| `local_entry` | 地图客户端的本地 Lua 入口 |
| `cloud_scripts` | 云脚本目录，`main.lua` 在这一层 |
| `archives` | 这张地图专用的存档目录，如 `build/state/map` |
| `editor` | KKWE 根目录 |
| `source_game` | 原始魔兽目录，只用来复制文件 |
| `game` | 所有客户端共用的测试目录，如 `build/warcraft-demo`，不能和 `source_game` 相同 |
| `seconds` | 云服务最长运行秒数，默认 3600 |
| `startup_timeout` | 等建房、加入、开局的超时秒数，默认 90 |
| `clients` | 一次开几个客户端，1 到 4；旧配置没有这一项，按 2 个算 |

路径用 `/` 比较省事，用 `\` 的话 JSON 里要写 `\\`。相对路径相对配置文件所在目录。换地图时另存一份配置，存档目录也换掉，别共用。

每个字段更细的说明，还有存档的规则，见 [云脚本控制台与配置.md](云脚本控制台与配置.md)。

## 存档和日志

存档都在 `archives` 目录下：

- `clients/player-0.ini`、`clients/player-1.ini`……：每个玩家的客户端存档
- `slot-0.archive`、`slot-1.archive`……：每个玩家的云脚本存档
- `global.sqlite3`：房间共享的数据

玩家就是 0 号、1 号……这些固定槽位，不是平台账号，也不能导入线上存档。备份要先退出游戏和云服务，然后整个目录一起复制，只拷一个 ini 不够。

日志在 `build/game`。启动失败先看控制台，再看 `launch-0.stderr.log`、`launch-1.stderr.log`；建房和加入的过程看 `quicktest-0.log`、`quicktest-1.log`。每轮结束还会写一份 `project-result-cpp.json` 作汇总。

## 从源码构建

要 CMake 3.24 以上、Visual Studio 2022（C++ 工具里 x64 和 Win32 都要装）、Git。构建界面时会从 GitHub 拉一份 BUI。

魔兽和 `scriptrunner.dll` 都是 32 位的，所以脚本宿主和游戏插件要编成 32 位，启动器本身是 64 位，分两次编：

```powershell
git clone --branch scriptrunner https://github.com/qq200774491/war3_mls.git
cd war3_mls
Copy-Item 本地测试配置.example.json 本地测试配置.json   # 再改成自己的路径

# 32 位：脚本宿主、桥接库、游戏插件
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release

# 64 位：启动器
cmake -S launcher -B build/launcher -G "Visual Studio 17 2022" -A x64
cmake --build build/launcher --config Release
```

不用另外打包，直接从构建目录运行就行。启动器会往上层目录找，找到同时有 `scriptrunner.dll` 和 `map/legacy_mls.lua` 的那一层当根目录。

- 界面：`build\launcher\gui\Release\ScriptRoom.exe`
- 命令行：`build\launcher\Release\ScriptRoomCLI.exe`

要拿给别人用，需要把这些放进同一个目录，相对位置不变：`ScriptRoom.exe`、`ScriptRoomCLI.exe`、`scriptrunner.dll`、`map/`、`tools/` 下的 `launch_game.lua` 和 `game_control.lua`，以及 `build/Release` 里的 `script_host.exe`、`lua_probe.exe`、`shared_room_native.dll`、`zz_shared_quicktest.dll`。

测试：

```powershell
.\build\launcher\tests\Release\scriptroom_tests.exe          # 全部
.\build\launcher\tests\Release\scriptroom_tests.exe room.    # 名字里带 room. 的
```

房间相关的测试会加载原版 DLL，要先编完上面 32 位那一步。少数用例要用本机的 KKWE 或云脚本目录，没有的话会显示“跳过”，不算失败。

目录：

| 目录 | 内容 |
|---|---|
| `launcher/core` | 房间服务、脚本宿主、存档、启动流程 |
| `launcher/cli` | 命令行程序 |
| `launcher/gui` | 图形界面 |
| `launcher/tests` | 测试 |
| `native` | 32 位部分：脚本宿主、桥接库、游戏插件 |
| `map` | 客户端那边的 Lua |
| `third_party` | Detours、SQLite、nlohmann/json |

## 关于

- 软件名称：云脚本Test启动器（英文名 ScriptRoom）
- 作者：花吃了阿九
- 参与者：Carlos、小为（xiaowei）（详见下面的“参与者”）
- 版本：0.3.0
- 许可：GPL-3.0，全文见 [LICENSE](LICENSE)，原因见下面的“许可与用到的东西”
- 项目主页：https://github.com/qq200774491/war3_mls

## 参与者

- 花吃了阿九
- Carlos
- 小为（提交里也署名 xiaowei）


## 许可与用到的东西

界面用的 [BUI](https://github.com/ppmpreetham/BUI) 是 GPL，所以本项目整体也按 GPL-3.0 开源。

- [war3_quicktest](https://github.com/shuen4/war3_quicktest)：快速建房和加入的游戏适配，`native/quicktest/game_hooks.cpp` 改自它，保留 [MIT 许可](native/quicktest/LICENSE)，来历在[这里](native/quicktest/README.md)。
- [Microsoft Detours](https://github.com/microsoft/Detours)：游戏适配层拦截函数用，仓库里放着 v4.0.1 源码和 [MIT 许可](third_party/detours/LICENSE.md)。
- [BUI](https://github.com/ppmpreetham/BUI)：窗口、控件、主题，构建时按固定提交拉取。
- [SQLite](https://sqlite.org/)：全局存档，源码在 `third_party/sqlite3`，公有领域。
- [nlohmann/json](https://github.com/nlohmann/json)：读写配置和房间协议，[MIT 许可](third_party/nlohmann/LICENSE.MIT)。
- [Serpent](https://github.com/dolag233/Serpent) 和本地项目 ShadowWorks：界面布局和深灰蓝配色的参考。

云脚本由 KKWE 环境里原版的 `scriptrunner.dll` 执行。第三方代码的版权和许可归各自作者，版本见 [third_party/README.md](third_party/README.md)。

## 其他文档

- [启动器.md](启动器.md)：界面和命令行细节、退出码、代码布局
- [云脚本控制台与配置.md](云脚本控制台与配置.md)：配置字段说明、控制台日志、存档规则
