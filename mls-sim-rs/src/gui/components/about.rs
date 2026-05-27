use std::sync::OnceLock;

use dioxus::prelude::*;

struct Contributor {
    name: &'static str,
    github: Option<&'static str>,
    role: &'static str,
    avatar: Option<(&'static [u8], &'static str)>,
}

const AVATAR_QQ: &[u8] = include_bytes!("../assets/avatars/qq200774491.jpg");
const AVATAR_SYH: &[u8] = include_bytes!("../assets/avatars/Syh1906.jpg");
const AVATAR_CARLOS: &[u8] = include_bytes!("../assets/avatars/Carlos-X.png");

// 维护一份精选参与者列表（手工维护，比解析 Cargo.toml authors 更可控）
const CONTRIBUTORS: &[Contributor] = &[
    Contributor {
        name: "花吃了阿九",
        github: Some("qq200774491"),
        role: "贡献者",
        avatar: Some((AVATAR_QQ, "image/jpeg")),
    },
    Contributor {
        name: "小为",
        github: Some("Syh1906"),
        role: "贡献者",
        avatar: Some((AVATAR_SYH, "image/jpeg")),
    },
    Contributor {
        name: "Carlos",
        github: Some("Carlos-X"),
        role: "贡献者",
        avatar: Some((AVATAR_CARLOS, "image/png")),
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

// 把头像字节编成 data:URI；首次访问后缓存，避免每次重渲染重新 base64。
fn avatar_data_uri(bytes: &'static [u8], mime: &str) -> &'static str {
    // 按字节切片指针做 key —— 三张静态切片地址互不相同，足以区分。
    static CACHE_QQ: OnceLock<String> = OnceLock::new();
    static CACHE_SYH: OnceLock<String> = OnceLock::new();
    static CACHE_CARLOS: OnceLock<String> = OnceLock::new();

    let slot = if std::ptr::eq(bytes.as_ptr(), AVATAR_QQ.as_ptr()) {
        &CACHE_QQ
    } else if std::ptr::eq(bytes.as_ptr(), AVATAR_SYH.as_ptr()) {
        &CACHE_SYH
    } else {
        &CACHE_CARLOS
    };
    slot.get_or_init(|| format!("data:{};base64,{}", mime, b64(bytes)))
        .as_str()
}

fn b64(data: &[u8]) -> String {
    const A: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = String::with_capacity(data.len().div_ceil(3) * 4);
    let mut chunks = data.chunks_exact(3);
    for c in &mut chunks {
        let n = ((c[0] as u32) << 16) | ((c[1] as u32) << 8) | c[2] as u32;
        out.push(A[((n >> 18) & 0x3F) as usize] as char);
        out.push(A[((n >> 12) & 0x3F) as usize] as char);
        out.push(A[((n >> 6) & 0x3F) as usize] as char);
        out.push(A[(n & 0x3F) as usize] as char);
    }
    let rem = chunks.remainder();
    match rem.len() {
        1 => {
            let n = (rem[0] as u32) << 16;
            out.push(A[((n >> 18) & 0x3F) as usize] as char);
            out.push(A[((n >> 12) & 0x3F) as usize] as char);
            out.push_str("==");
        }
        2 => {
            let n = ((rem[0] as u32) << 16) | ((rem[1] as u32) << 8);
            out.push(A[((n >> 18) & 0x3F) as usize] as char);
            out.push(A[((n >> 12) & 0x3F) as usize] as char);
            out.push(A[((n >> 6) & 0x3F) as usize] as char);
            out.push('=');
        }
        _ => {}
    }
    out
}

#[component]
pub fn About() -> Element {
    rsx! {
        div { class: "about-page",
            div { class: "about-card",
                div { class: "about-title",
                    span { class: "about-name", "MLS 云脚本模拟器" }
                    span { class: "about-ver mono", "v" { env!("CARGO_PKG_VERSION") } }
                }
                p { class: "muted", "本地 Lua 云脚本测试环境 · 帮助开发者在桌面环境模拟仙途云脚本运行" }

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

                section {
                    h4 { "参与者" }
                    div { class: "contributors",
                        for c in CONTRIBUTORS.iter() {
                            ContributorCard {
                                name: c.name,
                                github: c.github,
                                role: c.role,
                                avatar: c.avatar,
                            }
                        }
                    }
                }

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
fn ContributorCard(
    name: &'static str,
    github: Option<&'static str>,
    role: &'static str,
    avatar: Option<(&'static [u8], &'static str)>,
) -> Element {
    let color = color_for(name);
    let init = initial(name);
    let style = match avatar {
        Some((bytes, mime)) => format!(
            "background-color: {}; background-image: url('{}'); background-size: cover; background-position: center;",
            color,
            avatar_data_uri(bytes, mime)
        ),
        None => format!("background-color: {};", color),
    };

    rsx! {
        div { class: "contrib",
            div { class: "avatar", style: "{style}",
                if avatar.is_none() {
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
