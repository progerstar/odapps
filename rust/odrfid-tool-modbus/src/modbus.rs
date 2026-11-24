//! Modbus RTU master: line settings, framing and CRC, the requests this tool
//! sends and the checks every answer goes through.
//!
//! A [`Port`] only moves bytes: it writes a frame and collects the answer
//! until [`response_len`] says it is complete. [`Client`] builds the
//! requests, waits the right time for the line speed and validates the
//! answers against the request, like `tests/odrfid.py` of the firmware.

use std::fmt;
use std::time::{Duration, Instant};

use serde::{Deserialize, Serialize};

use crate::traffic::{Dir, Traffic};

pub const FC_READ_HOLDING: u8 = 0x03;
pub const FC_READ_INPUT: u8 = 0x04;
pub const FC_WRITE_SINGLE: u8 = 0x06;
pub const FC_WRITE_MULTIPLE: u8 = 0x10;
pub const FC_READ_WRITE: u8 = 0x17;
pub const FC_DEVICE_ID: u8 = 0x2B;
const MEI_DEVICE_ID: u8 = 0x0E;
/// INFO of the RS-485 bootloader (`bootloader/Readme.md` of the firmware):
/// the firmware itself answers it with exception 01.
pub const FC_LOADER_INFO: u8 = 0x41;
/// Address, function, 16 bytes of INFO, CRC.
const LOADER_INFO_LEN: usize = 20;

pub const EXC_ILLEGAL_FUNCTION: u8 = 1;
pub const EXC_ILLEGAL_ADDRESS: u8 = 2;
pub const EXC_ILLEGAL_VALUE: u8 = 3;
pub const EXC_DEVICE_FAILURE: u8 = 4;
pub const EXC_BUSY: u8 = 6;

/// Registers per request (Modbus limits).
pub const MAX_READ: u16 = 125;
pub const MAX_WRITE: u16 = 123;

// ---- line settings ---------------------------------------------------------

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Parity {
    #[default]
    None,
    Odd,
    Even,
}

impl Parity {
    pub const ALL: [Self; 3] = [Self::None, Self::Odd, Self::Even];

    /// Register 535 of the reader.
    pub fn code(self) -> u16 {
        match self {
            Self::None => 0,
            Self::Odd => 1,
            Self::Even => 2,
        }
    }

    pub fn from_code(code: u16) -> Option<Self> {
        Self::ALL.get(usize::from(code)).copied()
    }

    pub fn letter(self) -> char {
        match self {
            Self::None => 'N',
            Self::Odd => 'O',
            Self::Even => 'E',
        }
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub enum StopBits {
    #[default]
    #[serde(rename = "1")]
    One,
    #[serde(rename = "1.5")]
    OneAndHalf,
    #[serde(rename = "2")]
    Two,
}

impl StopBits {
    pub const ALL: [Self; 3] = [Self::One, Self::OneAndHalf, Self::Two];

    /// Register 536 of the reader: stop bits × 10.
    pub fn code(self) -> u16 {
        match self {
            Self::One => 10,
            Self::OneAndHalf => 15,
            Self::Two => 20,
        }
    }

    pub fn from_code(code: u16) -> Option<Self> {
        Self::ALL.into_iter().find(|s| s.code() == code)
    }

    pub fn label(self) -> &'static str {
        match self {
            Self::One => "1",
            Self::OneAndHalf => "1.5",
            Self::Two => "2",
        }
    }

    fn tenths(self) -> u32 {
        u32::from(self.code())
    }
}

/// Speeds offered in the window; any multiple of 100 the reader accepts can
/// be typed in.
pub const BAUD_RATES: [u32; 12] = [
    1200, 2400, 4800, 9600, 14400, 19200, 38400, 57600, 115200, 230400, 460800, 921600,
];
/// The reader's USART takes 489..=2_000_000 baud (16 ≤ 32 MHz / baud ≤ 65535);
/// register 534 holds the speed / 100.
pub const BAUD_MIN: u32 = 500;
pub const BAUD_MAX: u32 = 2_000_000;

/// Serial line settings; data bits are always 8.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default)]
pub struct Line {
    pub baud: u32,
    pub parity: Parity,
    pub stop: StopBits,
}

impl Default for Line {
    /// The reader's factory settings: 115200 8N1.
    fn default() -> Self {
        Self {
            baud: 115_200,
            parity: Parity::None,
            stop: StopBits::One,
        }
    }
}

impl Line {
    /// `115200 8N1`, `9600 8E1.5`.
    pub fn label(&self) -> String {
        format!(
            "{} 8{}{}",
            self.baud,
            self.parity.letter(),
            self.stop.label()
        )
    }

    /// One character on the wire: start, 8 data bits, parity and stop bits.
    pub fn char_time(&self) -> Duration {
        let tenths = 90 + if self.parity == Parity::None { 0 } else { 10 } + self.stop.tenths();
        Duration::from_nanos(u64::from(tenths) * 100_000_000 / u64::from(self.baud.max(1)))
    }

    /// The silence that separates RTU frames: 3.5 characters, 1.75 ms from
    /// 19200 baud up.
    pub fn frame_gap(&self) -> Duration {
        if self.baud > 19_200 {
            Duration::from_micros(1750)
        } else {
            self.char_time() * 7 / 2
        }
    }

    pub fn valid(&self) -> bool {
        (BAUD_MIN..=BAUD_MAX).contains(&self.baud) && self.baud.is_multiple_of(100)
    }
}

// ---- framing ---------------------------------------------------------------

pub fn crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    for &b in data {
        crc ^= u16::from(b);
        for _ in 0..8 {
            crc = if crc & 1 != 0 {
                (crc >> 1) ^ 0xA001
            } else {
                crc >> 1
            };
        }
    }
    crc
}

/// Address, PDU and the CRC, low byte first.
pub fn frame(address: u8, pdu: &[u8]) -> Vec<u8> {
    let mut f = Vec::with_capacity(pdu.len() + 3);
    f.push(address);
    f.extend_from_slice(pdu);
    f.extend_from_slice(&crc16(&f).to_le_bytes());
    f
}

/// The full length of an answer frame, once enough of it has arrived to tell.
pub fn response_len(frame: &[u8]) -> Option<usize> {
    let function = *frame.get(1)?;
    if function & 0x80 != 0 {
        return Some(5);
    }
    match function {
        0x01..=0x04 | FC_READ_WRITE => frame.get(2).map(|&n| 5 + usize::from(n)),
        0x05 | 0x06 | 0x0F | 0x10 => Some(8),
        FC_DEVICE_ID => {
            // addr, 0x2B, MEI, code, conformity, more, next, count, objects
            let count = *frame.get(7)?;
            let mut pos = 8;
            for _ in 0..count {
                pos += 2 + usize::from(*frame.get(pos + 1)?);
            }
            Some(pos + 2)
        }
        FC_LOADER_INFO => Some(LOADER_INFO_LEN),
        // Not ours: stop reading, validation rejects it.
        _ => Some(frame.len()),
    }
}

pub fn regs_to_bytes(regs: &[u16]) -> Vec<u8> {
    regs.iter().flat_map(|r| r.to_be_bytes()).collect()
}

/// Two bytes per register, the first in the high half; an odd tail is padded.
pub fn bytes_to_regs(bytes: &[u8]) -> Vec<u16> {
    bytes
        .chunks(2)
        .map(|c| u16::from_be_bytes([c[0], c.get(1).copied().unwrap_or(0)]))
        .collect()
}

// ---- errors ----------------------------------------------------------------

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Error {
    /// No answer, or it stopped short.
    Timeout,
    /// The answer is damaged or does not match the request.
    Frame(String),
    /// A Modbus exception code.
    Exception(u8),
    /// The serial port itself failed (an unplugged adapter).
    Port(String),
}

impl Error {
    /// Lost or damaged on the line: worth asking again.
    pub fn is_link(&self) -> bool {
        matches!(self, Self::Timeout | Self::Frame(_))
    }
}

pub fn exception_text(code: u8) -> &'static str {
    use crate::i18n::tr;
    match code {
        EXC_ILLEGAL_FUNCTION => tr("illegal function"),
        EXC_ILLEGAL_ADDRESS => tr("illegal data address"),
        EXC_ILLEGAL_VALUE => tr("illegal data value"),
        EXC_DEVICE_FAILURE => tr("device failure"),
        EXC_BUSY => tr("device busy"),
        _ => tr("exception"),
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        use crate::i18n::tr;
        match self {
            Self::Timeout => f.write_str(tr("no answer")),
            Self::Frame(text) => write!(f, "{}: {text}", tr("bad answer")),
            Self::Exception(code) => write!(
                f,
                "{} {code:02}: {}",
                tr("Modbus exception"),
                exception_text(*code)
            ),
            Self::Port(text) => f.write_str(text),
        }
    }
}

// ---- transport -------------------------------------------------------------

pub trait Port: Send {
    /// Writes `frame` and collects the answer until [`response_len`] says it
    /// is complete or `timeout` passes; what arrived is returned either way.
    /// `Err` only when the port itself fails.
    fn exchange(&mut self, frame: &[u8], timeout: Duration) -> Result<Vec<u8>, String>;
    /// Changes the speed, parity and stop bits of the open port.
    fn set_line(&mut self, line: &Line) -> Result<(), String>;
}

/// How long to wait for an answer.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Patience {
    /// Probing an address during a scan.
    Scan,
    /// Ordinary reads and writes.
    Normal,
    /// The command block: the card operation runs before the answer.
    Card,
}

impl Patience {
    fn base(self) -> Duration {
        match self {
            Self::Scan => Duration::from_millis(60),
            Self::Normal => Duration::from_millis(300),
            Self::Card => Duration::from_millis(1500),
        }
    }
}

pub struct Client {
    port: Box<dyn Port>,
    pub address: u8,
    pub line: Line,
    /// The reader's own end-of-frame pause (register 537) delays every
    /// answer by that much.
    pub frame_pause: Duration,
    traffic: Traffic,
    /// Marks routine polling in the traffic log.
    pub background: bool,
    last: Instant,
}

impl Client {
    pub fn new(port: Box<dyn Port>, line: Line, address: u8, traffic: Traffic) -> Self {
        Self {
            port,
            address,
            line,
            frame_pause: Duration::ZERO,
            traffic,
            background: false,
            last: Instant::now(),
        }
    }

    pub fn set_line(&mut self, line: Line) -> Result<(), Error> {
        self.port.set_line(&line).map_err(Error::Port)?;
        self.line = line;
        Ok(())
    }

    /// Sends one request PDU and returns the validated answer PDU.
    pub fn transact(&mut self, pdu: &[u8], patience: Patience) -> Result<Vec<u8>, Error> {
        let request = frame(self.address, pdu);
        let gap = self.line.frame_gap();
        let since = self.last.elapsed();
        if since < gap {
            std::thread::sleep(gap - since);
        }
        let expected = expected_len(pdu);
        let wire = self.line.char_time() * (request.len() + expected) as u32;
        let timeout = patience.base() + wire * 3 / 2 + self.frame_pause;
        self.traffic.add(Dir::Tx, &request, self.background);
        let result = self.port.exchange(&request, timeout);
        self.last = Instant::now();
        let answer = match result {
            Ok(a) => a,
            Err(e) => {
                self.traffic.note(&e, self.background);
                return Err(Error::Port(e));
            }
        };
        if answer.is_empty() {
            self.traffic
                .note(crate::i18n::tr("no answer"), self.background);
            return Err(Error::Timeout);
        }
        self.traffic.add(Dir::Rx, &answer, self.background);
        let checked = validate(self.address, pdu, &answer);
        if let Err(e) = &checked
            && !matches!(e, Error::Exception(_))
        {
            self.traffic.note(&e.to_string(), self.background);
        }
        checked
    }

    pub fn read_input(&mut self, start: u16, count: u16) -> Result<Vec<u16>, Error> {
        self.read(FC_READ_INPUT, start, count, Patience::Normal)
    }

    pub fn read_holding(&mut self, start: u16, count: u16) -> Result<Vec<u16>, Error> {
        self.read(FC_READ_HOLDING, start, count, Patience::Normal)
    }

    pub fn read(
        &mut self,
        function: u8,
        start: u16,
        count: u16,
        patience: Patience,
    ) -> Result<Vec<u16>, Error> {
        assert!((1..=MAX_READ).contains(&count));
        let mut pdu = vec![function];
        pdu.extend_from_slice(&start.to_be_bytes());
        pdu.extend_from_slice(&count.to_be_bytes());
        let answer = self.transact(&pdu, patience)?;
        Ok(be_words(&answer[2..]))
    }

    pub fn write_single(&mut self, register: u16, value: u16) -> Result<(), Error> {
        let mut pdu = vec![FC_WRITE_SINGLE];
        pdu.extend_from_slice(&register.to_be_bytes());
        pdu.extend_from_slice(&value.to_be_bytes());
        self.transact(&pdu, Patience::Normal).map(|_| ())
    }

    pub fn write_multiple(&mut self, start: u16, values: &[u16]) -> Result<(), Error> {
        assert!((1..=usize::from(MAX_WRITE)).contains(&values.len()));
        let mut pdu = vec![FC_WRITE_MULTIPLE];
        pdu.extend_from_slice(&start.to_be_bytes());
        pdu.extend_from_slice(&(values.len() as u16).to_be_bytes());
        pdu.push((values.len() * 2) as u8);
        pdu.extend(regs_to_bytes(values));
        self.transact(&pdu, Patience::Normal).map(|_| ())
    }

    /// Function 0x17: the write runs first, then the read.
    pub fn read_write(
        &mut self,
        read_start: u16,
        read_count: u16,
        write_start: u16,
        values: &[u16],
        patience: Patience,
    ) -> Result<Vec<u16>, Error> {
        assert!((1..=MAX_READ).contains(&read_count) && (1..=121).contains(&values.len()));
        let mut pdu = vec![FC_READ_WRITE];
        pdu.extend_from_slice(&read_start.to_be_bytes());
        pdu.extend_from_slice(&read_count.to_be_bytes());
        pdu.extend_from_slice(&write_start.to_be_bytes());
        pdu.extend_from_slice(&(values.len() as u16).to_be_bytes());
        pdu.push((values.len() * 2) as u8);
        pdu.extend(regs_to_bytes(values));
        let answer = self.transact(&pdu, patience)?;
        Ok(be_words(&answer[2..]))
    }

    /// Function 0x2B / MEI 0x0E: the identification objects.
    pub fn device_id(&mut self, code: u8, object: u8) -> Result<Vec<(u8, Vec<u8>)>, Error> {
        let answer = self.transact(
            &[FC_DEVICE_ID, MEI_DEVICE_ID, code, object],
            Patience::Normal,
        )?;
        let mut objects = Vec::new();
        let mut pos = 7;
        for _ in 0..answer[6] {
            let (id, len) = (answer[pos], usize::from(answer[pos + 1]));
            objects.push((id, answer[pos + 2..pos + 2 + len].to_vec()));
            pos += 2 + len;
        }
        Ok(objects)
    }

    /// Asks for the bootloader's INFO block: an answer means the reader
    /// waits in its bootloader, exception 01 that the firmware runs.
    pub fn loader_info(&mut self) -> Result<Vec<u8>, Error> {
        self.transact(&[FC_LOADER_INFO], Patience::Normal)
    }
}

fn be_words(bytes: &[u8]) -> Vec<u16> {
    bytes
        .as_chunks::<2>()
        .0
        .iter()
        .map(|c| u16::from_be_bytes(*c))
        .collect()
}

/// The longest answer a request can get, for the wait.
fn expected_len(pdu: &[u8]) -> usize {
    let count = || usize::from(u16::from_be_bytes([pdu[3], pdu[4]]));
    match pdu[0] {
        FC_READ_HOLDING | FC_READ_INPUT | FC_READ_WRITE if pdu.len() >= 5 => 5 + 2 * count(),
        FC_DEVICE_ID => 256,
        FC_LOADER_INFO => LOADER_INFO_LEN,
        _ => 8,
    }
}

/// Checks an answer frame against the request and returns its PDU.
pub fn validate(address: u8, request: &[u8], answer: &[u8]) -> Result<Vec<u8>, Error> {
    let bad = |text: &str| Err(Error::Frame(text.to_string()));
    let Some(len) = response_len(answer) else {
        return Err(Error::Timeout);
    };
    if answer.len() < len || len < 5 {
        return Err(Error::Timeout);
    }
    if answer.len() > len {
        return bad("extra bytes after the frame");
    }
    let (body, crc) = answer.split_at(len - 2);
    if crc16(body) != u16::from_le_bytes([crc[0], crc[1]]) {
        return bad("CRC mismatch");
    }
    if body[0] != address {
        return bad(&format!("answer from address {}", body[0]));
    }
    let pdu = &body[1..];
    let function = request[0];
    if pdu[0] == function | 0x80 {
        return Err(Error::Exception(pdu[1]));
    }
    if pdu[0] != function {
        return bad(&format!(
            "function {:02X} instead of {function:02X}",
            pdu[0]
        ));
    }
    match function {
        FC_READ_HOLDING | FC_READ_INPUT | FC_READ_WRITE => {
            let count = usize::from(u16::from_be_bytes([request[3], request[4]]));
            if usize::from(pdu[1]) != 2 * count {
                return bad("wrong register count");
            }
        }
        FC_WRITE_SINGLE | FC_WRITE_MULTIPLE => {
            if pdu.len() != 5 || pdu[..5] != request[..5] {
                return bad("the write is not echoed");
            }
        }
        FC_DEVICE_ID if pdu.len() < 7 || pdu[1] != MEI_DEVICE_ID || pdu[2] != request[2] => {
            return bad("identification does not match");
        }
        _ => {}
    }
    Ok(pdu.to_vec())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crc_of_known_frames() {
        // Read input registers 1..1 at 0x5F, as find_odrfid.py sends it.
        let f = frame(0x5F, &[0x04, 0x00, 0x01, 0x00, 0x01]);
        assert_eq!(crc16(&f[..f.len() - 2]), u16::from_le_bytes([f[6], f[7]]));
        // The textbook example: 01 03 00 00 00 0A → C5 CD.
        assert_eq!(
            frame(1, &[3, 0, 0, 0, 0x0A]),
            [1, 3, 0, 0, 0, 0x0A, 0xC5, 0xCD]
        );
    }

    #[test]
    fn lengths() {
        assert_eq!(response_len(&[1]), None);
        assert_eq!(response_len(&[1, 0x84]), Some(5));
        assert_eq!(response_len(&[1, 4]), None);
        assert_eq!(response_len(&[1, 4, 10]), Some(15));
        assert_eq!(response_len(&[1, 6]), Some(8));
        assert_eq!(response_len(&[1, FC_LOADER_INFO]), Some(20));
        assert_eq!(response_len(&[1, FC_LOADER_INFO | 0x80]), Some(5));
        // 2B: header, two objects of 3 and 1 bytes.
        let id = [
            1, 0x2B, 0x0E, 3, 0x83, 0, 0, 2, 0, 3, b'a', b'b', b'c', 1, 1, b'x',
        ];
        assert_eq!(response_len(&id[..9]), None);
        assert_eq!(response_len(&id), Some(id.len() + 2));
    }

    #[test]
    fn answers_are_checked() {
        let read = [FC_READ_INPUT, 0, 1, 0, 2];
        let ok = frame(5, &[FC_READ_INPUT, 4, 0, 1, 0, 2]);
        assert_eq!(validate(5, &read, &ok).unwrap(), [4, 4, 0, 1, 0, 2]);
        assert!(matches!(validate(6, &read, &ok), Err(Error::Frame(_))));
        let mut broken = ok.clone();
        broken[3] ^= 1;
        assert!(matches!(validate(5, &read, &broken), Err(Error::Frame(_))));
        assert_eq!(validate(5, &read, &ok[..4]), Err(Error::Timeout));
        let short = frame(5, &[FC_READ_INPUT, 2, 0, 1]);
        assert!(matches!(validate(5, &read, &short), Err(Error::Frame(_))));
        let exception = frame(5, &[FC_READ_INPUT | 0x80, 2]);
        assert_eq!(validate(5, &read, &exception), Err(Error::Exception(2)));
        let write = [FC_WRITE_SINGLE, 2, 26, 0x53, 0x41];
        assert!(validate(5, &write, &frame(5, &write)).is_ok());
        assert!(validate(5, &write, &frame(5, &[FC_WRITE_SINGLE, 2, 26, 0, 0])).is_err());
    }

    #[test]
    fn line_timing() {
        let l = Line::default();
        assert_eq!(l.label(), "115200 8N1");
        // 10 bits at 115200 baud ≈ 86.8 µs.
        assert_eq!(l.char_time().as_micros(), 86);
        assert_eq!(l.frame_gap(), Duration::from_micros(1750));
        let slow = Line {
            baud: 9600,
            parity: Parity::Even,
            stop: StopBits::OneAndHalf,
        };
        assert_eq!(slow.label(), "9600 8E1.5");
        // 11.5 bits → 1.198 ms; 3.5 characters ≈ 4.19 ms.
        assert_eq!(slow.char_time().as_micros(), 1197);
        assert_eq!(slow.frame_gap().as_micros(), 4192);
        assert!(slow.valid() && !Line { baud: 31250, ..l }.valid());
    }

    #[test]
    fn register_packing() {
        assert_eq!(bytes_to_regs(&[0xA1, 0xB2, 0xC3]), [0xA1B2, 0xC300]);
        assert_eq!(regs_to_bytes(&[0xA1B2, 0xC3D4]), [0xA1, 0xB2, 0xC3, 0xD4]);
    }
}
