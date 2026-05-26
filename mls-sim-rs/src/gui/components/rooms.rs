use std::collections::HashMap;
use std::path::PathBuf;

use dioxus::prelude::*;

use crate::gui::state::{
    now_secs, ADD_PLAYER_INDEX, ADD_PLAYER_NAME, DELETE_CONFIRM, EVENT_DATA, EVENT_NAME,
    EVENT_PLAYER_IDX, NEW_ROOM_MODE_ID, NEW_ROOM_SCRIPT_DIR, RECENT_EVENTS, SELECTED_ROOM,
    SHOW_CREATE_ROOM,
};
use crate::gui::AppCtx;
use crate::player::Player;
use crate::room::RoomStatus;
use crate::storage;

struct RoomRow {
    id: String,
    status: RoomStatus,
    script_dir: String,
    mode_id: i32,
    game_time: i64,
    error: String,
    players: Vec<PlayerRow>,
}

#[derive(Clone)]
struct PlayerRow {
    index: i32,
    name: String,
    map_level: i32,
    is_connected: bool,
}

fn snapshot_rooms(ctx: &AppCtx) -> Vec<RoomRow> {
    let mgr = ctx.manager.read().unwrap();
    let mut rows: Vec<RoomRow> = mgr
        .rooms
        .iter()
        .map(|(id, room)| {
            let shared = room.shared.read().unwrap();
            let game_time = if shared.loaded_ts > 0 {
                (now_secs() as i64) - shared.loaded_ts
            } else {
                0
            };
            let mut players: Vec<PlayerRow> = shared
                .players
                .iter()
                .map(|(idx, p)| PlayerRow {
                    index: *idx,
                    name: p.name.clone(),
                    map_level: p.map_level,
                    is_connected: p.is_connected,
                })
                .collect();
            players.sort_by_key(|p| p.index);
            RoomRow {
                id: id.clone(),
                status: shared.status.clone(),
                script_dir: shared.script_dir.to_string_lossy().to_string(),
                mode_id: shared.mode_id,
                game_time,
                error: shared.error_message.clone(),
                players,
            }
        })
        .collect();
    rows.sort_by(|a, b| a.id.cmp(&b.id));
    rows
}

fn status_class(s: &RoomStatus) -> &'static str {
    match s {
        RoomStatus::Running => "ok",
        RoomStatus::Stopped => "mut",
        RoomStatus::Error => "err",
        RoomStatus::Created => "info",
    }
}

fn status_label(s: &RoomStatus) -> &'static str {
    match s {
        RoomStatus::Running => "运行中",
        RoomStatus::Stopped => "已停止",
        RoomStatus::Error => "错误",
        RoomStatus::Created => "已创建",
    }
}

fn format_duration(s: i64) -> String {
    format!("{:02}:{:02}:{:02}", s / 3600, (s % 3600) / 60, s % 60)
}

#[component]
pub fn Rooms() -> Element {
    let ctx = use_context::<AppCtx>();
    let rooms = snapshot_rooms(&ctx);
    let selected = SELECTED_ROOM().clone();
    let show_create = SHOW_CREATE_ROOM();
    let event_idx = EVENT_PLAYER_IDX();

    let selected_room = selected
        .as_ref()
        .and_then(|id| rooms.iter().find(|r| &r.id == id));

    rsx! {
        div { class: "rooms-split",
            // ─── Sidebar ─────────────────────────────
            aside { class: "rooms-side",
                div { class: "rooms-side-head",
                    h3 { "房间列表" }
                    span { class: "muted small", "{rooms.len()}" }
                }
                button {
                    class: "btn btn-block",
                    onclick: move |_| *SHOW_CREATE_ROOM.write() = !show_create,
                    if show_create { "收起" } else { "新建房间 (N)" }
                }
                if show_create {
                    CreateRoomForm {}
                }
                div { class: "rooms-list",
                    if rooms.is_empty() {
                        div { class: "empty", "暂无房间 · 点 \"新建房间\" 或按 N" }
                    }
                    for room in rooms.iter() {
                        {
                            let id = room.id.clone();
                            let is_selected = selected.as_deref() == Some(id.as_str());
                            let title = room.script_dir.clone();
                            let label = status_label(&room.status);
                            let sclass = status_class(&room.status);
                            let pcount = room.players.len();
                            rsx! {
                                div {
                                    class: if is_selected { "room-item selected" } else { "room-item" },
                                    onclick: move |_| *SELECTED_ROOM.write() = Some(id.clone()),
                                    title: "{title}",
                                    div { class: "room-title",
                                        span { class: "status-dot {sclass}" }
                                        span { class: "room-id", "{room.id}" }
                                    }
                                    div { class: "room-sub muted small",
                                        "{label} · {pcount} 人"
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ─── Detail ──────────────────────────────
            section { class: "rooms-detail",
                match selected_room {
                    None => rsx! { div { class: "empty-big",
                        div { class: "empty-icon", "○" }
                        div { class: "empty-text", "请选择或创建一个房间" }
                        div { class: "muted small", "↑↓ 切换 · N 新建 · / 命令" }
                    }},
                    Some(room) => {
                        let id = room.id.clone();
                        let stop_id = id.clone();
                        let delete_id = id.clone();
                        let send_id = id.clone();
                        let add_id = id.clone();
                        let stop_ctx = ctx.clone();
                        let send_ctx = ctx.clone();
                        let add_ctx = ctx.clone();
                        let players = room.players.clone();
                        rsx! {
                            div { class: "room-detail-scroll",
                                section { class: "card",
                                    div { class: "room-head",
                                        h2 { "{room.id}" }
                                        span { class: "status-badge {status_class(&room.status)}", "{status_label(&room.status)}" }
                                    }
                                    div { class: "info-grid",
                                        div { class: "info-k", "脚本" }
                                        div { class: "info-v mono small", "{room.script_dir}" }
                                        div { class: "info-k", "模式" }
                                        div { class: "info-v", "{room.mode_id}" }
                                        div { class: "info-k", "运行时间" }
                                        div { class: "info-v mono", {format_duration(room.game_time)} }
                                    }
                                    if !room.error.is_empty() {
                                        p { class: "flash flash-err", "错误: {room.error}" }
                                    }
                                    div { class: "actions",
                                        button {
                                            class: "btn",
                                            onclick: move |_| {
                                                let mgr = stop_ctx.manager.read().unwrap();
                                                if let Some(r) = mgr.rooms.get(&stop_id) {
                                                    r.stop("UserStop".into());
                                                }
                                            },
                                            "停止"
                                        }
                                        button {
                                            class: "btn btn-danger",
                                            onclick: move |_| *DELETE_CONFIRM.write() = Some(delete_id.clone()),
                                            "删除"
                                        }
                                    }
                                }

                                section { class: "card",
                                    h3 { "玩家列表 · {players.len()} 人" }
                                    div { class: "table player-table",
                                        div { class: "thead",
                                            div { "槽位" } div { "名称" } div { "等级" } div { "状态" } div { "操作" }
                                        }
                                        div { class: "tbody",
                                            for p in players.iter() {
                                                PlayerRowView {
                                                    room: id.clone(),
                                                    idx: p.index,
                                                    name: p.name.clone(),
                                                    level: p.map_level,
                                                    online: p.is_connected,
                                                }
                                            }
                                        }
                                    }
                                    div { class: "add-player-row",
                                        label { "槽位" }
                                        input {
                                            r#type: "text",
                                            value: "{ADD_PLAYER_INDEX}",
                                            oninput: move |e| *ADD_PLAYER_INDEX.write() = e.value(),
                                            style: "max-width: 60px;",
                                        }
                                        label { "名称" }
                                        input {
                                            r#type: "text",
                                            value: "{ADD_PLAYER_NAME}",
                                            oninput: move |e| *ADD_PLAYER_NAME.write() = e.value(),
                                            placeholder: "默认 Player_<槽位>",
                                        }
                                        button {
                                            class: "btn",
                                            onclick: move |_| {
                                                let idx_str = ADD_PLAYER_INDEX();
                                                if let Ok(idx) = idx_str.parse::<i32>() {
                                                    let name = ADD_PLAYER_NAME();
                                                    let name = if name.is_empty() { format!("Player_{}", idx) } else { name };
                                                    let mgr = add_ctx.manager.read().unwrap();
                                                    if let Some(r) = mgr.rooms.get(&add_id) {
                                                        r.join_player(idx, name, "Connect".into());
                                                    }
                                                    ADD_PLAYER_NAME.write().clear();
                                                }
                                            },
                                            "添加"
                                        }
                                    }
                                }

                                section { class: "card",
                                    h3 { "发送事件" }
                                    div { class: "evt-form",
                                        label { "事件名" }
                                        input {
                                            r#type: "text",
                                            value: "{EVENT_NAME}",
                                            oninput: move |e| *EVENT_NAME.write() = e.value(),
                                        }
                                        label { "玩家" }
                                        select {
                                            value: event_idx.to_string(),
                                            onchange: move |e| {
                                                *EVENT_PLAYER_IDX.write() = e.value().parse().unwrap_or(-1);
                                            },
                                            option { value: "-1", selected: event_idx == -1, "全部" }
                                            for p in players.iter() {
                                                option {
                                                    value: "{p.index}",
                                                    selected: event_idx == p.index,
                                                    "{p.index} ({p.name})"
                                                }
                                            }
                                        }
                                        label { "数据" }
                                        input {
                                            r#type: "text",
                                            value: "{EVENT_DATA}",
                                            oninput: move |e| *EVENT_DATA.write() = e.value(),
                                            placeholder: "（可选）",
                                        }
                                        button {
                                            class: "btn btn-primary",
                                            onclick: move |_| {
                                                let name = EVENT_NAME();
                                                if name.is_empty() { return; }
                                                let data = EVENT_DATA();
                                                let pidx = EVENT_PLAYER_IDX();
                                                {
                                                    let mgr = send_ctx.manager.read().unwrap();
                                                    if let Some(r) = mgr.rooms.get(&send_id) {
                                                        let _ = r.send_event(name.clone(), data, pidx);
                                                    }
                                                }
                                                let mut recent = RECENT_EVENTS.write();
                                                recent.retain(|e| e != &name);
                                                recent.push_front(name);
                                                while recent.len() > 20 { recent.pop_back(); }
                                                EVENT_NAME.write().clear();
                                                EVENT_DATA.write().clear();
                                            },
                                            "发送"
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

#[component]
fn PlayerRowView(room: String, idx: i32, name: String, level: i32, online: bool) -> Element {
    let ctx = use_context::<AppCtx>();
    let toggle_room = room.clone();
    let toggle_name = name.clone();
    let toggle_ctx = ctx.clone();
    let remove_room = room.clone();
    let remove_ctx = ctx.clone();
    rsx! {
        div { class: "tr",
            div { class: "td", "{idx}" }
            div { class: "td", "{name}" }
            div { class: "td", "{level}" }
            div { class: "td",
                if online { span { class: "ok", "在线" } } else { span { class: "mut", "离线" } }
            }
            div { class: "td actions",
                button {
                    class: "btn btn-sm",
                    onclick: move |_| {
                        let mgr = toggle_ctx.manager.read().unwrap();
                        if let Some(r) = mgr.rooms.get(&toggle_room) {
                            if online { r.leave_player(idx, "Disconnect".into()); }
                            else { r.join_player(idx, toggle_name.clone(), "Connect".into()); }
                        }
                    },
                    if online { "离线" } else { "上线" }
                }
                button {
                    class: "btn btn-sm btn-danger",
                    onclick: move |_| {
                        let mgr = remove_ctx.manager.read().unwrap();
                        if let Some(r) = mgr.rooms.get(&remove_room) {
                            r.exit_player(idx, "Logout".into());
                        }
                    },
                    "移除"
                }
            }
        }
    }
}

#[component]
fn CreateRoomForm() -> Element {
    let ctx = use_context::<AppCtx>();
    let create = move |_| {
        let script_dir_str = NEW_ROOM_SCRIPT_DIR();
        if script_dir_str.is_empty() { return; }
        let script_dir = PathBuf::from(&script_dir_str);
        if !script_dir.is_dir() { return; }
        let mode_id = NEW_ROOM_MODE_ID();
        let archive_dir = ctx.config.read().unwrap().archive_dir.clone();
        let mut players = HashMap::new();
        players.insert(0, Player::new(0, "Player_0".into()));
        storage::apply_saved_archives(&archive_dir, &script_dir_str, &mut players);
        let room_id = ctx.manager.write().unwrap().create_room(script_dir, mode_id, players, archive_dir);
        *SELECTED_ROOM.write() = Some(room_id);
        *SHOW_CREATE_ROOM.write() = false;
        NEW_ROOM_SCRIPT_DIR.write().clear();
    };
    rsx! {
        div { class: "create-form",
            label { "脚本目录" }
            input {
                r#type: "text",
                value: "{NEW_ROOM_SCRIPT_DIR}",
                oninput: move |e| *NEW_ROOM_SCRIPT_DIR.write() = e.value(),
            }
            div { class: "row",
                label { "模式 ID" }
                input {
                    r#type: "number",
                    value: "{NEW_ROOM_MODE_ID}",
                    oninput: move |e| {
                        *NEW_ROOM_MODE_ID.write() = e.value().parse().unwrap_or(0);
                    },
                    style: "max-width: 80px;",
                }
            }
            div { class: "actions",
                button { class: "btn btn-primary", onclick: create, "创建" }
                button {
                    class: "btn",
                    onclick: move |_| *SHOW_CREATE_ROOM.write() = false,
                    "取消"
                }
            }
        }
    }
}
