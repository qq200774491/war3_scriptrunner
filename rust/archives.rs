use anyhow::{Context, Result};
use serde_json::json;
use std::{
    collections::BTreeMap,
    fs,
    io::Write,
    path::{Path, PathBuf},
};

pub fn atomic_write(path: &Path, bytes: &[u8]) -> Result<()> {
    let parent = path.parent().context("没有文件目录")?;
    fs::create_dir_all(parent)?;
    let mut pending = tempfile::NamedTempFile::new_in(parent)?;
    pending.write_all(bytes)?;
    pending.as_file().sync_all()?;
    pending.persist(path).map_err(|e| e.error)?;
    Ok(())
}

#[derive(Clone, Default)]
pub struct Ini {
    sections: BTreeMap<String, BTreeMap<String, String>>,
}
impl Ini {
    pub fn parse(bytes: &[u8]) -> Result<Self> {
        let text = if bytes.starts_with(&[255, 254]) || bytes.starts_with(&[254, 255]) {
            anyhow::ensure!(bytes.len().is_multiple_of(2), "INI UTF-16 不完整");
            let little = bytes[0] == 255;
            let chars: Vec<u16> = bytes[2..]
                .chunks_exact(2)
                .map(|c| {
                    if little {
                        u16::from_le_bytes([c[0], c[1]])
                    } else {
                        u16::from_be_bytes([c[0], c[1]])
                    }
                })
                .collect();
            String::from_utf16(&chars)?
        } else if let Some(bytes) = bytes.strip_prefix(&[239, 187, 191]) {
            String::from_utf8(bytes.to_vec())?
        } else {
            crate::platform::decode_ansi(bytes)?
        };
        Self::parse_text(&text)
    }
    pub fn parse_text(text: &str) -> Result<Self> {
        anyhow::ensure!(!text.contains('\0'), "INI 包含空字符");
        let mut result = Self::default();
        let mut section = String::new();
        for original in text.lines() {
            let line = original.trim();
            if line.is_empty() || line.starts_with(';') {
                continue;
            }
            if let Some(name) = line.strip_prefix('[').and_then(|s| s.strip_suffix(']')) {
                anyhow::ensure!(
                    !name.is_empty() && !name.eq_ignore_ascii_case("DEFAULT"),
                    "INI 不支持空节或 DEFAULT 继承"
                );
                anyhow::ensure!(
                    !result.sections.keys().any(|k| k.eq_ignore_ascii_case(name)),
                    "INI 重复节：{name}"
                );
                section = name.into();
                result.sections.insert(section.clone(), BTreeMap::new());
                continue;
            }
            let (key, value) = line.split_once('=').context("INI 包含非法行或多行值")?;
            let key = key.trim();
            anyhow::ensure!(!key.is_empty(), "INI 空键");
            let fields = result
                .sections
                .get_mut(&section)
                .context("INI 字段不在任何节内")?;
            anyhow::ensure!(
                !fields.keys().any(|k| k.eq_ignore_ascii_case(key)),
                "INI 重复键：{key}"
            );
            fields.insert(key.into(), value.trim().into());
        }
        Ok(result)
    }
    pub fn section_mut(&mut self, name: &str) -> &mut BTreeMap<String, String> {
        let key = self
            .sections
            .keys()
            .find(|key| key.eq_ignore_ascii_case(name))
            .cloned()
            .unwrap_or_else(|| name.into());
        self.sections.entry(key).or_default()
    }
    pub fn get(&self, section: &str, key: &str) -> Option<&str> {
        self.sections
            .iter()
            .find(|(name, _)| name.eq_ignore_ascii_case(section))?
            .1
            .iter()
            .find(|(name, _)| name.eq_ignore_ascii_case(key))
            .map(|(_, value)| value.as_str())
    }
    pub fn text(&self) -> String {
        let mut result = String::new();
        for (section, fields) in &self.sections {
            result.push_str(&format!("[{section}]\r\n"));
            for (key, value) in fields {
                result.push_str(&format!("{key}={value}\r\n"));
            }
            result.push_str("\r\n");
        }
        result
    }
    fn utf16(&self) -> Vec<u8> {
        [
            vec![255, 254],
            self.text()
                .encode_utf16()
                .flat_map(u16::to_le_bytes)
                .collect(),
        ]
        .concat()
    }
}

fn owner(key: &str) -> Option<usize> {
    let mut parts = key.splitn(3, '-');
    if !parts.next()?.eq_ignore_ascii_case("SSV") {
        return None;
    }
    let player = parts.next()?.parse().ok()?;
    if parts.next()?.is_empty() {
        return None;
    }
    Some(player)
}

pub fn prepare(folder: &Path, legacy: Option<&Path>) -> Result<[PathBuf; 2]> {
    fs::create_dir_all(folder)?;
    let paths = [folder.join("player-0.ini"), folder.join("player-1.ini")];
    let _locks: Vec<_> = paths
        .iter()
        .map(|path| crate::platform::exclusive_file(&path.with_extension("ini.lock")))
        .collect::<Result<_>>()?;
    let marker = folder.join("layout.json");
    let layout = if marker.exists() {
        serde_json::from_slice(&fs::read(&marker)?)?
    } else {
        serde_json::Value::Null
    };
    anyhow::ensure!(
        layout.is_null()
            || layout == json!({"version":1,"players":[0,1]})
            || layout == json!({"version":2,"players":[0,1]}),
        "未知客户端存档布局，已保留原文件"
    );
    let migrating = layout["version"] == 1;
    let backup = folder.join("legacy-dz_w3_plugin.ini.bak");
    let seed = if layout.is_null() && backup.exists() {
        fs::read(&backup)?
    } else if layout.is_null() && legacy.is_some_and(Path::is_file) {
        let bytes = fs::read(legacy.unwrap())?;
        Ini::parse(&bytes)?;
        atomic_write(&backup, &bytes)?;
        bytes
    } else {
        b"[DzAPI]\r\n".to_vec()
    };
    let mut documents = Vec::new();
    let mut owned = BTreeMap::new();
    // 所有权威槽位读完后才写快照；绝不能用队友的旧副本复活已删除的存档。
    for (player, path) in paths.iter().enumerate() {
        let source = if migrating {
            folder.join(format!("player-{player}/dz_w3_plugin.ini"))
        } else {
            path.clone()
        };
        let bytes = if !layout.is_null() || source.exists() {
            fs::read(&source)
                .with_context(|| format!("读取玩家 {player} 存档 {}", source.display()))?
        } else {
            seed.clone()
        };
        let mut ini = Ini::parse(&bytes)?;
        for (key, value) in ini.section_mut("DzAPI").iter() {
            if owner(key) == Some(player) {
                owned.insert(key.clone(), value.clone());
            }
        }
        documents.push((ini, bytes));
    }
    for (player, (mut ini, bytes)) in documents.into_iter().enumerate() {
        if migrating {
            let old = folder.join(format!("backups/v1-player-{player}.ini"));
            if !old.exists() {
                atomic_write(&old, &bytes)?;
            }
        }
        let fields = ini.section_mut("DzAPI");
        fields.retain(|key, _| !matches!(owner(key), Some(0 | 1)));
        fields.extend(owned.clone());
        atomic_write(&paths[player], &ini.utf16())?;
    }
    atomic_write(
        &marker,
        &serde_json::to_vec(&json!({"version":2,"players":[0,1]}))?,
    )?;
    Ok(paths)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rejects_ambiguous_ini() {
        for text in [
            "[DzAPI]\nx=1\nX=2",
            "[A]\n[A]\n",
            "[DEFAULT]\nx=1",
            "[A]\nx=1\ncontinued",
            "[A]\n[a]",
        ] {
            assert!(Ini::parse_text(text).is_err(), "{text}");
        }
    }
    #[test]
    fn snapshots_only_take_owner_data_and_preserve_settings() {
        let dir = tempfile::tempdir().unwrap();
        let paths = prepare(dir.path(), None).unwrap();
        atomic_write(
            &paths[0],
            &Ini::parse_text("[DzAPI]\nSSV-0-a=own0\nSSV-1-a=stale\nlevel=50")
                .unwrap()
                .utf16(),
        )
        .unwrap();
        atomic_write(
            &paths[1],
            &Ini::parse_text("[DzAPI]\nSSV-0-a=stale\nSSV-1-a=own1\nlevel=60")
                .unwrap()
                .utf16(),
        )
        .unwrap();
        prepare(dir.path(), None).unwrap();
        for (i, path) in paths.iter().enumerate() {
            let ini = Ini::parse(&fs::read(path).unwrap()).unwrap();
            assert_eq!(ini.get("DzAPI", "SSV-0-a"), Some("own0"));
            assert_eq!(ini.get("DzAPI", "SSV-1-a"), Some("own1"));
            assert_eq!(
                ini.get("DzAPI", "level"),
                Some(if i == 0 { "50" } else { "60" })
            );
        }
        fs::remove_file(&paths[0]).unwrap();
        let before = fs::read(&paths[1]).unwrap();
        assert!(prepare(dir.path(), None).is_err());
        assert_eq!(before, fs::read(&paths[1]).unwrap());
    }
    #[test]
    fn live_ini_lock_blocks_snapshot() {
        let dir = tempfile::tempdir().unwrap();
        let paths = prepare(dir.path(), None).unwrap();
        let _lock = crate::platform::exclusive_file(&paths[1].with_extension("ini.lock")).unwrap();
        assert!(prepare(dir.path(), None).is_err());
    }
}
