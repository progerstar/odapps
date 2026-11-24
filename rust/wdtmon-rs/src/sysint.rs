//! Desktop integration: autostart, opening files and links, uptime, the
//! Linux udev rule.

use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::Duration;

use auto_launch::{AutoLaunch, AutoLaunchBuilder};

use crate::i18n::trf;

/// What autostart should launch: the AppImage itself rather than its mount,
/// the `.app` bundle on macOS.
fn launch_path() -> Option<PathBuf> {
    if let Some(appimage) = std::env::var_os("APPIMAGE") {
        return Some(PathBuf::from(appimage));
    }
    let exe = std::env::current_exe().ok()?;
    if cfg!(target_os = "macos")
        && let Some(bundle) = exe
            .ancestors()
            .find(|p| p.extension().is_some_and(|e| e == "app"))
    {
        return Some(bundle.to_path_buf());
    }
    Some(exe)
}

fn auto_launch() -> Result<AutoLaunch, String> {
    let path = launch_path().ok_or("cannot locate the executable")?;
    AutoLaunchBuilder::new()
        .set_app_name(crate::config::APP_ID)
        .set_app_path(&path.to_string_lossy())
        .set_windows_enable_mode(auto_launch::WindowsEnableMode::CurrentUser)
        .build()
        .map_err(|e| e.to_string())
}

pub fn autostart_enabled() -> bool {
    auto_launch()
        .and_then(|a| a.is_enabled().map_err(|e| e.to_string()))
        .unwrap_or(false)
}

pub fn set_autostart(enable: bool) -> Result<(), String> {
    let a = auto_launch()?;
    if enable { a.enable() } else { a.disable() }.map_err(|e| e.to_string())
}

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

pub fn system_uptime() -> Duration {
    Duration::from_secs(sysinfo::System::uptime().unwrap_or_default())
}

/// `3 d 4 h 12 min`, `4 h 12 min`, `12 min 5 s`.
pub fn format_duration(d: Duration) -> String {
    let s = d.as_secs();
    let (days, hours, minutes, seconds) = (s / 86_400, s / 3600 % 24, s / 60 % 60, s % 60);
    if days > 0 {
        trf("{} d {} h {} min", &[&days, &hours, &minutes])
    } else if hours > 0 {
        trf("{} h {} min", &[&hours, &minutes])
    } else {
        trf("{} min {} s", &[&minutes, &seconds])
    }
}

pub const UDEV_RULES: &str = include_str!("../assets/99-opendev-hid.rules");
pub const UDEV_TARGET: &str = "/etc/udev/rules.d/99-opendev-hid.rules";

/// Writes the udev rule next to the app data and returns the shell command
/// that installs it.
pub fn prepare_udev_rule(data_dir: &Path) -> Result<String, String> {
    std::fs::create_dir_all(data_dir).map_err(|e| e.to_string())?;
    let file = data_dir.join("99-opendev-hid.rules");
    std::fs::write(&file, UDEV_RULES).map_err(|e| e.to_string())?;
    Ok(format!(
        "sudo cp {} {UDEV_TARGET} && sudo udevadm control --reload-rules && sudo udevadm trigger",
        shell_quote(&file.to_string_lossy())
    ))
}

fn shell_quote(value: &str) -> String {
    format!("'{}'", value.replace('\'', r"'\''"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn quoting_and_durations() {
        assert_eq!(shell_quote("/home/o'neil/x"), r"'/home/o'\''neil/x'");
        assert_eq!(format_duration(Duration::from_secs(65)), "1 min 5 s");
        assert_eq!(
            format_duration(Duration::from_secs(3 * 3600 + 120)),
            "3 h 2 min"
        );
        assert_eq!(
            format_duration(Duration::from_secs(2 * 86_400 + 3600)),
            "2 d 1 h 0 min"
        );
    }

    #[test]
    fn udev_rule_covers_the_watchdog() {
        assert!(UDEV_RULES.contains("ATTRS{idProduct}==\"a26d\""));
    }
}
