use std::collections::VecDeque;

use dioxus::prelude::*;
use serde::Serialize;

use crate::room::{LogEntry, OutEvent, ProfileData};

pub const MAX_LOGS: usize = 10_000;
pub const MAX_EVENTS: usize = 10_000;

#[derive(PartialEq, Eq, Clone, Copy, Debug)]
pub enum Tab {
    Rooms,
    Console,
    Events,
    State,
    Profiler,
    Settings,
}

impl Tab {
    pub const ALL: [Tab; 6] = [
        Tab::Rooms,
        Tab::Console,
        Tab::Events,
        Tab::State,
        Tab::Profiler,
        Tab::Settings,
    ];

    pub fn label(self) -> &'static str {
        match self {
            Tab::Rooms => "房间",
            Tab::Console => "日志",
            Tab::Events => "事件",
            Tab::State => "状态",
            Tab::Profiler => "性能",
            Tab::Settings => "设置",
        }
    }

    pub fn from_idx(i: usize) -> Option<Tab> {
        Self::ALL.get(i).copied()
    }
}

/// (message, is_error, timestamp_secs) — 3s 自动消失
pub type FlashMsg = Option<(String, bool, f64)>;

/// Profiler snapshot pushed to JS canvas every 200ms.
#[derive(Clone, Serialize, Default)]
pub struct ProfileSnapshot {
    pub room_id: String,
    pub available: bool,
    pub running: bool,
    pub hook_count: i32,
    pub window_sec: i32,
    pub frame_ms: f32,
    pub data: Option<ProfileData>,
}

// ─── GlobalSignals ────────────────────────────────────────

pub static SELECTED_TAB: GlobalSignal<Tab> = Signal::global(|| Tab::Rooms);

// Rooms
pub static SELECTED_ROOM: GlobalSignal<Option<String>> = Signal::global(|| None);
pub static SHOW_CREATE_ROOM: GlobalSignal<bool> = Signal::global(|| false);
pub static NEW_ROOM_SCRIPT_DIR: GlobalSignal<String> = Signal::global(String::new);
pub static NEW_ROOM_MODE_ID: GlobalSignal<i32> = Signal::global(|| 0);
pub static ADD_PLAYER_INDEX: GlobalSignal<String> = Signal::global(|| "0".to_string());
pub static ADD_PLAYER_NAME: GlobalSignal<String> = Signal::global(String::new);
pub static EVENT_NAME: GlobalSignal<String> = Signal::global(String::new);
pub static EVENT_DATA: GlobalSignal<String> = Signal::global(String::new);
pub static EVENT_PLAYER_IDX: GlobalSignal<i32> = Signal::global(|| -1);

// Console
pub static LOGS: GlobalSignal<VecDeque<LogEntry>> = Signal::global(VecDeque::new);
pub static LOG_LEVEL_FILTER: GlobalSignal<String> = Signal::global(String::new);
pub static LOG_ROOM_FILTER: GlobalSignal<String> = Signal::global(String::new);
pub static LOG_SEARCH: GlobalSignal<String> = Signal::global(String::new);
pub static AUTO_SCROLL: GlobalSignal<bool> = Signal::global(|| true);

// Events
pub static EVENTS: GlobalSignal<VecDeque<OutEvent>> = Signal::global(VecDeque::new);
pub static EVENT_ROOM_FILTER: GlobalSignal<String> = Signal::global(String::new);

// State view
pub static STATE_ROOM: GlobalSignal<Option<String>> = Signal::global(|| None);
pub static STATE_JSON: GlobalSignal<String> = Signal::global(String::new);

// Settings
pub static SETTINGS_HOST: GlobalSignal<String> = Signal::global(String::new);
pub static SETTINGS_PORT: GlobalSignal<String> = Signal::global(String::new);
pub static SETTINGS_ARCHIVE_DIR: GlobalSignal<String> = Signal::global(String::new);
pub static SAVE_MSG: GlobalSignal<FlashMsg> = Signal::global(|| None);

// Profiler
pub static PROFILER_ROOM: GlobalSignal<Option<String>> = Signal::global(|| None);
pub static PROFILER_HOOK_COUNT: GlobalSignal<i32> = Signal::global(|| 5000);
pub static PROFILER_WINDOW: GlobalSignal<i32> = Signal::global(|| 15);
pub static PROFILER_FRAME_MS: GlobalSignal<f32> = Signal::global(|| 50.0);
pub static PROFILE_SNAPSHOT: GlobalSignal<Option<ProfileSnapshot>> = Signal::global(|| None);

// Command bar
pub static CMD_INPUT: GlobalSignal<String> = Signal::global(String::new);
pub static CMD_MSG: GlobalSignal<FlashMsg> = Signal::global(|| None);
pub static CMD_HISTORY: GlobalSignal<VecDeque<String>> = Signal::global(VecDeque::new);
pub static CMD_HISTORY_CURSOR: GlobalSignal<Option<usize>> = Signal::global(|| None);
pub static CMD_POPUP_CURSOR: GlobalSignal<Option<usize>> = Signal::global(|| None);
pub static RECENT_EVENTS: GlobalSignal<VecDeque<String>> = Signal::global(VecDeque::new);

// Modals
pub static DELETE_CONFIRM: GlobalSignal<Option<String>> = Signal::global(|| None);
pub static SHOW_KEYBINDINGS: GlobalSignal<bool> = Signal::global(|| false);

// ─── helpers ──────────────────────────────────────────────

pub fn now_secs() -> f64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs_f64()
}
