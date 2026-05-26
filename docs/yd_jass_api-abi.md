# yd_jass_api / ydbase 实证 ABI（Track 0 产出）

为 mls-dll 的 jass 桥提供精确的 FFI 签名。所有签名都来自 dumpbin 实证（C-linkage 看 exports，C++ 看 mangled name 解码）+ AddJApi 反汇编验证 + 老 C++ 项目（`D:\Code\japi\`）对照。

## 关键发现总览

| 项 | 结论 |
|---|---|
| `AddJApi` 是否唯一 C 入口 | **是**。`yd_jass_api.dll` 只导出 3 个 C 符号：`AddJApi`、`Initialize`、`PluginName`。无 `HookJApi`。 |
| `ydbase.dll` 是否真实可加载 | **是**。位于 `D:\war3Project\KKYDWE\bin\ydbase.dll`（不在 plugin/warcraft3/）；yd_jass_api 已 import 它，进程内已 loaded。 |
| 调用约定 | 全部 `__cdecl`（mangled 末尾无 `@N`，AddJApi 反汇编也是 `C3 ret` 而非 `RET 4/8/c`）|
| Hook 路径 | 必须直接通过 `ydbase::japi_hook`（mangled），yd_jass_api 没有 hook 入口 |
| 老 C++ `japi_register` | inline asm 调 jass.dll 的 `japi_add`（fastcall hack）。**新路径用 ydbase cdecl 版本，无需 asm**。 |

## 加载策略

`yd_jass_api.dll` 与 `ydbase.dll` 在进程启动时已经被 KKYDWE loader 链上，但稳妥起见在 `Initialize()` 里再 `LoadLibraryW` 一次（仅是引用计数 +1），然后 `GetProcAddress` 解析所有需要的符号：

```rust
// 伪代码示意
let h_yd = LoadLibraryW("yd_jass_api.dll")?;
let h_ydbase = LoadLibraryW("ydbase.dll")?;

// AddJApi 是 C-linkage 直接名字
let p_AddJApi = GetProcAddress(h_yd, "AddJApi")?;

// 其余 ydbase 函数用 mangled 全名查
let p_japi_add  = GetProcAddress(h_ydbase, "?japi_add@jass@warcraft3@base@@YA_NIPBD0@Z")?;
let p_japi_hook = GetProcAddress(h_ydbase, "?japi_hook@jass@warcraft3@base@@YA_NPBDPAII@Z")?;
// ...
```

**为什么不全走 AddJApi**：AddJApi 把签名格式 `"(SIS)I"` 转成 func_value 后塞 `japi_function` 全局 map；行为等价于直接调 `japi_add`，但内部多一层 C++ string 构造。两条路都可用，我们选 `japi_add`（cdecl 直接，签名精确）。Hook 必须走 `japi_hook` 因为没别的入口。

## 实证签名表

### yd_jass_api.dll（C-linkage exports）

| 名字 | RVA | 用途 | Rust 类型 |
|---|---|---|---|
| `AddJApi` | 0x1EBB0 | 注册 jass native（高级 wrapper，内部走 japi_add）| `extern "cdecl" fn(uintptr, *const c_char, *const c_char)` —— 推断签名 `(func_addr, name, sig_str)`，参数顺序由 `japi_register.cpp:34-50` 反推：老 inline asm 调老 jass.dll 的 `japi_add` 时 ecx=address、edx=name、push=params，所以新版 cdecl 等价于 `(address, name, params)` |
| `Initialize` | 0x1EC40 | KKYDWE plugin loader 触发 | `extern "C" fn()` |
| `PluginName` | 0x1EC50 | KKYDWE 校验名 | `extern "C" fn() -> *const c_char` |

### ydbase.dll（C++ mangled exports — 我们直接调用的）

```rust
// 注册 native（首选路径，bypass AddJApi 的 C++ string 构造开销）
// mangled: ?japi_add@jass@warcraft3@base@@YA_NIPBD0@Z
type JapiAdd = extern "cdecl" fn(
    func_addr: u32,       // I = unsigned int
    name: *const c_char,  // PBD = const char*
    params: *const c_char // 0 = repeat last type = const char*
) -> bool;                // _N = bool

// Hook 现有 native
// mangled: ?japi_hook@jass@warcraft3@base@@YA_NPBDPAII@Z
type JapiHook = extern "cdecl" fn(
    name: *const c_char,    // PBD
    out_old_proc: *mut u32, // PAI = unsigned int*
    new_proc: u32           // I
) -> bool;

// 取消 hook（cleanup 用）
// mangled: ?japi_unhook@jass@warcraft3@base@@YA_NPBDPAII@Z
type JapiUnhook = extern "cdecl" fn(*const c_char, *mut u32, u32) -> bool;

// 创建 jass string，返回 jass string handle（uint32）
// mangled: ?create_string@jass@warcraft3@base@@YAIPBD@Z
type CreateString = extern "cdecl" fn(*const c_char) -> u32;

// 从 jass string handle 反解 C 字符串（指针仅在当前 native 帧有效，必须立即拷贝）
// mangled: ?from_string@jass@warcraft3@base@@YAPBDI@Z
type FromString = extern "cdecl" fn(u32) -> *const c_char;

// jass real ↔ float
// mangled: ?from_real@jass@warcraft3@base@@YAMI@Z   (M = float)
type FromReal = extern "cdecl" fn(u32) -> f32;
// mangled: ?to_real@jass@warcraft3@base@@YAIM@Z
type ToReal = extern "cdecl" fn(f32) -> u32;

// 按 NAME 调 jass 函数（varargs cdecl）—— 主要拿来调 GetPlayerId、CreateTimer 等内建 native
// mangled: ?call@jass@warcraft3@base@@YAIPBDZZ
type CallByName = extern "cdecl" fn(*const c_char, ...) -> u32;

// handle → object（实际地址）
// mangled: ?handle_to_object@warcraft3@base@@YAII@Z
type HandleToObject = extern "cdecl" fn(u32) -> u32;

// 查 japi_function 全局 map：name → func_value*
// mangled: ?japi_func@jass@warcraft3@base@@YAPBVfunc_value@123@PBD@Z
type JapiFunc = extern "cdecl" fn(*const c_char) -> *const c_void; // const func_value*
```

签名字符串格式（用于 japi_add / AddJApi 的第三个参数）：

| 字母 | 类型 |
|---|---|
| `I` | int32 / handle (uint32) |
| `S` | string (`const char*`) |
| `C` | code (jass function handle，uint32) |
| `H` | handle (uint32) |
| `B` | boolean (int32，0/1) |
| `V` | void |
| `R` | real (float) |

例：`"(SIS)I"` = `(string, int, string) -> int`；`"()V"` = `() -> void`。

## AddJApi 反汇编片段（手工验证用）

```
1001EBB0: 55                 push        ebp
1001EBB1: 8B EC              mov         ebp,esp
1001EBB3: 6A FF              push        0FFFFFFFFh        ; SEH frame
...
1001EBD5: FF 75 0C           push        [ebp+0Ch]          ; AddJApi arg2 → 进 func_value 构造（推断为 sig 字符串或派生字段）
1001EBD8: 8D 4D D4           lea         ecx,[ebp-2Ch]      ; this = 本地 func_value 槽
1001EBDB: FF 75 10           push        [ebp+10h]          ; AddJApi arg3 → func_value ctor 第一参（const char* name 或同类）
1001EBDE: FF 15 BC 41 05 10  call        [func_value::ctor] ; 100541BCh 是 ?func_value@... 的 IAT 槽
1001EBE4: 8B 4D 08           mov         ecx,[ebp+8]        ; AddJApi arg1 = 函数地址
1001EBE7: 89 4D B8           mov         [ebp-48h],ecx      ; 存入 func_value::func_address_
...
1001EC32: C3                 ret                            ; cdecl 返回
```

**结论**：`AddJApi(arg1, arg2, arg3)` —— 等价于 `japi_add(arg1=func_addr, arg2=...?, arg3=name?)` 的某种包装，但参数顺序未 100% 凿穿。**所以 production 路径直接走 `japi_add(uint addr, const char* name, const char* params)`**（mangled name 给定唯一签名，无歧义）。

## ⚠️ 重要设计发现：jass code handle 调用机制不存在于老 C++ 中

Plan 的"已知陷阱 #3"假设老 `mls_module.cpp:147-176` 里 `MlsPump` 包含 jass code handle 触发的范式。**实测：那段实现走的是 `DzSyncData(prefix, json)` —— 即被 plan 显式拒绝的 DzSync 分块通道**，并非直接调 code handle。

我们要的"DLL 把 s2c event 派发给 lua 注册的 callback"机制，在老 C++ 项目中**没有真实参考**。可选路径：

### 路径 A — Polling（推荐）
不需要从 C 里调 jass code handle。DLL 暴露：
- `MlsNextEvent() -> string`（返回下一条 s2c event 的 JSON，空队列返回 `""`；老 mls_module.cpp:178-193 是此模式的可工作蓝本，**仅 chunking 逻辑不复刻**）
- `MlsSendMlEvent(pi, ename, evalue) -> bool`（c2s）
- 一切 `RegisterEvent` 在 lua 侧用 lua 表实现（指向 lua function 而非 jass code handle）
- lua 侧用 ~50ms timer poll `MlsNextEvent`，dispatch 给 lua 表里的 callback

**优点**：DLL 完全不碰 jass code handle，最简单；与 仙途 现有的 `api.mls "ename" (cb)` 事件总线天然对齐；DLL 体积小。
**代价**：lua 侧多一个小 glue 文件（约 30 行），把 polling + dispatch 包成 `RegisterEvent` 给业务用。

### 路径 B — Trigger + Globals（复杂，不推荐）
在 DLL 里维护 trigger 池，每个 RegisterEvent 创建一个 trigger + TriggerAddAction(trig, code)。s2c event 到来时，先 set jass globals（id/ename/evalue/pi），再 TriggerExecute。callback 内读 globals。
**问题**：要在 DLL 里调 `CreateTrigger`、`TriggerAddAction`、`TriggerExecute`、设全局变量 —— 都要走 `jass::call(name, args...)`；setglobal 还涉及 jass VM 内部结构。复杂度高，没有可工作蓝本。

### 路径 C — DzSync（违反 plan 硬约束）
就是老 C++ 的方式。但 plan 禁止 chunking，所以 evalue 超 880B 会被 DzSync 自身限制截断 —— 实际等同于 drop。**形式上还引入 chunking 协议依赖**，违反"复刻平台 900B 限制"原则。**排除**。

## 推荐落地

走路径 A。Track 1-4 落实方案：

- Track 1 不变：japi loader + RequestExtraBooleanData hook（c2s 入口）
- Track 2 调整：注册的 native 改为：
  - `MlsNextEvent() -> string`（参考老 mls_module.cpp:180-193）
  - `MlsSendMlEvent(pi:int, ename:string, evalue:string) -> bool` （c2s 同步路径）
  - `MsGetRoomStartTs/LoadedTs/GameTime/PlayerCount/ModeId() -> int`（从 s2c 元数据 atomic 缓存读）
  - **不注册 RegisterEvent/UnregisterEvent**（这俩在 lua 实现）
- Track 3：dispatch 模块退化为单一队列 + `MlsNextEvent`。不再有 `Native_MlsPump` 调 code handle 的复杂性
- Track 4：lua glue 包含：
  ```lua
  local _registry = {}      -- ename → list of lua callbacks
  function RegisterEvent(ename, cb)
      _registry[ename] = _registry[ename] or {}
      table.insert(_registry[ename], cb)
      return #_registry[ename]
  end
  -- 50ms 周期 dispatch
  local function pump()
      while true do
          local json = mls.dll.MlsNextEvent()
          if json == "" then break end
          local ev = cjson.decode(json)
          for _, cb in ipairs(_registry[ev.ename] or {}) do
              cb(_, ev.ename, ev.evalue, ev.player_index)
          end
      end
  end
  ```

5 跳验收链不受影响 —— 最终一跳"lua 那侧 cb 实际执行"由 lua-glue 的 pump dispatch 完成，业务感受不到差异。

---

## 加载顺序提醒

`Initialize()` 是 jass init 的安全落点（KKYDWE loader 在 yd_loader 之后调，jass VM 此时已就绪）。**不要把 jass 调用放 DllMain** —— DllMain 在 LoadLibrary 时跑，jass VM 未启动。

`Initialize()` 内部要做：
1. `japi::init()` —— LoadLibrary + GetProcAddress 一切 jass FFI 符号
2. `hook_reb::install()` —— `japi_hook("RequestExtraBooleanData", Fake_REB)`
3. `natives::register_all()` —— 调 `japi_add` × N 注册 `MlsNextEvent`、`MlsSendMlEvent`、`MsGet*`
4. 全包 `std::panic::catch_unwind`

整段是同步操作，几毫秒内完成。完成后 lua loader 端的 `loader.mss(self)` 调过 `self.dll.Initialize()` 就立刻可用。
