//! Desktop integration: opening links, the operating time as text.

use std::process::{Command, Stdio};

use crate::i18n::trf;

/// Opens a file, folder or URL with the desktop's default handler.
pub fn open(target: impl AsRef<std::ffi::OsStr>) -> Result<(), String> {
    let program = if cfg!(windows) {
        "explorer"
    } else if cfg!(target_os = "macos") {
        "open"
    } else {
        "xdg-open"
    };
    Command::new(program)
        .arg(target)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .map(|_| ())
        .map_err(|e| e.to_string())
}

/// `3 d 4 h 12 min`, `4 h 12 min`, `12 min`.
pub fn format_minutes(minutes: u32) -> String {
    let (days, hours, minutes) = (minutes / 1440, minutes / 60 % 24, minutes % 60);
    if days > 0 {
        trf("{} d {} h {} min", &[&days, &hours, &minutes])
    } else if hours > 0 {
        trf("{} h {} min", &[&hours, &minutes])
    } else {
        trf("{} min", &[&minutes])
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn durations() {
        assert_eq!(format_minutes(5), "5 min");
        assert_eq!(format_minutes(125), "2 h 5 min");
        assert_eq!(format_minutes(4 * 1440 + 125), "4 d 2 h 5 min");
    }
}
