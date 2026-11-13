# 构建依赖

`detours/src`、`detours/LICENSE.md`、`detours/CREDITS.TXT` 和 `detours/README.md` 来自 Microsoft Detours v4.0.1，提交 `e4bfd6b03e50de46b47abfbd1e46b384f0c5f833`，MIT 许可。

上游：https://github.com/microsoft/Detours

源码随本仓库保存，CMake 直接构建静态库，无需递归克隆或上游 Makefile。开发机参考克隆的 Git 元数据和 samples 不纳入提交。

quicktest 游戏适配来源及 MIT 许可分别见 `native/quicktest/README.md` 和 `native/quicktest/LICENSE`。

安装向导的简体中文语言文件 `inno/ChineseSimplified.isl` 来自 Inno Setup 官方源码仓库，保留译者说明；对应许可见 `inno/LICENSE.TXT`。

来源：https://github.com/jrsoftware/issrc/blob/a942f923d84b7a227b49af1721a1dc4492831ec0/Files/Languages/ChineseSimplified.isl

## 启动器（`launcher/`）使用的依赖

`sqlite3/sqlite3.c`、`sqlite3/sqlite3.h` 是 SQLite 3.44.0 的合并源码，取自 rusqlite 0.30 随附的 libsqlite3-sys 0.27.0；SQLite 属公有领域。来源：https://sqlite.org/

`nlohmann/json.hpp` 是 nlohmann/json v3.11.3 的单头文件，MIT 许可，见 `nlohmann/LICENSE.MIT`。来源：https://github.com/nlohmann/json/releases/tag/v3.11.3

图形界面库 BUI（GPL-3.0-or-later）不随仓库保存，由 `launcher/gui/CMakeLists.txt` 在配置时按固定提交拉取：https://github.com/ppmpreetham/BUI ，提交 `e9783edceeac595302c630ebbb650391bc8bb41d`。分发包含 BUI 的启动器时，必须按 GPL 提供对应源码。
