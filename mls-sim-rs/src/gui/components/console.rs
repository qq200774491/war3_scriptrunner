use dioxus::prelude::*;

use crate::gui::state::{AUTO_SCROLL, LOGS, LOG_LEVEL_FILTER, LOG_ROOM_FILTER, LOG_SEARCH};
use crate::gui::AppCtx;

#[component]
pub fn Console() -> Element {
    let ctx = use_context::<AppCtx>();
    let level = LOG_LEVEL_FILTER();
    let room = LOG_ROOM_FILTER();
    let search = LOG_SEARCH();
    let auto = AUTO_SCROLL();
    let logs = LOGS.read();

    let room_ids: Vec<String> = {
        let mgr = ctx.manager.read().unwrap();
        let mut v: Vec<String> = mgr.rooms.keys().cloned().collect();
        v.sort();
        v
    };

    // 应用过滤；末尾保留最近一段（避免一帧渲染 10k DOM 节点；后续打磨补虚拟滚动）
    let rows: Vec<_> = logs
        .iter()
        .filter(|e| level.is_empty() || e.level.eq_ignore_ascii_case(&level))
        .filter(|e| room.is_empty() || e.room_id == room)
        .filter(|e| search.is_empty() || e.message.contains(&search))
        .rev()
        .take(1500)
        .collect();
    let total_visible = rows.len();

    rsx! {
        div { class: "console",
            div { class: "toolbar",
                label { "等级" }
                select {
                    value: "{level}",
                    onchange: move |e| *LOG_LEVEL_FILTER.write() = e.value(),
                    option { value: "", "全部" }
                    option { value: "DBG", selected: level == "DBG", "DBG" }
                    option { value: "INF", selected: level == "INF", "INF" }
                    option { value: "WRN", selected: level == "WRN", "WRN" }
                    option { value: "ERR", selected: level == "ERR", "ERR" }
                }
                label { "房间" }
                select {
                    value: "{room}",
                    onchange: move |e| *LOG_ROOM_FILTER.write() = e.value(),
                    option { value: "", "全部" }
                    for id in room_ids {
                        option { value: "{id}", selected: room == id, "{id}" }
                    }
                }
                input {
                    r#type: "text",
                    class: "search",
                    value: "{search}",
                    placeholder: "搜索（Ctrl+F）",
                    oninput: move |e| *LOG_SEARCH.write() = e.value(),
                }
                label { class: "checkbox",
                    input {
                        r#type: "checkbox",
                        checked: auto,
                        onchange: move |e| *AUTO_SCROLL.write() = e.value() == "true",
                    }
                    "自动滚动"
                }
                button {
                    class: "btn btn-sm",
                    onclick: move |_| LOGS.write().clear(),
                    title: "Ctrl+K",
                    "清空"
                }
                span { class: "muted small", "{total_visible} / {logs.len()}" }
            }
            div {
                class: if auto { "log-list auto-scroll" } else { "log-list" },
                for entry in rows.iter().rev() {
                    LogRow { ts: entry.timestamp, level: entry.level.clone(),
                        source: entry.source.clone(), room: entry.room_id.clone(),
                        player: entry.player_index, message: entry.message.clone() }
                }
            }
        }
    }
}

#[component]
fn LogRow(ts: f64, level: String, source: String, room: String, player: i32, message: String) -> Element {
    let level_class = match level.as_str() {
        "ERR" => "err", "WRN" => "warn", "INF" => "info", "DBG" => "dbg", _ => "",
    };
    let player_str = if player < 0 { String::new() } else { format!("@{}", player) };
    rsx! {
        div { class: "log-row",
            span { class: "log-ts mono small", {format_ts(ts)} }
            span { class: "log-level {level_class}", "{level}" }
            span { class: "log-room small mono", "{room}{player_str}" }
            span { class: "log-src small muted", "[{source}]" }
            span { class: "log-msg", "{message}" }
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
