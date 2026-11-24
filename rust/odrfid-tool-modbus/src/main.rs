// No console window on Windows release builds.
#![cfg_attr(all(windows, not(debug_assertions)), windows_subsystem = "windows")]

mod app;
mod config;
#[cfg(any(test, debug_assertions))]
mod demo;
mod engine;
mod glyph;
mod i18n;
mod icons;
mod modbus;
mod reader;
mod serial;
mod style;
mod sysint;
mod traffic;

use config::{Language, Paths};
use engine::Repaint;
use modbus::{Line, Parity, StopBits};
use traffic::Traffic;

#[derive(Default)]
struct Args {
    port: Option<String>,
    baud: Option<u32>,
    parity: Option<Parity>,
    stop: Option<StopBits>,
    /// `Some(None)`: scan all addresses.
    address: Option<Option<u8>>,
    lang: Option<Language>,
    reset: bool,
    dfu: bool,
}

fn usage() -> ! {
    println!(
        "odrfid-tool-modbus {} — ODRFID-485 configuration over Modbus RTU (firmware 4.x)\n\n\
         Usage: odrfid-tool-modbus [options] [port]\n\n\
         Options:\n  \
         -P, --port <name>          serial port of the RS-485 adapter; connects at start\n  \
         -b, --baud <rate>          speed: a multiple of 100 from 500 to 2000000 (default: 115200)\n  \
         -p, --parity <none|odd|even>  parity (default: none)\n  \
         -s, --stop <1|1.5|2>       stop bits (default: 1)\n  \
         -a, --address <1..247|scan>  the reader's address (default: 1; 0 or scan: ask all)\n  \
         -d, --dfu                  restart the reader into its RS-485 bootloader and quit\n  \
         -l, --lang <system|en|ru>  interface language\n      \
         --reset                forget all settings\n  \
         -h, --help                 show this help\n  \
         -V, --version              show the version",
        app::APP_VERSION
    );
    std::process::exit(0)
}

fn bad(name: &str, value: &str) -> ! {
    eprintln!("{name}: invalid value '{value}'; see --help");
    std::process::exit(2)
}

fn parse_args() -> Args {
    let mut args = Args::default();
    let mut it = std::env::args().skip(1);
    while let Some(arg) = it.next() {
        let (name, inline) = match arg.split_once('=') {
            Some((n, v)) if n.starts_with('-') => (n.to_string(), Some(v.to_string())),
            _ => (arg.clone(), None),
        };
        let value = |it: &mut dyn Iterator<Item = String>| {
            inline.clone().or_else(|| it.next()).unwrap_or_else(|| {
                eprintln!("{name} needs a value; see --help");
                std::process::exit(2)
            })
        };
        match name.as_str() {
            "-P" | "--port" => args.port = Some(value(&mut it)),
            "-b" | "--baud" => {
                let v = value(&mut it);
                // The reader stores the speed / 100; config::sanitize would
                // otherwise drop a speed it cannot use without a word.
                let baud = v.parse().ok().filter(|&baud| {
                    Line {
                        baud,
                        ..Line::default()
                    }
                    .valid()
                });
                args.baud = Some(baud.unwrap_or_else(|| bad(&name, &v)));
            }
            "-p" | "--parity" => {
                let v = value(&mut it);
                args.parity = Some(match v.to_ascii_lowercase().as_str() {
                    "none" | "n" => Parity::None,
                    "odd" | "o" => Parity::Odd,
                    "even" | "e" => Parity::Even,
                    _ => bad(&name, &v),
                });
            }
            "-s" | "--stop" => {
                let v = value(&mut it);
                args.stop = Some(match v.as_str() {
                    "1" => StopBits::One,
                    "1.5" => StopBits::OneAndHalf,
                    "2" => StopBits::Two,
                    _ => bad(&name, &v),
                });
            }
            "-a" | "--address" => {
                let v = value(&mut it);
                let lower = v.to_ascii_lowercase();
                let number = lower
                    .strip_prefix("0x")
                    .map(|h| u8::from_str_radix(h, 16))
                    .unwrap_or_else(|| lower.parse::<u8>());
                args.address = Some(match (lower.as_str(), number) {
                    ("scan", _) | (_, Ok(0)) => None,
                    (_, Ok(a)) if a <= 247 => Some(a),
                    _ => bad(&name, &v),
                });
            }
            "-d" | "--dfu" => args.dfu = true,
            "-l" | "--lang" => {
                let v = value(&mut it);
                args.lang = Some(match v.to_ascii_lowercase() {
                    l if l == "system" => Language::System,
                    l if l.starts_with("ru") => Language::Ru,
                    l if l.starts_with("en") => Language::En,
                    _ => bad(&name, &v),
                })
            }
            "--reset" => args.reset = true,
            "-h" | "--help" => usage(),
            "-V" | "--version" => {
                println!("odrfid-tool-modbus {}", app::APP_VERSION);
                std::process::exit(0)
            }
            other if !other.starts_with('-') && args.port.is_none() => {
                args.port = Some(other.to_string())
            }
            other => {
                eprintln!("unknown option {other}; see --help");
                std::process::exit(2)
            }
        }
    }
    args
}

fn main() {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("warn")).init();
    let args = parse_args();

    let paths = Paths::new();
    if args.reset {
        match std::fs::remove_file(&paths.config_file) {
            Ok(()) => eprintln!("odrfid-tool-modbus: settings reset"),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => {}
            Err(e) => eprintln!("odrfid-tool-modbus: {}: {e}", paths.config_file.display()),
        }
    }
    let mut config = config::load(&paths.config_file).unwrap_or_default();
    if let Some(lang) = args.lang {
        config.language = lang;
    }
    let explicit = args.port.is_some();
    if let Some(port) = args.port {
        config.port = Some(port);
    }
    if let Some(baud) = args.baud {
        config.line.baud = baud;
    }
    if let Some(parity) = args.parity {
        config.line.parity = parity;
    }
    if let Some(stop) = args.stop {
        config.line.stop = stop;
    }
    match args.address {
        Some(Some(a)) => {
            config.address = a;
            config.scan_all = false;
        }
        Some(None) => config.scan_all = true,
        None => {}
    }
    config.sanitize();
    let _ = config::save(&paths.config_file, &config);
    i18n::set_language(config.language);

    let traffic = Traffic::default();
    let repaint = Repaint::default();
    let bus: engine::BusFactory = Box::new(|| Box::new(serial::SystemBus));
    // Debug builds can run without hardware on the emulated reader.
    #[cfg(debug_assertions)]
    let (bus, demo): (engine::BusFactory, Option<String>) =
        if let Some(mode) = std::env::var_os("ODRFID_DEMO") {
            let still = mode == "still";
            // `ODRFID_DEMO_PORT` names the port, for screenshots.
            let port = std::env::var("ODRFID_DEMO_PORT").unwrap_or_else(|_| "/dev/ttyDEMO0".into());
            let name = port.clone();
            (
                Box::new(move || Box::new(demo::FakeBus::demo(&name, still))),
                Some(port),
            )
        } else {
            (bus, None)
        };
    #[cfg(debug_assertions)]
    if let Some(port) = demo {
        // The first demo reader sits at the factory address.
        config.port = Some(port);
        config.address = reader::FACTORY_ADDRESS;
        config.scan_all = false;
    }
    let engine = engine::spawn(bus, traffic.clone(), repaint.clone());
    let connect = explicit || args.dfu || config.auto_connect;

    let mut viewport = eframe::egui::ViewportBuilder::default()
        .with_title(format!("odrfid-tool-modbus {}", app::APP_VERSION))
        .with_app_id(config::APP_ID)
        .with_inner_size(config.size.map_or(app::WINDOW_SIZE, |[w, h]| [w, h].into()))
        .with_min_inner_size(app::MIN_SIZE);
    if let Some(icon) = icons::app_icon() {
        viewport = viewport.with_icon(eframe::egui::IconData {
            rgba: icon.pixels,
            width: icon.size,
            height: icon.size,
        });
    }
    if let Some([x, y]) = config
        .position
        .filter(|p| p.iter().all(|v| v.abs() < 20_000.0))
    {
        viewport = viewport.with_position([x, y]);
    }
    let options = eframe::NativeOptions {
        viewport,
        ..Default::default()
    };
    // The window owns the engine; dropping it stops the engine and closes
    // the port.
    let result = eframe::run_native(
        config::APP_ID,
        options,
        Box::new(move |cc| {
            Ok(Box::new(app::App::new(
                cc, config, paths, engine, traffic, repaint, connect, args.dfu,
            )))
        }),
    );
    if let Err(e) = result {
        eprintln!("odrfid-tool-modbus: cannot open the window: {e}");
        std::process::exit(1);
    }
}
