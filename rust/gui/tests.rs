//! 直接向 egui 输入队列注入事件，不操作桌面，不启动魔兽。
use super::*;
use egui::{Event, Key, Modifiers, PointerButton, Pos2, Rect, Vec2};

struct Harness {
    _root: tempfile::TempDir,
    app: App,
    ctx: egui::Context,
    size: Vec2,
    dpi: f32,
    output: egui::FullOutput,
    commands: Vec<egui::ViewportCommand>,
}
impl Harness {
    fn new(width: f32, height: f32, dpi: f32) -> Self {
        let root = tempfile::tempdir().unwrap();
        let path = root.path();
        let config = config::Config {
            map: path.join("test.w3x"),
            local_entry: path.join("client/main.lua"),
            cloud_scripts: path.join("cloud"),
            archives: path.join("archives"),
            editor: path.join("editor"),
            source_game: path.join("source"),
            game: path.join("game"),
            seconds: 3600,
            startup_timeout: 90.0,
        };
        config.save(&config::default_config(path)).unwrap();
        let ctx = egui::Context::default();
        ctx.enable_accesskit();
        let app = App::create(&ctx, path.to_owned(), None);
        let mut this = Self {
            _root: root,
            app,
            ctx,
            size: Vec2::new(width, height),
            dpi,
            output: Default::default(),
            commands: Vec::new(),
        };
        this.frame(vec![]);
        this.frame(vec![]);
        this
    }
    fn frame(&mut self, events: Vec<Event>) {
        let mut input = egui::RawInput {
            screen_rect: Some(Rect::from_min_size(Pos2::ZERO, self.size)),
            events,
            focused: true,
            ..Default::default()
        };
        input
            .viewports
            .get_mut(&egui::ViewportId::ROOT)
            .unwrap()
            .native_pixels_per_point = Some(self.dpi);
        self.output = self.ctx.run(input, |ctx| {
            self.app.shell(ctx);
            self.app.dialogs(ctx);
            chrome::edges(ctx, self.app.theme.palette());
        });
        self.commands.extend(
            self.output
                .viewport_output
                .values()
                .flat_map(|output| output.commands.iter().cloned()),
        );
    }
    fn rects(&self, name: &str) -> Vec<Rect> {
        self.output
            .platform_output
            .accesskit_update
            .as_ref()
            .unwrap()
            .nodes
            .iter()
            .filter(|(_, node)| node.name() == Some(name))
            .filter_map(|(_, node)| node.bounds())
            .map(|r| {
                Rect::from_min_max(
                    egui::pos2(r.x0 as f32, r.y0 as f32),
                    egui::pos2(r.x1 as f32, r.y1 as f32),
                )
            })
            .collect()
    }
    fn target(&self, name: &str) -> Pos2 {
        self.rects(name)
            .into_iter()
            .find(|r| self.ctx.screen_rect().contains(r.center()))
            .unwrap_or_else(|| {
                panic!(
                    "missing {name}; available {:?}",
                    self.output
                        .platform_output
                        .accesskit_update
                        .as_ref()
                        .unwrap()
                        .nodes
                        .iter()
                        .filter_map(|(_, n)| n.name())
                        .collect::<Vec<_>>()
                )
            })
            .center()
    }
    fn click(&mut self, point: Pos2) {
        self.frame(vec![Event::PointerMoved(point)]);
        for pressed in [true, false] {
            self.frame(vec![Event::PointerButton {
                pos: point,
                button: PointerButton::Primary,
                pressed,
                modifiers: Modifiers::NONE,
            }]);
        }
        self.frame(vec![]);
    }
    fn named(&mut self, name: &str) {
        self.click(self.target(name));
    }
    fn rail(&mut self, name: &str) {
        let rect = self
            .rects(name)
            .into_iter()
            .find(|r| r.right() <= 48.0)
            .expect("rail icon accessible name");
        self.click(rect.center());
    }
    fn key(&mut self, key: Key) {
        for pressed in [true, false] {
            self.frame(vec![Event::Key {
                key,
                physical_key: Some(key),
                pressed,
                repeat: false,
                modifiers: Modifiers::NONE,
            }]);
        }
    }
}

#[test]
fn navigation_icons_switch_pages_across_dpi_and_window_sizes() {
    for (width, height) in [(1240.0, 780.0), (900.0, 580.0)] {
        for dpi in [1.0, 1.25, 1.5, 2.0] {
            let mut h = Harness::new(width, height, dpi);
            h.rail("配置");
            assert!(h.app.page == Page::Settings);
            h.rail("玩家存档");
            assert!(h.app.page == Page::Archives);
            h.rail("云脚本控制台");
            assert!(h.app.page == Page::Console);
            assert_eq!(h.ctx.pixels_per_point(), dpi);
        }
    }
}

#[test]
fn details_toggle_and_config_shortcut_work() {
    let mut h = Harness::new(1240.0, 780.0, 1.25);
    h.named("显示或隐藏详情");
    assert!(!h.app.details_open);
    h.named("显示或隐藏详情");
    assert!(h.app.details_open);
    h.named("编辑配置");
    assert!(h.app.page == Page::Settings);
}

#[test]
fn themes_switch_through_menu_and_persist() {
    let mut h = Harness::new(1240.0, 780.0, 1.25);
    for label in ["浅色", "黑白灰", "深灰蓝"] {
        h.named("界面主题");
        h.named(label);
        assert_eq!(h.app.theme.label(), label);
        assert_eq!(Theme::load(&h.app.root).label(), label);
        assert_eq!(h.ctx.style().visuals.dark_mode, label != "浅色");
    }
}

#[test]
fn small_settings_footer_saves_only_isolated_configuration() {
    let mut h = Harness::new(900.0, 580.0, 1.25);
    h.rail("配置");
    let save = h.target("保存配置");
    assert!(save.y < 580.0 && save.y > 400.0);
    h.click(save);
    assert!(h.app.notice.starts_with("已保存"), "{}", h.app.notice);
    let saved = config::Config::load(&h.app.config_path).unwrap();
    assert!(saved.archives.starts_with(h._root.path()));
    let folders = ["浏览地图文件", "浏览客户端入口", "浏览云脚本目录"].map(|n| h.target(n));
    assert!((folders[0].x - folders[1].x).abs() < 0.1);
    assert!((folders[1].x - folders[2].x).abs() < 0.1);
}

#[test]
fn log_selection_filter_follow_and_clear_work() {
    let mut h = Harness::new(1240.0, 780.0, 1.25);
    h.app
        .log("[云脚本 0] alpha\n[系统] beta\nerror gamma".into());
    h.frame(vec![]);
    h.named("[云脚本 0] alpha");
    assert_eq!(h.app.selected_log.as_deref(), Some("[云脚本 0] alpha"));
    h.named("跟随");
    assert!(!h.app.follow);
    h.named("错误与异常");
    assert!(h.app.log_scope == views::LogScope::Errors);
    assert!(h.rects("[系统] beta").is_empty());
    assert!(!h.rects("error gamma").is_empty());
    h.named("全部日志");
    assert!(h.app.log_scope == views::LogScope::All);
    // 通过显式无障碍名称定位搜索框，输入文本后检查实际过滤结果。
    h.named("搜索日志");
    h.frame(vec![Event::Text("beta".into())]);
    assert_eq!(h.app.filter, "beta");
    assert!(!h.rects("[系统] beta").is_empty());
    assert!(h.rects("error gamma").is_empty());
    h.named("清空显示");
    assert!(h.app.logs.is_empty() && h.app.selected_log.is_none());
}

#[test]
fn vector_button_mouse_keyboard_and_disabled_state() {
    let ctx = egui::Context::default();
    let mut icons = icons::Icons::default();
    let mut count = 0;
    let mut enabled = true;
    let mut rect = Rect::NOTHING;
    let mut run = |events: Vec<Event>, enabled: bool| {
        let input = egui::RawInput {
            screen_rect: Some(Rect::from_min_size(Pos2::ZERO, Vec2::new(400.0, 200.0))),
            events,
            focused: true,
            ..Default::default()
        };
        let _ = ctx.run(input, |ctx| {
            egui::CentralPanel::default().show(ctx, |ui| {
                let response = icons.action(ui, icons::Icon::Check, "Action", enabled);
                rect = response.rect;
                if response.clicked() {
                    count += 1;
                }
            });
        });
        (rect, count)
    };
    run(vec![], enabled);
    let (rect, _) = run(vec![], enabled);
    let point = rect.center();
    run(vec![Event::PointerMoved(point)], enabled);
    for pressed in [true, false] {
        run(
            vec![Event::PointerButton {
                pos: point,
                button: PointerButton::Primary,
                pressed,
                modifiers: Modifiers::NONE,
            }],
            enabled,
        );
    }
    assert_eq!(run(vec![], enabled).1, 1);
    // 鼠标点击并不等同于键盘焦点，先通过 Tab 聚焦再测试空格激活。
    for pressed in [true, false] {
        run(
            vec![Event::Key {
                key: Key::Tab,
                physical_key: Some(Key::Tab),
                pressed,
                repeat: false,
                modifiers: Modifiers::NONE,
            }],
            enabled,
        );
    }
    assert!(ctx.memory(|m| m.focused()).is_some());
    for pressed in [true, false] {
        run(
            vec![Event::Key {
                key: Key::Space,
                physical_key: Some(Key::Space),
                pressed,
                repeat: false,
                modifiers: Modifiers::NONE,
            }],
            enabled,
        );
    }
    assert_eq!(run(vec![], enabled).1, 2);
    enabled = false;
    run(vec![], enabled);
    for pressed in [true, false] {
        run(
            vec![Event::PointerButton {
                pos: point,
                button: PointerButton::Primary,
                pressed,
                modifiers: Modifiers::NONE,
            }],
            enabled,
        );
    }
    assert_eq!(run(vec![], enabled).1, 2);
}

#[test]
fn window_controls_emit_commands_without_touching_desktop() {
    let mut h = Harness::new(1240.0, 780.0, 1.25);
    h.named("最大化");
    assert!(h
        .commands
        .iter()
        .any(|c| matches!(c, egui::ViewportCommand::Maximized(true))));
    h.named("最小化");
    assert!(h
        .commands
        .iter()
        .any(|c| matches!(c, egui::ViewportCommand::Minimized(true))));
    h.named("关闭窗口");
    assert!(h
        .commands
        .iter()
        .any(|c| matches!(c, egui::ViewportCommand::Close)));
}

#[test]
fn keyboard_tab_moves_focus() {
    let mut h = Harness::new(1240.0, 780.0, 1.25);
    h.key(Key::Tab);
    let first = h.ctx.memory(|m| m.focused()).expect("first focus");
    h.key(Key::Tab);
    let second = h.ctx.memory(|m| m.focused()).expect("second focus");
    assert_ne!(first, second);
}
