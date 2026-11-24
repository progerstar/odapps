//! An in-memory ODRFID-485 with firmware 4.0 behind a fake serial port: the
//! engine tests use it, and debug builds run on it with `ODRFID_DEMO=1` (no
//! hardware needed to work on the window).
//!
//! It follows `MODBUS_MAP.md` closely enough for this tool: the registers
//! it reads and writes, the bus settings in RAM with apply and save, the
//! command block with its sequence number, and a Classic 1K and an NTAG215
//! that come and go in demo mode.

use std::collections::VecDeque;
use std::sync::{Arc, Mutex, MutexGuard};
use std::time::{Duration, Instant};

use crate::modbus::{self, FC_LOADER_INFO, Line, Port, bytes_to_regs, crc16, regs_to_bytes};
use crate::reader::{BusSettings, FACTORY_ADDRESS, GAINS, MODEL_ID, holding};
use crate::serial::{Bus, OpenError, PortInfo};

const EXC_FUNCTION: u8 = 1;
const EXC_ADDRESS: u8 = 2;
const EXC_VALUE: u8 = 3;
const EXC_FAILURE: u8 = 4;

#[derive(Clone)]
pub struct FakeCard {
    pub kind: u8,
    pub uid: Vec<u8>,
    pub sak: u8,
    pub block_size: u8,
    pub memory: Vec<Vec<u8>>,
}

impl FakeCard {
    pub fn classic() -> Self {
        let mut memory = vec![vec![0u8; 16]; 64];
        memory[0] = vec![
            0xA1, 0xB2, 0xC3, 0xD4, 0x04, 0x08, 0x04, 0x00, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
            0x68, 0x69,
        ];
        memory[4][..5].copy_from_slice(b"Hello");
        Self {
            kind: 0,
            uid: vec![0xA1, 0xB2, 0xC3, 0xD4],
            sak: 0x08,
            block_size: 16,
            memory,
        }
    }

    pub fn ntag215() -> Self {
        Self {
            kind: 24,
            uid: vec![0x04, 0x5E, 0x21, 0x7A, 0x93, 0x10, 0x80],
            sak: 0x00,
            block_size: 4,
            memory: vec![vec![0u8; 4]; 135],
        }
    }

    fn info(&self) -> Vec<u8> {
        let blocks = self.memory.len() as u16;
        let mut d = vec![self.kind, self.sak, self.block_size];
        d.extend_from_slice(&blocks.to_be_bytes());
        d.push(self.uid.len() as u8);
        d.extend_from_slice(&self.uid);
        d
    }
}

pub struct FakeReader {
    pub address: u8,
    /// The line the reader listens on now.
    pub line: Line,
    pub serial: u64,
    pub version: u16,
    /// Firmware 3.x: the AT tunnel instead of the register model.
    pub legacy: bool,
    /// Holding 512..537 in RAM and in EEPROM.
    pub settings: [u16; 26],
    pub saved: [u16; 26],
    pub save_fails: bool,
    /// Holding 256..263.
    pub indication: [u16; 8],
    pub boot_count: u16,
    pub odometer: u32,
    pub field: Vec<FakeCard>,
    /// Index into `field` of the selected card.
    pub selected: Option<usize>,
    pub antenna: bool,
    pub last_event: u16,
    pub queue: VecDeque<(u16, FakeCard)>,
    pub overflow: bool,
    pub autoread: (u16, u16, Vec<u8>),
    pub cmd_last: Option<Vec<u16>>,
    pub cmd_response: Vec<u16>,
    pub enumerating: Option<Instant>,
    /// Restarted into the bootloader: it answers INFO (0x41) and refuses
    /// everything else until power-cycled.
    pub bootloader: bool,
    /// Powered off: nothing answers.
    pub off: bool,
    /// The next requests that do not reach it.
    pub lose_requests: u32,
    /// The next answers lost on the line (the requests did run).
    pub lose_answers: u32,
    /// "Apply the bus settings" does nothing.
    pub ignore_apply: bool,
    /// After "apply" the reader goes silent (a line that cannot carry the
    /// new settings).
    pub mute_after_apply: bool,
    /// Requests seen: (function, start register).
    pub requests: Vec<(u8, u16)>,
}

impl FakeReader {
    pub fn new(address: u8, serial: u64) -> Self {
        let factory = BusSettings::factory();
        let mut settings = [0u16; 26];
        settings[..8].copy_from_slice(&[1, 200, 33, 1, 1, 0, 0, 0]);
        settings[8..21].fill(0xFFFF);
        settings[21..].copy_from_slice(&factory.regs());
        settings[21] = u16::from(address);
        Self {
            address,
            line: Line::default(),
            serial,
            version: 0x0400,
            legacy: false,
            settings,
            saved: settings,
            save_fails: false,
            indication: [0, 0, 0, 0, 200, 200, 0, 0],
            boot_count: 1,
            odometer: 4 * 1440 + 125,
            field: Vec::new(),
            selected: None,
            antenna: true,
            last_event: 0,
            queue: VecDeque::new(),
            overflow: false,
            autoread: (0, 0, Vec::new()),
            cmd_last: None,
            cmd_response: vec![0, 255, 0, 0, 0],
            enumerating: None,
            bootloader: false,
            off: false,
            lose_requests: 0,
            lose_answers: 0,
            ignore_apply: false,
            mute_after_apply: false,
            requests: Vec::new(),
        }
    }

    fn scan_on(&self) -> bool {
        self.settings[0] != 0
    }

    /// Puts a card into the field, as the autoscan would find it.
    pub fn insert(&mut self, card: FakeCard) {
        self.field = vec![card.clone()];
        if self.scan_on() {
            self.selected = Some(0);
            self.last_event = self.last_event.wrapping_add(1).max(1);
            if self.queue.len() == 8 {
                self.queue.pop_front();
                self.overflow = true;
            }
            self.queue.push_back((self.last_event, card.clone()));
            let (first, count) = (usize::from(self.settings[5]), usize::from(self.settings[6]));
            if count > 0 {
                let data: Vec<u8> = card
                    .memory
                    .iter()
                    .skip(first)
                    .take(count)
                    .flatten()
                    .copied()
                    .take(64)
                    .collect();
                let error = if first >= card.memory.len() {
                    0xFFFF
                } else {
                    0
                };
                self.autoread = (self.last_event, error, data);
            }
            if self.settings[3] != 0 {
                self.indication[0] = 1;
            }
        }
    }

    pub fn remove(&mut self) {
        self.field.clear();
        if self.scan_on() {
            self.selected = None;
            if self.settings[3] != 0 {
                self.indication[0] = 0;
            }
        }
    }

    fn restart(&mut self) {
        self.settings = self.saved;
        let bus = &self.saved[21..];
        self.address = bus[0] as u8;
        self.line = Line {
            baud: u32::from(bus[1]) * 100,
            parity: modbus::Parity::from_code(bus[2]).unwrap_or_default(),
            stop: modbus::StopBits::from_code(bus[3]).unwrap_or_default(),
        };
        self.boot_count = self.boot_count.wrapping_add(1).max(1);
        self.queue.clear();
        self.overflow = false;
        self.last_event = 0;
        self.cmd_last = None;
        self.cmd_response = vec![0, 255, 0, 0, 0];
        self.indication = [0, 0, 0, 0, 200, 200, 0, 0];
    }

    fn input(&self, reg: u16) -> Option<u16> {
        if self.legacy {
            return match reg {
                0 => Some(0),
                1 => Some(u16::from(!self.field.is_empty())),
                2..=5 => Some(0),
                _ => None,
            };
        }
        let card = self.selected.and_then(|i| self.field.get(i));
        let uid = card
            .map(|c| {
                let mut b = c.uid.clone();
                b.resize(10, 0);
                bytes_to_regs(&b)
            })
            .unwrap_or_else(|| vec![0; 5]);
        Some(match reg {
            1 => u16::from(card.is_some() && self.scan_on()),
            2 => (self.odometer >> 16) as u16,
            3 => self.odometer as u16,
            4 | 5 => 0,
            6 => self.boot_count,
            7 => self.last_event,
            8 => self.queue.len() as u16,
            9 => u16::from(self.overflow),
            10 => card.map_or(255, |c| u16::from(c.kind)),
            11 => card.map_or(0, |c| c.uid.len() as u16),
            12..=16 => uid[usize::from(reg - 12)],
            17 => card.map_or(0, |c| u16::from(c.sak)),
            18 => card.map_or(0, |c| c.memory.len() as u16),
            19 => card.map_or(0, |c| u16::from(c.block_size)),
            20 => 0,
            21 => self.indication[3],
            22 => u16::from(self.indication[0] != 0),
            23 => self.indication[1],
            24 => u16::from(self.indication[2] != 0 || self.indication[6] != 0),
            256 => MODEL_ID,
            257 => self.version,
            258 => (self.serial >> 32) as u16,
            259 => (self.serial >> 16) as u16,
            260 => self.serial as u16,
            512 => self.queue.len() as u16,
            513 => u16::from(self.overflow),
            514..=585 => {
                let i = usize::from(reg - 514);
                match self.queue.get(i / 9) {
                    None => 0,
                    Some((n, c)) => {
                        let mut b = c.uid.clone();
                        b.resize(10, 0);
                        let uid = bytes_to_regs(&b);
                        match i % 9 {
                            0 => *n,
                            1 => u16::from(c.kind),
                            2 => u16::from(c.sak),
                            3 => c.uid.len() as u16,
                            k => uid[k - 4],
                        }
                    }
                }
            }
            586 => self.autoread.0,
            587 => self.autoread.2.len() as u16,
            588 => self.autoread.1,
            589..=620 => {
                let mut b = self.autoread.2.clone();
                b.resize(64, 0);
                bytes_to_regs(&b)[usize::from(reg - 589)]
            }
            _ => return None,
        })
    }

    fn holding(&self, reg: u16) -> Option<u16> {
        if self.legacy {
            return (reg <= 143).then_some(0);
        }
        Some(match reg {
            127..=129 | 538 => 0,
            256..=263 => self.indication[usize::from(reg - 256)],
            512..=537 => self.settings[usize::from(reg - 512)],
            1024..=1148 => self
                .cmd_response
                .get(usize::from(reg - 1024))
                .copied()
                .unwrap_or(0),
            _ => return None,
        })
    }

    fn write(&mut self, reg: u16, value: u16) -> Result<(), u8> {
        if self.enumerating.is_some() && ((512..533).contains(&reg) || reg == 143) {
            return Err(6);
        }
        let flag = |v: u16| if v > 1 { Err(EXC_VALUE) } else { Ok(v) };
        match reg {
            127 => self.restart(),
            128 => self.bootloader = true,
            143 => {
                if value == holding::FACTORY_RESET_MAGIC {
                    let mut fresh = FakeReader::new(FACTORY_ADDRESS, self.serial);
                    fresh.boot_count = self.boot_count;
                    self.saved = fresh.saved;
                    self.restart();
                }
            }
            256 => {
                self.indication[0] = if value > 2 {
                    return Err(EXC_VALUE);
                } else {
                    value
                }
            }
            257..=259 => self.indication[usize::from(reg - 256)] = flag(value)?,
            260 | 261 => {
                if value == 0 {
                    return Err(EXC_VALUE);
                }
                self.indication[usize::from(reg - 256)] = value;
            }
            262 => self.indication[6] = value,
            263 => {
                // Remove events up to `value`.
                if value != 0 && self.queue.iter().any(|(n, _)| *n == value) {
                    while let Some((n, _)) = self.queue.front() {
                        let n = *n;
                        self.queue.pop_front();
                        self.overflow = false;
                        self.indication[7] = n;
                        if n == value {
                            break;
                        }
                    }
                }
            }
            512 | 515 | 516 | 519 => {
                self.settings[usize::from(reg - 512)] = flag(value)?;
                if reg == 515 {
                    self.indication[0] = 0;
                }
                if reg == 512 && value == 1 {
                    self.selected = (!self.field.is_empty()).then_some(0);
                }
            }
            513 => self.settings[1] = value,
            514 => {
                if value > 255 {
                    return Err(EXC_VALUE);
                }
                self.settings[2] = GAINS
                    .iter()
                    .map(|&g| u16::from(g))
                    .find(|&g| g >= value)
                    .unwrap_or(48);
            }
            517 => {
                self.settings[5] = if value > 255 {
                    return Err(EXC_VALUE);
                } else {
                    value
                }
            }
            518 => {
                self.settings[6] = if value > 16 {
                    return Err(EXC_VALUE);
                } else {
                    value
                }
            }
            520..=532 => self.settings[usize::from(reg - 512)] = value,
            533 => {
                self.settings[21] = if value == 0 || value > 247 {
                    return Err(EXC_VALUE);
                } else {
                    value
                }
            }
            534 => {
                self.settings[22] = if !(5..=20_000).contains(&value) {
                    return Err(EXC_VALUE);
                } else {
                    value
                }
            }
            535 => {
                self.settings[23] = if value > 2 {
                    return Err(EXC_VALUE);
                } else {
                    value
                }
            }
            536 => {
                self.settings[24] = if matches!(value, 10 | 15 | 20) {
                    value
                } else {
                    return Err(EXC_VALUE);
                }
            }
            537 => self.settings[25] = value,
            538 => match value {
                holding::SAVE => {
                    if self.save_fails {
                        return Err(EXC_FAILURE);
                    }
                    self.saved = self.settings;
                }
                holding::APPLY_BUS => {} // after the answer, see `exchange`
                _ => return Err(EXC_VALUE),
            },
            _ => return Err(EXC_ADDRESS),
        }
        Ok(())
    }

    /// Function 16 or 23 at register 768.
    fn command(&mut self, regs: &[u16]) -> Result<(), u8> {
        if regs.len() < 5 {
            return Err(EXC_VALUE);
        }
        let len = usize::from(regs[4]);
        if len > 32 || regs[1] > 255 || regs.len() < 5 + len.div_ceil(2) {
            return Err(EXC_VALUE);
        }
        let request = regs[..5 + len.div_ceil(2)].to_vec();
        if self.cmd_last.as_ref() == Some(&request) {
            return Ok(());
        }
        if self.enumerating.is_some() {
            return Err(6);
        }
        self.cmd_last = Some(request.clone());
        let data = regs_to_bytes(&request[5..])[..len].to_vec();
        let (seq, op, p0) = (regs[0], regs[1] as u8, regs[2]);
        let (status, cme, answer) = self.run(op, p0, &data);
        self.respond(seq, status, cme, &answer);
        Ok(())
    }

    fn respond(&mut self, seq: u16, status: u16, cme: u32, data: &[u8]) {
        let mut r = vec![
            seq,
            status,
            (cme >> 16) as u16,
            cme as u16,
            data.len() as u16,
        ];
        r.extend(bytes_to_regs(data));
        self.cmd_response = r;
    }

    fn run(&mut self, op: u8, p0: u16, data: &[u8]) -> (u16, u32, Vec<u8>) {
        let rejected = (1, 0, Vec::new());
        let scan_allowed = matches!(op, 0x01..=0x03) || (op == 0x27 && p0 < 2);
        if self.scan_on() && !scan_allowed {
            return rejected;
        }
        let no_data = data.is_empty();
        let selected = self.selected.and_then(|i| self.field.get(i)).cloned();
        let timeout = (3, 0x0100, Vec::new());
        match op {
            0x01 if no_data => (0, 0, b"ODRFID (ModBus) 4.0F Nov 23 2026".to_vec()),
            0x02 if no_data => (0, 0, vec![0x30, 0x31, 0x32, 0x33, 4, 5, 6, 7, 8, 9, 10, 11]),
            0x03 if no_data => (0, 0, vec![0xC9, 0xC7, 0xAE, 0x2A]),
            0x20 if no_data => {
                self.enumerating = Some(Instant::now() + Duration::from_millis(250));
                (2, 0, Vec::new())
            }
            0x21 | 0x26 if no_data => match (op, &selected) {
                (0x26, Some(c)) => (0, 0, c.info()),
                (0x21, _) if !self.field.is_empty() && self.antenna => {
                    self.selected = Some(0);
                    (0, 0, self.field[0].info())
                }
                (0x26, None) => rejected,
                _ => timeout,
            },
            0x22 if no_data => timeout,
            0x23 | 0x24 if (4..=10).contains(&data.len()) => {
                match self.field.iter().position(|c| c.uid == data) {
                    Some(i) if self.antenna => {
                        if op == 0x24 {
                            self.selected = Some(i);
                            (0, 0, self.field[i].info())
                        } else {
                            (0, 0, Vec::new())
                        }
                    }
                    _ => timeout,
                }
            }
            0x25 if no_data => {
                self.selected = None;
                (0, 0, Vec::new())
            }
            0x27 if no_data && p0 <= 2 => {
                if p0 < 2 {
                    self.antenna = p0 == 1;
                }
                (0, 0, vec![u8::from(self.antenna)])
            }
            0x30 if no_data => match &selected {
                Some(c) if usize::from(p0) < c.memory.len() => {
                    (0, 0, c.memory[usize::from(p0)].clone())
                }
                Some(_) | None => rejected,
            },
            0x32 => match self.selected {
                Some(i)
                    if usize::from(p0) < self.field[i].memory.len()
                        && (data.len() == usize::from(self.field[i].block_size)
                            || data.len() == 16) =>
                {
                    let size = usize::from(self.field[i].block_size);
                    self.field[i].memory[usize::from(p0)] = data[..size].to_vec();
                    (0, 0, Vec::new())
                }
                _ => rejected,
            },
            _ => rejected,
        }
    }

    /// Finishes an enumeration once its time is up.
    fn advance(&mut self, now: Instant) {
        if let Some(until) = self.enumerating
            && now >= until
        {
            self.enumerating = None;
            let mut list = Vec::new();
            for c in &self.field {
                list.push(c.uid.len() as u8);
                list.extend_from_slice(&c.uid);
                list.push(c.sak);
            }
            let seq = self.cmd_response[0];
            self.respond(seq, 0, 0, &list);
        }
    }

    /// Answers one request PDU.
    fn answer(&mut self, pdu: &[u8]) -> Vec<u8> {
        let function = pdu[0];
        let word = |i: usize| u16::from_be_bytes([pdu[i], pdu[i + 1]]);
        let exception = |code: u8| vec![function | 0x80, code];
        let words = |r: &[u16]| {
            let mut out = vec![function, (r.len() * 2) as u8];
            out.extend(regs_to_bytes(r));
            out
        };
        self.requests
            .push((function, if pdu.len() >= 3 { word(1) } else { 0 }));
        if self.bootloader {
            // Protocol 1, a 12-byte MCU UID, board 1, 112 pages, flags
            // "application valid" and "entered on request".
            let mut info = vec![FC_LOADER_INFO, 1];
            info.extend_from_slice(&self.serial.to_le_bytes());
            info.extend_from_slice(&[0, 0, 0, 0, 1, 0x70, 0x03]);
            return match function {
                FC_LOADER_INFO if pdu.len() == 1 => info,
                _ => exception(EXC_FUNCTION),
            };
        }
        match function {
            0x03 | 0x04 if pdu.len() == 5 => {
                let (start, count) = (word(1), word(3));
                if !(1..=125).contains(&count) {
                    return exception(EXC_VALUE);
                }
                let read = |r| {
                    if function == 0x04 {
                        self.input(r)
                    } else {
                        self.holding(r)
                    }
                };
                match (start..start + count)
                    .map(read)
                    .collect::<Option<Vec<u16>>>()
                {
                    Some(r) => words(&r),
                    None => exception(EXC_ADDRESS),
                }
            }
            0x06 if pdu.len() == 5 && !self.legacy => {
                if (768..=788).contains(&word(1)) {
                    return exception(EXC_ADDRESS);
                }
                match self.write(word(1), word(3)) {
                    Ok(()) => pdu.to_vec(),
                    Err(e) => exception(e),
                }
            }
            0x10 | 0x17 if !self.legacy => {
                let (head, read) = if function == 0x17 {
                    (5, Some((word(1), word(3))))
                } else {
                    (1, None)
                };
                let (start, count) = (word(head), word(head + 2));
                let values: Vec<u16> = pdu[head + 5..]
                    .as_chunks::<2>()
                    .0
                    .iter()
                    .map(|c| u16::from_be_bytes(*c))
                    .collect();
                if values.len() != usize::from(count) {
                    return exception(EXC_VALUE);
                }
                if let Some((rs, rc)) = read
                    && (rs..rs + rc).any(|r| self.holding(r).is_none())
                {
                    return exception(EXC_ADDRESS);
                }
                let result = if start == holding::CMD_REQUEST {
                    if values.len() > 21 {
                        Err(EXC_ADDRESS)
                    } else {
                        self.command(&values)
                    }
                } else {
                    values
                        .iter()
                        .enumerate()
                        .try_for_each(|(i, &v)| self.write(start + i as u16, v))
                };
                if let Err(e) = result {
                    return exception(e);
                }
                match read {
                    Some((rs, rc)) => {
                        let r: Vec<u16> = (rs..rs + rc).filter_map(|r| self.holding(r)).collect();
                        words(&r)
                    }
                    None => pdu[..5].to_vec(),
                }
            }
            0x2B if !self.legacy && pdu.len() == 4 && pdu[1] == 0x0E => {
                let serial = format!("{:012X}", self.serial);
                let objects: [(u8, &[u8]); 5] = [
                    (0, b"OpenDev"),
                    (1, b"ODRFID (ModBus)"),
                    (2, b"4.0F"),
                    (0x80, serial.as_bytes()),
                    (0x81, b"Nov 23 2026"),
                ];
                let mut out = vec![0x2B, 0x0E, pdu[2], 0x83, 0, 0, objects.len() as u8];
                for (id, value) in objects {
                    out.push(id);
                    out.push(value.len() as u8);
                    out.extend_from_slice(value);
                }
                out
            }
            _ => exception(EXC_FUNCTION),
        }
    }
}

#[derive(Default)]
pub struct FakeState {
    pub ports: Vec<String>,
    pub readers: Vec<FakeReader>,
    pub denied: bool,
    /// The fastest line the adapter's driver accepts.
    pub adapter_max_baud: Option<u32>,
    /// Demo mode: cards come and go.
    pub cycle: bool,
    started: Option<Instant>,
}

impl FakeState {
    fn tick(&mut self, now: Instant) {
        let started = *self.started.get_or_insert(now);
        for r in &mut self.readers {
            r.advance(now);
        }
        if !self.cycle {
            return;
        }
        // Nothing, the Classic card, nothing, the NTAG: 4 s each.
        let phase = (now - started).as_secs() / 4 % 4;
        if let Some(r) = self.readers.first_mut() {
            let want = match phase {
                1 => Some(0u8),
                3 => Some(24),
                _ => None,
            };
            let have = r.field.first().map(|c| c.kind);
            if want != have {
                match want {
                    Some(0) => r.insert(FakeCard::classic()),
                    Some(_) => r.insert(FakeCard::ntag215()),
                    None => r.remove(),
                }
            }
        }
    }
}

#[derive(Clone, Default)]
pub struct FakeBus(pub Arc<Mutex<FakeState>>);

impl FakeBus {
    /// One port with readers at these addresses, the first with a card.
    pub fn with_readers(addresses: &[u8]) -> Self {
        let bus = Self::default();
        {
            let mut st = bus.state();
            st.ports = vec!["/dev/ttyDEMO0".into()];
            st.readers = addresses
                .iter()
                .enumerate()
                .map(|(i, &a)| FakeReader::new(a, 0x1121_3141_ABCD + i as u64))
                .collect();
        }
        bus
    }

    /// Debug builds' demo: two readers on `port`. Cards come and go, or with
    /// `still` a Classic 1K stays in the field.
    pub fn demo(port: &str, still: bool) -> Self {
        let bus = Self::with_readers(&[FACTORY_ADDRESS, 17]);
        let mut st = bus.state();
        st.cycle = !still;
        st.ports = vec![port.to_string()];
        if still {
            st.readers[0].insert(FakeCard::classic());
        }
        drop(st);
        bus
    }

    pub fn state(&self) -> MutexGuard<'_, FakeState> {
        self.0.lock().unwrap_or_else(|e| e.into_inner())
    }

    #[cfg(test)]
    pub fn with_reader<R>(&self, i: usize, f: impl FnOnce(&mut FakeReader) -> R) -> R {
        f(&mut self.state().readers[i])
    }
}

impl Bus for FakeBus {
    fn ports(&mut self) -> Vec<PortInfo> {
        self.state()
            .ports
            .iter()
            .map(|p| PortInfo {
                name: p.clone(),
                description: "Demo RS-485 (0000:0000)".into(),
            })
            .collect()
    }

    fn open(&mut self, name: &str, line: &Line) -> Result<Box<dyn Port>, OpenError> {
        let st = self.state();
        if st.denied {
            return Err(OpenError::AccessDenied("Permission denied".into()));
        }
        if !st.ports.iter().any(|p| p == name) {
            return Err(OpenError::Missing(format!("{name}: no such port")));
        }
        Ok(Box::new(FakePort {
            bus: self.clone(),
            name: name.into(),
            line: *line,
        }))
    }
}

struct FakePort {
    bus: FakeBus,
    name: String,
    line: Line,
}

impl Port for FakePort {
    fn exchange(&mut self, frame: &[u8], _timeout: Duration) -> Result<Vec<u8>, String> {
        let mut st = self.bus.state();
        if !st.ports.contains(&self.name) {
            return Err("Input/output error".into());
        }
        st.tick(Instant::now());
        let n = frame.len();
        if n < 4 || crc16(&frame[..n - 2]) != u16::from_le_bytes([frame[n - 2], frame[n - 1]]) {
            return Ok(Vec::new());
        }
        let (address, pdu) = (frame[0], &frame[1..n - 2]);
        let line = self.line;
        let Some(reader) = st
            .readers
            .iter_mut()
            .find(|r| r.address == address && r.line == line && !r.off)
        else {
            return Ok(Vec::new());
        };
        if reader.lose_requests > 0 {
            reader.lose_requests -= 1;
            return Ok(Vec::new());
        }
        let answer = reader.answer(pdu);
        // "Apply the bus settings" takes effect after the answer.
        if pdu.len() == 5
            && !reader.ignore_apply
            && pdu[0] == 0x06
            && u16::from_be_bytes([pdu[1], pdu[2]]) == holding::SETTINGS_COMMAND
            && u16::from_be_bytes([pdu[3], pdu[4]]) == holding::APPLY_BUS
        {
            let bus = reader.settings[21..].to_vec();
            reader.address = bus[0] as u8;
            reader.line = Line {
                baud: u32::from(bus[1]) * 100,
                parity: modbus::Parity::from_code(bus[2]).unwrap_or_default(),
                stop: modbus::StopBits::from_code(bus[3]).unwrap_or_default(),
            };
            reader.off = reader.mute_after_apply;
        }
        if reader.lose_answers > 0 {
            reader.lose_answers -= 1;
            return Ok(Vec::new());
        }
        Ok(modbus::frame(address, &answer))
    }

    fn set_line(&mut self, line: &Line) -> Result<(), String> {
        if self
            .bus
            .state()
            .adapter_max_baud
            .is_some_and(|max| line.baud > max)
        {
            return Err(format!("{}: Invalid argument", line.baud));
        }
        self.line = *line;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::modbus::{Client, Error};
    use crate::reader::input;
    use crate::traffic::Traffic;

    #[test]
    fn answers_like_the_emulator() {
        let mut bus = FakeBus::with_readers(&[95]);
        bus.with_reader(0, |r| r.insert(FakeCard::classic()));
        let port = bus.open("/dev/ttyDEMO0", &Line::default()).unwrap();
        let mut c = Client::new(port, Line::default(), 95, Traffic::default());
        assert_eq!(
            c.read_input(input::IDENTITY, 5).unwrap(),
            [0x0485, 0x0400, 0x1121, 0x3141, 0xABCD]
        );
        let status = c.read_input(1, 24).unwrap();
        assert_eq!(&status[9..13], [0, 4, 0xA1B2, 0xC3D4]);
        assert_eq!(c.read_input(0, 1), Err(Error::Exception(2)));
        assert_eq!(c.write_single(518, 17), Err(Error::Exception(3)));
        c.write_single(514, 30).unwrap();
        assert_eq!(c.read_holding(514, 1).unwrap(), [33]);
        let ids = c.device_id(3, 0).unwrap();
        assert_eq!(ids[3], (0x80, b"11213141ABCD".to_vec()));
        c.address = 7;
        assert_eq!(c.read_input(1, 1), Err(Error::Timeout));
    }
}
