//! The window: a header strip with the reader's state (the wdtmon-rs
//! strip), a list of pages on the left and the page on the right. The
//! settings pages share one draft and one save bar.

use std::time::{Duration, Instant};

use eframe::egui::{
    self, Align, Align2, Color32, CornerRadius, FontFamily, FontId, Layout, Pos2, Rect, RichText,
    Sense, Stroke, StrokeKind, Ui, UiBuilder, Vec2, ViewportCommand,
};

use crate::config::{self, Config, Language, Paths, Theme};
use crate::engine::{
    Busy, CmdOutcome, EngineHandle, Link, Notice, Repaint, Request, Status, Target,
};
use crate::glyph;
use crate::i18n::{self, tr, trf};
use crate::modbus::{BAUD_MAX, BAUD_MIN, BAUD_RATES, Parity, StopBits};
use crate::reader::{
    self, AUTOREAD_MAX, CMD_DATA_MAX, CmdRequest, CmdStatus, DataKind, FRAME_PAUSE_MAX, GAINS,
    INTERVAL_MIN, Indicate, OPS, Param, Settings, card_summary, card_type_name, cme_text,
    describe_answer, find_op, hex_compact, parse_hex,
};
use crate::style::{self, Kind, Tone, heading, pal, row, row_tip, strong, toggle, tool};
use crate::sysint;
use crate::traffic::{self, Dir, Entry, Traffic};

pub const APP_VERSION: &str = env!("CARGO_PKG_VERSION");
const STRIP_HEIGHT: f32 = 104.0;
const FLASH_TIME: Duration = Duration::from_secs(5);
const NAV_WIDTH: f32 = 184.0;
const SAVE_BAR_HEIGHT: f32 = 52.0;
pub const WINDOW_SIZE: Vec2 = Vec2::new(780.0, 640.0);
pub const MIN_SIZE: Vec2 = Vec2::new(700.0, 500.0);

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
enum Page {
    Connection,
    Scanning,
    Keys,
    Bus,
    Cards,
    Commands,
    Log,
    Service,
    App,
}

impl Page {
    /// The navigation: groups of pages under an optional heading.
    const GROUPS: [(&'static str, &'static [Page]); 4] = [
        ("", &[Page::Connection]),
        ("Settings", &[Page::Scanning, Page::Keys, Page::Bus]),
        ("Cards", &[Page::Cards, Page::Commands]),
        ("Tools", &[Page::Log, Page::Service, Page::App]),
    ];

    fn glyph(self) -> &'static str {
        match self {
            Page::Connection => glyph::PLUGS_CONNECTED,
            Page::Scanning => glyph::BROADCAST,
            Page::Keys => glyph::KEY,
            Page::Bus => glyph::ARROWS_LEFT_RIGHT,
            Page::Cards => glyph::CONTACTLESS_PAYMENT,
            Page::Commands => glyph::TERMINAL_WINDOW,
            Page::Log => glyph::LIST_BULLETS,
            Page::Service => glyph::CIRCUITRY,
            Page::App => glyph::PALETTE,
        }
    }

    fn title(self) -> &'static str {
        match self {
            Page::Connection => tr("Connection"),
            Page::Scanning => tr("Scanning"),
            Page::Keys => tr("Keys"),
            Page::Bus => tr("RS-485 bus"),
            Page::Cards => tr("Card and events"),
            Page::Commands => tr("Command block"),
            Page::Log => tr("Frame log"),
            Page::Service => tr("Maintenance"),
            Page::App => tr("Application"),
        }
    }

    /// Pages that edit the reader's settings.
    fn settings(self) -> bool {
        matches!(self, Page::Scanning | Page::Keys | Page::Bus)
    }

    /// Pages that show nothing without a reader.
    fn needs_reader(self) -> bool {
        !matches!(self, Page::Connection | Page::Log | Page::App)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Confirm {
    Restart,
    FactoryReset,
    Bootloader,
}

/// Edited reader settings next to what the reader holds.
struct Draft {
    serial: u64,
    base: Settings,
    edit: Settings,
    key: String,
    ul_password: String,
    aes_key: String,
}

impl Draft {
    fn new(serial: u64, base: Settings) -> Self {
        Self {
            serial,
            base,
            edit: base,
            key: hex_compact(&base.key),
            ul_password: hex_compact(&base.ul_password),
            aes_key: hex_compact(&base.aes_key),
        }
    }

    fn keys_valid(&self) -> bool {
        self.key.len() == 12 && self.ul_password.len() == 8 && self.aes_key.len() == 32
    }

    fn dirty(&self) -> bool {
        self.edit != self.base || !self.keys_valid()
    }

    /// Unsaved changes on one settings page.
    fn page_dirty(&self, page: Page) -> bool {
        let (e, b) = (&self.edit, &self.base);
        match page {
            Page::Scanning => {
                (e.scan, e.interval_ms, e.gain_db, e.auto_led, e.auto_beep)
                    != (b.scan, b.interval_ms, b.gain_db, b.auto_led, b.auto_beep)
                    || (e.autoread_first, e.autoread_count) != (b.autoread_first, b.autoread_count)
            }
            Page::Keys => {
                (e.key_b, e.key, e.ul_password, e.aes_key)
                    != (b.key_b, b.key, b.ul_password, b.aes_key)
                    || !self.keys_valid()
            }
            Page::Bus => e.bus != b.bus,
            _ => false,
        }
    }

    /// Copies the typed keys into `edit` where they are complete.
    fn sync_keys(&mut self) {
        let fill = |text: &str, out: &mut [u8]| {
            if let Some(bytes) = parse_hex(text).filter(|b| b.len() == out.len()) {
                out.copy_from_slice(&bytes);
            }
        };
        fill(&self.key, &mut self.edit.key);
        fill(&self.ul_password, &mut self.edit.ul_password);
        fill(&self.aes_key, &mut self.edit.aes_key);
    }

    fn reset_to(&mut self, s: Settings) {
        *self = Self::new(self.serial, self.base);
        self.edit = s;
        self.key = hex_compact(&s.key);
        self.ul_password = hex_compact(&s.ul_password);
        self.aes_key = hex_compact(&s.aes_key);
    }
}

/// The command block form.
struct CmdForm {
    op: u8,
    p0: u32,
    p1: u32,
    data: String,
}

impl Default for CmdForm {
    fn default() -> Self {
        Self {
            op: 0x01,
            p0: 0,
            p1: 0,
            data: String::new(),
        }
    }
}

pub struct App {
    config: Config,
    applied: Config,
    paths: Paths,
    engine: EngineHandle,
    traffic: Traffic,
    repaint: Repaint,
    page: Page,
    confirm: Option<Confirm>,
    flash: Option<(Instant, Notice)>,
    draft: Option<Draft>,
    /// The connection the form was last updated from.
    synced: Option<Target>,
    cmd: CmdForm,
    beeps: u16,
    /// `--dfu`: enter the bootloader once connected, then quit.
    dfu: DfuStep,
    theme: Option<Theme>,
    /// Debug builds: `ODRFID_DEMO_SCRIPT=commands|edit` puts the window into
    /// a state for documentation screenshots.
    #[cfg(debug_assertions)]
    demo_script: Option<String>,
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum DfuStep {
    Off,
    Waiting,
    Sent,
}

impl App {
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        cc: &eframe::CreationContext<'_>,
        config: Config,
        paths: Paths,
        engine: EngineHandle,
        traffic: Traffic,
        repaint: Repaint,
        connect: bool,
        dfu: bool,
    ) -> Self {
        style::install(&cc.egui_ctx);
        repaint.set(Some(cc.egui_ctx.clone()));
        engine.send(Request::RefreshPorts);
        let mut app = Self {
            applied: config.clone(),
            config,
            paths,
            engine,
            traffic,
            repaint,
            page: Page::Connection,
            confirm: None,
            flash: None,
            draft: None,
            synced: None,
            cmd: CmdForm::default(),
            beeps: 2,
            dfu: if dfu { DfuStep::Waiting } else { DfuStep::Off },
            theme: None,
            #[cfg(debug_assertions)]
            demo_script: std::env::var("ODRFID_DEMO_SCRIPT").ok(),
        };
        if connect && app.config.port.is_some() {
            app.connect();
        }
        #[cfg(debug_assertions)]
        {
            app.page = match std::env::var("ODRFID_PAGE").as_deref() {
                Ok("scanning") => Page::Scanning,
                Ok("keys") => Page::Keys,
                Ok("bus") => Page::Bus,
                Ok("cards") => Page::Cards,
                Ok("commands") => Page::Commands,
                Ok("log") => Page::Log,
                Ok("service") => Page::Service,
                Ok("app") => Page::App,
                _ => Page::Connection,
            };
            if let Ok("reset") = std::env::var("ODRFID_CONFIRM").as_deref() {
                // Asked once the reader is connected: the question needs one.
                app.demo_script = Some("reset".into());
            }
        }
        app
    }

    fn target(&self) -> Option<Target> {
        target_of(&self.config)
    }

    fn connect(&mut self) {
        if let Some(t) = self.target() {
            self.synced = None;
            self.draft = None;
            self.engine.send(Request::Connect(t));
        }
    }

    fn flash(&mut self, ok: bool, text: impl Into<String>) {
        self.flash = Some((
            Instant::now(),
            Notice {
                ok,
                text: text.into(),
            },
        ));
    }

    /// Persists changed settings.
    fn commit_config(&mut self) {
        self.config.sanitize();
        if self.config == self.applied {
            return;
        }
        if let Err(e) = config::save(&self.paths.config_file, &self.config) {
            log::warn!("{}: {e}", self.paths.config_file.display());
        }
        if self.config.language != self.applied.language {
            i18n::set_language(self.config.language);
        }
        self.applied = self.config.clone();
    }

    /// The form follows the connection the engine made: an address the scan
    /// found, a bus change, a factory reset.
    fn follow_target(&mut self, st: &Status) {
        if !st.connected() {
            return;
        }
        let Some(t) = &st.target else {
            return;
        };
        if self.synced.as_ref() == Some(t) {
            return;
        }
        self.synced = Some(t.clone());
        follow(&mut self.config, t);
    }

    /// Keeps the settings draft next to what the reader holds. Edits survive
    /// a re-read and a short loss of the link to the same reader.
    fn sync_draft(&mut self, st: &Status) {
        let (Some(mut device), Some(serial)) = (st.settings, st.identity.map(|i| i.serial)) else {
            self.draft = None;
            return;
        };
        // Autoscan switched until restart is not a change to save: the
        // draft holds the saved value, and saving keeps it.
        device.scan = st.scan_saved.unwrap_or(device.scan);
        self.draft = Some(match self.draft.take() {
            Some(d) if d.serial == serial && d.base == device => d,
            Some(d) if d.serial == serial && d.dirty() => Draft { base: device, ..d },
            _ => Draft::new(serial, device),
        });
    }

    #[cfg(debug_assertions)]
    fn run_demo_script(&mut self, st: &Status) {
        if !st.connected() || st.live.is_none() {
            return;
        }
        match self.demo_script.take().as_deref() {
            Some("commands") => {
                self.engine.send(Request::SetScan(false));
                self.engine.send(Request::Command(CmdRequest {
                    op: 0x01,
                    p0: 0,
                    p1: 0,
                    data: Vec::new(),
                }));
                self.engine.send(Request::Command(CmdRequest {
                    op: 0x21,
                    p0: 0,
                    p1: 0,
                    data: Vec::new(),
                }));
                self.engine.send(Request::Command(CmdRequest {
                    op: 0x30,
                    p0: 4,
                    p1: 0,
                    data: Vec::new(),
                }));
                self.cmd.op = 0x30;
                self.cmd.p0 = 4;
            }
            Some("reset") => self.confirm = Some(Confirm::FactoryReset),
            Some("edit") => match &mut self.draft {
                Some(d) => {
                    d.edit.interval_ms = 150;
                    d.edit.bus.address = 10;
                    d.edit.bus.baud = 57_600;
                }
                None => self.demo_script = Some("edit".into()),
            },
            _ => {}
        }
    }

    fn remember_window(&mut self, ctx: &egui::Context) {
        ctx.input(|i| {
            let v = i.viewport();
            if let Some(r) = v.outer_rect {
                self.config.position = Some([r.min.x, r.min.y]);
            }
            if let Some(r) = v.inner_rect {
                self.config.size = Some([r.width(), r.height()]);
            }
        });
    }
}

/// The connection the form describes.
fn target_of(config: &Config) -> Option<Target> {
    Some(Target {
        port: config.port.clone()?,
        line: config.line,
        address: (!config.scan_all).then_some(config.address),
        frame_pause_ms: config.frame_pause_ms,
    })
}

/// Takes the engine's connection into the form. An address the scan found
/// ends the scan: the next connection, at start too, goes straight there.
fn follow(config: &mut Config, t: &Target) {
    config.port = Some(t.port.clone());
    config.line = t.line;
    config.frame_pause_ms = t.frame_pause_ms;
    if let Some(a) = t.address {
        config.address = a;
        config.scan_all = false;
    }
}

impl eframe::App for App {
    fn logic(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        if ctx.input_mut(|i| i.consume_key(egui::Modifiers::COMMAND, egui::Key::Q)) {
            ctx.send_viewport_cmd(ViewportCommand::Close);
        }
        if ctx.input_mut(|i| i.consume_key(egui::Modifiers::NONE, egui::Key::Escape)) {
            self.confirm = None;
        }
        if ctx.input(|i| i.viewport().close_requested()) {
            self.remember_window(ctx);
            self.commit_config();
        }
        if self.theme != Some(self.config.theme) {
            self.theme = Some(self.config.theme);
            ctx.set_theme(match self.config.theme {
                Theme::System => egui::ThemePreference::System,
                Theme::Light => egui::ThemePreference::Light,
                Theme::Dark => egui::ThemePreference::Dark,
            });
        }
        // Typed values apply when the edit ends.
        if !(ctx.egui_wants_keyboard_input() || ctx.input(|i| i.pointer.any_down())) {
            self.commit_config();
        }
        if let Some(notice) = self.engine.take_notices().pop() {
            self.flash = Some((Instant::now(), notice));
        }
        if self
            .flash
            .as_ref()
            .is_some_and(|(t, _)| t.elapsed() >= FLASH_TIME)
        {
            self.flash = None;
        }
        let link = self.engine.status().link.clone();
        match self.dfu {
            DfuStep::Waiting if link == Link::Connected => {
                self.engine.send(Request::Bootloader);
                self.dfu = DfuStep::Sent;
            }
            DfuStep::Sent if link == Link::Bootloader => {
                ctx.send_viewport_cmd(ViewportCommand::Close);
            }
            _ => {}
        }
        // Keeps "N s ago" and the flash timing current.
        ctx.request_repaint_after(Duration::from_secs(1));
    }

    fn ui(&mut self, ui: &mut Ui, _frame: &mut eframe::Frame) {
        #[cfg(debug_assertions)]
        debug_screenshot(ui.ctx());
        let st = self.engine.status().clone();
        self.follow_target(&st);
        self.sync_draft(&st);
        #[cfg(debug_assertions)]
        self.run_demo_script(&st);
        let p = pal(ui);
        let full = ui.max_rect();
        ui.painter().rect_filled(full, 0.0, p.base);

        let strip = Rect::from_min_size(full.min, Vec2::new(full.width(), STRIP_HEIGHT));
        ui.scope_builder(UiBuilder::new().max_rect(strip).id_salt("strip"), |ui| {
            self.strip(ui, &st);
        });

        let nav = Rect::from_min_max(
            Pos2::new(full.left(), strip.bottom()),
            Pos2::new(full.left() + NAV_WIDTH, full.bottom()),
        );
        ui.painter()
            .hline(full.x_range(), strip.bottom(), Stroke::new(1.0, p.line));
        ui.scope_builder(
            UiBuilder::new()
                .max_rect(nav.shrink2(Vec2::new(10.0, 10.0)))
                .id_salt("nav"),
            |ui| self.navigation(ui, &st),
        );

        let body = Rect::from_min_max(Pos2::new(nav.right(), strip.bottom()), full.max);
        ui.painter().rect_filled(body, 0.0, p.panel);
        ui.painter()
            .vline(body.left(), body.y_range(), Stroke::new(1.0, p.line));
        let save_bar = self.page.settings() && st.connected() && self.draft.is_some();
        let (content, bar) = if save_bar {
            let split = body.bottom() - SAVE_BAR_HEIGHT;
            (
                Rect::from_min_max(body.min, Pos2::new(body.right(), split)),
                Some(Rect::from_min_max(Pos2::new(body.left(), split), body.max)),
            )
        } else {
            (body, None)
        };
        let inner = content.shrink2(Vec2::new(1.0, 1.0));
        ui.scope_builder(UiBuilder::new().max_rect(inner).id_salt("body"), |ui| {
            if self.page == Page::Log {
                egui::Frame::new()
                    .inner_margin(egui::Margin::symmetric(18, 12))
                    .show(ui, |ui| self.log_page(ui));
                return;
            }
            egui::ScrollArea::vertical()
                .auto_shrink(false)
                .id_salt(self.page)
                .show(ui, |ui| {
                    egui::Frame::new()
                        .inner_margin(egui::Margin {
                            left: 20,
                            right: 20,
                            top: 10,
                            bottom: 14,
                        })
                        .show(ui, |ui| {
                            ui.spacing_mut().item_spacing.y = 2.0;
                            self.page_body(ui, &st);
                        });
                });
        });
        if let Some(bar) = bar {
            ui.painter().rect_filled(bar, 0.0, p.base);
            ui.painter()
                .hline(bar.x_range(), bar.top(), Stroke::new(1.0, p.line));
            ui.scope_builder(
                UiBuilder::new()
                    .max_rect(bar.shrink2(Vec2::new(20.0, 8.0)))
                    .layout(Layout::right_to_left(Align::Center))
                    .id_salt("save-bar"),
                |ui| self.save_bar(ui, &st),
            );
        }
        ui.painter()
            .rect_stroke(full, 0.0, Stroke::new(1.0, p.line), StrokeKind::Inside);

        if self.confirm.is_some() && !st.connected() {
            self.confirm = None;
        }
    }
}

impl Drop for App {
    fn drop(&mut self) {
        self.commit_config();
        self.repaint.set(None);
    }
}

/// Debug builds: `ODRFID_SCREENSHOT=file.png` saves the window after a few
/// seconds and quits.
#[cfg(debug_assertions)]
fn debug_screenshot(ctx: &egui::Context) {
    use std::sync::atomic::{AtomicBool, Ordering};
    static REQUESTED: AtomicBool = AtomicBool::new(false);
    let Some(path) = std::env::var_os("ODRFID_SCREENSHOT") else {
        return;
    };
    let delay: f64 = std::env::var("ODRFID_SCREENSHOT_DELAY")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(4.0);
    if ctx.input(|i| i.time) > delay && !REQUESTED.swap(true, Ordering::Relaxed) {
        ctx.request_screenshot(move |image| {
            let file = std::fs::File::create(&path).expect("screenshot file");
            let mut encoder = png::Encoder::new(file, image.size[0] as u32, image.size[1] as u32);
            encoder.set_color(png::ColorType::Rgba);
            encoder.set_depth(png::BitDepth::Eight);
            let mut writer = encoder.write_header().expect("png header");
            writer.write_image_data(image.as_raw()).expect("png data");
            drop(writer);
            std::process::exit(0);
        });
    }
    ctx.request_repaint_after(Duration::from_millis(200));
}

// ---- the strip -------------------------------------------------------------

struct Look {
    tone: Tone,
    glyph: &'static str,
    spin: bool,
    text: String,
}

fn look(st: &Status) -> Look {
    let (tone, glyph, spin, text) = match &st.link {
        Link::Idle => (
            Tone::Idle,
            glyph::PLUGS,
            false,
            tr("Not connected").to_string(),
        ),
        Link::Opening => (
            Tone::Idle,
            "",
            true,
            match &st.target {
                Some(t) => trf("Connecting to {}…", &[&t.port]),
                None => tr("Connecting…").to_string(),
            },
        ),
        Link::Scanning { at } => (
            Tone::Idle,
            "",
            true,
            trf(
                "Scanning addresses: {} of 247, found {}",
                &[at, &st.found.len()],
            ),
        ),
        Link::Choose => (
            Tone::Caution,
            glyph::QUESTION,
            false,
            tr("Several readers answer: choose one below").to_string(),
        ),
        Link::NotFound => (
            Tone::Alert,
            glyph::MAGNIFYING_GLASS,
            false,
            match st
                .target
                .as_ref()
                .and_then(|t| t.address.map(|a| (a, t.line)))
            {
                Some((a, line)) => {
                    trf("No reader answers at address {} ({})", &[&a, &line.label()])
                }
                None => tr("No reader answered the scan").to_string(),
            },
        ),
        Link::Connected => match st.live.as_ref() {
            Some(live) if live.rfid_fault => (
                Tone::Alert,
                glyph::WARNING,
                false,
                tr("The RFID chip did not start").to_string(),
            ),
            Some(live) => match (&live.card, live.card_present || !st.scan_on()) {
                (Some(card), true) => (
                    Tone::Good,
                    glyph::CONTACTLESS_PAYMENT,
                    false,
                    card_summary(card),
                ),
                _ => (
                    Tone::Brand,
                    glyph::BROADCAST,
                    false,
                    if st.scan_on() {
                        tr("No card in the field").to_string()
                    } else {
                        tr("Autoscan is off").to_string()
                    },
                ),
            },
            None => (
                Tone::Idle,
                "",
                true,
                tr("Reading the reader state…").to_string(),
            ),
        },
        Link::Lost(e) => (
            Tone::Alert,
            glyph::WARNING,
            false,
            trf("The reader does not answer: {}", &[e]),
        ),
        Link::PortError(e) => (
            Tone::Alert,
            glyph::PLUGS,
            false,
            trf("Port error: {}", &[e]),
        ),
        Link::AccessDenied(_) => (
            Tone::Alert,
            glyph::LOCK_KEY,
            false,
            tr("No access to the serial port").to_string(),
        ),
        Link::PortBusy(_) => (
            Tone::Alert,
            glyph::LOCK_KEY,
            false,
            tr("The port is used by another program").to_string(),
        ),
        Link::Unsupported(e) => (
            Tone::Caution,
            glyph::QUESTION,
            false,
            trf("Not supported: {}", &[e]),
        ),
        Link::Bootloader => (
            Tone::Caution,
            glyph::ARROW_LINE_UP,
            false,
            tr("The reader waits for the firmware in its bootloader").to_string(),
        ),
    };
    Look {
        tone,
        glyph,
        spin,
        text,
    }
}

impl App {
    fn strip(&mut self, ui: &mut Ui, st: &Status) {
        let full = ui.max_rect();
        let look = look(st);
        let disc = Rect::from_center_size(
            Pos2::new(full.left() + 38.0, full.center().y),
            Vec2::splat(52.0),
        );
        let pulse = st
            .last_event_at
            .map(|t| t.elapsed().as_secs_f32())
            .filter(|_| st.connected());
        let (tone, glyph, spin) = match self.confirm {
            Some(_) => (Tone::Caution, glyph::WARNING, false),
            None => (look.tone, look.glyph, look.spin),
        };
        style::disc(
            ui,
            disc,
            tone,
            glyph,
            pulse.filter(|_| self.confirm.is_none()),
            spin,
        );
        let info = Rect::from_min_max(
            Pos2::new(disc.right() + 8.0, full.top() + 12.0),
            Pos2::new(full.right() - 14.0, full.bottom() - 12.0),
        );
        match self.confirm {
            Some(c) => self.confirm_view(ui, info, c),
            None => self.info_view(ui, info, st, &look),
        }
    }

    fn info_view(&mut self, ui: &mut Ui, info: Rect, st: &Status, look: &Look) {
        let p = pal(ui);
        let title_rect = Rect::from_min_size(info.min, Vec2::new(info.width(), 22.0));
        ui.scope_builder(
            UiBuilder::new()
                .max_rect(title_rect)
                .layout(Layout::left_to_right(Align::Center)),
            |ui| {
                ui.spacing_mut().item_spacing.x = 8.0;
                ui.add(
                    egui::Label::new(strong("ODRFID-485").size(15.5).color(p.text))
                        .selectable(false),
                );
                if let Some(id) = st.identity.filter(|_| st.connected()) {
                    let mut detail = st.build.clone().unwrap_or_else(|| id.version());
                    detail.push_str(" · S/N ");
                    detail.push_str(&id.serial_text());
                    ui.add(
                        egui::Label::new(RichText::new(detail).size(11.5).color(p.faint))
                            .truncate()
                            .selectable(false),
                    );
                }
            },
        );

        let status_rect = Rect::from_min_size(
            info.min + Vec2::new(0.0, 25.0),
            Vec2::new(info.width(), 18.0),
        );
        ui.scope_builder(
            UiBuilder::new()
                .max_rect(status_rect)
                .layout(Layout::right_to_left(Align::Center)),
            |ui| {
                if let Some(t) = st.target.as_ref().filter(|_| st.connected()) {
                    let text = format!(
                        "{}  {} · {}",
                        glyph::ARROWS_LEFT_RIGHT,
                        trf("address {}", &[&t.address.unwrap_or(0)]),
                        t.line.label()
                    );
                    ui.add(
                        egui::Label::new(RichText::new(text).size(12.0).color(p.sub))
                            .selectable(false),
                    )
                    .on_hover_text(&t.port);
                }
                ui.with_layout(Layout::left_to_right(Align::Center), |ui| {
                    let (text, color) = match &self.flash {
                        Some((_, n)) => (
                            n.text.clone(),
                            p.tone(if n.ok { Tone::Good } else { Tone::Alert }),
                        ),
                        None => (look.text.clone(), p.tone(look.tone)),
                    };
                    ui.add(
                        egui::Label::new(RichText::new(&text).size(12.5).color(color))
                            .truncate()
                            .selectable(false),
                    )
                    .on_hover_text(text);
                });
            },
        );

        let tools_row = Rect::from_min_max(Pos2::new(info.left(), info.bottom() - 30.0), info.max);
        ui.scope_builder(
            UiBuilder::new()
                .max_rect(tools_row)
                .layout(Layout::right_to_left(Align::Center)),
            |ui| {
                ui.spacing_mut().item_spacing.x = 6.0;
                self.tools(ui, st);
                ui.add_space(4.0);
                ui.with_layout(Layout::left_to_right(Align::Center), |ui| {
                    ui.set_clip_rect(ui.max_rect());
                    ui.spacing_mut().item_spacing.x = 12.0;
                    tags(ui, st);
                });
            },
        );
    }

    fn tools(&mut self, ui: &mut Ui, st: &Status) {
        // Right to left: connection, then the reader's actions.
        let connected = st.connected();
        let active = !matches!(st.link, Link::Idle);
        if active {
            if tool(ui, glyph::X, tr("Disconnect"), false, Some(Tone::Alert)).clicked() {
                self.engine.send(Request::Disconnect);
            }
        } else {
            ui.add_enabled_ui(self.config.port.is_some(), |ui| {
                if tool(ui, glyph::PLUGS_CONNECTED, tr("Connect"), false, None).clicked() {
                    self.connect();
                }
            });
        }
        ui.add_enabled_ui(connected && st.busy.is_none(), |ui| {
            if tool(
                ui,
                glyph::DOWNLOAD_SIMPLE,
                tr("Read the settings again"),
                false,
                None,
            )
            .clicked()
            {
                self.engine.send(Request::Reload);
            }
            if tool(ui, glyph::BELL_RINGING, tr("Beep once"), false, None).clicked() {
                self.engine.send(Request::Beep {
                    count: 1,
                    on_ms: 200,
                    off_ms: 200,
                });
            }
        });
    }

    fn confirm_view(&mut self, ui: &mut Ui, info: Rect, confirm: Confirm) {
        let p = pal(ui);
        let (title, text, action) = match confirm {
            Confirm::Restart => (
                tr("Restart the reader?"),
                tr("Settings that are not saved, the event queue and the last command are lost."),
                tr("Restart"),
            ),
            Confirm::FactoryReset => (
                tr("Restore the factory settings?"),
                tr("Keys, scanning and the bus go back to the defaults: 115200 8N1, address 1."),
                tr("Reset"),
            ),
            Confirm::Bootloader => (
                tr("Start the bootloader for a firmware update?"),
                tr(
                    "Modbus stops while the reader waits for the firmware. If no upload starts within 30 s, the reader returns to work.",
                ),
                tr("Start the bootloader"),
            ),
        };
        let title_rect = Rect::from_min_size(info.min, Vec2::new(info.width(), 22.0));
        ui.scope_builder(
            UiBuilder::new()
                .max_rect(title_rect)
                .layout(Layout::left_to_right(Align::Center)),
            |ui| {
                ui.add(
                    egui::Label::new(strong(title).size(15.0).color(p.caution))
                        .truncate()
                        .selectable(false),
                );
            },
        );
        let text_rect = Rect::from_min_size(
            info.min + Vec2::new(0.0, 25.0),
            Vec2::new(info.width(), 18.0),
        );
        ui.scope_builder(
            UiBuilder::new()
                .max_rect(text_rect)
                .layout(Layout::left_to_right(Align::Center)),
            |ui| {
                ui.add(
                    egui::Label::new(RichText::new(text).size(12.5).color(p.sub))
                        .truncate()
                        .selectable(false),
                )
                .on_hover_text(text);
            },
        );
        let buttons = Rect::from_min_max(Pos2::new(info.left(), info.bottom() - 28.0), info.max);
        ui.scope_builder(
            UiBuilder::new()
                .max_rect(buttons)
                .layout(Layout::left_to_right(Align::Center)),
            |ui| {
                if style::button(ui, Kind::Danger, action).clicked() {
                    self.engine.send(match confirm {
                        Confirm::Restart => Request::Restart,
                        Confirm::FactoryReset => Request::FactoryReset,
                        Confirm::Bootloader => Request::Bootloader,
                    });
                    self.confirm = None;
                }
                if style::button(ui, Kind::Plain, tr("Cancel")).clicked() {
                    self.confirm = None;
                }
            },
        );
    }
}

fn on_off_tag(ui: &mut Ui, on: bool, on_text: &str, off_text: &str) {
    let p = pal(ui);
    style::tag(
        ui,
        if on { p.brand } else { p.idle },
        if on { on_text } else { off_text },
    );
}

fn tags(ui: &mut Ui, st: &Status) {
    let p = pal(ui);
    if !st.connected() {
        let hint = match &st.link {
            Link::Idle => tr("Choose the port and connect"),
            Link::NotFound | Link::Lost(_) => {
                tr("Check the wiring, the power and the bus settings")
            }
            Link::PortError(_) => tr("Retrying…"),
            Link::AccessDenied(_) | Link::PortBusy(_) => tr("See the Connection page"),
            Link::Bootloader => tr("Open the loader and upload the firmware within 30 s"),
            _ => "",
        };
        style::sub(ui, hint);
        return;
    }
    let Some(live) = &st.live else {
        return;
    };
    on_off_tag(ui, live.input, tr("IN high"), tr("IN low"));
    on_off_tag(ui, live.output, tr("OUT on"), tr("OUT off"));
    on_off_tag(ui, live.led1, tr("Green on"), tr("Green off"));
    on_off_tag(ui, live.led2, tr("Red on"), tr("Red off"));
    if live.beeper {
        style::tag(ui, p.caution, tr("Beeping"));
    }
    if live.queue > 0 {
        style::tag(ui, p.brand, &trf("Events: {}", &[&live.queue]))
            .on_hover_text(tr("Card events waiting in the reader's queue"));
    }
    if live.odometer_fault {
        style::tag(ui, p.alert, tr("Odometer fault"))
            .on_hover_text(tr("The operating time is not saved to EEPROM"));
    }
    if let Some(test) = st.self_test.filter(|t| !t.passed()) {
        style::tag(ui, p.alert, tr("Self-test failed")).on_hover_text(test.faults());
    }
}

// ---- small widgets -----------------------------------------------------------

/// One line of a two-column [`egui::Grid`].
fn fact(ui: &mut Ui, label: &str, value: &str) {
    let p = pal(ui);
    ui.label(RichText::new(label).size(12.0).color(p.sub));
    ui.add(egui::Label::new(RichText::new(value).size(12.0).color(p.text)).wrap());
    ui.end_row();
}

fn facts_grid(ui: &mut Ui, id: &str, add: impl FnOnce(&mut Ui)) {
    egui::Frame::new()
        .inner_margin(egui::Margin {
            left: 23,
            right: 0,
            top: 2,
            bottom: 4,
        })
        .show(ui, |ui| {
            egui::Grid::new(id)
                .num_columns(2)
                .spacing([14.0, 3.0])
                .show(ui, add);
        });
}

/// A small explanation under a group of settings, aligned with the labels.
fn sub_note(ui: &mut Ui, text: &str) {
    let color = pal(ui).faint;
    egui::Frame::new()
        .inner_margin(egui::Margin {
            left: 23,
            right: 0,
            top: 2,
            bottom: 2,
        })
        .show(ui, |ui| {
            ui.add(egui::Label::new(RichText::new(text).size(11.5).color(color)).wrap());
        });
}

/// One entry of the page list. `marker` flags unsaved changes.
fn nav_item(
    ui: &mut Ui,
    glyph: &str,
    label: &str,
    selected: bool,
    marker: bool,
    dim: bool,
) -> egui::Response {
    let p = pal(ui);
    let width = ui.available_width();
    let (rect, response) = ui.allocate_exact_size(Vec2::new(width, 32.0), Sense::click());
    if ui.is_rect_visible(rect) {
        let hovered = response.hovered();
        let fill = if selected {
            p.wash(Tone::Brand)
        } else if hovered {
            p.control
        } else {
            Color32::TRANSPARENT
        };
        ui.painter().rect_filled(rect, CornerRadius::same(7), fill);
        let (icon, text) = if selected {
            (p.brand, p.text)
        } else if dim {
            (p.faint, p.faint)
        } else if hovered {
            (p.text, p.text)
        } else {
            (p.sub, p.sub)
        };
        style::icon(
            ui.painter(),
            Pos2::new(rect.left() + 18.0, rect.center().y),
            glyph,
            16.0,
            icon,
        );
        let family = if selected {
            style::bold()
        } else {
            FontFamily::Proportional
        };
        ui.painter().text(
            Pos2::new(rect.left() + 36.0, rect.center().y),
            Align2::LEFT_CENTER,
            label,
            FontId::new(13.0, family),
            text,
        );
        if marker {
            ui.painter().circle_filled(
                Pos2::new(rect.right() - 12.0, rect.center().y),
                3.5,
                p.caution,
            );
        }
    }
    response
}

/// Hex digits only, upper case, `bytes` long; red when incomplete.
fn hex_edit(ui: &mut Ui, text: &mut String, bytes: usize, width: f32) -> egui::Response {
    let p = pal(ui);
    let complete = text.len() == bytes * 2;
    let edit = egui::TextEdit::singleline(text)
        .char_limit(bytes * 2)
        .desired_width(width)
        .font(egui::TextStyle::Monospace)
        .text_color(if complete { p.text } else { p.alert });
    let r = ui.add(edit);
    if r.changed() {
        let clean: String = text
            .chars()
            .filter(char::is_ascii_hexdigit)
            .map(|c| c.to_ascii_uppercase())
            .take(bytes * 2)
            .collect();
        *text = clean;
    }
    r.on_hover_text(trf("{} bytes in hex", &[&bytes]))
}

fn baud_combo(ui: &mut Ui, id: &str, baud: &mut u32) {
    egui::ComboBox::from_id_salt(id)
        .width(110.0)
        .selected_text(baud.to_string())
        .show_ui(ui, |ui| {
            for b in BAUD_RATES {
                ui.selectable_value(baud, b, b.to_string());
            }
            ui.separator();
            ui.horizontal(|ui| {
                ui.label(tr("Other:"));
                ui.add(
                    egui::DragValue::new(baud)
                        .range(BAUD_MIN..=BAUD_MAX)
                        .speed(100.0),
                );
            });
        });
    // The reader stores the speed / 100.
    *baud = (*baud / 100 * 100).clamp(BAUD_MIN, BAUD_MAX);
}

fn parity_choice(ui: &mut Ui, parity: &mut Parity) -> bool {
    style::choice(
        ui,
        parity,
        &[
            (Parity::None, tr("None")),
            (Parity::Odd, tr("Odd")),
            (Parity::Even, tr("Even")),
        ],
    )
}

fn stop_choice(ui: &mut Ui, stop: &mut StopBits) -> bool {
    style::choice(
        ui,
        stop,
        &[
            (StopBits::One, "1"),
            (StopBits::OneAndHalf, "1.5"),
            (StopBits::Two, "2"),
        ],
    )
}

fn address_value(ui: &mut Ui, address: &mut u8) -> egui::Response {
    ui.add(
        egui::DragValue::new(address)
            .range(1..=247)
            .speed(0.2)
            .custom_formatter(|v, _| format!("{v} ({:02X}h)", v as u8)),
    )
}

// ---- navigation and the settings pages ---------------------------------------

impl App {
    fn navigation(&mut self, ui: &mut Ui, st: &Status) {
        let p = pal(ui);
        ui.spacing_mut().item_spacing.y = 2.0;
        for (heading, pages) in Page::GROUPS {
            if !heading.is_empty() {
                ui.add_space(10.0);
                ui.label(
                    RichText::new(tr(heading).to_uppercase())
                        .family(style::bold())
                        .size(10.5)
                        .extra_letter_spacing(0.8)
                        .color(p.faint),
                );
                ui.add_space(2.0);
            }
            for &page in pages {
                let marker = self.draft.as_ref().is_some_and(|d| d.page_dirty(page));
                let dim = page.needs_reader() && !st.connected();
                let r = nav_item(
                    ui,
                    page.glyph(),
                    page.title(),
                    self.page == page,
                    marker,
                    dim,
                );
                let r = if marker {
                    r.on_hover_text(tr("Not saved yet"))
                } else {
                    r
                };
                if r.clicked() {
                    self.page = page;
                }
            }
        }
    }

    fn page_body(&mut self, ui: &mut Ui, st: &Status) {
        heading(ui, self.page.title());
        if self.page.needs_reader() && !st.connected() {
            ui.add_space(4.0);
            style::sub(ui, tr("Connect to a reader on the Connection page."));
            ui.add_space(4.0);
            if style::button(ui, Kind::Plain, tr("Go to the Connection page")).clicked() {
                self.page = Page::Connection;
            }
            return;
        }
        match self.page {
            Page::Connection => {
                self.connection(ui, st);
                if st.connected() {
                    self.about_reader(ui, st);
                }
            }
            Page::Scanning | Page::Keys | Page::Bus => self.settings_page(ui, st),
            Page::Cards => self.cards_page(ui, st),
            Page::Commands => self.command_block(ui, st),
            Page::Service => self.maintenance(ui, st),
            Page::App => self.app_page(ui),
            Page::Log => {}
        }
    }

    fn settings_page(&mut self, ui: &mut Ui, st: &Status) {
        let Some(mut d) = self.draft.take() else {
            return;
        };
        let saving = st.busy == Some(Busy::Saving);
        let back = ui
            .add_enabled_ui(!saving, |ui| match self.page {
                Page::Scanning => scanning_form(ui, &mut d, st.scan_saved),
                Page::Keys => {
                    keys_form(ui, &mut d);
                    false
                }
                Page::Bus => {
                    bus_form(ui, &mut d);
                    false
                }
                _ => false,
            })
            .inner;
        if let (true, Some(saved)) = (back, st.scan_saved) {
            self.engine.send(Request::SetScan(saved));
        }
        d.sync_keys();
        self.draft = Some(d);
    }

    /// Save and undo for all three settings pages.
    fn save_bar(&mut self, ui: &mut Ui, st: &Status) {
        let Some(d) = &mut self.draft else {
            return;
        };
        let p = pal(ui);
        let saving = st.busy == Some(Busy::Saving);
        let problems = d.edit.problems();
        let dirty = d.dirty();
        let ok = !saving && dirty && d.keys_valid() && problems.is_empty();
        if ui
            .add_enabled_ui(ok, |ui| {
                style::button(
                    ui,
                    Kind::Primary,
                    &format!("{}  {}", glyph::FLOPPY_DISK, tr("Save to the reader")),
                )
            })
            .inner
            .on_hover_text(tr(
                "Saving writes the settings to the reader's EEPROM; they work at once.",
            ))
            .clicked()
        {
            self.engine.send(Request::WriteSettings(d.edit));
        }
        if ui
            .add_enabled_ui(dirty && !saving, |ui| {
                style::button(ui, Kind::Plain, tr("Undo"))
            })
            .inner
            .clicked()
        {
            let base = d.base;
            d.reset_to(base);
        }
        let (text, color) = if saving {
            (tr("Saving…").to_string(), p.sub)
        } else if !d.keys_valid() {
            (tr("A key is incomplete").to_string(), p.alert)
        } else if let Some(problem) = problems.first() {
            (problem.to_string(), p.alert)
        } else if dirty {
            let pages: Vec<&str> = [Page::Scanning, Page::Keys, Page::Bus]
                .into_iter()
                .filter(|&pg| d.page_dirty(pg))
                .map(|pg| pg.title())
                .collect();
            (trf("Not saved yet: {}", &[&pages.join(", ")]), p.caution)
        } else {
            (tr("The settings match the reader").to_string(), p.faint)
        };
        ui.with_layout(Layout::left_to_right(Align::Center), |ui| {
            ui.add(
                egui::Label::new(RichText::new(text).size(12.0).color(color))
                    .truncate()
                    .selectable(false),
            );
        });
    }
}

/// `held`: the saved autoscan, while RAM holds the other value until the
/// reader restarts. True when the user asks for the saved value back.
fn scanning_form(ui: &mut Ui, d: &mut Draft, held: Option<bool>) -> bool {
    let mut back = false;
    let e = &mut d.edit;
    row_tip(
        ui,
        glyph::BROADCAST,
        tr("Autoscan"),
        Some(tr(
            "Find cards by itself and queue an event for each new one. Card operations of the command block need it off.",
        )),
        |ui| toggle(ui, &mut e.scan),
    );
    if held.is_some() {
        style::note(ui, Tone::Caution, glyph::INFO, |ui| {
            ui.label(tr(
                "Switched on the Command block page until the reader restarts. The switch above shows the saved setting; saving keeps both.",
            ));
            back = style::button(ui, Kind::Plain, tr("Back to the saved setting")).clicked();
        });
    }
    row_tip(
        ui,
        glyph::TIMER,
        tr("Scan period, ms"),
        Some(tr("Values below 100 ms count as 100 ms")),
        |ui| {
            ui.add(
                egui::DragValue::new(&mut e.interval_ms)
                    .range(INTERVAL_MIN..=u16::MAX)
                    .speed(5.0),
            )
        },
    );
    row_tip(
        ui,
        glyph::WAVEFORM,
        tr("Receiver gain, dB"),
        Some(tr(
            "Higher gain reads cards from further away but picks up more noise",
        )),
        |ui| {
            let items: Vec<(u8, String)> = GAINS.iter().map(|&g| (g, g.to_string())).collect();
            let refs: Vec<(u8, &str)> = items.iter().map(|(g, s)| (*g, s.as_str())).collect();
            style::choice(ui, &mut e.gain_db, &refs)
        },
    );

    ui.add_space(8.0);
    heading(ui, tr("Indication"));
    row_tip(
        ui,
        glyph::LIGHTBULB,
        tr("Green LED shows a card in the field"),
        Some(tr(
            "Turn it off to drive the green LED only from the Card page",
        )),
        |ui| toggle(ui, &mut e.auto_led),
    );
    row_tip(
        ui,
        glyph::SPEAKER_HIGH,
        tr("Beep for a new card"),
        Some(tr("Also a 200 ms beep when the reader starts")),
        |ui| toggle(ui, &mut e.auto_beep),
    );

    ui.add_space(8.0);
    heading(ui, tr("Autoread"));
    row_tip(
        ui,
        glyph::STACK,
        tr("Blocks to read for each card"),
        Some(tr("0 turns autoread off")),
        |ui| ui.add(egui::DragValue::new(&mut e.autoread_count).range(0..=AUTOREAD_MAX)),
    );
    ui.add_enabled_ui(e.autoread_count > 0, |ui| {
        row(ui, "", tr("First block"), |ui| {
            ui.add(egui::DragValue::new(&mut e.autoread_first).range(0..=255))
        });
    });
    sub_note(
        ui,
        tr(
            "The autoscan reads them for each new card: at most 64 bytes, 4 Classic blocks or 16 Ultralight pages. Classic cards use the key on the Keys page.",
        ),
    );
    back
}

fn keys_form(ui: &mut Ui, d: &mut Draft) {
    row(ui, glyph::KEY, tr("Mifare Classic key"), |ui| {
        hex_edit(ui, &mut d.key, 6, 130.0);
        style::choice(ui, &mut d.edit.key_b, &[(false, "A"), (true, "B")]);
    });
    sub_note(
        ui,
        tr("Key type and 6 bytes: for autoread and for the block operations of the command block."),
    );
    ui.add_space(4.0);
    row(
        ui,
        glyph::PASSWORD,
        tr("Ultralight EV1 / NTAG password"),
        |ui| {
            hex_edit(ui, &mut d.ul_password, 4, 130.0);
        },
    );
    sub_note(ui, tr("4 bytes in card order."));
    ui.add_space(4.0);
    row(ui, glyph::KEY, tr("Mifare Plus AES key"), |ui| {
        hex_edit(ui, &mut d.aes_key, 16, 270.0);
    });
    sub_note(
        ui,
        tr("16 bytes; Plus SL3 cards are authenticated with it before reading."),
    );
}

fn bus_form(ui: &mut Ui, d: &mut Draft) {
    let b = &mut d.edit.bus;
    row(ui, glyph::HASH, tr("Address"), |ui| {
        address_value(ui, &mut b.address)
    });
    row(ui, "", tr("Speed, baud"), |ui| {
        baud_combo(ui, "bus-baud", &mut b.baud)
    });
    row(ui, "", tr("Parity"), |ui| parity_choice(ui, &mut b.parity));
    row(ui, "", tr("Stop bits"), |ui| stop_choice(ui, &mut b.stop));
    row_tip(
        ui,
        glyph::TIMER,
        tr("End-of-frame pause, ms"),
        Some(tr(
            "0: the Modbus RTU minimum, 3.5 characters (1.75 ms from 19200 baud). The reader waits this long before every answer: at most 100 ms here.",
        )),
        |ui| {
            // A longer pause set elsewhere is shown as it is, not cut down.
            ui.add(
                egui::DragValue::new(&mut b.frame_pause_ms)
                    .range(0..=FRAME_PAUSE_MAX)
                    .clamp_existing_to_range(false),
            )
        },
    );
    ui.add_space(6.0);
    if d.edit.bus != d.base.bus {
        style::note(ui, Tone::Caution, glyph::WARNING, |ui| {
            ui.label(trf(
                "The reader switches to {}, address {} at once and the tool follows. The settings are saved only if the reader answers there; otherwise power it off and on to return to the old ones.",
                &[&d.edit.bus.line().label(), &d.edit.bus.address],
            ));
        });
    } else {
        sub_note(
            ui,
            tr(
                "A change is applied first and saved only once the reader answers on the new settings.",
            ),
        );
    }
}

// ---- the connection page -------------------------------------------------------

impl App {
    fn connection(&mut self, ui: &mut Ui, st: &Status) {
        let p = pal(ui);
        row(ui, glyph::PLUGS, tr("Serial port"), |ui| {
            let current = self.config.port.clone().unwrap_or_default();
            let combo = egui::ComboBox::from_id_salt("port")
                .width(250.0)
                .selected_text(if current.is_empty() {
                    tr("Choose…").to_string()
                } else {
                    current.clone()
                })
                .show_ui(ui, |ui| {
                    for port in &st.ports {
                        let label = if port.description.is_empty() {
                            port.name.clone()
                        } else {
                            format!("{} — {}", port.name, port.description)
                        };
                        ui.selectable_value(&mut self.config.port, Some(port.name.clone()), label);
                    }
                    if st.ports.is_empty() {
                        ui.label(RichText::new(tr("No serial ports found")).color(p.sub));
                    }
                    ui.separator();
                    ui.horizontal(|ui| {
                        ui.label(tr("Other:"));
                        let mut text = current.clone();
                        if ui
                            .add(
                                egui::TextEdit::singleline(&mut text)
                                    .desired_width(160.0)
                                    .hint_text(if cfg!(windows) {
                                        "COM3"
                                    } else {
                                        "/dev/ttyUSB0"
                                    }),
                            )
                            .changed()
                        {
                            self.config.port =
                                Some(text.trim().to_string()).filter(|t| !t.is_empty());
                        }
                    });
                });
            // The list is read again whenever it is opened: adapters come
            // and go.
            if combo.response.clicked() {
                self.engine.send(Request::RefreshPorts);
            }
        });
        row(ui, "", tr("Speed, baud"), |ui| {
            baud_combo(ui, "baud", &mut self.config.line.baud)
        });
        row(ui, "", tr("Parity"), |ui| {
            parity_choice(ui, &mut self.config.line.parity)
        });
        row_tip(
            ui,
            "",
            tr("Stop bits"),
            Some(tr(
                "The PC sends 2 stop bits for 1.5: serial ports have no 1.5",
            )),
            |ui| stop_choice(ui, &mut self.config.line.stop),
        );
        row(ui, glyph::HASH, tr("Reader address"), |ui| {
            ui.add_enabled_ui(!self.config.scan_all, |ui| {
                address_value(ui, &mut self.config.address)
            });
        });
        row_tip(
            ui,
            glyph::MAGNIFYING_GLASS,
            tr("Scan all addresses"),
            Some(tr(
                "Ask every address 1 to 247: about 20 s at 115200 baud, a minute and a half at 1200",
            )),
            |ui| toggle(ui, &mut self.config.scan_all),
        );
        row(
            ui,
            glyph::PLUGS_CONNECTED,
            tr("Connect when the tool starts"),
            |ui| toggle(ui, &mut self.config.auto_connect),
        );

        let wanted = self.target();
        let state = match &st.link {
            Link::Connected if wanted != st.target && wanted.is_some() => {
                tr("The connection settings changed")
            }
            Link::Connected => "",
            Link::Idle => tr("Not connected"),
            _ => "",
        };
        ui.add_space(2.0);
        row(ui, "", state, |ui| {
            let busy = matches!(st.link, Link::Opening | Link::Scanning { .. });
            let connected_here = st.connected() && wanted == st.target;
            if busy {
                if style::button(ui, Kind::Plain, tr("Stop")).clicked() {
                    self.engine.send(Request::Disconnect);
                }
            } else if connected_here {
                if style::button(ui, Kind::Plain, tr("Disconnect")).clicked() {
                    self.engine.send(Request::Disconnect);
                }
            } else {
                let label = if matches!(st.link, Link::Idle) {
                    tr("Connect")
                } else {
                    tr("Connect again")
                };
                if ui
                    .add_enabled_ui(wanted.is_some(), |ui| {
                        style::button(ui, Kind::Primary, label)
                    })
                    .inner
                    .clicked()
                {
                    self.connect();
                }
                if !matches!(st.link, Link::Idle)
                    && style::button(ui, Kind::Plain, tr("Disconnect")).clicked()
                {
                    self.engine.send(Request::Disconnect);
                }
            }
        });

        match &st.link {
            Link::Choose | Link::Scanning { .. } if !st.found.is_empty() => {
                ui.add_space(4.0);
                heading(ui, tr("Devices found"));
                for f in &st.found {
                    let text = match (&f.identity, &f.note) {
                        (Some(id), None) => trf(
                            "Address {} · S/N {} · firmware {}",
                            &[&f.address, &id.serial_text(), &id.version()],
                        ),
                        (_, Some(note)) => trf("Address {} · {}", &[&f.address, note]),
                        (None, None) => trf("Address {}", &[&f.address]),
                    };
                    row(ui, glyph::CARET_RIGHT, &text, |ui| {
                        if f.usable()
                            && st.link == Link::Choose
                            && style::button(ui, Kind::Primary, tr("Use")).clicked()
                        {
                            self.engine.send(Request::Choose(f.address));
                        }
                    });
                }
            }
            Link::AccessDenied(e) | Link::PortBusy(e) => {
                ui.add_space(4.0);
                let denied = matches!(st.link, Link::AccessDenied(_));
                style::note(ui, Tone::Alert, glyph::LOCK_KEY, |ui| {
                    ui.label(strong(if denied {
                        tr("No access to the serial port")
                    } else {
                        tr("The port is used by another program")
                    }));
                    ui.label(RichText::new(e).size(11.5).color(p.sub));
                    if denied && cfg!(target_os = "linux") {
                        ui.label(tr("Linux opens serial ports for the dialout group (uucp on Arch). Add yourself to it, then log out and back in:"));
                        let command = "sudo usermod -aG dialout \"$USER\"";
                        ui.add(
                            egui::Label::new(RichText::new(command).monospace().size(11.5)).wrap(),
                        );
                        if style::button(
                            ui,
                            Kind::Primary,
                            &format!("{}  {}", glyph::COPY, tr("Copy the command")),
                        )
                        .clicked()
                        {
                            ui.ctx().copy_text(command.into());
                            self.flash(true, tr("The command is copied to the clipboard"));
                        }
                    } else if !denied {
                        ui.label(tr("Close the other program (a terminal, another configuration tool) and connect again."));
                    }
                });
            }
            Link::Unsupported(_) => {
                ui.add_space(4.0);
                style::note(ui, Tone::Caution, glyph::INFO, |ui| {
                    ui.label(tr("This tool works with ODRFID-485 readers with firmware 4.x. Configure readers with 3.x in ODRFIDConfigM."));
                });
            }
            _ => {}
        }
    }

    fn about_reader(&mut self, ui: &mut Ui, st: &Status) {
        let (Some(id), Some(live)) = (st.identity, st.live.as_ref()) else {
            return;
        };
        ui.add_space(8.0);
        heading(ui, tr("Reader"));
        facts_grid(ui, "reader-facts", |ui| {
            fact(ui, tr("Model"), "ODRFID-485 (Modbus RTU)");
            fact(
                ui,
                tr("Firmware"),
                &st.build.clone().unwrap_or_else(|| id.version()),
            );
            fact(ui, tr("Serial number"), &id.serial_text());
            let mut odometer = sysint::format_minutes(live.odometer_min);
            if live.odometer_fault {
                odometer.push_str(" · ");
                odometer.push_str(tr("not saved to EEPROM"));
            }
            fact(ui, tr("Operating time"), &odometer);
            fact(
                ui,
                tr("Starts"),
                &if live.boot_count == 0 {
                    tr("not counted: EEPROM write failed").to_string()
                } else {
                    live.boot_count.to_string()
                },
            );
            let chip = if live.rfid_fault {
                tr("MFRC522 did not start").to_string()
            } else {
                match st.self_test {
                    Some(t) => format!("{} · MFRC522 {}", tr("OK"), t.chip_text()),
                    None => tr("OK").to_string(),
                }
            };
            fact(ui, tr("RFID chip"), &chip);
            let Some(test) = st.self_test else {
                return;
            };
            fact(
                ui,
                tr("Self-test at start"),
                &if test.passed() {
                    tr("passed").to_string()
                } else {
                    test.faults()
                },
            );
            if let Some(vdda) = test.vdda_text() {
                fact(ui, tr("Supply at start"), &vdda);
            }
            fact(ui, tr("Last reset"), &test.reset_text());
        });
    }

    fn maintenance(&mut self, ui: &mut Ui, st: &Status) {
        let idle = st.busy.is_none();
        ui.add_enabled_ui(idle, |ui| {
            row(
                ui,
                glyph::ARROW_COUNTER_CLOCKWISE,
                tr("Restart the reader"),
                |ui| {
                    if style::button(ui, Kind::Plain, tr("Restart…")).clicked() {
                        self.confirm = Some(Confirm::Restart);
                    }
                },
            );
            row(ui, glyph::FACTORY, tr("Factory settings"), |ui| {
                if style::button(ui, Kind::Plain, tr("Reset…")).clicked() {
                    self.confirm = Some(Confirm::FactoryReset);
                }
            });
            row(ui, glyph::ARROW_LINE_UP, tr("Firmware update"), |ui| {
                if style::button(ui, Kind::Plain, tr("Bootloader…")).clicked() {
                    self.confirm = Some(Confirm::Bootloader);
                }
            });
        });
        sub_note(
            ui,
            tr(
                "The bootloader takes the firmware over the same RS-485 line. The tool closes the port: open the loader and upload the firmware. Without it the reader returns to work in 30 s.",
            ),
        );
    }
}

// ---- the cards page ------------------------------------------------------------

impl App {
    fn cards_page(&mut self, ui: &mut Ui, st: &Status) {
        let p = pal(ui);
        let Some(live) = st.live.clone() else {
            return;
        };
        let idle = st.busy.is_none();

        match &live.card {
            Some(card) => {
                facts_grid(ui, "card-facts", |ui| {
                    fact(ui, tr("Type"), &card_type_name(card.kind));
                    ui.label(RichText::new("UID").size(12.0).color(p.sub));
                    ui.horizontal(|ui| {
                        ui.label(
                            RichText::new(card.uid_text())
                                .monospace()
                                .size(12.0)
                                .color(p.text),
                        );
                        if style::corner(ui, glyph::COPY, tr("Copy"), false).clicked() {
                            ui.ctx().copy_text(card.uid_text());
                            self.flash(true, tr("The UID is copied to the clipboard"));
                        }
                    });
                    ui.end_row();
                    fact(ui, "SAK", &format!("{:02X}h", card.sak));
                    if card.blocks > 0 {
                        fact(
                            ui,
                            tr("Memory"),
                            &trf("{} blocks × {} B", &[&card.blocks, &card.block_size]),
                        );
                    }
                    fact(
                        ui,
                        tr("In the field"),
                        if live.card_present {
                            tr("yes")
                        } else {
                            tr("not now")
                        },
                    );
                });
            }
            None => {
                sub_note(
                    ui,
                    if st.scan_on() {
                        tr("No card in the field.")
                    } else {
                        tr("No card selected: autoscan is off, select one with the command block.")
                    },
                );
            }
        }

        ui.add_space(8.0);
        heading(ui, tr("Indication"));
        let ind = st.indication.unwrap_or_default();
        ui.add_enabled_ui(idle, |ui| {
            row(ui, glyph::LIGHTBULB, tr("Green LED"), |ui| {
                let mut mode = ind.led1;
                if style::choice(
                    ui,
                    &mut mode,
                    &[(0, tr("Off")), (1, tr("On")), (2, tr("Blink"))],
                ) {
                    self.engine.send(Request::Indicate(Indicate::Led1(mode)));
                }
            });
            row(ui, "", tr("Red LED"), |ui| {
                let mut on = ind.led2;
                if toggle(ui, &mut on).changed() {
                    self.engine.send(Request::Indicate(Indicate::Led2(on)));
                }
            });
            row(ui, glyph::TOGGLE_RIGHT, tr("OUT output"), |ui| {
                let mut on = ind.out;
                if toggle(ui, &mut on).changed() {
                    self.engine.send(Request::Indicate(Indicate::Out(on)));
                }
            });
            row(ui, glyph::SPEAKER_HIGH, tr("Beeper, continuous"), |ui| {
                let mut on = ind.beeper;
                if toggle(ui, &mut on).changed() {
                    self.engine.send(Request::Indicate(Indicate::Beeper(on)));
                }
            });
            row(ui, glyph::BELL_RINGING, tr("Beep pattern"), |ui| {
                if style::button(ui, Kind::Plain, tr("Beep")).clicked() {
                    self.engine.send(Request::Beep {
                        count: self.beeps,
                        on_ms: if ind.beep_on_ms > 0 {
                            ind.beep_on_ms
                        } else {
                            200
                        },
                        off_ms: if ind.beep_off_ms > 0 {
                            ind.beep_off_ms
                        } else {
                            200
                        },
                    });
                }
                ui.add(
                    egui::DragValue::new(&mut self.beeps)
                        .range(1..=50)
                        .prefix("× "),
                );
            });
            row(ui, glyph::SIGN_IN, tr("IN input"), |ui| {
                on_off_tag(ui, live.input, tr("high"), tr("low"));
            });
        });
        sub_note(
            ui,
            tr(
                "Manual indication works alongside the automatic LED and beeper; turn those off in the settings for full control.",
            ),
        );

        ui.add_space(8.0);
        self.events(ui, st);
    }

    fn events(&mut self, ui: &mut Ui, st: &Status) {
        let p = pal(ui);
        heading(ui, tr("Card events"));
        let Some(events) = &st.events else {
            return;
        };
        if events.overflow {
            style::note(ui, Tone::Caution, glyph::WARNING, |ui| {
                ui.label(tr(
                    "The queue overflowed: older events were pushed out by newer ones.",
                ));
            });
        }
        if events.entries.is_empty() {
            sub_note(
                ui,
                tr("The queue is empty. With autoscan on, each new card adds an event."),
            );
        } else {
            facts_grid(ui, "events", |ui| {
                for e in &events.entries {
                    ui.label(
                        RichText::new(format!("#{}", e.number))
                            .size(12.0)
                            .color(p.sub),
                    );
                    let mut text = format!("{} · {}", card_type_name(e.kind), hex_compact(&e.uid));
                    if events.autoread.event == e.number && events.autoread.event != 0 {
                        text.push('\n');
                        text.push_str(&match events.autoread.error {
                            0 => trf(
                                "Autoread: {}",
                                &[&reader::hex_spaced(&events.autoread.data)],
                            ),
                            0xFFFF => tr("Autoread: the card has no such blocks").to_string(),
                            code => trf("Autoread failed: CME {}", &[&format!("{code:04X}h")]),
                        });
                    }
                    ui.add(
                        egui::Label::new(RichText::new(text).monospace().size(11.5).color(p.text))
                            .wrap(),
                    );
                    ui.end_row();
                }
            });
            row(ui, "", "", |ui| {
                let last = events.entries.last().map_or(0, |e| e.number);
                if ui
                    .add_enabled_ui(st.busy.is_none(), |ui| {
                        style::button(ui, Kind::Plain, tr("Acknowledge all"))
                    })
                    .inner
                    .on_hover_text(tr("Remove these events from the reader's queue"))
                    .clicked()
                {
                    self.engine.send(Request::Ack(last));
                }
            });
        }
    }

    fn command_block(&mut self, ui: &mut Ui, st: &Status) {
        let p = pal(ui);
        let op = find_op(self.cmd.op).unwrap_or(&OPS[0]);
        if st.scan_on() && !op.with_scan {
            style::note(ui, Tone::Caution, glyph::INFO, |ui| {
                ui.label(tr("Autoscan is on: the reader rejects card operations."));
                if style::button(ui, Kind::Plain, tr("Turn autoscan off until restart")).clicked() {
                    self.engine.send(Request::SetScan(false));
                }
            });
            ui.add_space(4.0);
        }
        row(ui, glyph::TERMINAL_WINDOW, tr("Operation"), |ui| {
            egui::ComboBox::from_id_salt("op")
                .width(300.0)
                .selected_text(format!("{:02X}h  {}", op.code, tr(op.name)))
                .show_ui(ui, |ui| {
                    for o in OPS {
                        ui.selectable_value(
                            &mut self.cmd.op,
                            o.code,
                            format!("{:02X}h  {}", o.code, tr(o.name)),
                        );
                    }
                });
        });
        let param_row = |ui: &mut Ui, label: &str, kind: Param, value: &mut u32| {
            let (max, hint) = match kind {
                Param::None => return,
                Param::Block => (255, tr("block 0..255")),
                Param::Number => (65535, tr("block or key number")),
                Param::Antenna => (2, tr("0 off, 1 on, 2 ask")),
                Param::Direction => (1, tr("0 increment, 1 decrement")),
                Param::Level => (3, tr("level 1..3")),
            };
            *value = (*value).min(max);
            row(ui, "", label, |ui| {
                ui.label(RichText::new(hint).size(11.5).color(pal(ui).faint));
                ui.add(egui::DragValue::new(value).range(0..=max));
            });
        };
        param_row(ui, "p0", op.p0, &mut self.cmd.p0);
        param_row(ui, "p1", op.p1, &mut self.cmd.p1);
        let data = match op.data {
            DataKind::None => Some(Vec::new()),
            DataKind::Value => self
                .cmd
                .data
                .trim()
                .parse::<i32>()
                .ok()
                .map(|v| v.to_be_bytes().to_vec()),
            _ => parse_hex(&self.cmd.data),
        };
        let data_ok = match (op.data, &data) {
            (_, None) => false,
            (DataKind::Uid, Some(d)) => (4..=10).contains(&d.len()),
            (DataKind::Block, Some(d)) => matches!(d.len(), 4 | 16),
            (DataKind::Block16, Some(d)) => d.len() == 16,
            (_, Some(d)) => d.len() <= CMD_DATA_MAX,
        };
        if op.data != DataKind::None {
            let (label, hint) = match op.data {
                DataKind::Uid => (tr("UID, hex"), "A1B2C3D4"),
                DataKind::Value => (tr("Operand"), "1"),
                _ => (tr("Data, hex"), "00112233…"),
            };
            row(ui, "", label, |ui| {
                ui.add(
                    egui::TextEdit::singleline(&mut self.cmd.data)
                        .desired_width(300.0)
                        .font(egui::TextStyle::Monospace)
                        .hint_text(hint)
                        .text_color(if data_ok { p.text } else { p.alert }),
                );
            });
        }
        let running = st.busy == Some(Busy::Command);
        row(ui, "", if running { tr("Running…") } else { "" }, |ui| {
            if ui
                .add_enabled_ui(st.busy.is_none() && data_ok, |ui| {
                    style::button(
                        ui,
                        Kind::Primary,
                        &format!("{}  {}", glyph::PLAY, tr("Run")),
                    )
                })
                .inner
                .clicked()
            {
                let request = CmdRequest {
                    op: op.code,
                    p0: if op.p0 == Param::None {
                        0
                    } else {
                        self.cmd.p0 as u16
                    },
                    p1: if op.p1 == Param::None {
                        0
                    } else {
                        self.cmd.p1 as u16
                    },
                    data: data.clone().unwrap_or_default(),
                };
                self.engine.send(Request::Command(request));
            }
        });
        if !st.commands.is_empty() {
            ui.add_space(8.0);
            heading(ui, tr("Results"));
            for outcome in st.commands.iter().rev().take(20) {
                command_result(ui, outcome);
            }
        }
    }
}

fn command_result(ui: &mut Ui, outcome: &CmdOutcome) {
    let p = pal(ui);
    let op = find_op(outcome.request.op);
    let name = op.map_or("", |o| tr(o.name));
    let mut head = format!(
        "{}  {:02X}h {}",
        outcome.time.format("%H:%M:%S"),
        outcome.request.op,
        name
    );
    if op.is_some_and(|o| o.p0 != Param::None) {
        head.push_str(&format!(" p0={}", outcome.request.p0));
    }
    if op.is_some_and(|o| o.p1 != Param::None) {
        head.push_str(&format!(" p1={}", outcome.request.p1));
    }
    let (tone, body) = match &outcome.result {
        Err(e) => (Tone::Alert, e.clone()),
        Ok(r) => {
            let tone = match r.status {
                CmdStatus::Done => Tone::Good,
                CmdStatus::Rejected => Tone::Caution,
                _ => Tone::Alert,
            };
            let mut text = r.status.text();
            if r.status == CmdStatus::CardError {
                text.push_str(&format!(", CME {:04X}h", r.cme));
                if let Some(t) = cme_text(r.cme) {
                    text.push_str(&format!(" ({t})"));
                }
            }
            let answer = describe_answer(op, &r.data);
            if !answer.is_empty() {
                text.push_str(": ");
                text.push_str(&answer);
            }
            (tone, text)
        }
    };
    egui::Frame::new()
        .fill(p.base)
        .corner_radius(egui::CornerRadius::same(6))
        .inner_margin(egui::Margin::symmetric(10, 6))
        .show(ui, |ui| {
            ui.set_min_width(ui.available_width());
            ui.label(RichText::new(head).size(11.5).color(p.sub));
            ui.add(
                egui::Label::new(
                    RichText::new(body)
                        .monospace()
                        .size(11.5)
                        .color(p.tone(tone)),
                )
                .wrap(),
            );
        });
    ui.add_space(2.0);
}

// ---- the frame log ---------------------------------------------------------------

impl App {
    fn log_page(&mut self, ui: &mut Ui) {
        let p = pal(ui);
        let entries = self.traffic.snapshot(self.config.log_polls);
        ui.horizontal(|ui| {
            ui.spacing_mut().item_spacing.x = 8.0;
            toggle(ui, &mut self.config.log_polls);
            ui.label(RichText::new(tr("Show polling")).color(p.text));
            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                ui.spacing_mut().item_spacing.x = 6.0;
                if tool(ui, glyph::EXPORT, tr("Save…"), false, None).clicked() {
                    self.save_log(&entries);
                }
                if tool(ui, glyph::COPY, tr("Copy"), false, None).clicked() {
                    ui.ctx().copy_text(traffic::export(&entries));
                    self.flash(true, tr("The log is copied to the clipboard"));
                }
                if tool(ui, glyph::TRASH, tr("Clear"), false, Some(Tone::Alert)).clicked() {
                    self.traffic.clear();
                }
                let paused = self.traffic.paused();
                let tip = if paused { tr("Resume") } else { tr("Pause") };
                if tool(ui, glyph::PAUSE, tip, paused, None).clicked() {
                    self.traffic.set_paused(!paused);
                }
            });
        });
        ui.add_space(6.0);
        let font = FontId::monospace(11.5);
        let row_height = ui.ctx().fonts_mut(|f| f.row_height(&font));
        egui::Frame::new()
            .fill(p.base)
            .corner_radius(egui::CornerRadius::same(6))
            .inner_margin(egui::Margin::symmetric(8, 6))
            .show(ui, |ui| {
                ui.set_min_size(ui.available_size());
                if entries.is_empty() {
                    ui.label(RichText::new(tr("No frames yet.")).color(p.faint));
                    return;
                }
                // show_rows sizes the list with the spacing it is called
                // with: set it here, not inside.
                ui.spacing_mut().item_spacing.y = 1.0;
                egui::ScrollArea::both()
                    .auto_shrink(false)
                    .stick_to_bottom(true)
                    .show_rows(ui, row_height, entries.len(), |ui, range| {
                        for e in &entries[range] {
                            log_row(ui, e, &font);
                        }
                    });
            });
    }

    fn save_log(&mut self, entries: &[Entry]) {
        let name = format!(
            "odrfid-frames-{}.tsv",
            chrono::Local::now().format("%Y%m%d-%H%M%S")
        );
        let Some(path) = rfd::FileDialog::new()
            .set_title(tr("Save the frame log"))
            .set_file_name(name)
            .add_filter("TSV", &["tsv", "txt"])
            .save_file()
        else {
            return;
        };
        match std::fs::write(&path, traffic::export(entries)) {
            Ok(()) => self.flash(true, trf("Saved to {}", &[&path.display()])),
            Err(e) => self.flash(false, trf("Cannot write {}: {}", &[&path.display(), &e])),
        }
    }
}

/// One frame as a single line: time, direction, bytes.
fn log_row(ui: &mut Ui, e: &Entry, font: &FontId) {
    use egui::text::{LayoutJob, TextFormat};
    let p = pal(ui);
    let color = match e.dir {
        Dir::Tx => p.brand,
        Dir::Rx => p.good,
        Dir::Note => p.caution,
    };
    let faded = |c: Color32| {
        if e.background {
            c.gamma_multiply(0.6)
        } else {
            c
        }
    };
    let format = |color: Color32| TextFormat {
        font_id: font.clone(),
        color: faded(color),
        ..Default::default()
    };
    let mut job = LayoutJob::default();
    job.append(
        &e.time.format("%H:%M:%S%.3f").to_string(),
        0.0,
        format(p.faint),
    );
    job.append(traffic::arrow(e.dir), 10.0, format(color));
    job.append(&e.text, 10.0, format(p.text));
    ui.add(egui::Label::new(job).extend());
}

// ---- the application page ----------------------------------------------------------

impl App {
    fn app_page(&mut self, ui: &mut Ui) {
        let p = pal(ui);
        row(ui, glyph::TRANSLATE, tr("Language"), |ui| {
            style::choice(
                ui,
                &mut self.config.language,
                &[
                    (Language::System, tr("System")),
                    (Language::En, "English"),
                    (Language::Ru, "Русский"),
                ],
            );
        });
        row(ui, glyph::PALETTE, tr("Theme"), |ui| {
            style::choice(
                ui,
                &mut self.config.theme,
                &[
                    (Theme::System, tr("System")),
                    (Theme::Light, tr("Light")),
                    (Theme::Dark, tr("Dark")),
                ],
            );
        });

        ui.add_space(8.0);
        heading(ui, tr("About"));
        ui.add_space(2.0);
        ui.label(strong(format!("odrfid-tool-modbus {APP_VERSION}")).color(p.text));
        style::sub(
            ui,
            tr(
                "OpenDev RFID configuration utility for ODRFID-485 readers with firmware 4.x over Modbus RTU.",
            ),
        );
        ui.horizontal(|ui| {
            style::sub(ui, "© Open Development");
            if ui
                .link(RichText::new("help.unitx.pro").size(12.0))
                .clicked()
            {
                let _ = sysint::open("https://help.unitx.pro");
            }
        });
        style::sub(
            ui,
            tr("Uses egui, serialport, the Inter font and Phosphor icons."),
        );
        ui.add_space(4.0);
        style::sub(
            ui,
            trf("Settings file: {}", &[&self.paths.config_file.display()]),
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_found_address_ends_the_scan() {
        let mut config = Config {
            port: Some("/dev/ttyUSB0".into()),
            scan_all: true,
            ..Config::default()
        };
        assert_eq!(target_of(&config).unwrap().address, None);
        // What the engine reports once the scan found the reader.
        let found = Target {
            address: Some(17),
            frame_pause_ms: 2,
            ..target_of(&config).unwrap()
        };
        follow(&mut config, &found);
        assert!(!config.scan_all);
        assert_eq!(config.address, 17);
        // The form now describes this very connection: nothing to redo,
        // and the next start connects to 17 without a scan.
        assert_eq!(target_of(&config), Some(found));
    }
}
