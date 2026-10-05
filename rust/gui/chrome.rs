use super::{
    icons::{Icon, Icons},
    theme::Palette,
};
use eframe::egui::{
    self, Align2, CursorIcon, Id, LayerId, Order, Rect, ResizeDirection, Sense, Stroke,
    ViewportCommand,
};

pub fn controls(ui: &mut egui::Ui, icons: &mut Icons) {
    if icons.button(ui, Icon::Close, "关闭窗口").clicked() {
        ui.ctx().send_viewport_cmd(ViewportCommand::Close);
    }
    let maximized = ui
        .ctx()
        .input(|input| input.viewport().maximized.unwrap_or(false));
    if icons
        .button(
            ui,
            if maximized {
                Icon::Restore
            } else {
                Icon::Maximize
            },
            if maximized { "还原" } else { "最大化" },
        )
        .clicked()
    {
        ui.ctx()
            .send_viewport_cmd(ViewportCommand::Maximized(!maximized));
    }
    if icons.button(ui, Icon::Minimize, "最小化").clicked() {
        ui.ctx().send_viewport_cmd(ViewportCommand::Minimized(true));
    }
}
pub fn drag(ui: &mut egui::Ui, id: &str, rect: Rect) {
    let response = ui.interact(rect, Id::new(id), Sense::click_and_drag());
    if response.drag_started() {
        ui.ctx().send_viewport_cmd(ViewportCommand::StartDrag);
    }
    if response.double_clicked() {
        let maximized = ui
            .ctx()
            .input(|input| input.viewport().maximized.unwrap_or(false));
        ui.ctx()
            .send_viewport_cmd(ViewportCommand::Maximized(!maximized));
    }
}
pub fn edges(ctx: &egui::Context, palette: Palette) {
    if ctx.input(|input| input.viewport().maximized.unwrap_or(false)) {
        return;
    }
    let rect = ctx.screen_rect();
    let border = 4.0;
    let corner = 10.0;
    let regions = [
        (
            Align2::LEFT_TOP,
            ResizeDirection::NorthWest,
            CursorIcon::ResizeNwSe,
        ),
        (
            Align2::RIGHT_TOP,
            ResizeDirection::NorthEast,
            CursorIcon::ResizeNeSw,
        ),
        (
            Align2::LEFT_BOTTOM,
            ResizeDirection::SouthWest,
            CursorIcon::ResizeNeSw,
        ),
        (
            Align2::RIGHT_BOTTOM,
            ResizeDirection::SouthEast,
            CursorIcon::ResizeNwSe,
        ),
    ];
    let mut hits = Vec::new();
    for (align, direction, cursor) in regions {
        hits.push((
            align.align_size_within_rect(egui::vec2(corner, corner), rect),
            direction,
            cursor,
        ));
    }
    hits.extend([
        (
            Rect::from_min_max(
                rect.min + egui::vec2(corner, 0.0),
                egui::pos2(rect.max.x - corner, rect.min.y + border),
            ),
            ResizeDirection::North,
            CursorIcon::ResizeVertical,
        ),
        (
            Rect::from_min_max(
                egui::pos2(rect.min.x + corner, rect.max.y - border),
                rect.max - egui::vec2(corner, 0.0),
            ),
            ResizeDirection::South,
            CursorIcon::ResizeVertical,
        ),
        (
            Rect::from_min_max(
                rect.min + egui::vec2(0.0, corner),
                egui::pos2(rect.min.x + border, rect.max.y - corner),
            ),
            ResizeDirection::West,
            CursorIcon::ResizeHorizontal,
        ),
        (
            Rect::from_min_max(
                egui::pos2(rect.max.x - border, rect.min.y + corner),
                rect.max - egui::vec2(0.0, corner),
            ),
            ResizeDirection::East,
            CursorIcon::ResizeHorizontal,
        ),
    ]);
    let pixel = 1.0 / ctx.pixels_per_point();
    ctx.layer_painter(LayerId::new(Order::Foreground, Id::new("window_border")))
        .rect_stroke(
            rect.shrink(pixel * 0.5),
            0.0,
            Stroke::new(pixel, palette.border),
        );
    for (index, (region, direction, cursor)) in hits.into_iter().enumerate() {
        egui::Area::new(Id::new(("resize", index)))
            .order(Order::Foreground)
            .fixed_pos(region.min)
            .show(ctx, |ui| {
                let (_, response) = ui.allocate_exact_size(region.size(), Sense::drag());
                if response.hovered() {
                    ctx.set_cursor_icon(cursor);
                }
                if response.drag_started() {
                    ctx.send_viewport_cmd(ViewportCommand::BeginResize(direction));
                }
            });
    }
}
