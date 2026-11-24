//! Single instance: the first process listens on a loopback port; a second
//! one asks it to show its window and exits.

use std::io::{BufRead, BufReader, Write};
use std::net::{Ipv4Addr, SocketAddr, TcpListener, TcpStream};
use std::thread;
use std::time::Duration;

const HELLO: &str = "wdtmon-rs-show";
const WELCOME: &str = "wdtmon-rs-ok";

/// Fixed per user.
fn port() -> u16 {
    let user = std::env::var("USER")
        .or_else(|_| std::env::var("USERNAME"))
        .unwrap_or_default();
    // FNV-1a over the app and user names, folded into 50200..=50999.
    let mut hash: u32 = 0x811c_9dc5;
    for b in HELLO.bytes().chain(user.bytes()) {
        hash ^= u32::from(b);
        hash = hash.wrapping_mul(0x0100_0193);
    }
    50200 + (hash % 800) as u16
}

pub enum Instance {
    /// This is the first instance; the listener receives show requests.
    Primary(TcpListener),
    /// Another instance took the request.
    Secondary,
}

pub fn acquire() -> Instance {
    let addr = SocketAddr::from((Ipv4Addr::LOCALHOST, port()));
    match TcpListener::bind(addr) {
        Ok(listener) => Instance::Primary(listener),
        Err(_) if notify(addr) => Instance::Secondary,
        Err(_) => {
            // Some other program owns the port: run without the guard.
            log::warn!("single-instance port {addr} is taken by another program");
            Instance::Primary(TcpListener::bind((Ipv4Addr::LOCALHOST, 0)).expect("bind loopback"))
        }
    }
}

fn notify(addr: SocketAddr) -> bool {
    let Ok(mut stream) = TcpStream::connect_timeout(&addr, Duration::from_millis(500)) else {
        return false;
    };
    let _ = stream.set_read_timeout(Some(Duration::from_secs(2)));
    if stream.write_all(format!("{HELLO}\n").as_bytes()).is_err() {
        return false;
    }
    let mut line = String::new();
    BufReader::new(stream).read_line(&mut line).is_ok() && line.trim() == WELCOME
}

/// Calls `on_show` for every request from a later instance.
pub fn listen(listener: TcpListener, on_show: impl Fn() + Send + 'static) {
    thread::Builder::new()
        .name("instance".into())
        .spawn(move || {
            for stream in listener.incoming().flatten() {
                let _ = stream.set_read_timeout(Some(Duration::from_secs(2)));
                let mut line = String::new();
                if BufReader::new(&stream).read_line(&mut line).is_ok() && line.trim() == HELLO {
                    let _ = (&stream).write_all(format!("{WELCOME}\n").as_bytes());
                    on_show();
                }
            }
        })
        .expect("spawn instance listener");
}
