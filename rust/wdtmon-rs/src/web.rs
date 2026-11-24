//! The optional web server of wdtmon3-mini: a status page at `/`, the JSON
//! state at `/data` (same fields as the Qt version) and POST `/` actions
//! (`ping`, `reset`, `hreset`) guarded by a per-run token.
//!
//! It listens on loopback unless `remote_access` is set in the config file.
//! On loopback, requests must name a loopback host, which stops DNS
//! rebinding from reaching the reset buttons through a web page.

use std::io::{Read, Write};
use std::net::{Ipv4Addr, SocketAddr, TcpListener, TcpStream};
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

use serde_json::json;

use crate::config::WebConfig;
use crate::engine::{EngineLink, Link, Request, Status};
use crate::hid::{Command, InState, Model};
use crate::i18n::tr;
use crate::sysint;

const MAX_HEAD: usize = 16 * 1024;
const MAX_BODY: usize = 4 * 1024;
const MAX_CLIENTS: usize = 16;
const IO_TIMEOUT: Duration = Duration::from_secs(5);

const LOGO: &[u8] = include_bytes!("../assets/web/logo.png");
const FAVICON: &[u8] = include_bytes!("../assets/web/favicon.png");

#[derive(Clone)]
pub struct WebContext {
    pub engine: EngineLink,
    /// CSRF token for the POST actions.
    pub token: Arc<str>,
    pub started: Instant,
}

pub struct WebServer {
    pub addr: SocketAddr,
    stop: Arc<AtomicBool>,
    thread: Option<JoinHandle<()>>,
}

impl Drop for WebServer {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::Relaxed);
        if let Some(t) = self.thread.take() {
            let _ = t.join();
        }
    }
}

pub fn start(config: &WebConfig, ctx: WebContext) -> Result<WebServer, String> {
    let ip = if config.remote_access {
        Ipv4Addr::UNSPECIFIED
    } else {
        Ipv4Addr::LOCALHOST
    };
    let listener = TcpListener::bind((ip, config.port)).map_err(|e| e.to_string())?;
    listener.set_nonblocking(true).map_err(|e| e.to_string())?;
    let addr = listener.local_addr().map_err(|e| e.to_string())?;
    let stop = Arc::new(AtomicBool::new(false));
    let remote = config.remote_access;
    let thread = {
        let stop = stop.clone();
        thread::Builder::new()
            .name("web".into())
            .spawn(move || serve(listener, stop, ctx, remote))
            .map_err(|e| e.to_string())?
    };
    log::info!("web server on http://{addr}");
    Ok(WebServer {
        addr,
        stop,
        thread: Some(thread),
    })
}

fn serve(listener: TcpListener, stop: Arc<AtomicBool>, ctx: WebContext, remote: bool) {
    let active = Arc::new(AtomicUsize::new(0));
    while !stop.load(Ordering::Relaxed) {
        match listener.accept() {
            Ok((stream, _)) => {
                if active.load(Ordering::Relaxed) >= MAX_CLIENTS {
                    continue; // dropped: closes the connection
                }
                active.fetch_add(1, Ordering::Relaxed);
                let (ctx, worker) = (ctx.clone(), active.clone());
                let spawned = thread::Builder::new()
                    .name("web-client".into())
                    .spawn(move || {
                        handle(stream, &ctx, remote);
                        worker.fetch_sub(1, Ordering::Relaxed);
                    });
                if spawned.is_err() {
                    active.fetch_sub(1, Ordering::Relaxed);
                }
            }
            Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                thread::sleep(Duration::from_millis(50))
            }
            Err(e) => {
                log::warn!("web server: {e}");
                thread::sleep(Duration::from_millis(200));
            }
        }
    }
}

// ---- HTTP ------------------------------------------------------------------

struct HttpRequest {
    method: String,
    path: String,
    query: String,
    headers: Vec<(String, String)>,
    body: Vec<u8>,
}

impl HttpRequest {
    fn header(&self, name: &str) -> Option<&str> {
        self.headers
            .iter()
            .find(|(n, _)| n.eq_ignore_ascii_case(name))
            .map(|(_, v)| v.as_str())
    }
}

struct Response {
    status: u16,
    content_type: &'static str,
    body: Vec<u8>,
    location: Option<String>,
}

impl Response {
    fn empty(status: u16) -> Self {
        Self {
            status,
            content_type: "text/plain; charset=utf-8",
            body: Vec::new(),
            location: None,
        }
    }

    fn html(body: String) -> Self {
        Self {
            status: 200,
            content_type: "text/html; charset=utf-8",
            body: body.into_bytes(),
            location: None,
        }
    }

    fn redirect(to: &str) -> Self {
        Self {
            location: Some(to.into()),
            ..Self::empty(303)
        }
    }
}

fn reason(status: u16) -> &'static str {
    match status {
        200 => "OK",
        303 => "See Other",
        400 => "Bad Request",
        403 => "Forbidden",
        404 => "Not Found",
        405 => "Method Not Allowed",
        413 => "Payload Too Large",
        421 => "Misdirected Request",
        431 => "Request Header Fields Too Large",
        _ => "Service Unavailable",
    }
}

fn handle(mut stream: TcpStream, ctx: &WebContext, remote: bool) {
    let _ = stream.set_nonblocking(false);
    let _ = stream.set_read_timeout(Some(IO_TIMEOUT));
    let _ = stream.set_write_timeout(Some(IO_TIMEOUT));
    let (response, head_only) = match read_request(&mut stream) {
        Ok(request) => (route(&request, ctx, remote), request.method == "HEAD"),
        Err(status) => (Response::empty(status), false),
    };
    let mut head = format!(
        "HTTP/1.1 {} {}\r\nContent-Type: {}\r\nContent-Length: {}\r\nConnection: close\r\n\
         Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nX-Frame-Options: DENY\r\n\
         Referrer-Policy: same-origin\r\n\
         Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; img-src 'self'; form-action 'self'; frame-ancestors 'none'\r\n",
        response.status,
        reason(response.status),
        response.content_type,
        response.body.len()
    );
    if response.status == 405 {
        head.push_str("Allow: GET, HEAD, POST\r\n");
    }
    if let Some(location) = &response.location {
        head.push_str(&format!("Location: {location}\r\n"));
    }
    head.push_str("\r\n");
    let _ = stream.write_all(head.as_bytes());
    if !head_only {
        let _ = stream.write_all(&response.body);
    }
    let _ = stream.flush();
}

fn read_request(stream: &mut impl Read) -> Result<HttpRequest, u16> {
    let mut buf = Vec::with_capacity(1024);
    let mut chunk = [0u8; 1024];
    let head_end = loop {
        if let Some(at) = buf.windows(4).position(|w| w == b"\r\n\r\n") {
            break at;
        }
        if buf.len() > MAX_HEAD {
            return Err(431);
        }
        match stream.read(&mut chunk) {
            Ok(0) | Err(_) => return Err(400),
            Ok(n) => buf.extend_from_slice(&chunk[..n]),
        }
    };
    let head = std::str::from_utf8(&buf[..head_end]).map_err(|_| 400u16)?;
    let mut lines = head.split("\r\n");
    let mut first = lines.next().unwrap_or("").split(' ');
    let (Some(method), Some(target), Some(version)) = (first.next(), first.next(), first.next())
    else {
        return Err(400);
    };
    if !version.starts_with("HTTP/1.") || !target.starts_with('/') {
        return Err(400);
    }
    let headers: Vec<(String, String)> = lines
        .filter_map(|l| l.split_once(':'))
        .map(|(n, v)| (n.trim().to_string(), v.trim().to_string()))
        .collect();
    let length = match headers
        .iter()
        .find(|(n, _)| n.eq_ignore_ascii_case("content-length"))
    {
        Some((_, v)) => v.parse::<usize>().map_err(|_| 400u16)?,
        None => 0,
    };
    if length > MAX_BODY {
        return Err(413);
    }
    if headers
        .iter()
        .any(|(n, _)| n.eq_ignore_ascii_case("transfer-encoding"))
    {
        return Err(400);
    }
    let mut body = buf[head_end + 4..].to_vec();
    while body.len() < length {
        match stream.read(&mut chunk) {
            Ok(0) | Err(_) => return Err(400),
            Ok(n) => body.extend_from_slice(&chunk[..n]),
        }
    }
    body.truncate(length);
    let (path, query) = target.split_once('?').unwrap_or((target, ""));
    Ok(HttpRequest {
        method: method.to_string(),
        path: path.to_string(),
        query: query.to_string(),
        headers,
        body,
    })
}

/// On loopback the Host header must name loopback too.
fn host_allowed(host: Option<&str>) -> bool {
    let Some(host) = host else { return true };
    let name = if let Some(rest) = host.strip_prefix('[') {
        rest.split(']').next().unwrap_or("")
    } else {
        host.rsplit_once(':').map_or(host, |(n, _)| n)
    };
    let name = name.to_ascii_lowercase();
    name == "localhost" || name.ends_with(".localhost") || name == "127.0.0.1" || name == "::1"
}

fn route(request: &HttpRequest, ctx: &WebContext, remote: bool) -> Response {
    if !remote && !host_allowed(request.header("host")) {
        return Response::empty(421);
    }
    match (request.method.as_str(), request.path.as_str()) {
        ("GET" | "HEAD", "/") => {
            let st = ctx.engine.status().clone();
            Response::html(page(
                &st,
                ctx,
                Body::Status(form_value(&request.query, "done")),
            ))
        }
        ("GET" | "HEAD", "/data") => {
            let st = ctx.engine.status().clone();
            Response {
                status: 200,
                content_type: "application/json; charset=utf-8",
                body: serde_json::to_vec_pretty(&data_json(&st)).unwrap_or_default(),
                location: None,
            }
        }
        ("GET" | "HEAD", "/logo" | "/logo.png") => Response {
            status: 200,
            content_type: "image/png",
            body: LOGO.to_vec(),
            location: None,
        },
        ("GET" | "HEAD", "/favicon.png" | "/favicon.ico") => Response {
            status: 200,
            content_type: "image/png",
            body: FAVICON.to_vec(),
            location: None,
        },
        ("POST", "/") => action(request, ctx),
        (_, "/" | "/data" | "/logo" | "/logo.png" | "/favicon.png" | "/favicon.ico") => {
            Response::empty(405)
        }
        _ => Response::empty(404),
    }
}

fn action(request: &HttpRequest, ctx: &WebContext) -> Response {
    // A cross-site form post carries the other site's origin, or `null`.
    // The page's own buttons send the real one only because its referrer
    // policy is `same-origin`: under `no-referrer` browsers send `null` too.
    if let (Some(origin), Some(host)) = (request.header("origin"), request.header("host"))
        && !origin.eq_ignore_ascii_case(&format!("http://{host}"))
    {
        return Response::empty(403);
    }
    let body = String::from_utf8_lossy(&request.body);
    if form_value(&body, "token").as_deref() != Some(&*ctx.token) {
        log::warn!("web: rejected an action with an invalid token");
        return Response::empty(403);
    }
    let st = ctx.engine.status().clone();
    if st.link != Link::Connected {
        return Response::empty(503);
    }
    let tests = st.has_tests();
    // The page's buttons ask first; `reset=1` / `hreset=1` act at once, as
    // the Qt server did, for scripts.
    if let Some(ask) = form_value(&body, "ask") {
        return match ask.as_str() {
            "reset" | "hreset" if tests => {
                Response::html(page(&st, ctx, Body::Confirm(ask == "hreset")))
            }
            _ => Response::empty(400),
        };
    }
    let (command, done) = if form_value(&body, "ping").as_deref() == Some("1") {
        (Command::Heartbeat, "ping")
    } else if form_value(&body, "reset").as_deref() == Some("1") && tests {
        (Command::TestReset, "reset")
    } else if form_value(&body, "hreset").as_deref() == Some("1") && tests {
        (Command::TestPowerCycle, "hreset")
    } else {
        return Response::empty(400);
    };
    ctx.engine.send(Request::Command(command));
    Response::redirect(&format!("/?done={done}"))
}

/// Value of `key` in an `application/x-www-form-urlencoded` string.
fn form_value(form: &str, key: &str) -> Option<String> {
    form.split('&').find_map(|pair| {
        let (k, v) = pair.split_once('=').unwrap_or((pair, ""));
        (url_decode(k) == key).then(|| url_decode(v))
    })
}

fn url_decode(s: &str) -> String {
    let bytes = s.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        let hex = bytes
            .get(i + 1..i + 3)
            .and_then(|h| std::str::from_utf8(h).ok())
            .and_then(|h| u8::from_str_radix(h, 16).ok());
        match (bytes[i], hex) {
            (b'+', _) => out.push(b' '),
            (b'%', Some(b)) => {
                out.push(b);
                i += 2;
            }
            (b, _) => out.push(b),
        }
        i += 1;
    }
    String::from_utf8_lossy(&out).into_owned()
}

// ---- content ---------------------------------------------------------------

fn data_json(st: &Status) -> serde_json::Value {
    let data = st.data.filter(|_| st.connected());
    let channel = |v: Option<u8>| match v {
        Some(v) => json!({ "value": v, "state": if v != 0 { "open" } else { "closed" } }),
        None => json!({}),
    };
    let ch3 = match data.map(|d| d.input) {
        Some(InState::Input(v)) => json!({ "type": "input", "value": u8::from(v) }),
        Some(InState::Output(v)) => json!({ "type": "output", "value": u8::from(v) }),
        // Through the decimal text: f32 → f64 would print 21.299999237060547.
        Some(InState::Temperature(Some(t))) => {
            json!({ "type": "temperature", "value": format!("{t:.1}").parse::<f64>().unwrap_or_default() })
        }
        _ => json!({}),
    };
    let firmware = st.firmware.as_ref().filter(|_| st.connected());
    json!({
        "time": st.data_time.map(|t| t.format("%Y.%m.%d %H:%M:%S").to_string()),
        "ch1": channel(data.map(|d| d.ch1)),
        "ch2": channel(data.and_then(|d| d.ch2)),
        "ch3": ch3,
        "connected": st.connected(),
        "serial": st.serial,
        "model": firmware.map(|f| model_name(f.model)),
        "firmware": firmware.map(|f| f.raw.clone()),
    })
}

fn model_name(model: Model) -> &'static str {
    match model {
        Model::Pro2 => "Pro2",
        Model::Lite => "Lite",
        Model::Unknown => "?",
    }
}

fn esc(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for c in s.chars() {
        match c {
            '&' => out.push_str("&amp;"),
            '<' => out.push_str("&lt;"),
            '>' => out.push_str("&gt;"),
            '"' => out.push_str("&quot;"),
            '\'' => out.push_str("&#39;"),
            c => out.push(c),
        }
    }
    out
}

fn channel_text(v: Option<u8>) -> String {
    match v {
        Some(0) => tr("closed").into(),
        Some(_) => tr("open").into(),
        None => tr("n/a").into(),
    }
}

fn input_text(input: Option<InState>) -> String {
    match input {
        Some(InState::Input(v)) => format!("{} ({})", u8::from(v), tr("input")),
        Some(InState::Output(v)) => format!("{} ({})", u8::from(v), tr("output")),
        Some(InState::Temperature(Some(t))) => format!("{} °C", crate::i18n::number(t, 1)),
        Some(InState::Temperature(None)) => tr("n/a").into(),
        Some(InState::Off) => tr("off").into(),
        None => tr("n/a").into(),
    }
}

enum Body {
    /// The status page, with the action just done.
    Status(Option<String>),
    /// Asks before a reset (`false`) or a power cycle (`true`).
    Confirm(bool),
}

const STYLE: &str = r#"
:root{--brand:#009ADF;--bg:#eef1f4;--card:#fff;--text:#17202b;--sub:#5a6675;--line:#dce2e8;--alert:#cf3434}
@media (prefers-color-scheme:dark){:root{--bg:#121519;--card:#1c2026;--text:#e6eaef;--sub:#98a2af;--line:#2e343d;--brand:#2ba8e8;--alert:#ef5f5f}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
header{display:flex;align-items:center;gap:12px;padding:14px 20px;background:var(--card);border-bottom:2px solid var(--brand)}
header img{width:32px;height:32px}
header h1{font-size:18px;margin:0;flex:1}
header a{color:var(--brand);text-decoration:none}
main{max-width:640px;margin:20px auto;padding:0 16px;display:grid;gap:14px}
section{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px 18px}
h2{margin:0 0 8px;font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:var(--sub)}
table{width:100%;border-collapse:collapse}
td{padding:5px 0;border-top:1px solid var(--line)}
tr:first-child td{border-top:0}
td:first-child{color:var(--sub);width:45%}
form{display:flex;flex-wrap:wrap;gap:8px;margin:0}
button,.link{font:inherit;padding:7px 16px;border-radius:7px;border:1px solid var(--line);background:var(--card);color:var(--text);cursor:pointer;text-decoration:none}
button.primary{background:var(--brand);border-color:var(--brand);color:#fff}
button.danger{background:var(--alert);border-color:var(--alert);color:#fff}
.flash{border-color:var(--brand)}
footer{max-width:640px;margin:0 auto 24px;padding:0 16px;display:flex;gap:16px;justify-content:flex-end}
footer a{color:var(--brand);text-decoration:none}
"#;

fn page(st: &Status, ctx: &WebContext, body: Body) -> String {
    let connected = st.connected();
    let firmware = st.firmware.as_ref().filter(|_| connected);
    let data = st.data.filter(|_| connected);
    let serial = st.serial.clone().unwrap_or_default();
    let token = esc(&ctx.token);
    let mut main = String::new();
    match body {
        Body::Confirm(power) => {
            let (question, field, label) = if power {
                (
                    tr("Power-cycle the PC now? Unsaved work is lost."),
                    "hreset",
                    tr("Hard reset"),
                )
            } else {
                (
                    tr("Press the reset button of the PC now?"),
                    "reset",
                    tr("Reset"),
                )
            };
            main.push_str(&format!(
                "<section class=flash><h2>{}</h2><p>{}</p><form method=post action=/>\
                 <input type=hidden name=token value=\"{token}\">\
                 <button class=danger type=submit name={field} value=1>{}</button>\
                 <a class=link href=/>{}</a></form></section>",
                esc(tr("Confirm")),
                esc(question),
                esc(label),
                esc(tr("Cancel")),
            ));
        }
        Body::Status(done) => {
            let flash = match done.as_deref() {
                Some("ping") => Some(tr("Heartbeat sent")),
                Some("reset") => Some(tr("Reset requested")),
                Some("hreset") => Some(tr("Hard reset requested")),
                _ => None,
            };
            if let Some(text) = flash {
                main.push_str(&format!("<section class=flash>{}</section>", esc(text)));
            }
            let state = if connected {
                tr("connected")
            } else {
                tr("not connected")
            };
            let rows = [
                (
                    tr("Type"),
                    firmware.map_or_else(
                        || tr("n/a").to_string(),
                        |f| model_name(f.model).to_string(),
                    ),
                ),
                (
                    tr("Firmware"),
                    firmware.map_or_else(|| tr("n/a").to_string(), |f| f.raw.clone()),
                ),
                (
                    tr("Serial number"),
                    if serial.is_empty() {
                        tr("n/a").to_string()
                    } else {
                        serial.clone()
                    },
                ),
                (tr("State"), state.to_string()),
                (
                    tr("Last update"),
                    st.data_time.map_or_else(
                        || tr("n/a").to_string(),
                        |t| t.format("%Y.%m.%d %H:%M:%S").to_string(),
                    ),
                ),
            ];
            main.push_str(&card(tr("Watchdog"), &rows));
            let rows = [
                ("CH1", channel_text(data.map(|d| d.ch1))),
                ("CH2", channel_text(data.and_then(|d| d.ch2))),
                (
                    "CH3",
                    input_text(data.filter(|d| d.ch2.is_some()).map(|d| d.input)),
                ),
            ];
            main.push_str(&card(tr("Channels"), &rows));
            let rows = [
                (
                    tr("System"),
                    sysint::format_duration(sysint::system_uptime()),
                ),
                (
                    tr("Application"),
                    sysint::format_duration(ctx.started.elapsed()),
                ),
            ];
            main.push_str(&card(tr("Uptime"), &rows));
            if connected {
                let mut buttons = format!(
                    "<button class=primary type=submit name=ping value=1>{}</button>",
                    esc(tr("Update"))
                );
                if st.has_tests() {
                    buttons.push_str(&format!(
                        "<button type=submit name=ask value=reset>{}</button>\
                         <button class=danger type=submit name=ask value=hreset>{}</button>",
                        esc(tr("Reset")),
                        esc(tr("Hard reset"))
                    ));
                }
                main.push_str(&format!(
                    "<section><form method=post action=/><input type=hidden name=token value=\"{token}\">{buttons}</form></section>"
                ));
            }
        }
    }
    format!(
        "<!doctype html><html><head><meta charset=utf-8>\
         <meta name=viewport content=\"width=device-width,initial-scale=1\">\
         <title>USB Watchdog {}</title><link rel=icon type=image/png href=/favicon.png>\
         <style>{STYLE}</style></head><body>\
         <header><img src=/logo alt=\"\"><h1>USB Watchdog</h1><a href=/>{}</a></header>\
         <main>{main}</main>\
         <footer><a href=/data>JSON API</a><a href=https://unitx.pro rel=noreferrer>unitx.pro</a></footer>\
         </body></html>",
        esc(&serial),
        esc(tr("Refresh")),
    )
}

fn card(title: &str, rows: &[(&str, String)]) -> String {
    let rows: String = rows
        .iter()
        .map(|(k, v)| format!("<tr><td>{}</td><td>{}</td></tr>", esc(k), esc(v)))
        .collect();
    format!(
        "<section><h2>{}</h2><table>{rows}</table></section>",
        esc(title)
    )
}

/// A token nobody outside this process can guess: SipHash keyed by the
/// standard library's per-process random keys.
pub fn new_token() -> String {
    use std::collections::hash_map::RandomState;
    use std::hash::{BuildHasher, Hasher};
    let mut out = String::new();
    for salt in 0..2u64 {
        let mut h = RandomState::new().build_hasher();
        h.write_u64(salt);
        h.write_u128(
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap_or_default()
                .as_nanos(),
        );
        h.write_u32(std::process::id());
        out.push_str(&format!("{:016x}", h.finish()));
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hosts() {
        assert!(host_allowed(Some("localhost:34242")));
        assert!(host_allowed(Some("127.0.0.1:34242")));
        assert!(host_allowed(Some("[::1]:34242")));
        assert!(host_allowed(None));
        assert!(!host_allowed(Some("evil.example:34242")));
        assert!(!host_allowed(Some("127.0.0.1.evil.example")));
    }

    #[test]
    fn forms() {
        assert_eq!(
            form_value("token=a%2Bb&ping=1", "token").as_deref(),
            Some("a+b")
        );
        assert_eq!(form_value("token=a+b", "token").as_deref(), Some("a b"));
        assert_eq!(form_value("x=%zz%4", "x").as_deref(), Some("%zz%4"));
        assert_eq!(form_value("ping=1", "reset"), None);
    }

    #[test]
    fn parses_requests() {
        let raw =
            b"POST /?a=1 HTTP/1.1\r\nHost: localhost\r\nContent-Length: 7\r\n\r\nping=1&extra";
        let r = read_request(&mut &raw[..]).unwrap();
        assert_eq!(r.method, "POST");
        assert_eq!(r.path, "/");
        assert_eq!(r.query, "a=1");
        assert_eq!(r.header("HOST"), Some("localhost"));
        assert_eq!(r.body, b"ping=1&");
        assert_eq!(
            read_request(&mut &b"GET / HTTP/1.1\r\n"[..]).err(),
            Some(400)
        );
        assert_eq!(
            read_request(&mut &b"GET x HTTP/1.1\r\n\r\n"[..]).err(),
            Some(400)
        );
        let big = b"POST / HTTP/1.1\r\nContent-Length: 999999\r\n\r\n";
        assert_eq!(read_request(&mut &big[..]).err(), Some(413));
    }

    #[test]
    fn token_is_random_hex() {
        let (a, b) = (new_token(), new_token());
        assert_eq!(a.len(), 32);
        assert_ne!(a, b);
        assert!(a.chars().all(|c| c.is_ascii_hexdigit()));
    }

    #[test]
    fn escapes_html() {
        assert_eq!(
            esc("<a href=\"x\">&'"),
            "&lt;a href=&quot;x&quot;&gt;&amp;&#39;"
        );
    }
}
