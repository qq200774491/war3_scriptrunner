use dioxus::prelude::*;

use crate::gui::state::{
    now_secs, SAVE_MSG, SETTINGS_ARCHIVE_DIR, SETTINGS_HOST, SETTINGS_PORT,
};
use crate::gui::AppCtx;

#[component]
pub fn Settings() -> Element {
    let ctx = use_context::<AppCtx>();
    let save_msg = SAVE_MSG();

    let on_save = move |_| {
        let port: u16 = SETTINGS_PORT.peek().parse().unwrap_or(5000);
        let host = SETTINGS_HOST.peek().clone();
        let archive_dir = SETTINGS_ARCHIVE_DIR.peek().clone();
        let mut cfg = ctx.config.write().unwrap();
        cfg.host = host;
        cfg.port = port;
        cfg.archive_dir = archive_dir;
        let now = now_secs();
        match cfg.save(&ctx.config_path) {
            Ok(_) => *SAVE_MSG.write() = Some(("配置已保存".into(), false, now)),
            Err(e) => *SAVE_MSG.write() = Some((format!("保存失败: {}", e), true, now)),
        }
    };

    let msg_visible = save_msg
        .as_ref()
        .map(|(_, _, ts)| now_secs() - ts < 3.0)
        .unwrap_or(false);

    rsx! {
        div { class: "settings",
            section { class: "card",
                h3 { "Bridge 服务" }
                div { class: "form-row",
                    label { "监听地址" }
                    input {
                        r#type: "text",
                        value: "{SETTINGS_HOST}",
                        oninput: move |e| *SETTINGS_HOST.write() = e.value(),
                    }
                }
                div { class: "form-row",
                    label { "端口" }
                    input {
                        r#type: "text",
                        value: "{SETTINGS_PORT}",
                        oninput: move |e| *SETTINGS_PORT.write() = e.value(),
                        style: "max-width: 120px;",
                    }
                }
            }

            section { class: "card",
                h3 { "存档" }
                div { class: "form-row",
                    label { "存档目录" }
                    input {
                        r#type: "text",
                        value: "{SETTINGS_ARCHIVE_DIR}",
                        oninput: move |e| *SETTINGS_ARCHIVE_DIR.write() = e.value(),
                    }
                }
            }

            div { class: "save-row",
                button { class: "btn btn-primary", onclick: on_save, "保存配置" }
                if msg_visible {
                    if let Some((msg, is_err, _)) = save_msg.as_ref() {
                        span {
                            class: if *is_err { "flash flash-err" } else { "flash flash-ok" },
                            "{msg}"
                        }
                    }
                }
            }
        }
    }
}
