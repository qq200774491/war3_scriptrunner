mod cmd;
mod components;
mod coroutines;
pub mod state;

use std::sync::{Arc, RwLock};

use dioxus::desktop::tao::dpi::LogicalSize;
use dioxus::desktop::{Config as DesktopConfig, WindowBuilder};
use dioxus::prelude::*;

use crate::config::AppConfig;
use crate::room::RoomManager;

use components::about::About;
use components::command_bar::CommandBar;
use components::console::Console;
use components::events::Events;
use components::modals::{DeleteConfirmModal, KeybindingsModal};
use components::profiler::Profiler;
use components::rooms::Rooms;
use components::settings::Settings;
use components::state_view::StateView;
use components::tab_bar::TabBar;
use state::{
    Tab, SELECTED_TAB, SETTINGS_ARCHIVE_DIR, SETTINGS_HOST, SETTINGS_PORT, SHOW_KEYBINDINGS,
};

#[derive(Clone)]
pub struct AppCtx {
    pub manager: Arc<RwLock<RoomManager>>,
    pub config: Arc<RwLock<AppConfig>>,
    pub config_path: String,
}

pub fn launch_app(
    manager: Arc<RwLock<RoomManager>>,
    config: Arc<RwLock<AppConfig>>,
    config_path: String,
) {
    let ctx = AppCtx {
        manager,
        config,
        config_path,
    };
    let title = format!("MLS 云脚本环境模拟 v{}", env!("CARGO_PKG_VERSION"));
    let cfg = DesktopConfig::new()
        .with_menu(None)
        .with_window(
            WindowBuilder::new()
                .with_title(title)
                .with_inner_size(LogicalSize::new(1200.0, 800.0))
                .with_min_inner_size(LogicalSize::new(800.0, 600.0)),
        );
    LaunchBuilder::desktop()
        .with_cfg(cfg)
        .with_context(ctx)
        .launch(app);
}

fn app() -> Element {
    let ctx = use_context::<AppCtx>();

    // 第一次渲染：把 config 字段同步到 SETTINGS_* GlobalSignal
    use_hook(|| {
        let c = ctx.config.read().unwrap();
        *SETTINGS_HOST.write() = c.host.clone();
        *SETTINGS_PORT.write() = c.port.to_string();
        *SETTINGS_ARCHIVE_DIR.write() = c.archive_dir.clone();
    });

    // 启动 broadcast pull coroutine（100ms tick）
    let _ = use_resource({
        let manager = ctx.manager.clone();
        move || {
            let manager = manager.clone();
            async move { coroutines::pump(manager).await }
        }
    });

    let on_keydown = move |e: Event<KeyboardData>| {
        let key = e.key();
        let mods = e.modifiers();
        if key == Key::F1 {
            *SHOW_KEYBINDINGS.write() = !SHOW_KEYBINDINGS();
            e.prevent_default();
            return;
        }
        if mods.ctrl() {
            // Ctrl+1..7 切 Tab
            if let Key::Character(c) = &key {
                if let Some(n) = c.chars().next().and_then(|c| c.to_digit(10)) {
                    if (1..=7).contains(&n) {
                        if let Some(t) = Tab::from_idx((n - 1) as usize) {
                            *SELECTED_TAB.write() = t;
                            e.prevent_default();
                        }
                    }
                }
            }
        }
    };

    let current = SELECTED_TAB();

    rsx! {
        style { {include_str!("style.css")} }
        div { class: "app", tabindex: "-1", autofocus: true, onkeydown: on_keydown,
            TabBar {}
            main { class: "panel",
                match current {
                    Tab::Rooms    => rsx! { Rooms {} },
                    Tab::Console  => rsx! { Console {} },
                    Tab::Events   => rsx! { Events {} },
                    Tab::State    => rsx! { StateView {} },
                    Tab::Profiler => rsx! { Profiler {} },
                    Tab::Settings => rsx! { Settings {} },
                    Tab::About    => rsx! { About {} },
                }
            }
            CommandBar {}
            DeleteConfirmModal {}
            KeybindingsModal {}
        }
    }
}
