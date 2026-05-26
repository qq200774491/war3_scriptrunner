use dioxus::prelude::*;

use crate::gui::state::{DELETE_CONFIRM, SELECTED_ROOM, SHOW_KEYBINDINGS};
use crate::gui::AppCtx;

const KEYBINDINGS: &[(&str, &str, &str)] = &[
    ("全局", "Ctrl+1 .. Ctrl+6", "切到对应标签页"),
    ("全局", "Ctrl+Tab / Ctrl+Shift+Tab", "循环切换标签页"),
    ("全局", "/ 或 Ctrl+L", "聚焦命令栏"),
    ("全局", "F1", "显示/隐藏本帮助"),
    ("全局", "Esc", "关闭弹窗 / 清空输入 / 失焦"),
    ("命令栏", "↑ / ↓", "回溯/前进命令历史"),
    ("命令栏", "Tab / Shift+Tab", "循环候选补全"),
    ("命令栏", "Enter", "执行命令"),
    ("房间", "↑ / ↓", "在房间列表中切换选中"),
    ("房间", "N", "切换\"新建房间\"表单"),
    ("房间", "Delete", "删除当前选中房间（需二次确认）"),
    ("控制台", "Ctrl+F", "聚焦搜索框"),
    ("控制台", "Ctrl+K", "清空日志"),
];

#[component]
pub fn DeleteConfirmModal() -> Element {
    let ctx = use_context::<AppCtx>();
    let id = match DELETE_CONFIRM().clone() {
        Some(id) => id,
        None => return rsx! {},
    };

    let confirm = {
        let id = id.clone();
        let ctx = ctx.clone();
        move |_| {
            ctx.manager.write().unwrap().destroy_room(&id);
            if SELECTED_ROOM.peek().as_deref() == Some(id.as_str()) {
                *SELECTED_ROOM.write() = None;
            }
            *DELETE_CONFIRM.write() = None;
        }
    };

    rsx! {
        div { class: "modal-overlay", onclick: move |_| *DELETE_CONFIRM.write() = None,
            div { class: "modal", onclick: move |e| e.stop_propagation(),
                h3 { "确认删除" }
                p { "确定要删除房间 " span { class: "mono accent", "{id}" } " 吗？此操作不可撤销。" }
                div { class: "actions",
                    button { class: "btn", onclick: move |_| *DELETE_CONFIRM.write() = None, "取消" }
                    button { class: "btn btn-danger", onclick: confirm, "删除（Enter）" }
                }
            }
        }
    }
}

#[component]
pub fn KeybindingsModal() -> Element {
    if !SHOW_KEYBINDINGS() {
        return rsx! {};
    }
    rsx! {
        div { class: "modal-overlay", onclick: move |_| *SHOW_KEYBINDINGS.write() = false,
            div { class: "modal modal-wide", onclick: move |e| e.stop_propagation(),
                div { class: "modal-header",
                    h3 { "快捷键帮助" }
                    button { class: "x", onclick: move |_| *SHOW_KEYBINDINGS.write() = false, "✕" }
                }
                div { class: "kb-table",
                    div { class: "kb-row kb-head",
                        div { "范围" } div { "按键" } div { "说明" }
                    }
                    for (scope, keys, desc) in KEYBINDINGS.iter() {
                        div { class: "kb-row",
                            div { class: "kb-scope", "{scope}" }
                            div { class: "kb-keys mono", "{keys}" }
                            div { class: "kb-desc", "{desc}" }
                        }
                    }
                }
            }
        }
    }
}
