use std::path::PathBuf;

use dioxus::prelude::*;

use crate::gui::cmd::{
    apply_completion, compute_completions, parse_event_command, split_command_args,
};
use crate::gui::state::{
    now_secs, CMD_HISTORY, CMD_HISTORY_CURSOR, CMD_INPUT, CMD_MSG, CMD_POPUP_CURSOR, LOGS,
    RECENT_EVENTS, SELECTED_ROOM, SELECTED_TAB, Tab,
};
use crate::gui::AppCtx;
use crate::player::Player;
use crate::storage;

#[component]
pub fn CommandBar() -> Element {
    let ctx = use_context::<AppCtx>();
    let input = CMD_INPUT();
    let msg = CMD_MSG();
    let popup_cursor = CMD_POPUP_CURSOR();

    let candidates = compute_candidates(&ctx, &input);

    let msg_visible = msg
        .as_ref()
        .map(|(_, _, ts)| now_secs() - ts < 6.0)
        .unwrap_or(false);

    let on_input = move |e: Event<FormData>| {
        *CMD_INPUT.write() = e.value();
        *CMD_POPUP_CURSOR.write() = None;
    };

    let on_keydown = {
        let ctx = ctx.clone();
        move |e: Event<KeyboardData>| {
            let key = e.key();
            let shift = e.modifiers().shift();
            match key {
                Key::Enter => {
                    let cmd = CMD_INPUT.peek().clone();
                    if !cmd.is_empty() {
                        execute_command(&ctx, &cmd);
                        push_history(cmd);
                        CMD_INPUT.write().clear();
                        *CMD_POPUP_CURSOR.write() = None;
                        *CMD_HISTORY_CURSOR.write() = None;
                    }
                    e.prevent_default();
                }
                Key::Tab => {
                    e.prevent_default();
                    let cands = compute_candidates(&ctx, &CMD_INPUT.peek().clone());
                    if cands.is_empty() {
                        return;
                    }
                    let len = cands.len() as i32;
                    let cur = *CMD_POPUP_CURSOR.peek();
                    let new_idx = match cur {
                        None => if shift { (len - 1) as usize } else { 0 },
                        Some(i) => {
                            let dir = if shift { -1 } else { 1 };
                            ((i as i32 + dir).rem_euclid(len)) as usize
                        }
                    };
                    let new_input = apply_completion(&CMD_INPUT.peek().clone(), &cands[new_idx]);
                    *CMD_INPUT.write() = new_input;
                    *CMD_POPUP_CURSOR.write() = Some(new_idx);
                }
                Key::ArrowUp => {
                    let history = CMD_HISTORY.peek().clone();
                    let len = history.len();
                    if len > 0 {
                        let new_idx = match *CMD_HISTORY_CURSOR.peek() {
                            None => 0,
                            Some(i) if i + 1 < len => i + 1,
                            Some(i) => i,
                        };
                        *CMD_HISTORY_CURSOR.write() = Some(new_idx);
                        *CMD_INPUT.write() = history[len - 1 - new_idx].clone();
                    }
                    e.prevent_default();
                }
                Key::ArrowDown => {
                    let history = CMD_HISTORY.peek().clone();
                    let len = history.len();
                    if len > 0 {
                        match *CMD_HISTORY_CURSOR.peek() {
                            None => {}
                            Some(0) => {
                                *CMD_HISTORY_CURSOR.write() = None;
                                CMD_INPUT.write().clear();
                            }
                            Some(i) => {
                                let new_idx = i - 1;
                                *CMD_HISTORY_CURSOR.write() = Some(new_idx);
                                *CMD_INPUT.write() = history[len - 1 - new_idx].clone();
                            }
                        }
                    }
                    e.prevent_default();
                }
                Key::Escape => {
                    CMD_INPUT.write().clear();
                    *CMD_POPUP_CURSOR.write() = None;
                    *CMD_HISTORY_CURSOR.write() = None;
                }
                _ => {}
            }
        }
    };

    rsx! {
        div { class: "cmd-bar",
            if !candidates.is_empty() && !input.is_empty() {
                div { class: "cmd-popup",
                    for (i, c) in candidates.iter().take(10).enumerate() {
                        div {
                            class: if Some(i) == popup_cursor { "cmd-cand active" } else { "cmd-cand" },
                            "{c}"
                        }
                    }
                }
            }
            div { class: "cmd-row",
                span { class: "cmd-label", "命令" }
                input {
                    id: "cmd-input",
                    r#type: "text",
                    class: "cmd-input mono",
                    value: "{input}",
                    placeholder: "输入命令（Tab补全 · ↑↓历史 · F1帮助）",
                    oninput: on_input,
                    onkeydown: on_keydown,
                }
                span { class: "cmd-hint small muted", "F1 帮助" }
            }
            if msg_visible {
                if let Some((msg, is_err, _)) = msg.as_ref() {
                    div {
                        class: if *is_err { "cmd-msg flash flash-err" } else { "cmd-msg flash flash-ok" },
                        "{msg}"
                    }
                }
            }
        }
    }
}

fn compute_candidates(ctx: &AppCtx, input: &str) -> Vec<String> {
    let rooms: Vec<String> = {
        let mgr = ctx.manager.read().unwrap();
        let mut v: Vec<String> = mgr.rooms.keys().cloned().collect();
        v.sort();
        v
    };
    let ctx_cloned = ctx.clone();
    let players_fn = move |id: &str| -> Vec<i32> {
        let mgr = ctx_cloned.manager.read().unwrap();
        if let Some(r) = mgr.rooms.get(id) {
            if let Ok(s) = r.shared.try_read() {
                let mut v: Vec<i32> = s.players.keys().copied().collect();
                v.sort();
                return v;
            }
        }
        Vec::new()
    };
    let recent = RECENT_EVENTS.peek().clone();
    compute_completions(input, &rooms, players_fn, &recent)
}

fn push_history(cmd: String) {
    let trimmed = cmd.trim().to_string();
    if trimmed.is_empty() {
        return;
    }
    let mut h = CMD_HISTORY.write();
    if h.back().map(|s| s == &trimmed).unwrap_or(false) {
        // skip dedup
    } else {
        h.push_back(trimmed);
        while h.len() > 100 {
            h.pop_front();
        }
    }
}

fn flash(msg: impl Into<String>, is_err: bool) {
    *CMD_MSG.write() = Some((msg.into(), is_err, now_secs()));
}

fn execute_command(ctx: &AppCtx, cmd: &str) {
    let args = match split_command_args(cmd) {
        Ok(a) => a,
        Err(e) => { flash(e, true); return; }
    };
    if args.is_empty() {
        return;
    }
    let head = args[0].as_str();
    match head {
        "clear" => { LOGS.write().clear(); flash("日志已清空", false); }
        "rooms" => {
            let mgr = ctx.manager.read().unwrap();
            let mut ids: Vec<String> = mgr.rooms.keys().cloned().collect();
            ids.sort();
            flash(format!("房间: {}", ids.join(", ")), false);
        }
        "select" => {
            if args.len() < 2 { flash("用法: select <房间>", true); return; }
            *SELECTED_ROOM.write() = Some(args[1].clone());
            *SELECTED_TAB.write() = Tab::Rooms;
            flash(format!("已选择房间 {}", args[1]), false);
        }
        "stop" => {
            if args.len() < 2 { flash("用法: stop <房间>", true); return; }
            let mgr = ctx.manager.read().unwrap();
            if let Some(r) = mgr.rooms.get(&args[1]) {
                r.stop("UserStop".into());
                flash(format!("已停止 {}", args[1]), false);
            } else { flash("房间不存在", true); }
        }
        "destroy" => {
            if args.len() < 2 { flash("用法: destroy <房间>", true); return; }
            let ok = ctx.manager.write().unwrap().destroy_room(&args[1]);
            if ok { flash(format!("已删除 {}", args[1]), false); }
            else { flash("房间不存在", true); }
        }
        "restart" => {
            // 简化：先 destroy 再 create 同 script_dir
            if args.len() < 2 { flash("用法: restart <房间>", true); return; }
            flash("restart 暂未在新 GUI 实现", true);
        }
        "create" => {
            if args.len() < 2 { flash("用法: create <脚本目录> [模式]", true); return; }
            let script_dir = PathBuf::from(&args[1]);
            if !script_dir.is_dir() { flash("脚本目录不存在", true); return; }
            let mode_id = args.get(2).and_then(|s| s.parse().ok()).unwrap_or(0);
            let archive_dir = ctx.config.read().unwrap().archive_dir.clone();
            let mut players = std::collections::HashMap::new();
            players.insert(0, Player::new(0, "Player_0".into()));
            storage::apply_saved_archives(&archive_dir, &args[1], &mut players);
            let id = ctx.manager.write().unwrap().create_room(script_dir, mode_id, players, archive_dir);
            *SELECTED_ROOM.write() = Some(id.clone());
            flash(format!("已创建 {}", id), false);
        }
        "join" | "leave" | "exit" => {
            if args.len() < 3 { flash(format!("用法: {} <房间> <槽位>", head), true); return; }
            let idx: i32 = match args[2].parse() { Ok(n) => n, Err(_) => { flash("槽位必须是整数", true); return; } };
            let mgr = ctx.manager.read().unwrap();
            let r = match mgr.rooms.get(&args[1]) { Some(r) => r, None => { flash("房间不存在", true); return; } };
            match head {
                "join" => {
                    let name = args.get(3).cloned().unwrap_or_else(|| format!("Player_{}", idx));
                    r.join_player(idx, name, "Connect".into());
                    flash("玩家上线", false);
                }
                "leave" => { r.leave_player(idx, "Disconnect".into()); flash("玩家离线", false); }
                "exit"  => { r.exit_player(idx, "Logout".into()); flash("玩家退出", false); }
                _ => {}
            }
        }
        "event" => {
            let parsed = match parse_event_command(&args) { Ok(p) => p, Err(e) => { flash(e, true); return; } };
            let mgr = ctx.manager.read().unwrap();
            let r = match mgr.rooms.get(&parsed.room_id) { Some(r) => r, None => { flash("房间不存在", true); return; } };
            let _ = r.send_event(parsed.ename.clone(), parsed.evalue, parsed.player_index);
            let mut recent = RECENT_EVENTS.write();
            recent.retain(|e| e != &parsed.ename);
            recent.push_front(parsed.ename.clone());
            while recent.len() > 20 { recent.pop_back(); }
            flash("事件已发送", false);
        }
        other => flash(format!("未知命令: {}", other), true),
    }
}
