use dioxus::prelude::*;

use crate::gui::state::{EVENTS, EVENT_ROOM_FILTER};
use crate::gui::AppCtx;

#[component]
pub fn Events() -> Element {
    let ctx = use_context::<AppCtx>();
    let filter = EVENT_ROOM_FILTER();
    let events = EVENTS.read();

    let room_ids: Vec<String> = {
        let mgr = ctx.manager.read().unwrap();
        let mut v: Vec<String> = mgr.rooms.keys().cloned().collect();
        v.sort();
        v
    };

    // 末尾 500 条 (新的在下)，按过滤反向遍历
    let rows: Vec<_> = events
        .iter()
        .rev()
        .filter(|e| filter.is_empty() || e.room_id == filter)
        .take(500)
        .collect();

    rsx! {
        div { class: "events",
            div { class: "toolbar",
                label { "房间:" }
                select {
                    value: "{filter}",
                    onchange: move |e| *EVENT_ROOM_FILTER.write() = e.value(),
                    option { value: "", "全部" }
                    for id in room_ids {
                        option { value: "{id}", selected: filter == id, "{id}" }
                    }
                }
                span { class: "muted small", "事件数: {events.len()}" }
            }
            div { class: "table evt-table",
                div { class: "thead",
                    div { "时间" }
                    div { "房间" }
                    div { "玩家" }
                    div { "事件" }
                    div { "数据" }
                }
                div { class: "tbody",
                    for evt in rows.iter().rev() {
                        div { class: "tr",
                            div { class: "td mono small", {format_ts(evt.timestamp)} }
                            div { class: "td", "{evt.room_id}" }
                            div { class: "td",
                                {if evt.player_index < 0 { "*".to_string() } else { evt.player_index.to_string() }}
                            }
                            div { class: "td", "{evt.ename}" }
                            div { class: "td mono small ellipsis", "{evt.evalue}" }
                        }
                    }
                }
            }
        }
    }
}

fn format_ts(ts: f64) -> String {
    let secs = ts as i64;
    let ms = ((ts - secs as f64) * 1000.0) as i64;
    let h = (secs / 3600) % 24;
    let m = (secs / 60) % 60;
    let s = secs % 60;
    format!("{:02}:{:02}:{:02}.{:03}", h, m, s, ms)
}
