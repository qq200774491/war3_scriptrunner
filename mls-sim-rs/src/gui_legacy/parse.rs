//! Command-bar parsing and completion logic.
//!
//! Kept egui-free so it's directly testable.

use std::collections::VecDeque;

pub const MAX_HISTORY: usize = 100;
pub const MAX_RECENT_EVENTS: usize = 20;

pub struct CommandHint {
    pub name: &'static str,
    pub usage: &'static str,
}

pub const COMMAND_HINTS: &[CommandHint] = &[
    CommandHint {
        name: "rooms",
        usage: "rooms - 列出所有房间",
    },
    CommandHint {
        name: "clear",
        usage: "clear - 清空控制台日志",
    },
    CommandHint {
        name: "create",
        usage: "create <脚本目录> [模式] - 创建房间",
    },
    CommandHint {
        name: "select",
        usage: "select <房间> - 选择房间",
    },
    CommandHint {
        name: "stop",
        usage: "stop <房间> - 停止房间",
    },
    CommandHint {
        name: "destroy",
        usage: "destroy <房间> - 删除房间",
    },
    CommandHint {
        name: "restart",
        usage: "restart <房间> - 重启房间",
    },
    CommandHint {
        name: "join",
        usage: "join <房间> <槽位> [名称] - 玩家上线",
    },
    CommandHint {
        name: "leave",
        usage: "leave <房间> <槽位> - 玩家离线",
    },
    CommandHint {
        name: "exit",
        usage: "exit <房间> <槽位> - 玩家退出",
    },
    CommandHint {
        name: "event",
        usage: "event <房间> <事件名> <玩家|*> [数据...] - 发送事件",
    },
];

/// Split a command string into args, honoring single/double quotes.
pub fn split_command_args(command: &str) -> Result<Vec<String>, String> {
    let mut args = Vec::new();
    let mut current = String::new();
    let mut quote: Option<char> = None;
    let mut token_started = false;

    for ch in command.chars() {
        match (quote, ch) {
            (Some(q), c) if c == q => {
                quote = None;
            }
            (Some(_), c) => {
                token_started = true;
                current.push(c);
            }
            (None, '"') | (None, '\'') => {
                quote = Some(ch);
                token_started = true;
            }
            (None, c) if c.is_whitespace() => {
                if token_started {
                    args.push(std::mem::take(&mut current));
                    token_started = false;
                }
            }
            (None, c) => {
                token_started = true;
                current.push(c);
            }
        }
    }

    if let Some(q) = quote {
        return Err(format!("缺少结束引号: {}", q));
    }
    if token_started {
        args.push(current);
    }
    Ok(args)
}

/// `event` command parsed shape: `event <房间> <事件名> <玩家|*> [数据...]`.
pub struct EventCommand {
    pub room_id: String,
    pub ename: String,
    pub player_index: i32,
    pub evalue: String,
}

pub fn parse_event_player(s: &str) -> Result<i32, String> {
    if s == "*" || s == "全部" || s == "-1" {
        return Ok(-1);
    }
    s.parse::<i32>()
        .map_err(|_| format!("玩家必须是整数或 * (得到: {})", s))
}

/// `args[0]` must be the literal `"event"`.
pub fn parse_event_command(args: &[String]) -> Result<EventCommand, String> {
    if args.len() < 4 {
        return Err("用法: event <房间> <事件名> <玩家|*> [数据...]".into());
    }
    let room_id = args[1].clone();
    if room_id.is_empty() {
        return Err("房间ID不能为空".into());
    }
    let ename = args[2].clone();
    if ename.is_empty() {
        return Err("事件名不能为空".into());
    }
    let player_index = parse_event_player(&args[3])?;
    let evalue = if args.len() > 4 {
        args[4..].join(" ")
    } else {
        String::new()
    };
    Ok(EventCommand {
        room_id,
        ename,
        player_index,
        evalue,
    })
}

/// Split input for completion purposes.
/// Returns (already-completed-tokens, in-progress-partial).
/// If the input ends with whitespace, `partial` is empty and `tokens` includes everything.
pub fn tokens_for_completion(input: &str) -> (Vec<String>, String) {
    if input.is_empty() {
        return (Vec::new(), String::new());
    }

    let ends_with_ws = input
        .chars()
        .last()
        .map(|c| c.is_whitespace())
        .unwrap_or(false);

    // Fall back to naive split if quoting is unclosed.
    let tokens = split_command_args(input).unwrap_or_else(|_| {
        input.split_whitespace().map(String::from).collect()
    });

    if ends_with_ws {
        (tokens, String::new())
    } else if let Some((last, prefix)) = tokens.split_last() {
        (prefix.to_vec(), last.clone())
    } else {
        (Vec::new(), String::new())
    }
}

/// Compute completion candidates for the current input.
///
/// - `rooms`: known room IDs.
/// - `players_in_room`: returns player indices for a given room id.
/// - `recent_events`: most recently used event names (newest first).
pub fn compute_completions(
    input: &str,
    rooms: &[String],
    players_in_room: impl Fn(&str) -> Vec<i32>,
    recent_events: &VecDeque<String>,
) -> Vec<String> {
    let (tokens, partial) = tokens_for_completion(input);
    let partial_lower = partial.to_ascii_lowercase();

    let raw: Vec<String> = if tokens.is_empty() {
        COMMAND_HINTS.iter().map(|h| h.name.to_string()).collect()
    } else {
        let cmd = tokens[0].as_str();
        let position = tokens.len(); // position of the partial token

        match (cmd, position) {
            // Position 1: room id for these commands
            (
                "select" | "stop" | "destroy" | "restart" | "join" | "leave" | "exit" | "event",
                1,
            ) => rooms.to_vec(),

            // Position 2: player index for join/leave/exit
            ("join" | "leave" | "exit", 2) => players_in_room(&tokens[1])
                .into_iter()
                .map(|i| i.to_string())
                .collect(),

            // Position 2 for event: recent ename history
            ("event", 2) => recent_events.iter().cloned().collect(),

            // Position 3 for event: * + player indices
            ("event", 3) => {
                let mut v = vec!["*".to_string()];
                v.extend(players_in_room(&tokens[1]).into_iter().map(|i| i.to_string()));
                v
            }

            _ => Vec::new(),
        }
    };

    if partial.is_empty() {
        raw
    } else {
        raw.into_iter()
            .filter(|c| c.to_ascii_lowercase().starts_with(&partial_lower))
            .collect()
    }
}

/// Replace the in-progress token in `input` with `candidate` and append a trailing space.
pub fn apply_completion(input: &str, candidate: &str) -> String {
    let (tokens, _) = tokens_for_completion(input);
    let mut out = String::new();
    for (i, tok) in tokens.iter().enumerate() {
        if i > 0 {
            out.push(' ');
        }
        // Re-quote if the token contains spaces
        if tok.contains(' ') {
            out.push('"');
            out.push_str(tok);
            out.push('"');
        } else {
            out.push_str(tok);
        }
    }
    if !tokens.is_empty() {
        out.push(' ');
    }
    if candidate.contains(' ') {
        out.push('"');
        out.push_str(candidate);
        out.push('"');
    } else {
        out.push_str(candidate);
    }
    out.push(' ');
    out
}

/// Match COMMAND_HINTS by prefix of the first token (used for the popup display).
pub fn matching_command_hints(input: &str) -> Vec<&'static CommandHint> {
    let trimmed = input.trim_start();
    if trimmed.is_empty() {
        return COMMAND_HINTS.iter().collect();
    }

    let command = trimmed
        .split_whitespace()
        .next()
        .unwrap_or_default()
        .to_ascii_lowercase();
    if command.is_empty() {
        return Vec::new();
    }

    if let Some(hint) = COMMAND_HINTS.iter().find(|hint| hint.name == command) {
        return vec![hint];
    }

    COMMAND_HINTS
        .iter()
        .filter(|hint| hint.name.starts_with(&command))
        .take(5)
        .collect()
}

// ─────────── Command history ───────────

/// Bash-like history with up/down navigation, draft preservation, and dedup.
pub struct CommandHistory {
    entries: VecDeque<String>,
    cursor: Option<usize>, // None = draft mode; Some(i) = at offset i from newest (0 = newest)
    draft: String,
}

impl CommandHistory {
    pub fn new() -> Self {
        Self {
            entries: VecDeque::new(),
            cursor: None,
            draft: String::new(),
        }
    }

    pub fn push(&mut self, cmd: String) {
        let trimmed = cmd.trim().to_string();
        if trimmed.is_empty() {
            return;
        }
        if self.entries.back().map(|s| s == &trimmed).unwrap_or(false) {
            // Skip consecutive duplicate
        } else {
            self.entries.push_back(trimmed);
            while self.entries.len() > MAX_HISTORY {
                self.entries.pop_front();
            }
        }
        self.reset_cursor();
    }

    pub fn reset_cursor(&mut self) {
        self.cursor = None;
        self.draft.clear();
    }

    #[cfg(test)]
    pub fn is_navigating(&self) -> bool {
        self.cursor.is_some()
    }

    /// `direction > 0` = older (↑); `direction < 0` = newer (↓).
    /// Returns the new text to display, or None if at boundary.
    pub fn navigate(&mut self, direction: i32, current_input: &str) -> Option<String> {
        if self.entries.is_empty() {
            return None;
        }
        let len = self.entries.len();

        match (self.cursor, direction) {
            (None, d) if d > 0 => {
                // First ↑: save draft, jump to newest entry
                self.draft = current_input.to_string();
                self.cursor = Some(0);
                Some(self.entries[len - 1].clone())
            }
            (None, _) => None, // ↓ from draft = no-op
            (Some(i), d) if d > 0 => {
                // ↑: go older (bounded)
                if i + 1 >= len {
                    return None;
                }
                let new_i = i + 1;
                self.cursor = Some(new_i);
                Some(self.entries[len - 1 - new_i].clone())
            }
            (Some(0), _) => {
                // ↓ at newest: restore draft
                self.cursor = None;
                Some(std::mem::take(&mut self.draft))
            }
            (Some(i), _) => {
                let new_i = i - 1;
                self.cursor = Some(new_i);
                Some(self.entries[len - 1 - new_i].clone())
            }
        }
    }

    #[cfg(test)]
    pub fn len(&self) -> usize {
        self.entries.len()
    }
}

// ─────────── Recent events ───────────

pub fn push_recent_event(deque: &mut VecDeque<String>, ename: &str) {
    let ename = ename.trim();
    if ename.is_empty() {
        return;
    }
    deque.retain(|e| e != ename);
    deque.push_front(ename.to_string());
    while deque.len() > MAX_RECENT_EVENTS {
        deque.pop_back();
    }
}

// ─────────── Tests ───────────

#[cfg(test)]
mod tests {
    use super::*;

    fn s(v: &[&str]) -> Vec<String> {
        v.iter().map(|s| s.to_string()).collect()
    }

    // split_command_args

    #[test]
    fn split_simple() {
        assert_eq!(split_command_args("a b c").unwrap(), s(&["a", "b", "c"]));
    }

    #[test]
    fn split_quoted() {
        assert_eq!(
            split_command_args("event r1 \"hello world\" *").unwrap(),
            s(&["event", "r1", "hello world", "*"])
        );
    }

    #[test]
    fn split_single_quoted() {
        assert_eq!(
            split_command_args("a 'b c' d").unwrap(),
            s(&["a", "b c", "d"])
        );
    }

    #[test]
    fn split_unclosed_quote() {
        assert!(split_command_args("a \"b").is_err());
    }

    #[test]
    fn split_empty() {
        assert_eq!(split_command_args("").unwrap(), Vec::<String>::new());
    }

    #[test]
    fn split_extra_whitespace() {
        assert_eq!(
            split_command_args("  a   b  ").unwrap(),
            s(&["a", "b"])
        );
    }

    // parse_event_command (the breaking-change parser)

    #[test]
    fn event_full_args() {
        let args = s(&["event", "r1", "attack", "2", "hello"]);
        let p = parse_event_command(&args).unwrap();
        assert_eq!(p.room_id, "r1");
        assert_eq!(p.ename, "attack");
        assert_eq!(p.player_index, 2);
        assert_eq!(p.evalue, "hello");
    }

    #[test]
    fn event_all_players_via_star() {
        let args = s(&["event", "r1", "broadcast", "*", "msg"]);
        let p = parse_event_command(&args).unwrap();
        assert_eq!(p.player_index, -1);
    }

    #[test]
    fn event_all_players_via_negative_one() {
        let args = s(&["event", "r1", "broadcast", "-1"]);
        assert_eq!(parse_event_command(&args).unwrap().player_index, -1);
    }

    #[test]
    fn event_empty_data() {
        let args = s(&["event", "r1", "ping", "0"]);
        let p = parse_event_command(&args).unwrap();
        assert_eq!(p.evalue, "");
    }

    #[test]
    fn event_numeric_data_not_swallowed_as_player() {
        // The whole point of the breaking change: data="123" is preserved.
        let args = s(&["event", "r1", "score", "0", "123"]);
        let p = parse_event_command(&args).unwrap();
        assert_eq!(p.player_index, 0);
        assert_eq!(p.evalue, "123");
    }

    #[test]
    fn event_multi_word_data() {
        let args = s(&["event", "r1", "say", "0", "hello", "world"]);
        let p = parse_event_command(&args).unwrap();
        assert_eq!(p.evalue, "hello world");
    }

    #[test]
    fn event_missing_player() {
        let args = s(&["event", "r1", "attack"]);
        assert!(parse_event_command(&args).is_err());
    }

    #[test]
    fn event_invalid_player() {
        let args = s(&["event", "r1", "attack", "abc"]);
        assert!(parse_event_command(&args).is_err());
    }

    #[test]
    fn event_zh_quan_bu() {
        let args = s(&["event", "r1", "ping", "全部"]);
        assert_eq!(parse_event_command(&args).unwrap().player_index, -1);
    }

    // tokens_for_completion

    #[test]
    fn tokens_empty() {
        assert_eq!(tokens_for_completion(""), (vec![], "".into()));
    }

    #[test]
    fn tokens_typing_command() {
        assert_eq!(tokens_for_completion("sel"), (vec![], "sel".into()));
    }

    #[test]
    fn tokens_after_command() {
        assert_eq!(
            tokens_for_completion("select "),
            (s(&["select"]), "".into())
        );
    }

    #[test]
    fn tokens_typing_arg() {
        assert_eq!(
            tokens_for_completion("select r"),
            (s(&["select"]), "r".into())
        );
    }

    // compute_completions

    fn no_players(_: &str) -> Vec<i32> {
        vec![]
    }

    #[test]
    fn complete_command_names() {
        let rooms = s(&[]);
        let recent = VecDeque::new();
        let r = compute_completions("", &rooms, no_players, &recent);
        assert!(r.contains(&"rooms".to_string()));
        assert!(r.contains(&"event".to_string()));
    }

    #[test]
    fn complete_command_prefix() {
        let rooms = s(&[]);
        let recent = VecDeque::new();
        let r = compute_completions("se", &rooms, no_players, &recent);
        assert_eq!(r, s(&["select"]));
    }

    #[test]
    fn complete_room_id_after_select() {
        let rooms = s(&["r1", "r2", "alpha"]);
        let recent = VecDeque::new();
        let r = compute_completions("select ", &rooms, no_players, &recent);
        assert_eq!(r.len(), 3);
    }

    #[test]
    fn complete_room_id_filtered() {
        let rooms = s(&["r1", "r2", "alpha"]);
        let recent = VecDeque::new();
        let r = compute_completions("select r", &rooms, no_players, &recent);
        assert_eq!(r, s(&["r1", "r2"]));
    }

    #[test]
    fn complete_join_player_index() {
        let rooms = s(&["r1"]);
        let recent = VecDeque::new();
        let r = compute_completions(
            "join r1 ",
            &rooms,
            |r| if r == "r1" { vec![0, 1, 2] } else { vec![] },
            &recent,
        );
        assert_eq!(r, s(&["0", "1", "2"]));
    }

    #[test]
    fn complete_event_player_includes_star() {
        let rooms = s(&["r1"]);
        let recent = VecDeque::new();
        let r = compute_completions(
            "event r1 attack ",
            &rooms,
            |r| if r == "r1" { vec![0, 1] } else { vec![] },
            &recent,
        );
        assert_eq!(r, s(&["*", "0", "1"]));
    }

    #[test]
    fn complete_event_ename_from_recent() {
        let rooms = s(&["r1"]);
        let mut recent = VecDeque::new();
        push_recent_event(&mut recent, "attack");
        push_recent_event(&mut recent, "heal");
        let r = compute_completions("event r1 ", &rooms, no_players, &recent);
        assert_eq!(r, s(&["heal", "attack"])); // newest first
    }

    // apply_completion

    #[test]
    fn apply_first_token() {
        assert_eq!(apply_completion("sel", "select"), "select ");
    }

    #[test]
    fn apply_room_after_command() {
        assert_eq!(apply_completion("select ", "r1"), "select r1 ");
    }

    #[test]
    fn apply_replaces_partial() {
        assert_eq!(apply_completion("select r", "r2"), "select r2 ");
    }

    #[test]
    fn apply_quoted_candidate() {
        assert_eq!(apply_completion("event ", "my room"), "event \"my room\" ");
    }

    // CommandHistory

    #[test]
    fn history_push_and_navigate() {
        let mut h = CommandHistory::new();
        h.push("a".into());
        h.push("b".into());
        h.push("c".into());
        assert_eq!(h.navigate(1, "draft").as_deref(), Some("c"));
        assert_eq!(h.navigate(1, "").as_deref(), Some("b"));
        assert_eq!(h.navigate(1, "").as_deref(), Some("a"));
        assert_eq!(h.navigate(1, ""), None); // boundary
    }

    #[test]
    fn history_down_returns_draft() {
        let mut h = CommandHistory::new();
        h.push("a".into());
        h.push("b".into());
        assert_eq!(h.navigate(1, "my-draft").as_deref(), Some("b"));
        assert_eq!(h.navigate(-1, "").as_deref(), Some("my-draft"));
        // After draft restore, further ↓ does nothing
        assert_eq!(h.navigate(-1, ""), None);
    }

    #[test]
    fn history_dedup_consecutive() {
        let mut h = CommandHistory::new();
        h.push("a".into());
        h.push("a".into());
        h.push("a".into());
        assert_eq!(h.len(), 1);
    }

    #[test]
    fn history_allows_nondup_repeat() {
        let mut h = CommandHistory::new();
        h.push("a".into());
        h.push("b".into());
        h.push("a".into());
        assert_eq!(h.len(), 3);
    }

    #[test]
    fn history_caps_at_max() {
        let mut h = CommandHistory::new();
        for i in 0..(MAX_HISTORY + 10) {
            h.push(format!("cmd{}", i));
        }
        assert_eq!(h.len(), MAX_HISTORY);
    }

    #[test]
    fn history_empty_navigate() {
        let mut h = CommandHistory::new();
        assert_eq!(h.navigate(1, ""), None);
        assert_eq!(h.navigate(-1, ""), None);
    }

    #[test]
    fn history_push_resets_cursor() {
        let mut h = CommandHistory::new();
        h.push("a".into());
        h.push("b".into());
        h.navigate(1, ""); // now at "b"
        assert!(h.is_navigating());
        h.push("c".into());
        assert!(!h.is_navigating());
    }

    // push_recent_event

    #[test]
    fn recent_events_moves_to_front() {
        let mut q = VecDeque::new();
        push_recent_event(&mut q, "a");
        push_recent_event(&mut q, "b");
        push_recent_event(&mut q, "a"); // touch a again
        assert_eq!(q.iter().cloned().collect::<Vec<_>>(), vec!["a", "b"]);
    }

    #[test]
    fn recent_events_caps() {
        let mut q = VecDeque::new();
        for i in 0..(MAX_RECENT_EVENTS + 5) {
            push_recent_event(&mut q, &format!("ev{}", i));
        }
        assert_eq!(q.len(), MAX_RECENT_EVENTS);
    }

    #[test]
    fn recent_events_ignores_empty() {
        let mut q = VecDeque::new();
        push_recent_event(&mut q, "");
        push_recent_event(&mut q, "  ");
        assert!(q.is_empty());
    }
}
