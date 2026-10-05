use anyhow::{Context, Result};
use serde::{Deserialize, Serialize};
use std::{
    fs,
    path::{Path, PathBuf},
};

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Config {
    pub map: PathBuf,
    pub local_entry: PathBuf,
    pub cloud_scripts: PathBuf,
    pub archives: PathBuf,
    pub editor: PathBuf,
    pub source_game: PathBuf,
    pub game: PathBuf,
    pub seconds: u64,
    pub startup_timeout: f64,
}

impl Config {
    pub fn load(path: &Path) -> Result<Self> {
        let text =
            fs::read_to_string(path).with_context(|| format!("读取配置 {}", path.display()))?;
        let mut config: Self = serde_json::from_str(text.trim_start_matches('\u{feff}'))?;
        let parent = std::path::absolute(path)?
            .parent()
            .context("无配置目录")?
            .to_path_buf();
        config.resolve_paths(&parent)?;
        config.validate_numbers()?;
        Ok(config)
    }
    pub fn resolve_paths(&mut self, parent: &Path) -> Result<()> {
        for value in [
            &mut self.map,
            &mut self.local_entry,
            &mut self.cloud_scripts,
            &mut self.archives,
            &mut self.editor,
            &mut self.source_game,
            &mut self.game,
        ] {
            anyhow::ensure!(!value.as_os_str().is_empty(), "配置路径不能为空");
            if value.is_relative() {
                *value = parent.join(&*value);
            }
        }
        Ok(())
    }
    pub fn validate_numbers(&self) -> Result<()> {
        anyhow::ensure!(
            self.seconds > 0 && self.seconds <= 31_536_000,
            "运行时长应在 1 秒至 1 年之间"
        );
        anyhow::ensure!(
            self.startup_timeout.is_finite()
                && self.startup_timeout > 0.0
                && self.startup_timeout <= 3600.0,
            "启动超时应在 0 至 3600 秒之间"
        );
        Ok(())
    }
    pub fn save(&self, path: &Path) -> Result<()> {
        self.validate_numbers()?;
        crate::archives::atomic_write(path, &serde_json::to_vec_pretty(self)?)
    }
}

pub fn root() -> Result<PathBuf> {
    let exe = std::env::current_exe()?;
    for parent in exe.ancestors().skip(1) {
        if parent.join("scriptrunner.dll").is_file() && parent.join("map/legacy_mls.lua").is_file()
        {
            return Ok(parent.to_owned());
        }
    }
    anyhow::bail!("找不到运行资源，请保留完整 ScriptRoom 文件夹")
}

pub fn default_config(root: &Path) -> PathBuf {
    root.join("本地测试配置.json")
}
