//! Event log: the process log always, a rotating file when enabled.

use std::fs::{self, File, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};

use chrono::Local;

use crate::config::LogConfig;

struct Inner {
    file: Option<File>,
    written: u64,
    config: LogConfig,
    path: PathBuf,
}

#[derive(Clone)]
pub struct Journal {
    inner: Arc<Mutex<Inner>>,
}

impl Journal {
    pub fn new(path: PathBuf, config: LogConfig) -> Self {
        Self {
            inner: Arc::new(Mutex::new(Inner {
                file: None,
                written: 0,
                config,
                path,
            })),
        }
    }

    pub fn path(&self) -> PathBuf {
        self.lock().path.clone()
    }

    pub fn configure(&self, config: LogConfig) {
        let mut inner = self.lock();
        if !config.enabled {
            inner.file = None;
        }
        inner.config = config;
    }

    pub fn info(&self, text: impl AsRef<str>) {
        log::info!("{}", text.as_ref());
        self.write("", text.as_ref());
    }

    pub fn warn(&self, text: impl AsRef<str>) {
        log::warn!("{}", text.as_ref());
        self.write("[warning] ", text.as_ref());
    }

    fn write(&self, prefix: &str, text: &str) {
        let mut inner = self.lock();
        if inner.config.enabled {
            let line = format!(
                "{} {prefix}{text}\n",
                Local::now().format("%Y-%m-%d %H:%M:%S")
            );
            inner.append(&line);
        }
    }

    fn lock(&self) -> std::sync::MutexGuard<'_, Inner> {
        self.inner.lock().unwrap_or_else(|e| e.into_inner())
    }
}

impl Inner {
    fn append(&mut self, line: &str) {
        let limit = u64::from(self.config.max_size_mb.max(1)) * 1024 * 1024;
        let len = line.len() as u64;
        if !self.open() {
            return;
        }
        if self.written > 0 && self.written + len > limit {
            self.file = None;
            rotate(&self.path, self.config.backups);
            // One try per line: when the file cannot be renamed (on Windows
            // another program may hold it open) it grows past the limit
            // until a later line rotates it.
            if !self.open() {
                return;
            }
        }
        if let Some(f) = &mut self.file {
            if f.write_all(line.as_bytes()).and_then(|_| f.flush()).is_ok() {
                self.written += len;
            } else {
                self.file = None;
            }
        }
    }

    /// Opens the file unless it is open; false when it cannot be.
    fn open(&mut self) -> bool {
        if self.file.is_some() {
            return true;
        }
        if let Some(dir) = self.path.parent() {
            let _ = fs::create_dir_all(dir);
        }
        match OpenOptions::new()
            .create(true)
            .append(true)
            .open(&self.path)
        {
            Ok(f) => {
                self.written = f.metadata().map(|m| m.len()).unwrap_or(0);
                self.file = Some(f);
                true
            }
            Err(e) => {
                log::warn!("{}: {e}", self.path.display());
                false
            }
        }
    }
}

/// `x.log` → `x.log.1` → … → `x.log.<backups>`.
fn rotate(path: &Path, backups: u32) {
    let numbered = |n: u32| {
        let mut p = path.as_os_str().to_owned();
        p.push(format!(".{n}"));
        PathBuf::from(p)
    };
    let backups = backups.max(1);
    let _ = fs::remove_file(numbered(backups));
    for n in (1..backups).rev() {
        let _ = fs::rename(numbered(n), numbered(n + 1));
    }
    let _ = fs::rename(path, numbered(1));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rotates_by_size() {
        let dir = std::env::temp_dir().join(format!("wdtmon-rs-journal-{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        let path = dir.join("test.log");
        let journal = Journal::new(
            path.clone(),
            LogConfig {
                enabled: true,
                max_size_mb: 1,
                backups: 2,
            },
        );
        let text = "x".repeat(400 * 1024);
        for _ in 0..7 {
            journal.info(&text);
        }
        assert!(path.is_file());
        assert!(dir.join("test.log.1").is_file());
        assert!(dir.join("test.log.2").is_file());
        assert!(!dir.join("test.log.3").exists());
        assert!(fs::metadata(&path).unwrap().len() <= 1024 * 1024);
        // Disabled logging writes nothing.
        journal.configure(LogConfig {
            enabled: false,
            ..Default::default()
        });
        let before = fs::metadata(&path).unwrap().len();
        journal.info("quiet");
        assert_eq!(fs::metadata(&path).unwrap().len(), before);
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn a_failed_rotation_keeps_writing() {
        let dir = std::env::temp_dir().join(format!("wdtmon-rs-stuck-{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        let path = dir.join("test.log");
        // A directory in the way of `test.log.1` makes every rename fail.
        fs::create_dir_all(dir.join("test.log.1").join("busy")).unwrap();
        let journal = Journal::new(
            path.clone(),
            LogConfig {
                enabled: true,
                max_size_mb: 1,
                backups: 1,
            },
        );
        let text = "x".repeat(400 * 1024);
        for _ in 0..4 {
            journal.info(&text);
        }
        assert!(fs::metadata(&path).unwrap().len() > 1024 * 1024);
        let _ = fs::remove_dir_all(&dir);
    }
}
