mod console;
mod events;
mod palette;
mod parse;
mod profiler;
mod rooms;
mod settings;
mod state_view;

use std::collections::{HashMap, HashSet, VecDeque};
use std::path::PathBuf;
use std::sync::{Arc, RwLock};
use tokio::sync::broadcast;

use eframe::egui;

use crate::config::AppConfig;
use crate::player::Player;
use crate::room::{LogEntry, OutEvent, RoomManager};
use crate::storage;

use parse::{
    apply_completion, compute_completions, matching_command_hints, parse_event_command,
    push_recent_event, split_command_args, tokens_for_completion, CommandHistory,
};

#[derive(PartialEq, Clone, Copy)]
pub enum Tab {
    Rooms,
    Console,
    Events,
    State,
    Profiler,
    Settings,
}

const TAB_COUNT: usize = 6;

impl Tab {
    fn from_idx(i: usize) -> Option<Tab> {
        Some(match i {
            0 => Tab::Rooms,
            1 => Tab::Console,
            2 => Tab::Events,
            3 => Tab::State,
            4 => Tab::Profiler,
            5 => Tab::Settings,
            _ => return None,
        })
    }
    fn to_idx(self) -> usize {
        match self {
            Tab::Rooms => 0,
            Tab::Console => 1,
            Tab::Events => 2,
            Tab::State => 3,
            Tab::Profiler => 4,
            Tab::Settings => 5,
        }
    }
}

// Single source of truth for the F1 cheatsheet. Add new bindings here.
struct Keybinding {
    scope: &'static str,
    keys: &'static str,
    desc: &'static str,
}

const KEYBINDINGS: &[Keybinding] = &[
    Keybinding { scope: "全局",   keys: "Ctrl+1 .. Ctrl+6",          desc: "切到对应标签页" },
    Keybinding { scope: "全局",   keys: "Ctrl+Tab / Ctrl+Shift+Tab", desc: "循环切换标签页" },
    Keybinding { scope: "全局",   keys: "/  或  Ctrl+L",             desc: "聚焦命令栏" },
    Keybinding { scope: "全局",   keys: "F1",                        desc: "显示/隐藏本帮助" },
    Keybinding { scope: "全局",   keys: "Esc",                       desc: "关闭弹窗 / 清空输入 / 失焦" },
    Keybinding { scope: "命令栏", keys: "↑ / ↓",                     desc: "回溯/前进命令历史" },
    Keybinding { scope: "命令栏", keys: "Tab / Shift+Tab",           desc: "循环候选补全（命令名 / 房间ID / 玩家 / 事件名）" },
    Keybinding { scope: "命令栏", keys: "Enter",                     desc: "执行命令" },
    Keybinding { scope: "房间",   keys: "↑ / ↓",                     desc: "在房间列表中切换选中" },
    Keybinding { scope: "房间",   keys: "N",                         desc: "切换\"新建房间\"表单" },
    Keybinding { scope: "房间",   keys: "Delete",                    desc: "删除当前选中房间（需二次确认）" },
    Keybinding { scope: "房间",   keys: "Enter（表单聚焦）",         desc: "提交对应表单（添加玩家 / 发送事件）" },
    Keybinding { scope: "控制台", keys: "Ctrl+F",                    desc: "聚焦搜索框" },
    Keybinding { scope: "控制台", keys: "Ctrl+K",                    desc: "清空日志" },
    Keybinding { scope: "控制台", keys: "Esc（搜索框聚焦）",         desc: "清空搜索内容 → 失焦" },
];

pub struct GuiApp {
    pub manager: Arc<RwLock<RoomManager>>,
    pub config: Arc<RwLock<AppConfig>>,
    pub config_path: String,
    pub active_tab: Tab,

    // Rooms tab
    pub selected_room_id: Option<String>,
    pub show_create_room: bool,
    pub new_room_script_dir: String,
    pub new_room_mode_id: i32,
    pub add_player_index: String,
    pub add_player_name: String,
    pub event_name: String,
    pub event_data: String,
    pub event_player_idx: i32,

    // Console
    pub logs: Vec<LogEntry>,
    pub out_events: Vec<OutEvent>,
    subscribed_rooms: HashSet<String>,
    log_receivers: HashMap<String, broadcast::Receiver<LogEntry>>,
    event_receivers: HashMap<String, broadcast::Receiver<OutEvent>>,
    pub log_level_filter: String,
    pub log_search: String,
    pub log_room_filter: String,
    pub auto_scroll: bool,

    // State viewer
    pub state_room_id: Option<String>,
    pub state_json_text: String,

    // Settings
    pub settings_host: String,
    pub settings_port: String,
    pub settings_archive_dir: String,

    // Profiler
    pub profiler_room_id: Option<String>,
    pub profiler_hook_count: i32,
    pub profiler_window: i32,
    pub profiler_frame_ms: f32,
    pub profiler_hover_info: String,

    // Status / command bar
    pub save_msg: Option<(String, bool, f64)>,
    pub command_input: String,
    pub command_msg: Option<(String, bool, f64)>,

    // Keyboard / UX state
    pub(crate) history: CommandHistory,
    pub(crate) command_input_prev: String,
    pub(crate) popup_cursor: Option<usize>,
    pub(crate) recent_events: VecDeque<String>,
    pub(crate) delete_confirm: Option<String>,
    pub(crate) show_keybindings: bool,
    pub(crate) focus_command: bool,
    pub(crate) focus_search: bool,
    pub(crate) scroll_to_selected_room: bool,
}

impl GuiApp {
    pub fn new(
        cc: &eframe::CreationContext<'_>,
        manager: Arc<RwLock<RoomManager>>,
        config: Arc<RwLock<AppConfig>>,
        config_path: String,
    ) -> Self {
        setup_fonts(&cc.egui_ctx);
        setup_style(&cc.egui_ctx);

        let (host, port, archive_dir) = {
            let cfg = config.read().unwrap();
            (
                cfg.host.clone(),
                cfg.port.to_string(),
                cfg.archive_dir.clone(),
            )
        };

        Self {
            manager,
            config,
            config_path,
            active_tab: Tab::Rooms,
            selected_room_id: None,
            show_create_room: false,
            new_room_script_dir: String::new(),
            new_room_mode_id: 0,
            add_player_index: "0".into(),
            add_player_name: String::new(),
            event_name: String::new(),
            event_data: String::new(),
            event_player_idx: -1,
            logs: Vec::new(),
            out_events: Vec::new(),
            subscribed_rooms: HashSet::new(),
            log_receivers: HashMap::new(),
            event_receivers: HashMap::new(),
            log_level_filter: String::new(),
            log_search: String::new(),
            log_room_filter: String::new(),
            auto_scroll: true,
            state_room_id: None,
            state_json_text: String::new(),
            profiler_room_id: None,
            profiler_hook_count: 5000,
            profiler_window: 15,
            profiler_frame_ms: 50.0,
            profiler_hover_info: String::new(),
            settings_host: host,
            settings_port: port,
            settings_archive_dir: archive_dir,
            save_msg: None,
            command_input: String::new(),
            command_msg: None,
            history: CommandHistory::new(),
            command_input_prev: String::new(),
            popup_cursor: None,
            recent_events: VecDeque::new(),
            delete_confirm: None,
            show_keybindings: false,
            focus_command: false,
            focus_search: false,
            scroll_to_selected_room: false,
        }
    }

    fn sync_subscriptions(&mut self) {
        let manager = match self.manager.try_read() {
            Ok(m) => m,
            Err(_) => return,
        };
        let current: HashSet<String> = manager.rooms.keys().cloned().collect();

        for id in &current {
            if !self.subscribed_rooms.contains(id) {
                if let Some(room) = manager.rooms.get(id) {
                    if let Ok(shared) = room.shared.try_read() {
                        self.logs.extend(shared.log_buffer.iter().cloned());
                        self.out_events.extend(shared.event_buffer.iter().cloned());
                    }
                    self.log_receivers
                        .insert(id.clone(), room.log_tx.subscribe());
                    self.event_receivers
                        .insert(id.clone(), room.out_event_tx.subscribe());
                }
            }
        }

        let removed: Vec<String> = self
            .subscribed_rooms
            .difference(&current)
            .cloned()
            .collect();
        for id in &removed {
            self.log_receivers.remove(id);
            self.event_receivers.remove(id);
        }

        self.subscribed_rooms = current;
    }

    fn drain_channels(&mut self) {
        for rx in self.log_receivers.values_mut() {
            loop {
                match rx.try_recv() {
                    Ok(entry) => self.logs.push(entry),
                    Err(broadcast::error::TryRecvError::Lagged(_)) => continue,
                    _ => break,
                }
            }
        }
        for rx in self.event_receivers.values_mut() {
            loop {
                match rx.try_recv() {
                    Ok(event) => self.out_events.push(event),
                    Err(broadcast::error::TryRecvError::Lagged(_)) => continue,
                    _ => break,
                }
            }
        }

        const MAX_LOGS: usize = 10000;
        if self.logs.len() > MAX_LOGS {
            self.logs.drain(0..self.logs.len() - MAX_LOGS);
        }
        if self.out_events.len() > MAX_LOGS {
            self.out_events.drain(0..self.out_events.len() - MAX_LOGS);
        }
    }

    // ─────────── pub(crate) helpers used by sibling tab modules ───────────

    /// Record an ename so it appears in `event` command completion next time.
    pub(crate) fn record_recent_event(&mut self, ename: &str) {
        push_recent_event(&mut self.recent_events, ename);
    }

    /// Stage a room for deletion; the modal will pick it up on the next frame.
    pub(crate) fn request_delete_room(&mut self, id: String) {
        self.delete_confirm = Some(id);
    }

    /// Wrap-around navigation through the sorted room list.
    pub(crate) fn navigate_rooms(&mut self, direction: i32) {
        let rooms = self.sorted_room_ids();
        if rooms.is_empty() {
            return;
        }
        let cur_idx = self
            .selected_room_id
            .as_ref()
            .and_then(|id| rooms.iter().position(|r| r == id));
        let new_idx = match cur_idx {
            None => {
                if direction > 0 {
                    0
                } else {
                    rooms.len() - 1
                }
            }
            Some(i) => {
                let len = rooms.len() as i32;
                ((i as i32 + direction).rem_euclid(len)) as usize
            }
        };
        self.selected_room_id = Some(rooms[new_idx].clone());
        self.scroll_to_selected_room = true;
    }

    pub(crate) fn sorted_room_ids(&self) -> Vec<String> {
        let mgr = match self.manager.try_read() {
            Ok(m) => m,
            Err(_) => return Vec::new(),
        };
        let mut v: Vec<String> = mgr.rooms.keys().cloned().collect();
        v.sort();
        v
    }

    pub(crate) fn consume_scroll_to_selected_room(&mut self) -> bool {
        let v = self.scroll_to_selected_room;
        self.scroll_to_selected_room = false;
        v
    }

    pub(crate) fn consume_focus_search(&mut self) -> bool {
        let v = self.focus_search;
        self.focus_search = false;
        v
    }

    // ─────────── Global key dispatch ───────────

    fn handle_global_keys(&mut self, ctx: &egui::Context) {
        // F1 toggles help overlay — works regardless of focus.
        if ctx.input_mut(|i| i.consume_key(egui::Modifiers::NONE, egui::Key::F1)) {
            self.show_keybindings = !self.show_keybindings;
        }

        // Esc priority chain (highest layer here; command bar / search handle their own).
        if ctx.input(|i| i.key_pressed(egui::Key::Escape)) {
            if self.show_keybindings {
                self.show_keybindings = false;
                ctx.input_mut(|i| {
                    let _ = i.consume_key(egui::Modifiers::NONE, egui::Key::Escape);
                });
            } else if self.delete_confirm.is_some() {
                self.delete_confirm = None;
                ctx.input_mut(|i| {
                    let _ = i.consume_key(egui::Modifiers::NONE, egui::Key::Escape);
                });
            }
        }

        // When delete modal is open, Enter = confirm. Consume it here so the underlying
        // tab (e.g. rooms.rs's 添加/发送 forms) doesn't also see Enter.
        if self.delete_confirm.is_some()
            && ctx.input_mut(|i| i.consume_key(egui::Modifiers::NONE, egui::Key::Enter))
        {
            if let Some(id) = self.delete_confirm.take() {
                self.manager.write().unwrap().destroy_room(&id);
                if self.selected_room_id.as_ref() == Some(&id) {
                    self.selected_room_id = None;
                }
            }
        }

        // Ctrl+1..6 — switch tab
        let num_keys = [
            egui::Key::Num1,
            egui::Key::Num2,
            egui::Key::Num3,
            egui::Key::Num4,
            egui::Key::Num5,
            egui::Key::Num6,
        ];
        for (i, key) in num_keys.iter().enumerate() {
            if ctx.input_mut(|input| input.consume_key(egui::Modifiers::CTRL, *key)) {
                if let Some(tab) = Tab::from_idx(i) {
                    self.active_tab = tab;
                }
            }
        }

        // Ctrl+Tab / Ctrl+Shift+Tab — cycle
        let ctrl_shift = egui::Modifiers {
            ctrl: true,
            shift: true,
            ..Default::default()
        };
        if ctx.input_mut(|i| i.consume_key(ctrl_shift, egui::Key::Tab)) {
            let prev = (self.active_tab.to_idx() + TAB_COUNT - 1) % TAB_COUNT;
            if let Some(t) = Tab::from_idx(prev) {
                self.active_tab = t;
            }
        }
        if ctx.input_mut(|i| i.consume_key(egui::Modifiers::CTRL, egui::Key::Tab)) {
            let next = (self.active_tab.to_idx() + 1) % TAB_COUNT;
            if let Some(t) = Tab::from_idx(next) {
                self.active_tab = t;
            }
        }

        // Ctrl+L — focus command bar (works regardless of focus, overrides browser-style)
        if ctx.input_mut(|i| i.consume_key(egui::Modifiers::CTRL, egui::Key::L)) {
            self.focus_command = true;
        }

        // Idle-only naked-key shortcuts (no text input focused)
        let idle = ctx.memory(|m| m.focused().is_none());
        if idle && ctx.input_mut(|i| i.consume_key(egui::Modifiers::NONE, egui::Key::Slash)) {
            self.focus_command = true;
        }
    }

    // ─────────── Command bar ───────────

    fn command_line(&mut self, ctx: &egui::Context) {
        // Compute completions ahead of borrowing self mutably for keys.
        let candidates: Vec<String> = {
            let rooms = self.sorted_room_ids();
            let recent = self.recent_events.clone();
            let mgr = self.manager.clone();
            let players_fn = |id: &str| -> Vec<i32> {
                let mgr = match mgr.try_read() {
                    Ok(m) => m,
                    Err(_) => return Vec::new(),
                };
                if let Some(r) = mgr.rooms.get(id) {
                    if let Ok(s) = r.shared.try_read() {
                        let mut v: Vec<i32> = s.players.keys().copied().collect();
                        v.sort();
                        return v;
                    }
                }
                Vec::new()
            };
            compute_completions(&self.command_input, &rooms, players_fn, &recent)
        };

        egui::TopBottomPanel::bottom("command_line")
            .frame(
                egui::Frame::none()
                    .inner_margin(egui::Margin::symmetric(10.0, 6.0))
                    .fill(ctx.style().visuals.window_fill()),
            )
            .show(ctx, |ui: &mut egui::Ui| {
                ui.horizontal(|ui: &mut egui::Ui| {
                    ui.label(egui::RichText::new("命令:").strong());

                    let input_width = (ui.available_width() - 110.0).max(160.0);
                    let response = ui.add_sized(
                        [input_width, 24.0],
                        egui::TextEdit::singleline(&mut self.command_input)
                            .font(egui::TextStyle::Monospace)
                            .lock_focus(true)
                            .hint_text("输入命令（Tab补全 · ↑↓历史 · F1帮助）"),
                    );

                    if self.focus_command {
                        response.request_focus();
                        self.focus_command = false;
                    }

                    // Detect user typing → reset cursors so next Tab/↑ starts fresh.
                    if self.command_input != self.command_input_prev {
                        if response.has_focus() {
                            self.popup_cursor = None;
                            self.history.reset_cursor();
                        }
                        self.command_input_prev = self.command_input.clone();
                    }

                    let focused = response.has_focus();

                    if focused {
                        self.handle_command_bar_keys(ctx, &response, &candidates);
                        render_command_popup(
                            ctx,
                            &response,
                            &self.command_input,
                            &candidates,
                            self.popup_cursor,
                        );
                    }

                    ui.label(
                        egui::RichText::new("F1 帮助")
                            .small()
                            .color(palette::TEXT_DIM),
                    );
                });

                if let Some((ref msg, is_err, ts)) = self.command_msg {
                    if current_ts() - ts < 6.0 {
                        let color = if is_err {
                            palette::DANGER
                        } else {
                            palette::SUCCESS
                        };
                        ui.colored_label(color, msg);
                    }
                }
            });
    }

    fn handle_command_bar_keys(
        &mut self,
        ctx: &egui::Context,
        response: &egui::Response,
        candidates: &[String],
    ) {
        // Detect Shift+Tab BEFORE plain Tab so we don't double-consume.
        let shift_tab = ctx.input_mut(|i| i.consume_key(egui::Modifiers::SHIFT, egui::Key::Tab));
        let tab = ctx.input_mut(|i| i.consume_key(egui::Modifiers::NONE, egui::Key::Tab));

        if (tab || shift_tab) && !candidates.is_empty() {
            let len = candidates.len() as i32;
            let new_cursor = match self.popup_cursor {
                None => {
                    if shift_tab {
                        (len - 1) as usize
                    } else {
                        0
                    }
                }
                Some(i) => {
                    let direction = if shift_tab { -1i32 } else { 1 };
                    ((i as i32 + direction).rem_euclid(len)) as usize
                }
            };
            let candidate = &candidates[new_cursor];
            self.command_input = apply_completion(&self.command_input, candidate);
            self.command_input_prev = self.command_input.clone();
            self.popup_cursor = Some(new_cursor);
        }

        // ↑ / ↓ — history. Single-line TextEdit doesn't otherwise use these.
        if ctx.input_mut(|i| i.consume_key(egui::Modifiers::NONE, egui::Key::ArrowUp)) {
            if let Some(text) = self.history.navigate(1, &self.command_input) {
                self.command_input = text;
                self.command_input_prev = self.command_input.clone();
                self.popup_cursor = None;
            }
        }
        if ctx.input_mut(|i| i.consume_key(egui::Modifiers::NONE, egui::Key::ArrowDown)) {
            if let Some(text) = self.history.navigate(-1, &self.command_input) {
                self.command_input = text;
                self.command_input_prev = self.command_input.clone();
                self.popup_cursor = None;
            }
        }

        // Esc — local priority chain: popup → clear input → blur
        if ctx.input(|i| i.key_pressed(egui::Key::Escape)) {
            ctx.input_mut(|i| {
                let _ = i.consume_key(egui::Modifiers::NONE, egui::Key::Escape);
            });
            if self.popup_cursor.is_some() {
                self.popup_cursor = None;
            } else if !self.command_input.is_empty() {
                self.command_input.clear();
                self.command_input_prev.clear();
                self.history.reset_cursor();
            } else {
                response.surrender_focus();
            }
        }

        // Enter — execute. lock_focus(true) prevents the TextEdit from losing focus on Enter,
        // so we keep the cursor here for the next command.
        if ctx.input(|i| i.key_pressed(egui::Key::Enter)) {
            ctx.input_mut(|i| {
                let _ = i.consume_key(egui::Modifiers::NONE, egui::Key::Enter);
            });
            self.execute_command_input();
            self.popup_cursor = None;
            self.focus_command = true;
        }
    }

    fn execute_command_input(&mut self) {
        let command = self.command_input.trim().to_string();
        if command.is_empty() {
            return;
        }

        self.command_input.clear();
        self.command_input_prev.clear();
        self.history.push(command.clone());

        let now = current_ts();
        let (message, is_err) = match self.execute_command(&command) {
            Ok(message) => (message, false),
            Err(message) => (message, true),
        };

        self.logs.push(LogEntry {
            timestamp: now,
            level: if is_err { "ERR" } else { "INF" }.into(),
            source: "GUI".into(),
            message: format!("$ {} -> {}", command, message),
            room_id: "gui".into(),
            player_index: -1,
        });
        self.command_msg = Some((message, is_err, now));
    }

    fn execute_command(&mut self, command: &str) -> Result<String, String> {
        let args = split_command_args(command)?;
        if args.is_empty() {
            return Ok(String::new());
        }

        match args[0].as_str() {
            "rooms" => self.command_list_rooms(),
            "clear" => {
                self.logs.clear();
                Ok("日志已清空".into())
            }
            "create" => self.command_create_room(&args),
            "select" => self.command_select_room(&args),
            "stop" => self.command_stop_room(&args),
            "destroy" => self.command_destroy_room(&args),
            "restart" => self.command_restart_room(&args),
            "join" => self.command_join_player(&args),
            "leave" => self.command_leave_player(&args),
            "exit" => self.command_exit_player(&args),
            "event" => self.command_send_event(&args),
            other => Err(format!("未知命令: {}", other)),
        }
    }

    fn command_list_rooms(&self) -> Result<String, String> {
        let manager = self.manager.read().unwrap();
        if manager.rooms.is_empty() {
            return Ok("暂无房间".into());
        }

        let mut rooms: Vec<String> = manager
            .rooms
            .iter()
            .map(|(id, room)| {
                let shared = room.shared.read().unwrap();
                format!("{}({})", id, shared.status)
            })
            .collect();
        rooms.sort();
        Ok(format!("房间: {}", rooms.join(", ")))
    }

    fn command_create_room(&mut self, args: &[String]) -> Result<String, String> {
        if args.len() < 2 {
            return Err("用法: create <脚本目录> [模式]".into());
        }

        let script_dir = PathBuf::from(&args[1]);
        if !script_dir.is_dir() {
            return Err(format!("脚本目录不存在: {}", args[1]));
        }

        let mode_id = parse_optional_i32(args.get(2), 0, "模式")?;
        let archive_dir = self.config.read().unwrap().archive_dir.clone();
        let mut players = HashMap::new();
        players.insert(0, Player::new(0, "Player_0".into()));
        storage::apply_saved_archives(&archive_dir, &args[1], &mut players);

        let room_id =
            self.manager
                .write()
                .unwrap()
                .create_room(script_dir, mode_id, players, archive_dir);
        self.selected_room_id = Some(room_id.clone());
        self.active_tab = Tab::Rooms;
        Ok(format!("已创建房间 {}", room_id))
    }

    fn command_select_room(&mut self, args: &[String]) -> Result<String, String> {
        let room_id = required_arg(args, 1, "房间")?;
        let exists = self.manager.read().unwrap().rooms.contains_key(room_id);
        if !exists {
            return Err(format!("房间不存在: {}", room_id));
        }

        self.selected_room_id = Some(room_id.to_string());
        self.scroll_to_selected_room = true;
        self.active_tab = Tab::Rooms;
        Ok(format!("已选择房间 {}", room_id))
    }

    fn command_stop_room(&self, args: &[String]) -> Result<String, String> {
        let room_id = required_arg(args, 1, "房间")?;
        let manager = self.manager.read().unwrap();
        let room = manager
            .rooms
            .get(room_id)
            .ok_or_else(|| format!("房间不存在: {}", room_id))?;
        room.stop("GuiCommand".into());
        Ok(format!("已停止房间 {}", room_id))
    }

    fn command_destroy_room(&mut self, args: &[String]) -> Result<String, String> {
        let room_id = required_arg(args, 1, "房间")?;
        let destroyed = self.manager.write().unwrap().destroy_room(room_id);
        if !destroyed {
            return Err(format!("房间不存在: {}", room_id));
        }
        if self.selected_room_id.as_deref() == Some(room_id) {
            self.selected_room_id = None;
        }
        Ok(format!("已删除房间 {}", room_id))
    }

    fn command_restart_room(&mut self, args: &[String]) -> Result<String, String> {
        let room_id = required_arg(args, 1, "房间")?;
        let archive_dir = self.config.read().unwrap().archive_dir.clone();
        let new_room_id = self
            .manager
            .write()
            .unwrap()
            .restart_room(room_id, archive_dir, "GuiCommand".into())
            .ok_or_else(|| format!("房间不存在: {}", room_id))?;
        self.selected_room_id = Some(new_room_id.clone());
        self.active_tab = Tab::Rooms;
        Ok(format!("已重启房间 {} -> {}", room_id, new_room_id))
    }

    fn command_join_player(&self, args: &[String]) -> Result<String, String> {
        let room_id = required_arg(args, 1, "房间")?;
        let player_index = parse_required_i32(args, 2, "槽位")?;
        let name = if args.len() > 3 {
            args[3..].join(" ")
        } else {
            format!("Player_{}", player_index)
        };

        let manager = self.manager.read().unwrap();
        let room = manager
            .rooms
            .get(room_id)
            .ok_or_else(|| format!("房间不存在: {}", room_id))?;
        room.join_player(player_index, name, "Connect".into());
        Ok(format!("玩家 {} 已加入 {}", player_index, room_id))
    }

    fn command_leave_player(&self, args: &[String]) -> Result<String, String> {
        let room_id = required_arg(args, 1, "房间")?;
        let player_index = parse_required_i32(args, 2, "槽位")?;
        let manager = self.manager.read().unwrap();
        let room = manager
            .rooms
            .get(room_id)
            .ok_or_else(|| format!("房间不存在: {}", room_id))?;
        let errnu = room.leave_player(player_index, "Disconnect".into());
        if errnu != crate::room::ERR_OK {
            return Err(format!("玩家离线失败: errnu={}", errnu));
        }
        Ok(format!("玩家 {} 已离线", player_index))
    }

    fn command_exit_player(&self, args: &[String]) -> Result<String, String> {
        let room_id = required_arg(args, 1, "房间")?;
        let player_index = parse_required_i32(args, 2, "槽位")?;
        let manager = self.manager.read().unwrap();
        let room = manager
            .rooms
            .get(room_id)
            .ok_or_else(|| format!("房间不存在: {}", room_id))?;
        let errnu = room.exit_player(player_index, "Logout".into());
        if errnu != crate::room::ERR_OK {
            return Err(format!("玩家退出失败: errnu={}", errnu));
        }
        Ok(format!("玩家 {} 已退出", player_index))
    }

    fn command_send_event(&mut self, args: &[String]) -> Result<String, String> {
        let parsed = parse_event_command(args)?;
        {
            let manager = self.manager.read().unwrap();
            let room = manager
                .rooms
                .get(&parsed.room_id)
                .ok_or_else(|| format!("房间不存在: {}", parsed.room_id))?;
            let errnu =
                room.send_event(parsed.ename.clone(), parsed.evalue, parsed.player_index);
            if errnu != crate::room::ERR_OK {
                return Err(format!("发送事件失败: errnu={}", errnu));
            }
        }
        self.record_recent_event(&parsed.ename);
        Ok(format!("已发送事件 {} 到 {}", parsed.ename, parsed.room_id))
    }

    // ─────────── Overlays ───────────

    fn render_delete_confirm(&mut self, ctx: &egui::Context) {
        let id = match &self.delete_confirm {
            Some(id) => id.clone(),
            None => return,
        };

        let mut cancel = false;
        let mut confirm = false;

        egui::Window::new("确认删除房间")
            .collapsible(false)
            .resizable(false)
            .movable(false)
            .anchor(egui::Align2::CENTER_CENTER, egui::vec2(0.0, 0.0))
            .order(egui::Order::Foreground)
            .show(ctx, |ui| {
                ui.set_min_width(380.0);
                ui.add_space(8.0);

                // Centered warning icon + body
                ui.vertical_centered(|ui| {
                    ui.label(
                        egui::RichText::new("⚠")
                            .size(36.0)
                            .color(palette::WARNING),
                    );
                    ui.add_space(6.0);
                    ui.label(
                        egui::RichText::new(format!("删除房间 \"{}\"", id))
                            .size(15.0)
                            .strong()
                            .color(palette::TEXT_PRIMARY),
                    );
                    ui.add_space(2.0);
                    ui.label(
                        egui::RichText::new("此操作不可恢复")
                            .size(13.0)
                            .color(palette::TEXT_DIM),
                    );
                });

                ui.add_space(14.0);

                // Action buttons — danger button visually heavier
                ui.vertical_centered(|ui| {
                    ui.horizontal(|ui| {
                        ui.spacing_mut().item_spacing.x = 10.0;
                        let delete_btn = egui::Button::new(
                            egui::RichText::new("  删除  ")
                                .strong()
                                .color(egui::Color32::WHITE),
                        )
                        .fill(palette::DANGER);
                        if ui.add(delete_btn).clicked() {
                            confirm = true;
                        }
                        if ui.button("  取消  ").clicked() {
                            cancel = true;
                        }
                    });
                });

                ui.add_space(4.0);
                ui.vertical_centered(|ui| {
                    ui.label(
                        egui::RichText::new("Enter 确认 · Esc 取消")
                            .size(11.0)
                            .color(palette::TEXT_FAINT),
                    );
                });
                ui.add_space(4.0);
            });

        // Note: Enter for confirm is consumed in handle_global_keys earlier in this frame
        // so the underlying tab forms don't double-fire.

        if cancel {
            self.delete_confirm = None;
        } else if confirm {
            self.delete_confirm = None;
            self.manager.write().unwrap().destroy_room(&id);
            if self.selected_room_id.as_ref() == Some(&id) {
                self.selected_room_id = None;
            }
        }
    }

    fn render_keybindings(&mut self, ctx: &egui::Context) {
        if !self.show_keybindings {
            return;
        }
        let mut open = true;
        egui::Window::new("键盘快捷键")
            .collapsible(false)
            .resizable(false)
            .anchor(egui::Align2::CENTER_CENTER, egui::vec2(0.0, 0.0))
            .order(egui::Order::Foreground)
            .open(&mut open)
            .show(ctx, |ui| {
                ui.set_min_width(520.0);
                egui::Grid::new("keybindings_grid")
                    .num_columns(3)
                    .striped(true)
                    .spacing([14.0, 4.0])
                    .show(ui, |ui| {
                        ui.label(egui::RichText::new("范围").strong());
                        ui.label(egui::RichText::new("按键").strong());
                        ui.label(egui::RichText::new("作用").strong());
                        ui.end_row();

                        let mut last_scope = "";
                        for kb in KEYBINDINGS {
                            if kb.scope != last_scope {
                                ui.label(
                                    egui::RichText::new(kb.scope)
                                        .strong()
                                        .color(palette::ACCENT),
                                );
                                last_scope = kb.scope;
                            } else {
                                ui.label("");
                            }
                            ui.label(
                                egui::RichText::new(kb.keys)
                                    .monospace()
                                    .color(palette::TEXT_PRIMARY),
                            );
                            ui.label(kb.desc);
                            ui.end_row();
                        }
                    });
                ui.add_space(6.0);
                ui.label(
                    egui::RichText::new("按 F1 或 Esc 关闭")
                        .small()
                        .color(palette::TEXT_DIM),
                );
            });
        if !open {
            self.show_keybindings = false;
        }
    }
}

impl Drop for GuiApp {
    fn drop(&mut self) {
        if let Ok(mut mgr) = self.manager.write() {
            mgr.shutdown_all();
        }
    }
}

impl eframe::App for GuiApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        if let Some(native_ppp) = ctx.native_pixels_per_point() {
            if (ctx.pixels_per_point() - native_ppp).abs() > 0.01 {
                ctx.set_pixels_per_point(native_ppp);
            }
        }

        // eframe may flip us into light mode if the system theme changes mid-session.
        // Re-apply our dark style whenever we detect a flip. Cheap to check; only re-runs setup on actual changes.
        if !ctx.style().visuals.dark_mode {
            setup_style(ctx);
        }

        self.sync_subscriptions();
        self.drain_channels();

        // Global keys first so modals/tabs see consumed state.
        self.handle_global_keys(ctx);

        let room_count = self.subscribed_rooms.len();

        egui::TopBottomPanel::top("tab_bar")
            .frame(
                egui::Frame::none()
                    .inner_margin(egui::Margin::symmetric(8.0, 6.0))
                    .fill(ctx.style().visuals.window_fill()),
            )
            .show(ctx, |ui: &mut egui::Ui| {
                ui.horizontal(|ui: &mut egui::Ui| {
                    ui.spacing_mut().item_spacing.x = 6.0;
                    for (idx, (tab, label)) in [
                        (Tab::Rooms, "房间"),
                        (Tab::Console, "控制台"),
                        (Tab::Events, "出站事件"),
                        (Tab::State, "状态"),
                        (Tab::Profiler, "性能分析"),
                        (Tab::Settings, "设置"),
                    ]
                    .iter()
                    .enumerate()
                    {
                        let selected = self.active_tab == *tab;
                        let key_hint = egui::RichText::new(format!(" {}", idx + 1))
                            .size(11.0)
                            .color(palette::TEXT_FAINT);
                        let label_text = egui::RichText::new(*label).size(15.0);
                        let label_text = if selected {
                            label_text.strong().color(palette::TEXT_BRIGHT)
                        } else {
                            label_text.color(palette::TEXT_DIM)
                        };

                        let resp = ui
                            .horizontal(|ui| {
                                ui.spacing_mut().item_spacing.x = 1.0;
                                let r1 = ui.selectable_label(selected, label_text);
                                let r2 = ui.selectable_label(selected, key_hint);
                                r1.union(r2)
                            })
                            .inner;

                        if resp.clicked() {
                            self.active_tab = *tab;
                        }
                        if selected {
                            palette::underline(ui.painter(), resp.rect, palette::ACCENT);
                        }
                    }

                    ui.with_layout(
                        egui::Layout::right_to_left(egui::Align::Center),
                        |ui: &mut egui::Ui| {
                            ui.label(
                                egui::RichText::new(format!(
                                    "房间 {} · 日志 {}",
                                    room_count,
                                    self.logs.len()
                                ))
                                .small()
                                .color(palette::TEXT_DIM),
                            );
                            ui.separator();
                            ui.label(
                                egui::RichText::new("/ 命令 · F1 帮助")
                                    .small()
                                    .color(palette::TEXT_DIM),
                            );
                        },
                    );
                });
            });

        self.command_line(ctx);

        match self.active_tab {
            Tab::Rooms => self.rooms_tab(ctx),
            Tab::Console => self.console_tab(ctx),
            Tab::Events => self.events_tab(ctx),
            Tab::State => self.state_tab(ctx),
            Tab::Profiler => self.profiler_tab(ctx),
            Tab::Settings => self.settings_tab(ctx),
        }

        // Modals last so they paint on top.
        self.render_delete_confirm(ctx);
        self.render_keybindings(ctx);

        ctx.request_repaint_after(std::time::Duration::from_millis(100));
    }
}

// ─────────── Popup rendering ───────────

fn render_command_popup(
    ctx: &egui::Context,
    response: &egui::Response,
    input: &str,
    candidates: &[String],
    cursor: Option<usize>,
) {
    // Don't crowd the screen with the full command list before the user types anything.
    if input.trim().is_empty() {
        return;
    }

    let (tokens, _) = tokens_for_completion(input);
    let at_command_position = tokens.is_empty();

    let rows: Vec<String> = if at_command_position && !candidates.is_empty() {
        let hints = matching_command_hints(input);
        if !hints.is_empty() {
            hints.iter().map(|h| h.usage.to_string()).collect()
        } else {
            candidates.to_vec()
        }
    } else {
        candidates.to_vec()
    };

    if rows.is_empty() {
        return;
    }

    let row_height = 20.0;
    let visible = rows.len().min(10);
    let popup_height = 12.0 + row_height * visible as f32;
    let pos = egui::pos2(
        response.rect.left(),
        (response.rect.top() - popup_height - 6.0).max(0.0),
    );

    egui::Area::new(egui::Id::new("command_hint_popup"))
        .order(egui::Order::Foreground)
        .fixed_pos(pos)
        .show(ctx, |ui| {
            egui::Frame::popup(ui.style()).show(ui, |ui| {
                ui.set_min_width(response.rect.width().min(640.0));
                for (i, row) in rows.iter().enumerate().take(visible) {
                    let selected = cursor == Some(i);
                    let mut text = egui::RichText::new(row).monospace().size(13.0);
                    if selected {
                        text = text
                            .strong()
                            .background_color(palette::ACCENT_BG)
                            .color(palette::TEXT_BRIGHT);
                    }
                    ui.label(text);
                }
            });
        });
}

// ─────────── Style / helpers ───────────

fn setup_fonts(ctx: &egui::Context) {
    let mut fonts = egui::FontDefinitions::default();

    let font_paths = [
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc",
    ];

    for path in &font_paths {
        if let Ok(data) = std::fs::read(path) {
            fonts
                .font_data
                .insert("chinese".to_owned(), egui::FontData::from_owned(data));
            if let Some(family) = fonts.families.get_mut(&egui::FontFamily::Proportional) {
                family.insert(0, "chinese".to_owned());
            }
            if let Some(family) = fonts.families.get_mut(&egui::FontFamily::Monospace) {
                family.push("chinese".to_owned());
            }
            break;
        }
    }

    ctx.set_fonts(fonts);
}

fn setup_style(ctx: &egui::Context) {
    // Start fresh from dark baseline so all text/stroke defaults are dark-mode-correct,
    // even if eframe initialized us in light mode following system theme.
    ctx.set_visuals(egui::Visuals::dark());

    let mut style = (*ctx.style()).clone();
    style.visuals = egui::Visuals::dark();

    style.text_styles = [
        (
            egui::TextStyle::Small,
            egui::FontId::new(13.0, egui::FontFamily::Proportional),
        ),
        (
            egui::TextStyle::Body,
            egui::FontId::new(15.0, egui::FontFamily::Proportional),
        ),
        (
            egui::TextStyle::Button,
            egui::FontId::new(15.0, egui::FontFamily::Proportional),
        ),
        (
            egui::TextStyle::Heading,
            egui::FontId::new(20.0, egui::FontFamily::Proportional),
        ),
        (
            egui::TextStyle::Monospace,
            egui::FontId::new(14.0, egui::FontFamily::Monospace),
        ),
    ]
    .into();

    style.spacing.item_spacing = egui::vec2(8.0, 6.0);
    style.spacing.window_margin = egui::Margin::same(10.0);
    style.spacing.button_padding = egui::vec2(10.0, 5.0);

    let v = &mut style.visuals;
    v.window_fill = palette::BG_BASE;
    v.panel_fill = palette::BG_PANEL;
    v.faint_bg_color = palette::BG_RAISED;
    v.extreme_bg_color = palette::BG_DEEP;
    v.code_bg_color = palette::BG_RAISED;

    v.error_fg_color = palette::DANGER;
    v.warn_fg_color = palette::WARNING;
    v.hyperlink_color = palette::ACCENT;

    v.selection.bg_fill = palette::ACCENT_BG;
    v.selection.stroke = egui::Stroke::new(1.0, palette::ACCENT);

    v.widgets.noninteractive.bg_fill = palette::BG_NONINTERACTIVE;
    v.widgets.noninteractive.weak_bg_fill = palette::BG_NONINTERACTIVE_WEAK;
    v.widgets.noninteractive.bg_stroke = egui::Stroke::new(1.0, palette::STROKE_NONINTERACTIVE);
    v.widgets.noninteractive.fg_stroke = egui::Stroke::new(1.0, palette::TEXT_PRIMARY);
    v.widgets.noninteractive.rounding = egui::Rounding::same(4.0);

    v.widgets.inactive.bg_fill = palette::BG_INACTIVE;
    v.widgets.inactive.weak_bg_fill = palette::BG_INACTIVE_WEAK;
    v.widgets.inactive.bg_stroke = egui::Stroke::new(1.0, palette::STROKE_BASE);
    v.widgets.inactive.fg_stroke = egui::Stroke::new(1.0, palette::TEXT_PRIMARY);
    v.widgets.inactive.rounding = egui::Rounding::same(4.0);

    v.widgets.hovered.bg_fill = palette::BG_HOVERED;
    v.widgets.hovered.weak_bg_fill = palette::BG_HOVERED_WEAK;
    v.widgets.hovered.bg_stroke = egui::Stroke::new(1.0, palette::STROKE_STRONG);
    v.widgets.hovered.fg_stroke = egui::Stroke::new(1.5, palette::TEXT_BRIGHT);
    v.widgets.hovered.rounding = egui::Rounding::same(4.0);

    v.widgets.active.bg_fill = palette::BG_ACTIVE;
    v.widgets.active.weak_bg_fill = palette::BG_ACTIVE_WEAK;
    v.widgets.active.bg_stroke = egui::Stroke::new(1.0, palette::ACCENT);
    v.widgets.active.fg_stroke = egui::Stroke::new(2.0, egui::Color32::WHITE);
    v.widgets.active.rounding = egui::Rounding::same(4.0);

    v.widgets.open.bg_fill = palette::BG_OPEN;
    v.widgets.open.weak_bg_fill = palette::BG_OPEN_WEAK;
    v.widgets.open.bg_stroke = egui::Stroke::new(1.0, palette::STROKE_OPEN);
    v.widgets.open.fg_stroke = egui::Stroke::new(1.0, palette::TEXT_PRIMARY);
    v.widgets.open.rounding = egui::Rounding::same(4.0);

    v.window_rounding = egui::Rounding::same(6.0);
    v.window_stroke = egui::Stroke::new(1.0, palette::STROKE_FAINT);
    v.striped = true;

    ctx.set_style(style);
}

pub fn section_heading(ui: &mut egui::Ui, text: &str) {
    ui.add_space(2.0);
    ui.horizontal(|ui| {
        ui.spacing_mut().item_spacing.x = 6.0;
        palette::accent_bar(ui, 18.0, palette::ACCENT);
        ui.label(egui::RichText::new(text).strong().size(16.0));
    });
    ui.add_space(2.0);
}

pub fn format_time(ts: f64) -> String {
    let secs = ts as i64;
    let nanos = ((ts - secs as f64) * 1e9) as u32;
    chrono::DateTime::from_timestamp(secs, nanos)
        .map(|dt| {
            dt.with_timezone(&chrono::Local)
                .format("%H:%M:%S")
                .to_string()
        })
        .unwrap_or_else(|| format!("{:.1}", ts))
}

fn current_ts() -> f64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs_f64()
}

fn required_arg<'a>(args: &'a [String], index: usize, name: &str) -> Result<&'a str, String> {
    args.get(index)
        .map(|s| s.as_str())
        .ok_or_else(|| format!("缺少参数: {}", name))
}

fn parse_required_i32(args: &[String], index: usize, name: &str) -> Result<i32, String> {
    required_arg(args, index, name)?
        .parse::<i32>()
        .map_err(|_| format!("{} 必须是整数", name))
}

fn parse_optional_i32(value: Option<&String>, default: i32, name: &str) -> Result<i32, String> {
    match value {
        Some(value) => value
            .parse::<i32>()
            .map_err(|_| format!("{} 必须是整数", name)),
        None => Ok(default),
    }
}

pub fn format_duration(secs: i64) -> String {
    let h = secs / 3600;
    let m = (secs % 3600) / 60;
    let s = secs % 60;
    format!("{:02}:{:02}:{:02}", h, m, s)
}
