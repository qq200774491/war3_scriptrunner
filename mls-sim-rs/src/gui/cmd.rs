// 直接复用 gui_legacy/parse.rs（egui-free，含完整单元测试）。
// gui_legacy 整体不被 mod 链覆盖，但通过 #[path] 单独把 parse.rs 拉进编译。
//
// parse.rs 里 CommandHistory / push_recent_event / matching_command_hints / MAX_*
// 这些项在新 Dioxus GUI 还没接入（旧 egui 用过），但保留作为"已测试的命令行库"
// shelf，方便后续接 history popup / event hint 等。`#[allow(dead_code)]` 一次性
// 盖掉 dead_code 警告——这是有意保留，不是真的无用。
#[path = "../gui_legacy/parse.rs"]
#[allow(dead_code)]
mod inner;

pub use inner::{
    apply_completion, compute_completions, parse_event_command, split_command_args,
};
