//! Centralized color palette and small UI widgets.
//!
//! All `Color32::from_rgb(...)` literals in the rest of the GUI should come through here.
//! Adding a new color? Give it a *semantic* name (`SUCCESS_BG`, not `DARK_GREEN`).

use eframe::egui::{self, Color32, RichText, Sense, Stroke};

// ───────── Backgrounds (dark → light) ─────────

pub const BG_DEEP: Color32 = Color32::from_rgb(14, 14, 18);
pub const BG_BASE: Color32 = Color32::from_rgb(22, 22, 28);
pub const BG_PANEL: Color32 = Color32::from_rgb(28, 28, 35);
pub const BG_RAISED: Color32 = Color32::from_rgb(38, 40, 50);
pub const BG_INACTIVE: Color32 = Color32::from_rgb(45, 46, 58);
pub const BG_INACTIVE_WEAK: Color32 = Color32::from_rgb(40, 41, 52);
pub const BG_HOVERED: Color32 = Color32::from_rgb(58, 60, 76);
pub const BG_HOVERED_WEAK: Color32 = Color32::from_rgb(52, 54, 68);
pub const BG_ACTIVE: Color32 = Color32::from_rgb(65, 75, 110);
pub const BG_ACTIVE_WEAK: Color32 = Color32::from_rgb(58, 66, 96);
pub const BG_OPEN: Color32 = Color32::from_rgb(48, 50, 64);
pub const BG_OPEN_WEAK: Color32 = Color32::from_rgb(44, 46, 58);
pub const BG_NONINTERACTIVE: Color32 = Color32::from_rgb(35, 36, 44);
pub const BG_NONINTERACTIVE_WEAK: Color32 = Color32::from_rgb(32, 33, 40);

// ───────── Text (bright → dim) ─────────

pub const TEXT_PRIMARY: Color32 = Color32::from_rgb(220, 225, 235);
pub const TEXT_BRIGHT: Color32 = Color32::from_rgb(240, 242, 250);
pub const TEXT_DIM: Color32 = Color32::from_rgb(140, 148, 165);
pub const TEXT_FAINT: Color32 = Color32::from_rgb(95, 100, 115);

// ───────── Strokes / borders ─────────

pub const STROKE_FAINT: Color32 = Color32::from_rgb(55, 58, 72);
pub const STROKE_BASE: Color32 = Color32::from_rgb(70, 72, 90);
pub const STROKE_NONINTERACTIVE: Color32 = Color32::from_rgb(62, 64, 80);
pub const STROKE_STRONG: Color32 = Color32::from_rgb(100, 120, 190);
pub const STROKE_OPEN: Color32 = Color32::from_rgb(82, 85, 105);

// ───────── Accent (brand) ─────────

pub const ACCENT: Color32 = Color32::from_rgb(120, 165, 255);
pub const ACCENT_BG: Color32 = Color32::from_rgb(45, 75, 135);

// ───────── Semantic states ─────────

pub const SUCCESS: Color32 = Color32::from_rgb(75, 215, 105);
pub const SUCCESS_BG: Color32 = Color32::from_rgb(28, 55, 38);

pub const WARNING: Color32 = Color32::from_rgb(255, 200, 80);
pub const WARNING_BG: Color32 = Color32::from_rgb(70, 55, 25);

pub const DANGER: Color32 = Color32::from_rgb(255, 95, 95);
pub const DANGER_BRIGHT: Color32 = Color32::from_rgb(255, 175, 175);
pub const DANGER_BG: Color32 = Color32::from_rgb(70, 30, 30);

pub const INFO: Color32 = Color32::from_rgb(105, 165, 255);

pub const NEUTRAL_BG: Color32 = Color32::from_rgb(50, 52, 62);

// Profiler-specific (keep here so all colors are in one file)
pub const PROFILER_HOT: Color32 = Color32::from_rgb(230, 115, 60);
pub const PROFILER_C: Color32 = Color32::from_rgb(130, 175, 235);
pub const PROFILER_BAR_TOTAL: Color32 = Color32::from_rgb(100, 160, 230);

// ───────── UI helpers ─────────

/// A 12px filled circle character in the given color.
/// Used as a status indicator before labels.
pub fn status_dot(ui: &mut egui::Ui, color: Color32) {
    ui.label(RichText::new("●").color(color).size(12.0));
}

/// A 3px-wide accent bar, height matches surrounding line.
/// Use before a heading to draw the eye.
pub fn accent_bar(ui: &mut egui::Ui, height: f32, color: Color32) {
    let (rect, _) = ui.allocate_exact_size(egui::vec2(3.0, height), Sense::hover());
    ui.painter().rect_filled(rect, 1.5, color);
}

/// A filled, rounded chip with monospace text. Use for log levels, tags, etc.
pub fn chip(ui: &mut egui::Ui, text: &str, fg: Color32, bg: Color32) {
    egui::Frame::none()
        .fill(bg)
        .rounding(3.0)
        .inner_margin(egui::Margin::symmetric(5.0, 1.0))
        .show(ui, |ui| {
            ui.label(
                RichText::new(text)
                    .monospace()
                    .size(11.5)
                    .color(fg)
                    .strong(),
            );
        });
}

/// Pick fg / bg for a log level.
pub fn level_chip_colors(level: &str) -> (Color32, Color32) {
    match level {
        "ERR" => (DANGER, DANGER_BG),
        "INF" => (SUCCESS, SUCCESS_BG),
        "DBG" => (TEXT_DIM, NEUTRAL_BG),
        "WRN" | "WARN" => (WARNING, WARNING_BG),
        _ => (TEXT_PRIMARY, BG_RAISED),
    }
}

/// One-call log level chip.
pub fn level_chip(ui: &mut egui::Ui, level: &str) {
    let (fg, bg) = level_chip_colors(level);
    chip(ui, level, fg, bg);
}

/// Centered empty-state placeholder with a large icon glyph, a title, and an
/// optional secondary hint line.
///
/// Layout takes the available space and centers vertically at ~30%.
pub fn empty_state(ui: &mut egui::Ui, icon: &str, title: &str, hint: Option<&str>) {
    ui.vertical_centered(|ui| {
        ui.add_space(ui.available_height() * 0.25);
        ui.label(RichText::new(icon).size(48.0).color(TEXT_FAINT));
        ui.add_space(10.0);
        ui.label(RichText::new(title).size(16.0).color(TEXT_DIM));
        if let Some(h) = hint {
            ui.add_space(4.0);
            ui.label(RichText::new(h).size(12.0).color(TEXT_FAINT));
        }
    });
}

/// Smaller empty state inline (for sidebars). Single line with icon + text.
pub fn empty_state_small(ui: &mut egui::Ui, icon: &str, title: &str, hint: Option<&str>) {
    ui.add_space(8.0);
    ui.vertical_centered(|ui| {
        ui.label(RichText::new(icon).size(24.0).color(TEXT_FAINT));
        ui.add_space(4.0);
        ui.label(RichText::new(title).size(13.0).color(TEXT_DIM));
        if let Some(h) = hint {
            ui.add_space(2.0);
            ui.label(RichText::new(h).size(11.0).color(TEXT_FAINT));
        }
    });
    ui.add_space(8.0);
}

/// Draw a 2px-tall accent underline along the bottom of `rect`.
/// Used for tab-bar "selected" indication.
pub fn underline(painter: &egui::Painter, rect: egui::Rect, color: Color32) {
    let y = rect.bottom() + 1.0;
    painter.line_segment(
        [egui::pos2(rect.left(), y), egui::pos2(rect.right(), y)],
        Stroke::new(2.0, color),
    );
}

