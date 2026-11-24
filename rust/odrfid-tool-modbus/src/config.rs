//! Persistent settings of the tool (not of the reader).

use std::fs;
use std::io;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

use crate::modbus::Line;

pub const APP_ID: &str = "odrfid-tool-modbus";
/// The reader address the connection form starts with.
pub const DEFAULT_ADDRESS: u8 = 1;

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
pub struct Config {
    /// The serial port of the RS-485 adapter.
    pub port: Option<String>,
    pub line: Line,
    /// The reader's Modbus address.
    pub address: u8,
    /// Look for the reader at every address 1..=247 instead.
    pub scan_all: bool,
    /// The reader's end-of-frame pause, ms, learned from it: it answers
    /// that much later, and a tool that does not wait misses it.
    pub frame_pause_ms: u16,
    /// Connect to the last port when the tool starts.
    pub auto_connect: bool,
    pub language: Language,
    pub theme: Theme,
    /// Show routine polling in the frame log.
    pub log_polls: bool,
    /// Last window position and size.
    pub position: Option<[f32; 2]>,
    pub size: Option<[f32; 2]>,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            port: None,
            line: Line::default(),
            address: DEFAULT_ADDRESS,
            scan_all: false,
            frame_pause_ms: 0,
            auto_connect: true,
            language: Language::System,
            theme: Theme::System,
            log_polls: false,
            position: None,
            size: None,
        }
    }
}

impl Config {
    /// Brings hand-edited values back into range.
    pub fn sanitize(&mut self) {
        if !(1..=247).contains(&self.address) {
            self.address = DEFAULT_ADDRESS;
        }
        if !self.line.valid() {
            self.line.baud = Line::default().baud;
        }
        if self.port.as_deref().is_some_and(|s| s.trim().is_empty()) {
            self.port = None;
        }
        let finite = |p: Option<[f32; 2]>| p.filter(|p| p.iter().all(|v| v.is_finite()));
        self.position = finite(self.position);
        self.size = finite(self.size).filter(|s| s[0] >= 200.0 && s[1] >= 200.0);
    }
}

pub struct Paths {
    pub config_file: PathBuf,
}

impl Paths {
    pub fn new() -> Self {
        let dirs = directories::ProjectDirs::from("ru", "Open-dev", APP_ID);
        let config_dir = match &dirs {
            Some(d) => d.config_dir().to_path_buf(),
            None => PathBuf::from("."),
        };
        Self {
            config_file: config_dir.join("config.json"),
        }
    }
}

pub fn load(path: &Path) -> Option<Config> {
    let text = fs::read_to_string(path).ok()?;
    match serde_json::from_str::<Config>(&text) {
        Ok(mut c) => {
            c.sanitize();
            Some(c)
        }
        Err(e) => {
            log::warn!("{}: {e}", path.display());
            None
        }
    }
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
    use crate::modbus::{Parity, StopBits};

    #[test]
    fn json_roundtrip_and_defaults() {
        let c = Config {
            port: Some("/dev/ttyUSB0".into()),
            line: Line {
                baud: 9600,
                parity: Parity::Even,
                stop: StopBits::OneAndHalf,
            },
            address: 17,
            language: Language::Ru,
            ..Default::default()
        };
        let text = serde_json::to_string(&c).unwrap();
        assert!(text.contains(r#""stop":"1.5""#), "{text}");
        assert_eq!(serde_json::from_str::<Config>(&text).unwrap(), c);
        let mut partial: Config =
            serde_json::from_str(r#"{"address": 0, "port": " ", "line": {"baud": 31250}}"#)
                .unwrap();
        partial.sanitize();
        assert_eq!(partial.address, DEFAULT_ADDRESS);
        assert_eq!(partial.port, None);
        assert_eq!(partial.line.baud, 115_200);
        assert!(partial.auto_connect);
    }
}
