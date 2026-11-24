//! The serial ports: listing them and the [`Port`] over `serialport`.

use std::io::{ErrorKind, Read, Write};
use std::time::{Duration, Instant};

use serialport::{ClearBuffer, SerialPort, SerialPortType};

use crate::modbus::{Line, Parity, Port, StopBits, response_len};

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PortInfo {
    pub name: String,
    /// The USB adapter's product, when known.
    pub description: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum OpenError {
    /// The OS denies access (Linux: the user is not in `dialout`).
    AccessDenied(String),
    /// Another program holds the port.
    Busy(String),
    /// No such port (unplugged).
    Missing(String),
    Other(String),
}

/// Where ports come from: the system, or the emulated reader of the tests.
pub trait Bus: Send {
    fn ports(&mut self) -> Vec<PortInfo>;
    fn open(&mut self, name: &str, line: &Line) -> Result<Box<dyn Port>, OpenError>;
}

pub struct SystemBus;

impl Bus for SystemBus {
    fn ports(&mut self) -> Vec<PortInfo> {
        list_ports()
    }

    fn open(&mut self, name: &str, line: &Line) -> Result<Box<dyn Port>, OpenError> {
        let builder = serialport::new(name, line.baud)
            .data_bits(serialport::DataBits::Eight)
            .parity(parity(line.parity))
            .stop_bits(stop_bits(line.stop))
            .flow_control(serialport::FlowControl::None)
            .timeout(SLICE);
        match builder.open() {
            Ok(port) => {
                let _ = port.clear(ClearBuffer::All);
                Ok(Box::new(SerialLink { port }))
            }
            Err(e) => Err(open_error(&e, || {
                list_ports()
                    .iter()
                    .any(|p| p.name.eq_ignore_ascii_case(name))
            })),
        }
    }
}

/// What a failed open means. serialport 4.10 has no "busy" kind: a port
/// another program holds comes back as `NoDevice` (Unix: TIOCEXCL's EBUSY or
/// a taken flock; Windows: ERROR_ACCESS_DENIED). On Unix a missing node is
/// `Io(NotFound)`, but Windows reports a missing port as `NoDevice` too, so
/// there the port list tells: a port the system lists is there, and busy.
fn open_error(e: &serialport::Error, listed: impl FnOnce() -> bool) -> OpenError {
    let text = e.to_string();
    match e.kind() {
        serialport::ErrorKind::NoDevice if cfg!(windows) && !listed() => OpenError::Missing(text),
        serialport::ErrorKind::NoDevice => OpenError::Busy(text),
        serialport::ErrorKind::Io(ErrorKind::PermissionDenied) => OpenError::AccessDenied(text),
        serialport::ErrorKind::Io(ErrorKind::NotFound) => OpenError::Missing(text),
        serialport::ErrorKind::Io(ErrorKind::ResourceBusy) => OpenError::Busy(text),
        _ => OpenError::Other(text),
    }
}

pub fn list_ports() -> Vec<PortInfo> {
    let mut ports: Vec<PortInfo> = serialport::available_ports()
        .unwrap_or_default()
        .into_iter()
        .map(|p| {
            let description = match &p.port_type {
                SerialPortType::UsbPort(usb) => {
                    let mut text = usb
                        .product
                        .clone()
                        .or_else(|| usb.manufacturer.clone())
                        .unwrap_or_else(|| "USB".into());
                    text.push_str(&format!(" ({:04X}:{:04X})", usb.vid, usb.pid));
                    text
                }
                SerialPortType::PciPort => "PCI".into(),
                SerialPortType::BluetoothPort => "Bluetooth".into(),
                SerialPortType::Unknown => String::new(),
            };
            PortInfo {
                name: p.port_name,
                description,
            }
        })
        .collect();
    // macOS lists every device twice; the call-out (cu.*) node is the one to
    // open.
    if cfg!(target_os = "macos") {
        ports.retain(|p| !p.name.starts_with("/dev/tty."));
    }
    // USB adapters first: an RS-485 dongle is what this tool talks through.
    ports.sort_by(|a, b| {
        let usb = |p: &PortInfo| p.description.contains(':');
        usb(b).cmp(&usb(a)).then(a.name.cmp(&b.name))
    });
    ports
}

/// Reads wait at most this long at a time, so the deadline is kept.
const SLICE: Duration = Duration::from_millis(10);

fn parity(p: Parity) -> serialport::Parity {
    match p {
        Parity::None => serialport::Parity::None,
        Parity::Odd => serialport::Parity::Odd,
        Parity::Even => serialport::Parity::Even,
    }
}

/// The PC side has no 1.5 stop bits; two are sent instead, which a receiver
/// set to 1.5 takes, and a receiver checks only the first stop bit anyway.
fn stop_bits(s: StopBits) -> serialport::StopBits {
    match s {
        StopBits::One => serialport::StopBits::One,
        StopBits::OneAndHalf | StopBits::Two => serialport::StopBits::Two,
    }
}

struct SerialLink {
    port: Box<dyn SerialPort>,
}

impl Port for SerialLink {
    fn exchange(&mut self, frame: &[u8], timeout: Duration) -> Result<Vec<u8>, String> {
        let io = |e: std::io::Error| e.to_string();
        // A late answer to an earlier request must not be taken for this one.
        self.port
            .clear(ClearBuffer::Input)
            .map_err(|e| e.to_string())?;
        self.port.write_all(frame).map_err(io)?;
        self.port.flush().map_err(io)?;
        let deadline = Instant::now() + timeout;
        let mut answer = Vec::new();
        let mut chunk = [0u8; 256];
        loop {
            if let Some(len) = response_len(&answer)
                && answer.len() >= len
            {
                return Ok(answer);
            }
            if Instant::now() >= deadline {
                return Ok(answer);
            }
            match self.port.read(&mut chunk) {
                Ok(n) => answer.extend_from_slice(&chunk[..n]),
                Err(e) if matches!(e.kind(), ErrorKind::TimedOut | ErrorKind::Interrupted) => {}
                Err(e) => return Err(e.to_string()),
            }
        }
    }

    fn set_line(&mut self, line: &Line) -> Result<(), String> {
        let e = |e: serialport::Error| e.to_string();
        self.port.set_baud_rate(line.baud).map_err(e)?;
        self.port.set_parity(parity(line.parity)).map_err(e)?;
        self.port.set_stop_bits(stop_bits(line.stop)).map_err(e)?;
        let _ = self.port.clear(ClearBuffer::All);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serialport::Error;

    #[test]
    fn open_errors() {
        let held = Error::new(
            serialport::ErrorKind::NoDevice,
            "Unable to acquire exclusive lock on serial port",
        );
        // Listed: busy everywhere. Not listed: Windows' missing port.
        assert!(matches!(open_error(&held, || true), OpenError::Busy(_)));
        let unlisted = open_error(&held, || false);
        if cfg!(windows) {
            assert!(matches!(unlisted, OpenError::Missing(_)));
        } else {
            assert!(matches!(unlisted, OpenError::Busy(_)));
        }
        let gone = Error::new(
            serialport::ErrorKind::Io(ErrorKind::NotFound),
            "No such file",
        );
        assert!(matches!(open_error(&gone, || true), OpenError::Missing(_)));
        let denied = Error::new(
            serialport::ErrorKind::Io(ErrorKind::PermissionDenied),
            "Permission denied",
        );
        assert!(matches!(
            open_error(&denied, || true),
            OpenError::AccessDenied(_)
        ));
    }
}
