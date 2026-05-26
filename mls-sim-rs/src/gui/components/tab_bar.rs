use dioxus::prelude::*;

use crate::gui::state::{Tab, SELECTED_TAB};

#[component]
pub fn TabBar() -> Element {
    let current = SELECTED_TAB();
    rsx! {
        nav { class: "tab-bar",
            for tab in Tab::ALL {
                button {
                    class: if current == tab { "tab tab-active" } else { "tab" },
                    onclick: move |_| *SELECTED_TAB.write() = tab,
                    "{tab.label()}"
                }
            }
        }
    }
}
