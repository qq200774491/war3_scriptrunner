# quicktest 适配层

game_hooks.cpp 派生自 shuen4/war3_quicktest，提交 17f4099607f8759802e63ce67927875f9b4bb58f；许可证见 LICENSE。上游源码见 https://github.com/shuen4/war3_quicktest/tree/17f4099607f8759802e63ce67927875f9b4bb58f。

本适配支持 Warcraft III 1.27，并检查游戏文件 SHA256。使用 KKWE 的 PluginName/Initialize 插件入口，DllMain 不执行 hook 或等待；外部 Python 启动器按每场唯一命名事件先等主机，再启动加入端。会话参数与日志在 session.cpp。内部地址、网络结构与建房流程来自上游。

地图传入游戏目录内的短相对 Windows 路径；Lua 配置序列化保留反斜杠。房间描述的插入区间使用同一个命名数组，不依赖编译器合并字面量。菜单回调捕获 C++ 异常并写入诊断日志，避免异常越过游戏 ABI。

依赖 Microsoft Detours v4.0.1，第三方源码与许可证保存在 third_party/detours。使用本仓库 CMake 构建。
