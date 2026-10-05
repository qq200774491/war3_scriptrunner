use eframe::egui::{self, Color32, Pos2, Rect, Stroke, Vec2, Widget};
use std::{collections::HashMap, sync::Arc};

#[derive(Clone, Copy, Hash, PartialEq, Eq)]
pub enum Icon {
    Console,
    Settings,
    Archive,
    Play,
    Stop,
    Check,
    Folder,
    Search,
    Close,
    Minimize,
    Maximize,
    Restore,
    Inspector,
    Script,
    Layers,
    Game,
    File,
    Trash,
}
impl Icon {
    fn svg(self) -> &'static str {
        match self {
            Self::Console => include_str!("assets/tabler/terminal-2.svg"),
            Self::Settings => include_str!("assets/tabler/settings.svg"),
            Self::Archive => include_str!("assets/tabler/archive.svg"),
            Self::Play => include_str!("assets/tabler/player-play.svg"),
            Self::Stop => include_str!("assets/tabler/player-stop.svg"),
            Self::Check => include_str!("assets/tabler/check.svg"),
            Self::Folder => include_str!("assets/tabler/folder.svg"),
            Self::Search => include_str!("assets/tabler/search.svg"),
            Self::Close => include_str!("assets/tabler/x.svg"),
            Self::Minimize => include_str!("assets/tabler/minus.svg"),
            Self::Maximize => include_str!("assets/tabler/square.svg"),
            Self::Restore => include_str!("assets/tabler/copy.svg"),
            Self::Inspector => include_str!("assets/tabler/layout-sidebar-right.svg"),
            Self::Script => include_str!("assets/tabler/file-code.svg"),
            Self::Layers => include_str!("assets/tabler/layers-subtract.svg"),
            Self::Game => include_str!("assets/tabler/device-gamepad-2.svg"),
            Self::File => include_str!("assets/tabler/file-text.svg"),
            Self::Trash => include_str!("assets/tabler/trash.svg"),
        }
    }
}

/// 缓存 SVG 的矢量轮廓，不创建图标纹理。
#[derive(Default)]
pub struct Icons {
    paths: HashMap<Icon, Arc<Vec<Contour>>>,
}
struct Contour {
    points: Vec<Pos2>,
    closed: bool,
    width: f32,
}
impl Contour {
    fn pixel_aligned_points(&self, origin: Vec2, scale: f32, stroke_pixels: f32) -> Vec<Pos2> {
        let bounds = Rect::from_points(&self.points);
        let min = origin + bounds.min.to_vec2() * scale;
        let max = origin + bounds.max.to_vec2() * scale;
        // 奇数像素描边的中心落在半像素上，偶数像素描边落在整数上。
        let phase = Vec2::splat((stroke_pixels % 2.0) * 0.5);
        let aligned_min = (min - phase).round() + phase;
        let aligned_max = (max - phase).round() + phase;
        let align = |value: f32, start: f32, end: f32, target_start: f32, target_end: f32| {
            if end > start {
                target_start + (value - start) * (target_end - target_start) / (end - start)
            } else {
                target_start
            }
        };
        // 连续变换整个轮廓，保留曲线平滑度，避免逐点取整使圆弧变成锯齿。
        self.points
            .iter()
            .map(|point| {
                let physical = origin + point.to_vec2() * scale;
                egui::pos2(
                    align(physical.x, min.x, max.x, aligned_min.x, aligned_max.x),
                    align(physical.y, min.y, max.y, aligned_min.y, aligned_max.y),
                )
            })
            .collect()
    }
}
fn contours(svg: &str) -> Vec<Contour> {
    let tree = usvg::Tree::from_str(svg, &usvg::Options::default()).expect("bundled Tabler SVG");
    let mut result = Vec::new();
    collect(tree.root(), &mut result);
    result
}
fn collect(group: &usvg::Group, result: &mut Vec<Contour>) {
    use usvg::{tiny_skia_path::PathSegment, Node};
    for node in group.children() {
        let path = match node {
            Node::Group(group) => {
                collect(group, result);
                continue;
            }
            Node::Path(path) if path.stroke().is_some() => path,
            _ => continue,
        };
        let transform = path.abs_transform();
        let point = |mut value| {
            transform.map_point(&mut value);
            egui::pos2(value.x, value.y)
        };
        let width = path.stroke().unwrap().width().get() * transform.get_scale().0;
        let mut points = Vec::new();
        for segment in path.data().segments() {
            match segment {
                PathSegment::MoveTo(p) => {
                    finish(result, &mut points, false, width);
                    points.push(point(p));
                }
                PathSegment::LineTo(p) => points.push(point(p)),
                PathSegment::QuadTo(control, end) => {
                    let start = *points.last().expect("SVG subpath start");
                    let curve = egui::epaint::QuadraticBezierShape::from_points_stroke(
                        [start, point(control), point(end)],
                        false,
                        Color32::TRANSPARENT,
                        Stroke::NONE,
                    );
                    points.extend(curve.flatten(Some(0.025)).into_iter().skip(1));
                }
                PathSegment::CubicTo(c1, c2, end) => {
                    let start = *points.last().expect("SVG subpath start");
                    let curve = egui::epaint::CubicBezierShape::from_points_stroke(
                        [start, point(c1), point(c2), point(end)],
                        false,
                        Color32::TRANSPARENT,
                        Stroke::NONE,
                    );
                    points.extend(curve.flatten(Some(0.025)).into_iter().skip(1));
                }
                PathSegment::Close => finish(result, &mut points, true, width),
            }
        }
        finish(result, &mut points, false, width);
    }
}
fn finish(result: &mut Vec<Contour>, points: &mut Vec<Pos2>, closed: bool, width: f32) {
    if points.len() > 1 {
        // 闭合 SVG 可能已经含首点，避免零长度的尾段影响描边。
        if closed && points.first() == points.last() {
            points.pop();
        }
        result.push(Contour {
            points: std::mem::take(points),
            closed,
            width,
        });
    } else {
        points.clear();
    }
}
pub struct VectorIcon {
    contours: Arc<Vec<Contour>>,
    size: f32,
    tint: Color32,
}
impl VectorIcon {
    fn paint(&self, ui: &egui::Ui, rect: Rect) {
        let pixels_per_point = ui.ctx().pixels_per_point();
        let origin = (rect.min.to_vec2() * pixels_per_point).round() / pixels_per_point;
        let scale = rect.width() / 24.0;
        let color = if ui.is_enabled() {
            self.tint
        } else {
            self.tint.gamma_multiply(0.4)
        };
        for path in self.contours.iter() {
            let stroke_pixels = (path.width * scale * pixels_per_point).round().max(1.0);
            let points = path
                .pixel_aligned_points(
                    origin * pixels_per_point,
                    scale * pixels_per_point,
                    stroke_pixels,
                )
                .into_iter()
                .map(|point| point / pixels_per_point)
                .collect();
            ui.painter().add(egui::epaint::PathShape {
                points,
                closed: path.closed,
                fill: Color32::TRANSPARENT,
                stroke: Stroke::new(stroke_pixels / pixels_per_point, color).into(),
            });
        }
    }
    pub fn button(self, label: impl Into<egui::WidgetText>) -> VectorButton {
        VectorButton {
            icon: self,
            label: label.into(),
            frame: true,
            min_size: Vec2::ZERO,
            fill: None,
            stroke: None,
        }
    }
}
impl Widget for VectorIcon {
    fn ui(self, ui: &mut egui::Ui) -> egui::Response {
        let (rect, response) = ui.allocate_exact_size(Vec2::splat(self.size), egui::Sense::hover());
        if ui.is_rect_visible(rect) {
            self.paint(ui, rect);
        }
        response
    }
}
pub struct VectorButton {
    icon: VectorIcon,
    label: egui::WidgetText,
    frame: bool,
    min_size: Vec2,
    fill: Option<Color32>,
    stroke: Option<Stroke>,
}
impl VectorButton {
    pub fn frame(mut self, frame: bool) -> Self {
        self.frame = frame;
        self
    }
    pub fn min_size(mut self, size: Vec2) -> Self {
        self.min_size = size;
        self
    }
    pub fn fill(mut self, fill: Color32) -> Self {
        self.fill = Some(fill);
        self
    }
    pub fn stroke(mut self, stroke: Stroke) -> Self {
        self.stroke = Some(stroke);
        self
    }
}
impl Widget for VectorButton {
    fn ui(self, ui: &mut egui::Ui) -> egui::Response {
        let text = self.label.into_galley(
            ui,
            Some(egui::TextWrapMode::Extend),
            f32::INFINITY,
            egui::TextStyle::Button,
        );
        let gap = if text.is_empty() {
            0.0
        } else {
            ui.spacing().icon_spacing
        };
        let padding = if self.frame {
            ui.spacing().button_padding
        } else {
            Vec2::ZERO
        };
        let content = egui::vec2(
            self.icon.size + gap + text.size().x,
            self.icon.size.max(text.size().y),
        );
        let desired = (content + padding * 2.0)
            .max(self.min_size)
            .max(egui::vec2(0.0, ui.spacing().interact_size.y));
        let (rect, response) = ui.allocate_at_least(desired, egui::Sense::click());
        response.widget_info(|| {
            egui::WidgetInfo::labeled(egui::WidgetType::Button, ui.is_enabled(), text.text())
        });
        if ui.is_rect_visible(rect) {
            let visuals = ui.style().interact(&response);
            let fill = self.fill.unwrap_or(
                if self.frame || response.hovered() || response.has_focus() {
                    visuals.weak_bg_fill
                } else {
                    Color32::TRANSPARENT
                },
            );
            let stroke = self
                .stroke
                .unwrap_or(if self.frame || response.has_focus() {
                    visuals.bg_stroke
                } else {
                    Stroke::NONE
                });
            ui.painter().rect(rect, visuals.rounding, fill, stroke);
            let x = if text.is_empty() {
                rect.center().x - self.icon.size * 0.5
            } else {
                rect.left() + padding.x
            };
            let icon_rect = Rect::from_min_size(
                egui::pos2(x, rect.center().y - self.icon.size * 0.5),
                Vec2::splat(self.icon.size),
            );
            self.icon.paint(ui, icon_rect);
            ui.painter().galley(
                egui::pos2(
                    icon_rect.right() + gap,
                    rect.center().y - text.size().y * 0.5,
                ),
                text,
                visuals.text_color(),
            );
        }
        response
    }
}
impl Icons {
    pub fn image(
        &mut self,
        ctx: &egui::Context,
        icon: Icon,
        size: f32,
        tint: Color32,
    ) -> VectorIcon {
        let paths = self
            .paths
            .entry(icon)
            .or_insert_with(|| Arc::new(contours(icon.svg())))
            .clone();
        let scale = ctx.pixels_per_point();
        VectorIcon {
            contours: paths,
            size: (size * scale).round().max(1.0) / scale,
            tint,
        }
    }
    pub fn button(&mut self, ui: &mut egui::Ui, icon: Icon, label: &str) -> egui::Response {
        let image = self.image(ui.ctx(), icon, 16.0, ui.visuals().text_color());
        let response = ui
            .add(image.button("").frame(false).min_size(Vec2::splat(28.0)))
            .on_hover_text(label);
        response.widget_info(|| {
            egui::WidgetInfo::labeled(egui::WidgetType::Button, ui.is_enabled(), label)
        });
        response
    }
    pub fn action(
        &mut self,
        ui: &mut egui::Ui,
        icon: Icon,
        label: &str,
        enabled: bool,
    ) -> egui::Response {
        let image = self.image(ui.ctx(), icon, 15.0, ui.visuals().text_color());
        ui.add_enabled(enabled, image.button(label))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_bundled_icon_has_finite_vector_contours() {
        for icon in [
            Icon::Console,
            Icon::Settings,
            Icon::Archive,
            Icon::Play,
            Icon::Stop,
            Icon::Check,
            Icon::Folder,
            Icon::Search,
            Icon::Close,
            Icon::Minimize,
            Icon::Maximize,
            Icon::Restore,
            Icon::Inspector,
            Icon::Script,
            Icon::Layers,
            Icon::Game,
            Icon::File,
            Icon::Trash,
        ] {
            let paths = contours(icon.svg());
            assert!(!paths.is_empty());
            for path in paths {
                assert!(path.points.len() >= 2);
                assert!(path.width.is_finite() && path.width > 0.0);
                assert!(path
                    .points
                    .iter()
                    .all(|p| p.x.is_finite() && p.y.is_finite()));
            }
        }
    }

    #[test]
    fn svg_line_and_curves_preserve_endpoints() {
        let search = contours(Icon::Search.svg());
        assert_eq!(search.len(), 2);
        assert_eq!(
            search[1].points,
            [egui::pos2(21.0, 21.0), egui::pos2(15.0, 15.0)]
        );
        assert!(search[0].points.len() > 12);
    }
}
