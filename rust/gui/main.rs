#![windows_subsystem = "windows"]
mod chrome;
mod game_close;
mod icons;
mod settings;
mod snapshot;
#[cfg(test)]
mod tests;
mod theme;
mod views;
mod worker;
use eframe::egui;
use game_close::GameClose;
use scriptroom::config;
use settings::Draft;
use std::{collections::VecDeque, path::PathBuf, time::Duration};
use theme::Theme;
use worker::Worker;

#[derive(Clone, Copy, PartialEq)]
enum Page {
    Console,
    Settings,
    Archives,
}
struct App {
    icons: icons::Icons,
    details_open: bool,
    log_scope: views::LogScope,
    selected_log: Option<String>,
    snapshot: Option<snapshot::Snapshot>,
    root: PathBuf,
    config_path: PathBuf,
    draft: Draft,
    theme: Theme,
    page: Page,
    worker: Option<Worker>,
    logs: VecDeque<String>,
    notice: String,
    filter: String,
    follow: bool,
    game_close: GameClose,
    confirm_exit: bool,
    exit_after_stop: bool,
}
impl App {
    fn new(cc: &eframe::CreationContext<'_>, root: PathBuf) -> Self {
        let mut app = Self::create(&cc.egui_ctx, root, snapshot::Snapshot::from_args());
        // 显式启动参数复用按钮入口；截图模式不启动游戏。
        if app.snapshot.is_none() && std::env::args_os().any(|arg| arg == "--start") {
            app.start(false);
        }
        app
    }
    fn create(ctx: &egui::Context, root: PathBuf, snapshot: Option<snapshot::Snapshot>) -> Self {
        theme::fonts(ctx);
        let theme = snapshot
            .as_ref()
            .map(|s| s.theme)
            .unwrap_or_else(|| Theme::load(&root));
        theme.apply(ctx);
        let config_path = config::default_config(&root);
        let (draft, notice) = match config::Config::load(&config_path) {
            Ok(config) => (Draft::from(&config), "就绪".into()),
            Err(error) => (Draft::default(), format!("配置未载入：{error:#}")),
        };
        let mut app = Self {
            icons: icons::Icons::default(),
            details_open: true,
            log_scope: views::LogScope::All,
            selected_log: None,
            snapshot,
            root,
            config_path,
            draft,
            theme,
            page: Page::Console,
            worker: None,
            logs: VecDeque::new(),
            notice,
            filter: String::new(),
            follow: true,
            game_close: GameClose::Idle,
            confirm_exit: false,
            exit_after_stop: false,
        };
        if let Some(snapshot) = &app.snapshot {
            app.page = snapshot.page;
            if let Some(path) = &snapshot.log {
                if let Ok(text) = std::fs::read_to_string(path) {
                    app.log(text);
                    app.notice = "历史日志预览".into();
                }
            }
        }
        app
    }
    fn log(&mut self, line: String) {
        for line in line.lines() {
            self.logs.push_back(line.into());
        }
        while self.logs.len() > 5000 {
            self.logs.pop_front();
        }
    }
    fn poll(&mut self, ctx: &egui::Context) {
        if let Some(worker) = &mut self.worker {
            let mut lines = Vec::new();
            while let Ok(line) = worker.logs.try_recv() {
                lines.push(line);
            }
            let completed = worker.completed();
            for line in lines {
                self.log(line);
            }
            if let Some(result) = completed {
                self.worker = None;
                self.notice = match result {
                    Ok(()) => "已结束".into(),
                    Err(error) => format!("故障：{error}"),
                };
                self.log(self.notice.clone());
                if self.exit_after_stop {
                    ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                }
            }
        }
        for message in self.game_close.poll() {
            self.notice = message.clone();
            self.log(message);
        }
    }
    fn start(&mut self, check: bool) {
        match self.draft.config(&self.config_path) {
            Ok(config) => {
                self.worker = Some(Worker::start(self.root.clone(), config, check));
                self.notice = if check {
                    "检查配置中"
                } else {
                    "启动中"
                }
                .into();
                self.page = Page::Console;
            }
            Err(error) => {
                self.notice = error.to_string();
                self.page = Page::Settings;
            }
        }
    }
    fn close_games(&mut self) {
        match self.game_close.request() {
            Ok(messages) => {
                for message in messages {
                    self.log(message.clone());
                    self.notice = message;
                }
                if self.game_close.busy() {
                    self.notice = "已请求正常关闭全部魔兽，等待退出…".into();
                }
            }
            Err(error) => self.notice = error.to_string(),
        }
    }
    fn settings(&mut self, ui: &mut egui::Ui) {
        egui::TopBottomPanel::bottom("configuration_actions")
            .frame(egui::Frame::none().inner_margin(egui::Margin::symmetric(0.0, 8.0)))
            .show_inside(ui, |ui| {
                ui.horizontal(|ui| {
                    if ui.button("保存配置").clicked() {
                        self.notice = match self
                            .draft
                            .config(&self.config_path)
                            .and_then(|config| config.save(&self.config_path))
                        {
                            Ok(()) => format!("已保存 {}", self.config_path.display()),
                            Err(error) => error.to_string(),
                        };
                    }
                    if ui.button("载入配置…").clicked() {
                        if let Some(path) = rfd::FileDialog::new()
                            .add_filter("JSON", &["json"])
                            .pick_file()
                        {
                            match config::Config::load(&path) {
                                Ok(config) => {
                                    self.config_path = path;
                                    self.draft = Draft::from(&config);
                                    self.notice = "已载入配置".into();
                                }
                                Err(error) => self.notice = error.to_string(),
                            }
                        }
                    }
                    if ui.button("另存为…").clicked() {
                        if let Some(path) = rfd::FileDialog::new()
                            .add_filter("JSON", &["json"])
                            .set_file_name("本地测试配置.json")
                            .save_file()
                        {
                            match self
                                .draft
                                .config(&self.config_path)
                                .and_then(|config| config.save(&path))
                            {
                                Ok(()) => {
                                    self.config_path = path;
                                    self.notice = "已保存配置".into();
                                }
                                Err(error) => self.notice = error.to_string(),
                            }
                        }
                    }
                });
                ui.add_space(8.0);
                ui.add(
                    egui::Label::new(
                        egui::RichText::new(self.config_path.to_string_lossy()).small(),
                    )
                    .truncate(),
                );
            });
        egui::ScrollArea::vertical().show(ui, |ui| {
            self.draft.draw(ui, self.theme.palette(), &mut self.icons);
        });
    }
    fn dialogs(&mut self, ctx: &egui::Context) {
        if let GameClose::Confirm(targets) = &self.game_close {
            let pids = targets
                .iter()
                .map(|target| target.pid.to_string())
                .collect::<Vec<_>>();
            egui::Window::new("仍有魔兽未退出")
                .collapsible(false)
                .resizable(false)
                .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
                .show(ctx, |ui| {
                    ui.label(format!(
                        "准备强制结束 {} 个进程。未保存的进度会丢失。",
                        pids.len()
                    ));
                    ui.label(pids.join("、"));
                    ui.horizontal(|ui| {
                        if ui.button("取消").clicked() {
                            self.game_close.cancel();
                        }
                        if ui.button("强制结束").clicked() {
                            self.notice = match self.game_close.terminate() {
                                Ok(()) => "正在后台结束魔兽，结果见控制台…".into(),
                                Err(error) => error.to_string(),
                            };
                        }
                    });
                });
        }
        if self.confirm_exit {
            egui::Window::new("退出 ScriptRoom")
                .collapsible(false)
                .resizable(false)
                .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
                .show(ctx, |ui| {
                    ui.label("云服务正在运行，退出会中断本轮云脚本。");
                    ui.horizontal(|ui| {
                        if ui.button("继续运行").clicked() {
                            self.confirm_exit = false;
                        }
                        if ui.button("停止并退出").clicked() {
                            self.confirm_exit = false;
                            self.exit_after_stop = true;
                            if let Some(worker) = &self.worker {
                                worker.stop();
                            }
                        }
                    });
                });
        }
    }
}
impl eframe::App for App {
    fn update(&mut self, ctx: &egui::Context, _: &mut eframe::Frame) {
        self.poll(ctx);
        ctx.request_repaint_after(Duration::from_millis(150));
        if ctx.input(|input| input.viewport().close_requested()) && self.worker.is_some() {
            ctx.send_viewport_cmd(egui::ViewportCommand::CancelClose);
            self.confirm_exit = true;
        }
        self.shell(ctx);
        self.dialogs(ctx);
        chrome::edges(ctx, self.theme.palette());
        if let Some(snapshot) = &mut self.snapshot {
            snapshot.update(ctx);
        }
    }
}
fn main() {
    let result = (|| -> anyhow::Result<()> {
        let root = config::root()?;
        let options = eframe::NativeOptions {
            viewport: egui::ViewportBuilder::default()
                .with_inner_size(snapshot::window_size())
                .with_min_inner_size([900.0, 580.0])
                .with_decorations(false),
            ..Default::default()
        };
        eframe::run_native(
            "ScriptRoom",
            options,
            Box::new(move |cc| Ok(Box::new(App::new(cc, root)))),
        )
        .map_err(|error| anyhow::anyhow!(error.to_string()))
    })();
    if let Err(error) = result {
        rfd::MessageDialog::new()
            .set_title("ScriptRoom 启动失败")
            .set_description(format!("{error:#}"))
            .set_level(rfd::MessageLevel::Error)
            .show();
    }
}
