use super::{
    chrome,
    icons::Icon,
    theme::{Palette, Theme, SMALL_TEXT_SIZE},
    App, Page,
};
use eframe::egui::{self, Align, Color32, Frame, Layout, Margin, RichText, Stroke};
use std::path::Path;

#[derive(Clone, Copy, PartialEq)]
pub enum LogScope {
    All,
    Cloud,
    Errors,
}
fn is_error(line: &str) -> bool {
    let lower = line.to_lowercase();
    line.contains("故障") || lower.contains("error") || lower.contains("failed")
}
fn section(ui: &mut egui::Ui, label: &str, p: Palette) {
    ui.add_space(18.0);
    ui.label(RichText::new(label).size(SMALL_TEXT_SIZE).color(p.muted));
    ui.add_space(5.0);
}
fn panel(fill: Color32) -> Frame {
    Frame::none()
        .fill(fill)
        .inner_margin(Margin::symmetric(16.0, 12.0))
}

impl App {
    pub(super) fn shell(&mut self, ctx: &egui::Context) {
        let p = self.theme.palette();
        self.header(ctx, p);
        egui::SidePanel::left("icon_rail")
            .exact_width(48.0)
            .resizable(false)
            .frame(
                Frame::none()
                    .fill(p.rail)
                    .inner_margin(Margin::symmetric(7.0, 14.0)),
            )
            .show(ctx, |ui| {
                // 图标按钮含内边距的总宽度需落在 34 像素内，避免面板裁剪留下黑缝。
                ui.spacing_mut().button_padding.x = 4.0;
                for (page, icon, label) in [
                    (Page::Console, Icon::Console, "云脚本控制台"),
                    (Page::Settings, Icon::Settings, "配置"),
                    (Page::Archives, Icon::Archive, "玩家存档"),
                ] {
                    let tint = if self.theme == Theme::Light {
                        p.muted
                    } else {
                        Color32::from_rgb(193, 209, 234)
                    };
                    let image = self.icons.image(ui.ctx(), icon, 20.0, tint);
                    let fill = if self.page == page {
                        if self.theme == Theme::Light {
                            p.selected
                        } else {
                            Color32::from_white_alpha(28)
                        }
                    } else {
                        Color32::TRANSPARENT
                    };
                    let response = ui
                        .add(
                            image
                                .button("")
                                .min_size(egui::vec2(34.0, 36.0))
                                .fill(fill)
                                .stroke(Stroke::NONE),
                        )
                        .on_hover_text(label);
                    response.widget_info(|| {
                        egui::WidgetInfo::labeled(egui::WidgetType::Button, ui.is_enabled(), label)
                    });
                    if response.clicked() {
                        self.page = page;
                    }
                    ui.add_space(7.0);
                }
            });
        egui::SidePanel::left("navigation")
            .exact_width(164.0)
            .resizable(false)
            .frame(panel(p.panel))
            .show(ctx, |ui| self.navigation(ui, p));
        egui::TopBottomPanel::bottom("status")
            .exact_height(28.0)
            .frame(
                Frame::none()
                    .fill(p.panel)
                    .inner_margin(Margin::symmetric(14.0, 4.0)),
            )
            .show(ctx, |ui| {
                ui.horizontal(|ui| {
                    if self.worker.is_some() {
                        ui.add(egui::Spinner::new().size(12.0));
                    }
                    ui.add(
                        egui::Label::new(
                            RichText::new(&self.notice)
                                .size(SMALL_TEXT_SIZE)
                                .color(p.muted),
                        )
                        .truncate(),
                    )
                    .on_hover_text(&self.notice);
                    ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                        ui.label(
                            RichText::new(format!("{} 条记录", self.logs.len()))
                                .size(SMALL_TEXT_SIZE)
                                .color(p.muted),
                        );
                    });
                });
            });
        // 小窗口优先保留主工作区，宽度足够后恢复详情栏。
        if self.details_open && ctx.screen_rect().width() >= 1080.0 {
            egui::SidePanel::right("inspector")
                .exact_width(256.0)
                .resizable(false)
                .frame(panel(p.panel))
                .show(ctx, |ui| self.inspector(ui, p));
        }
        egui::CentralPanel::default()
            .frame(Frame::none().fill(p.canvas).inner_margin(0.0))
            .show(ctx, |ui| {
                self.actions(ui, p);
                Frame::none()
                    .inner_margin(Margin::symmetric(20.0, 16.0))
                    .show(ui, |ui| match self.page {
                        Page::Console => self.console(ui, p),
                        Page::Settings => self.settings(ui),
                        Page::Archives => {
                            egui::ScrollArea::vertical().show(ui, |ui| self.archives(ui, p));
                        }
                    });
            });
    }
    fn header(&mut self, ctx: &egui::Context, p: Palette) {
        egui::TopBottomPanel::top("title")
            .exact_height(48.0)
            .frame(
                Frame::none()
                    .fill(p.panel)
                    .inner_margin(Margin::symmetric(14.0, 8.0)),
            )
            .show(ctx, |ui| {
                ui.horizontal(|ui| {
                    let start = ui.cursor().min;
                    ui.add(self.icons.image(ui.ctx(), Icon::Layers, 21.0, p.accent));
                    ui.label(RichText::new("ScriptRoom").size(15.0).strong());
                    let end = ui.cursor().min;
                    chrome::drag(
                        ui,
                        "brand_drag",
                        egui::Rect::from_min_max(start, egui::pos2(end.x + 22.0, start.y + 30.0)),
                    );
                    ui.add_space(38.0);
                    Frame::none()
                        .fill(p.canvas)
                        .stroke(Stroke::new(1.0_f32, p.border))
                        .rounding(4.0)
                        .inner_margin(Margin::symmetric(8.0, 1.0))
                        .show(ui, |ui| {
                            ui.horizontal(|ui| {
                                ui.add(self.icons.image(ui.ctx(), Icon::Search, 14.0, p.muted));
                                let search = ui.add(
                                    egui::TextEdit::singleline(&mut self.filter)
                                        .hint_text("搜索日志")
                                        .frame(false)
                                        .desired_width(210.0),
                                );
                                ui.ctx().accesskit_node_builder(search.id, |node| {
                                    node.set_name("搜索日志")
                                });
                            });
                        });
                    ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                        chrome::controls(ui, &mut self.icons);
                        ui.separator();
                        if self
                            .icons
                            .button(ui, Icon::Inspector, "显示或隐藏详情")
                            .clicked()
                        {
                            self.details_open = !self.details_open;
                        }
                        let previous = self.theme;
                        let theme_menu = egui::ComboBox::from_id_salt("theme")
                            .selected_text(self.theme.label())
                            .width(90.0)
                            .show_ui(ui, |ui| {
                                for theme in [Theme::Shadowworks, Theme::Monochrome, Theme::Light] {
                                    ui.selectable_value(&mut self.theme, theme, theme.label());
                                }
                            });
                        ui.ctx()
                            .accesskit_node_builder(theme_menu.response.id, |node| {
                                node.set_name("界面主题")
                            });
                        if previous != self.theme {
                            self.theme.apply(ctx);
                            if let Err(error) = self.theme.save(&self.root) {
                                self.notice = error.to_string();
                            }
                        }
                        let spare = ui.available_rect_before_wrap();
                        if spare.width() > 8.0 {
                            chrome::drag(ui, "spare_drag", spare);
                        }
                    });
                });
            });
    }
    fn navigation(&mut self, ui: &mut egui::Ui, p: Palette) {
        ui.add_space(2.0);
        ui.label(RichText::new("工作区").strong().size(SMALL_TEXT_SIZE));
        section(ui, "控制台", p);
        for (scope, label) in [
            (LogScope::All, "全部日志"),
            (LogScope::Cloud, "云脚本"),
            (LogScope::Errors, "错误与异常"),
        ] {
            let selected = self.page == Page::Console && self.log_scope == scope;
            let count = self
                .logs
                .iter()
                .filter(|line| match scope {
                    LogScope::All => true,
                    LogScope::Cloud => line.contains("[云脚本"),
                    LogScope::Errors => is_error(line),
                })
                .count();
            let response = ui.add(
                egui::Button::new(label)
                    .shortcut_text(count.to_string())
                    .min_size(egui::vec2(132.0, 30.0))
                    .fill(if selected {
                        p.selected
                    } else {
                        Color32::TRANSPARENT
                    })
                    .stroke(Stroke::NONE),
            );
            if response.clicked() {
                self.page = Page::Console;
                self.log_scope = scope;
            }
        }
        section(ui, "项目", p);
        for (page, icon, label) in [
            (Page::Settings, Icon::Settings, "路径与配置"),
            (Page::Archives, Icon::Archive, "玩家存档"),
        ] {
            let image = self.icons.image(ui.ctx(), icon, 15.0, p.muted);
            if ui
                .add_sized(
                    [132.0, 30.0],
                    image
                        .button(label)
                        .fill(if self.page == page {
                            p.selected
                        } else {
                            Color32::TRANSPARENT
                        })
                        .stroke(Stroke::NONE),
                )
                .clicked()
            {
                self.page = page;
            }
        }
        section(ui, "当前配置", p);
        ui.add(self.icons.image(ui.ctx(), Icon::File, 17.0, p.muted));
        ui.add(
            egui::Label::new(
                RichText::new(
                    self.config_path
                        .file_name()
                        .unwrap_or_default()
                        .to_string_lossy(),
                )
                .size(SMALL_TEXT_SIZE)
                .color(p.muted),
            )
            .wrap(),
        );
        ui.with_layout(Layout::bottom_up(Align::LEFT), |ui| {
            ui.label(
                RichText::new("本地工作区")
                    .size(SMALL_TEXT_SIZE)
                    .color(p.muted),
            );
        });
    }
    fn actions(&mut self, ui: &mut egui::Ui, p: Palette) {
        Frame::none()
            .fill(p.panel)
            .inner_margin(Margin::symmetric(16.0, 9.0))
            .show(ui, |ui| {
                ui.set_min_width(ui.available_width());
                ui.horizontal(|ui| {
                    let image = self.icons.image(
                        ui.ctx(),
                        Icon::Play,
                        15.0,
                        if self.theme == Theme::Monochrome {
                            Color32::BLACK
                        } else {
                            Color32::WHITE
                        },
                    );
                    let button = image
                        .button(
                            RichText::new("启动").color(if self.theme == Theme::Monochrome {
                                Color32::BLACK
                            } else {
                                Color32::WHITE
                            }),
                        )
                        .fill(p.accent)
                        .stroke(Stroke::NONE);
                    if ui.add_enabled(self.worker.is_none(), button).clicked() {
                        self.start(false);
                    }
                    if self
                        .icons
                        .action(ui, Icon::Check, "检查", self.worker.is_none())
                        .on_hover_text("检查配置及运行组件")
                        .clicked()
                    {
                        self.start(true);
                    }
                    if self
                        .icons
                        .action(ui, Icon::Stop, "停止", self.worker.is_some())
                        .on_hover_text("停止云服务")
                        .clicked()
                    {
                        if let Some(worker) = &self.worker {
                            worker.stop();
                            self.notice = "正在停止云服务…".into();
                        }
                    }
                    ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                        if self
                            .icons
                            .action(ui, Icon::Game, "关闭全部魔兽", !self.game_close.busy())
                            .clicked()
                        {
                            self.close_games();
                        }
                    });
                });
            });
        ui.separator();
    }
    fn console(&mut self, ui: &mut egui::Ui, p: Palette) {
        ui.horizontal(|ui| {
            ui.label(RichText::new("云脚本").size(15.0).strong());
            ui.label(RichText::new("日志").size(SMALL_TEXT_SIZE).color(p.muted));
            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                if self.icons.button(ui, Icon::Trash, "清空显示").clicked() {
                    self.logs.clear();
                    self.selected_log = None;
                }
                ui.checkbox(&mut self.follow, "跟随");
            });
        });
        ui.add_space(12.0);
        if self.logs.is_empty() {
            let height = (ui.available_height() * 0.25).max(40.0);
            ui.add_space(height);
            ui.vertical_centered(|ui| {
                ui.add(self.icons.image(ui.ctx(), Icon::Console, 40.0, p.muted));
                ui.add_space(16.0);
                ui.label(RichText::new("控制台已就绪").size(16.0));
                ui.add_space(4.0);
                ui.label(
                    RichText::new("启动房间后，云脚本输出会显示在这里")
                        .size(SMALL_TEXT_SIZE)
                        .color(p.muted),
                );
                ui.add_space(18.0);
                if self
                    .icons
                    .action(ui, Icon::Settings, "检查路径配置", true)
                    .clicked()
                {
                    self.page = Page::Settings;
                }
            });
            return;
        }
        let lines: Vec<_> = self
            .logs
            .iter()
            .enumerate()
            .filter(|(_, line)| {
                (self.filter.is_empty()
                    || line.to_lowercase().contains(&self.filter.to_lowercase()))
                    && match self.log_scope {
                        LogScope::All => true,
                        LogScope::Cloud => line.contains("[云脚本"),
                        LogScope::Errors => is_error(line),
                    }
            })
            .collect();
        ui.horizontal(|ui| {
            ui.add_sized(
                [34.0, 20.0],
                egui::Label::new(RichText::new("#").small().color(p.muted)),
            );
            ui.add_sized(
                [48.0, 20.0],
                egui::Label::new(RichText::new("来源").small().color(p.muted)),
            );
            ui.label(RichText::new("内容").small().color(p.muted));
        });
        ui.separator();
        egui::ScrollArea::vertical()
            .auto_shrink([false, false])
            .stick_to_bottom(self.follow)
            .show_rows(ui, 27.0, lines.len(), |ui, range| {
                for index in range {
                    let (number, line) = lines[index];
                    let selected = self.selected_log.as_ref() == Some(line);
                    let fill = if selected {
                        p.selected
                    } else if index % 2 == 0 {
                        p.canvas
                    } else {
                        p.panel
                    };
                    Frame::none()
                        .fill(fill)
                        .inner_margin(Margin::symmetric(6.0, 3.0))
                        .show(ui, |ui| {
                            ui.spacing_mut().interact_size.y = 21.0;
                            ui.set_min_width(ui.available_width());
                            ui.horizontal(|ui| {
                                ui.add_sized(
                                    [28.0, 21.0],
                                    egui::Label::new(
                                        RichText::new(format!("{:03}", number + 1))
                                            .monospace()
                                            .size(SMALL_TEXT_SIZE)
                                            .color(p.muted),
                                    ),
                                );
                                let source = if is_error(line) {
                                    "错误"
                                } else if line.contains("[云脚本") {
                                    "脚本"
                                } else {
                                    "系统"
                                };
                                ui.add_sized(
                                    [42.0, 21.0],
                                    egui::Label::new(
                                        RichText::new(source).size(SMALL_TEXT_SIZE).color(
                                            if is_error(line) {
                                                Color32::from_rgb(212, 100, 100)
                                            } else {
                                                p.muted
                                            },
                                        ),
                                    ),
                                );
                                if ui
                                    .add(
                                        egui::Label::new(RichText::new(line).monospace())
                                            .truncate()
                                            .sense(egui::Sense::click()),
                                    )
                                    .on_hover_text(line)
                                    .clicked()
                                {
                                    self.selected_log = Some(line.clone());
                                    self.details_open = true;
                                }
                            });
                        });
                }
            });
    }
    fn inspector(&mut self, ui: &mut egui::Ui, p: Palette) {
        ui.horizontal(|ui| {
            ui.label(RichText::new("详情").strong().size(SMALL_TEXT_SIZE));
            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                if self.icons.button(ui, Icon::Inspector, "收起详情").clicked() {
                    self.details_open = false;
                }
            });
        });
        ui.add_space(14.0);
        Frame::none()
            .fill(p.canvas)
            .stroke(Stroke::new(1.0_f32, p.border))
            .rounding(5.0)
            .inner_margin(16.0)
            .show(ui, |ui| {
                ui.set_min_width(ui.available_width());
                ui.horizontal(|ui| {
                    ui.add(self.icons.image(ui.ctx(), Icon::Script, 24.0, p.accent));
                    ui.vertical(|ui| {
                        ui.label(RichText::new("云脚本房间").strong());
                        ui.label(
                            RichText::new(if self.worker.is_some() {
                                "任务执行中"
                            } else {
                                "未启动"
                            })
                            .size(SMALL_TEXT_SIZE)
                            .color(p.muted),
                        );
                    });
                });
            });
        // 长路径和较大字号只滚动详情内容，配置入口始终保留在底部。
        egui::TopBottomPanel::bottom("inspector_actions")
            .frame(Frame::none().inner_margin(Margin::symmetric(0.0, 8.0)))
            .show_inside(ui, |ui| {
                if self
                    .icons
                    .action(ui, Icon::Settings, "编辑配置", true)
                    .clicked()
                {
                    self.page = Page::Settings;
                }
            });
        egui::ScrollArea::vertical().show(ui, |ui| {
            if let Some(line) = self.selected_log.clone() {
                section(ui, "选中日志", p);
                ui.add(
                    egui::Label::new(RichText::new(&line).monospace())
                        .wrap()
                        .selectable(true),
                );
                if ui.small_button("复制内容").clicked() {
                    ui.ctx().copy_text(line);
                }
            }
            if let Ok(config) = self.draft.config(&self.config_path) {
                section(ui, "运行路径", p);
                for (label, path) in [
                    ("地图", &config.map),
                    ("云脚本", &config.cloud_scripts),
                    ("客户端入口", &config.local_entry),
                ] {
                    self.path_detail(ui, label, path, p);
                }
                section(ui, "存档", p);
                self.path_detail(ui, "目录", &config.archives, p);
                ui.label(
                    RichText::new("玩家 0 / 玩家 1 · 独立 INI")
                        .size(SMALL_TEXT_SIZE)
                        .color(p.muted),
                );
                section(ui, "运行设置", p);
                ui.horizontal(|ui| {
                    ui.label(RichText::new("服务时长").color(p.muted));
                    ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                        ui.label(format!("{} 分钟", config.seconds / 60));
                    });
                });
                ui.horizontal(|ui| {
                    ui.label(RichText::new("启动超时").color(p.muted));
                    ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                        ui.label(format!("{} 秒", config.startup_timeout));
                    });
                });
            }
        });
    }
    fn path_detail(&mut self, ui: &mut egui::Ui, label: &str, path: &Path, p: Palette) {
        ui.label(RichText::new(label).size(SMALL_TEXT_SIZE).color(p.muted));
        ui.add(
            egui::Label::new(RichText::new(path.to_string_lossy()).size(SMALL_TEXT_SIZE))
                .wrap()
                .selectable(true),
        );
        ui.add_space(8.0);
    }
    fn archives(&mut self, ui: &mut egui::Ui, p: Palette) {
        ui.label(RichText::new("玩家存档").size(16.0).strong());
        ui.add_space(5.0);
        ui.label(
            RichText::new("共用一套魔兽，每位玩家独立保存")
                .size(SMALL_TEXT_SIZE)
                .color(p.muted),
        );
        ui.add_space(20.0);
        if let Ok(config) = self.draft.config(&self.config_path) {
            for player in 0..2 {
                Frame::none()
                    .fill(p.panel)
                    .stroke(Stroke::new(1.0_f32, p.border))
                    .rounding(5.0)
                    .inner_margin(18.0)
                    .show(ui, |ui| {
                        ui.set_min_width(ui.available_width());
                        ui.horizontal(|ui| {
                            ui.add(self.icons.image(ui.ctx(), Icon::Archive, 22.0, p.accent));
                            ui.label(RichText::new(format!("玩家 {player}")).strong());
                        });
                        ui.add_space(14.0);
                        self.path_detail(
                            ui,
                            "客户端 INI",
                            &config.archives.join(format!("clients/player-{player}.ini")),
                            p,
                        );
                        self.path_detail(
                            ui,
                            "云端存档",
                            &config.archives.join(format!("slot-{player}.archive")),
                            p,
                        );
                    });
                ui.add_space(12.0);
            }
            if self
                .icons
                .action(ui, Icon::Folder, "打开存档目录", true)
                .clicked()
            {
                if let Err(error) = std::process::Command::new("explorer.exe")
                    .arg(&config.archives)
                    .spawn()
                {
                    self.notice = error.to_string();
                }
            }
        }
    }
}
