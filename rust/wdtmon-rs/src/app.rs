//! The widget: a borderless horizontal strip with the watchdog state and a
//! few actions. The gear opens a settings drawer below it by growing the
//! window; confirmations replace the strip's text in place.

use std::cell::RefCell;
use std::collections::VecDeque;
use std::rc::Rc;
use std::sync::mpsc::{self, Receiver, Sender};
use std::time::{Duration, Instant};

use eframe::egui::{
    self, Align, Layout, Pos2, Rect, RichText, Sense, Stroke, StrokeKind, Ui, UiBuilder, Vec2,
    ViewportCommand, WindowLevel,
};

use crate::config::{self, Config, Language, Paths, Theme};
use crate::engine::{
    EngineConfig, EngineHandle, Health, Link, Notice, Repaint, Request, Status, Writing,
};
use crate::glyph;
use crate::hid::{ChannelMode, Command, DIGIT_MAX, InMode, InState, Model, Settings};
use crate::i18n::{self, tr, trf};
use crate::journal::Journal;
use crate::style::{self, Kind, Tone, heading, pal, row, strong, toggle, tool};
use crate::sysint;
use crate::tray::TrayAction;
use crate::web::{self, WebContext, WebServer};

pub const APP_VERSION: &str = env!("CARGO_PKG_VERSION");
const WIDTH: f32 = 540.0;
const STRIP_HEIGHT: f32 = 104.0;
const DRAWER_HEIGHT: f32 = 440.0;
const FLASH_TIME: Duration = Duration::from_secs(4);

pub fn window_size(drawer: bool) -> Vec2 {
    Vec2::new(
        WIDTH,
        if drawer {
            STRIP_HEIGHT + DRAWER_HEIGHT
        } else {
            STRIP_HEIGHT
        },
    )
}

pub enum GuiRequest {
    Tray(TrayAction),
    /// A second instance was started.
    Show,
    #[cfg_attr(target_os = "linux", allow(dead_code))]
    Health(Health),
}

/// Sends to the UI and wakes it, also while the window is hidden.
#[derive(Clone)]
pub struct GuiSender {
    tx: Sender<GuiRequest>,
    repaint: Repaint,
}

impl GuiSender {
    pub fn new(repaint: Repaint) -> (Self, Receiver<GuiRequest>) {
        let (tx, rx) = mpsc::channel();
        (Self { tx, repaint }, rx)
    }

    pub fn send(&self, request: GuiRequest) {
        let _ = self.tx.send(request);
        self.repaint.request();
    }
}

/// How the window goes to the tray.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WindowMode {
    /// Close the window and leave the event loop (Linux: Wayland cannot hide
    /// a window); `main` opens it again on request.
    ReturnOnHide,
    /// Hide the window and keep the event loop (Windows, macOS).
    #[cfg_attr(target_os = "linux", allow(dead_code))]
    HideInPlace,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Confirm {
    Test(Command),
    Quit,
}

#[derive(Default)]
pub(crate) struct UiState {
    pub drawer: bool,
    pub confirm: Option<Confirm>,
    pub flash: Option<(Instant, Notice)>,
    pub autostart: Option<bool>,
    /// The command that installs the udev rule, once prepared.
    pub udev: Option<Result<String, String>>,
    /// Device settings being edited.
    pub draft: Option<Draft>,
}

/// Edited device settings next to what the device holds.
pub(crate) struct Draft {
    serial: String,
    base: Settings,
    edit: Settings,
}

/// Everything that outlives one window.
pub struct Shared {
    pub config: Config,
    applied: Config,
    pub paths: Paths,
    pub engine: EngineHandle,
    pub journal: Journal,
    web: Option<WebServer>,
    web_error: Option<String>,
    web_ctx: WebContext,
    pub gui_rx: Receiver<GuiRequest>,
    #[cfg_attr(target_os = "linux", allow(dead_code))]
    pub gui_tx: GuiSender,
    pub repaint: Repaint,
    pub mode: WindowMode,
    /// A tray was created (Windows/macOS: by the window).
    pub tray_created: bool,
    #[cfg(target_os = "linux")]
    pub tray: Option<std::sync::Arc<crate::tray::Tray>>,
    /// `Some(suspend)` once the user chose to quit.
    pub quit: Option<bool>,
    /// Tray actions to handle once the window is open.
    pub pending: VecDeque<TrayAction>,
    pub start_hidden: bool,
    pub started: Instant,
    pub(crate) ui: UiState,
}

impl Shared {
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        config: Config,
        paths: Paths,
        engine: EngineHandle,
        journal: Journal,
        gui_rx: Receiver<GuiRequest>,
        gui_tx: GuiSender,
        repaint: Repaint,
        mode: WindowMode,
    ) -> Self {
        let started = Instant::now();
        let web_ctx = WebContext {
            engine: engine.link(),
            token: web::new_token().into(),
            started,
        };
        let mut shared = Self {
            applied: config.clone(),
            config,
            paths,
            engine,
            journal,
            web: None,
            web_error: None,
            web_ctx,
            gui_rx,
            gui_tx,
            repaint,
            mode,
            tray_created: false,
            #[cfg(target_os = "linux")]
            tray: None,
            quit: None,
            pending: VecDeque::new(),
            start_hidden: false,
            started,
            ui: UiState::default(),
        };
        shared.apply_web();
        shared
    }

    /// Persists and applies changed settings.
    fn commit_config(&mut self) {
        self.config.sanitize();
        if self.config == self.applied {
            return;
        }
        if let Err(e) = config::save(&self.paths.config_file, &self.config) {
            log::warn!("{}: {e}", self.paths.config_file.display());
        }
        let engine_config = EngineConfig::from_config(&self.config);
        if engine_config != EngineConfig::from_config(&self.applied) {
            self.engine.send(Request::Configure(engine_config));
        }
        if self.config.logging != self.applied.logging {
            self.journal.configure(self.config.logging.clone());
        }
        if self.config.language != self.applied.language {
            i18n::set_language(self.config.language);
            #[cfg(target_os = "linux")]
            if let Some(t) = &self.tray {
                t.retranslate();
            }
        }
        let web_changed = self.config.web != self.applied.web;
        self.applied = self.config.clone();
        if web_changed {
            self.apply_web();
        }
    }

    /// Starts, restarts or stops the web server to match the settings.
    fn apply_web(&mut self) {
        self.web = None;
        self.web_error = None;
        if !self.config.web.enabled {
            return;
        }
        match web::start(&self.config.web, self.web_ctx.clone()) {
            Ok(server) => {
                self.journal
                    .info(trf("Web server started at {}", &[&server.addr]));
                self.web = Some(server);
            }
            Err(e) => {
                self.journal.warn(trf(
                    "Cannot start the web server on port {}: {}",
                    &[&self.config.web.port, &e],
                ));
                self.web_error = Some(e);
            }
        }
    }

    /// Whether the window can go to the tray right now.
    pub fn tray_available(&self) -> bool {
        #[cfg(target_os = "linux")]
        {
            self.tray.as_ref().is_some_and(|t| t.is_online())
        }
        #[cfg(not(target_os = "linux"))]
        {
            self.tray_created
        }
    }

    pub fn quit_needs_confirmation(&self) -> bool {
        self.config.confirm_quit && self.engine.status().timer_left_running().is_some()
    }

    pub(crate) fn flash(&mut self, ok: bool, text: impl Into<String>) {
        self.ui.flash = Some((
            Instant::now(),
            Notice {
                ok,
                text: text.into(),
            },
        ));
    }
}

pub struct WdtApp {
    shared: Rc<RefCell<Shared>>,
    #[cfg(not(target_os = "linux"))]
    tray: Option<crate::tray::Tray>,
    visible: bool,
    /// The close in progress hides the window to the tray.
    closing_to_tray: bool,
    theme: Option<Theme>,
    on_top: bool,
    drawer_size: bool,
    #[cfg(not(target_os = "linux"))]
    language: Option<Language>,
}

impl WdtApp {
    pub fn new(cc: &eframe::CreationContext<'_>, shared: Rc<RefCell<Shared>>) -> Self {
        style::install(&cc.egui_ctx);
        let mut sh = shared.borrow_mut();
        sh.repaint.set(Some(cc.egui_ctx.clone()));
        if sh.ui.autostart.is_none() {
            sh.ui.autostart = Some(sysint::autostart_enabled());
        }
        #[cfg(not(target_os = "linux"))]
        let tray = {
            let sender = sh.gui_tx.clone();
            let tray = crate::tray::Tray::new(std::sync::Arc::new(move |action| {
                sender.send(GuiRequest::Tray(action));
            }));
            sh.tray_created = tray.is_some();
            tray
        };
        // Linux skips the first window instead (see main); a hidden start on
        // Windows/macOS creates the window invisible. Without a tray it must
        // show up.
        let visible = !(sh.start_hidden && sh.mode == WindowMode::HideInPlace);
        sh.start_hidden = false;
        if !visible && !sh.tray_available() {
            cc.egui_ctx
                .send_viewport_cmd(ViewportCommand::Visible(true));
        }
        let visible = visible || !sh.tray_available();
        #[cfg(target_os = "linux")]
        if let Some(t) = &sh.tray {
            t.set_window_visible(true);
        }
        let (on_top, drawer_size) = (sh.config.always_on_top, sh.ui.drawer);
        #[cfg(debug_assertions)]
        {
            match std::env::var("WDTMON_CONFIRM").as_deref() {
                Ok("reset") => sh.ui.confirm = Some(Confirm::Test(Command::TestReset)),
                Ok("quit") => sh.ui.confirm = Some(Confirm::Quit),
                _ => {}
            }
        }
        drop(sh);
        Self {
            shared,
            #[cfg(not(target_os = "linux"))]
            tray,
            visible,
            closing_to_tray: false,
            theme: None,
            on_top,
            drawer_size,
            #[cfg(not(target_os = "linux"))]
            language: None,
        }
    }

    fn set_visible(&mut self, ctx: &egui::Context, sh: &mut Shared, visible: bool) {
        if visible {
            if !self.visible {
                ctx.send_viewport_cmd(ViewportCommand::Visible(true));
                ctx.send_viewport_cmd(ViewportCommand::Minimized(false));
            }
            ctx.send_viewport_cmd(ViewportCommand::Focus);
            self.visible = true;
        } else if sh.tray_available() {
            remember_position(ctx, sh);
            match sh.mode {
                WindowMode::ReturnOnHide => {
                    self.closing_to_tray = true;
                    ctx.send_viewport_cmd(ViewportCommand::Close);
                }
                WindowMode::HideInPlace => {
                    ctx.send_viewport_cmd(ViewportCommand::Visible(false));
                    self.visible = false;
                }
            }
        }
        #[cfg(not(target_os = "linux"))]
        if let Some(t) = &self.tray {
            t.set_window_visible(self.visible);
        }
        #[cfg(target_os = "linux")]
        if let Some(t) = &sh.tray {
            t.set_window_visible(visible);
        }
    }

    fn handle_action(&mut self, ctx: &egui::Context, sh: &mut Shared, action: TrayAction) {
        match action {
            TrayAction::Toggle => {
                let show = !self.visible;
                self.set_visible(ctx, sh, show);
            }
            TrayAction::Show => self.set_visible(ctx, sh, true),
            TrayAction::Hide => self.set_visible(ctx, sh, false),
            TrayAction::TestReset | TrayAction::TestPowerCycle => {
                self.set_visible(ctx, sh, true);
                if sh.engine.status().has_tests() {
                    let command = if action == TrayAction::TestReset {
                        Command::TestReset
                    } else {
                        Command::TestPowerCycle
                    };
                    sh.ui.confirm = Some(Confirm::Test(command));
                } else {
                    sh.flash(false, tr("The watchdog is not connected"));
                }
            }
            TrayAction::Quit => self.request_quit(ctx, sh),
        }
    }

    fn request_quit(&mut self, ctx: &egui::Context, sh: &mut Shared) {
        if sh.quit_needs_confirmation() {
            self.set_visible(ctx, sh, true);
            sh.ui.confirm = Some(Confirm::Quit);
        } else {
            self.quit(ctx, sh, false);
        }
    }

    fn quit(&mut self, ctx: &egui::Context, sh: &mut Shared, suspend: bool) {
        remember_position(ctx, sh);
        sh.quit = Some(suspend);
        ctx.send_viewport_cmd(ViewportCommand::Close);
    }

    fn sync_window(&mut self, ctx: &egui::Context, sh: &Shared) {
        if self.theme != Some(sh.config.theme) {
            self.theme = Some(sh.config.theme);
            ctx.set_theme(match sh.config.theme {
                Theme::System => egui::ThemePreference::System,
                Theme::Light => egui::ThemePreference::Light,
                Theme::Dark => egui::ThemePreference::Dark,
            });
        }
        if self.on_top != sh.config.always_on_top {
            self.on_top = sh.config.always_on_top;
            ctx.send_viewport_cmd(ViewportCommand::WindowLevel(if self.on_top {
                WindowLevel::AlwaysOnTop
            } else {
                WindowLevel::Normal
            }));
        }
        if self.drawer_size != sh.ui.drawer {
            self.drawer_size = sh.ui.drawer;
            // The size is pinned by equal min/max; widen the bound on the
            // side the window grows to before resizing.
            let size = window_size(sh.ui.drawer);
            let commands = if sh.ui.drawer {
                [
                    ViewportCommand::MaxInnerSize(size),
                    ViewportCommand::InnerSize(size),
                    ViewportCommand::MinInnerSize(size),
                ]
            } else {
                [
                    ViewportCommand::MinInnerSize(size),
                    ViewportCommand::InnerSize(size),
                    ViewportCommand::MaxInnerSize(size),
                ]
            };
            for c in commands {
                ctx.send_viewport_cmd(c);
            }
        }
    }
}

fn remember_position(ctx: &egui::Context, sh: &mut Shared) {
    if let Some(rect) = ctx.input(|i| i.viewport().outer_rect) {
        sh.config.position = Some([rect.min.x, rect.min.y]);
        sh.commit_config();
    }
}

impl eframe::App for WdtApp {
    fn logic(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        let shared = self.shared.clone();
        let mut sh = shared.borrow_mut();
        while let Some(action) = sh.pending.pop_front() {
            self.handle_action(ctx, &mut sh, action);
        }
        while let Ok(request) = sh.gui_rx.try_recv() {
            match request {
                GuiRequest::Tray(action) => self.handle_action(ctx, &mut sh, action),
                GuiRequest::Show => self.set_visible(ctx, &mut sh, true),
                #[allow(unused_variables)]
                GuiRequest::Health(health) =>
                {
                    #[cfg(not(target_os = "linux"))]
                    if let Some(t) = &mut self.tray {
                        t.set_health(health);
                    }
                }
            }
        }

        if ctx.input_mut(|i| i.consume_key(egui::Modifiers::COMMAND, egui::Key::Q)) {
            self.request_quit(ctx, &mut sh);
        }
        if ctx.input_mut(|i| i.consume_key(egui::Modifiers::NONE, egui::Key::Escape)) {
            if sh.ui.confirm.is_some() {
                sh.ui.confirm = None;
            } else {
                sh.ui.drawer = false;
            }
        }
        if ctx.input(|i| i.viewport().close_requested()) {
            if sh.quit.is_some() || self.closing_to_tray {
                // Let it close.
            } else if sh.tray_available() {
                ctx.send_viewport_cmd(ViewportCommand::CancelClose);
                self.set_visible(ctx, &mut sh, false);
            } else {
                ctx.send_viewport_cmd(ViewportCommand::CancelClose);
                self.request_quit(ctx, &mut sh);
            }
        }

        // Typed values apply when the edit ends.
        if !(ctx.egui_wants_keyboard_input() || ctx.input(|i| i.pointer.any_down())) {
            sh.commit_config();
        }
        self.sync_window(ctx, &sh);
        #[cfg(not(target_os = "linux"))]
        if self.language != Some(sh.config.language) {
            if self.language.is_some()
                && let Some(t) = &self.tray
            {
                t.retranslate();
            }
            self.language = Some(sh.config.language);
        }
        if let Some(notice) = sh.engine.take_notices().pop() {
            sh.ui.flash = Some((Instant::now(), notice));
        }
        if sh
            .ui
            .flash
            .as_ref()
            .is_some_and(|(t, _)| t.elapsed() >= FLASH_TIME)
        {
            sh.ui.flash = None;
        }
        // Keeps "N s ago" current.
        ctx.request_repaint_after(Duration::from_secs(1));
    }

    fn ui(&mut self, ui: &mut Ui, _frame: &mut eframe::Frame) {
        #[cfg(debug_assertions)]
        {
            debug_screenshot(ui.ctx());
            // Opens the drawer once the window is up, as a click would.
            if std::env::var_os("WDTMON_DRAWER").is_some()
                && ui.input(|i| i.time) > 1.0
                && !self.drawer_size
            {
                self.shared.borrow_mut().ui.drawer = true;
            }
        }
        let shared = self.shared.clone();
        let mut sh = shared.borrow_mut();
        let st = sh.engine.status().clone();
        let p = pal(ui);
        let full = ui.max_rect();
        ui.painter().rect_filled(full, 0.0, p.base);

        let strip = Rect::from_min_size(full.min, Vec2::new(full.width(), STRIP_HEIGHT));
        ui.scope_builder(UiBuilder::new().max_rect(strip).id_salt("strip"), |ui| {
            self.strip(ui, &mut sh, &st);
        });
        if sh.ui.drawer && full.height() > STRIP_HEIGHT + 20.0 {
            let area = Rect::from_min_max(Pos2::new(full.left(), strip.bottom()), full.max);
            ui.painter().rect_filled(area, 0.0, p.panel);
            ui.painter()
                .hline(area.x_range(), area.top(), Stroke::new(1.0, p.line));
            let inner = area.shrink2(Vec2::new(0.0, 1.0));
            ui.scope_builder(UiBuilder::new().max_rect(inner).id_salt("drawer"), |ui| {
                let scroll = egui::ScrollArea::vertical().auto_shrink(false);
                #[cfg(debug_assertions)]
                let scroll = match std::env::var("WDTMON_SCROLL")
                    .ok()
                    .and_then(|v| v.parse().ok())
                {
                    Some(offset) => scroll.vertical_scroll_offset(offset),
                    None => scroll,
                };
                scroll.show(ui, |ui| {
                    egui::Frame::new()
                        .inner_margin(egui::Margin {
                            left: 18,
                            right: 18,
                            top: 8,
                            bottom: 14,
                        })
                        .show(ui, |ui| {
                            ui.spacing_mut().item_spacing.y = 2.0;
                            drawer(ui, &mut sh, &st);
                        });
                });
            });
        }
        ui.painter()
            .rect_stroke(full, 0.0, Stroke::new(1.0, p.line), StrokeKind::Inside);

        if let Some(confirm) = sh.ui.confirm
            && !confirm_valid(confirm, &st)
        {
            // The device went away or changed under the question.
            sh.ui.confirm = None;
        }
    }
}

/// Debug builds: `WDTMON_SCREENSHOT=file.png` saves the window after
/// a few seconds and quits.
#[cfg(debug_assertions)]
fn debug_screenshot(ctx: &egui::Context) {
    use std::sync::atomic::{AtomicBool, Ordering};
    static REQUESTED: AtomicBool = AtomicBool::new(false);
    let Some(path) = std::env::var_os("WDTMON_SCREENSHOT") else {
        return;
    };
    let delay: f64 = std::env::var("WDTMON_SCREENSHOT_DELAY")
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

fn confirm_valid(confirm: Confirm, st: &Status) -> bool {
    match confirm {
        Confirm::Test(_) => st.has_tests(),
        Confirm::Quit => true,
    }
}

// ---- the strip -------------------------------------------------------------

/// How the current state is shown.
struct Look {
    tone: Tone,
    glyph: &'static str,
    spin: bool,
    pulse: Option<f32>,
    text: String,
}

fn look(st: &Status, sh: &Shared) -> Look {
    let pulse = st.last_beat.map(|t| t.elapsed().as_secs_f32());
    let (tone, glyph, spin, text) = match &st.link {
        Link::Connected if st.paused => (
            Tone::Caution,
            glyph::PAUSE,
            false,
            tr("Timer suspended: the PC will not be restarted").to_string(),
        ),
        Link::Connected => match st.last_beat {
            Some(t) => {
                let secs = t.elapsed().as_secs();
                let text = if secs == 0 {
                    tr("Heartbeat sent just now").to_string()
                } else {
                    trf("Heartbeat sent {} s ago", &[&secs])
                };
                (Tone::Good, "", false, text)
            }
            None => (Tone::Idle, "", true, tr("Connecting…").to_string()),
        },
        Link::Searching => (
            Tone::Idle,
            "",
            true,
            tr("Looking for the watchdog…").to_string(),
        ),
        Link::NoDevice => (
            Tone::Alert,
            glyph::PLUGS,
            false,
            tr("No watchdog connected").to_string(),
        ),
        Link::Missing(serial) => (
            Tone::Alert,
            glyph::PLUGS,
            false,
            trf("Watchdog {} is not connected", &[serial]),
        ),
        Link::AccessDenied => (
            Tone::Alert,
            glyph::LOCK_KEY,
            false,
            tr("No access to the watchdog").to_string(),
        ),
        Link::Failed(e) => (
            Tone::Alert,
            glyph::WARNING,
            false,
            trf("Device error: {}", &[e]),
        ),
        Link::Unsupported(v) => (
            Tone::Caution,
            glyph::QUESTION,
            false,
            trf("Unsupported firmware {}", &[v]),
        ),
    };
    let text = match &sh.ui.flash {
        Some((_, notice)) => notice.text.clone(),
        None => text,
    };
    // Only a live heartbeat pulses; the disc stays empty then.
    let pulse = pulse.filter(|_| matches!(st.link, Link::Connected) && !st.paused);
    Look {
        tone,
        glyph,
        spin,
        pulse,
        text,
    }
}

impl WdtApp {
    fn toggle_drawer(&mut self, sh: &mut Shared) {
        sh.ui.drawer = !sh.ui.drawer;
    }

    fn strip(&mut self, ui: &mut Ui, sh: &mut Shared, st: &Status) {
        let ctx = ui.ctx().clone();
        let full = ui.max_rect();
        // The empty parts of the strip move the window.
        let background = ui.interact(
            full,
            ui.scope_id().with("strip-drag"),
            Sense::click_and_drag(),
        );
        if background.drag_started_by(egui::PointerButton::Primary) {
            ctx.send_viewport_cmd(ViewportCommand::StartDrag);
        }
        if background.double_clicked() {
            self.toggle_drawer(sh);
        }

        let look = look(st, sh);
        let disc = Rect::from_center_size(
            Pos2::new(full.left() + 38.0, full.center().y),
            Vec2::splat(52.0),
        );
        let (tone, glyph) = match sh.ui.confirm {
            Some(_) => (Tone::Caution, glyph::WARNING),
            None => (look.tone, look.glyph),
        };
        style::disc(
            ui,
            disc,
            tone,
            glyph,
            look.pulse.filter(|_| sh.ui.confirm.is_none()),
            look.spin && sh.ui.confirm.is_none(),
        );

        let corner = Rect::from_min_max(
            Pos2::new(full.right() - 64.0, full.top() + 8.0),
            Pos2::new(full.right() - 8.0, full.top() + 30.0),
        );
        ui.scope_builder(
            UiBuilder::new()
                .max_rect(corner)
                .layout(Layout::right_to_left(Align::Center)),
            |ui| {
                ui.spacing_mut().item_spacing.x = 4.0;
                let to_tray = sh.tray_available();
                let tip = if to_tray {
                    tr("Hide to the tray")
                } else {
                    tr("Quit")
                };
                if style::corner(ui, glyph::X, tip, false).clicked() {
                    if to_tray {
                        self.set_visible(&ctx, sh, false);
                    } else {
                        self.request_quit(&ctx, sh);
                    }
                }
                let gear = if sh.ui.drawer {
                    glyph::CARET_UP
                } else {
                    glyph::GEAR_SIX
                };
                if style::corner(ui, gear, tr("Settings"), sh.ui.drawer).clicked() {
                    self.toggle_drawer(sh);
                }
            },
        );

        let info = Rect::from_min_max(
            Pos2::new(disc.right() + 8.0, full.top() + 12.0),
            Pos2::new(full.right() - 14.0, full.bottom() - 12.0),
        );
        match sh.ui.confirm {
            Some(confirm) => self.confirm_view(ui, info, sh, st, confirm),
            None => self.info_view(ui, info, sh, st, &look),
        }
    }

    fn info_view(&mut self, ui: &mut Ui, info: Rect, sh: &mut Shared, st: &Status, look: &Look) {
        let p = pal(ui);
        let title_rect = Rect::from_min_size(info.min, Vec2::new(info.width() - 64.0, 22.0));
        ui.scope_builder(
            UiBuilder::new()
                .max_rect(title_rect)
                .layout(Layout::left_to_right(Align::Center)),
            |ui| {
                ui.spacing_mut().item_spacing.x = 8.0;
                let firmware = st.firmware.as_ref().filter(|_| st.connected());
                let title = match firmware.map(|f| f.model) {
                    Some(Model::Pro2) => "Watchdog Pro2",
                    Some(Model::Lite) => "Watchdog Lite",
                    _ => "USB Watchdog",
                };
                ui.add(egui::Label::new(strong(title).size(15.5).color(p.text)).selectable(false));
                if let Some(f) = firmware {
                    let mut detail = f.label();
                    if !f.build.is_empty() {
                        detail.push_str(" · ");
                        detail.push_str(&f.build);
                    }
                    if st.devices.len() > 1
                        && let Some(serial) = &st.serial
                    {
                        detail.push_str(" · #");
                        let tail = serial.chars().count().saturating_sub(4);
                        detail.extend(serial.chars().skip(tail));
                    }
                    ui.add(
                        egui::Label::new(RichText::new(detail).size(11.5).color(p.faint))
                            .truncate()
                            .selectable(false),
                    )
                    .on_hover_text(st.serial.clone().unwrap_or_default());
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
                // The device timeout on the right, the state on the left.
                if let Some(s) = st.settings.filter(|_| st.connected()) {
                    let (text, color, hint) = if s.timer_armed() {
                        (
                            format!("{}  {}", glyph::TIMER, trf("{} min", &[&s.reboot_min])),
                            p.sub,
                            tr("The watchdog acts after this long without heartbeats"),
                        )
                    } else {
                        (
                            format!("{}  {}", glyph::TIMER, tr("Timer off")),
                            p.caution,
                            tr("The device settings leave the watchdog unable to restart the PC"),
                        )
                    };
                    ui.add(
                        egui::Label::new(RichText::new(text).size(12.0).color(color))
                            .selectable(false),
                    )
                    .on_hover_text(hint);
                }
                ui.with_layout(Layout::left_to_right(Align::Center), |ui| {
                    let color = match &sh.ui.flash {
                        Some((_, n)) => p.tone(if n.ok { Tone::Good } else { Tone::Alert }),
                        None => p.tone(look.tone),
                    };
                    ui.add(
                        egui::Label::new(RichText::new(&look.text).size(12.5).color(color))
                            .truncate()
                            .selectable(false),
                    );
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
                self.tools(ui, sh, st);
                ui.add_space(4.0);
                ui.with_layout(Layout::left_to_right(Align::Center), |ui| {
                    ui.set_clip_rect(ui.max_rect());
                    ui.spacing_mut().item_spacing.x = 12.0;
                    tags(ui, sh, st);
                });
            },
        );
    }

    fn tools(&mut self, ui: &mut Ui, sh: &mut Shared, st: &Status) {
        // Right to left: the PC actions, then the LED.
        if st.model() != Some(Model::Lite) {
            ui.add_enabled_ui(st.has_tests(), |ui| {
                ui.spacing_mut().item_spacing.x = 6.0;
                let mut ask = None;
                if tool(
                    ui,
                    glyph::POWER,
                    tr("Shut down the PC"),
                    false,
                    Some(Tone::Alert),
                )
                .clicked()
                {
                    ask = Some(Command::TestShutdown);
                }
                if tool(
                    ui,
                    glyph::LIGHTNING,
                    tr("Hard reset: power-cycle the PC"),
                    false,
                    Some(Tone::Alert),
                )
                .clicked()
                {
                    ask = Some(Command::TestPowerCycle);
                }
                if tool(
                    ui,
                    glyph::ARROW_COUNTER_CLOCKWISE,
                    tr("Reset the PC"),
                    false,
                    Some(Tone::Caution),
                )
                .clicked()
                {
                    ask = Some(Command::TestReset);
                }
                if let Some(c) = ask {
                    sh.ui.confirm = Some(Confirm::Test(c));
                }
            });
        }
        let on = sh.config.led_on;
        let tip = if on {
            tr("Green LED blinks on each heartbeat")
        } else {
            tr("Green LED is off")
        };
        if tool(ui, glyph::LIGHTBULB, tip, on, None).clicked() {
            sh.config.led_on = !on;
        }
    }

    fn confirm_view(
        &mut self,
        ui: &mut Ui,
        info: Rect,
        sh: &mut Shared,
        st: &Status,
        confirm: Confirm,
    ) {
        let p = pal(ui);
        let ctx = ui.ctx().clone();
        let (title, text, action) = match confirm {
            Confirm::Test(Command::TestReset) => (
                tr("Reset the PC now?"),
                tr("The watchdog presses the reset button of this PC."),
                tr("Reset"),
            ),
            Confirm::Test(Command::TestPowerCycle) => (
                tr("Power-cycle the PC now?"),
                tr("The power is cut and restored; unsaved work is lost."),
                tr("Hard reset"),
            ),
            Confirm::Test(Command::TestShutdown) => (
                tr("Shut down the PC now?"),
                tr("The watchdog holds the power button until the PC turns off."),
                tr("Shut down"),
            ),
            Confirm::Test(_) => (
                tr("Run the self-test?"),
                tr("LEDs blink, CH1 and CH2 pulse for 200 ms: wired channels act on the PC."),
                tr("Run"),
            ),
            Confirm::Quit => (tr("Quit while the timer is running?"), "", ""),
        };
        let text = match confirm {
            Confirm::Quit => match st.timer_left_running() {
                Some(m) if m > 0 => trf(
                    "Without heartbeats the watchdog restarts this PC in {} min.",
                    &[&m],
                ),
                _ => tr("Without heartbeats the watchdog restarts this PC.").to_string(),
            },
            _ => text.to_string(),
        };
        let title_rect = Rect::from_min_size(info.min, Vec2::new(info.width() - 64.0, 22.0));
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
                    egui::Label::new(RichText::new(&text).size(12.5).color(p.sub))
                        .truncate()
                        .selectable(false),
                );
            },
        );
        let buttons = Rect::from_min_max(Pos2::new(info.left(), info.bottom() - 28.0), info.max);
        ui.scope_builder(
            UiBuilder::new()
                .max_rect(buttons)
                .layout(Layout::left_to_right(Align::Center)),
            |ui| {
                match confirm {
                    Confirm::Test(command) => {
                        if style::button(ui, Kind::Danger, action).clicked() {
                            sh.engine.send(Request::Command(command));
                            sh.ui.confirm = None;
                        }
                    }
                    Confirm::Quit => {
                        if style::button(ui, Kind::Primary, tr("Suspend the timer and quit"))
                            .clicked()
                        {
                            sh.ui.confirm = None;
                            // Done here so a failure is seen; the engine
                            // repeats it on the way out.
                            if sh.engine.suspend() {
                                self.quit(&ctx, sh, true);
                            } else {
                                sh.flash(false, tr("The watchdog timer could not be suspended"));
                            }
                        }
                        if style::button(ui, Kind::Plain, tr("Quit anyway")).clicked() {
                            sh.ui.confirm = None;
                            self.quit(&ctx, sh, false);
                        }
                    }
                }
                if style::button(ui, Kind::Plain, tr("Cancel")).clicked() {
                    sh.ui.confirm = None;
                }
            },
        );
    }
}

fn channel_tag(ui: &mut Ui, n: u8, level: u8) {
    let p = pal(ui);
    let closed = level == 0;
    let text = if closed {
        trf("CH{} closed", &[&n])
    } else {
        trf("CH{} open", &[&n])
    };
    style::tag(ui, if closed { p.brand } else { p.idle }, &text);
}

fn tags(ui: &mut Ui, sh: &mut Shared, st: &Status) {
    let p = pal(ui);
    if !st.connected() {
        let hint = match &st.link {
            Link::AccessDenied => {
                if ui
                    .link(RichText::new(tr("How to fix")).size(12.0))
                    .clicked()
                {
                    sh.ui.drawer = true;
                }
                return;
            }
            Link::NoDevice | Link::Missing(_) => tr("Plug the watchdog into a USB port"),
            Link::Failed(_) => tr("Retrying…"),
            _ => "",
        };
        style::sub(ui, hint);
        return;
    }
    let Some(data) = st.data else {
        style::sub(ui, tr("Reading the device state…"));
        return;
    };
    channel_tag(ui, 1, data.ch1);
    if let Some(ch2) = data.ch2 {
        channel_tag(ui, 2, ch2);
        match data.input {
            InState::Temperature(Some(t)) => {
                style::tag(ui, p.brand, &format!("{} °C", i18n::number(t, 1)));
            }
            InState::Temperature(None) => {
                style::tag(ui, p.caution, tr("Sensor n/a"))
                    .on_hover_text(tr("The DS18B20 sensor does not answer"));
            }
            InState::Input(high) => {
                style::tag(
                    ui,
                    if high { p.brand } else { p.idle },
                    if high { tr("IN high") } else { tr("IN low") },
                );
            }
            InState::Output(high) => {
                style::tag(
                    ui,
                    if high { p.brand } else { p.idle },
                    if high { tr("OUT high") } else { tr("OUT low") },
                );
            }
            InState::Off => {}
        }
    }
    if let Some(d) = data.diagnostics.filter(|d| d.any_fault()) {
        style::tag(ui, p.alert, tr("Device fault")).on_hover_text(fault_text(&d));
    }
}

fn fault_text(d: &crate::hid::Diagnostics) -> String {
    let mut lines = Vec::new();
    if d.adc_fault {
        lines.push(tr("ADC fault: supply and MCU temperature are not measured").to_string());
    }
    if d.settings_fault {
        lines.push(tr("Settings could not be saved to flash").to_string());
    }
    if d.odometer_fault {
        lines.push(tr("The operating time counter is damaged").to_string());
    }
    lines.join("\n")
}

// ---- the drawer ------------------------------------------------------------

fn channel_mode(m: ChannelMode) -> &'static str {
    match m {
        ChannelMode::Off => tr("off"),
        ChannelMode::Reset => tr("reset"),
        ChannelMode::Power => tr("power"),
        ChannelMode::OutputOpen => tr("output, open"),
        ChannelMode::OutputClosed => tr("output, closed"),
    }
}

fn in_mode(m: InMode) -> &'static str {
    match m {
        InMode::Off => tr("off"),
        InMode::Input => tr("input"),
        InMode::Output => tr("output"),
        InMode::Temperature => tr("DS18B20 sensor"),
    }
}

/// One line of a two-column [`egui::Grid`].
fn fact(ui: &mut Ui, label: &str, value: &str) {
    let p = pal(ui);
    ui.label(RichText::new(label).size(12.0).color(p.sub));
    ui.add(egui::Label::new(RichText::new(value).size(12.0).color(p.text)).wrap());
    ui.end_row();
}

fn facts(ui: &mut Ui, st: &Status, f: &crate::hid::Firmware) {
    fact(ui, tr("Serial number"), st.serial.as_deref().unwrap_or(""));
    fact(ui, tr("Firmware"), &f.raw);
    if let Some(d) = st.data.and_then(|d| d.diagnostics) {
        let text = if d.any_fault() {
            fault_text(&d)
        } else if d.adc_faults > 0 {
            trf("OK, {} ADC errors recovered", &[&d.adc_faults])
        } else {
            tr("OK").to_string()
        };
        fact(ui, tr("Self-check"), &text);
    }
}

fn drawer(ui: &mut Ui, sh: &mut Shared, st: &Status) {
    let p = pal(ui);
    let ctx = ui.ctx().clone();

    if st.link == Link::AccessDenied {
        ui.add_space(6.0);
        access_help(ui, sh);
    }

    heading(ui, tr("Watchdog"));
    row(ui, glyph::USB, tr("Device"), |ui| {
        let current = match &sh.config.serial {
            None => tr("Auto: the first one found").to_string(),
            Some(s) => s.clone(),
        };
        egui::ComboBox::from_id_salt("serial")
            .width(210.0)
            .selected_text(current)
            .show_ui(ui, |ui| {
                ui.selectable_value(&mut sh.config.serial, None, tr("Auto: the first one found"));
                for s in &st.devices {
                    ui.selectable_value(&mut sh.config.serial, Some(s.clone()), s);
                }
                if let Some(s) = sh.config.serial.clone()
                    && !st.devices.contains(&s)
                {
                    ui.selectable_value(
                        &mut sh.config.serial,
                        Some(s.clone()),
                        format!("{s} ({})", tr("not connected")),
                    );
                }
            });
        if style::corner(ui, glyph::ARROWS_CLOCKWISE, tr("Scan"), false).clicked() {
            sh.engine.send(Request::Rescan);
        }
    });

    if let Some(f) = st.firmware.as_ref().filter(|_| st.connected()) {
        ui.add_space(2.0);
        egui::Frame::new()
            .inner_margin(egui::Margin {
                left: 23,
                right: 0,
                top: 0,
                bottom: 4,
            })
            .show(ui, |ui| {
                egui::Grid::new("facts")
                    .num_columns(2)
                    .spacing([14.0, 3.0])
                    .show(ui, |ui| facts(ui, st, f));
            });
    }

    row(
        ui,
        glyph::LIGHTBULB,
        tr("Blink the green LED on heartbeats"),
        |ui| toggle(ui, &mut sh.config.led_on),
    );
    if st.model() != Some(Model::Lite) {
        row(ui, glyph::PAUSE, tr("Suspend the watchdog timer"), |ui| {
            toggle(ui, &mut sh.config.timer_paused)
        });
        if sh.config.timer_paused {
            style::note(ui, Tone::Caution, glyph::WARNING, |ui| {
                ui.label(tr(
                    "While the timer is suspended, the watchdog does not restart a hung PC.",
                ));
            });
            ui.add_space(4.0);
        }
        ui.add_enabled_ui(st.has_tests(), |ui| {
            row(ui, glyph::FLASK, tr("Self-test"), |ui| {
                if style::button(ui, Kind::Plain, tr("Run…")).clicked() {
                    sh.ui.confirm = Some(Confirm::Test(Command::SelfTest));
                }
            });
        });
    }

    device_settings(ui, sh, st);

    ui.add_space(6.0);
    heading(ui, tr("Web server"));
    row(
        ui,
        glyph::GLOBE_SIMPLE,
        tr("Status page and JSON API"),
        |ui| toggle(ui, &mut sh.config.web.enabled),
    );
    row(ui, "", tr("Port"), |ui| {
        ui.add(
            egui::DragValue::new(&mut sh.config.web.port)
                .range(1024..=65535)
                .speed(1.0),
        );
    });
    if let Some(server) = &sh.web {
        let url = format!("http://localhost:{}", server.addr.port());
        ui.horizontal(|ui| {
            ui.add_space(23.0);
            if ui
                .link(RichText::new(&url).size(12.0))
                .on_hover_text(tr("Open in the browser"))
                .clicked()
            {
                let _ = sysint::open(&url);
            }
            if sh.config.web.remote_access {
                ui.label(
                    RichText::new(tr("also open to the network"))
                        .size(12.0)
                        .color(p.caution),
                );
            }
        });
    }
    if let Some(e) = &sh.web_error {
        style::note(ui, Tone::Alert, glyph::WARNING, |ui| {
            ui.label(trf("The server cannot start: {}", &[e]));
        });
    }

    ui.add_space(6.0);
    heading(ui, tr("Log"));
    row(ui, glyph::FILE_TEXT, tr("Write events to a file"), |ui| {
        toggle(ui, &mut sh.config.logging.enabled)
    });
    row(ui, "", tr("File size limit, MB"), |ui| {
        ui.add(egui::DragValue::new(&mut sh.config.logging.max_size_mb).range(1..=1024));
    });
    row(ui, "", tr("Old files to keep"), |ui| {
        ui.add(egui::DragValue::new(&mut sh.config.logging.backups).range(1..=16));
    });
    ui.horizontal(|ui| {
        ui.add_space(23.0);
        let log = sh.journal.path();
        if style::button(ui, Kind::Plain, tr("Open the log")).clicked() {
            if log.is_file() {
                if let Err(e) = sysint::open(&log) {
                    sh.flash(false, e);
                }
            } else {
                sh.flash(false, tr("The log file does not exist yet"));
            }
        }
        if style::button(ui, Kind::Plain, tr("Open the folder")).clicked() {
            let dir = log.parent().map(|d| d.to_path_buf()).unwrap_or_default();
            let _ = std::fs::create_dir_all(&dir);
            if let Err(e) = sysint::open(&dir) {
                sh.flash(false, e);
            }
        }
    });

    ui.add_space(6.0);
    heading(ui, tr("Application"));
    row(ui, glyph::TRANSLATE, tr("Language"), |ui| {
        style::choice(
            ui,
            &mut sh.config.language,
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
            &mut sh.config.theme,
            &[
                (Theme::System, tr("System")),
                (Theme::Light, tr("Light")),
                (Theme::Dark, tr("Dark")),
            ],
        );
    });
    row(
        ui,
        glyph::ROCKET_LAUNCH,
        tr("Start with the system"),
        |ui| {
            let mut on = sh.ui.autostart.unwrap_or(false);
            if toggle(ui, &mut on).changed() {
                match sysint::set_autostart(on) {
                    Ok(()) => sh.ui.autostart = Some(on),
                    Err(e) => sh.flash(false, trf("Autostart: {}", &[&e])),
                }
            }
        },
    );
    row(ui, glyph::EYE_SLASH, tr("Start hidden in the tray"), |ui| {
        toggle(ui, &mut sh.config.start_hidden)
    });
    row(ui, glyph::PUSH_PIN, tr("Keep above other windows"), |ui| {
        toggle(ui, &mut sh.config.always_on_top)
    });
    row(ui, glyph::SIGN_OUT, tr("Ask before quitting"), |ui| {
        toggle(ui, &mut sh.config.confirm_quit)
    });

    ui.add_space(6.0);
    heading(ui, tr("About"));
    ui.add_space(2.0);
    ui.label(strong(format!("wdtmon-rs {APP_VERSION}")).color(p.text));
    style::sub(
        ui,
        tr(
            "Heartbeat monitor for USB Watchdog Pro2 and Lite over USB HID: no driver or serial port needed.",
        ),
    );
    style::sub(
        ui,
        trf(
            "PC uptime {} · monitor uptime {}",
            &[
                &sysint::format_duration(sysint::system_uptime()),
                &sysint::format_duration(sh.started.elapsed()),
            ],
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
    ui.add_space(8.0);
    if style::button(
        ui,
        Kind::Plain,
        &format!("{}  {}", glyph::SIGN_OUT, tr("Quit")),
    )
    .clicked()
    {
        ctx.send_viewport_cmd(ViewportCommand::Focus);
        sh.pending.push_back(TrayAction::Quit);
        ctx.request_repaint();
    }
}

/// `0` reads as `zero` (off, no limit), anything else through `unit`.
fn amount(zero: &'static str, unit: &'static str) -> impl Fn(u8) -> String {
    move |v| {
        if v == 0 {
            tr(zero).to_string()
        } else {
            trf(unit, &[&v])
        }
    }
}

/// Tenths of a second, `0,2 s` in Russian.
fn tenths(v: u8) -> String {
    if v == 0 {
        tr("off").to_string()
    } else {
        trf("{} s", &[&i18n::number(f32::from(v) / 10.0, 1)])
    }
}

fn channel_combo(ui: &mut Ui, id: &str, mode: &mut ChannelMode) {
    egui::ComboBox::from_id_salt(id)
        .width(150.0)
        .selected_text(channel_mode(*mode))
        .show_ui(ui, |ui| {
            for m in ChannelMode::ALL {
                ui.selectable_value(mode, m, channel_mode(m));
            }
        });
}

/// The Pro2's own settings, written over HID by firmware that takes them.
fn device_settings(ui: &mut Ui, sh: &mut Shared, st: &Status) {
    let (Some(device), Some(serial)) = (
        st.settings.filter(|_| st.model() == Some(Model::Pro2)),
        st.serial.clone(),
    ) else {
        sh.ui.draft = None;
        return;
    };
    // Edits survive a re-read of the device; untouched fields follow it, so
    // saving never reverts what another tool changed meanwhile.
    let (base, mut edit) = match sh.ui.draft.take() {
        Some(d) if d.serial == serial => (device, d.edit.rebased(&d.base, &device)),
        _ => (device, device),
    };
    let writable = st.writing != Writing::Unsupported;
    let saving = st.writing == Writing::Saving;

    ui.add_space(6.0);
    heading(ui, tr("Device settings"));
    if !writable {
        style::note(ui, Tone::Caution, glyph::INFO, |ui| {
            ui.label(tr("This firmware cannot change settings over USB HID. Update the watchdog firmware, or change them in wdtmon3 over the serial port."));
        });
        ui.add_space(4.0);
    }
    ui.add_enabled_ui(writable && !saving, |ui| {
        let e = &mut edit;
        row(ui, glyph::TIMER, tr("Restart after no heartbeat for"), |ui| {
            style::stepper(ui, &mut e.reboot_min, 0..=DIGIT_MAX, amount("off", "{} min"))
        });
        row(ui, glyph::PLUGS, tr("Channel 1"), |ui| channel_combo(ui, "ch1", &mut e.ch1));
        row(ui, "", tr("Channel 2"), |ui| channel_combo(ui, "ch2", &mut e.ch2));
        ui.add_enabled_ui(e.uses(ChannelMode::Reset), |ui| {
            row(ui, glyph::ARROW_COUNTER_CLOCKWISE, tr("Reset button press"), |ui| {
                style::stepper(ui, &mut e.reset_pulse, 0..=DIGIT_MAX, tenths)
            });
        });
        ui.add_enabled_ui(e.uses(ChannelMode::Power), |ui| {
            row(ui, glyph::LIGHTNING, tr("Power off: button hold"), |ui| {
                style::stepper(ui, &mut e.power_off_s, 0..=DIGIT_MAX, amount("off", "{} s"))
            });
            row(ui, "", tr("Pause before power on"), |ui| {
                style::stepper(ui, &mut e.power_delay_s, 0..=DIGIT_MAX, amount("off", "{} s"))
            });
            row(ui, "", tr("Power on: button press"), |ui| {
                style::stepper(ui, &mut e.power_on_pulse, 0..=DIGIT_MAX, tenths)
            });
        });
        style::row_tip(
            ui,
            glyph::REPEAT,
            tr("Restarts before turning the PC off"),
            Some(tr("Restarts in a row that bring no heartbeat back; then the watchdog turns the PC off")),
            |ui| style::stepper(ui, &mut e.reboot_limit, 0..=DIGIT_MAX, amount("unlimited", "{}")),
        );
        row(ui, glyph::SIGN_IN, tr("IN/TEMP channel"), |ui| {
            egui::ComboBox::from_id_salt("in")
                .width(150.0)
                .selected_text(in_mode(e.in_mode))
                .show_ui(ui, |ui| {
                    for m in InMode::ALL {
                        ui.selectable_value(&mut e.in_mode, m, in_mode(m));
                    }
                });
        });
        style::row_tip(
            ui,
            glyph::THERMOMETER_SIMPLE,
            tr("Temperature limit"),
            Some(tr("Reaching it acts like a missed heartbeat")),
            |ui| style::stepper(ui, &mut e.temp_limit, 0..=125, amount("off", "{} °C")),
        );
        ui.add_enabled_ui(e.temp_limit > 0, |ui| {
            row(ui, "", tr("Measured by"), |ui| {
                style::choice(
                    ui,
                    &mut e.internal_sensor,
                    &[(false, "DS18B20"), (true, tr("MCU sensor"))],
                );
            });
        });
    });

    let warning = if edit.reboot_min == 0 {
        Some(tr("The timer is off: the watchdog never restarts the PC."))
    } else if !edit.has_actor() {
        Some(tr(
            "Neither channel presses Reset or Power: the timer cannot restart the PC.",
        ))
    } else if edit.temp_limit > 0 && !edit.internal_sensor && edit.in_mode != InMode::Temperature {
        Some(tr(
            "The temperature limit needs the IN/TEMP channel in DS18B20 sensor mode.",
        ))
    } else if edit.temp_limit > 0 && edit.internal_sensor {
        Some(tr(
            "The MCU sensor reads the chip itself: within ±5 °C and warmer than the air, so leave a margin.",
        ))
    } else {
        None
    };
    if let Some(text) = warning {
        ui.add_space(4.0);
        style::note(ui, Tone::Caution, glyph::WARNING, |ui| {
            ui.label(text);
        });
    }

    let dirty = edit != base;
    let state = if saving {
        tr("Saving…")
    } else if dirty {
        tr("Not saved yet")
    } else {
        ""
    };
    ui.add_space(4.0);
    row(ui, "", state, |ui| {
        if ui
            .add_enabled_ui(writable && dirty && !saving, |ui| {
                style::button(ui, Kind::Primary, tr("Save to the watchdog"))
            })
            .inner
            .clicked()
        {
            sh.engine.send(Request::WriteSettings(edit));
        }
        if ui
            .add_enabled_ui(dirty && !saving, |ui| {
                style::button(ui, Kind::Plain, tr("Undo"))
            })
            .inner
            .clicked()
        {
            edit = base;
        }
        let defaults = Settings::default();
        if ui
            .add_enabled_ui(writable && !saving && edit != defaults, |ui| {
                style::button(ui, Kind::Plain, tr("Defaults"))
            })
            .inner
            .on_hover_text(tr(
                "The factory settings; nothing is written until you save",
            ))
            .clicked()
        {
            edit = defaults;
        }
    });
    if writable {
        ui.horizontal(|ui| {
            ui.add_space(23.0);
            sub_note(ui, tr("Saving restarts the device timer."));
        });
    }
    sh.ui.draft = Some(Draft { serial, base, edit });
}

fn sub_note(ui: &mut Ui, text: &str) {
    let color = pal(ui).faint;
    ui.add(egui::Label::new(RichText::new(text).size(11.5).color(color)).wrap());
}

fn access_help(ui: &mut Ui, sh: &mut Shared) {
    let ctx = ui.ctx().clone();
    style::note(ui, Tone::Alert, glyph::LOCK_KEY, |ui| {
        ui.label(strong(tr("No access to the watchdog")));
        if cfg!(target_os = "linux") {
            ui.label(tr("Linux lets only root open USB HID devices unless a udev rule allows it. Install the rule with this command, then re-plug the watchdog:"));
            let data_dir = sh.paths.data_dir.clone();
            let command = sh
                .ui
                .udev
                .get_or_insert_with(|| sysint::prepare_udev_rule(&data_dir))
                .clone();
            match command {
                Ok(command) => {
                    ui.add(egui::Label::new(RichText::new(&command).monospace().size(11.5)).wrap());
                    if style::button(
                        ui,
                        Kind::Primary,
                        &format!("{}  {}", glyph::COPY, tr("Copy the command")),
                    )
                    .clicked()
                    {
                        ctx.copy_text(command);
                        sh.flash(true, tr("The command is copied to the clipboard"));
                    }
                }
                Err(e) => {
                    ui.label(trf("Cannot prepare the rule file: {}", &[&e]));
                }
            }
        } else {
            ui.label(tr(
                "Another program may hold the device, or the system denies access to it.",
            ));
        }
    });
    ui.add_space(4.0);
}
