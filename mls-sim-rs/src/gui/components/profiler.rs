use dioxus::prelude::*;

use crate::gui::state::{
    PROFILER_FRAME_MS, PROFILER_HOOK_COUNT, PROFILER_ROOM, PROFILER_WINDOW, PROFILE_SNAPSHOT,
};
use crate::gui::AppCtx;

#[component]
pub fn Profiler() -> Element {
    let ctx = use_context::<AppCtx>();
    let snap = PROFILE_SNAPSHOT();
    let selected = PROFILER_ROOM().clone();
    let hook = PROFILER_HOOK_COUNT();
    let window_sec = PROFILER_WINDOW();
    let frame_ms = PROFILER_FRAME_MS();

    let room_ids: Vec<String> = {
        let mgr = ctx.manager.read().unwrap();
        let mut v: Vec<String> = mgr.rooms.keys().cloned().collect();
        v.sort();
        v
    };

    // 注入 flame.js 一次（脚本自我守门，重复注入无效果）
    let _ = use_resource(|| async {
        let _ = dioxus::document::eval(include_str!("../assets/flame.js")).await;
    });

    // 监听 snapshot，每次变化推到 JS canvas
    use_effect(move || {
        let s = PROFILE_SNAPSHOT.read();
        let json = match s.as_ref() {
            Some(snap) => serde_json::to_string(snap).unwrap_or_default(),
            None => "null".to_string(),
        };
        let _ = dioxus::document::eval(&format!("window.renderFlame && window.renderFlame({});", json));
    });

    // Top 30 hot functions（直接从 snap.data.root 递归收集）
    let hot: Vec<HotItem> = snap
        .as_ref()
        .and_then(|s| s.data.as_ref())
        .map(top_hot)
        .unwrap_or_default();

    let toggle_profiler = {
        let ctx = ctx.clone();
        move |_: Event<MouseData>| {
            let rid = match PROFILER_ROOM.peek().clone() {
                Some(r) => r,
                None => return,
            };
            let mgr = ctx.manager.read().unwrap();
            if let Some(_room) = mgr.rooms.get(&rid) {
                // 实际启停由 sim 引擎控制；这里仅作触发占位（gui_legacy 也并未直接 toggle）
                // 后续若有 Room::set_profiler_running 等 API，可在此调用。
                tracing::info!("profiler toggle request for {}", rid);
            }
        }
    };

    rsx! {
        div { class: "profiler",
            div { class: "toolbar wrap",
                label { "房间" }
                select {
                    value: selected.clone().unwrap_or_default(),
                    onchange: move |e| {
                        let v = e.value();
                        *PROFILER_ROOM.write() = if v.is_empty() { None } else { Some(v) };
                    },
                    option { value: "", "（请选择）" }
                    for id in room_ids {
                        option { value: "{id}", selected: selected.as_deref() == Some(id.as_str()), "{id}" }
                    }
                }
                label { "Hook 采样" }
                input {
                    r#type: "number",
                    value: "{hook}",
                    oninput: move |e| *PROFILER_HOOK_COUNT.write() = e.value().parse().unwrap_or(5000),
                    style: "max-width: 90px;",
                }
                label { "时间窗(s)" }
                input {
                    r#type: "number",
                    value: "{window_sec}",
                    oninput: move |e| *PROFILER_WINDOW.write() = e.value().parse().unwrap_or(15),
                    style: "max-width: 60px;",
                }
                label { "帧时(ms)" }
                input {
                    r#type: "number",
                    value: "{frame_ms}",
                    oninput: move |e| *PROFILER_FRAME_MS.write() = e.value().parse().unwrap_or(50.0),
                    style: "max-width: 60px;",
                }
                if let Some(s) = snap.as_ref() {
                    span { class: "muted small",
                        if s.running { "● 采样中" } else { "○ 未运行" }
                    }
                }
                button { class: "btn btn-sm", onclick: toggle_profiler, "重置" }
            }
            div { class: "flame-wrap",
                canvas { id: "flame", style: "width: 100%; height: 320px; display: block; background: #1a1a1a;" }
                div { id: "flame-tip", class: "flame-tip" }
            }
            div { class: "hot-section",
                h3 { "Top 30 热点函数" }
                div { class: "table hot-table",
                    div { class: "thead", div { "函数" } div { "总耗时(ms)" } div { "调用" } }
                    div { class: "tbody",
                        if hot.is_empty() {
                            div { class: "tr empty", div { class: "td", "（无数据）" } }
                        }
                        for h in hot.iter() {
                            div { class: "tr",
                                div { class: "td mono small", "{h.name}" }
                                div { class: "td mono small", "{(h.total_ms):.2}" }
                                div { class: "td mono small", "{h.calls}" }
                            }
                        }
                    }
                }
            }
        }
    }
}

#[derive(Clone)]
struct HotItem {
    name: String,
    total_ms: f64,
    calls: u64,
}

fn top_hot(data: &crate::room::ProfileData) -> Vec<HotItem> {
    use std::collections::HashMap;
    let mut by_name: HashMap<String, (f64, u64)> = HashMap::new();
    if let Ok(v) = serde_json::to_value(data) {
        collect_nodes(&v, &mut by_name);
    }
    let mut items: Vec<HotItem> = by_name
        .into_iter()
        .map(|(name, (dur, calls))| HotItem {
            name,
            total_ms: dur * 1000.0,
            calls,
        })
        .collect();
    items.sort_by(|a, b| b.total_ms.partial_cmp(&a.total_ms).unwrap_or(std::cmp::Ordering::Equal));
    items.truncate(30);
    items
}

fn collect_nodes(v: &serde_json::Value, acc: &mut std::collections::HashMap<String, (f64, u64)>) {
    if let Some(obj) = v.as_object() {
        let name = obj
            .get("name")
            .and_then(|x| x.as_str())
            .or_else(|| obj.get("id").and_then(|x| x.as_str()))
            .unwrap_or("?")
            .to_string();
        let dur = obj
            .get("duration")
            .and_then(|x| x.as_f64())
            .or_else(|| obj.get("dur").and_then(|x| x.as_f64()))
            .unwrap_or(0.0);
        let calls = obj
            .get("calls")
            .and_then(|x| x.as_u64())
            .or_else(|| obj.get("count").and_then(|x| x.as_u64()))
            .unwrap_or(1);
        let e = acc.entry(name).or_insert((0.0, 0));
        e.0 += dur;
        e.1 += calls;
        if let Some(children) = obj.get("children").and_then(|c| c.as_array()) {
            for c in children {
                collect_nodes(c, acc);
            }
        }
    } else if let Some(arr) = v.as_array() {
        for c in arr {
            collect_nodes(c, acc);
        }
    }
}
