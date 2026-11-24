// No console window on Windows release builds.
#![cfg_attr(all(windows, not(debug_assertions)), windows_subsystem = "windows")]

mod app;
mod config;
#[cfg(any(test, debug_assertions))]
mod demo;
mod engine;
mod glyph;
mod hid;
mod i18n;
mod icons;
mod instance;
mod journal;
mod style;
mod sysint;
mod tray;
mod web;

use std::cell::RefCell;
use std::rc::Rc;

use app::{GuiRequest, GuiSender, Shared, WdtApp, WindowMode};
use config::{Language, Paths};
use engine::{EngineConfig, Repaint};
use journal::Journal;

struct Args {
    hidden: bool,
    serial: Option<String>,
    lang: Option<Language>,
    reset: bool,
}

fn usage() -> ! {
    println!(
        "wdtmon-rs {} — USB WatchDog monitor over HID\n\n\
         Usage: wdtmon-rs [options]\n\n\
         Options:\n  \
         --hidden, --minimized   start in the system tray\n  \
         --serial <number|auto>  serve this watchdog (\"auto\": the first found)\n  \
         --lang <system|en|ru>   interface language\n  \
         --reset                 forget all settings\n  \
         -h, --help              show this help\n  \
         -V, --version           show the version",
        app::APP_VERSION
    );
    std::process::exit(0)
}

fn parse_args() -> Args {
    let mut args = Args {
        hidden: false,
        serial: None,
        lang: None,
        reset: false,
    };
    let mut it = std::env::args().skip(1);
    while let Some(arg) = it.next() {
        let (name, inline) = match arg.split_once('=') {
            Some((n, v)) => (n.to_string(), Some(v.to_string())),
            None => (arg.clone(), None),
        };
        let value = |it: &mut dyn Iterator<Item = String>| {
            inline.clone().or_else(|| it.next()).unwrap_or_else(|| {
                eprintln!("{name} needs a value; see --help");
                std::process::exit(2)
            })
        };
        match name.as_str() {
            "--hidden" | "--minimized" => args.hidden = true,
            "--serial" => args.serial = Some(value(&mut it)),
            "--lang" | "-l" => {
                args.lang = Some(match value(&mut it).to_ascii_lowercase() {
                    l if l.starts_with("ru") => Language::Ru,
                    l if l.starts_with("en") => Language::En,
                    _ => Language::System,
                })
            }
            "--reset" | "-r" => args.reset = true,
            "-h" | "--help" => usage(),
            "-V" | "--version" => {
                println!("wdtmon-rs {}", app::APP_VERSION);
                std::process::exit(0)
            }
            other => {
                eprintln!("unknown option {other}; see --help");
                std::process::exit(2)
            }
        }
    }
    args
}

fn native_options(sh: &Shared, visible: bool) -> eframe::NativeOptions {
    let icon = icons::app_icon();
    let size = app::window_size(sh.ui.drawer);
    let mut viewport = eframe::egui::ViewportBuilder::default()
        .with_title(format!("wdtmon-rs {}", app::APP_VERSION))
        .with_app_id(config::APP_ID)
        .with_inner_size(size)
        .with_min_inner_size(size)
        .with_max_inner_size(size)
        .with_decorations(false)
        .with_visible(visible)
        .with_window_level(if sh.config.always_on_top {
            eframe::egui::WindowLevel::AlwaysOnTop
        } else {
            eframe::egui::WindowLevel::Normal
        })
        .with_icon(eframe::egui::IconData {
            rgba: icon.pixels,
            width: icon.size,
            height: icon.size,
        });
    if let Some([x, y]) = sh
        .config
        .position
        .filter(|p| p.iter().all(|v| v.abs() < 20_000.0))
    {
        viewport = viewport.with_position([x, y]);
    }
    eframe::NativeOptions {
        viewport,
        run_and_return: true,
        ..Default::default()
    }
}

fn main() {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("warn")).init();
    let args = parse_args();

    let listener = match instance::acquire() {
        instance::Instance::Primary(listener) => listener,
        instance::Instance::Secondary => return,
    };

    let paths = Paths::new();
    if args.reset {
        match std::fs::remove_file(&paths.config_file) {
            Ok(()) => eprintln!("wdtmon-rs: settings reset"),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => {}
            Err(e) => eprintln!("wdtmon-rs: {}: {e}", paths.config_file.display()),
        }
    }
    // A file that does not parse is kept aside, not overwritten with the
    // defaults saved below: `Some(where it went)`.
    let (mut config, broken) = match config::load(&paths.config_file) {
        Ok(config) => (config.unwrap_or_default(), None),
        Err(e) => {
            let file = paths.config_file.display();
            log::warn!("{file}: {e}");
            let aside = config::set_aside(&paths.config_file)
                .inspect_err(|e| log::warn!("{file}: {e}"))
                .ok();
            (config::Config::default(), Some(aside))
        }
    };
    if let Some(lang) = args.lang {
        config.language = lang;
    }
    if let Some(serial) = &args.serial {
        config.serial = (!serial.eq_ignore_ascii_case("auto")).then(|| serial.clone());
    }
    let _ = config::save(&paths.config_file, &config);
    i18n::set_language(config.language);

    let journal = Journal::new(paths.log_file.clone(), config.logging.clone());
    journal.info(i18n::tr("Application started"));
    let broken = broken.map(|aside| {
        let text = |name: &dyn std::fmt::Display| {
            i18n::trf(
                "Settings reset: the file could not be read and was kept as {}",
                &[name],
            )
        };
        match aside {
            Some(path) => {
                journal.warn(text(&path.display()));
                let name = path.file_name().unwrap_or_default().display().to_string();
                text(&name)
            }
            None => {
                let text = i18n::tr("Settings reset: the file could not be read");
                journal.warn(text);
                text.to_string()
            }
        }
    });

    let repaint = Repaint::default();
    let (gui_tx, gui_rx) = GuiSender::new(repaint.clone());
    {
        let gui_tx = gui_tx.clone();
        instance::listen(listener, move || gui_tx.send(GuiRequest::Show));
    }

    #[cfg(target_os = "linux")]
    let tray = {
        let sender = gui_tx.clone();
        tray::Tray::new(std::sync::Arc::new(move |action| {
            sender.send(GuiRequest::Tray(action))
        }))
        .map(std::sync::Arc::new)
    };

    let on_health: engine::HealthObserver = {
        #[cfg(target_os = "linux")]
        {
            let tray = tray.clone();
            Box::new(move |h| {
                if let Some(t) = &tray {
                    t.set_health(h);
                }
            })
        }
        #[cfg(not(target_os = "linux"))]
        {
            let sender = gui_tx.clone();
            Box::new(move |h| sender.send(GuiRequest::Health(h)))
        }
    };
    let bus: engine::BusFactory = Box::new(|| Box::new(hid::HidBus::new()));
    // Debug builds can run without hardware on the emulator.
    #[cfg(debug_assertions)]
    let bus: engine::BusFactory = if let Some(mode) = std::env::var_os("WDTMON_DEMO") {
        Box::new(move || {
            let bus = demo::FakeBus::pro2(&["3473365A3036", "205F33634B52"]);
            // `denied` shows the Linux permission problem.
            bus.state().denied = mode == "denied";
            Box::new(bus)
        })
    } else {
        bus
    };
    let engine = engine::spawn(
        bus,
        EngineConfig::from_config(&config),
        journal.clone(),
        repaint.clone(),
        on_health,
    );

    let mode = if cfg!(target_os = "linux") {
        WindowMode::ReturnOnHide
    } else {
        WindowMode::HideInPlace
    };
    let start_hidden = args.hidden || config.start_hidden;
    let shared = Rc::new(RefCell::new(Shared::new(
        config,
        paths,
        engine,
        journal.clone(),
        gui_rx,
        gui_tx,
        repaint.clone(),
        mode,
    )));
    {
        let mut sh = shared.borrow_mut();
        sh.start_hidden = start_hidden;
        if let Some(text) = broken {
            sh.flash(false, text);
        }
        #[cfg(target_os = "linux")]
        {
            sh.tray_created = tray.is_some();
            sh.tray = tray.clone();
        }
    }

    run_windows(&shared, start_hidden);

    let suspend = shared.borrow().quit.unwrap_or(false);
    shared.borrow().repaint.set(None);
    let mut sh = shared.borrow_mut();
    // The window already suspended the timer; this repeats it in case the
    // watchdog reconnected since, which resumes it.
    let suspended = sh.engine.shutdown(suspend);
    if suspend && !suspended {
        journal.warn(i18n::tr(
            "The watchdog timer could not be suspended before exit",
        ));
    }
    journal.info(i18n::tr("Application closed"));
    #[cfg(target_os = "linux")]
    {
        sh.tray = None;
        drop(tray);
    }
}

/// Shows the window, and on Linux opens it again from the tray as needed.
fn run_windows(shared: &Rc<RefCell<Shared>>, start_hidden: bool) {
    let mut first = true;
    loop {
        let (mode, tray) = {
            let sh = shared.borrow();
            (sh.mode, sh.tray_available())
        };
        let skip_window = first && start_hidden && tray && mode == WindowMode::ReturnOnHide;
        let invisible = first && start_hidden && mode == WindowMode::HideInPlace;
        first = false;
        if !skip_window {
            let app_shared = shared.clone();
            let options = native_options(&shared.borrow(), !invisible);
            let result = eframe::run_native(
                config::APP_ID,
                options,
                Box::new(move |cc| Ok(Box::new(WdtApp::new(cc, app_shared)))),
            );
            shared.borrow().repaint.set(None);
            if let Err(e) = result {
                log::error!("window: {e}");
                eprintln!("wdtmon-rs: cannot open the window: {e}");
                if !tray {
                    return;
                }
            }
        }
        if mode == WindowMode::HideInPlace || !tray || shared.borrow().quit.is_some() {
            return;
        }
        #[cfg(target_os = "linux")]
        if let Some(t) = &shared.borrow().tray {
            t.set_window_visible(false);
        }
        // Closed to the tray: wait for something that needs the window.
        loop {
            let request = shared.borrow().gui_rx.recv();
            let mut sh = shared.borrow_mut();
            match request {
                Err(_) => return,
                Ok(
                    GuiRequest::Show
                    | GuiRequest::Tray(tray::TrayAction::Toggle | tray::TrayAction::Show),
                ) => break,
                Ok(GuiRequest::Tray(tray::TrayAction::Hide) | GuiRequest::Health(_)) => {}
                Ok(GuiRequest::Tray(tray::TrayAction::Quit)) => {
                    if sh.quit_needs_confirmation() {
                        sh.pending.push_back(tray::TrayAction::Quit);
                        break;
                    }
                    sh.quit = Some(false);
                    return;
                }
                Ok(GuiRequest::Tray(action)) => {
                    sh.pending.push_back(action);
                    break;
                }
            }
        }
    }
}
