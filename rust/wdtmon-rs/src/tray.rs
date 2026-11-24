//! System tray icon.
//!
//! Linux uses StatusNotifierItem over D-Bus (`ksni`): it runs on its own
//! threads and keeps working while the window is closed. Windows and macOS
//! use `tray-icon`, created on the UI thread.

use std::sync::Arc;

use crate::engine::Health;
use crate::i18n::tr;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TrayAction {
    Toggle,
    Show,
    Hide,
    TestReset,
    TestPowerCycle,
    Quit,
}

pub type ActionSink = Arc<dyn Fn(TrayAction) + Send + Sync>;

const MENU: &[Option<TrayAction>] = &[
    Some(TrayAction::Hide),
    Some(TrayAction::Show),
    None,
    Some(TrayAction::TestReset),
    Some(TrayAction::TestPowerCycle),
    None,
    Some(TrayAction::Quit),
];

fn label(action: TrayAction) -> &'static str {
    match action {
        TrayAction::Toggle | TrayAction::Show => tr("Show"),
        TrayAction::Hide => tr("Hide"),
        TrayAction::TestReset => tr("Test Reset"),
        TrayAction::TestPowerCycle => tr("Test Hard Reset"),
        TrayAction::Quit => tr("Quit"),
    }
}

pub fn tooltip(health: Health) -> String {
    let state = match health {
        Health::Ok => tr("heartbeats are being sent"),
        Health::Paused => tr("the watchdog timer is suspended"),
        Health::Problem => tr("no connection to the watchdog"),
        Health::Unknown => tr("starting"),
    };
    format!("wdtmon-rs — {state}")
}

#[cfg(not(target_os = "linux"))]
pub use desktop::Tray;
#[cfg(target_os = "linux")]
pub use linux::Tray;

#[cfg(target_os = "linux")]
mod linux {
    use super::*;
    use ksni::blocking::{Handle, TrayMethods};
    use ksni::menu::{MenuItem, StandardItem};
    use std::sync::atomic::{AtomicBool, Ordering};

    struct Sni {
        health: Health,
        window_visible: bool,
        sink: ActionSink,
        /// The desktop's StatusNotifierWatcher is running.
        online: Arc<AtomicBool>,
    }

    impl ksni::Tray for Sni {
        fn id(&self) -> String {
            crate::config::APP_ID.into()
        }

        fn title(&self) -> String {
            "wdtmon-rs".into()
        }

        fn category(&self) -> ksni::Category {
            ksni::Category::Hardware
        }

        fn icon_pixmap(&self) -> Vec<ksni::Icon> {
            [22, 32, 48]
                .into_iter()
                .map(|size| {
                    let icon = crate::icons::tray_icon(self.health, size);
                    ksni::Icon {
                        width: size as i32,
                        height: size as i32,
                        data: icon.to_argb(),
                    }
                })
                .collect()
        }

        fn tool_tip(&self) -> ksni::ToolTip {
            ksni::ToolTip {
                title: tooltip(self.health),
                ..Default::default()
            }
        }

        fn activate(&mut self, _x: i32, _y: i32) {
            (self.sink)(TrayAction::Toggle);
        }

        fn watcher_online(&self) {
            self.online.store(true, Ordering::Relaxed);
        }

        fn watcher_offline(&self, reason: ksni::OfflineReason) -> bool {
            log::warn!("system tray went away: {reason:?}");
            self.online.store(false, Ordering::Relaxed);
            true
        }

        fn menu(&self) -> Vec<MenuItem<Self>> {
            MENU.iter()
                .filter_map(|entry| match *entry {
                    None => Some(MenuItem::Separator),
                    Some(TrayAction::Hide) if !self.window_visible => None,
                    Some(TrayAction::Show) if self.window_visible => None,
                    Some(action) => Some(MenuItem::Standard(StandardItem {
                        label: label(action).into(),
                        activate: Box::new(move |tray: &mut Sni| (tray.sink)(action)),
                        ..Default::default()
                    })),
                })
                .collect()
        }
    }

    pub struct Tray {
        handle: Handle<Sni>,
        online: Arc<AtomicBool>,
    }

    impl Tray {
        /// `None` only without a D-Bus session; see [`Tray::is_online`].
        pub fn new(sink: ActionSink) -> Option<Self> {
            let online = Arc::new(AtomicBool::new(true));
            let sni = |sink: ActionSink| Sni {
                health: Health::Unknown,
                window_visible: true,
                sink,
                online: online.clone(),
            };
            match sni(sink.clone()).spawn() {
                Ok(handle) => return Some(Self { handle, online }),
                Err(e) => log::warn!("system tray unavailable: {e}"),
            }
            // No tray host yet (autostart before the panel, GNOME without
            // the AppIndicator extension): stay registered for when it comes.
            online.store(false, Ordering::Relaxed);
            match sni(sink).assume_sni_available(true).spawn() {
                Ok(handle) => Some(Self { handle, online }),
                Err(e) => {
                    log::warn!("cannot start the tray service: {e}");
                    None
                }
            }
        }

        pub fn set_health(&self, health: Health) {
            self.handle.update(|t| t.health = health);
        }

        /// False while the desktop's tray is gone; hiding then would lose
        /// the window.
        pub fn is_online(&self) -> bool {
            self.online.load(Ordering::Relaxed)
        }

        pub fn set_window_visible(&self, visible: bool) {
            self.handle.update(|t| t.window_visible = visible);
        }

        /// Labels are read on every menu request; nudge the host to refetch.
        pub fn retranslate(&self) {
            self.handle.update(|_| {});
        }
    }

    impl Drop for Tray {
        fn drop(&mut self) {
            self.handle.shutdown().wait();
        }
    }
}

#[cfg(not(target_os = "linux"))]
mod desktop {
    use super::*;
    use tray_icon::menu::{Menu, MenuEvent, MenuItem, PredefinedMenuItem};
    use tray_icon::{
        Icon, MouseButton, MouseButtonState, TrayIcon, TrayIconBuilder, TrayIconEvent,
    };

    pub struct Tray {
        icon: TrayIcon,
        items: Vec<(TrayAction, MenuItem)>,
        health: Health,
    }

    fn icon(health: Health) -> Option<Icon> {
        let size = if cfg!(target_os = "macos") { 36 } else { 32 };
        let rgba = crate::icons::tray_icon(health, size);
        Icon::from_rgba(rgba.pixels, size, size).ok()
    }

    impl Tray {
        /// Must run on the UI thread with the event loop running.
        pub fn new(sink: ActionSink) -> Option<Self> {
            let menu = Menu::new();
            let mut items = Vec::new();
            for entry in MENU {
                let result = match entry {
                    None => menu.append(&PredefinedMenuItem::separator()),
                    Some(action) => {
                        let item = MenuItem::new(label(*action), true, None);
                        let r = menu.append(&item);
                        items.push((*action, item));
                        r
                    }
                };
                if let Err(e) = result {
                    log::warn!("tray menu: {e}");
                }
            }
            let ids: Vec<(String, TrayAction)> = items
                .iter()
                .map(|(action, item)| (item.id().as_ref().to_string(), *action))
                .collect();
            let menu_sink = sink.clone();
            MenuEvent::set_event_handler(Some(move |event: MenuEvent| {
                if let Some((_, action)) = ids.iter().find(|(id, _)| id == event.id.as_ref()) {
                    menu_sink(*action);
                }
            }));
            TrayIconEvent::set_event_handler(Some(move |event: TrayIconEvent| {
                if let TrayIconEvent::Click {
                    button: MouseButton::Left,
                    button_state: MouseButtonState::Up,
                    ..
                } = event
                {
                    sink(TrayAction::Toggle);
                }
            }));
            let mut builder = TrayIconBuilder::new()
                .with_menu(Box::new(menu))
                .with_tooltip(tooltip(Health::Unknown));
            if let Some(i) = icon(Health::Unknown) {
                builder = builder.with_icon(i);
            }
            // Windows: left click toggles the window, right click opens the
            // menu. macOS shows the menu on any click.
            if cfg!(windows) {
                builder = builder.with_menu_on_left_click(false);
            }
            match builder.build() {
                Ok(icon) => Some(Self {
                    icon,
                    items,
                    health: Health::Unknown,
                }),
                Err(e) => {
                    log::warn!("system tray unavailable: {e}");
                    None
                }
            }
        }

        pub fn set_health(&mut self, health: Health) {
            if self.health != health {
                self.health = health;
                let _ = self.icon.set_icon(icon(health));
                let _ = self.icon.set_tooltip(Some(tooltip(health)));
            }
        }

        pub fn set_window_visible(&self, visible: bool) {
            for (action, item) in &self.items {
                match action {
                    TrayAction::Hide => item.set_enabled(visible),
                    TrayAction::Show => item.set_enabled(!visible),
                    _ => {}
                }
            }
        }

        pub fn retranslate(&self) {
            for (action, item) in &self.items {
                item.set_text(label(*action));
            }
            let _ = self.icon.set_tooltip(Some(tooltip(self.health)));
        }
    }
}
