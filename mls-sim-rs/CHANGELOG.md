# Changelog

## Unreleased

### 视觉一致性升级

GUI 改用语义化色板 + 状态点 + chip + accent 视觉系统，整体一致性显著提升。

- 新建 `src/gui/palette.rs` 单一色源 + UI helper
  - 背景 / 文本 / 描边 / 强调色 / 状态色全部语义命名（`SUCCESS / WARNING / DANGER / ACCENT / TEXT_DIM` 等）
  - 全部硬编码 `Color32::from_rgb(...)` 字面量收敛到一处
  - 提供 `status_dot / level_chip / chip / empty_state / accent_bar / underline` 等可复用 widget
- **Tab 栏选中态** 加 accent 2px 底部下划线，扫一眼可识别
- **Section 标题** 加左侧 3px accent 竖条，提升视觉层级
- **房间侧栏卡片化** — 房间项首行 ● 状态点 + 房间名，次行小字 dim 灰显示状态/人数
- **日志等级 chip** — `[ERR] [INF] [DBG]` 改为填充圆角 chip（背景色随级别变化），一眼可扫
- **空状态升级** — "暂无房间"、"请选择房间查看状态" 等改为居中大灰图标 + 标题 + 操作提示（如 `↑↓ 切换 · N 新建 · / 命令`）
- **删除确认模态** 加 ⚠ 图标 + 配色突出 warning/danger
- **命令栏 popup 高亮** 改用 `ACCENT_BG`，配色与全局选择态一致
- Profiler 配色（pct_bar / ms_color / 按钮 ▶ ■）接入 palette
- 设置、出站事件等剩余 tab 全部走 palette，移除散落的颜色字面量

### 新功能 — 键盘快捷键全覆盖

GUI 现在按"人类操作习惯"全键盘可达，所有快捷键按 `F1` 查看速查表。

**全局**
- `Ctrl+1` … `Ctrl+6` 切到对应标签页（标签栏会显示数字提示，如 `房间 (1)`）
- `Ctrl+Tab` / `Ctrl+Shift+Tab` 循环切换标签页
- `/` 或 `Ctrl+L` 聚焦底部命令栏（`/` 仅在无输入框聚焦时触发，避免抢键）
- `F1` 显示/隐藏键盘快捷键速查表
- `Esc` 优先级降级：关闭弹窗 → 清空命令栏 → 失焦

**命令栏**
- `↑` / `↓` 回溯/前进命令历史（最近 100 条，连续去重，按 Enter 后游标重置）
- `Tab` / `Shift+Tab` 循环候选补全：
  - 命令名前缀匹配
  - `select/stop/destroy/restart/join/leave/exit/event` 第 2 位补全房间 ID
  - `join/leave/exit` 第 3 位补全玩家槽位
  - `event` 第 3 位补全 `*` + 玩家槽位，第 4 位补全最近用过的事件名
- `Enter` 执行命令，焦点保留在输入框（连发不丢焦）
- 命令栏 popup 高亮当前选中候选

**房间标签**
- `↑` / `↓` 在房间列表中切换选中（环绕；自动滚动到可见区域）
- `N` 切换"新建房间"表单
- `Delete` 请求删除当前选中房间（弹二次确认）
- "添加玩家" / "发送事件" 表单按 `Enter` 即可提交

**控制台标签**
- `Ctrl+F` 聚焦搜索框
- `Ctrl+K` 清空日志
- 搜索框聚焦时按 `Esc` 清空内容

### 改进
- 顶栏右侧新增 `/ 命令 · F1 帮助` 常驻提示
- 删除房间从直接执行改为"二次确认模态" — 防止误触造成不可恢复操作
- 命令栏 popup 在命令名位置显示完整用法（含参数形态），帮助用户发现参数语法

### BREAKING CHANGES
- **`event` 命令语法变更**：旧 `event <房间> <事件名> [数据] [玩家]` 因末位推断 player_index 在数字 `evalue` 上有歧义 bug（如 `event r1 score 0 123` 会把 `123` 误吞为 player_index）。
  - 新语法：`event <房间> <事件名> <玩家|*> [数据...]`
  - `<玩家>` 强制为第 3 位，`*` 或 `-1` 或 `全部` 表示广播；数据从第 4 位起空格拼接。
  - 旧脚本/操作记忆需要调整位置。GUI 内的「发送事件」表单不受影响（始终用结构化字段）。

### 内部
- 命令解析逻辑（`split_command_args` / 补全候选 / 历史游标 / `event` 解析）抽到独立 `src/gui/parse.rs`，新增 40 个单元测试覆盖。

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
