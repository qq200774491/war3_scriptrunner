use dioxus::prelude::*;

use crate::gui::state::{STATE_JSON, STATE_ROOM};
use crate::gui::AppCtx;

#[component]
pub fn StateView() -> Element {
    let ctx = use_context::<AppCtx>();
    let room_ids: Vec<String> = {
        let mgr = ctx.manager.read().unwrap();
        let mut v: Vec<String> = mgr.rooms.keys().cloned().collect();
        v.sort();
        v
    };
    let selected = STATE_ROOM().clone();

    let refresh_for = {
        let ctx = ctx.clone();
        move |rid: String| {
            let mgr = ctx.manager.read().unwrap();
            if let Some(room) = mgr.rooms.get(&rid) {
                if let Ok(shared) = room.shared.try_read() {
                    let json = serde_json::to_string_pretty(&shared.to_json())
                        .unwrap_or_else(|e| format!("序列化失败: {}", e));
                    *STATE_JSON.write() = json;
                }
            }
        }
    };

    let refresh = {
        let refresh_for = refresh_for.clone();
        move |_| {
            if let Some(rid) = STATE_ROOM.peek().clone() {
                refresh_for(rid);
            }
        }
    };

    rsx! {
        div { class: "state-view",
            div { class: "toolbar",
                label { "房间:" }
                select {
                    value: selected.clone().unwrap_or_default(),
                    onchange: {
                        let refresh_for = refresh_for.clone();
                        move |e| {
                            let v = e.value();
                            if v.is_empty() {
                                *STATE_ROOM.write() = None;
                                STATE_JSON.write().clear();
                            } else {
                                *STATE_ROOM.write() = Some(v.clone());
                                refresh_for(v);
                            }
                        }
                    },
                    option { value: "", "（请选择）" }
                    for id in room_ids {
                        option { value: "{id}", selected: selected.as_deref() == Some(id.as_str()), "{id}" }
                    }
                }
                button { class: "btn", onclick: refresh, "刷新" }
            }
            textarea {
                class: "json-area",
                readonly: true,
                value: "{STATE_JSON}",
            }
        }
    }
}
