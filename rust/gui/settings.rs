use super::theme::SMALL_TEXT_SIZE;
use eframe::egui::{self, Ui};
use scriptroom::config::Config;
use std::path::{Path, PathBuf};

#[derive(Default)]
pub struct Draft {
    paths: [String; 7],
    seconds: String,
    timeout: String,
}
impl From<&Config> for Draft {
    fn from(config: &Config) -> Self {
        Self {
            paths: [
                &config.map,
                &config.local_entry,
                &config.cloud_scripts,
                &config.archives,
                &config.editor,
                &config.source_game,
                &config.game,
            ]
            .map(|p| p.to_string_lossy().into_owned()),
            seconds: config.seconds.to_string(),
            timeout: config.startup_timeout.to_string(),
        }
    }
}
impl Draft {
    pub fn config(&self, config_path: &Path) -> anyhow::Result<Config> {
        anyhow::ensure!(
            self.paths.iter().all(|p| !p.trim().is_empty()),
            "请补全地图、脚本、存档和运行环境路径"
        );
        let paths = self.paths.clone().map(PathBuf::from);
        let [map, local_entry, cloud_scripts, archives, editor, source_game, game] = paths;
        let mut config = Config {
            map,
            local_entry,
            cloud_scripts,
            archives,
            editor,
            source_game,
            game,
            seconds: self.seconds.parse()?,
            startup_timeout: self.timeout.parse()?,
        };
        config.validate_numbers()?;
        let absolute = std::path::absolute(config_path)?;
        config.resolve_paths(
            absolute
                .parent()
                .ok_or_else(|| anyhow::anyhow!("无配置目录"))?,
        )?;
        Ok(config)
    }
    pub fn draw(&mut self, ui: &mut Ui, p: super::theme::Palette, icons: &mut super::icons::Icons) {
        ui.label(egui::RichText::new("路径与配置").size(16.0).strong());
        ui.add_space(5.0);
        ui.label(
            egui::RichText::new("地图、脚本与存档的运行位置")
                .size(SMALL_TEXT_SIZE)
                .color(p.muted),
        );
        ui.add_space(22.0);
        self.group(ui, p, "地图与脚本", |this, ui| {
            this.path(ui, icons, 0, "地图文件", false);
            this.path(ui, icons, 1, "客户端入口", false);
            this.path(ui, icons, 2, "云脚本目录", true);
        });
        ui.add_space(16.0);
        self.group(ui, p, "存档", |this, ui| {
            this.path(ui, icons, 3, "存档目录", true);
            ui.label(
                egui::RichText::new("按玩家分别保存，重新启动后继续读取")
                    .size(SMALL_TEXT_SIZE)
                    .color(p.muted),
            );
        });
        ui.add_space(16.0);
        egui::CollapsingHeader::new("运行环境")
            .default_open(true)
            .show(ui, |ui| {
                self.path(ui, icons, 4, "KKWE 编辑器", true);
                self.path(ui, icons, 5, "原魔兽目录", true);
                self.path(ui, icons, 6, "共用测试魔兽", true);
            });
        ui.add_space(8.0);
        egui::CollapsingHeader::new("运行时限").show(ui, |ui| {
            ui.horizontal(|ui| {
                ui.label("服务时长（秒）");
                ui.add(egui::TextEdit::singleline(&mut self.seconds).desired_width(70.0));
                ui.label("启动超时（秒）");
                ui.add(egui::TextEdit::singleline(&mut self.timeout).desired_width(60.0));
            });
        });
    }
    fn group(
        &mut self,
        ui: &mut Ui,
        p: super::theme::Palette,
        label: &str,
        content: impl FnOnce(&mut Self, &mut Ui),
    ) {
        ui.label(egui::RichText::new(label).size(SMALL_TEXT_SIZE).strong());
        ui.add_space(7.0);
        egui::Frame::none()
            .fill(p.panel)
            .stroke(egui::Stroke::new(1.0_f32, p.border))
            .rounding(5.0)
            .inner_margin(14.0)
            .show(ui, |ui| {
                ui.set_min_width(ui.available_width());
                content(self, ui);
            });
    }
    fn path(
        &mut self,
        ui: &mut Ui,
        icons: &mut super::icons::Icons,
        index: usize,
        label: &str,
        directory: bool,
    ) {
        // 先确定整行的三个列边界，输入框内边距不参与下一行宽度计算。
        let (row, _) =
            ui.allocate_exact_size(egui::vec2(ui.available_width(), 32.0), egui::Sense::hover());
        let label_rect = egui::Rect::from_min_size(row.min, egui::vec2(84.0, row.height()));
        let browse_rect =
            egui::Rect::from_min_max(egui::pos2(row.right() - 36.0, row.top()), row.max);
        let input_rect = egui::Rect::from_min_max(
            egui::pos2(label_rect.right() + 8.0, row.top()),
            egui::pos2(browse_rect.left() - 8.0, row.bottom()),
        );
        ui.put(label_rect, egui::Label::new(label));
        ui.put(
            input_rect,
            egui::TextEdit::singleline(&mut self.paths[index])
                .desired_width(input_rect.width() - 16.0)
                .margin(egui::vec2(8.0, 6.0)),
        );
        let image = icons.image(
            ui.ctx(),
            super::icons::Icon::Folder,
            16.0,
            ui.visuals().text_color(),
        );
        let browse = ui
            .put(browse_rect, image.button("").frame(false))
            .on_hover_text("浏览路径");
        browse.widget_info(|| {
            egui::WidgetInfo::labeled(
                egui::WidgetType::Button,
                ui.is_enabled(),
                format!("浏览{label}"),
            )
        });
        if browse.clicked() {
            let dialog = rfd::FileDialog::new().set_title(label);
            let path = if directory {
                dialog.pick_folder()
            } else {
                dialog.pick_file()
            };
            if let Some(path) = path {
                self.paths[index] = path.to_string_lossy().into_owned();
            }
        }
    }
}
