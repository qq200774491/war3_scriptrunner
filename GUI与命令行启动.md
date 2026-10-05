# GUI 与命令行启动

首次克隆后，复制 `本地测试配置.example.json` 为 `本地测试配置.json`，再填写实际地图、脚本和存档路径。示例中的 `map` 是通用项目名；本机配置不纳入版本控制。

map 已接入现有地图、本地脚本和共享云脚本。便携版不用安装 Python、CMake 或 Visual Studio；运行需要现有 KKWE、对应版本魔兽、地图和脚本文件。

## 双击使用

打开发行目录的 `ScriptRoom.exe`，先点“检查配置”，再点“启动”。首页显示项目概览、两端状态与云脚本日志。“项目设置”集中管理地图、脚本、存档和运行环境；勾选“另开云脚本控制台”可额外打开独立查看窗口。客户端原有控制台不受影响。

测试期间保持界面运行。“停止”只关闭共享云端；请先在两个游戏中正常保存、退出，再停止服务。每轮开始前应关闭上一轮两个游戏窗口。默认运行 3600 秒，可在界面修改。

需要保留整个便携文件夹，不能只复制一个 EXE。两个 EXE 共用 `_internal` 运行库，原生组件在 `build/Release`。日志在便携目录下的 `build/game`。

界面沿用 ShadowWorks 的紧凑布局，提供黑白灰、深灰蓝两种配色；顶部仅保留 ScriptRoom，首页以日志为主；设置页分组展示路径，运行环境与运行选项默认收起，点“展开”修改。保留 Windows 原生标题栏的移动、缩放和最大化。

在“设置 → 外观”选择主题，立即生效并自动保存。主题偏好保存在启动器旁的 `界面设置.json`，与地图的 `本地测试配置.json` 分开；切换主题保留未保存的路径编辑、日志和正在运行的云服务。

## 配置 map

首次打包会从项目根目录的 `本地测试配置.json` 导出绝对路径，沿用原来的存档，不创建另一套玩家数据。重新打包前请先关闭启动器。重新打包不会覆盖发行目录中已有的配置。界面修改在下次启动生效；点击“保存配置”后下次打开才能沿用。

```json
{
  "map": "D:/Code/map/test.w3x",
  "local_entry": "D:/Code/map/script/lightning_main.lua",
  "cloud_scripts": "D:/Code/MlScriptFolder/script_compile",
  "archives": "F:/code/scriptrunner/build/state/map",
  "editor": "D:/war3Project/KKWE插件",
  "source_game": "D:/war3Project/war3",
  "game": "F:/code/scriptrunner/build/warcraft-demo",
  "seconds": 3600,
  "startup_timeout": 90
}
```

- `map`：已有地图；本工具不编译或打包地图，需自行生成最新 `test.w3x`。
- `local_entry`：客户端本地 Lua 入口。
- `cloud_scripts`：直接包含 `main.lua` 的云脚本目录。
- `archives`：本项目存档根目录。玩家客户端 INI 分别为 `clients/player-0.ini`、`clients/player-1.ini`；云存档为 `slot-0.archive`、`slot-1.archive`，全局存档为 `global.sqlite3`。
- `source_game`：原始魔兽文件的来源。
- `game`：唯一的共用测试魔兽目录，两个客户端都用这个目录里的 `war3.exe`；不能与 `source_game` 相同。
- `editor`：已有 KKWE 插件及 Lua 运行库来源。

相对路径以配置文件所在目录为基准。换电脑需要重新选择路径。切换地图请另存一份 JSON，并选择独立的存档根目录，避免跨地图共用存档。

## 命令行

在发行目录打开 PowerShell：

```powershell
# 只检查路径、插件及支持的魔兽版本，不启动游戏
.\ScriptRoomCLI.exe --check

# 读取默认配置并启动，另开云脚本控制台
.\ScriptRoomCLI.exe

# 指定项目配置，运行两小时；不另开日志查看窗口
.\ScriptRoomCLI.exe --config "本地测试配置.json" --seconds 7200 --no-console

# 查看全部参数
.\ScriptRoomCLI.exe --help
```

可用 `--map`、`--local-entry`、`--cloud-scripts`、`--archives`、`--editor`、`--source-game`、`--game` 覆盖 JSON 字段。命令行 `Ctrl+C` 停止云服务；游戏窗口仍需正常退出。

## 开发与重新编译

```powershell
python -m pip install PyInstaller==6.15.0
python tools/package_launcher.py
```

输出 `build/package/ScriptRoom`，包含 GUI 和 CLI。开发环境需要 Python、CMake、VS 2022 C++ Win32 工具链；运行便携版不需要这些开发工具。源码也可执行 `python tools/launcher.py --gui` 或 `python tools/launcher.py --cli --check`。

当前地图启动器支持本机固定双人，共享唯一云脚本房间。已适配map 使用的 MLS 接口；其他地图需使用兼容接口与本地入口，不能仅修改路径就保证所有平台 API 可用。地图包、云脚本和魔兽资源不包含在便携包内。
