//! The frame log of the Qt version's log window: what went out on the line
//! and what came back, as bytes actually sent and received.

use std::collections::VecDeque;
use std::fmt::Write as _;
use std::sync::{Arc, Mutex, MutexGuard};

use chrono::{DateTime, Local};

const CAPACITY: usize = 5000;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Dir {
    Tx,
    Rx,
    /// A remark: a timeout, a port error, a damaged answer.
    Note,
}

#[derive(Clone, Debug)]
pub struct Entry {
    pub time: DateTime<Local>,
    pub dir: Dir,
    pub text: String,
    /// Routine polling, hidden unless asked for.
    pub background: bool,
}

#[derive(Default)]
struct Inner {
    entries: VecDeque<Entry>,
    paused: bool,
}

#[derive(Clone, Default)]
pub struct Traffic(Arc<Mutex<Inner>>);

pub fn hex(bytes: &[u8]) -> String {
    let mut s = String::with_capacity(bytes.len() * 3);
    for (i, b) in bytes.iter().enumerate() {
        if i > 0 {
            s.push(' ');
        }
        let _ = write!(s, "{b:02X}");
    }
    s
}

impl Traffic {
    fn lock(&self) -> MutexGuard<'_, Inner> {
        self.0.lock().unwrap_or_else(|e| e.into_inner())
    }

    fn push(&self, dir: Dir, text: String, background: bool) {
        let mut inner = self.lock();
        if inner.paused {
            return;
        }
        if inner.entries.len() >= CAPACITY {
            inner.entries.pop_front();
        }
        inner.entries.push_back(Entry {
            time: Local::now(),
            dir,
            text,
            background,
        });
    }

    pub fn add(&self, dir: Dir, bytes: &[u8], background: bool) {
        self.push(dir, hex(bytes), background);
    }

    pub fn note(&self, text: &str, background: bool) {
        self.push(Dir::Note, text.to_string(), background);
    }

    pub fn snapshot(&self, with_background: bool) -> Vec<Entry> {
        self.lock()
            .entries
            .iter()
            .filter(|e| with_background || !e.background)
            .cloned()
            .collect()
    }

    pub fn clear(&self) {
        self.lock().entries.clear();
    }

    pub fn paused(&self) -> bool {
        self.lock().paused
    }

    pub fn set_paused(&self, paused: bool) {
        self.lock().paused = paused;
    }
}

pub fn arrow(dir: Dir) -> &'static str {
    match dir {
        Dir::Tx => ">>",
        Dir::Rx => "<<",
        Dir::Note => "--",
    }
}

/// Tab-separated, as the Qt log window exported it.
pub fn export(entries: &[Entry]) -> String {
    let mut out = String::from("#Time\tDirection\tData\n");
    for e in entries {
        let _ = writeln!(
            out,
            "{}\t{}\t{}",
            e.time.format("%Y-%m-%d %H:%M:%S%.3f"),
            arrow(e.dir),
            e.text
        );
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn keeps_order_filters_and_exports() {
        let t = Traffic::default();
        t.add(Dir::Tx, &[0x5F, 0x04, 0x00, 0x01], false);
        t.add(Dir::Rx, &[0x5F], true);
        t.note("no answer", false);
        assert_eq!(t.snapshot(true).len(), 3);
        let shown = t.snapshot(false);
        assert_eq!(shown.len(), 2);
        assert_eq!(shown[0].text, "5F 04 00 01");
        let text = export(&shown);
        assert!(text.starts_with("#Time\tDirection\tData\n"));
        assert!(text.contains("\t>>\t5F 04 00 01\n"));
        assert!(text.contains("\t--\tno answer\n"));
        t.set_paused(true);
        t.note("lost", false);
        assert_eq!(t.snapshot(true).len(), 3);
        t.clear();
        assert!(t.snapshot(true).is_empty());
    }
}
