//! The engine thread: finds the watchdog on the HID bus, sends the
//! heartbeat and reads the device state. The heartbeat never depends on the
//! window being shown.

use std::sync::mpsc::{self, Receiver, RecvTimeoutError, Sender};
use std::sync::{Arc, Mutex, MutexGuard};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

use chrono::{DateTime, Local};

use crate::hid::{
    Bus, Command, Data, Device, DeviceExt, Firmware, Found, Model, OpenError, Settings, WriteStatus,
};
use crate::i18n::{tr, trf};
use crate::journal::Journal;

const TICK: Duration = Duration::from_millis(50);
/// As in the Qt monitor; the shortest device timeout is one minute.
pub const HEARTBEAT_INTERVAL: Duration = Duration::from_millis(2500);
/// Device list refresh while a watchdog is served (picks up new ones for the
/// selector and notices removal).
const SCAN_CONNECTED: Duration = Duration::from_secs(5);
/// Retry period while nothing usable is connected.
const SCAN_IDLE: Duration = Duration::from_secs(2);
const SETTINGS_REFRESH: Duration = Duration::from_secs(60);
/// A settings write is polled through byte 15 of the data report (the device
/// never sends it by itself): every 10–20 ms for about a second, the time
/// the firmware takes to store it.
const WRITE_POLL: Duration = Duration::from_millis(15);
const WRITE_TIMEOUT: Duration = Duration::from_secs(1);

/// Lets other threads wake the UI.
#[derive(Clone, Default)]
pub struct Repaint(Arc<Mutex<Option<eframe::egui::Context>>>);

impl Repaint {
    pub fn set(&self, ctx: Option<eframe::egui::Context>) {
        *self.0.lock().unwrap_or_else(|e| e.into_inner()) = ctx;
    }

    pub fn request(&self) {
        if let Some(ctx) = self.0.lock().unwrap_or_else(|e| e.into_inner()).as_ref() {
            ctx.request_repaint();
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct EngineConfig {
    pub serial: Option<String>,
    pub led_on: bool,
    pub timer_paused: bool,
}

impl EngineConfig {
    pub fn from_config(c: &crate::config::Config) -> Self {
        Self {
            serial: c.serial.clone(),
            led_on: c.led_on,
            timer_paused: c.timer_paused,
        }
    }
}

pub enum Request {
    Configure(EngineConfig),
    /// A command from the user or the web page.
    Command(Command),
    /// Store new device settings (Pro2 1.8U with HID writes).
    WriteSettings(Settings),
    Rescan,
    /// Suspends the Pro2's timer ahead of quitting; answers whether it got
    /// there.
    Suspend(Sender<bool>),
    /// Stops the engine; with `suspend` the device timer is paused first.
    Shutdown {
        suspend: bool,
        done: Sender<bool>,
    },
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub enum Link {
    #[default]
    Searching,
    NoDevice,
    /// The configured watchdog is not plugged in.
    Missing(String),
    /// The OS denies access to the device node.
    AccessDenied,
    Failed(String),
    /// A device with our IDs but a firmware this monitor does not know.
    Unsupported(String),
    Connected,
}

/// Summary for the tray icon.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Health {
    #[default]
    Unknown,
    /// Heartbeats are delivered.
    Ok,
    /// Delivered, but the device timer is suspended.
    Paused,
    /// No device or it fails.
    Problem,
}

/// Changing the device settings over HID.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Writing {
    #[default]
    Ready,
    /// A write is waiting for the device to store it.
    Saving,
    /// This firmware takes no settings over HID.
    Unsupported,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Notice {
    pub ok: bool,
    pub text: String,
}

#[derive(Clone, Debug, Default)]
pub struct Status {
    /// Serial numbers of the watchdogs found.
    pub devices: Vec<String>,
    /// The watchdog served (or wanted).
    pub serial: Option<String>,
    pub link: Link,
    pub firmware: Option<Firmware>,
    pub settings: Option<Settings>,
    pub data: Option<Data>,
    pub data_time: Option<DateTime<Local>>,
    pub last_beat: Option<Instant>,
    /// Heartbeats delivered since start.
    pub beats: u64,
    pub health: Health,
    /// The connected Pro2 has its timer suspended.
    pub paused: bool,
    pub writing: Writing,
    pub notices: Vec<Notice>,
}

impl Status {
    pub fn connected(&self) -> bool {
        self.link == Link::Connected
    }

    pub fn model(&self) -> Option<Model> {
        self.firmware
            .as_ref()
            .map(|f| f.model)
            .filter(|_| self.connected())
    }

    /// Reset, power and shut-down tests exist (Pro2).
    pub fn has_tests(&self) -> bool {
        self.model() == Some(Model::Pro2)
    }

    /// Quitting now leaves a counting timer behind: `Some(minutes)`.
    pub fn timer_left_running(&self) -> Option<u8> {
        if self.model() != Some(Model::Pro2) || self.paused {
            return None;
        }
        match self.settings {
            Some(s) if !s.timer_armed() => None,
            Some(s) => Some(s.reboot_min),
            None => Some(0),
        }
    }
}

/// What the web server and other threads need from the engine.
#[derive(Clone)]
pub struct EngineLink {
    tx: Sender<Request>,
    status: Arc<Mutex<Status>>,
}

impl EngineLink {
    pub fn send(&self, request: Request) {
        let _ = self.tx.send(request);
    }

    pub fn status(&self) -> MutexGuard<'_, Status> {
        self.status.lock().unwrap_or_else(|e| e.into_inner())
    }
}

pub struct EngineHandle {
    link: EngineLink,
    thread: Option<JoinHandle<()>>,
}

impl EngineHandle {
    pub fn send(&self, request: Request) {
        self.link.send(request);
    }

    pub fn status(&self) -> MutexGuard<'_, Status> {
        self.link.status()
    }

    pub fn link(&self) -> EngineLink {
        self.link.clone()
    }

    pub fn take_notices(&self) -> Vec<Notice> {
        std::mem::take(&mut self.status().notices)
    }

    /// Suspends the device timer ahead of quitting, while the window can
    /// still tell that it failed.
    pub fn suspend(&self) -> bool {
        let (done_tx, done_rx) = mpsc::channel();
        self.send(Request::Suspend(done_tx));
        done_rx
            .recv_timeout(Duration::from_secs(4))
            .unwrap_or(false)
    }

    /// Stops the engine; returns whether the requested pause was delivered.
    pub fn shutdown(&mut self, suspend: bool) -> bool {
        let (done_tx, done_rx) = mpsc::channel();
        self.send(Request::Shutdown {
            suspend,
            done: done_tx,
        });
        let confirmed = done_rx
            .recv_timeout(Duration::from_secs(4))
            .unwrap_or(false);
        if let Some(t) = self.thread.take() {
            let _ = t.join();
        }
        confirmed
    }
}

pub type HealthObserver = Box<dyn Fn(Health) + Send>;
pub type BusFactory = Box<dyn FnOnce() -> Box<dyn Bus> + Send>;

pub fn spawn(
    bus: BusFactory,
    config: EngineConfig,
    journal: Journal,
    repaint: Repaint,
    on_health: HealthObserver,
) -> EngineHandle {
    let (tx, rx) = mpsc::channel();
    let status = Arc::new(Mutex::new(Status::default()));
    let shared = status.clone();
    let thread = thread::Builder::new()
        .name("engine".into())
        .spawn(move || {
            let mut engine = Engine::new(bus(), config, shared, journal, repaint, on_health);
            engine.run(rx);
        })
        .expect("spawn engine thread");
    EngineHandle {
        link: EngineLink { tx, status },
        thread: Some(thread),
    }
}

struct Engine {
    bus: Box<dyn Bus>,
    device: Option<Box<dyn Device>>,
    found: Vec<Found>,
    config: EngineConfig,
    status: Arc<Mutex<Status>>,
    journal: Journal,
    repaint: Repaint,
    on_health: HealthObserver,
    next_scan: Instant,
    next_beat: Instant,
    next_settings: Instant,
    /// The last problem written to the log, so a lasting one is logged once.
    logged_problem: Option<String>,
    last_health: Health,
    changed: bool,
    /// A settings write the device accepted and has not finished yet.
    write: Option<PendingWrite>,
}

struct PendingWrite {
    settings: Settings,
    next_poll: Instant,
    deadline: Instant,
}

impl Engine {
    fn new(
        bus: Box<dyn Bus>,
        config: EngineConfig,
        status: Arc<Mutex<Status>>,
        journal: Journal,
        repaint: Repaint,
        on_health: HealthObserver,
    ) -> Self {
        let now = Instant::now();
        Self {
            bus,
            device: None,
            found: Vec::new(),
            config,
            status,
            journal,
            repaint,
            on_health,
            next_scan: now,
            next_beat: now,
            next_settings: now,
            logged_problem: None,
            last_health: Health::Unknown,
            changed: true,
            write: None,
        }
    }

    fn run(&mut self, rx: Receiver<Request>) {
        loop {
            let wait = if self.write.is_some() {
                WRITE_POLL
            } else {
                TICK
            };
            let first = match rx.recv_timeout(wait) {
                Ok(r) => Some(r),
                Err(RecvTimeoutError::Timeout) => None,
                Err(RecvTimeoutError::Disconnected) => return,
            };
            for request in first
                .into_iter()
                .chain(std::iter::from_fn(|| rx.try_recv().ok()))
            {
                if let Request::Shutdown { suspend, done } = request {
                    let confirmed = suspend && self.suspend_before_exit();
                    self.device = None;
                    let _ = done.send(confirmed);
                    return;
                }
                self.handle(request);
            }
            self.tick(Instant::now());
            self.publish();
        }
    }

    fn mutate<R>(&mut self, f: impl FnOnce(&mut Status) -> R) -> R {
        self.changed = true;
        f(&mut self.status.lock().unwrap_or_else(|e| e.into_inner()))
    }

    fn notice(&mut self, ok: bool, text: String) {
        self.mutate(|s| {
            s.notices.push(Notice { ok, text });
            if s.notices.len() > 10 {
                s.notices.remove(0);
            }
        });
    }

    fn publish(&mut self) {
        let health = {
            let st = self.status.lock().unwrap_or_else(|e| e.into_inner());
            match &st.link {
                Link::Connected if st.paused => Health::Paused,
                Link::Connected if st.last_beat.is_some() => Health::Ok,
                Link::Connected | Link::Searching => Health::Unknown,
                _ => Health::Problem,
            }
        };
        if health != self.last_health {
            self.last_health = health;
            self.mutate(|s| s.health = health);
            (self.on_health)(health);
        }
        if std::mem::take(&mut self.changed) {
            self.repaint.request();
        }
    }

    /// Logs a lasting problem once, and sets the link state.
    fn problem(&mut self, link: Link, text: String) {
        if self.logged_problem.as_ref() != Some(&text) {
            self.journal.warn(&text);
            self.logged_problem = Some(text);
        }
        self.mutate(|s| {
            s.link = link;
            s.firmware = None;
            s.settings = None;
            s.data = None;
            s.writing = Writing::Ready;
        });
    }

    // ---- requests -------------------------------------------------------

    fn handle(&mut self, request: Request) {
        match request {
            Request::Configure(config) => {
                let old = std::mem::replace(&mut self.config, config);
                if old.serial != self.config.serial {
                    self.disconnect();
                    self.next_scan = Instant::now();
                }
                if old.led_on != self.config.led_on {
                    self.send_quiet(Command::Led(self.config.led_on));
                }
                if old.timer_paused != self.config.timer_paused {
                    let paused = self.config.timer_paused;
                    // Shown once the Pro2 has it. A failed send drops the
                    // link, and the next connect sends it again.
                    if self.pro2() && self.send_quiet(Command::Pause(paused)) {
                        self.mutate(|s| s.paused = paused);
                        self.journal.info(if paused {
                            tr("Watchdog timer suspended")
                        } else {
                            tr("Watchdog timer resumed")
                        });
                    }
                }
            }
            Request::Command(command) => self.user_command(command),
            Request::WriteSettings(settings) => self.write_settings(settings, Instant::now()),
            Request::Rescan => self.next_scan = Instant::now(),
            Request::Suspend(done) => {
                let ok = self.suspend_before_exit();
                let _ = done.send(ok);
            }
            Request::Shutdown { .. } => unreachable!("handled by run()"),
        }
    }

    fn user_command(&mut self, command: Command) {
        let text = match command {
            Command::TestReset => Some(tr("Reset requested")),
            Command::TestPowerCycle => Some(tr("Hard reset requested")),
            Command::TestShutdown => Some(tr("Shut down requested")),
            Command::SelfTest => Some(tr("Self-test requested")),
            _ => None,
        };
        if self.device.is_none() {
            self.notice(false, tr("The watchdog is not connected").into());
            return;
        }
        if let Some(t) = text {
            self.journal.info(t);
        }
        if command == Command::Heartbeat {
            self.next_beat = Instant::now();
        } else if self.send_quiet(command) {
            if let Some(t) = text {
                self.notice(true, t.into());
            }
        } else {
            self.notice(false, tr("The command did not reach the watchdog").into());
        }
    }

    fn write_settings(&mut self, settings: Settings, now: Instant) {
        if !self.pro2() {
            return self.notice(false, tr("The watchdog is not connected").into());
        }
        if self.write.is_some() {
            return self.notice(
                false,
                tr("The previous settings are still being saved").into(),
            );
        }
        let Some(device) = &mut self.device else {
            return;
        };
        // Compared with what the device stores afterwards, so in its own form.
        let settings = Settings::parse(&settings.report()).unwrap_or(settings);
        match device.write_settings(&settings) {
            Ok(()) => {
                self.write = Some(PendingWrite {
                    settings,
                    next_poll: now + WRITE_POLL,
                    deadline: now + WRITE_TIMEOUT,
                });
                self.mutate(|s| s.writing = Writing::Saving);
            }
            Err(e) => {
                // STALL: byte 15 tells why; no answer at all is a lost device.
                let Some(data) = device.data(true) else {
                    return self.lost(e);
                };
                let text = match data.write {
                    WriteStatus::Invalid => tr("The watchdog rejected the settings"),
                    WriteStatus::Pending => tr("The previous settings are still being saved"),
                    _ => tr("The watchdog did not accept the settings"),
                };
                self.journal.warn(trf("Settings not saved: {}", &[&text]));
                self.notice(false, text.into());
            }
        }
    }

    /// Follows an accepted write through byte 15 of the data report.
    fn check_write(&mut self, now: Instant) {
        let Some(write) = &mut self.write else {
            return;
        };
        if now < write.next_poll {
            return;
        }
        write.next_poll = now + WRITE_POLL;
        let timed_out = now >= write.deadline;
        let wanted = write.settings;
        let Some(device) = &mut self.device else {
            self.write = None;
            self.mutate(|s| s.writing = Writing::Ready);
            return;
        };
        let status = device.data(true).map(|d| d.write);
        let failure = match status {
            Some(WriteStatus::Saved) => None,
            // A USB reset clears byte 15. A report accepted before it is
            // still stored, so the settings themselves tell.
            Some(WriteStatus::Idle) if device.settings() == Some(wanted) => None,
            Some(WriteStatus::Busy) => Some(tr(
                "The watchdog is busy with a restart; the settings were not saved",
            )),
            Some(WriteStatus::Invalid) => Some(tr("The watchdog rejected the settings")),
            Some(WriteStatus::Failed) => Some(tr("The watchdog could not write its flash memory")),
            Some(WriteStatus::Other(_)) => Some(tr("The watchdog reported an unknown result")),
            _ if timed_out => Some(tr("The watchdog did not confirm the settings")),
            _ => return,
        };
        self.write = None;
        let stored = self.device.as_mut().and_then(|d| d.settings());
        let saved = failure.is_none();
        let paused = self.config.timer_paused;
        self.mutate(|s| {
            s.writing = Writing::Ready;
            match stored {
                Some(stored) => s.settings = Some(stored),
                // The device confirmed the write; only the read-back failed.
                None if saved => s.settings = Some(wanted),
                None => {}
            }
        });
        // A failed read-back is retried with the next heartbeat.
        self.next_settings = if stored.is_some() {
            now + SETTINGS_REFRESH
        } else {
            now
        };
        match failure {
            None if stored.is_none_or(|s| s == wanted) => {
                self.journal
                    .info(trf("Watchdog settings saved: {}", &[&wanted.text()]));
                self.notice(true, tr("Settings saved to the watchdog").into());
                // Applying settings restarts the device timer, which also
                // ends a suspend.
                if paused {
                    self.send_quiet(Command::Pause(true));
                }
                self.next_beat = now;
            }
            None => {
                let text = tr("The watchdog stored different settings");
                self.journal.warn(text);
                self.notice(false, text.into());
            }
            Some(text) => {
                self.journal.warn(trf("Settings not saved: {}", &[&text]));
                self.notice(false, text.into());
            }
        }
    }

    fn pro2(&self) -> bool {
        self.device.is_some()
            && self
                .status
                .lock()
                .unwrap_or_else(|e| e.into_inner())
                .model()
                == Some(Model::Pro2)
    }

    /// Sends to the connected device; a failure drops the connection.
    fn send_quiet(&mut self, command: Command) -> bool {
        let Some(device) = &mut self.device else {
            return false;
        };
        match device.send(command) {
            Ok(()) => true,
            Err(e) => {
                self.lost(e);
                false
            }
        }
    }

    /// Whether the Pro2's timer is suspended now.
    fn suspend_before_exit(&mut self) -> bool {
        if !self.pro2() {
            return false;
        }
        if self.status.lock().unwrap_or_else(|e| e.into_inner()).paused {
            return true;
        }
        let ok = self.send_quiet(Command::Pause(true));
        if ok {
            self.mutate(|s| s.paused = true);
            self.journal
                .info(tr("Watchdog timer suspended before exit"));
        }
        ok
    }

    // ---- periodic work --------------------------------------------------

    fn tick(&mut self, now: Instant) {
        if now >= self.next_scan {
            self.scan(now);
        }
        self.check_write(now);
        if self.device.is_some() && now >= self.next_beat {
            self.next_beat = now + HEARTBEAT_INTERVAL;
            self.heartbeat(now);
        }
    }

    fn scan(&mut self, now: Instant) {
        match self.bus.scan() {
            Ok(found) => self.found = found,
            Err(e) => {
                log::warn!("HID enumeration: {e}");
                self.found.clear();
            }
        }
        let serials: Vec<String> = self.found.iter().map(|f| f.serial.clone()).collect();
        if self
            .status
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .devices
            != serials
        {
            for s in &serials {
                if !self
                    .status
                    .lock()
                    .unwrap_or_else(|e| e.into_inner())
                    .devices
                    .contains(s)
                {
                    self.journal.info(trf("Detected watchdog {}", &[s]));
                }
            }
            self.mutate(|st| st.devices = serials);
        }

        let current = self
            .status
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .serial
            .clone();
        let candidates: Vec<Found> = match &self.config.serial {
            Some(serial) => self
                .found
                .iter()
                .filter(|f| &f.serial == serial)
                .cloned()
                .collect(),
            // Auto: the watchdog served (or last tried) goes first, so one
            // plugged in later never takes over and leaves it unfed; the
            // others are tried when it cannot be opened.
            None => {
                let mut all = self.found.clone();
                if let Some(at) = all.iter().position(|f| Some(&f.serial) == current.as_ref()) {
                    all[..=at].rotate_right(1);
                }
                all
            }
        };
        if self.device.is_some() && candidates.first().map(|f| &f.serial) != current.as_ref() {
            self.journal.warn(tr("The watchdog was disconnected"));
            self.disconnect();
        }
        if self.device.is_none() {
            let mut failed = false;
            for found in candidates {
                match self.connect(&found, now) {
                    Ok(()) => break,
                    // Only the first failure is shown and logged: it is the
                    // same one on every retry.
                    Err((link, text)) if !failed => {
                        failed = true;
                        self.mutate(|s| s.serial = Some(found.serial));
                        self.problem(link, text);
                    }
                    Err((_, text)) => log::debug!("{text}"),
                }
            }
            if self.device.is_none() && !failed {
                let (link, text) = match &self.config.serial {
                    Some(s) => (
                        Link::Missing(s.clone()),
                        trf("Watchdog {} is not connected", &[s]),
                    ),
                    None => (
                        Link::NoDevice,
                        tr("No connected watchdogs detected").to_string(),
                    ),
                };
                let serial = self.config.serial.clone();
                self.mutate(|s| s.serial = serial);
                self.problem(link, text);
            }
        }
        self.next_scan = now
            + if self.device.is_some() {
                SCAN_CONNECTED
            } else {
                SCAN_IDLE
            };
    }

    /// Opens a watchdog and brings it in line with the settings; a failure
    /// comes back for the caller to report.
    fn connect(&mut self, found: &Found, now: Instant) -> Result<(), (Link, String)> {
        let serial = found.serial.clone();
        let mut device = match self.bus.open(found) {
            Ok(d) => d,
            Err(OpenError::AccessDenied(e)) => {
                log::warn!("{serial}: {e}");
                return Err((
                    Link::AccessDenied,
                    trf("No access to watchdog {}", &[&serial]),
                ));
            }
            Err(OpenError::Other(e)) => {
                let text = trf("Cannot open watchdog {}: {}", &[&serial, &e]);
                return Err((Link::Failed(e), text));
            }
        };
        let firmware = device.firmware().map_err(|e| {
            let text = trf("Watchdog {} does not answer: {}", &[&serial, &e]);
            (Link::Failed(e), text)
        })?;
        if firmware.model == Model::Unknown {
            let raw = firmware.raw.clone();
            let text = trf("Unsupported device {} ({})", &[&serial, &raw]);
            return Err((Link::Unsupported(raw), text));
        }

        let pro2 = firmware.model == Model::Pro2;
        // Only the Pro2 has a timer to suspend.
        let paused = pro2 && self.config.timer_paused;
        let ok = device.send(Command::Led(self.config.led_on)).is_ok()
            && (!pro2 || device.send(Command::Pause(paused)).is_ok());
        if !ok {
            return Err((
                Link::Failed(tr("write failed").into()),
                trf("Watchdog {} rejects commands", &[&serial]),
            ));
        }
        self.logged_problem = None;
        let model = if pro2 { tr("Pro2") } else { tr("Lite") };
        self.journal.info(trf(
            "Connected to watchdog {} {}, firmware {}",
            &[&model, &serial, &firmware.raw],
        ));
        let settings = if pro2 { device.settings() } else { None };
        // Pro2 firmware before 1.8U has no HID writes.
        let writing = if firmware.has_diagnostics() {
            Writing::Ready
        } else {
            Writing::Unsupported
        };
        self.mutate(|s| {
            s.serial = Some(serial);
            s.link = Link::Connected;
            s.firmware = Some(firmware);
            s.settings = settings;
            s.paused = paused;
            s.writing = writing;
        });
        self.device = Some(device);
        self.next_beat = now;
        self.next_settings = now + SETTINGS_REFRESH;
        Ok(())
    }

    fn disconnect(&mut self) {
        self.device = None;
        self.write = None;
        self.mutate(|s| {
            s.link = Link::Searching;
            s.firmware = None;
            s.settings = None;
            s.data = None;
            s.writing = Writing::Ready;
        });
    }

    fn lost(&mut self, error: String) {
        let serial = self
            .status
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .serial
            .clone()
            .unwrap_or_default();
        self.device = None;
        self.write = None;
        self.problem(
            Link::Failed(error.clone()),
            trf("Lost connection to watchdog {}: {}", &[&serial, &error]),
        );
        self.next_scan = Instant::now();
    }

    fn heartbeat(&mut self, now: Instant) {
        let Some(device) = &mut self.device else {
            return;
        };
        if let Err(e) = device.send(Command::Heartbeat) {
            return self.lost(e);
        }
        let diagnostics = self
            .status
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .firmware
            .as_ref()
            .is_some_and(|f| f.has_diagnostics());
        let data = device.data(diagnostics);
        let refresh = now >= self.next_settings && self.write.is_none();
        let settings = (refresh && self.pro2()).then(|| {
            self.next_settings = now + SETTINGS_REFRESH;
            self.device.as_mut().and_then(|d| d.settings())
        });
        self.mutate(|s| {
            s.beats += 1;
            s.last_beat = Some(now);
            s.data = data;
            if data.is_some() {
                s.data_time = Some(Local::now());
            }
            if let Some(Some(settings)) = settings {
                s.settings = Some(settings);
            }
        });
    }
}

// ---- tests ---------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::LogConfig;
    use crate::demo::FakeBus;
    use crate::hid::InState;

    fn engine(bus: &FakeBus, config: EngineConfig) -> Engine {
        let journal = Journal::new(
            std::env::temp_dir().join("wdtmon-rs-test.log"),
            LogConfig::default(),
        );
        Engine::new(
            Box::new(bus.clone()),
            config,
            Arc::new(Mutex::new(Status::default())),
            journal,
            Repaint::default(),
            Box::new(|_| {}),
        )
    }

    fn config() -> EngineConfig {
        EngineConfig {
            serial: None,
            led_on: true,
            timer_paused: false,
        }
    }

    fn status(e: &Engine) -> Status {
        e.status.lock().unwrap().clone()
    }

    #[test]
    fn connects_syncs_and_beats() {
        let bus = FakeBus::pro2(&["AAA", "BBB"]);
        let mut e = engine(&bus, config());
        let t0 = Instant::now();
        e.tick(t0);
        e.publish();
        let st = status(&e);
        assert_eq!(st.link, Link::Connected);
        assert_eq!(st.serial.as_deref(), Some("AAA"));
        assert_eq!(st.devices, vec!["AAA", "BBB"]);
        assert_eq!(st.settings.unwrap().reboot_min, 5);
        // LED on, timer resumed, then the first heartbeat.
        let codes: Vec<u8> = bus.commands().iter().map(|c| c.1).collect();
        assert_eq!(codes, vec![15, 17, 1]);
        assert_eq!(st.beats, 1);
        assert_eq!(st.health, Health::Ok);
        let data = st.data.unwrap();
        assert_eq!(data.input, InState::Temperature(Some(21.3)));
        assert!(data.diagnostics.is_some());
        assert_eq!(st.timer_left_running(), Some(5));

        // Nothing more until the interval passes.
        e.tick(t0 + HEARTBEAT_INTERVAL / 2);
        assert_eq!(bus.commands().len(), 3);
        e.tick(t0 + HEARTBEAT_INTERVAL);
        assert_eq!(bus.commands().last(), Some(&("AAA".to_string(), 1)));
        assert_eq!(status(&e).beats, 2);
    }

    #[test]
    fn follows_the_chosen_serial_and_replug() {
        let bus = FakeBus::pro2(&["AAA", "BBB"]);
        let mut e = engine(
            &bus,
            EngineConfig {
                serial: Some("BBB".into()),
                ..config()
            },
        );
        let t0 = Instant::now();
        e.tick(t0);
        assert_eq!(status(&e).serial.as_deref(), Some("BBB"));
        assert!(bus.commands().iter().all(|c| c.0 == "BBB"));

        // Unplugged: the next heartbeat fails, the rescan reports it missing
        // and does not fall back to the other watchdog.
        bus.0.lock().unwrap().plugged = vec!["AAA".into()];
        e.tick(t0 + HEARTBEAT_INTERVAL);
        e.tick(t0 + HEARTBEAT_INTERVAL);
        e.publish();
        let st = status(&e);
        assert_eq!(st.link, Link::Missing("BBB".into()));
        assert_eq!(st.health, Health::Problem);
        assert!(bus.commands().iter().all(|c| c.0 == "BBB"));

        // Plugged back: found on the next scan.
        bus.0.lock().unwrap().plugged = vec!["AAA".into(), "BBB".into()];
        e.tick(t0 + HEARTBEAT_INTERVAL + SCAN_IDLE);
        assert_eq!(status(&e).link, Link::Connected);
    }

    #[test]
    fn auto_keeps_the_watchdog_it_serves() {
        let bus = FakeBus::pro2(&["AAA"]);
        let mut e = engine(&bus, config());
        let t0 = Instant::now();
        e.tick(t0);
        assert_eq!(status(&e).serial.as_deref(), Some("AAA"));
        // A second one enumerates first: AAA is still fed.
        bus.state().plugged = vec!["BBB".into(), "AAA".into()];
        e.tick(t0 + SCAN_CONNECTED);
        let st = status(&e);
        assert_eq!(st.link, Link::Connected);
        assert_eq!(st.serial.as_deref(), Some("AAA"));
        assert_eq!(st.devices, vec!["BBB", "AAA"]);
        assert!(bus.commands().iter().all(|c| c.0 == "AAA"));
        // AAA unplugged: BBB takes over.
        bus.state().plugged = vec!["BBB".into()];
        e.tick(t0 + SCAN_CONNECTED * 2);
        assert_eq!(status(&e).serial.as_deref(), Some("BBB"));
        assert_eq!(status(&e).link, Link::Connected);
    }

    #[test]
    fn auto_skips_a_watchdog_it_cannot_open() {
        let bus = FakeBus::pro2(&["AAA", "BBB"]);
        bus.state().unopenable = vec!["AAA".into()];
        let mut e = engine(&bus, config());
        e.tick(Instant::now());
        let st = status(&e);
        assert_eq!(st.link, Link::Connected);
        assert_eq!(st.serial.as_deref(), Some("BBB"));

        // Neither opens: the first one's problem is shown.
        let bus = FakeBus::pro2(&["AAA", "BBB"]);
        bus.state().unopenable = vec!["AAA".into(), "BBB".into()];
        let mut e = engine(&bus, config());
        e.tick(Instant::now());
        let st = status(&e);
        assert!(matches!(st.link, Link::Failed(_)));
        assert_eq!(st.serial.as_deref(), Some("AAA"));
    }

    #[test]
    fn configure_pushes_led_and_pause() {
        let bus = FakeBus::pro2(&["AAA"]);
        let mut e = engine(&bus, config());
        e.tick(Instant::now());
        e.handle(Request::Configure(EngineConfig {
            led_on: false,
            timer_paused: true,
            ..config()
        }));
        let codes: Vec<u8> = bus.commands().iter().map(|c| c.1).collect();
        assert_eq!(&codes[codes.len() - 2..], &[14, 16]);
        e.publish();
        let st = status(&e);
        assert!(st.paused);
        assert_eq!(st.health, Health::Paused);
        assert_eq!(st.timer_left_running(), None);
    }

    #[test]
    fn suspend_on_exit_and_tests() {
        let bus = FakeBus::pro2(&["AAA"]);
        let mut e = engine(&bus, config());
        e.tick(Instant::now());
        e.handle(Request::Command(Command::TestReset));
        assert_eq!(bus.commands().last().unwrap().1, 2);
        assert!(status(&e).notices.iter().any(|n| n.ok));
        assert!(e.suspend_before_exit());
        assert_eq!(bus.commands().last().unwrap().1, 16);
    }

    #[test]
    fn lite_gets_no_pause_and_no_tests() {
        let bus = FakeBus::pro2(&["LLL"]);
        bus.0.lock().unwrap().version = "1.2L 1712011200".into();
        let mut e = engine(&bus, config());
        e.tick(Instant::now());
        let st = status(&e);
        assert_eq!(st.model(), Some(Model::Lite));
        assert!(!st.has_tests());
        assert_eq!(st.timer_left_running(), None);
        let codes: Vec<u8> = bus.commands().iter().map(|c| c.1).collect();
        assert_eq!(codes, vec![15, 1]);
        assert!(!e.suspend_before_exit());
    }

    #[test]
    fn a_suspend_left_from_a_pro2_does_not_show_on_a_lite() {
        let bus = FakeBus::pro2(&["LLL"]);
        bus.state().version = "1.2L 1712011200".into();
        let paused = EngineConfig {
            timer_paused: true,
            ..config()
        };
        let mut e = engine(&bus, paused.clone());
        e.tick(Instant::now());
        e.publish();
        let st = status(&e);
        assert!(!st.paused);
        assert_eq!(st.health, Health::Ok);
        // Nor does switching it on the Lite.
        e.handle(Request::Configure(config()));
        e.handle(Request::Configure(paused));
        e.publish();
        assert!(!status(&e).paused);
        assert!(bus.commands().iter().all(|c| c.1 != 16 && c.1 != 17));
    }

    #[test]
    fn access_denied_and_unknown_firmware() {
        let bus = FakeBus::pro2(&["AAA"]);
        bus.0.lock().unwrap().denied = true;
        let mut e = engine(&bus, config());
        let t0 = Instant::now();
        e.tick(t0);
        assert_eq!(status(&e).link, Link::AccessDenied);
        assert!(bus.commands().is_empty());

        bus.0.lock().unwrap().denied = false;
        bus.0.lock().unwrap().version = "9.9Q".into();
        e.tick(t0 + SCAN_IDLE);
        assert_eq!(status(&e).link, Link::Unsupported("9.9Q".into()));
        assert!(bus.commands().is_empty());
    }

    /// Runs the engine until the pending write ends.
    fn settle(e: &mut Engine, mut t: Instant) -> Instant {
        for _ in 0..10 {
            t += WRITE_POLL;
            e.tick(t);
            if e.write.is_none() {
                break;
            }
        }
        t
    }

    #[test]
    fn writes_settings_and_restores_the_suspend() {
        let bus = FakeBus::pro2(&["AAA"]);
        let mut e = engine(
            &bus,
            EngineConfig {
                timer_paused: true,
                ..config()
            },
        );
        let t0 = Instant::now();
        e.tick(t0);
        assert_eq!(status(&e).writing, Writing::Ready);
        let wanted = Settings {
            reboot_min: 3,
            ch2: crate::hid::ChannelMode::Off,
            temp_limit: 70,
            internal_sensor: true,
            ..Settings::default()
        };
        let before = bus.commands().len();
        e.write_settings(wanted, t0);
        assert_eq!(status(&e).writing, Writing::Saving);
        // A second write while the first is pending is refused here.
        e.write_settings(Settings::default(), t0);
        assert!(status(&e).notices.iter().any(|n| !n.ok));
        settle(&mut e, t0);
        let st = status(&e);
        assert_eq!(st.writing, Writing::Ready);
        assert_eq!(st.settings, Some(wanted));
        assert!(st.notices.iter().any(|n| n.ok));
        assert_eq!(bus.state().settings[..11], *b"32532100346");
        assert_eq!(bus.state().settings[11], 1);
        // The device restarted its timer; the suspend is sent again.
        let codes: Vec<u8> = bus.commands()[before..].iter().map(|c| c.1).collect();
        assert_eq!(codes.first(), Some(&16));
    }

    #[test]
    fn a_usb_reset_during_a_write_reads_the_settings_back() {
        let bus = FakeBus::pro2(&["AAA"]);
        let mut e = engine(&bus, config());
        let t0 = Instant::now();
        e.tick(t0);
        for stored in [true, false] {
            let wanted = Settings {
                reboot_min: if stored { 9 } else { 11 },
                ..Settings::default()
            };
            let notices = status(&e).notices.len();
            e.write_settings(wanted, t0);
            assert_eq!(status(&e).writing, Writing::Saving);
            bus.state().usb_reset(stored);
            settle(&mut e, t0);
            let st = status(&e);
            assert_eq!(st.writing, Writing::Ready);
            assert_eq!(st.settings, Some(wanted));
            assert_eq!(st.notices.len(), notices + 1);
            assert!(st.notices.last().is_some_and(|n| n.ok));
        }
    }

    #[test]
    fn a_failed_read_back_still_reports_the_save() {
        let bus = FakeBus::pro2(&["AAA"]);
        let mut e = engine(&bus, config());
        let t0 = Instant::now();
        e.tick(t0);
        let wanted = Settings {
            reboot_min: 8,
            ..Settings::default()
        };
        e.write_settings(wanted, t0);
        bus.state().no_settings = true;
        settle(&mut e, t0);
        let st = status(&e);
        assert_eq!(st.writing, Writing::Ready);
        assert_eq!(st.settings, Some(wanted));
        assert!(st.notices.last().is_some_and(|n| n.ok));
    }

    #[test]
    fn write_failures() {
        let bus = FakeBus::pro2(&["AAA"]);
        let mut e = engine(&bus, config());
        let t0 = Instant::now();
        e.tick(t0);

        // Busy: a forced action was pending in the device.
        bus.state().busy = true;
        e.write_settings(
            Settings {
                reboot_min: 7,
                ..Settings::default()
            },
            t0,
        );
        settle(&mut e, t0);
        let st = status(&e);
        assert_eq!(st.writing, Writing::Ready);
        assert_eq!(st.settings.unwrap().reboot_min, 5);
        assert!(st.notices.last().is_some_and(|n| !n.ok));

        // A STALL with nothing in byte 15 keeps the form usable.
        bus.state().stall = true;
        e.write_settings(Settings::default(), t0);
        let st = status(&e);
        assert_eq!(st.writing, Writing::Ready);
        assert_eq!(st.link, Link::Connected);
        assert!(st.notices.last().is_some_and(|n| !n.ok));
    }

    #[test]
    fn old_pro2_cannot_write() {
        let bus = FakeBus::pro2(&["AAA"]);
        bus.state().version = "1.7U_1806141530".into();
        let mut e = engine(&bus, config());
        e.tick(Instant::now());
        assert_eq!(status(&e).writing, Writing::Unsupported);
    }

    #[test]
    fn commands_without_device_report_a_notice() {
        let bus = FakeBus::pro2(&[]);
        let mut e = engine(&bus, config());
        e.tick(Instant::now());
        e.handle(Request::Command(Command::TestPowerCycle));
        let st = status(&e);
        assert_eq!(st.link, Link::NoDevice);
        assert!(st.notices.iter().any(|n| !n.ok));
    }
}
