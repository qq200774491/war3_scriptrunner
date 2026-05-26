use std::collections::HashMap;

use dioxus::prelude::*;

use crate::gui::state::{
    PROFILER_FRAME_MS, PROFILER_HOOK_COUNT, PROFILER_ROOM, PROFILER_WINDOW, PROFILE_SNAPSHOT,
};
use crate::gui::AppCtx;
use crate::room::{ProfileData, ProfileNode};

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

    let _ = use_resource(|| async {
        let _ = dioxus::document::eval(include_str!("../assets/flame.js")).await;
    });

    use_effect(move || {
        let s = PROFILE_SNAPSHOT.read();
        let json = match s.as_ref() {
            Some(snap) => serde_json::to_string(snap).unwrap_or_default(),
            None => "null".to_string(),
        };
        let _ = dioxus::document::eval(&format!(
            "window.renderFlame && window.renderFlame({});",
            json
        ));
    });

    let hot: Vec<HotFunc> = snap
        .as_ref()
        .and_then(|s| s.data.as_ref())
        .map(|d| collect_hot(d))
        .unwrap_or_default();
    let total_samples = snap
        .as_ref()
        .and_then(|s| s.data.as_ref())
        .map(|d| d.total_samples.max(1) as f64)
        .unwrap_or(1.0);
    let running = snap.as_ref().map(|s| s.running).unwrap_or(false);
    let available = snap.as_ref().map(|s| s.available).unwrap_or(false);

    let start_or_stop = {
        let ctx = ctx.clone();
        move |_: Event<MouseData>| {
            let rid = match PROFILER_ROOM() {
                Some(r) => r,
                None => return,
            };
            let mgr = ctx.manager.read().unwrap();
            if let Some(room) = mgr.rooms.get(&rid) {
                if running {
                    room.profiler_stop();
                } else {
                    room.profiler_start(PROFILER_HOOK_COUNT(), PROFILER_WINDOW());
                }
            }
        }
    };
    let reset_profiler = {
        let ctx = ctx.clone();
        move |_: Event<MouseData>| {
            let rid = match PROFILER_ROOM() {
                Some(r) => r,
                None => return,
            };
            let mgr = ctx.manager.read().unwrap();
            if let Some(room) = mgr.rooms.get(&rid) {
                room.profiler_reset();
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
                label {
                    title: "Lua 调试 hook 每执行多少条虚拟机指令触发一次采样。值越小越精准但开销大；100 ~ 500000",
                    "采样间隔"
                }
                input {
                    r#type: "number",
                    value: "{hook}",
                    title: "每 N 条 Lua 指令采样一次",
                    oninput: move |e| *PROFILER_HOOK_COUNT.write() = e.value().parse().unwrap_or(5000),
                    style: "max-width: 90px;",
                }
                label {
                    title: "保留最近多少秒的采样数据用于聚合（窗口长度）",
                    "窗口长度"
                }
                input {
                    r#type: "number",
                    value: "{window_sec}",
                    title: "秒",
                    oninput: move |e| *PROFILER_WINDOW.write() = e.value().parse().unwrap_or(15),
                    style: "max-width: 70px;",
                }
                label {
                    title: "用于估算每个函数占满帧的耗时百分比（毫秒）。50 = 20fps、16.67 = 60fps",
                    "目标帧时"
                }
                input {
                    r#type: "number",
                    value: "{frame_ms}",
                    title: "毫秒，仅用于把采样占比换算成 ms/帧",
                    oninput: move |e| *PROFILER_FRAME_MS.write() = e.value().parse().unwrap_or(50.0),
                    style: "max-width: 70px;",
                }
                span {
                    class: if running { "ok small" } else { "muted small" },
                    if running { "● 采样中" } else { "○ 未运行" }
                }
                if !available && snap.is_some() {
                    span { class: "warn small", "（采样不可用：脚本未启动？）" }
                }
                button {
                    class: if running { "btn btn-sm btn-danger" } else { "btn btn-sm btn-primary" },
                    onclick: start_or_stop,
                    if running { "停止采样" } else { "开始采样" }
                }
                button { class: "btn btn-sm", onclick: reset_profiler, "重置" }
            }
            div { class: "flame-wrap",
                canvas { id: "flame", style: "width: 100%; height: 320px; display: block; background: #1a1a1a;" }
                div { id: "flame-tip", class: "flame-tip" }
            }
            div { class: "hot-section",
                div { class: "hot-head",
                    h3 { "热点函数" }
                    span { class: "muted small", "({hot.len()} 个 · 共 {total_samples as u64} 采样)" }
                }
                div { class: "table hot-table",
                    div { class: "thead",
                        div { "函数" }
                        div { "自身%" }
                        div { "自身 ms/帧" }
                        div { "总计%" }
                        div { "采样 自/总" }
                    }
                    div { class: "tbody",
                        if hot.is_empty() {
                            div { class: "empty", "（暂无数据 · 点「开始采样」后稍候）" }
                        }
                        for h in hot.iter().take(30) {
                            HotRow {
                                name: h.name.clone(),
                                id: h.id.clone(),
                                self_count: h.self_count,
                                total_count: h.total_count,
                                total_samples: total_samples,
                                frame_ms: frame_ms as f64,
                            }
                        }
                    }
                }
            }
        }
    }
}

#[derive(Clone)]
struct HotFunc {
    name: String,
    id: String,
    self_count: u64,
    total_count: u64,
}

fn collect_hot(data: &ProfileData) -> Vec<HotFunc> {
    let mut map: HashMap<String, HotFunc> = HashMap::new();
    walk(&data.root, &mut map);
    let mut v: Vec<HotFunc> = map.into_values().collect();
    v.sort_by(|a, b| b.self_count.cmp(&a.self_count));
    v
}

fn walk(node: &ProfileNode, map: &mut HashMap<String, HotFunc>) {
    if node.id != "(root)" {
        let e = map.entry(node.id.clone()).or_insert_with(|| HotFunc {
            name: node.name.clone(),
            id: node.id.clone(),
            self_count: 0,
            total_count: 0,
        });
        e.self_count += node.self_count;
        e.total_count += node.count;
    }
    for c in &node.children {
        walk(c, map);
    }
}

#[component]
fn HotRow(
    name: String,
    id: String,
    self_count: u64,
    total_count: u64,
    total_samples: f64,
    frame_ms: f64,
) -> Element {
    let self_pct = self_count as f64 / total_samples * 100.0;
    let total_pct = total_count as f64 / total_samples * 100.0;
    let self_ms_per_frame = self_pct / 100.0 * frame_ms;
    let is_c = id.starts_with("[C]:");
    let name_class = if is_c { "mono small accent" } else { "mono small" };

    rsx! {
        div { class: "tr", title: "{id}",
            div { class: "td {name_class}", "{name}" }
            div { class: "td mono small", "{self_pct:.1}%" }
            div { class: "td mono small", "{self_ms_per_frame:.2}" }
            div { class: "td mono small muted", "{total_pct:.1}%" }
            div { class: "td mono small muted", "{self_count} / {total_count}" }
        }
    }
}
