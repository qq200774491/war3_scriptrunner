use eframe::egui;

use super::palette;
use super::{format_time, section_heading, GuiApp};

impl GuiApp {
    pub(crate) fn console_tab(&mut self, ctx: &egui::Context) {
        // Ctrl+F — request focus to the 搜索 TextEdit (consumed below when it renders).
        if ctx.input_mut(|i| i.consume_key(egui::Modifiers::CTRL, egui::Key::F)) {
            self.focus_search = true;
        }

        // Ctrl+K — clear logs.
        if ctx.input_mut(|i| i.consume_key(egui::Modifiers::CTRL, egui::Key::K)) {
            self.logs.clear();
        }

        let room_ids: Vec<String> = {
            let mut ids: Vec<String> = self.subscribed_rooms.iter().cloned().collect();
            ids.sort();
            ids
        };

        // ── Filter bar ──
        egui::TopBottomPanel::top("console_filter")
            .frame(
                egui::Frame::none()
                    .inner_margin(egui::Margin::symmetric(10.0, 6.0))
                    .fill(ctx.style().visuals.window_fill()),
            )
            .show(ctx, |ui: &mut egui::Ui| {
                ui.horizontal(|ui: &mut egui::Ui| {
                    section_heading(ui, "日志");

                    ui.add_space(12.0);

                    ui.label(egui::RichText::new("级别:").strong());
                    ui.spacing_mut().item_spacing.x = 2.0;
                    let levels = [("全部", ""), ("DBG", "DBG"), ("INF", "INF"), ("ERR", "ERR")];
                    for (label, value) in &levels {
                        let selected = self.log_level_filter == *value;
                        let text = if *value == "ERR" && selected {
                            egui::RichText::new(*label).color(palette::DANGER)
                        } else {
                            egui::RichText::new(*label)
                        };
                        if ui.selectable_label(selected, text).clicked() {
                            self.log_level_filter = value.to_string();
                        }
                    }
                    ui.spacing_mut().item_spacing.x = 8.0;

                    ui.separator();

                    ui.label(egui::RichText::new("房间:").strong());
                    egui::ComboBox::from_id_salt("log_room_filter")
                        .selected_text(if self.log_room_filter.is_empty() {
                            "全部"
                        } else {
                            &self.log_room_filter
                        })
                        .width(100.0)
                        .show_ui(ui, |ui: &mut egui::Ui| {
                            ui.selectable_value(&mut self.log_room_filter, String::new(), "全部");
                            for id in &room_ids {
                                ui.selectable_value(&mut self.log_room_filter, id.clone(), id);
                            }
                        });

                    ui.separator();

                    ui.label(egui::RichText::new("搜索:").strong());
                    let search_resp = ui.add(
                        egui::TextEdit::singleline(&mut self.log_search)
                            .desired_width(120.0)
                            .hint_text("关键字..."),
                    );
                    if self.consume_focus_search() {
                        search_resp.request_focus();
                    }
                    // Esc while search focused with content → clear (don't blur — next Esc will).
                    if search_resp.has_focus()
                        && !self.log_search.is_empty()
                        && ctx.input_mut(|i| {
                            i.consume_key(egui::Modifiers::NONE, egui::Key::Escape)
                        })
                    {
                        self.log_search.clear();
                    }

                    ui.separator();
                    ui.checkbox(&mut self.auto_scroll, "自动滚动");

                    ui.with_layout(
                        egui::Layout::right_to_left(egui::Align::Center),
                        |ui: &mut egui::Ui| {
                            if ui.button("清空").clicked() {
                                self.logs.clear();
                            }
                            ui.label(
                                egui::RichText::new(format!("{} 条", self.logs.len()))
                                    .small()
                                    .color(palette::TEXT_DIM),
                            );
                        },
                    );
                });
            });

        // ── Log entries ──
        egui::CentralPanel::default()
            .frame(egui::Frame::none().inner_margin(egui::Margin::symmetric(10.0, 4.0)))
            .show(ctx, |ui: &mut egui::Ui| {
                let filtered_logs: Vec<_> = self
                    .logs
                    .iter()
                    .filter(|l| {
                        if !self.log_level_filter.is_empty() && l.level != self.log_level_filter {
                            return false;
                        }
                        if !self.log_room_filter.is_empty() && l.room_id != self.log_room_filter {
                            return false;
                        }
                        if !self.log_search.is_empty()
                            && !l
                                .message
                                .to_lowercase()
                                .contains(&self.log_search.to_lowercase())
                        {
                            return false;
                        }
                        true
                    })
                    .collect();

                let row_height = 20.0;
                let total = filtered_logs.len();

                egui::ScrollArea::vertical()
                    .auto_shrink(false)
                    .stick_to_bottom(self.auto_scroll)
                    .show_rows(ui, row_height, total, |ui: &mut egui::Ui, row_range| {
                        for i in row_range {
                            let log = filtered_logs[i];
                            let msg_color = match log.level.as_str() {
                                "ERR" => palette::DANGER_BRIGHT,
                                "DBG" => palette::TEXT_DIM,
                                _ => ui.visuals().text_color(),
                            };

                            ui.horizontal(|ui: &mut egui::Ui| {
                                ui.spacing_mut().item_spacing.x = 0.0;
                                ui.label(
                                    egui::RichText::new(format_time(log.timestamp))
                                        .monospace()
                                        .size(13.0)
                                        .color(palette::TEXT_DIM),
                                );
                                ui.add_space(2.0);
                                palette::level_chip(ui, &log.level);
                                ui.add_space(2.0);
                                ui.label(
                                    egui::RichText::new(format!(" [{}] ", log.room_id))
                                        .monospace()
                                        .size(13.0)
                                        .color(palette::INFO),
                                );
                                ui.label(
                                    egui::RichText::new(&log.message)
                                        .monospace()
                                        .size(13.0)
                                        .color(msg_color),
                                );
                            });
                        }
                    });
            });
    }
}
