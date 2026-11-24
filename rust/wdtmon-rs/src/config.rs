//! Persistent settings.

use std::fs;
use std::io;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

pub const APP_ID: &str = "wdtmon-rs";

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Language {
    #[default]
    System,
    En,
    Ru,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Theme {
    #[default]
    System,
    Light,
    Dark,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct LogConfig {
    pub enabled: bool,
    pub max_size_mb: u32,
    pub backups: u32,
}

impl Default for LogConfig {
    fn default() -> Self {
        Self {
            enabled: false,
            max_size_mb: 64,
            backups: 3,
        }
    }
}

pub const DEFAULT_WEB_PORT: u16 = 34242;

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct WebConfig {
    pub enabled: bool,
    pub port: u16,
    /// Listen on every interface instead of loopback only. Not in the UI:
    /// the page can reset the PC, so this is opted into by editing the file.
    pub remote_access: bool,
}

impl Default for WebConfig {
    fn default() -> Self {
        Self {
            enabled: false,
            port: DEFAULT_WEB_PORT,
            remote_access: false,
        }
    }
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct Config {
    /// Serial number of the watchdog to serve; `None` takes the first found.
    pub serial: Option<String>,
    /// Pushed to the device whenever it connects.
    pub led_on: bool,
    pub timer_paused: bool,
    pub start_hidden: bool,
    pub always_on_top: bool,
    /// Ask before quitting while the device timer runs.
    pub confirm_quit: bool,
    pub language: Language,
    pub theme: Theme,
    pub logging: LogConfig,
    pub web: WebConfig,
    /// Last window position (ignored where the desktop places windows).
    pub position: Option<[f32; 2]>,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            serial: None,
            led_on: true,
            timer_paused: false,
            start_hidden: false,
            always_on_top: false,
            confirm_quit: true,
            language: Language::System,
            theme: Theme::System,
            logging: LogConfig::default(),
            web: WebConfig::default(),
            position: None,
        }
    }
}

impl Config {
    /// Brings hand-edited values back into range.
    pub fn sanitize(&mut self) {
        self.logging.max_size_mb = self.logging.max_size_mb.clamp(1, 1024);
        self.logging.backups = self.logging.backups.clamp(1, 16);
        if self.web.port < 1024 {
            self.web.port = DEFAULT_WEB_PORT;
        }
        if self.serial.as_deref().is_some_and(|s| s.trim().is_empty()) {
            self.serial = None;
        }
        if self
            .position
            .is_some_and(|p| !p.iter().all(|v| v.is_finite()))
        {
            self.position = None;
        }
    }
}

pub struct Paths {
    pub config_file: PathBuf,
    pub data_dir: PathBuf,
    pub log_file: PathBuf,
}

impl Paths {
    pub fn new() -> Self {
        let dirs = directories::ProjectDirs::from("ru", "Open-dev", APP_ID);
        let (config_dir, data_dir) = match &dirs {
            Some(d) => (d.config_dir().to_path_buf(), d.data_dir().to_path_buf()),
            None => (PathBuf::from("."), PathBuf::from(".")),
        };
        Self {
            config_file: config_dir.join("config.json"),
            log_file: data_dir.join(format!("{APP_ID}.log")),
            data_dir,
        }
    }
}

/// Reads the settings; `Ok(None)` when there are none yet.
pub fn load(path: &Path) -> Result<Option<Config>, String> {
    let text = match fs::read_to_string(path) {
        Ok(text) => text,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(None),
        Err(e) => return Err(e.to_string()),
    };
    let mut config = serde_json::from_str::<Config>(&text).map_err(|e| e.to_string())?;
    config.sanitize();
    Ok(Some(config))
}

/// Moves a settings file that cannot be read out of the way of the defaults
/// saved next: people edit it by hand, and a typo should not cost the rest.
pub fn set_aside(path: &Path) -> io::Result<PathBuf> {
    let aside = path.with_extension("json.bad");
    fs::rename(path, &aside)?;
    Ok(aside)
}

/// Writes through a temporary file so a crash never leaves half a config.
pub fn save(path: &Path, config: &Config) -> io::Result<()> {
    if let Some(dir) = path.parent() {
        fs::create_dir_all(dir)?;
    }
    let text = serde_json::to_string_pretty(config).map_err(io::Error::other)?;
    let tmp = path.with_extension("json.tmp");
    fs::write(&tmp, text)?;
    fs::rename(&tmp, path)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn json_roundtrip_and_defaults() {
        let c = Config {
            serial: Some("0123456789AB".into()),
            language: Language::Ru,
            web: WebConfig {
                enabled: true,
                ..Default::default()
            },
            ..Default::default()
        };
        let text = serde_json::to_string(&c).unwrap();
        assert_eq!(serde_json::from_str::<Config>(&text).unwrap(), c);
        let mut partial: Config =
            serde_json::from_str(r#"{"web": {"port": 80}, "serial": " "}"#).unwrap();
        partial.sanitize();
        assert_eq!(partial.web.port, DEFAULT_WEB_PORT);
        assert_eq!(partial.serial, None);
        assert!(partial.led_on && partial.confirm_quit);
    }

    #[test]
    fn a_broken_file_is_reported_and_set_aside() {
        let dir = std::env::temp_dir().join(format!("wdtmon-rs-config-{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        let path = dir.join("config.json");
        assert_eq!(load(&path), Ok(None));
        fs::create_dir_all(&dir).unwrap();
        fs::write(
            &path,
            r#"{"serial": "0123", "web": {"remote_access": tru}}"#,
        )
        .unwrap();
        assert!(load(&path).is_err());
        let aside = set_aside(&path).unwrap();
        assert_eq!(aside, dir.join("config.json.bad"));
        assert!(!path.exists() && aside.is_file());
        let _ = fs::remove_dir_all(&dir);
    }
}
