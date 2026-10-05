use anyhow::{Context, Result};
use rusqlite::{params, Connection, OptionalExtension};
use serde_json::{json, Map, Value};
use std::{
    collections::HashMap,
    path::Path,
    time::{Duration, Instant},
};

pub struct GlobalArchive {
    database: Connection,
    locks: HashMap<String, Instant>,
}
impl GlobalArchive {
    pub fn open(directory: &Path) -> Result<Self> {
        let database = Connection::open(directory.join("global.sqlite3"))?;
        database.execute_batch("PRAGMA synchronous=FULL; CREATE TABLE IF NOT EXISTS fields (key TEXT NOT NULL, field TEXT NOT NULL, value TEXT NOT NULL, PRIMARY KEY(key,field));")?;
        Ok(Self {
            database,
            locks: HashMap::new(),
        })
    }
    pub fn request(
        &mut self,
        transaction: u32,
        operation: &str,
        key: &str,
        argument: &str,
    ) -> Result<(String, Value)> {
        let answer = self.checked_request(operation, key, argument);
        let data = match answer {
            Ok(data) => data,
            Err(error)
                if error.downcast_ref::<rusqlite::Error>().is_some()
                    || error.downcast_ref::<std::io::Error>().is_some() =>
            {
                return Err(error)
            }
            Err(error) => {
                return Ok((
                    "_msga_reply".into(),
                    json!({"errnu":-1,"op":operation,"trans_id":transaction,"data":{},"local_error":error.to_string()}),
                ))
            }
        };
        if operation == "MsBroadcast" {
            return Ok(("_msbroadcast".into(), data));
        }
        Ok((
            "_msga_reply".into(),
            json!({"errnu":0,"op":operation,"trans_id":transaction,"data":data}),
        ))
    }
    fn checked_request(&mut self, operation: &str, key: &str, argument: &str) -> Result<Value> {
        checked(key, 256)?;
        anyhow::ensure!(!key.is_empty(), "empty global archive key");
        match operation {
            "MsBroadcast" => {
                checked(argument, 4096)?;
                Ok(json!({"key":key,"value":argument}))
            }
            "MsGALockAcquire" | "MsGALockRelease" => self.lease(operation, key, argument),
            "MsGAHmSet" | "MsGAHmGet" | "MsGAHGetAll" | "MsGAHDel" => {
                self.hash(operation, key, argument)
            }
            _ => anyhow::bail!("unsupported local global archive operation"),
        }
    }
    fn lease(&mut self, operation: &str, key: &str, argument: &str) -> Result<Value> {
        let exists: bool = self.database.query_row(
            "SELECT EXISTS(SELECT 1 FROM fields WHERE key=?)",
            [key],
            |row| row.get(0),
        )?;
        anyhow::ensure!(!exists, "key is a hash, not a lock");
        if operation == "MsGALockRelease" {
            self.locks.remove(key);
            return Ok(json!({"ok":true}));
        }
        let ttl = argument.trim().parse::<i64>()?.clamp(1000, 300000) as u64;
        let now = Instant::now();
        if self.locks.get(key).is_some_and(|expiry| *expiry > now) {
            return Ok(json!({"ok":false,"token":"","ttl_ms":ttl}));
        }
        self.locks
            .insert(key.into(), now + Duration::from_millis(ttl));
        Ok(json!({"ok":true,"token":hex::encode(rand::random::<[u8;16]>()),"ttl_ms":ttl}))
    }
    fn hash(&mut self, operation: &str, key: &str, argument: &str) -> Result<Value> {
        anyhow::ensure!(
            self.locks
                .get(key)
                .is_none_or(|expiry| *expiry <= Instant::now()),
            "key is a lock, not a hash"
        );
        let tx = self.database.transaction()?;
        let data = match operation {
            "MsGAHmSet" => {
                anyhow::ensure!(argument.len() <= 40 * 1024, "hash batch exceeds 40 KiB");
                let input: Value = serde_json::from_str(argument)?;
                let values = input
                    .as_object()
                    .filter(|v| !v.is_empty())
                    .context("expected nonempty hash object")?;
                for (field, value) in values {
                    let value = value.as_str().context("invalid global archive text")?;
                    checked(field, 256)?;
                    checked(value, 4096)?;
                    tx.execute("INSERT INTO fields VALUES (?,?,?) ON CONFLICT(key,field) DO UPDATE SET value=excluded.value",params![key,field,value])?;
                }
                json!({"value":values.len()})
            }
            "MsGAHmGet" => {
                let mut values = Map::new();
                for field in fields(argument)? {
                    let value: Option<String> = tx
                        .query_row(
                            "SELECT value FROM fields WHERE key=? AND field=?",
                            params![key, field],
                            |r| r.get(0),
                        )
                        .optional()?;
                    if let Some(value) = value {
                        values.insert(field, Value::String(value));
                    }
                }
                json!({"value":values})
            }
            "MsGAHGetAll" => {
                let mut statement =
                    tx.prepare("SELECT field,value FROM fields WHERE key=? ORDER BY field")?;
                let rows = statement.query_map([key], |r| {
                    Ok((r.get::<_, String>(0)?, r.get::<_, String>(1)?))
                })?;
                let mut values = Map::new();
                for row in rows {
                    let (field, value) = row?;
                    values.insert(field, Value::String(value));
                }
                json!({"value":values})
            }
            "MsGAHDel" => {
                let mut count = 0;
                for field in fields(argument)? {
                    count += tx.execute(
                        "DELETE FROM fields WHERE key=? AND field=?",
                        params![key, field],
                    )?;
                }
                json!({"value":count})
            }
            _ => unreachable!("operation validated at boundary"),
        };
        anyhow::ensure!(
            serde_json::to_vec(&data)?.len() <= 64 * 1024,
            "global archive result exceeds 64 KiB"
        );
        tx.commit()?;
        Ok(data)
    }
}
fn checked(text: &str, limit: usize) -> Result<()> {
    anyhow::ensure!(
        text.len() <= limit && !text.contains('\0'),
        "invalid global archive text"
    );
    Ok(())
}
fn fields(argument: &str) -> Result<Vec<String>> {
    let fields: Vec<String> = serde_json::from_str(argument)?;
    anyhow::ensure!(
        (1..=100).contains(&fields.len()),
        "expected 1..100 field names"
    );
    for field in &fields {
        checked(field, 256)?;
    }
    Ok(fields)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn persistence_validation_and_locks() {
        let directory = tempfile::tempdir().unwrap();
        let mut archive = GlobalArchive::open(directory.path()).unwrap();
        assert_eq!(
            archive
                .request(1, "MsGAHmSet", "k", r#"{"a":"你好"}"#)
                .unwrap()
                .1["errnu"],
            0
        );
        assert_eq!(
            archive
                .request(2, "MsGAHmSet", "k", r#"{"b":1}"#)
                .unwrap()
                .1["errnu"],
            -1
        );
        drop(archive);
        let mut archive = GlobalArchive::open(directory.path()).unwrap();
        assert_eq!(
            archive.request(3, "MsGAHGetAll", "k", "").unwrap().1["data"]["value"],
            json!({"a":"你好"})
        );
        assert_eq!(
            archive
                .request(4, "MsGALockAcquire", "k", "1000")
                .unwrap()
                .1["errnu"],
            -1
        );
        assert_eq!(
            archive
                .request(5, "MsGALockAcquire", "lock", "1000")
                .unwrap()
                .1["data"]["ok"],
            true
        );
        assert_eq!(
            archive.request(6, "MsGAHGetAll", "lock", "").unwrap().1["errnu"],
            -1
        );
        assert_eq!(
            archive
                .request(7, "MsGALockAcquire", "lock", "1000")
                .unwrap()
                .1["data"]["ok"],
            false
        );
    }
}
