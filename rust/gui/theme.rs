use eframe::egui::{self, Color32, FontData, FontDefinitions, FontFamily, FontId, TextStyle};
use serde::{Deserialize, Serialize};
use std::path::Path;

#[derive(Clone, Copy, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Theme {
    Monochrome,
    #[default]
    Shadowworks,
    Light,
}

#[derive(Clone, Copy)]
pub struct Palette {
    pub canvas: Color32,
    pub panel: Color32,
    pub raised: Color32,
    pub border: Color32,
    pub divider: Color32,
    pub text: Color32,
    pub muted: Color32,
    pub accent: Color32,
    pub selected: Color32,
    pub rail: Color32,
}
impl Theme {
    pub fn label(self) -> &'static str {
        match self {
            Self::Monochrome => "黑白灰",
            Self::Shadowworks => "深灰蓝",
            Self::Light => "浅色",
        }
    }
    pub fn palette(self) -> Palette {
        if self == Self::Light {
            return Palette {
                canvas: Color32::WHITE,
                panel: Color32::from_rgb(246, 247, 249),
                raised: Color32::from_rgb(238, 240, 243),
                border: Color32::from_rgb(222, 225, 230),
                divider: Color32::from_rgb(232, 234, 238),
                text: Color32::from_rgb(45, 50, 58),
                muted: Color32::from_rgb(117, 124, 135),
                accent: Color32::from_rgb(55, 112, 215),
                selected: Color32::from_rgb(224, 234, 251),
                rail: Color32::from_rgb(233, 237, 244),
            };
        }
        let blue = self == Self::Shadowworks;
        Palette {
            canvas: Color32::from_gray(32),
            panel: Color32::from_gray(38),
            raised: Color32::from_gray(45),
            border: Color32::from_gray(57),
            divider: Color32::from_gray(48),
            text: Color32::from_rgb(224, 227, 232),
            muted: Color32::from_rgb(163, 170, 181),
            accent: if blue {
                Color32::from_rgb(74, 139, 240)
            } else {
                Color32::from_gray(216)
            },
            selected: if blue {
                Color32::from_rgb(35, 57, 86)
            } else {
                Color32::from_gray(60)
            },
            rail: if blue {
                Color32::from_rgb(23, 64, 124)
            } else {
                Color32::from_gray(30)
            },
        }
    }
    pub fn apply(self, ctx: &egui::Context) {
        ctx.set_theme(if self == Self::Light {
            egui::Theme::Light
        } else {
            egui::Theme::Dark
        });
        let p = self.palette();
        let mut style = (*ctx.style()).clone();
        style.visuals = if self == Self::Light {
            egui::Visuals::light()
        } else {
            egui::Visuals::dark()
        };
        style.visuals.panel_fill = p.canvas;
        style.visuals.window_fill = p.panel;
        style.visuals.extreme_bg_color = p.canvas;
        style.visuals.override_text_color = Some(p.text);
        style.visuals.selection.bg_fill = p.selected;
        style.visuals.selection.stroke = egui::Stroke::new(1.0_f32, p.accent);
        for visuals in [
            &mut style.visuals.widgets.inactive,
            &mut style.visuals.widgets.hovered,
            &mut style.visuals.widgets.active,
        ] {
            visuals.rounding = egui::Rounding::same(4.0);
            visuals.bg_stroke = egui::Stroke::new(1.0_f32, p.border);
            visuals.fg_stroke = egui::Stroke::new(1.0_f32, p.text);
        }
        style.visuals.widgets.inactive.bg_fill = p.raised;
        style.visuals.widgets.inactive.weak_bg_fill = p.raised;
        style.visuals.widgets.hovered.bg_fill = p.selected;
        style.visuals.widgets.hovered.weak_bg_fill = p.selected;
        style.visuals.widgets.active.bg_fill = p.selected;
        style.visuals.widgets.active.weak_bg_fill = p.selected;
        style.visuals.widgets.noninteractive.bg_stroke = egui::Stroke::new(1.0_f32, p.divider);
        style.visuals.widgets.noninteractive.fg_stroke = egui::Stroke::new(1.0_f32, p.muted);
        style.visuals.window_rounding = egui::Rounding::same(6.0);
        style.spacing.item_spacing = egui::vec2(8.0, 8.0);
        style.spacing.button_padding = egui::vec2(10.0, 5.0);
        style.spacing.interact_size = egui::vec2(28.0, 28.0);
        style.spacing.scroll.bar_width = 5.0;
        style.spacing.scroll.floating = true;
        for (kind, size) in [
            (TextStyle::Body, 14.0),
            (TextStyle::Button, 14.0),
            (TextStyle::Small, 12.0),
            (TextStyle::Heading, 16.0),
        ] {
            style.text_styles.insert(kind, FontId::proportional(size));
        }
        style
            .text_styles
            .insert(TextStyle::Monospace, FontId::monospace(12.0));
        ctx.set_style(style);
    }
    pub fn load(root: &Path) -> Self {
        std::fs::read(root.join("界面设置.json"))
            .ok()
            .and_then(|bytes| serde_json::from_slice::<serde_json::Value>(&bytes).ok())
            .and_then(|value| serde_json::from_value(value["theme"].clone()).ok())
            .unwrap_or_default()
    }
    pub fn save(self, root: &Path) -> anyhow::Result<()> {
        scriptroom::archives::atomic_write(
            &root.join("界面设置.json"),
            &serde_json::to_vec_pretty(&serde_json::json!({"theme":self}))?,
        )
    }
}
pub fn fonts(ctx: &egui::Context) {
    let mut fonts = FontDefinitions::default();
    let windows = std::env::var_os("WINDIR").unwrap_or_else(|| "C:\\Windows".into());
    for (name, index) in [("msyh.ttc", 1), ("simhei.ttf", 0), ("simsun.ttc", 0)] {
        if let Ok(bytes) = std::fs::read(Path::new(&windows).join("Fonts").join(name)) {
            // 微软雅黑字体集的第 1 个字体为 UI 版本，具有适合控件的行高。
            let mut font = FontData::from_owned(bytes);
            font.index = index;
            fonts.font_data.insert("chinese".into(), font);
            fonts
                .families
                .entry(FontFamily::Proportional)
                .or_default()
                .insert(0, "chinese".into());
            fonts
                .families
                .entry(FontFamily::Monospace)
                .or_default()
                .push("chinese".into());
            break;
        }
    }
    if let Ok(bytes) = std::fs::read(Path::new(&windows).join("Fonts/segoeui.ttf")) {
        fonts
            .font_data
            .insert("interface".into(), FontData::from_owned(bytes));
        fonts
            .families
            .entry(FontFamily::Proportional)
            .or_default()
            .insert(0, "interface".into());
    }
    ctx.set_fonts(fonts);
}
