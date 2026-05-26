// 直接复用 gui_legacy/parse.rs（egui-free，含完整单元测试）。
// gui_legacy 整体不被 mod 链覆盖，但通过 #[path] 单独把 parse.rs 拉进编译。
#[path = "../gui_legacy/parse.rs"]
mod inner;

pub use inner::{
    apply_completion, compute_completions, parse_event_command, split_command_args,
    CommandHistory, COMMAND_HINTS,
};
