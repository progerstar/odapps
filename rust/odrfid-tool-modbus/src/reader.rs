//! The register model of ODRFID-485 firmware 4.x (`MODBUS_MAP.md` of
//! `opendev/odrfid-rs485`): identification, state, settings, card events and
//! the command block.

use crate::i18n::{tr, trf};
use crate::modbus::{Line, Parity, StopBits, bytes_to_regs, regs_to_bytes};

/// The address of a new reader and after a factory reset (register 143);
/// firmware 3.x used 95.
pub const FACTORY_ADDRESS: u8 = 1;
pub const MODEL_ID: u16 = 0x0485;

/// Input registers.
pub mod input {
    /// 1..24: card in the field, odometer, faults, the current card, I/O.
    pub const STATUS: u16 = 1;
    pub const STATUS_COUNT: u16 = 24;
    /// 25..29: the power-on self-test; builds before Nov 25 2026 answer
    /// exception 02.
    pub const SELF_TEST: u16 = 25;
    pub const SELF_TEST_COUNT: u16 = 5;
    /// 256..260: model, firmware version, serial number.
    pub const IDENTITY: u16 = 256;
    pub const IDENTITY_COUNT: u16 = 5;
    /// 512..620: the event queue and the autoread data.
    pub const EVENTS: u16 = 512;
    pub const EVENTS_COUNT: u16 = 109;
    /// The 3.x AT reply size; 4.x answers exception 02.
    pub const LEGACY_BUFFER: u16 = 0;
}

/// Holding registers.
pub mod holding {
    pub const REBOOT: u16 = 127;
    pub const BOOTLOADER: u16 = 128;
    pub const FACTORY_RESET: u16 = 143;
    pub const FACTORY_RESET_MAGIC: u16 = 0x4242;
    /// 256..263: LED1, LED2, beeper, OUT, beep on/off ms, beep count, event ack.
    pub const INDICATION: u16 = 256;
    pub const INDICATION_COUNT: u16 = 8;
    pub const LED1: u16 = 256;
    pub const LED2: u16 = 257;
    pub const BEEPER: u16 = 258;
    pub const OUT: u16 = 259;
    pub const BEEP_ON_MS: u16 = 260;
    pub const QUEUE_ACK: u16 = 263;
    /// 512..537: settings; 512..532 card, 533..537 bus.
    pub const SETTINGS: u16 = 512;
    pub const SETTINGS_COUNT: u16 = 26;
    pub const SCAN: u16 = 512;
    pub const BUS: u16 = 533;
    pub const SETTINGS_COMMAND: u16 = 538;
    pub const SAVE: u16 = 0x5341;
    pub const APPLY_BUS: u16 = 0x4150;
    pub const CMD_REQUEST: u16 = 768;
    pub const CMD_RESPONSE: u16 = 1024;
    /// Header and 240 data bytes.
    pub const CMD_RESPONSE_COUNT: u16 = 125;
}

// ---- identification --------------------------------------------------------

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Identity {
    pub model: u16,
    pub major: u8,
    pub minor: u8,
    /// 48 bits, printed as 12 hex digits as on the label.
    pub serial: u64,
}

impl Identity {
    /// Input registers 256..260.
    pub fn parse(regs: &[u16]) -> Option<Self> {
        let r: &[u16; 5] = regs.get(..5)?.try_into().ok()?;
        Some(Self {
            model: r[0],
            major: (r[1] >> 8) as u8,
            minor: r[1] as u8,
            serial: (u64::from(r[2]) << 32) | (u64::from(r[3]) << 16) | u64::from(r[4]),
        })
    }

    pub fn version(&self) -> String {
        format!("{}.{}", self.major, self.minor)
    }

    pub fn serial_text(&self) -> String {
        format!("{:012X}", self.serial)
    }

    pub fn is_odrfid(&self) -> bool {
        self.model == MODEL_ID
    }

    /// This tool speaks the 4.x register model.
    pub fn supported(&self) -> bool {
        self.is_odrfid() && self.major == 4
    }
}

// ---- state -----------------------------------------------------------------

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Card {
    pub kind: u8,
    pub uid: Vec<u8>,
    pub sak: u8,
    /// 0: the type has no blocks.
    pub blocks: u16,
    pub block_size: u8,
}

impl Card {
    pub fn uid_text(&self) -> String {
        hex_compact(&self.uid)
    }
}

/// Input registers 1..24.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Live {
    pub card_present: bool,
    /// Whole minutes of operation.
    pub odometer_min: u32,
    pub odometer_fault: bool,
    /// The MFRC522 did not initialise.
    pub rfid_fault: bool,
    /// Firmware start number; 0 when it could not be stored.
    pub boot_count: u16,
    pub last_event: u16,
    pub queue: u16,
    pub overflow: bool,
    /// The selected card: the one in the field under autoscan, otherwise
    /// the one the last command selected.
    pub card: Option<Card>,
    pub input: bool,
    pub output: bool,
    pub led1: bool,
    pub led2: bool,
    pub beeper: bool,
}

impl Live {
    pub fn parse(r: &[u16]) -> Option<Self> {
        if r.len() < usize::from(input::STATUS_COUNT) {
            return None;
        }
        // r[i] is input register i + 1.
        let uid_len = usize::from(r[10]).min(10);
        let card = (r[9] != 255 && uid_len > 0).then(|| Card {
            kind: r[9] as u8,
            uid: regs_to_bytes(&r[11..16])[..uid_len].to_vec(),
            sak: r[16] as u8,
            blocks: r[17],
            block_size: r[18] as u8,
        });
        Some(Self {
            card_present: r[0] != 0,
            odometer_min: (u32::from(r[1]) << 16) | u32::from(r[2]),
            odometer_fault: r[3] != 0,
            rfid_fault: r[4] != 0,
            boot_count: r[5],
            last_event: r[6],
            queue: r[7],
            overflow: r[8] != 0,
            card,
            input: r[19] != 0,
            output: r[20] != 0,
            led1: r[21] != 0,
            led2: r[22] != 0,
            beeper: r[23] != 0,
        })
    }
}

/// Input registers 25..29: what the firmware found at its last start.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SelfTest {
    /// 0: no fault; 1: P01, the MFRC522; 2: P02, the EEPROM write. The
    /// lower code when both failed.
    pub code: u16,
    /// Checks run and failed: bit 0 the MFRC522, bit 1 the EEPROM write.
    pub done: u8,
    pub failed: u8,
    /// The MFRC522 VersionReg, read twice.
    pub chip_version: [u8; 2],
    /// VDDA in mV; 0: not measured.
    pub vdda_mv: u16,
    /// RCC_CSR bits 24..31 before this start.
    pub reset: u8,
}

/// Input 29 bits, lowest first, as the MODBUS_MAP table names them.
pub const RESET_CAUSES: [&str; 8] = [
    "firewall",
    "option bytes loaded",
    "NRST pin",
    "power on or brownout",
    "software restart",
    "watchdog",
    "window watchdog",
    "low-power mode exit",
];

impl SelfTest {
    pub fn parse(r: &[u16]) -> Option<Self> {
        let r: &[u16; 5] = r.get(..5)?.try_into().ok()?;
        Some(Self {
            code: r[0],
            done: (r[1] >> 8) as u8,
            failed: r[1] as u8,
            chip_version: [(r[2] >> 8) as u8, r[2] as u8],
            vdda_mv: r[3],
            reset: r[4] as u8,
        })
    }

    pub fn passed(&self) -> bool {
        self.code == 0 && self.failed == 0
    }

    /// `P01 MFRC522, P02 EEPROM write` for the failed checks.
    pub fn faults(&self) -> String {
        let mut f = Vec::new();
        if self.code == 1 || self.failed & 1 != 0 {
            f.push(format!("P01 {}", tr("MFRC522 did not start")));
        }
        if self.code == 2 || self.failed & 2 != 0 {
            f.push(format!("P02 {}", tr("EEPROM write failed")));
        }
        if f.is_empty() {
            f.push(format!("P{:02}", self.code));
        }
        f.join(", ")
    }

    /// `0x92` (NXP MFRC522 2.0), or both reads when they differed.
    pub fn chip_text(&self) -> String {
        let [a, b] = self.chip_version;
        let name = match a {
            0x91 => " (NXP 1.0)",
            0x92 => " (NXP 2.0)",
            _ => "",
        };
        if a == b {
            format!("0x{a:02X}{name}")
        } else {
            format!("0x{a:02X} / 0x{b:02X}")
        }
    }

    /// `3283 mV`, or `None` when it was not measured.
    pub fn vdda_text(&self) -> Option<String> {
        (self.vdda_mv != 0).then(|| trf("{} mV", &[&self.vdda_mv]))
    }

    /// The reset causes, comma-separated. Every internal reset drives the
    /// NRST pin too, so that bit alone means the pin.
    pub fn reset_text(&self) -> String {
        const NRST: u8 = 1 << 2;
        let bits = if self.reset & !NRST != 0 {
            self.reset & !NRST
        } else {
            self.reset
        };
        let causes: Vec<&str> = (0..8)
            .filter(|b| bits & (1 << b) != 0)
            .map(|b| tr(RESET_CAUSES[b]))
            .collect();
        if causes.is_empty() {
            tr("not reported").to_string()
        } else {
            causes.join(", ")
        }
    }
}

/// Holding registers 256..263.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Indication {
    /// 0 off, 1 on, 2 blinking.
    pub led1: u8,
    pub led2: bool,
    pub beeper: bool,
    pub out: bool,
    pub beep_on_ms: u16,
    pub beep_off_ms: u16,
    pub beeps_left: u16,
    pub acked: u16,
}

impl Indication {
    pub fn parse(r: &[u16]) -> Option<Self> {
        let r: &[u16; 8] = r.get(..8)?.try_into().ok()?;
        Some(Self {
            led1: r[0].min(2) as u8,
            led2: r[1] != 0,
            beeper: r[2] != 0,
            out: r[3] != 0,
            beep_on_ms: r[4],
            beep_off_ms: r[5],
            beeps_left: r[6],
            acked: r[7],
        })
    }
}

/// Manual indication; the automatic LED and beeper keep working.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Indicate {
    Led1(u8),
    Led2(bool),
    Beeper(bool),
    Out(bool),
}

impl Indicate {
    pub fn register(self) -> (u16, u16) {
        match self {
            Self::Led1(mode) => (holding::LED1, u16::from(mode.min(2))),
            Self::Led2(on) => (holding::LED2, u16::from(on)),
            Self::Beeper(on) => (holding::BEEPER, u16::from(on)),
            Self::Out(on) => (holding::OUT, u16::from(on)),
        }
    }
}

// ---- events ----------------------------------------------------------------

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Event {
    pub number: u16,
    pub kind: u8,
    pub sak: u8,
    pub uid: Vec<u8>,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Autoread {
    /// The event the data belongs to; 0: none yet.
    pub event: u16,
    /// 0: no error; the low 16 bits of the CME code; 0xFFFF: no such blocks.
    pub error: u16,
    pub data: Vec<u8>,
}

/// Input registers 512..620.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Events {
    pub overflow: bool,
    /// The oldest first.
    pub entries: Vec<Event>,
    pub autoread: Autoread,
}

impl Events {
    pub fn parse(r: &[u16]) -> Option<Self> {
        if r.len() < usize::from(input::EVENTS_COUNT) {
            return None;
        }
        let count = usize::from(r[0]).min(8);
        let entries = (0..count)
            .map(|i| {
                let e = &r[2 + i * 9..2 + (i + 1) * 9];
                let len = usize::from(e[3]).min(10);
                Event {
                    number: e[0],
                    kind: e[1] as u8,
                    sak: e[2] as u8,
                    uid: regs_to_bytes(&e[4..9])[..len].to_vec(),
                }
            })
            .collect();
        let tail = &r[74..];
        let size = usize::from(tail[1]).min(64);
        Some(Self {
            overflow: r[1] != 0,
            entries,
            autoread: Autoread {
                event: tail[0],
                error: tail[2],
                data: regs_to_bytes(&tail[3..35])[..size].to_vec(),
            },
        })
    }
}

// ---- settings --------------------------------------------------------------

/// Receiver gain steps of the MFRC522; writes round up to the next one.
pub const GAINS: [u8; 6] = [18, 23, 33, 38, 43, 48];
pub const INTERVAL_MIN: u16 = 100;
pub const AUTOREAD_MAX: u8 = 16;
/// The longest end-of-frame pause the window sets. The reader takes up to
/// 65535 ms, but it waits that long before every answer: a reader that slow
/// is not found by a tool that does not know the pause yet.
pub const FRAME_PAUSE_MAX: u16 = 100;

/// Registers 533..537. Writing them changes a copy in RAM: register 538
/// applies or saves it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct BusSettings {
    pub address: u8,
    pub baud: u32,
    pub parity: Parity,
    pub stop: StopBits,
    /// End-of-frame pause, ms; 0: the RTU minimum.
    pub frame_pause_ms: u16,
}

impl BusSettings {
    pub fn line(&self) -> Line {
        Line {
            baud: self.baud,
            parity: self.parity,
            stop: self.stop,
        }
    }

    pub fn regs(&self) -> [u16; 5] {
        [
            u16::from(self.address),
            (self.baud / 100).min(u32::from(u16::MAX)) as u16,
            self.parity.code(),
            self.stop.code(),
            self.frame_pause_ms,
        ]
    }

    /// What the reader goes back to after a factory reset.
    pub fn factory() -> Self {
        Self {
            address: FACTORY_ADDRESS,
            baud: 115_200,
            parity: Parity::None,
            stop: StopBits::One,
            frame_pause_ms: 2,
        }
    }
}

/// Holding registers 512..537.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Settings {
    pub scan: bool,
    pub interval_ms: u16,
    pub gain_db: u8,
    pub auto_led: bool,
    pub auto_beep: bool,
    pub autoread_first: u8,
    /// 0: autoread off.
    pub autoread_count: u8,
    /// Classic key type: false A, true B.
    pub key_b: bool,
    pub key: [u8; 6],
    /// Ultralight EV1 / NTAG password, in card order.
    pub ul_password: [u8; 4],
    /// Mifare Plus AES key.
    pub aes_key: [u8; 16],
    pub bus: BusSettings,
}

impl Settings {
    pub fn parse(r: &[u16]) -> Option<Self> {
        if r.len() < usize::from(holding::SETTINGS_COUNT) {
            return None;
        }
        let bytes = regs_to_bytes(&r[8..21]);
        Some(Self {
            scan: r[0] != 0,
            interval_ms: r[1],
            gain_db: r[2].min(255) as u8,
            auto_led: r[3] != 0,
            auto_beep: r[4] != 0,
            autoread_first: r[5].min(255) as u8,
            autoread_count: r[6].min(255) as u8,
            key_b: r[7] != 0,
            key: bytes[0..6].try_into().ok()?,
            ul_password: bytes[6..10].try_into().ok()?,
            aes_key: bytes[10..26].try_into().ok()?,
            bus: BusSettings {
                address: r[21].min(255) as u8,
                baud: u32::from(r[22]) * 100,
                parity: Parity::from_code(r[23]).unwrap_or_default(),
                stop: StopBits::from_code(r[24]).unwrap_or_default(),
                frame_pause_ms: r[25],
            },
        })
    }

    /// Registers 512..532.
    pub fn card_regs(&self) -> Vec<u16> {
        let mut regs = vec![
            u16::from(self.scan),
            self.interval_ms,
            u16::from(self.gain_db),
            u16::from(self.auto_led),
            u16::from(self.auto_beep),
            u16::from(self.autoread_first),
            u16::from(self.autoread_count),
            u16::from(self.key_b),
        ];
        let mut keys = Vec::with_capacity(26);
        keys.extend_from_slice(&self.key);
        keys.extend_from_slice(&self.ul_password);
        keys.extend_from_slice(&self.aes_key);
        regs.extend(bytes_to_regs(&keys));
        regs
    }

    /// What the window lets through: the reader rejects the rest.
    pub fn problems(&self) -> Vec<&'static str> {
        let mut p = Vec::new();
        if !(1..=247).contains(&self.bus.address) {
            p.push(tr("The address must be 1 to 247"));
        }
        if !self.bus.line().valid() {
            p.push(tr(
                "The speed must be a multiple of 100 from 500 to 2000000 baud",
            ));
        }
        if self.autoread_count > AUTOREAD_MAX {
            p.push(tr("Autoread takes at most 16 blocks"));
        }
        p
    }
}

// ---- card types --------------------------------------------------------------

pub fn card_type_name(kind: u8) -> String {
    let name = match kind {
        0 => "Mifare Classic 1K",
        1 => "Mifare Classic 4K",
        2 => "Mifare Classic Mini",
        3 => "Mifare Ultralight",
        4 => "Mifare Ultralight C",
        5 => "Ultralight EV1 80",
        6 => "Ultralight EV1 164",
        7 => "Mifare Plus S 2K SL1",
        8 => "Mifare Plus S 4K SL1",
        9 => "DESFire 2K",
        10 => "DESFire 4K",
        11 => "DESFire 8K",
        12 => "Mifare Classic 2K",
        13 => "Mifare Plus X 2K SL1",
        14 => "Mifare Plus X 4K SL1",
        15 => "Mifare Plus X SL0",
        16 => "Mifare Plus X 2K SL2",
        17 => "Mifare Plus X 4K SL2",
        18 => "Mifare Plus X SL3",
        19 => "Mifare Plus S SL0",
        22 => "Mifare Plus S SL3",
        23 => "NTAG213",
        24 => "NTAG215",
        25 => "NTAG216",
        26 => "Ultralight Nano",
        28 => "NTAG413",
        29 => "NTAG424",
        30 => "Mifare Plus X 4K SL3",
        31 => "Mifare Plus S 4K SL3",
        255 => return tr("unknown type").to_string(),
        other => return trf("type {}", &[&other]),
    };
    name.to_string()
}

// ---- command block -----------------------------------------------------------

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CmdRequest {
    pub op: u8,
    pub p0: u16,
    pub p1: u16,
    pub data: Vec<u8>,
}

pub const CMD_DATA_MAX: usize = 32;

impl CmdRequest {
    #[cfg_attr(not(test), allow(dead_code))]
    pub fn new(op: u8) -> Self {
        Self {
            op,
            p0: 0,
            p1: 0,
            data: Vec::new(),
        }
    }

    /// Registers 768..: seq, operation, p0, p1, length, data.
    pub fn regs(&self, seq: u16) -> Vec<u16> {
        let mut r = vec![
            seq,
            u16::from(self.op),
            self.p0,
            self.p1,
            self.data.len() as u16,
        ];
        r.extend(bytes_to_regs(&self.data));
        r
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CmdStatus {
    Done,
    /// Unknown operation, bad parameters, autoscan on, no selected card…
    Rejected,
    Running,
    /// The card did not answer or refused; see the CME code.
    CardError,
    /// No request since the reader started.
    None,
    Other(u16),
}

impl CmdStatus {
    pub fn from_code(code: u16) -> Self {
        match code {
            0 => Self::Done,
            1 => Self::Rejected,
            2 => Self::Running,
            3 => Self::CardError,
            255 => Self::None,
            other => Self::Other(other),
        }
    }

    pub fn text(self) -> String {
        match self {
            Self::Done => tr("done").into(),
            Self::Rejected => tr("rejected").into(),
            Self::Running => tr("running").into(),
            Self::CardError => tr("card error").into(),
            Self::None => tr("no request yet").into(),
            Self::Other(c) => trf("status {}", &[&c]),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CmdResponse {
    pub seq: u16,
    pub status: CmdStatus,
    pub cme: u32,
    pub data: Vec<u8>,
}

impl CmdResponse {
    /// Registers 1024..: seq, status, CME high, CME low, length, data.
    pub fn parse(r: &[u16]) -> Option<Self> {
        if r.len() < 5 {
            return None;
        }
        let size = usize::from(r[4]).min((r.len() - 5) * 2);
        Some(Self {
            seq: r[0],
            status: CmdStatus::from_code(r[1]),
            cme: (u32::from(r[2]) << 16) | u32::from(r[3]),
            data: regs_to_bytes(&r[5..])[..size].to_vec(),
        })
    }
}

/// The MFRC522 driver's error codes, as `+CME ERROR` of 3.x.
pub fn cme_text(cme: u32) -> Option<&'static str> {
    Some(match cme {
        0x0100 => tr("timeout"),
        0x0200 => tr("NAK"),
        0x0400 => tr("authentication error"),
        0x0800 => tr("communication error"),
        _ => return None,
    })
}

/// What an operation's p0, p1 and data mean, for the form.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Param {
    None,
    Block,
    Number,
    /// 0 off, 1 on, 2 query.
    Antenna,
    /// 0 increment, 1 decrement.
    Direction,
    Level,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DataKind {
    None,
    /// A UID, 4..10 bytes.
    Uid,
    /// A block: the block size or 16 bytes.
    Block,
    /// Exactly 16 bytes.
    Block16,
    /// A signed 32-bit number.
    Value,
}

#[derive(Clone, Copy, Debug)]
pub struct Op {
    pub code: u8,
    /// English text, translated where shown.
    pub name: &'static str,
    pub p0: Param,
    pub p1: Param,
    pub data: DataKind,
    /// Allowed while autoscan runs.
    pub with_scan: bool,
    /// What the answer data holds.
    pub answer: Answer,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Answer {
    None,
    Text,
    Hex,
    CardInfo,
    CardList,
    Antenna,
}

const fn op(
    code: u8,
    name: &'static str,
    p0: Param,
    p1: Param,
    data: DataKind,
    with_scan: bool,
    answer: Answer,
) -> Op {
    Op {
        code,
        name,
        p0,
        p1,
        data,
        with_scan,
        answer,
    }
}

use Answer as A;
use DataKind as D;
use Param as P;

pub const OPS: &[Op] = &[
    op(
        0x01,
        "Version string",
        P::None,
        P::None,
        D::None,
        true,
        A::Text,
    ),
    op(
        0x02,
        "MCU unique ID",
        P::None,
        P::None,
        D::None,
        true,
        A::Hex,
    ),
    op(
        0x03,
        "Firmware checksum (CRC32)",
        P::None,
        P::None,
        D::None,
        true,
        A::Hex,
    ),
    op(
        0x20,
        "Enumerate cards in the field",
        P::None,
        P::None,
        D::None,
        false,
        A::CardList,
    ),
    op(
        0x21,
        "Select the first card",
        P::None,
        P::None,
        D::None,
        false,
        A::CardInfo,
    ),
    op(
        0x22,
        "Select the next card",
        P::None,
        P::None,
        D::None,
        false,
        A::CardInfo,
    ),
    op(
        0x23,
        "Check that a card is in the field",
        P::None,
        P::None,
        D::Uid,
        false,
        A::None,
    ),
    op(
        0x24,
        "Select a card by UID",
        P::None,
        P::None,
        D::Uid,
        false,
        A::CardInfo,
    ),
    op(
        0x25,
        "Halt the card",
        P::None,
        P::None,
        D::None,
        false,
        A::None,
    ),
    op(
        0x26,
        "Selected card info",
        P::None,
        P::None,
        D::None,
        false,
        A::CardInfo,
    ),
    op(
        0x27,
        "Antenna field",
        P::Antenna,
        P::None,
        D::None,
        true,
        A::Antenna,
    ),
    op(
        0x30,
        "Read a block",
        P::Block,
        P::None,
        D::None,
        false,
        A::Hex,
    ),
    op(
        0x31,
        "Read a Plus SL3 block",
        P::Number,
        P::None,
        D::None,
        false,
        A::Hex,
    ),
    op(
        0x32,
        "Write a block",
        P::Block,
        P::None,
        D::Block,
        false,
        A::None,
    ),
    op(
        0x33,
        "Write a Plus block",
        P::Number,
        P::None,
        D::Block16,
        false,
        A::None,
    ),
    op(
        0x34,
        "Change a Classic value block",
        P::Block,
        P::Direction,
        D::Value,
        false,
        A::None,
    ),
    op(
        0x35,
        "Plus authentication",
        P::Number,
        P::None,
        D::None,
        false,
        A::None,
    ),
    op(
        0x36,
        "Switch the Plus security level",
        P::Level,
        P::None,
        D::None,
        false,
        A::None,
    ),
    op(
        0x37,
        "Change the UID",
        P::None,
        P::None,
        D::Uid,
        false,
        A::None,
    ),
    op(
        0x38,
        "Ultralight EV1 / NTAG PACK",
        P::None,
        P::None,
        D::None,
        false,
        A::Hex,
    ),
];

pub fn find_op(code: u8) -> Option<&'static Op> {
    OPS.iter().find(|o| o.code == code)
}

/// The card info of operations 0x21, 0x22, 0x24, 0x26: type, SAK, block
/// size, block count (2 bytes), UID length, UID.
pub fn parse_card_info(data: &[u8]) -> Option<Card> {
    if data.len() < 6 || data.len() != 6 + usize::from(data[5]) {
        return None;
    }
    Some(Card {
        kind: data[0],
        sak: data[1],
        block_size: data[2],
        blocks: u16::from_be_bytes([data[3], data[4]]),
        uid: data[6..].to_vec(),
    })
}

/// Operation 0x20: UID length, UID, SAK for each card.
pub fn parse_card_list(data: &[u8]) -> Option<Vec<(Vec<u8>, u8)>> {
    let mut cards = Vec::new();
    let mut pos = 0;
    while pos < data.len() {
        let len = usize::from(data[pos]);
        let uid = data.get(pos + 1..pos + 1 + len)?.to_vec();
        let sak = *data.get(pos + 1 + len)?;
        cards.push((uid, sak));
        pos += len + 2;
    }
    Some(cards)
}

/// A readable account of an answer's data.
pub fn describe_answer(op: Option<&Op>, data: &[u8]) -> String {
    match op.map(|o| o.answer) {
        Some(A::Text) => String::from_utf8_lossy(data).trim().to_string(),
        Some(A::CardInfo) => match parse_card_info(data) {
            Some(c) => card_summary(&c),
            None => hex_spaced(data),
        },
        Some(A::CardList) => match parse_card_list(data) {
            Some(cards) if cards.is_empty() => tr("no cards").to_string(),
            Some(cards) => cards
                .iter()
                .map(|(uid, sak)| format!("{} (SAK {sak:02X})", hex_compact(uid)))
                .collect::<Vec<_>>()
                .join(", "),
            None => hex_spaced(data),
        },
        Some(A::Antenna) if data.len() == 1 => {
            if data[0] != 0 {
                tr("field on").to_string()
            } else {
                tr("field off").to_string()
            }
        }
        _ if data.is_empty() => String::new(),
        _ => hex_spaced(data),
    }
}

pub fn card_summary(c: &Card) -> String {
    let mut s = format!("{} · UID {}", card_type_name(c.kind), c.uid_text());
    if c.blocks > 0 {
        s.push_str(&trf(" · {} blocks × {} B", &[&c.blocks, &c.block_size]));
    }
    s
}

pub fn hex_compact(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02X}")).collect()
}

pub fn hex_spaced(bytes: &[u8]) -> String {
    crate::traffic::hex(bytes)
}

/// Hex digits with any spaces, colons or dashes between them.
pub fn parse_hex(text: &str) -> Option<Vec<u8>> {
    let digits: Vec<u8> = text
        .bytes()
        .filter(|b| !matches!(b, b' ' | b':' | b'-' | b'\t'))
        .collect();
    if !digits.len().is_multiple_of(2) {
        return None;
    }
    digits
        .chunks(2)
        .map(|c| u8::from_str_radix(std::str::from_utf8(c).ok()?, 16).ok())
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn self_test() {
        // Read from the bench reader, 4.0F Nov 25 2026, after a restart.
        let t = SelfTest::parse(&[0, 0x0300, 0x8282, 3283, 0x14]).unwrap();
        assert!(t.passed());
        assert_eq!(t.chip_text(), "0x82");
        assert_eq!(t.vdda_text().as_deref(), Some("3283 mV"));
        // NRST goes with the software restart.
        assert_eq!(t.reset_text(), "software restart");
        let power = SelfTest { reset: 0x0C, ..t };
        assert_eq!(power.reset_text(), "power on or brownout");
        let pin = SelfTest { reset: 0x04, ..t };
        assert_eq!(pin.reset_text(), "NRST pin");
        assert_eq!(SelfTest { reset: 0, ..t }.reset_text(), "not reported");

        let bad = SelfTest::parse(&[1, 0x0303, 0x0000, 0, 0x20]).unwrap();
        assert!(!bad.passed());
        assert_eq!(
            bad.faults(),
            "P01 MFRC522 did not start, P02 EEPROM write failed"
        );
        assert_eq!(bad.vdda_text(), None);
        assert_eq!(bad.reset_text(), "watchdog");
        assert_eq!(
            SelfTest {
                chip_version: [0x92, 0x92],
                ..t
            }
            .chip_text(),
            "0x92 (NXP 2.0)"
        );
        assert_eq!(
            SelfTest {
                chip_version: [0x92, 0x00],
                ..t
            }
            .chip_text(),
            "0x92 / 0x00"
        );
        assert!(SelfTest::parse(&[0; 4]).is_none());
    }

    #[test]
    fn identity_and_status_of_the_emulator() {
        // Read from the firmware emulator (tests/host/emulator.py).
        let id = Identity::parse(&[0x0485, 0x0400, 0x1121, 0x3141, 0xABCD]).unwrap();
        assert!(id.supported());
        assert_eq!(id.version(), "4.0");
        assert_eq!(id.serial_text(), "11213141ABCD");
        assert!(!Identity { major: 3, ..id }.supported());

        let mut r = [0u16; 24];
        r[0] = 1;
        r[5] = 1;
        r[6] = 1;
        r[7] = 1;
        r[9] = 0;
        r[10] = 4;
        r[11] = 0xA1B2;
        r[12] = 0xC3D4;
        r[16] = 8;
        r[17] = 64;
        r[18] = 16;
        r[21] = 1;
        let live = Live::parse(&r).unwrap();
        assert!(live.card_present && live.led1);
        let card = live.card.unwrap();
        assert_eq!(card.uid, [0xA1, 0xB2, 0xC3, 0xD4]);
        assert_eq!(card.uid_text(), "A1B2C3D4");
        assert_eq!(
            card_summary(&card),
            "Mifare Classic 1K · UID A1B2C3D4 · 64 blocks × 16 B"
        );
        r[9] = 255;
        r[10] = 0;
        assert_eq!(Live::parse(&r).unwrap().card, None);
    }

    #[test]
    fn settings_round_trip() {
        // The emulator's defaults.
        let mut r = vec![1, 200, 33, 1, 1, 0, 0, 0];
        r.extend([0xFFFF; 13]);
        r.extend([1, 1152, 0, 10, 2]);
        let s = Settings::parse(&r).unwrap();
        assert!(s.scan && s.auto_led && s.auto_beep && !s.key_b);
        assert_eq!(s.gain_db, 33);
        assert_eq!(s.key, [0xFF; 6]);
        assert_eq!(s.bus, BusSettings::factory());
        assert_eq!(s.card_regs(), r[..21]);
        assert_eq!(s.bus.regs(), r[21..]);
        assert!(s.problems().is_empty());

        let s2 = Settings {
            key_b: true,
            key: [1, 2, 3, 4, 5, 6],
            ul_password: [0xA, 0xB, 0xC, 0xD],
            aes_key: [7; 16],
            ..s
        };
        let mut r2 = s2.card_regs();
        r2.extend(s2.bus.regs());
        assert_eq!(Settings::parse(&r2), Some(s2));
        assert_eq!(r2[8..11], [0x0102, 0x0304, 0x0506]);
        assert_eq!(r2[11..13], [0x0A0B, 0x0C0D]);
    }

    #[test]
    fn events_block() {
        let mut r = vec![0u16; 109];
        r[0] = 2;
        r[2..11].copy_from_slice(&[7, 0, 8, 4, 0xA1B2, 0xC3D4, 0, 0, 0]);
        r[11..20].copy_from_slice(&[8, 23, 0, 7, 0x0411, 0x2233, 0x4455, 0x6600, 0]);
        r[74] = 8;
        r[75] = 3;
        r[77] = 0xDEAD;
        r[78] = 0xBE00;
        let e = Events::parse(&r).unwrap();
        assert_eq!(e.entries.len(), 2);
        assert_eq!(e.entries[1].uid, [4, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66]);
        assert_eq!(e.autoread.data, [0xDE, 0xAD, 0xBE]);
        assert_eq!(e.autoread.event, 8);
    }

    #[test]
    fn command_block() {
        let req = CmdRequest {
            op: 0x24,
            p0: 0,
            p1: 0,
            data: vec![0xA1, 0xB2, 0xC3],
        };
        assert_eq!(req.regs(9), [9, 0x24, 0, 0, 3, 0xA1B2, 0xC300]);
        let rsp = CmdResponse::parse(&[9, 3, 0, 0x0400, 2, 0x4142, 0]).unwrap();
        assert_eq!(rsp.status, CmdStatus::CardError);
        assert_eq!(rsp.cme, 0x400);
        assert_eq!(cme_text(rsp.cme), Some("authentication error"));
        assert_eq!(rsp.data, b"AB");
        let info = [0, 8, 16, 0, 64, 4, 0xA1, 0xB2, 0xC3, 0xD4];
        let card = parse_card_info(&info).unwrap();
        assert_eq!((card.blocks, card.block_size), (64, 16));
        assert!(parse_card_info(&info[..9]).is_none());
        let list = [4, 1, 2, 3, 4, 8, 7, 1, 2, 3, 4, 5, 6, 7, 0];
        assert_eq!(parse_card_list(&list).unwrap().len(), 2);
        assert!(parse_card_list(&list[..5]).is_none());
        assert_eq!(parse_hex("a1 b2:C3-d4"), Some(vec![0xA1, 0xB2, 0xC3, 0xD4]));
        assert_eq!(parse_hex("a1b"), None);
        assert_eq!(parse_hex("zz"), None);
    }
}
