//! 云脚本全局存档（MsGA*）与地图广播（MsBroadcast）的本地模拟。
//!
//! 同一个 `GlobalArchiveStore` 由同一 `RoomManager` 内相同 `script_dir` 的所有房间共享，
//! 对应平台上的 `map_id` 级共享；测试模式每个用例拥有独立的 manager，因此天然隔离。
//! 所有存档 / 锁操作都立即返回 `trans_id`，结果经房间命令队列异步投递 `_msga_reply`，
//! 绝不在调用栈内同步回调，以贴合真实平台"先返回 trans_id、再登记 pending"的时序。

use super::{CommandSender, RoomCommand};
use serde_json::{json, Value};
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

pub const GA_ERR_OK: i32 = 0;
pub const GA_ERR_INVALID_ARGS: i32 = 1;
pub const GA_ERR_STORAGE: i32 = 2;
pub const GA_ERR_RESULT_TOO_LARGE: i32 = 3;
pub const GA_ERR_INTERNAL: i32 = 4;

pub const GA_KEY_MAX_BYTES: usize = 256;
pub const GA_VALUE_MAX_BYTES: usize = 4096;
pub const GA_HMSET_TOTAL_MAX_BYTES: usize = 40960;
pub const GA_RESULT_MAX_BYTES: usize = 65536;
pub const GA_LOCK_TTL_MIN_MS: i64 = 1000;
pub const GA_LOCK_TTL_MAX_MS: i64 = 300_000;
pub const GA_BROADCAST_KEY_MAX_BYTES: usize = 128;
pub const GA_BROADCAST_VALUE_MAX_BYTES: usize = 4096;

pub const GA_REPLY_EVENT: &str = "_msga_reply";
pub const GA_BROADCAST_EVENT: &str = "_msbroadcast";

pub type SharedGlobalArchive = Arc<Mutex<GlobalArchiveStore>>;

/// 故障注入规则：匹配的操作以指定 errnu 回包，`remaining` 为 None 时持续生效。
#[derive(Debug, Clone)]
pub struct GlobalArchiveFault {
    pub op: Option<String>,
    pub errnu: i32,
    pub remaining: Option<u32>,
}

#[derive(Debug, Clone)]
struct LockEntry {
    token: String,
    expires_at: Instant,
}

#[derive(Default)]
pub struct GlobalArchiveStore {
    kv: HashMap<String, String>,
    hashes: HashMap<String, HashMap<String, String>>,
    locks: HashMap<String, LockEntry>,
    next_token: u64,
    subscribers: Vec<(String, CommandSender)>,
    reply_delay: Duration,
    fault: Option<GlobalArchiveFault>,
}

// CommandSender 没有 Debug；诊断输出只需要数据规模与订阅房间数。
impl std::fmt::Debug for GlobalArchiveStore {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("GlobalArchiveStore")
            .field("kv", &self.kv.len())
            .field("hashes", &self.hashes.len())
            .field("locks", &self.locks.len())
            .field(
                "subscribers",
                &self
                    .subscribers
                    .iter()
                    .map(|(id, _)| id.as_str())
                    .collect::<Vec<_>>(),
            )
            .field("reply_delay", &self.reply_delay)
            .field("fault", &self.fault)
            .finish()
    }
}

/// 一次存档 / 锁操作的结果：平台错误码与 `data` 字段。
pub struct GaOutcome {
    pub errnu: i32,
    pub data: Value,
}

impl GaOutcome {
    fn ok(data: Value) -> Self {
        Self {
            errnu: GA_ERR_OK,
            data,
        }
    }

    fn fail(op: &str, errnu: i32) -> Self {
        // 平台文档：失败时 data 大多为 null，MsGASet / MsGALock* 给 {"ok": false}
        let data = match op {
            "MsGASet" | "MsGALockAcquire" | "MsGALockRelease" => json!({"ok": false}),
            _ => Value::Null,
        };
        Self { errnu, data }
    }
}

fn key_valid(key: &str) -> bool {
    !key.is_empty() && key.len() <= GA_KEY_MAX_BYTES
}

fn parse_counter(text: Option<&String>) -> Option<i64> {
    match text {
        None => Some(0),
        Some(value) => value.trim().parse::<i64>().ok(),
    }
}

fn result_too_large(data: &Value) -> bool {
    data.to_string().len() > GA_RESULT_MAX_BYTES
}

impl GlobalArchiveStore {
    pub fn subscribe(&mut self, room_id: String, sender: CommandSender) {
        self.subscribers.retain(|(id, _)| id != &room_id);
        self.subscribers.push((room_id, sender));
    }

    pub fn unsubscribe(&mut self, room_id: &str) {
        self.subscribers.retain(|(id, _)| id != room_id);
    }

    pub fn subscriber_count(&self) -> usize {
        self.subscribers.len()
    }

    pub fn set_reply_delay(&mut self, delay: Duration) {
        self.reply_delay = delay;
    }

    pub fn set_fault(&mut self, fault: Option<GlobalArchiveFault>) {
        self.fault = fault;
    }

    /// 命中故障规则时消耗一次并返回要注入的 errnu。
    fn take_fault(&mut self, op: &str) -> Option<i32> {
        let fault = self.fault.as_mut()?;
        if fault.op.as_deref().is_some_and(|target| target != op) {
            return None;
        }
        let errnu = fault.errnu;
        match fault.remaining.as_mut() {
            Some(remaining) => {
                *remaining = remaining.saturating_sub(1);
                if *remaining == 0 {
                    self.fault = None;
                }
            }
            None => {}
        }
        Some(errnu)
    }

    /// 平台侧的通用执行入口：先套故障注入，再做参数校验与真实读写。
    pub fn execute(&mut self, op: &str, args: GaArgs) -> GaOutcome {
        if let Some(errnu) = self.take_fault(op) {
            return GaOutcome::fail(op, errnu);
        }
        match args {
            GaArgs::Set { key, value } => self.set(&key, value),
            GaArgs::Get { key } => self.get(&key),
            GaArgs::Del { key } => self.del(&key),
            GaArgs::Incrby { key, delta } => self.incrby(&key, delta),
            GaArgs::HmSet { key, kvs } => self.hmset(&key, kvs),
            GaArgs::HmGet { key, fields } => self.hmget(&key, &fields),
            GaArgs::HGetAll { key } => self.hgetall(&key),
            GaArgs::HIncrby { key, field, delta } => self.hincrby(&key, &field, delta),
            GaArgs::HDel { key, fields } => self.hdel(&key, &fields),
            GaArgs::LockAcquire { key, ttl_ms } => self.lock_acquire(&key, ttl_ms),
            GaArgs::LockRelease { key } => self.lock_release(&key),
        }
    }

    pub fn set(&mut self, key: &str, value: String) -> GaOutcome {
        if !key_valid(key) || value.len() > GA_VALUE_MAX_BYTES {
            return GaOutcome::fail("MsGASet", GA_ERR_INVALID_ARGS);
        }
        self.kv.insert(key.to_string(), value);
        GaOutcome::ok(json!({"ok": true}))
    }

    pub fn get(&self, key: &str) -> GaOutcome {
        if !key_valid(key) {
            return GaOutcome::fail("MsGAGet", GA_ERR_INVALID_ARGS);
        }
        let data = json!({"value": self.kv.get(key).cloned()});
        if result_too_large(&data) {
            return GaOutcome::fail("MsGAGet", GA_ERR_RESULT_TOO_LARGE);
        }
        GaOutcome::ok(data)
    }

    pub fn del(&mut self, key: &str) -> GaOutcome {
        if !key_valid(key) {
            return GaOutcome::fail("MsGADel", GA_ERR_INVALID_ARGS);
        }
        let removed = self.kv.remove(key).is_some();
        GaOutcome::ok(json!({"value": if removed { 1 } else { 0 }}))
    }

    pub fn incrby(&mut self, key: &str, delta: i64) -> GaOutcome {
        if !key_valid(key) {
            return GaOutcome::fail("MsGAIncrby", GA_ERR_INVALID_ARGS);
        }
        let Some(current) = parse_counter(self.kv.get(key)) else {
            return GaOutcome::fail("MsGAIncrby", GA_ERR_INVALID_ARGS);
        };
        let Some(next) = current.checked_add(delta) else {
            return GaOutcome::fail("MsGAIncrby", GA_ERR_INVALID_ARGS);
        };
        self.kv.insert(key.to_string(), next.to_string());
        GaOutcome::ok(json!({"value": next}))
    }

    pub fn hmset(&mut self, key: &str, kvs: Vec<(String, String)>) -> GaOutcome {
        if !key_valid(key) || kvs.is_empty() {
            return GaOutcome::fail("MsGAHmSet", GA_ERR_INVALID_ARGS);
        }
        let mut total = 0usize;
        for (field, value) in &kvs {
            if !key_valid(field) || value.len() > GA_VALUE_MAX_BYTES {
                return GaOutcome::fail("MsGAHmSet", GA_ERR_INVALID_ARGS);
            }
            total += value.len();
        }
        if total > GA_HMSET_TOTAL_MAX_BYTES {
            return GaOutcome::fail("MsGAHmSet", GA_ERR_INVALID_ARGS);
        }
        let count = kvs.len();
        let hash = self.hashes.entry(key.to_string()).or_default();
        for (field, value) in kvs {
            hash.insert(field, value);
        }
        GaOutcome::ok(json!({"value": count}))
    }

    pub fn hmget(&self, key: &str, fields: &[String]) -> GaOutcome {
        if !key_valid(key) || fields.is_empty() || fields.iter().any(|field| !key_valid(field)) {
            return GaOutcome::fail("MsGAHmGet", GA_ERR_INVALID_ARGS);
        }
        let hash = self.hashes.get(key);
        let mut value = serde_json::Map::new();
        for field in fields {
            value.insert(
                field.clone(),
                hash.and_then(|hash| hash.get(field))
                    .map(|text| Value::String(text.clone()))
                    .unwrap_or(Value::Null),
            );
        }
        let data = json!({"value": Value::Object(value)});
        if result_too_large(&data) {
            return GaOutcome::fail("MsGAHmGet", GA_ERR_RESULT_TOO_LARGE);
        }
        GaOutcome::ok(data)
    }

    pub fn hgetall(&self, key: &str) -> GaOutcome {
        if !key_valid(key) {
            return GaOutcome::fail("MsGAHGetAll", GA_ERR_INVALID_ARGS);
        }
        let value = self
            .hashes
            .get(key)
            .map(|hash| {
                hash.iter()
                    .map(|(field, text)| (field.clone(), Value::String(text.clone())))
                    .collect::<serde_json::Map<String, Value>>()
            })
            .unwrap_or_default();
        let data = json!({"value": Value::Object(value)});
        if result_too_large(&data) {
            return GaOutcome::fail("MsGAHGetAll", GA_ERR_RESULT_TOO_LARGE);
        }
        GaOutcome::ok(data)
    }

    pub fn hincrby(&mut self, key: &str, field: &str, delta: i64) -> GaOutcome {
        if !key_valid(key) || !key_valid(field) {
            return GaOutcome::fail("MsGAHIncrby", GA_ERR_INVALID_ARGS);
        }
        let current = parse_counter(self.hashes.get(key).and_then(|hash| hash.get(field)));
        let Some(current) = current else {
            return GaOutcome::fail("MsGAHIncrby", GA_ERR_INVALID_ARGS);
        };
        let Some(next) = current.checked_add(delta) else {
            return GaOutcome::fail("MsGAHIncrby", GA_ERR_INVALID_ARGS);
        };
        self.hashes
            .entry(key.to_string())
            .or_default()
            .insert(field.to_string(), next.to_string());
        GaOutcome::ok(json!({"value": next}))
    }

    pub fn hdel(&mut self, key: &str, fields: &[String]) -> GaOutcome {
        if !key_valid(key) || fields.is_empty() || fields.iter().any(|field| !key_valid(field)) {
            return GaOutcome::fail("MsGAHDel", GA_ERR_INVALID_ARGS);
        }
        let mut removed = 0usize;
        if let Some(hash) = self.hashes.get_mut(key) {
            for field in fields {
                if hash.remove(field).is_some() {
                    removed += 1;
                }
            }
            // 平台约定：所有字段删完后整个 hash key 被回收
            if hash.is_empty() {
                self.hashes.remove(key);
            }
        }
        GaOutcome::ok(json!({"value": removed}))
    }

    pub fn lock_acquire(&mut self, key: &str, ttl_ms: i64) -> GaOutcome {
        if !key_valid(key) {
            return GaOutcome::fail("MsGALockAcquire", GA_ERR_INVALID_ARGS);
        }
        let ttl_ms = ttl_ms.clamp(GA_LOCK_TTL_MIN_MS, GA_LOCK_TTL_MAX_MS);
        let now = Instant::now();
        let held = self
            .locks
            .get(key)
            .is_some_and(|entry| entry.expires_at > now);
        if held {
            return GaOutcome::ok(json!({"ok": false, "token": "", "ttl_ms": ttl_ms}));
        }
        self.next_token += 1;
        let token = format!("sim-lock-{}", self.next_token);
        self.locks.insert(
            key.to_string(),
            LockEntry {
                token: token.clone(),
                expires_at: now + Duration::from_millis(ttl_ms as u64),
            },
        );
        GaOutcome::ok(json!({"ok": true, "token": token, "ttl_ms": ttl_ms}))
    }

    /// 与平台一致：裸删，不校验持有者。
    pub fn lock_release(&mut self, key: &str) -> GaOutcome {
        if !key_valid(key) {
            return GaOutcome::fail("MsGALockRelease", GA_ERR_INVALID_ARGS);
        }
        self.locks.remove(key);
        GaOutcome::ok(json!({"ok": true}))
    }

    /// 测试侧只读快照：kv、hash 与仍在有效期内的锁 token。
    pub fn dump(&self) -> Value {
        let now = Instant::now();
        let locks = self
            .locks
            .iter()
            .filter(|(_, entry)| entry.expires_at > now)
            .map(|(key, entry)| (key.clone(), Value::String(entry.token.clone())))
            .collect::<serde_json::Map<String, Value>>();
        json!({
            "kv": self.kv,
            "hashes": self.hashes,
            "locks": Value::Object(locks),
        })
    }

    /// 向房间异步投递 `_msga_reply`；有延迟时由独立线程等待后再入队，房间已销毁则静默丢弃。
    pub fn deliver_reply(
        &self,
        sender: &CommandSender,
        op: &str,
        trans_id: i32,
        outcome: GaOutcome,
    ) {
        let payload = json!({
            "errnu": outcome.errnu,
            "op": op,
            "trans_id": trans_id,
            "data": outcome.data,
        })
        .to_string();
        let command = RoomCommand::DispatchEvent {
            ename: GA_REPLY_EVENT.to_string(),
            evalue: payload,
            player_index: -1,
        };
        if self.reply_delay.is_zero() {
            let _ = sender.send(command);
            return;
        }
        let delay = self.reply_delay;
        let sender = sender.clone();
        std::thread::spawn(move || {
            std::thread::sleep(delay);
            let _ = sender.send(command);
        });
    }

    /// 广播到同一作用域内所有房间（含发起方）；超限直接丢弃，与平台一致无回执。
    pub fn broadcast(&self, key: &str, value: &str) -> bool {
        if key.is_empty()
            || key.len() > GA_BROADCAST_KEY_MAX_BYTES
            || value.len() > GA_BROADCAST_VALUE_MAX_BYTES
        {
            return false;
        }
        let payload = json!({"key": key, "value": value}).to_string();
        for (_, sender) in &self.subscribers {
            let _ = sender.send(RoomCommand::DispatchEvent {
                ename: GA_BROADCAST_EVENT.to_string(),
                evalue: payload.clone(),
                player_index: -1,
            });
        }
        true
    }
}

/// 各平台 API 的参数集合；由 Lua 绑定层解析后交给 `GlobalArchiveStore::execute`。
pub enum GaArgs {
    Set {
        key: String,
        value: String,
    },
    Get {
        key: String,
    },
    Del {
        key: String,
    },
    Incrby {
        key: String,
        delta: i64,
    },
    HmSet {
        key: String,
        kvs: Vec<(String, String)>,
    },
    HmGet {
        key: String,
        fields: Vec<String>,
    },
    HGetAll {
        key: String,
    },
    HIncrby {
        key: String,
        field: String,
        delta: i64,
    },
    HDel {
        key: String,
        fields: Vec<String>,
    },
    LockAcquire {
        key: String,
        ttl_ms: i64,
    },
    LockRelease {
        key: String,
    },
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn incrby_rejects_non_integer_values() {
        let mut store = GlobalArchiveStore::default();
        assert_eq!(store.set("k", "abc".into()).errnu, GA_ERR_OK);
        assert_eq!(store.incrby("k", 1).errnu, GA_ERR_INVALID_ARGS);
        assert_eq!(store.incrby("n", 5).data, json!({"value": 5}));
        assert_eq!(store.incrby("n", -7).data, json!({"value": -2}));
    }

    #[test]
    fn hdel_recycles_empty_hash() {
        let mut store = GlobalArchiveStore::default();
        store.hmset(
            "h",
            vec![("a".into(), "1".into()), ("b".into(), "2".into())],
        );
        assert_eq!(
            store.hdel("h", &["a".into(), "zzz".into()]).data,
            json!({"value": 1})
        );
        assert!(store.hashes.contains_key("h"));
        store.hdel("h", &["b".into()]);
        assert!(!store.hashes.contains_key("h"));
        assert_eq!(store.hgetall("h").data, json!({"value": {}}));
    }

    #[test]
    fn lock_is_exclusive_until_released_or_expired() {
        let mut store = GlobalArchiveStore::default();
        let first = store.lock_acquire("l", 10);
        assert_eq!(first.data["ok"], json!(true));
        assert_eq!(first.data["ttl_ms"], json!(GA_LOCK_TTL_MIN_MS));
        assert_eq!(store.lock_acquire("l", 5000).data["ok"], json!(false));
        store.lock_release("l");
        assert_eq!(store.lock_acquire("l", 5000).data["ok"], json!(true));
    }

    #[test]
    fn fault_applies_to_matching_op_and_counts_down() {
        let mut store = GlobalArchiveStore::default();
        store.set_fault(Some(GlobalArchiveFault {
            op: Some("MsGAGet".into()),
            errnu: GA_ERR_STORAGE,
            remaining: Some(1),
        }));
        assert_eq!(
            store
                .execute(
                    "MsGASet",
                    GaArgs::Set {
                        key: "k".into(),
                        value: "v".into()
                    }
                )
                .errnu,
            GA_ERR_OK
        );
        let failed = store.execute("MsGAGet", GaArgs::Get { key: "k".into() });
        assert_eq!(failed.errnu, GA_ERR_STORAGE);
        assert_eq!(failed.data, Value::Null);
        assert_eq!(
            store
                .execute("MsGAGet", GaArgs::Get { key: "k".into() })
                .data,
            json!({"value": "v"})
        );
    }
}
