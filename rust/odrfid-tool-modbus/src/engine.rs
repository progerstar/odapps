//! The engine thread: owns the serial port, finds the reader, polls its
//! state and carries out what the window asks. The window only reads the
//! shared [`Status`] and sends [`Request`]s, so a slow line never freezes it.

use std::sync::mpsc::{self, Receiver, RecvTimeoutError, Sender};
use std::sync::{Arc, Mutex, MutexGuard};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

use chrono::{DateTime, Local};

use crate::i18n::{tr, trf};
use crate::modbus::{Client, EXC_BUSY, EXC_DEVICE_FAILURE, Error, Line, Patience};
use crate::reader::{
    BusSettings, CmdRequest, CmdResponse, CmdStatus, Events, Identity, Indicate, Indication, Live,
    SelfTest, Settings, holding, input,
};
use crate::serial::{Bus, OpenError, PortInfo};
use crate::traffic::Traffic;

const TICK: Duration = Duration::from_millis(50);
/// State poll while connected.
pub const POLL: Duration = Duration::from_millis(400);
const INDICATION_POLL: Duration = Duration::from_secs(2);
/// Retry while the reader or the port is gone.
const RETRY: Duration = Duration::from_millis(1500);
/// Failed polls in a row before the reader counts as lost.
const LOST_AFTER: u32 = 3;
/// How long writes wait out exception 06 (a card enumeration runs).
const BUSY_WAIT: Duration = Duration::from_secs(3);
/// How long an enumeration may run.
const RUNNING_WAIT: Duration = Duration::from_secs(6);
/// Command results kept for the window.
const HISTORY: usize = 50;
/// How long a reader that lost the answer to a restart, reset or bootloader
/// request is waited for, and how often it is asked meanwhile.
const RESTART_WAIT: Duration = Duration::from_millis(if cfg!(test) { 300 } else { 3000 });
const RESTART_STEP: Duration = Duration::from_millis(if cfg!(test) { 20 } else { 300 });

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

/// Where the reader is: the port, its line settings and the address.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Target {
    pub port: String,
    pub line: Line,
    /// `None`: scan addresses 1..=247.
    pub address: Option<u8>,
    /// The reader's end-of-frame pause (register 537), ms: it answers that
    /// much later, so every wait, the scan's too, grows by it.
    pub frame_pause_ms: u16,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub enum Link {
    #[default]
    Idle,
    Opening,
    /// Probing addresses; `at` is the one being asked.
    Scanning {
        at: u8,
    },
    /// Several readers answered the scan: the user picks one.
    Choose,
    /// Nobody answers (at the address, or anywhere in the scan).
    NotFound,
    Connected,
    /// The reader stopped answering; the engine keeps asking.
    Lost(String),
    PortError(String),
    /// The OS denies the port (Linux: the user is not in `dialout`).
    AccessDenied(String),
    /// Another program holds the port.
    PortBusy(String),
    /// Something answers, but it is not an ODRFID-485 with firmware 4.x.
    Unsupported(String),
    /// Restarted into the RS-485 bootloader for a firmware update.
    Bootloader,
}

/// A device that answered the scan.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Found {
    pub address: u8,
    pub identity: Option<Identity>,
    /// Why it cannot be used, if it cannot.
    pub note: Option<String>,
}

impl Found {
    pub fn usable(&self) -> bool {
        self.note.is_none() && self.identity.is_some_and(|i| i.supported())
    }
}

pub enum Request {
    Connect(Target),
    Disconnect,
    /// Picks a reader from the scan.
    Choose(u8),
    RefreshPorts,
    /// Reads the settings, indication and events again.
    Reload,
    WriteSettings(Settings),
    Indicate(Indicate),
    Beep {
        count: u16,
        on_ms: u16,
        off_ms: u16,
    },
    /// Acknowledges events up to this number.
    Ack(u16),
    Command(CmdRequest),
    /// Autoscan on or off, in RAM only (card operations need it off).
    SetScan(bool),
    Restart,
    Bootloader,
    FactoryReset,
    Shutdown(Sender<()>),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Busy {
    Saving,
    Command,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Notice {
    pub ok: bool,
    pub text: String,
}

#[derive(Clone, Debug)]
pub struct CmdOutcome {
    pub request: CmdRequest,
    pub time: DateTime<Local>,
    pub result: Result<CmdResponse, String>,
}

#[derive(Clone, Debug, Default)]
pub struct Status {
    pub ports: Vec<PortInfo>,
    pub target: Option<Target>,
    pub link: Link,
    pub found: Vec<Found>,
    pub identity: Option<Identity>,
    /// `4.0F Nov 23 2026` from function 0x2B.
    pub build: Option<String>,
    /// The settings in the reader's RAM.
    pub settings: Option<Settings>,
    /// Autoscan as saved, while [`Request::SetScan`] holds another value in
    /// RAM until the reader restarts. Saving keeps both.
    pub scan_saved: Option<bool>,
    pub live: Option<Live>,
    /// Input 25..29; `None` from builds without the self-test.
    pub self_test: Option<SelfTest>,
    pub indication: Option<Indication>,
    pub events: Option<Events>,
    /// When a new card event last arrived.
    pub last_event_at: Option<Instant>,
    pub busy: Option<Busy>,
    pub commands: Vec<CmdOutcome>,
    pub notices: Vec<Notice>,
}

impl Status {
    pub fn connected(&self) -> bool {
        self.link == Link::Connected
    }

    pub fn scan_on(&self) -> bool {
        self.settings.is_some_and(|s| s.scan)
    }
}

pub struct EngineHandle {
    tx: Sender<Request>,
    status: Arc<Mutex<Status>>,
    thread: Option<JoinHandle<()>>,
}

impl EngineHandle {
    pub fn send(&self, request: Request) {
        let _ = self.tx.send(request);
    }

    pub fn status(&self) -> MutexGuard<'_, Status> {
        self.status.lock().unwrap_or_else(|e| e.into_inner())
    }

    pub fn take_notices(&self) -> Vec<Notice> {
        std::mem::take(&mut self.status().notices)
    }

    /// Stops the engine thread; the port closes with it.
    pub fn shutdown(&mut self) {
        let Some(thread) = self.thread.take() else {
            return;
        };
        let (done_tx, done_rx) = mpsc::channel();
        self.send(Request::Shutdown(done_tx));
        // A command on a slow line may hold the thread a little longer.
        if done_rx.recv_timeout(Duration::from_secs(8)).is_ok() {
            let _ = thread.join();
        }
    }
}

impl Drop for EngineHandle {
    fn drop(&mut self) {
        self.shutdown();
    }
}

pub type BusFactory = Box<dyn FnOnce() -> Box<dyn Bus> + Send>;

pub fn spawn(bus: BusFactory, traffic: Traffic, repaint: Repaint) -> EngineHandle {
    let (tx, rx) = mpsc::channel();
    let status = Arc::new(Mutex::new(Status::default()));
    let shared = status.clone();
    let thread = thread::Builder::new()
        .name("engine".into())
        .spawn(move || Engine::new(bus(), shared, traffic, repaint).run(rx))
        .expect("spawn engine thread");
    EngineHandle {
        tx,
        status,
        thread: Some(thread),
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Phase {
    /// Nothing to do until the user asks.
    Idle,
    Scanning {
        next: u16,
    },
    Connected,
    /// The port is open, the reader does not answer: ask again.
    Waiting,
    /// The port failed: open it again.
    Reopening,
}

struct Engine {
    bus: Box<dyn Bus>,
    client: Option<Client>,
    target: Option<Target>,
    phase: Phase,
    status: Arc<Mutex<Status>>,
    traffic: Traffic,
    repaint: Repaint,
    /// The last command block sequence number used.
    seq: Option<u16>,
    failures: u32,
    serial: Option<u64>,
    boot_count: Option<u16>,
    /// Until when a new start count is the restart the user asked for.
    restart_expected: Option<Instant>,
    last_event: Option<u16>,
    next_poll: Instant,
    next_indication: Instant,
    next_retry: Instant,
    changed: bool,
}

impl Engine {
    fn new(
        bus: Box<dyn Bus>,
        status: Arc<Mutex<Status>>,
        traffic: Traffic,
        repaint: Repaint,
    ) -> Self {
        let now = Instant::now();
        Self {
            bus,
            client: None,
            target: None,
            phase: Phase::Idle,
            status,
            traffic,
            repaint,
            seq: None,
            failures: 0,
            serial: None,
            boot_count: None,
            restart_expected: None,
            last_event: None,
            next_poll: now,
            next_indication: now,
            next_retry: now,
            changed: true,
        }
    }

    fn run(mut self, rx: Receiver<Request>) {
        loop {
            // A scan runs flat out between requests.
            let wait = if matches!(self.phase, Phase::Scanning { .. }) {
                Duration::ZERO
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
                if let Request::Shutdown(done) = request {
                    self.client = None;
                    let _ = done.send(());
                    return;
                }
                self.handle(request);
                self.publish();
            }
            self.tick(Instant::now());
            self.publish();
        }
    }

    fn mutate<R>(&mut self, f: impl FnOnce(&mut Status) -> R) -> R {
        self.changed = true;
        f(&mut self.status.lock().unwrap_or_else(|e| e.into_inner()))
    }

    fn publish(&mut self) {
        if std::mem::take(&mut self.changed) {
            self.repaint.request();
        }
    }

    fn notice(&mut self, ok: bool, text: impl Into<String>) {
        let text = text.into();
        if ok {
            log::info!("{text}");
        } else {
            log::warn!("{text}");
        }
        self.mutate(|s| {
            s.notices.push(Notice { ok, text });
            if s.notices.len() > 10 {
                s.notices.remove(0);
            }
        });
    }

    fn set_link(&mut self, link: Link) {
        if self.status.lock().unwrap_or_else(|e| e.into_inner()).link != link {
            self.mutate(|s| s.link = link);
        }
    }

    // ---- requests ----------------------------------------------------------

    /// Marks what follows as routine polling (hidden in the frame log
    /// unless asked for) or as the user's own request.
    fn background(&mut self, on: bool) {
        if let Some(c) = &mut self.client {
            c.background = on;
        }
    }

    fn handle(&mut self, request: Request) {
        self.background(false);
        match request {
            Request::Connect(target) => self.connect(target),
            Request::Disconnect => self.disconnect(Link::Idle),
            Request::Choose(address) => self.choose(address),
            Request::RefreshPorts => self.refresh_ports(),
            Request::Reload => {
                if self.ready() {
                    self.reload();
                }
            }
            Request::WriteSettings(settings) => {
                if self.ready() {
                    self.mutate(|s| s.busy = Some(Busy::Saving));
                    self.publish();
                    self.write_settings(settings);
                    self.mutate(|s| s.busy = None);
                }
            }
            Request::Indicate(what) => {
                if self.ready() {
                    let (register, value) = what.register();
                    let r = self.with_busy_retry(|c| c.write_single(register, value));
                    self.report_failure(r, tr("The indication did not change"));
                    self.read_indication();
                }
            }
            Request::Beep {
                count,
                on_ms,
                off_ms,
            } => {
                if self.ready() {
                    let r = self.with_client(|c| {
                        c.write_multiple(holding::BEEP_ON_MS, &[on_ms.max(1), off_ms.max(1), count])
                    });
                    self.report_failure(r, tr("The beeper did not start"));
                }
            }
            Request::Ack(number) => {
                if self.ready() {
                    let r = self.with_client(|c| c.write_single(holding::QUEUE_ACK, number));
                    self.report_failure(r, tr("The events were not acknowledged"));
                    self.read_events();
                }
            }
            Request::Command(request) => {
                if self.ready() {
                    self.mutate(|s| s.busy = Some(Busy::Command));
                    self.publish();
                    let result = self.command(&request);
                    let outcome = CmdOutcome {
                        request,
                        time: Local::now(),
                        result: result.map_err(|e| e.to_string()),
                    };
                    self.mutate(|s| {
                        s.busy = None;
                        s.commands.push(outcome);
                        if s.commands.len() > HISTORY {
                            s.commands.remove(0);
                        }
                    });
                    self.next_poll = Instant::now();
                }
            }
            Request::SetScan(on) => {
                if self.ready() {
                    let before = self.shared().settings.map(|s| s.scan);
                    match self.with_busy_retry(|c| c.write_single(holding::SCAN, u16::from(on))) {
                        Ok(()) => {
                            // A restart brings back what the reader held
                            // before the first such switch.
                            let saved = self.shared().scan_saved.or(before);
                            self.set_scan_saved(saved.filter(|&s| s != on));
                            self.read_settings();
                        }
                        Err(e) => self.write_failed(e, tr("Autoscan did not change")),
                    }
                    self.next_poll = Instant::now();
                }
            }
            Request::Restart => self.restart(),
            Request::Bootloader => self.bootloader(),
            Request::FactoryReset => self.factory_reset(),
            Request::Shutdown(_) => unreachable!("handled by run()"),
        }
    }

    fn ready(&mut self) -> bool {
        if self.phase == Phase::Connected && self.client.is_some() {
            return true;
        }
        self.notice(false, tr("The reader is not connected"));
        false
    }

    fn with_client<T>(
        &mut self,
        f: impl FnOnce(&mut Client) -> Result<T, Error>,
    ) -> Result<T, Error> {
        match &mut self.client {
            Some(c) => f(c),
            None => Err(Error::Port(tr("the port is closed").into())),
        }
    }

    /// Waits out exception 06 (an enumeration holds the card settings) and
    /// asks again after a lost answer: for writes that may run twice.
    fn with_busy_retry<T>(
        &mut self,
        mut f: impl FnMut(&mut Client) -> Result<T, Error>,
    ) -> Result<T, Error> {
        let start = Instant::now();
        let mut link_retries = 2;
        loop {
            match self.with_client(&mut f) {
                Err(Error::Exception(EXC_BUSY)) if start.elapsed() < BUSY_WAIT => {}
                Err(e) if e.is_link() && link_retries > 0 => link_retries -= 1,
                other => return other,
            }
            thread::sleep(Duration::from_millis(100));
        }
    }

    fn report_failure<T>(&mut self, result: Result<T, Error>, what: &str) {
        if let Err(e) = result {
            if let Error::Port(text) = &e {
                self.port_failed(text.clone());
            }
            self.notice(false, format!("{what}: {e}"));
        }
    }

    /// Reads the settings again after a failed write and reports it. Not
    /// after a port failure: the read could only fail too, and its "the port
    /// is closed" would replace the system's error.
    fn write_failed(&mut self, e: Error, what: &str) {
        if !matches!(e, Error::Port(_)) {
            self.read_settings();
        }
        self.report_failure(Err::<(), _>(e), what);
    }

    fn shared(&self) -> MutexGuard<'_, Status> {
        self.status.lock().unwrap_or_else(|e| e.into_inner())
    }

    fn set_scan_saved(&mut self, saved: Option<bool>) {
        if self.shared().scan_saved != saved {
            self.mutate(|s| s.scan_saved = saved);
        }
    }

    // ---- connecting ----------------------------------------------------------

    fn refresh_ports(&mut self) {
        let ports = self.bus.ports();
        if self.status.lock().unwrap_or_else(|e| e.into_inner()).ports != ports {
            self.mutate(|s| s.ports = ports);
        }
    }

    fn clear_reader(&mut self) {
        self.seq = None;
        self.failures = 0;
        self.serial = None;
        self.boot_count = None;
        self.restart_expected = None;
        self.last_event = None;
        self.mutate(|s| {
            s.identity = None;
            s.build = None;
            s.settings = None;
            s.scan_saved = None;
            s.live = None;
            s.self_test = None;
            s.indication = None;
            s.events = None;
            s.busy = None;
        });
    }

    fn disconnect(&mut self, link: Link) {
        self.client = None;
        self.target = None;
        self.phase = Phase::Idle;
        self.clear_reader();
        self.mutate(|s| {
            s.target = None;
            s.found.clear();
            s.link = link;
        });
    }

    fn connect(&mut self, target: Target) {
        self.client = None;
        self.clear_reader();
        self.target = Some(target.clone());
        self.mutate(|s| {
            s.target = Some(target);
            s.found.clear();
            s.link = Link::Opening;
        });
        self.publish();
        self.open();
    }

    /// Opens the port of the target and starts looking for the reader.
    fn open(&mut self) {
        let Some(target) = self.target.clone() else {
            return;
        };
        let now = Instant::now();
        match self.bus.open(&target.port, &target.line) {
            Ok(port) => {
                let address = target.address.unwrap_or(1);
                let mut client = Client::new(port, target.line, address, self.traffic.clone());
                client.frame_pause = pause(target.frame_pause_ms);
                self.client = Some(client);
                match target.address {
                    Some(address) => {
                        self.set_link(Link::Opening);
                        self.publish();
                        self.probe_target(address);
                    }
                    None => {
                        self.phase = Phase::Scanning { next: 1 };
                        self.mutate(|s| s.found.clear());
                        self.set_link(Link::Scanning { at: 1 });
                    }
                }
            }
            Err(e) => {
                let link = match e {
                    OpenError::AccessDenied(t) => Link::AccessDenied(t),
                    OpenError::Busy(t) => Link::PortBusy(t),
                    OpenError::Missing(t) | OpenError::Other(t) => Link::PortError(t),
                };
                log::warn!("{}: {link:?}", target.port);
                self.set_link(link);
                self.phase = Phase::Reopening;
                self.next_retry = now + RETRY;
            }
        }
    }

    /// Identifies the reader at a fixed address.
    fn probe_target(&mut self, address: u8) {
        match self.identify(address, Patience::Normal) {
            Ok(Some(identity)) if identity.supported() => self.connect_to(address, identity),
            Ok(Some(identity)) => {
                let text = unsupported(&identity);
                self.halt(Link::Unsupported(text));
            }
            Ok(None) => self.halt(Link::Unsupported(
                tr("firmware 3.x: use ODRFIDConfigM").into(),
            )),
            Err(Error::Port(e)) => self.port_failed(e),
            Err(Error::Exception(_)) => self.halt(Link::Unsupported(
                tr("a Modbus device that is not an ODRFID-485").into(),
            )),
            Err(_) => {
                // A lost reader stays "lost" while it is asked again.
                let lost = matches!(
                    self.status.lock().unwrap_or_else(|e| e.into_inner()).link,
                    Link::Lost(_)
                );
                if !lost {
                    self.set_link(Link::NotFound);
                }
                self.phase = Phase::Waiting;
                self.next_retry = Instant::now() + RETRY;
            }
        }
    }

    /// Asks `address` who it is. `Ok(None)`: an ODRFID with firmware 3.x.
    fn identify(&mut self, address: u8, patience: Patience) -> Result<Option<Identity>, Error> {
        let client = self.client.as_mut().ok_or(Error::Timeout)?;
        client.address = address;
        if patience == Patience::Scan {
            client.background = true;
        }
        let attempts = if patience == Patience::Scan { 1 } else { 2 };
        let mut last = Error::Timeout;
        for _ in 0..attempts {
            match client.read(
                crate::modbus::FC_READ_INPUT,
                input::IDENTITY,
                input::IDENTITY_COUNT,
                patience,
            ) {
                Ok(r) => return Ok(Identity::parse(&r)),
                Err(Error::Exception(code)) => {
                    // 3.x has no identity registers but answers the size of
                    // its AT buffer.
                    return match client.read(
                        crate::modbus::FC_READ_INPUT,
                        input::LEGACY_BUFFER,
                        1,
                        patience,
                    ) {
                        Ok(_) => Ok(None),
                        Err(_) => Err(Error::Exception(code)),
                    };
                }
                Err(e) if e.is_link() => last = e,
                Err(e) => return Err(e),
            }
        }
        Err(last)
    }

    fn halt(&mut self, link: Link) {
        self.phase = Phase::Idle;
        self.client = None;
        self.set_link(link);
    }

    fn scan_step(&mut self, next: u16) {
        if next > 247 {
            return self.scan_done();
        }
        let address = next as u8;
        self.set_link(Link::Scanning { at: address });
        let found = match self.identify(address, Patience::Scan) {
            Ok(Some(identity)) => Some(Found {
                address,
                identity: Some(identity),
                note: (!identity.supported()).then(|| unsupported(&identity)),
            }),
            Ok(None) => Some(Found {
                address,
                identity: None,
                note: Some(tr("firmware 3.x: use ODRFIDConfigM").into()),
            }),
            Err(Error::Exception(_)) => Some(Found {
                address,
                identity: None,
                note: Some(tr("a Modbus device that is not an ODRFID-485").into()),
            }),
            Err(Error::Port(e)) => return self.port_failed(e),
            Err(_) => None,
        };
        if let Some(f) = found {
            self.mutate(|s| s.found.push(f));
        }
        self.phase = Phase::Scanning { next: next + 1 };
    }

    fn scan_done(&mut self) {
        let found = self
            .status
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .found
            .clone();
        let usable: Vec<&Found> = found.iter().filter(|f| f.usable()).collect();
        match usable.as_slice() {
            [one] => {
                let (address, identity) = (one.address, one.identity.expect("usable"));
                self.connect_to(address, identity);
            }
            [] => {
                let link = match found.first().and_then(|f| f.note.clone()) {
                    Some(note) => Link::Unsupported(note),
                    None => Link::NotFound,
                };
                self.halt(link);
            }
            _ => {
                self.phase = Phase::Idle;
                self.set_link(Link::Choose);
            }
        }
    }

    fn choose(&mut self, address: u8) {
        let found = self
            .status
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .found
            .iter()
            .find(|f| f.address == address && f.usable())
            .cloned();
        match (found, self.client.is_some()) {
            (Some(f), true) => self.connect_to(address, f.identity.expect("usable")),
            _ => self.notice(false, tr("This reader cannot be used")),
        }
    }

    /// Reads everything about the reader at `address` and starts polling.
    fn connect_to(&mut self, address: u8, identity: Identity) {
        let Some(client) = &mut self.client else {
            return;
        };
        client.address = address;
        client.background = false;
        let build = client.device_id(3, 0).ok().map(|objects| {
            let text = |id| {
                objects
                    .iter()
                    .find(|o| o.0 == id)
                    .map(|o| String::from_utf8_lossy(&o.1).trim().to_string())
                    .unwrap_or_default()
            };
            format!("{} {}", text(2), text(0x81)).trim().to_string()
        });
        let restored = self.phase == Phase::Waiting && self.serial == Some(identity.serial);
        self.serial = Some(identity.serial);
        self.phase = Phase::Connected;
        self.failures = 0;
        self.seq = None;
        if let Some(t) = &mut self.target {
            t.address = Some(address);
        }
        let target = self.target.clone();
        self.mutate(|s| {
            s.identity = Some(identity);
            s.build = build.filter(|b| !b.is_empty());
            s.target = target;
            s.link = Link::Connected;
        });
        self.reload();
        let now = Instant::now();
        self.next_poll = now;
        self.next_indication = now + INDICATION_POLL;
        if restored {
            self.notice(true, tr("Connection restored"));
        }
        log::info!("connected to {} at {address}", identity.serial_text());
    }

    fn reload(&mut self) {
        self.read_settings();
        self.read_self_test();
        self.read_indication();
        let _ = self.poll_live();
        self.read_events();
    }

    fn read_settings(&mut self) {
        let r = self.with_client(|c| c.read_holding(holding::SETTINGS, holding::SETTINGS_COUNT));
        match r.map(|r| Settings::parse(&r)) {
            Ok(Some(settings)) => {
                self.follow_pause(settings.bus.frame_pause_ms);
                self.mutate(|s| s.settings = Some(settings));
            }
            Ok(None) => {}
            Err(e) => self.report_failure(Err::<(), _>(e), tr("Cannot read the settings")),
        }
    }

    /// The pause read from the reader (its RAM copy is the one in use: bus
    /// settings written but not applied are put back) lengthens the waits
    /// and goes with the target to the next connection.
    fn follow_pause(&mut self, ms: u16) {
        if let Some(c) = &mut self.client {
            c.frame_pause = pause(ms);
        }
        if let Some(t) = self.target.as_mut().filter(|t| t.frame_pause_ms != ms) {
            t.frame_pause_ms = ms;
            let target = t.clone();
            self.mutate(|s| s.target = Some(target));
        }
    }

    /// The self-test changes only at a start. Builds without it answer
    /// exception 02: nothing to show then.
    fn read_self_test(&mut self) {
        let r = self.with_client(|c| c.read_input(input::SELF_TEST, input::SELF_TEST_COUNT));
        let test = r.ok().and_then(|r| SelfTest::parse(&r));
        self.mutate(|s| s.self_test = test);
    }

    fn read_indication(&mut self) {
        let r =
            self.with_client(|c| c.read_holding(holding::INDICATION, holding::INDICATION_COUNT));
        if let Ok(Some(indication)) = r.map(|r| Indication::parse(&r)) {
            self.mutate(|s| s.indication = Some(indication));
        }
        self.next_indication = Instant::now() + INDICATION_POLL;
    }

    fn read_events(&mut self) {
        let r = self.with_client(|c| {
            c.read(
                crate::modbus::FC_READ_INPUT,
                input::EVENTS,
                input::EVENTS_COUNT,
                Patience::Normal,
            )
        });
        if let Ok(Some(events)) = r.map(|r| Events::parse(&r)) {
            self.mutate(|s| s.events = Some(events));
        }
    }

    // ---- periodic work ---------------------------------------------------------

    fn tick(&mut self, now: Instant) {
        self.background(true);
        match self.phase {
            Phase::Idle => {}
            Phase::Scanning { next } => self.scan_step(next),
            Phase::Connected => {
                if now >= self.next_poll {
                    self.next_poll = now + POLL;
                    self.poll();
                }
                if self.phase == Phase::Connected && now >= self.next_indication {
                    self.background(true);
                    self.read_indication();
                }
            }
            Phase::Waiting => {
                if now >= self.next_retry {
                    self.next_retry = now + RETRY;
                    if let Some(address) = self.target.as_ref().and_then(|t| t.address) {
                        self.probe_target(address);
                    }
                }
            }
            Phase::Reopening => {
                if now >= self.next_retry {
                    self.next_retry = now + RETRY;
                    self.open();
                }
            }
        }
    }

    fn poll(&mut self) {
        self.background(true);
        match self.poll_live() {
            Ok(()) => self.failures = 0,
            Err(Error::Port(e)) => self.port_failed(e),
            Err(e) => {
                self.failures += 1;
                if self.failures >= LOST_AFTER {
                    self.lost(e.to_string());
                }
            }
        }
    }

    fn poll_live(&mut self) -> Result<(), Error> {
        let regs = self.with_client(|c| {
            c.read(
                crate::modbus::FC_READ_INPUT,
                input::STATUS,
                input::STATUS_COUNT,
                Patience::Normal,
            )
        });
        let regs = regs?;
        let Some(live) = Live::parse(&regs) else {
            return Ok(());
        };
        let restarted = self.boot_count.is_some_and(|b| b != live.boot_count);
        let new_event = self.last_event != Some(live.last_event);
        let first = self.boot_count.is_none();
        self.boot_count = Some(live.boot_count);
        self.last_event = Some(live.last_event);
        let queue_changed = self
            .status
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .events
            .as_ref()
            .is_some_and(|e| e.entries.len() != usize::from(live.queue));
        let event_arrived = new_event && !first && live.last_event != 0;
        self.mutate(|s| {
            s.live = Some(live);
            if event_arrived {
                s.last_event_at = Some(Instant::now());
            }
        });
        if restarted {
            // Unsaved settings, the queue and the last request are gone.
            self.seq = None;
            self.set_scan_saved(None);
            self.read_self_test();
            let asked = self
                .restart_expected
                .take()
                .is_some_and(|until| Instant::now() < until);
            if asked {
                self.notice(true, tr("The reader restarted"));
            } else {
                // Nobody asked: a power cut, the reset pin or the watchdog.
                let cause = self.shared().self_test.map(|t| t.reset_text());
                self.notice(
                    false,
                    match cause {
                        Some(cause) => trf("The reader restarted unexpectedly: {}", &[&cause]),
                        None => tr("The reader restarted unexpectedly").to_string(),
                    },
                );
            }
            self.read_settings();
            self.read_indication();
        }
        if (new_event && !first) || queue_changed || restarted {
            self.read_events();
        }
        Ok(())
    }

    fn lost(&mut self, error: String) {
        self.phase = Phase::Waiting;
        self.next_retry = Instant::now() + RETRY;
        self.mutate(|s| {
            s.link = Link::Lost(error);
            s.live = None;
        });
    }

    fn port_failed(&mut self, error: String) {
        self.client = None;
        self.phase = Phase::Reopening;
        self.next_retry = Instant::now() + RETRY;
        self.seq = None;
        self.mutate(|s| {
            s.link = Link::PortError(error);
            s.live = None;
        });
    }

    // ---- settings ----------------------------------------------------------------

    fn write_settings(&mut self, wanted: Settings) {
        let current = self.shared().settings;
        let Some(current) = current else {
            return self.notice(false, tr("The settings have not been read yet"));
        };
        if let Some(problem) = wanted.problems().first() {
            return self.notice(false, *problem);
        }
        // While autoscan is switched until restart, the window offers the
        // saved value. Saving stores what RAM holds, so RAM gets the saved
        // value for the save and the switched one back afterwards.
        let held = self.shared().scan_saved;
        self.store(&current, &wanted);
        match held {
            Some(saved) if wanted.scan == saved => self.hold_scan(current.scan),
            // Chosen on the Scanning page: it is the saved value now.
            Some(_) => self.set_scan_saved(None),
            None => {}
        }
    }

    /// Puts autoscan back to the value it keeps until the reader restarts.
    fn hold_scan(&mut self, until_restart: bool) {
        let ram = self.shared().settings.map(|s| s.scan);
        if self.phase != Phase::Connected || ram.is_none_or(|s| s == until_restart) {
            return;
        }
        let value = u16::from(until_restart);
        if let Err(e) = self.with_busy_retry(|c| c.write_single(holding::SCAN, value)) {
            self.set_scan_saved(None);
            return self.write_failed(e, tr("Autoscan did not change"));
        }
        self.read_settings();
    }

    /// Writes what changed, moves to new bus settings and saves.
    fn store(&mut self, current: &Settings, wanted: &Settings) {
        let bus_changed = wanted.bus != current.bus;
        if bus_changed {
            // Before anything is written: once the reader switches, a speed
            // the adapter refuses would leave it where the tool cannot follow.
            let line = wanted.bus.line();
            if let Err(e) = self.adapter_takes(line) {
                return self.notice(
                    false,
                    trf("The adapter cannot use {}: {}", &[&line.label(), &e]),
                );
            }
        }
        let card_regs = wanted.card_regs();
        if card_regs != current.card_regs() {
            let r = self.with_busy_retry(|c| c.write_multiple(holding::SETTINGS, &card_regs));
            if let Err(e) = r {
                return self.write_failed(e, tr("Settings not written"));
            }
        }
        if bus_changed {
            let regs = wanted.bus.regs();
            let r = self.with_busy_retry(|c| c.write_multiple(holding::BUS, &regs));
            if let Err(e) = r {
                if !matches!(e, Error::Port(_)) {
                    self.restore_bus(&current.bus);
                }
                return self.report_failure(Err::<(), _>(e), tr("Bus settings not written"));
            }
            if !self.apply_bus(&current.bus, &wanted.bus) {
                return;
            }
        }
        match self.with_busy_retry(|c| c.write_single(holding::SETTINGS_COMMAND, holding::SAVE)) {
            Ok(()) => {}
            Err(Error::Exception(EXC_DEVICE_FAILURE)) => {
                self.read_settings();
                return self.notice(
                    false,
                    tr("The reader could not write its EEPROM: the settings work until it restarts"),
                );
            }
            Err(e) => return self.write_failed(e, tr("Settings not saved")),
        }
        self.read_settings();
        let stored = self.shared().settings;
        if stored == Some(*wanted) {
            if bus_changed {
                self.notice(
                    true,
                    trf(
                        "Settings saved; the reader now uses {}, address {}",
                        &[&wanted.bus.line().label(), &wanted.bus.address],
                    ),
                );
            } else {
                self.notice(true, tr("Settings saved to the reader"));
            }
        } else {
            self.notice(false, tr("The reader stored different settings"));
        }
    }

    /// Switches the reader and the port to the new bus settings and checks
    /// the reader answers there before anything is saved.
    fn apply_bus(&mut self, old: &BusSettings, new: &BusSettings) -> bool {
        // The reader answers on the old settings, then switches. A lost
        // answer may or may not have switched it: the check below tells.
        match self.with_client(|c| c.write_single(holding::SETTINGS_COMMAND, holding::APPLY_BUS)) {
            Ok(()) | Err(Error::Timeout | Error::Frame(_)) => {}
            Err(e) => {
                if !matches!(e, Error::Port(_)) {
                    self.restore_bus(old);
                }
                self.report_failure(Err::<(), _>(e), tr("The bus settings were not applied"));
                return false;
            }
        }
        thread::sleep(Duration::from_millis(50));
        if self.answers_at(new) {
            let target = self.target.as_mut().map(|t| {
                t.line = new.line();
                t.address = Some(new.address);
                t.frame_pause_ms = new.frame_pause_ms;
                t.clone()
            });
            self.mutate(|s| s.target = target);
            return true;
        }
        if self.answers_at(old) {
            self.restore_bus(old);
            self.notice(
                false,
                tr("The reader did not switch to the new bus settings"),
            );
            return false;
        }
        // It runs on the new settings in RAM and does not answer there:
        // after a restart it comes back on the saved, old ones.
        self.notice(
            false,
            trf(
                "The reader does not answer at {}, address {}. Nothing was saved: power the reader off and on to return to {}, address {}.",
                &[&new.line().label(), &new.address, &old.line().label(), &old.address],
            ),
        );
        self.lost(tr("no answer after the bus change").into());
        false
    }

    /// Puts the old bus settings back into RAM, so a later save keeps them
    /// and the window shows what the reader uses.
    fn restore_bus(&mut self, old: &BusSettings) {
        let regs = old.regs();
        let _ = self.with_busy_retry(|c| c.write_multiple(holding::BUS, &regs));
        self.read_settings();
    }

    /// Whether the adapter takes `line`. The port goes back to its line
    /// either way; `Err` is the driver's reason.
    fn adapter_takes(&mut self, line: Line) -> Result<(), String> {
        let Some(client) = &mut self.client else {
            return Err(tr("the port is closed").into());
        };
        let now = client.line;
        if line == now {
            return Ok(());
        }
        let tried = client.set_line(line);
        if let Err(e) = client.set_line(now) {
            let text = e.to_string();
            self.port_failed(text.clone());
            return Err(text);
        }
        tried.map_err(|e| e.to_string())
    }

    /// The bus settings the tool talks to the reader with now.
    fn active_bus(&self) -> Option<BusSettings> {
        let t = self.target.as_ref()?;
        Some(BusSettings {
            address: t.address?,
            baud: t.line.baud,
            parity: t.line.parity,
            stop: t.line.stop,
            frame_pause_ms: t.frame_pause_ms,
        })
    }

    /// Moves the port to `bus`: line, address and the reader's pause.
    fn use_bus(&mut self, bus: &BusSettings) -> bool {
        let Some(client) = &mut self.client else {
            return false;
        };
        if client.set_line(bus.line()).is_err() {
            return false;
        }
        client.address = bus.address;
        client.frame_pause = pause(bus.frame_pause_ms);
        client.background = false;
        true
    }

    /// Moves the port to `bus` and checks the same reader answers there.
    fn answers_at(&mut self, bus: &BusSettings) -> bool {
        let serial = self.serial;
        if !self.use_bus(bus) {
            return false;
        }
        let Some(client) = &mut self.client else {
            return false;
        };
        for _ in 0..3 {
            match client.read_input(input::IDENTITY, input::IDENTITY_COUNT) {
                Ok(r) => return Identity::parse(&r).is_some_and(|i| Some(i.serial) == serial),
                Err(Error::Port(_)) => return false,
                Err(_) => thread::sleep(Duration::from_millis(100)),
            }
        }
        false
    }

    // ---- command block -------------------------------------------------------------

    fn command(&mut self, request: &CmdRequest) -> Result<CmdResponse, Error> {
        let seq = match self.seq {
            Some(s) => s,
            None => {
                // Never repeat the last request's number: it would not run.
                let r = self.with_client(|c| c.read_holding(holding::CMD_RESPONSE, 2))?;
                if CmdStatus::from_code(r[1]) == CmdStatus::None {
                    0
                } else {
                    r[0]
                }
            }
        }
        .wrapping_add(1);
        self.seq = Some(seq);
        let regs = request.regs(seq);
        let start = Instant::now();
        let mut attempts = 0;
        // A request with the same number and contents does not run twice,
        // so a lost answer is simply asked again.
        let answer = loop {
            attempts += 1;
            let r = self.with_client(|c| {
                c.read_write(
                    holding::CMD_RESPONSE,
                    holding::CMD_RESPONSE_COUNT,
                    holding::CMD_REQUEST,
                    &regs,
                    Patience::Card,
                )
            });
            match r {
                Ok(a) => break a,
                Err(Error::Exception(EXC_BUSY)) if start.elapsed() < RUNNING_WAIT => {}
                Err(e) if e.is_link() && attempts < 4 => {}
                Err(e) => return Err(e),
            }
            thread::sleep(Duration::from_millis(50));
        };
        let mut response = CmdResponse::parse(&answer).ok_or(Error::Timeout)?;
        if response.seq != seq {
            return Err(Error::Frame(format!(
                "answer to request {} instead of {seq}",
                response.seq
            )));
        }
        while response.status == CmdStatus::Running {
            if start.elapsed() > RUNNING_WAIT {
                return Err(Error::Timeout);
            }
            thread::sleep(Duration::from_millis(50));
            let r = self.with_client(|c| {
                c.read_holding(holding::CMD_RESPONSE, holding::CMD_RESPONSE_COUNT)
            });
            match r {
                Ok(a) => {
                    if let Some(next) = CmdResponse::parse(&a) {
                        response = next;
                    }
                }
                Err(e) if e.is_link() || e == Error::Exception(EXC_BUSY) => {}
                Err(e) => return Err(e),
            }
        }
        Ok(response)
    }

    // ---- maintenance -----------------------------------------------------------------

    /// Writes a register the reader answers before it restarts. Exception 06
    /// is waited out, but a lost answer is not asked again: the request may
    /// have run, and a second one would restart the reader twice (or reach
    /// the bootloader, which refuses it). The callers check instead.
    fn write_once(&mut self, register: u16, value: u16) -> Result<(), Error> {
        let start = Instant::now();
        loop {
            match self.with_client(|c| c.write_single(register, value)) {
                Err(Error::Exception(EXC_BUSY)) if start.elapsed() < BUSY_WAIT => {}
                other => return other,
            }
            thread::sleep(Duration::from_millis(100));
        }
    }

    /// The next new start count is this request's, not a fault. A request
    /// that did not restart the reader leaves the window to run out.
    fn expect_restart(&mut self) {
        self.restart_expected = Some(Instant::now() + RESTART_WAIT + Duration::from_secs(10));
    }

    /// The start counter of this reader at `bus` (the port moves there), or
    /// `None` when it does not answer there.
    fn boot_count_at(&mut self, bus: &BusSettings) -> Option<u16> {
        if !self.answers_at(bus) {
            return None;
        }
        let r = self
            .with_client(|c| c.read_input(input::STATUS, input::STATUS_COUNT))
            .ok()?;
        Live::parse(&r).map(|l| l.boot_count)
    }

    /// Asks `check` until it has an answer or [`RESTART_WAIT`] passes; the
    /// reader is silent while it starts.
    fn await_restart(&mut self, mut check: impl FnMut(&mut Self) -> Option<bool>) -> Option<bool> {
        let deadline = Instant::now() + RESTART_WAIT;
        loop {
            thread::sleep(RESTART_STEP);
            if let Some(answer) = check(self) {
                return Some(answer);
            }
            if Instant::now() >= deadline {
                return None;
            }
        }
    }

    fn restart(&mut self) {
        if !self.ready() {
            return;
        }
        let Some(bus) = self.active_bus() else {
            return;
        };
        let before = self.boot_count;
        self.expect_restart();
        let restarted = match self.write_once(holding::REBOOT, 1) {
            Ok(()) => Some(true),
            // Restarted: it answers with another start count.
            Err(e) if e.is_link() => {
                self.await_restart(|en| en.boot_count_at(&bus).map(|b| Some(b) != before))
            }
            Err(e) => {
                return self.report_failure(Err::<(), _>(e), tr("The reader did not restart"));
            }
        };
        match restarted {
            Some(true) => {
                self.set_scan_saved(None);
                self.notice(true, tr("The reader is restarting"));
                self.next_poll = Instant::now() + Duration::from_millis(1500);
            }
            Some(false) => self.notice(false, tr("The reader did not restart")),
            // Powered off, or still starting: the polls tell which.
            None => self.notice(false, tr("The reader did not answer the restart request")),
        }
    }

    fn bootloader(&mut self) {
        if !self.ready() {
            return;
        }
        let entered = match self.write_once(holding::BOOTLOADER, 1) {
            Ok(()) => Some(true),
            // The bootloader answers INFO at the same address and line; the
            // firmware answers it with exception 01.
            Err(e) if e.is_link() => {
                self.await_restart(|en| match en.with_client(|c| c.loader_info()) {
                    Ok(_) => Some(true),
                    Err(Error::Exception(_)) => Some(false),
                    Err(_) => None,
                })
            }
            Err(e) => {
                let what = tr("The reader did not enter the bootloader");
                return self.report_failure(Err::<(), _>(e), what);
            }
        };
        match entered {
            Some(true) => {
                // Let the updater have the port.
                self.client = None;
                self.phase = Phase::Idle;
                self.clear_reader();
                self.set_link(Link::Bootloader);
                self.notice(true, tr("The reader is in the RS-485 bootloader"));
            }
            Some(false) => self.notice(false, tr("The reader did not enter the bootloader")),
            None => self.notice(
                false,
                tr("The reader did not answer the bootloader request"),
            ),
        }
    }

    fn factory_reset(&mut self) {
        if !self.ready() {
            return;
        }
        let Some(old) = self.active_bus() else {
            return;
        };
        let factory = BusSettings::factory();
        let before = self.boot_count;
        self.expect_restart();
        let reset = match self.write_once(holding::FACTORY_RESET, holding::FACTORY_RESET_MAGIC) {
            Ok(()) => Some(true),
            // Reset: it answers at the factory settings, started anew. Still
            // at the old ones: it was not.
            Err(e) if e.is_link() => self.await_restart(|en| {
                if let Some(b) = en.boot_count_at(&factory) {
                    return Some(Some(b) != before);
                }
                (old != factory && en.boot_count_at(&old).is_some()).then_some(false)
            }),
            Err(e) => return self.report_failure(Err::<(), _>(e), tr("The factory reset failed")),
        };
        if reset != Some(true) {
            // The polls go on where it was.
            self.use_bus(&old);
            return self.notice(
                false,
                match reset {
                    Some(_) => tr("The factory reset failed").to_string(),
                    None => trf(
                        "The reader did not answer the factory reset. If it was reset, it answers at {}, address {}.",
                        &[&factory.line().label(), &factory.address],
                    ),
                },
            );
        }
        self.use_bus(&factory);
        let target = self.target.as_mut().map(|t| {
            t.line = factory.line();
            t.address = Some(factory.address);
            t.frame_pause_ms = factory.frame_pause_ms;
            t.clone()
        });
        self.set_scan_saved(None);
        self.mutate(|s| s.target = target);
        self.notice(
            true,
            trf(
                "Factory settings restored: {}, address {}",
                &[&factory.line().label(), &factory.address],
            ),
        );
        self.next_poll = Instant::now() + Duration::from_millis(1500);
    }
}

fn pause(ms: u16) -> Duration {
    Duration::from_millis(u64::from(ms))
}

fn unsupported(identity: &Identity) -> String {
    if identity.is_odrfid() {
        trf("firmware {}: this tool needs 4.x", &[&identity.version()])
    } else {
        trf(
            "model {} is not an ODRFID-485",
            &[&format!("{:04X}h", identity.model)],
        )
    }
}

// ---- tests -------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use crate::demo::{FakeBus, FakeCard};
    use crate::modbus::{Parity, StopBits};
    use crate::reader::FACTORY_ADDRESS;

    const PORT: &str = "/dev/ttyDEMO0";

    fn engine(bus: &FakeBus) -> Engine {
        Engine::new(
            Box::new(bus.clone()),
            Arc::new(Mutex::new(Status::default())),
            Traffic::default(),
            Repaint::default(),
        )
    }

    fn status(e: &Engine) -> Status {
        e.status.lock().unwrap().clone()
    }

    fn target(address: Option<u8>) -> Target {
        Target {
            port: PORT.into(),
            line: Line::default(),
            address,
            frame_pause_ms: 0,
        }
    }

    fn connected(bus: &FakeBus) -> Engine {
        let mut e = engine(bus);
        e.handle(Request::Connect(target(Some(FACTORY_ADDRESS))));
        assert_eq!(status(&e).link, Link::Connected);
        e
    }

    /// Runs the engine until it settles or `limit` ticks pass.
    fn run(e: &mut Engine, limit: usize) {
        let mut t = Instant::now();
        for _ in 0..limit {
            t += POLL;
            e.tick(t);
        }
    }

    #[test]
    fn connects_reads_everything_and_polls() {
        let bus = FakeBus::with_readers(&[FACTORY_ADDRESS]);
        bus.with_reader(0, |r| r.insert(FakeCard::classic()));
        let mut e = connected(&bus);
        let st = status(&e);
        let id = st.identity.unwrap();
        assert_eq!(id.serial_text(), "11213141ABCD");
        assert_eq!(st.build.as_deref(), Some("4.0F Nov 23 2026"));
        let settings = st.settings.unwrap();
        assert!(settings.scan);
        assert_eq!(settings.bus, BusSettings::factory());
        let live = st.live.unwrap();
        assert!(live.card_present);
        assert_eq!(live.card.unwrap().uid_text(), "A1B2C3D4");
        assert_eq!(st.events.unwrap().entries.len(), 1);
        assert_eq!(st.target.unwrap().address, Some(FACTORY_ADDRESS));

        // A new card: an event, read with the next poll.
        bus.with_reader(0, |r| r.insert(FakeCard::ntag215()));
        run(&mut e, 1);
        let st = status(&e);
        assert_eq!(st.events.unwrap().entries.len(), 2);
        assert!(st.last_event_at.is_some());
        e.handle(Request::Ack(2));
        assert!(status(&e).events.unwrap().entries.is_empty());
    }

    #[test]
    fn scan_finds_readers_and_lets_the_user_choose() {
        let bus = FakeBus::with_readers(&[FACTORY_ADDRESS, 17]);
        bus.with_reader(1, |r| r.version = 0x0305);
        let mut e = engine(&bus);
        e.handle(Request::Connect(target(None)));
        run(&mut e, 250);
        // 17 has firmware 3.5 identity registers but is not 4.x: only the
        // reader at the factory address is usable, so it is taken.
        let st = status(&e);
        assert_eq!(st.found.len(), 2);
        assert_eq!(st.link, Link::Connected);
        assert_eq!(st.target.unwrap().address, Some(FACTORY_ADDRESS));

        let bus = FakeBus::with_readers(&[FACTORY_ADDRESS, 17]);
        let mut e = engine(&bus);
        e.handle(Request::Connect(target(None)));
        run(&mut e, 250);
        assert_eq!(status(&e).link, Link::Choose);
        e.handle(Request::Choose(17));
        let st = status(&e);
        assert_eq!(st.link, Link::Connected);
        assert_eq!(st.identity.unwrap().serial_text(), "11213141ABCE");
    }

    #[test]
    fn nobody_and_legacy_firmware() {
        let bus = FakeBus::with_readers(&[FACTORY_ADDRESS]);
        let mut e = engine(&bus);
        e.handle(Request::Connect(target(Some(9))));
        assert_eq!(status(&e).link, Link::NotFound);
        // It keeps asking: the reader may just be powered off.
        assert_eq!(e.phase, Phase::Waiting);

        bus.with_reader(0, |r| r.legacy = true);
        e.handle(Request::Connect(target(Some(FACTORY_ADDRESS))));
        assert!(matches!(status(&e).link, Link::Unsupported(t) if t.contains("3.x")));

        bus.state().denied = true;
        e.handle(Request::Connect(target(Some(FACTORY_ADDRESS))));
        assert!(matches!(status(&e).link, Link::AccessDenied(_)));
    }

    #[test]
    fn writes_card_settings_and_saves() {
        let bus = FakeBus::with_readers(&[FACTORY_ADDRESS]);
        let mut e = connected(&bus);
        let mut s = status(&e).settings.unwrap();
        s.scan = false;
        s.gain_db = 48;
        s.key_b = true;
        s.key = [1, 2, 3, 4, 5, 6];
        s.autoread_count = 2;
        e.handle(Request::WriteSettings(s));
        let st = status(&e);
        assert_eq!(st.settings, Some(s));
        assert!(st.notices.last().unwrap().ok);
        bus.with_reader(0, |r| {
            assert_eq!(r.saved, r.settings);
            assert_eq!(r.saved[2], 48);
            assert_eq!(r.saved[8], 0x0102);
        });

        // EEPROM failure: exception 04.
        bus.with_reader(0, |r| r.save_fails = true);
        s.gain_db = 18;
        e.handle(Request::WriteSettings(s));
        assert!(!status(&e).notices.last().unwrap().ok);
    }

    #[test]
    fn bus_change_is_applied_checked_then_saved() {
        let bus = FakeBus::with_readers(&[FACTORY_ADDRESS]);
        let mut e = connected(&bus);
        let mut s = status(&e).settings.unwrap();
        s.bus.address = 33;
        s.bus.baud = 57_600;
        s.bus.parity = Parity::Even;
        s.bus.stop = StopBits::Two;
        e.handle(Request::WriteSettings(s));
        let st = status(&e);
        assert!(st.notices.last().unwrap().ok, "{:?}", st.notices);
        let t = st.target.unwrap();
        assert_eq!(t.address, Some(33));
        assert_eq!(t.line.baud, 57_600);
        assert_eq!(t.line.parity, Parity::Even);
        bus.with_reader(0, |r| {
            assert_eq!(r.address, 33);
            assert_eq!(r.saved[21..], [33, 576, 2, 20, 2]);
        });
        // Still talking.
        run(&mut e, 2);
        assert_eq!(status(&e).link, Link::Connected);
    }

    #[test]
    fn bus_change_without_answer_is_not_saved() {
        let bus = FakeBus::with_readers(&[FACTORY_ADDRESS]);
        let mut e = connected(&bus);
        let mut s = status(&e).settings.unwrap();
        s.bus.baud = 9600;
        // It switches, then nothing answers (a line that cannot do 9600).
        bus.with_reader(0, |r| r.mute_after_apply = true);
        let before = bus.with_reader(0, |r| r.saved);
        e.handle(Request::WriteSettings(s));
        let st = status(&e);
        let last = st.notices.last().unwrap();
        assert!(!last.ok && last.text.contains("9600"), "{last:?}");
        assert!(matches!(st.link, Link::Lost(_)));
        assert_eq!(bus.with_reader(0, |r| r.saved), before);
        // The engine keeps asking on the saved settings.
        assert_eq!(st.target.unwrap().line.baud, 115_200);
        assert_eq!(e.client.as_ref().unwrap().line.baud, 115_200);
    }

    #[test]
    fn ignored_apply_keeps_the_old_bus_settings() {
        let bus = FakeBus::with_readers(&[FACTORY_ADDRESS]);
        let mut e = connected(&bus);
        let mut s = status(&e).settings.unwrap();
        s.bus.address = 50;
        bus.with_reader(0, |r| r.ignore_apply = true);
        let before = bus.with_reader(0, |r| r.saved);
        e.handle(Request::WriteSettings(s));
        let st = status(&e);
        assert!(!st.notices.last().unwrap().ok);
        assert_eq!(st.link, Link::Connected);
        assert_eq!(st.settings.unwrap().bus.address, FACTORY_ADDRESS);
        assert_eq!(bus.with_reader(0, |r| r.saved), before);
    }

    #[test]
    fn command_block_with_sequence_numbers() {
        let bus = FakeBus::with_readers(&[FACTORY_ADDRESS]);
        bus.with_reader(0, |r| r.insert(FakeCard::classic()));
        let mut e = connected(&bus);
        e.handle(Request::Command(CmdRequest::new(0x01)));
        let st = status(&e);
        let r = st.commands.last().unwrap().result.clone().unwrap();
        assert_eq!(r.status, CmdStatus::Done);
        assert_eq!(r.seq, 1);
        assert_eq!(r.data, b"ODRFID (ModBus) 4.0F Nov 23 2026");

        // Card operations need autoscan off.
        let read4 = CmdRequest {
            p0: 4,
            ..CmdRequest::new(0x30)
        };
        e.handle(Request::Command(read4.clone()));
        let r = status(&e).commands.last().unwrap().result.clone().unwrap();
        assert_eq!(r.status, CmdStatus::Rejected);
        e.handle(Request::SetScan(false));
        assert!(!status(&e).scan_on());
        e.handle(Request::Command(CmdRequest::new(0x21)));
        e.handle(Request::Command(read4));
        let r = status(&e).commands.last().unwrap().result.clone().unwrap();
        assert_eq!(r.status, CmdStatus::Done);
        assert_eq!(&r.data[..5], b"Hello");
        assert_eq!(r.seq, 4);

        // An enumeration answers "running" first.
        e.handle(Request::Command(CmdRequest::new(0x20)));
        let r = status(&e).commands.last().unwrap().result.clone().unwrap();
        assert_eq!(r.status, CmdStatus::Done);
        assert_eq!(r.data, [4, 0xA1, 0xB2, 0xC3, 0xD4, 0x08]);
    }

    #[test]
    fn sequence_continues_after_the_readers_last() {
        let bus = FakeBus::with_readers(&[FACTORY_ADDRESS]);
        bus.with_reader(0, |r| r.cmd_response = vec![41, 0, 0, 0, 0]);
        let mut e = connected(&bus);
        e.handle(Request::Command(CmdRequest::new(0x03)));
        let r = status(&e).commands.last().unwrap().result.clone().unwrap();
        assert_eq!(r.seq, 42);
        assert_eq!(r.data, [0xC9, 0xC7, 0xAE, 0x2A]);
    }

    #[test]
    fn restart_loss_and_return() {
        let bus = FakeBus::with_readers(&[FACTORY_ADDRESS]);
        let mut e = connected(&bus);
        // The self-test of the power-on start.
        let test = status(&e).self_test.expect("self-test");
        assert!(test.passed());
        assert_eq!(test.reset_text(), "power on or brownout");

        // A restart nobody asked for is noticed by the boot counter and
        // told with its cause.
        bus.with_reader(0, |r| {
            r.boot_count += 1;
            r.reset_cause = 0x24;
        });
        run(&mut e, 1);
        let n = last_notice(&e);
        assert!(!n.ok, "{n:?}");
        assert!(n.text.ends_with("unexpectedly: watchdog"), "{n:?}");
        assert_eq!(status(&e).self_test.unwrap().reset_text(), "watchdog");

        // Silent reader: lost after three polls, back when it answers.
        bus.with_reader(0, |r| r.off = true);
        run(&mut e, 4);
        assert!(matches!(status(&e).link, Link::Lost(_)));
        bus.with_reader(0, |r| r.off = false);
        let mut t = Instant::now() + RETRY * 10;
        for _ in 0..3 {
            t += RETRY;
            e.tick(t);
        }
        assert_eq!(status(&e).link, Link::Connected);
        assert!(status(&e).notices.iter().any(|n| n.ok));

        // The adapter is unplugged.
        bus.state().ports.clear();
        e.tick(t + POLL);
        assert!(matches!(status(&e).link, Link::PortError(_)));
    }

    /// Runs against a real reader or the firmware emulator of
    /// `opendev/odrfid-rs485` (`tests/host/emulator.py`) at the factory
    /// address: `ODRFID_PORT=/dev/pts/N cargo test -- --ignored real_reader`.
    #[test]
    #[ignore]
    fn real_reader_end_to_end() {
        let port = std::env::var("ODRFID_PORT").expect("ODRFID_PORT");
        let mut e = Engine::new(
            Box::new(crate::serial::SystemBus),
            Arc::new(Mutex::new(Status::default())),
            Traffic::default(),
            Repaint::default(),
        );
        let t = |address| Target {
            port: port.clone(),
            line: Line::default(),
            address,
            frame_pause_ms: 0,
        };
        e.handle(Request::Connect(t(Some(FACTORY_ADDRESS))));
        let st = status(&e);
        assert_eq!(st.link, Link::Connected, "{st:?}");
        let id = st.identity.unwrap();
        assert!(id.supported());
        eprintln!("S/N {} firmware {:?}", id.serial_text(), st.build);
        let original = st.settings.unwrap();

        // Command block: identity, then card operations with autoscan off.
        e.handle(Request::Command(CmdRequest::new(0x01)));
        let r = status(&e).commands.last().unwrap().result.clone().unwrap();
        assert!(r.data.starts_with(b"ODRFID (ModBus) 4."), "{r:?}");
        e.handle(Request::SetScan(false));
        assert!(!status(&e).scan_on());
        e.handle(Request::Command(CmdRequest::new(0x21)));
        let r = status(&e).commands.last().unwrap().result.clone().unwrap();
        eprintln!("select: {r:?}");
        if r.status == CmdStatus::Done {
            e.handle(Request::Command(CmdRequest {
                p0: 1,
                ..CmdRequest::new(0x30)
            }));
            let r = status(&e).commands.last().unwrap().result.clone().unwrap();
            eprintln!("block 1: {r:?}");
            assert_eq!(r.status, CmdStatus::Done);
            assert_eq!(r.data.len(), 16);
        }
        e.handle(Request::Command(CmdRequest::new(0x20)));
        let r = status(&e).commands.last().unwrap().result.clone().unwrap();
        eprintln!("enumerate: {r:?}");
        assert_eq!(r.status, CmdStatus::Done);

        // Card settings, written and saved while autoscan is off until the
        // restart: RAM keeps it off, the EEPROM gets the saved value (the
        // restart below shows it).
        assert_eq!(status(&e).scan_saved, original.scan.then_some(true));
        let held = |s: Settings| Settings { scan: false, ..s };
        let mut s = original;
        s.gain_db = 48;
        s.interval_ms = 300;
        s.key_b = true;
        s.autoread_first = 4;
        s.autoread_count = 2;
        e.handle(Request::WriteSettings(s));
        let st = status(&e);
        assert_eq!(st.settings, Some(held(s)), "{:?}", st.notices);
        assert!(st.notices.last().unwrap().ok, "{:?}", st.notices);

        // The bus: a new address, applied, checked, saved; and back.
        let mut moved = s;
        moved.bus.address = 33;
        e.handle(Request::WriteSettings(moved));
        let st = status(&e);
        assert!(st.notices.last().unwrap().ok, "{:?}", st.notices);
        assert_eq!(st.target.as_ref().unwrap().address, Some(33));
        assert_eq!(st.settings.unwrap().bus.address, 33);
        e.handle(Request::WriteSettings(original));
        let st = status(&e);
        assert!(st.notices.last().unwrap().ok, "{:?}", st.notices);
        assert_eq!(st.target.as_ref().unwrap().address, Some(FACTORY_ADDRESS));
        assert_eq!(st.settings, Some(held(original)));

        // Events, polling and a restart noticed by the boot counter; the
        // saved settings come back, autoscan on.
        let before = status(&e).live.unwrap().boot_count;
        e.handle(Request::Restart);
        let mut now = Instant::now();
        for _ in 0..20 {
            thread::sleep(Duration::from_millis(200));
            now += Duration::from_millis(500);
            e.tick(now);
        }
        let st = status(&e);
        assert_eq!(st.link, Link::Connected, "{st:?}");
        assert_ne!(st.live.as_ref().unwrap().boot_count, before);
        assert!(st.events.is_some());
        assert_eq!(st.settings, Some(original));
        assert_eq!(st.scan_saved, None);
        // Asked for: no alert.
        let unexpected = |st: &Status| st.notices.iter().any(|n| n.text.contains("unexpectedly"));
        assert!(!unexpected(&st), "{:?}", st.notices);
        assert!(st.notices.last().unwrap().ok, "{:?}", st.notices);
        assert_eq!(st.self_test.unwrap().reset_text(), "software restart");

        // A scan finds it.
        e.handle(Request::Connect(t(None)));
        for _ in 0..300 {
            e.tick(Instant::now());
            if !matches!(status(&e).link, Link::Scanning { .. }) {
                break;
            }
        }
        let st = status(&e);
        assert_eq!(st.link, Link::Connected, "{:?}", st.found);
        assert_eq!(st.target.unwrap().address, Some(FACTORY_ADDRESS));
    }

    #[test]
    fn bootloader_and_factory_reset() {
        let bus = FakeBus::with_readers(&[FACTORY_ADDRESS]);
        let mut e = connected(&bus);
        let mut s = status(&e).settings.unwrap();
        s.bus.address = 40;
        e.handle(Request::WriteSettings(s));
        assert_eq!(status(&e).target.unwrap().address, Some(40));
        e.handle(Request::FactoryReset);
        assert_eq!(status(&e).target.unwrap().address, Some(FACTORY_ADDRESS));
        run(&mut e, 2);
        assert_eq!(status(&e).link, Link::Connected);
        let st = status(&e);
        assert!(
            !st.notices.iter().any(|n| n.text.contains("unexpectedly")),
            "{:?}",
            st.notices
        );
        assert_eq!(bus.with_reader(0, |r| r.address), FACTORY_ADDRESS);

        e.handle(Request::Bootloader);
        assert_eq!(status(&e).link, Link::Bootloader);
        assert!(bus.with_reader(0, |r| r.bootloader));
        assert!(e.client.is_none());
    }

    fn last_notice(e: &Engine) -> Notice {
        status(e).notices.last().cloned().expect("a notice")
    }

    fn writes_to(bus: &FakeBus, register: u16) -> usize {
        bus.with_reader(0, |r| {
            r.requests
                .iter()
                .filter(|&&q| q == (crate::modbus::FC_WRITE_SINGLE, register))
                .count()
        })
    }

    #[test]
    fn restart_without_an_answer_is_checked() {
        // The answer is lost: sent once, the start counter tells it ran.
        let bus = FakeBus::with_readers(&[FACTORY_ADDRESS]);
        let mut e = connected(&bus);
        let boots = bus.with_reader(0, |r| r.boot_count);
        bus.with_reader(0, |r| r.lose_answers = 1);
        e.handle(Request::Restart);
        assert!(last_notice(&e).ok, "{:?}", last_notice(&e));
        assert_eq!(bus.with_reader(0, |r| r.boot_count), boots + 1);
        assert_eq!(writes_to(&bus, holding::REBOOT), 1);
        // The polls see the new start count: the asked-for restart, no alert.
        run(&mut e, 20);
        let st = status(&e);
        assert_eq!(st.live.as_ref().unwrap().boot_count, boots + 1);
        assert!(st.notices.iter().all(|n| n.ok), "{:?}", st.notices);
        assert_eq!(last_notice(&e).text, "The reader restarted");

        // The request is lost: it answers with the same counter.
        let bus = FakeBus::with_readers(&[FACTORY_ADDRESS]);
        let mut e = connected(&bus);
        bus.with_reader(0, |r| r.lose_requests = 1);
        e.handle(Request::Restart);
        let n = last_notice(&e);
        assert!(
            !n.ok && n.text.starts_with("The reader did not restart"),
            "{n:?}"
        );

        // Powered off: no success.
        let bus = FakeBus::with_readers(&[FACTORY_ADDRESS]);
        let mut e = connected(&bus);
        bus.with_reader(0, |r| r.off = true);
        e.handle(Request::Restart);
        assert!(!last_notice(&e).ok);
    }

    #[test]
    fn factory_reset_without_an_answer_is_checked() {
        let moved = || {
            let bus = FakeBus::with_readers(&[FACTORY_ADDRESS]);
            let mut e = connected(&bus);
            let mut s = status(&e).settings.unwrap();
            s.bus.address = 40;
            e.handle(Request::WriteSettings(s));
            assert_eq!(status(&e).target.unwrap().address, Some(40));
            (bus, e)
        };

        // Powered off: the tool stays on the reader's settings.
        let (bus, mut e) = moved();
        bus.with_reader(0, |r| r.off = true);
        e.handle(Request::FactoryReset);
        assert!(!last_notice(&e).ok);
        assert_eq!(status(&e).target.unwrap().address, Some(40));
        assert_eq!(e.client.as_ref().unwrap().address, 40);
        assert_eq!(bus.with_reader(0, |r| r.address), 40);

        // The request is lost: it still answers at 40.
        let (bus, mut e) = moved();
        bus.with_reader(0, |r| r.lose_requests = 1);
        e.handle(Request::FactoryReset);
        let n = last_notice(&e);
        assert!(
            !n.ok && n.text.starts_with("The factory reset failed"),
            "{n:?}"
        );
        assert_eq!(status(&e).target.unwrap().address, Some(40));
        assert_eq!(e.client.as_ref().unwrap().address, 40);

        // The answer is lost: found at the factory settings.
        let (bus, mut e) = moved();
        bus.with_reader(0, |r| r.lose_answers = 1);
        e.handle(Request::FactoryReset);
        assert!(last_notice(&e).ok, "{:?}", last_notice(&e));
        assert_eq!(status(&e).target.unwrap().address, Some(FACTORY_ADDRESS));
        assert_eq!(bus.with_reader(0, |r| r.address), FACTORY_ADDRESS);
        assert_eq!(writes_to(&bus, holding::FACTORY_RESET), 1);
        run(&mut e, 5);
        assert_eq!(status(&e).link, Link::Connected);
    }

    #[test]
    fn bootloader_without_an_answer_is_checked() {
        // The answer is lost: the bootloader answers INFO.
        let bus = FakeBus::with_readers(&[FACTORY_ADDRESS]);
        let mut e = connected(&bus);
        bus.with_reader(0, |r| r.lose_answers = 1);
        e.handle(Request::Bootloader);
        assert_eq!(status(&e).link, Link::Bootloader);
        assert_eq!(writes_to(&bus, holding::BOOTLOADER), 1);

        // The request is lost: the firmware refuses INFO.
        let bus = FakeBus::with_readers(&[FACTORY_ADDRESS]);
        let mut e = connected(&bus);
        bus.with_reader(0, |r| r.lose_requests = 1);
        e.handle(Request::Bootloader);
        assert_eq!(status(&e).link, Link::Connected);
        assert!(!last_notice(&e).ok);
        assert!(!bus.with_reader(0, |r| r.bootloader));

        // Powered off: not reported as the bootloader.
        let bus = FakeBus::with_readers(&[FACTORY_ADDRESS]);
        let mut e = connected(&bus);
        bus.with_reader(0, |r| r.off = true);
        e.handle(Request::Bootloader);
        assert_ne!(status(&e).link, Link::Bootloader);
        assert!(!last_notice(&e).ok);
    }

    #[test]
    fn a_speed_the_adapter_refuses_is_not_applied() {
        let bus = FakeBus::with_readers(&[FACTORY_ADDRESS]);
        bus.state().adapter_max_baud = Some(1_000_000);
        let mut e = connected(&bus);
        let mut s = status(&e).settings.unwrap();
        s.bus.baud = 2_000_000;
        s.gain_db = 48;
        let before = bus.with_reader(0, |r| (r.settings, r.saved));
        e.handle(Request::WriteSettings(s));
        let n = last_notice(&e);
        assert!(!n.ok && n.text.contains("2000000"), "{n:?}");
        // Nothing written, the port and the reader where they were.
        assert_eq!(bus.with_reader(0, |r| (r.settings, r.saved)), before);
        assert_eq!(e.client.as_ref().unwrap().line.baud, 115_200);
        run(&mut e, 2);
        assert_eq!(status(&e).link, Link::Connected);
    }

    #[test]
    fn autoscan_off_until_restart_survives_a_save() {
        let bus = FakeBus::with_readers(&[FACTORY_ADDRESS]);
        let mut e = connected(&bus);
        e.handle(Request::SetScan(false));
        let st = status(&e);
        assert!(!st.scan_on());
        assert_eq!(st.scan_saved, Some(true));

        // The window offers the saved autoscan; a key is changed.
        let mut s = st.settings.unwrap();
        s.scan = true;
        s.key = [1, 2, 3, 4, 5, 6];
        e.handle(Request::WriteSettings(s));
        let st = status(&e);
        assert!(st.notices.last().unwrap().ok, "{:?}", st.notices);
        assert!(!st.scan_on());
        assert_eq!(st.scan_saved, Some(true));
        bus.with_reader(0, |r| {
            assert_eq!((r.saved[0], r.saved[8]), (1, 0x0102));
            assert_eq!(r.settings[0], 0);
        });

        // A restart brings the saved value back.
        e.handle(Request::Restart);
        run(&mut e, 5);
        let st = status(&e);
        assert!(st.scan_on());
        assert_eq!(st.scan_saved, None);

        // So does switching back to the saved value.
        e.handle(Request::SetScan(false));
        e.handle(Request::SetScan(true));
        let st = status(&e);
        assert!(st.scan_on());
        assert_eq!(st.scan_saved, None);

        // Turned off on the Scanning page: saved off, nothing held.
        e.handle(Request::SetScan(false));
        let mut s = status(&e).settings.unwrap();
        s.scan = false;
        e.handle(Request::WriteSettings(s));
        assert_eq!(status(&e).scan_saved, None);
        assert_eq!(bus.with_reader(0, |r| r.saved[0]), 0);
    }

    #[test]
    fn the_frame_pause_goes_with_the_target() {
        let bus = FakeBus::with_readers(&[FACTORY_ADDRESS]);
        let mut e = connected(&bus);
        assert_eq!(status(&e).target.unwrap().frame_pause_ms, 2);
        let mut s = status(&e).settings.unwrap();
        s.bus.frame_pause_ms = 80;
        e.handle(Request::WriteSettings(s));
        let target = status(&e).target.unwrap();
        assert_eq!(target.frame_pause_ms, 80);

        // The next connection waits for it from the first request on.
        let mut next = engine(&bus);
        next.connect(Target {
            address: None,
            ..target
        });
        let client = next.client.as_ref().unwrap();
        assert_eq!(client.frame_pause, Duration::from_millis(80));
    }

    #[test]
    fn a_port_failure_keeps_its_own_error() {
        let bus = FakeBus::with_readers(&[FACTORY_ADDRESS]);
        let mut e = connected(&bus);
        bus.state().ports.clear();
        e.handle(Request::SetScan(false));
        let st = status(&e);
        assert_eq!(st.link, Link::PortError("Input/output error".into()));
        let n = st.notices.last().unwrap();
        assert!(n.text.ends_with("Input/output error"), "{n:?}");
    }
}
