use dioxus::prelude::*;

struct Contributor {
    name: &'static str,
    github: Option<&'static str>,
    role: &'static str,
}

// 维护一份精选参与者列表（手工维护，比解析 Cargo.toml authors 更可控）
const CONTRIBUTORS: &[Contributor] = &[
    Contributor {
        name: "花吃了阿九",
        github: Some("qq200774491"),
        role: "贡献者",
    },
    Contributor {
        name: "小为",
        github: Some("Syh1906"),
        role: "贡献者",
    },
    Contributor {
        name: "Carlos",
        github: Some("Carlos-X"),
        role: "贡献者",
    },
];

fn initial(name: &str) -> String {
    name.chars().next().map(|c| c.to_string()).unwrap_or_default()
}

fn color_for(name: &str) -> String {
    let mut h: u32 = 0;
    for b in name.bytes() {
        h = h.wrapping_mul(31).wrapping_add(b as u32);
    }
    let hue = h % 360;
    format!("hsl({}, 45%, 45%)", hue)
}

#[component]
pub fn About() -> Element {
    rsx! {
        div { class: "about-page",
            div { class: "about-card",
                // ── 头部：名称 + 版本 ───────────
                div { class: "about-title",
                    span { class: "about-name", "MLS 云脚本模拟器" }
                    span { class: "about-ver mono", "v" { env!("CARGO_PKG_VERSION") } }
                }
                p { class: "muted", "本地 Lua 云脚本测试环境 · 帮助开发者在桌面环境模拟仙途云脚本运行" }

                // ── 链接 ─────────────────────────
                section {
                    h4 { "链接" }
                    div { class: "about-links",
                        a {
                            href: env!("CARGO_PKG_REPOSITORY"),
                            target: "_blank",
                            class: "about-link",
                            "📦 GitHub 仓库"
                        }
                        a {
                            href: format!("{}/issues", env!("CARGO_PKG_REPOSITORY")),
                            target: "_blank",
                            class: "about-link",
                            "🐛 Issue Tracker"
                        }
                        a {
                            href: format!("{}/releases", env!("CARGO_PKG_REPOSITORY")),
                            target: "_blank",
                            class: "about-link",
                            "🚀 Releases"
                        }
                    }
                }

                // ── 参与者 ───────────────────────
                section {
                    h4 { "参与者" }
                    div { class: "contributors",
                        for c in CONTRIBUTORS.iter() {
                            ContributorCard {
                                name: c.name,
                                github: c.github,
                                role: c.role,
                            }
                        }
                    }
                }

                // ── 技术栈 ───────────────────────
                section {
                    h4 { "技术栈" }
                    div { class: "tech-stack",
                        span { class: "tech-tag", "Rust" }
                        span { class: "tech-tag", "Dioxus 0.7" }
                        span { class: "tech-tag", "WebView2" }
                        span { class: "tech-tag", "Tokio" }
                        span { class: "tech-tag", "Lua 5.3 (mlua)" }
                        span { class: "tech-tag", "Axum" }
                    }
                }
            }
        }
    }
}

#[component]
fn ContributorCard(name: &'static str, github: Option<&'static str>, role: &'static str) -> Element {
    let color = color_for(name);
    let init = initial(name);
    // 把 github 头像走 background-image：加载失败时 webview 静默不显示，
    // 自动留下底色 + 首字母作为兜底。比 <img onerror> 兼容性更好。
    let style = match github {
        Some(u) => format!(
            "background-color: {}; background-image: url('https://github.com/{}.png?size=80'); background-size: cover; background-position: center;",
            color, u
        ),
        None => format!("background-color: {};", color),
    };

    rsx! {
        div { class: "contrib",
            div { class: "avatar", style: "{style}",
                // 仅在没有 GitHub 头像时显示首字母占位；有头像时即使加载失败也仅显示色块。
                if github.is_none() {
                    span { class: "avatar-init", "{init}" }
                }
            }
            div { class: "contrib-meta",
                div { class: "contrib-name", "{name}" }
                div { class: "contrib-role muted small", "{role}" }
            }
            if let Some(user) = github {
                a {
                    href: "https://github.com/{user}",
                    target: "_blank",
                    class: "contrib-link",
                    title: "GitHub @{user}",
                    "@{user} ↗"
                }
            }
        }
    }
}
