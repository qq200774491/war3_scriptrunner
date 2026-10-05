//! 开发验收使用 egui 自身帧缓冲导出，不读取桌面或其他应用。
use super::{theme::Theme, Page};
use eframe::egui;
use std::path::PathBuf;
pub struct Snapshot {
    pub theme: Theme,
    pub page: Page,
    pub log: Option<PathBuf>,
    path: PathBuf,
    frames: u32,
}
fn option(name: &str) -> Option<String> {
    let args: Vec<_> = std::env::args().collect();
    args.windows(2)
        .find(|pair| pair[0] == name)
        .map(|pair| pair[1].clone())
}
pub fn window_size() -> [f32; 2] {
    [
        option("--width")
            .and_then(|s| s.parse().ok())
            .unwrap_or(1240.0),
        option("--height")
            .and_then(|s| s.parse().ok())
            .unwrap_or(780.0),
    ]
}
impl Snapshot {
    pub fn from_args() -> Option<Self> {
        Some(Self {
            path: option("--capture")?.into(),
            theme: match option("--theme").as_deref() {
                Some("light") => Theme::Light,
                Some("monochrome") => Theme::Monochrome,
                _ => Theme::Shadowworks,
            },
            page: match option("--page").as_deref() {
                Some("settings") => Page::Settings,
                Some("archives") => Page::Archives,
                _ => Page::Console,
            },
            log: option("--capture-log").map(PathBuf::from),
            frames: 0,
        })
    }
    pub fn update(&mut self, ctx: &egui::Context) {
        self.frames += 1;
        if self.frames == 5 {
            ctx.send_viewport_cmd(egui::ViewportCommand::Screenshot);
        }
        let captured = ctx.input(|input| {
            input.events.iter().find_map(|event| {
                if let egui::Event::Screenshot { image, .. } = event {
                    Some(image.clone())
                } else {
                    None
                }
            })
        });
        if let Some(image) = captured {
            let metadata = serde_json::json!({
                "native_pixels_per_point": ctx.native_pixels_per_point(),
                "pixels_per_point": ctx.pixels_per_point(),
                "zoom_factor": ctx.zoom_factor(),
                "image_pixels": image.size,
                "logical_size": [ctx.screen_rect().width(), ctx.screen_rect().height()],
            });
            let _ = std::fs::write(self.path.with_extension("json"), metadata.to_string());
            let bytes: Vec<u8> = image
                .pixels
                .iter()
                .flat_map(|color| color.to_array())
                .collect();
            let result = image::save_buffer(
                &self.path,
                &bytes,
                image.width() as u32,
                image.height() as u32,
                image::ColorType::Rgba8,
            );
            if let Err(error) = result {
                let _ = std::fs::write(self.path.with_extension("error.txt"), error.to_string());
            }
            ctx.send_viewport_cmd(egui::ViewportCommand::Close);
        }
        ctx.request_repaint();
    }
}
