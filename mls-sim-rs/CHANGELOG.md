# Changelog

## v0.6.0 (2026-05-26)

### 新功能 — GUI 大爆炸式迁移到 Dioxus 0.7

GUI 整片由 **egui 即时模式**迁移到 **Dioxus 0.7 desktop**（基于 webview / RSX 声明式），主窗口标题由「MLS 云脚本模拟器」改为「MLS 云脚本环境模拟」。

- 全新组件树：`src/gui/components/` —— 控制台、设置、性能分析、关于页面（`about.rs` 新增）等子页面用 RSX 表达
- 协程驱动：`src/gui/coroutines.rs` 接管原 egui 主循环的状态推进；样式集中到 `src/gui/style.css`
- 旧版 egui 实现作为 **shelf 库**保留在 `src/gui_legacy/`（不在 `mod` 链中），其中 `parse.rs`（命令栏解析 + 补全候选 + 历史游标 + `event` 命令解析）+ 40 个单元测试通过 `#[path = "../gui_legacy/parse.rs"]` 单独编译复用——未来 Dioxus 接入 history popup / event hint 时不必重写

> ⚠️ 前一版 `Unreleased` 描述的视觉一致性升级、键盘快捷键全覆盖、删除确认模态等 egui-era UI 功能**未跟随发布**，对应实现整体被 Dioxus 重写取代；要参考其设计意图请看 `gui_legacy/` 源码。

### 新增配套客户端 DLL — `mls-dll`（独立 crate）

仓库新增 `mls-dll/` —— 32-bit Windows cdylib，作为 KKYDWE 插件挂进 war3 / 编辑器，把仙途等真业务客户端与 mls-sim 服务端打通。

- **pipe 层**：纯 Rust 复刻老 C++ `mls/pipe_client.cpp` + `codec.cpp`，OVERLAPPED I/O，3 秒心跳 + 退避重连；DllMain 直接通过 sentinel 文件 / 环境变量触发，无需额外 host 进程
- **jass 桥（本次主线工作）**：通过 `LoadLibraryA + GetProcAddress` 解析 `yd_jass_api.dll` + `ydbase.dll` 的 cdecl mangled 符号，调用 sanctioned 路径——彻底告别老 C++ inline asm + `GetModuleHandleA("jass.dll")` 那套
  - `async_hook("RequestExtraBooleanData", ...)`：拦截 `dataType==1009`（玩家→云脚本上行），合法 c2s 进 send queue；其他 dataType 透传原 native；超长 ename(>32B) / evalue(>900B) 直接 drop + WRN（**复刻平台真实限制，不做分块**）
  - 注册 7 个 JASS native：`MlsNextEvent`（pop 一条 s2c 事件返 JSON）、`MsSendMlEvent`（lua-level c2s）、`MsGetRoom{StartTs,LoadedTs,GameTime,PlayerCount,ModeId}`（atomic 缓存的房间元信息）
  - **路径 A 设计**：DLL 不持有 jass code handle，`RegisterEvent` / `UnregisterEvent` 完全留给 lua glue 实现 —— 因为老 C++ `mls_module.cpp` 走 DzSync 而非 code-handle 派发，没有可工作蓝本，硬上反而要 trigger + globals + ExecuteFunc 一堆复杂度
- `docs/yd_jass_api-abi.md`：实证 ABI 调研（mangled 名、calling convention、AddJApi 反汇编片段）
- `docs/mls-bridge-glue.lua`：war3 客户端 lua glue 参考（`RegisterEvent` 注册表 + 50ms pump + `centertimer` / jass timer 两套接法）
- `docs/jass-bridge-e2e.md`：5 跳端到端验证步骤 + 排查矩阵

### 修复
- `mls-dll` 与 `ydbase` 交互全部包 `std::panic::catch_unwind` —— jass 调用栈穿透 Rust panic = war3 直接崩。`Cargo.toml` 释放配置由 `panic = "abort"` 改为 `panic = "unwind"` 让兜底真生效
- `hook_reb::install` / `natives::register_all` / `Initialize` 全部加 idempotent 守卫 —— KKYDWE 编辑器侧 + war3 侧可能各调一次 `Initialize()`，没守卫会把自己 hook 自己导致 dt!=1009 路径栈溢出

### 内部
- `src/gui/cmd.rs` 清理：删除空 re-export `CommandHistory` / `COMMAND_HINTS`（新 Dioxus GUI 还没接入），给 `mod inner` 加 `#[allow(dead_code)]` 一次性盖掉 6 条 `gui_legacy/parse.rs` 的 dead_code 警告 —— shelf 库有意保留，不是无用
- 仓库根新增 `tools/pipe_test.py` —— 不依赖 war3 的 named-pipe 直连测试脚本，方便快速验证 mls-sim 的 pipe 服务端

## v0.5.3 (2026-05-26)

### 新功能
- 新增快捷命令功能，命令输入栏
  - `rooms`
  - `create <脚本目录> [模式]`
  - `select <房间>`
  - `stop <房间>`
  - `destroy <房间>`
  - `restart <房间>`
  - `join <房间> <槽位> [名称]`
  - `leave <房间> <槽位>`
  - `exit <房间> <槽位>`
  - `event <房间> <事件名> [数据] [玩家]`

### 改进
- 命令输入栏会根据输入内容显示悬浮命令提示
- 命令输入栏获得焦点且内容为空时，会悬浮显示全部命令
- 程序窗口失去激活焦点时，命令输入栏会释放焦点

## v0.5.2 (2026-05-10)

### 修复
- 限制 Lua 标准库以对标 MLS 线上环境
  - `os` 模块仅保留 `difftime`、`clock`、`time`、`date`，移除 `execute`、`exit`、`getenv`、`remove`、`rename`、`setlocale`、`tmpname`
  - 完全移除 `io` 模块（VM 层面不加载）
  - 完全移除 `debug` 模块

## v0.5.1 (2026-05-10)

### 修复
- 修复自定义 `require` 不识别 Lua 5.3 内置模块的问题
  - `require 'utf8'`、`require 'math'`、`require 'string'` 等调用会报 `module not found`
  - 在文件搜索前新增全局表查找，已注册的内置模块（utf8、math、string、table、coroutine、os 等）和项目自定义模块（json、Log、Timer 等）均可通过 `require` 正常加载

## v0.5.0 (2026-04-28)

### 新功能
- 新增 Rust 原生采样式性能分析器（Sampling Profiler）
  - 基于 `mlua::Lua::set_hook` + `inspect_stack` 实现，零 Lua 分配、零 GC 压力
  - 按秒分桶聚合调用树，滑动窗口自动过期，合并结果缓存
  - 函数名首次解析后缓存，C 函数按名称区分（`[C]:print`、`[C]:MsGetPlayerName`）
- 新增 GUI「性能分析」Tab
  - 火焰图渲染：Lua 函数暖色、C/API 冷色，文本裁剪，Hover Tooltip
  - 热点函数表：自身/总计占比（带比例条）、ms/帧 估算、颜色分级
  - 录制指示灯（脉冲动画）、采样频率/窗口/帧时间可配置
  - 底部面板可拖拽调整高度

## v0.4.6 (2026-04-25)

### 新功能
- 新增本地调试控制接口 `/api/debug/*`（由 Syh1906 贡献，PR #2）
  - `GET /api/debug/rooms/{room_id}/logs` — 查看房间日志
  - `POST /api/debug/rooms/{room_id}/logs/clear` — 清除房间日志
  - `POST /api/debug/rooms/{room_id}/restart` — 重启房间
  - `POST /api/debug/restart` — 服务重启（预留）
- 提取 `build_bridge_router()` 为可复用的路由构建函数
- 新增 `lib.rs` 库入口，支持集成测试
- 新增 debug API 集成测试 (`tests/debug_api.rs`)

## v0.4.5 (2026-04-25)

### 修复
- 修复 json.encode 分页传输中文键名损坏的问题
  - send_paged 按字节切割 JSON 字符串时可能断在 UTF-8 多字节字符中间，v0.4.4 的 serde_json 编码器会将不完整 UTF-8 字节转为 Latin-1 字符（单字节 0xE6 → 双字节 C3 A6），导致客户端拼接后 json.decode 失败或键名对不上
  - json.encode 改为直接构建原始字节输出（Vec\<u8\>），字符串逐字节透传只做 JSON 必要转义，与 cjson/KK 平台行为一致
  - json.decode 保持 serde_json 不变

## v0.4.4 (2026-04-25)

### 改进
- 重写 json.encode / json.decode：从 Lua 手写实现替换为 Rust (serde_json) 实现，对齐 cjson 标准
  - 空 Lua 表 `{}` 序列化为 JSON object `{}`（与 KK 对战平台一致），不再错误地序列化为 `[]`
  - JSON 合规性由 serde_json 保证（Unicode 转义、数字精度、嵌套结构等）
  - 非 UTF-8 Lua 字符串安全降级处理

## v0.4.3 (2026-04-25)

### 新功能
- 新增 `--console-notwrte` 启动参数，可隐藏 Windows 控制台窗口，仅保留 GUI 界面

### 修复
- Lua 脚本中使用 `print()` 现在会输出到 GUI 控制台（INF 级别），不再只写 stdout

## v0.4.2

### 修复
- 修复非 UTF-8 字节转义格式导致客户端 JSON 解析失败的问题

## v0.4.1

### 修复
- 修复存档保存问题——立即落盘、优雅关闭、退出前持久化
- lua_value_to_string 逐字节解析 UTF-8，分页切断处不再整串转义
- lua_value_to_string() 改为先尝试 to_str() 处理合法 UTF-8，非 UTF-8 时用无损转义
- esc() 函数补充转义 JSON 规范不允许的控制字符，产生合规 JSON 输出
