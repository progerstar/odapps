//! The watchdog's HID interface.
//!
//! The device is a composite CDC + HID function (VID:PID `0483:A26D`). The
//! HID side needs no driver and carries:
//!
//! * output report 1 — a one-byte command, mapped by the firmware onto its
//!   CDC commands (`~U`, `~T1`, …) and executed silently;
//! * feature report 2 — the firmware version, 16 ASCII bytes
//!   (`1.8U+<git sha>`; older builds `1.7U_<date>`);
//! * feature report 4 — channel states, the IN/TEMP channel and, from 1.8U,
//!   diagnostics;
//! * feature report 5 — the settings in the `~F`/`~W` text format plus the
//!   TempLimit source byte. Firmware from the HID settings change on accepts
//!   it back with SET_REPORT and stores it like `~W` with `~i`; byte 15 of
//!   report 4, polled with GET_REPORT, then tells how the write ended.

use std::ffi::CString;

pub const USB_VID: u16 = 0x0483;
pub const USB_PID: u16 = 0xA26D;
const MANUFACTURER: &str = "Open Development";
/// The device's iProduct (Linux, macOS) or its HID interface string
/// (Windows reports that one instead).
const PRODUCTS: [&str; 2] = ["USB Watchdog", "WDG HID Interface"];

const REPORT_COMMAND: u8 = 1;
const REPORT_VERSION: u8 = 2;
const REPORT_DATA: u8 = 4;
const REPORT_SETTINGS: u8 = 5;
/// Report sizes including the report ID byte.
const VERSION_LEN: usize = 17;
const DATA_LEN: usize = 17;
const SETTINGS_LEN: usize = 13;
/// GET_FEATURE buffer for the shorter settings report: the longest feature
/// report. The Windows HID class driver expects room for that one whatever
/// the ID (the Qt monitor reads 17 bytes too); the device answers with its
/// own length.
const FEATURE_LEN: usize = 17;

/// HID command codes (`watchdog_hid_cdc_command` in the firmware). Only the
/// ones this monitor sends are listed; 18/19 enter the bootloader and are
/// deliberately left out.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Command {
    /// `~U`: reload the timer.
    Heartbeat,
    /// `~T1`: press the reset button now.
    TestReset,
    /// `~T2`: power-cycle the PC now.
    TestPowerCycle,
    /// `~T3`: hold the power button: the PC turns off.
    TestShutdown,
    /// `~T0`: blink the LEDs and pulse CH1/CH2.
    SelfTest,
    /// `~L1` / `~L0`: green LED blinks on each heartbeat.
    Led(bool),
    /// `~P1` / `~P0`: suspend or resume the timer.
    Pause(bool),
}

impl Command {
    pub fn code(self) -> u8 {
        match self {
            Command::Heartbeat => 1,
            Command::TestReset => 2,
            Command::TestPowerCycle => 3,
            Command::TestShutdown => 4,
            Command::SelfTest => 5,
            Command::Led(false) => 14,
            Command::Led(true) => 15,
            Command::Pause(true) => 16,
            Command::Pause(false) => 17,
        }
    }

    pub fn report(self) -> [u8; 2] {
        [REPORT_COMMAND, self.code()]
    }
}

// ---- version -------------------------------------------------------------

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Model {
    Pro2,
    Lite,
    Unknown,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Firmware {
    /// As reported, e.g. `1.8U+3f2a1c9`.
    pub raw: String,
    /// `1.8`
    pub version: String,
    pub model: Model,
    /// Git commit (1.8U) or build date (older), possibly empty.
    pub build: String,
    numbers: (u32, u32),
}

impl Firmware {
    /// Parses feature report 2 (with its ID byte).
    pub fn parse(report: &[u8]) -> Option<Self> {
        let body = report.get(1..)?;
        let end = body.iter().position(|&b| b == 0).unwrap_or(body.len());
        let raw: String = body[..end].iter().map(|&b| char::from(b)).collect();
        let raw = raw.trim().to_string();
        let letter = raw.find(|c: char| c.is_ascii_alphabetic())?;
        let version = raw[..letter].to_string();
        let mut parts = version.split('.');
        let major = parts.next()?.parse().ok()?;
        let minor = parts.next().unwrap_or("0").parse().ok()?;
        let model = match raw.as_bytes()[letter] {
            b'U' => Model::Pro2,
            b'L' => Model::Lite,
            _ => Model::Unknown,
        };
        // One separator follows the model letter: `+`, `_` or a space.
        let build = raw.get(letter + 2..).unwrap_or("").trim().to_string();
        Some(Self {
            raw,
            version,
            model,
            build,
            numbers: (major, minor),
        })
    }

    /// Model letter and version, `1.8U`.
    pub fn label(&self) -> String {
        let letter = match self.model {
            Model::Pro2 => "U",
            Model::Lite => "L",
            Model::Unknown => "?",
        };
        format!("{}{letter}", self.version)
    }

    /// Bytes 7–14 of the data report (1.8U and later).
    pub fn has_diagnostics(&self) -> bool {
        self.model == Model::Pro2 && self.numbers >= (1, 8)
    }
}

// ---- data report ---------------------------------------------------------

/// The IN/TEMP channel.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum InState {
    Off,
    /// Input level.
    Input(bool),
    /// Output level.
    Output(bool),
    /// DS18B20 reading in °C, `None` while the sensor fails.
    Temperature(Option<f32>),
}

/// 1.8U health flags.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Diagnostics {
    pub adc_fault: bool,
    pub adc_faults: u32,
    pub settings_fault: bool,
    pub odometer_fault: bool,
    /// TempLimit uses the MCU sensor instead of the DS18B20.
    pub internal_sensor: bool,
}

impl Diagnostics {
    pub fn any_fault(&self) -> bool {
        self.adc_fault || self.settings_fault || self.odometer_fault
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Data {
    /// CH1 output level: 0 is closed (the PC's button is pressed).
    pub ch1: u8,
    /// CH2 output level; `None` on the Lite, which has one channel.
    pub ch2: Option<u8>,
    pub input: InState,
    pub diagnostics: Option<Diagnostics>,
    /// The last settings write over HID in this USB session (1.8U).
    pub write: WriteStatus,
}

/// Byte 15 of the data report.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum WriteStatus {
    /// No write since the USB session started: also after a USB reset in
    /// the middle of a write (the accepted report is stored all the same).
    #[default]
    Idle,
    /// Accepted, not stored yet.
    Pending,
    /// Stored and applied.
    Saved,
    /// Not stored: a forced action (`~T1`–`~T3`, TempLimit) is pending.
    Busy,
    /// Rejected by SET_REPORT.
    Invalid,
    /// The flash write failed.
    Failed,
    Other(u8),
}

impl WriteStatus {
    fn from_byte(b: u8) -> Self {
        match b {
            0 => Self::Idle,
            1 => Self::Pending,
            2 => Self::Saved,
            3 => Self::Busy,
            4 => Self::Invalid,
            5 => Self::Failed,
            other => Self::Other(other),
        }
    }
}

impl Data {
    /// Parses feature report 4 (with its ID byte).
    pub fn parse(report: &[u8], with_diagnostics: bool) -> Option<Self> {
        if report.len() < 7 || report[0] != REPORT_DATA {
            return None;
        }
        let ch1 = report[1];
        if report[2] == 0xFF {
            // Lite: CH2 and the IN channel do not exist.
            return Some(Self {
                ch1,
                ch2: None,
                input: InState::Off,
                diagnostics: None,
                write: WriteStatus::Idle,
            });
        }
        let input = match report[3] {
            1 => InState::Input(report[4] != 0),
            2 => InState::Output(report[4] != 0),
            3 => InState::Temperature(temperature(&report[4..7])),
            _ => InState::Off,
        };
        let diagnostics = (with_diagnostics && report.len() >= 15).then(|| Diagnostics {
            adc_fault: report[7] != 0,
            adc_faults: u32::from_le_bytes([report[8], report[9], report[10], report[11]]),
            settings_fault: report[12] != 0,
            odometer_fault: report[13] != 0,
            internal_sensor: report[14] != 0,
        });
        let write = match report.get(15) {
            Some(&b) if with_diagnostics => WriteStatus::from_byte(b),
            _ => WriteStatus::Idle,
        };
        Some(Self {
            ch1,
            ch2: Some(report[2]),
            input,
            diagnostics,
            write,
        })
    }
}

/// Whole degrees (signed byte) and ten-thousandths (signed 16-bit, same sign
/// as the whole part). All three bytes 0xFF mean "no reading".
fn temperature(bytes: &[u8]) -> Option<f32> {
    if bytes.iter().all(|&b| b == 0xFF) {
        return None;
    }
    let whole = bytes[0] as i8;
    let fraction = i16::from_le_bytes([bytes[1], bytes[2]]);
    let tenths = (i32::from(whole) * 10_000 + i32::from(fraction)) as f32 / 1_000.0;
    Some(tenths.round() / 10.0)
}

// ---- settings report -----------------------------------------------------

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ChannelMode {
    Off,
    Reset,
    Power,
    /// A plain output, open after power-up (firmware `GPIO_LO`).
    OutputOpen,
    /// A plain output, closed after power-up (firmware `GPIO_HI`).
    OutputClosed,
}

impl ChannelMode {
    pub const ALL: [Self; 5] = [
        Self::Off,
        Self::Reset,
        Self::Power,
        Self::OutputOpen,
        Self::OutputClosed,
    ];
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum InMode {
    Off,
    Input,
    Output,
    /// DS18B20 sensor.
    Temperature,
}

impl InMode {
    pub const ALL: [Self; 4] = [Self::Off, Self::Input, Self::Output, Self::Temperature];
}

/// The largest value of every one-digit field.
pub const DIGIT_MAX: u8 = 15;

/// Pro2 settings as feature report 5 carries them.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Settings {
    /// T1: minutes without a heartbeat before acting; 0 turns the timer off.
    pub reboot_min: u8,
    /// T2: reset button press, ×100 ms.
    pub reset_pulse: u8,
    /// T3: power button hold that turns the PC off, seconds.
    pub power_off_s: u8,
    /// T4: pause before the PC is turned on again, seconds.
    pub power_delay_s: u8,
    /// T5: power button press that turns the PC on, ×100 ms.
    pub power_on_pulse: u8,
    pub ch1: ChannelMode,
    pub ch2: ChannelMode,
    /// Consecutive reboots before the PC is shut down; 0 = no limit.
    pub reboot_limit: u8,
    pub in_mode: InMode,
    /// °C; 0 turns the limit off.
    pub temp_limit: u8,
    /// TempLimit watches the MCU sensor instead of the DS18B20 (`~i1`).
    pub internal_sensor: bool,
}

impl Default for Settings {
    /// The firmware defaults (`ODC_WDGPRO2_init`).
    fn default() -> Self {
        Self {
            reboot_min: 5,
            reset_pulse: 2,
            power_off_s: 5,
            power_delay_s: 3,
            power_on_pulse: 2,
            ch1: ChannelMode::Reset,
            ch2: ChannelMode::Power,
            reboot_limit: 0,
            in_mode: InMode::Temperature,
            temp_limit: 0,
            internal_sensor: false,
        }
    }
}

/// The firmware's settings digit: `0`–`9`, then `A`… (values up to 15 here).
fn digit_char(v: u8) -> u8 {
    match v.min(DIGIT_MAX) {
        v @ 0..=9 => b'0' + v,
        v => b'A' + v - 10,
    }
}

fn char_digit(c: u8) -> Option<u8> {
    match c {
        b'0'..=b'9' => Some(c - b'0'),
        b'A'..=b'Z' => Some(c - b'A' + 10),
        _ => None,
    }
}

impl Settings {
    /// Parses feature report 5: nine digits and a hex byte
    /// (`watchdog_settings_encode`), then the TempLimit source.
    pub fn parse(report: &[u8]) -> Option<Self> {
        if report.len() < 12 || report[0] != REPORT_SETTINGS {
            return None;
        }
        let text = &report[1..12];
        let mut d = [0u8; 9];
        for (slot, &c) in d.iter_mut().zip(text) {
            *slot = char_digit(c)?;
        }
        if d[..5].iter().chain(&d[7..8]).any(|&v| v > DIGIT_MAX) {
            return None;
        }
        let hex = std::str::from_utf8(&text[9..11]).ok()?;
        Some(Self {
            reboot_min: d[0],
            reset_pulse: d[1],
            power_off_s: d[2],
            power_delay_s: d[3],
            power_on_pulse: d[4],
            ch1: *ChannelMode::ALL.get(usize::from(d[5]))?,
            ch2: *ChannelMode::ALL.get(usize::from(d[6]))?,
            reboot_limit: d[7],
            in_mode: *InMode::ALL.get(usize::from(d[8]))?,
            temp_limit: u8::from_str_radix(hex, 16).ok()?,
            // Firmware before HID writes always sends 0 here.
            internal_sensor: report.get(12) == Some(&1),
        })
    }

    /// Feature report 5 for SET_REPORT. Out-of-range fields are clamped so
    /// the device never sees a value it would reject.
    pub fn report(&self) -> [u8; SETTINGS_LEN] {
        let index =
            |m: ChannelMode| ChannelMode::ALL.iter().position(|&x| x == m).unwrap_or(0) as u8;
        let in_index = InMode::ALL
            .iter()
            .position(|&x| x == self.in_mode)
            .unwrap_or(0) as u8;
        let hex = format!("{:02X}", self.temp_limit);
        let mut r = [0u8; SETTINGS_LEN];
        r[0] = REPORT_SETTINGS;
        let digits = [
            self.reboot_min,
            self.reset_pulse,
            self.power_off_s,
            self.power_delay_s,
            self.power_on_pulse,
            index(self.ch1),
            index(self.ch2),
            self.reboot_limit,
            in_index,
        ];
        for (slot, v) in r[1..10].iter_mut().zip(digits) {
            *slot = digit_char(v);
        }
        r[10..12].copy_from_slice(hex.as_bytes());
        r[12] = u8::from(self.internal_sensor);
        r
    }

    /// The 11 `~F` characters, for the log.
    pub fn text(&self) -> String {
        self.report()[1..12]
            .iter()
            .map(|&b| char::from(b))
            .collect()
    }

    fn acts(m: ChannelMode) -> bool {
        matches!(m, ChannelMode::Reset | ChannelMode::Power)
    }

    /// Some channel presses the PC's reset or power button.
    pub fn has_actor(&self) -> bool {
        Self::acts(self.ch1) || Self::acts(self.ch2)
    }

    pub fn uses(&self, mode: ChannelMode) -> bool {
        self.ch1 == mode || self.ch2 == mode
    }

    /// The timer counts and some channel can act on the PC.
    pub fn timer_armed(&self) -> bool {
        self.reboot_min > 0 && self.has_actor()
    }

    /// `self`, an edit of `base`, moved onto `device`: the fields changed in
    /// the edit keep their value, the others follow the device.
    pub fn rebased(&self, base: &Settings, device: &Settings) -> Settings {
        macro_rules! merge {
            ($($field:ident),*) => {
                Settings {
                    $($field: if self.$field == base.$field {
                        device.$field
                    } else {
                        self.$field
                    },)*
                }
            };
        }
        merge!(
            reboot_min,
            reset_pulse,
            power_off_s,
            power_delay_s,
            power_on_pulse,
            ch1,
            ch2,
            reboot_limit,
            in_mode,
            temp_limit,
            internal_sensor
        )
    }
}

// ---- transport -----------------------------------------------------------

/// A watchdog found on the bus.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Found {
    pub serial: String,
    /// Backend-specific path used to open it.
    pub path: CString,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum OpenError {
    /// The OS denies access (Linux without the udev rule).
    AccessDenied(String),
    Other(String),
}

pub trait Bus {
    fn scan(&mut self) -> Result<Vec<Found>, String>;
    fn open(&mut self, found: &Found) -> Result<Box<dyn Device>, OpenError>;
}

pub trait Device {
    fn write(&mut self, report: &[u8]) -> Result<(), String>;
    /// Reads feature report `id` into a buffer of `len` bytes (including the
    /// ID); the result may be shorter.
    fn feature(&mut self, id: u8, len: usize) -> Result<Vec<u8>, String>;
    /// Sends a feature report (the first byte is its ID).
    fn set_feature(&mut self, report: &[u8]) -> Result<(), String>;
}

/// Reading helpers shared by every transport.
pub trait DeviceExt: Device {
    fn send(&mut self, command: Command) -> Result<(), String> {
        self.write(&command.report())
    }

    fn firmware(&mut self) -> Result<Firmware, String> {
        let report = self.feature(REPORT_VERSION, VERSION_LEN)?;
        Firmware::parse(&report).ok_or_else(|| format!("unexpected version report {report:02X?}"))
    }

    fn data(&mut self, with_diagnostics: bool) -> Option<Data> {
        let report = self.feature(REPORT_DATA, DATA_LEN).ok()?;
        Data::parse(&report, with_diagnostics)
    }

    fn settings(&mut self) -> Option<Settings> {
        let mut report = self.feature(REPORT_SETTINGS, FEATURE_LEN).ok()?;
        // Windows may hand back the whole buffer, zero-padded.
        report.truncate(SETTINGS_LEN);
        Settings::parse(&report)
    }

    /// Hands the settings to the device; the data report tells the outcome.
    /// Fails (STALL) for a rejected report, while the previous one is still
    /// pending, and on Pro2 firmware before 1.8U.
    fn write_settings(&mut self, settings: &Settings) -> Result<(), String> {
        self.set_feature(&settings.report())
    }
}

impl<T: Device + ?Sized> DeviceExt for T {}

/// The real bus through hidapi.
pub struct HidBus {
    api: Option<hidapi::HidApi>,
}

impl HidBus {
    pub fn new() -> Self {
        // Enumerate only our VID:PID instead of every HID device.
        hidapi::HidApi::disable_device_discovery();
        Self { api: None }
    }

    fn api(&mut self) -> Result<&mut hidapi::HidApi, String> {
        if self.api.is_none() {
            self.api = Some(hidapi::HidApi::new().map_err(|e| e.to_string())?);
        }
        Ok(self.api.as_mut().expect("just set"))
    }
}

impl Bus for HidBus {
    fn scan(&mut self) -> Result<Vec<Found>, String> {
        let api = self.api()?;
        api.reset_devices().map_err(|e| e.to_string())?;
        api.add_devices(USB_VID, USB_PID)
            .map_err(|e| e.to_string())?;
        let mut found: Vec<Found> = Vec::new();
        for info in api.device_list() {
            let vendor = info.manufacturer_string().unwrap_or("");
            let product = info.product_string().unwrap_or("");
            let Some(serial) = info.serial_number().filter(|s| !s.is_empty()) else {
                continue;
            };
            if vendor != MANUFACTURER || !PRODUCTS.contains(&product) {
                log::debug!("skipping {vendor} / {product} @ {serial}");
                continue;
            }
            if !found.iter().any(|f| f.serial == serial) {
                found.push(Found {
                    serial: serial.to_string(),
                    path: info.path().to_owned(),
                });
            }
        }
        Ok(found)
    }

    fn open(&mut self, found: &Found) -> Result<Box<dyn Device>, OpenError> {
        let api = self.api().map_err(OpenError::Other)?;
        match api.open_path(&found.path) {
            Ok(device) => Ok(Box::new(HidDev(device))),
            Err(e) => {
                let text = e.to_string();
                if permission_denied(&found.path) {
                    Err(OpenError::AccessDenied(text))
                } else {
                    Err(OpenError::Other(text))
                }
            }
        }
    }
}

/// hidapi folds the cause into a message; ask the OS directly.
fn permission_denied(path: &CString) -> bool {
    #[cfg(target_os = "linux")]
    {
        let Ok(path) = path.to_str() else {
            return false;
        };
        matches!(
            std::fs::OpenOptions::new().read(true).write(true).open(path),
            Err(e) if e.kind() == std::io::ErrorKind::PermissionDenied
        )
    }
    #[cfg(not(target_os = "linux"))]
    {
        let _ = path;
        false
    }
}

struct HidDev(hidapi::HidDevice);

impl Device for HidDev {
    fn write(&mut self, report: &[u8]) -> Result<(), String> {
        match self.0.write(report) {
            Ok(n) if n >= report.len() => Ok(()),
            Ok(n) => Err(format!("short write ({n} bytes)")),
            Err(e) => Err(e.to_string()),
        }
    }

    fn feature(&mut self, id: u8, len: usize) -> Result<Vec<u8>, String> {
        let mut buf = vec![0u8; len];
        buf[0] = id;
        let n = self
            .0
            .get_feature_report(&mut buf)
            .map_err(|e| e.to_string())?;
        buf.truncate(n);
        if buf.first() != Some(&id) {
            return Err(format!("feature report {id}: got {buf:02X?}"));
        }
        Ok(buf)
    }

    fn set_feature(&mut self, report: &[u8]) -> Result<(), String> {
        // hidapi pads to the longest feature report on Windows itself.
        self.0
            .send_feature_report(report)
            .map_err(|e| e.to_string())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn version_report(text: &str) -> Vec<u8> {
        let mut r = vec![0u8; VERSION_LEN];
        r[0] = REPORT_VERSION;
        r[1..=text.len()].copy_from_slice(text.as_bytes());
        r
    }

    #[test]
    fn firmware_versions() {
        let f = Firmware::parse(&version_report("1.8U+3f2a1c9")).unwrap();
        assert_eq!(f.model, Model::Pro2);
        assert_eq!(f.version, "1.8");
        assert_eq!(f.build, "3f2a1c9");
        assert_eq!(f.label(), "1.8U");
        assert!(f.has_diagnostics());

        let old = Firmware::parse(&version_report("1.7U_1806141530")).unwrap();
        assert_eq!(old.model, Model::Pro2);
        assert_eq!(old.build, "1806141530");
        assert!(!old.has_diagnostics());

        let lite = Firmware::parse(&version_report("1.2L 1712011200")).unwrap();
        assert_eq!(lite.model, Model::Lite);
        assert!(!lite.has_diagnostics());

        assert_eq!(
            Firmware::parse(&version_report("2.0X")).unwrap().model,
            Model::Unknown
        );
        assert!(Firmware::parse(&version_report("garbage")).is_none());
        assert!(Firmware::parse(&[REPORT_VERSION]).is_none());
    }

    #[test]
    fn data_report_pro2() {
        // CH1 closed, CH2 open, temperature 24.5625 °C, 1.8U diagnostics.
        let mut r = [0u8; DATA_LEN];
        r[0] = REPORT_DATA;
        r[1] = 0;
        r[2] = 1;
        r[3] = 3;
        r[4] = 24;
        r[5..7].copy_from_slice(&5625i16.to_le_bytes());
        r[7] = 0;
        r[8..12].copy_from_slice(&3u32.to_le_bytes());
        r[12] = 1;
        r[14] = 1;
        let d = Data::parse(&r, true).unwrap();
        assert_eq!(d.ch1, 0);
        assert_eq!(d.ch2, Some(1));
        assert_eq!(d.input, InState::Temperature(Some(24.6)));
        let diag = d.diagnostics.unwrap();
        assert_eq!(diag.adc_faults, 3);
        assert!(diag.settings_fault && diag.internal_sensor && diag.any_fault());
        assert!(Data::parse(&r, false).unwrap().diagnostics.is_none());
    }

    #[test]
    fn temperatures() {
        let t = |whole: i8, frac: i16| {
            let f = frac.to_le_bytes();
            temperature(&[whole as u8, f[0], f[1]])
        };
        assert_eq!(t(-7, -3000), Some(-7.3));
        assert_eq!(t(0, -5000), Some(-0.5));
        assert_eq!(t(0, 5000), Some(0.5));
        // -1.x °C is a valid reading, not the 0xFF error marker.
        assert_eq!(t(-1, -2500), Some(-1.3));
        assert_eq!(temperature(&[0xFF, 0xFF, 0xFF]), None);
    }

    #[test]
    fn data_report_lite_and_modes() {
        let lite = Data::parse(&[REPORT_DATA, 1, 0xFF, 0, 0, 0, 0], true).unwrap();
        assert_eq!(lite.ch2, None);
        assert_eq!(lite.input, InState::Off);
        let input = Data::parse(&[REPORT_DATA, 1, 1, 1, 1, 0, 0], false).unwrap();
        assert_eq!(input.input, InState::Input(true));
        let output = Data::parse(&[REPORT_DATA, 1, 1, 2, 0, 0, 0], false).unwrap();
        assert_eq!(output.input, InState::Output(false));
        assert!(Data::parse(&[REPORT_VERSION, 1, 1, 2, 0, 0, 0], false).is_none());
        assert!(Data::parse(&[REPORT_DATA, 1], false).is_none());
    }

    fn settings_report_of(text: &[u8], source: u8) -> Vec<u8> {
        let mut r = vec![REPORT_SETTINGS];
        r.extend_from_slice(text);
        r.push(source);
        r
    }

    #[test]
    fn settings_report() {
        let s = Settings::parse(&settings_report_of(b"52532120332", 0)).unwrap();
        assert_eq!(s.reboot_min, 5);
        assert_eq!(s.reset_pulse, 2);
        assert_eq!(s.power_off_s, 5);
        assert_eq!(s.power_delay_s, 3);
        assert_eq!(s.power_on_pulse, 2);
        assert_eq!(s.ch1, ChannelMode::Reset);
        assert_eq!(s.ch2, ChannelMode::Power);
        assert_eq!(s.reboot_limit, 0);
        assert_eq!(s.in_mode, InMode::Temperature);
        assert_eq!(s.temp_limit, 0x32);
        assert!(!s.internal_sensor);
        assert!(s.timer_armed());
        // The firmware defaults except TempLimit.
        assert_eq!(
            s,
            Settings {
                temp_limit: 0x32,
                ..Settings::default()
            }
        );

        // Digits above 9 ("F" = 15 minutes), the MCU sensor, timer off.
        let s = Settings::parse(&settings_report_of(b"F25324300A5", 1)).unwrap();
        assert_eq!(s.reboot_min, 15);
        assert_eq!(s.ch1, ChannelMode::OutputClosed);
        assert_eq!(s.ch2, ChannelMode::OutputOpen);
        assert_eq!(s.reboot_limit, 0);
        assert_eq!(s.in_mode, InMode::Off);
        assert_eq!(s.temp_limit, 0xA5);
        assert!(s.internal_sensor);
        assert!(!s.timer_armed());
        let off = Settings::parse(&settings_report_of(b"02532120300", 0)).unwrap();
        assert!(!off.timer_armed());

        // Channel mode 9, a T value above 15, broken hex.
        assert!(Settings::parse(&settings_report_of(b"52532920300", 0)).is_none());
        assert!(Settings::parse(&settings_report_of(b"G2532120300", 0)).is_none());
        assert!(Settings::parse(&settings_report_of(b"525321203ZZ", 0)).is_none());
        // Firmware before HID writes: 12 bytes are enough.
        assert!(Settings::parse(&settings_report_of(b"52532120300", 0)[..12]).is_some());
    }

    #[test]
    fn settings_round_trip() {
        let s = Settings {
            reboot_min: 12,
            reset_pulse: 15,
            power_off_s: 0,
            power_delay_s: 9,
            power_on_pulse: 10,
            ch1: ChannelMode::Power,
            ch2: ChannelMode::OutputClosed,
            reboot_limit: 3,
            in_mode: InMode::Input,
            temp_limit: 70,
            internal_sensor: true,
        };
        let r = s.report();
        assert_eq!(&r[..12], b"\x05CF09A243146");
        assert_eq!(r[12], 1);
        assert_eq!(s.text(), "CF09A243146");
        assert_eq!(Settings::parse(&r), Some(s));
        // Clamped rather than sent out of range.
        let wild = Settings {
            reboot_min: 200,
            ..Settings::default()
        };
        assert_eq!(wild.report()[1], b'F');
    }

    #[test]
    fn an_edit_follows_the_device_where_untouched() {
        let base = Settings::default();
        let edit = Settings {
            reboot_min: 9,
            ..base
        };
        // Changed elsewhere: TempLimit and the timer.
        let device = Settings {
            reboot_min: 7,
            temp_limit: 60,
            ..base
        };
        let merged = edit.rebased(&base, &device);
        assert_eq!(merged.reboot_min, 9);
        assert_eq!(merged.temp_limit, 60);
        assert_eq!(merged.ch1, base.ch1);
        // No edit: the device as it is.
        assert_eq!(base.rebased(&base, &device), device);
    }

    #[test]
    fn write_status_byte() {
        let mut r = [0u8; DATA_LEN];
        r[0] = REPORT_DATA;
        r[2] = 1;
        r[15] = 2;
        assert_eq!(Data::parse(&r, true).unwrap().write, WriteStatus::Saved);
        // Not read from firmware older than 1.8U.
        assert_eq!(Data::parse(&r, false).unwrap().write, WriteStatus::Idle);
        r[15] = 4;
        assert_eq!(Data::parse(&r, true).unwrap().write, WriteStatus::Invalid);
        r[15] = 9;
        assert_eq!(Data::parse(&r, true).unwrap().write, WriteStatus::Other(9));
    }

    #[test]
    fn command_codes_match_firmware_table() {
        // NULL, "~U", "~T1", "~T2", "~T3", "~T0", …, "~L0", "~L1", "~P1", "~P0"
        assert_eq!(Command::Heartbeat.report(), [1, 1]);
        assert_eq!(Command::TestReset.code(), 2);
        assert_eq!(Command::TestPowerCycle.code(), 3);
        assert_eq!(Command::TestShutdown.code(), 4);
        assert_eq!(Command::SelfTest.code(), 5);
        assert_eq!(Command::Led(false).code(), 14);
        assert_eq!(Command::Led(true).code(), 15);
        assert_eq!(Command::Pause(true).code(), 16);
        assert_eq!(Command::Pause(false).code(), 17);
    }
}
