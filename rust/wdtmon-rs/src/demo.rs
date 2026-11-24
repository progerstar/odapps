//! An in-memory watchdog speaking the 1.8U HID reports: the engine tests
//! use it, and debug builds run on it with `WDTMON_DEMO=1` (no
//! hardware needed to work on the window).

use std::sync::{Arc, Mutex, MutexGuard};
use std::time::Instant;

use crate::hid::{Bus, Device, Found, OpenError, Settings};

pub struct FakeState {
    pub plugged: Vec<String>,
    pub denied: bool,
    pub version: String,
    pub commands: Vec<(String, u8)>,
    /// Feature report 5 after its ID: 11 characters and the source byte.
    pub settings: [u8; 12],
    /// The next settings write stalls without a reason in byte 15.
    pub stall: bool,
    /// Byte 15 of the data report.
    pub write_status: u8,
    /// The accepted report, stored on the second data report read after it.
    parked: Option<[u8; 12]>,
    /// The pending status of the parked report was read once.
    seen: bool,
    /// The next write is answered `Busy` (a forced action is pending).
    pub busy: bool,
    /// Serials whose device cannot be opened (held by another program).
    pub unopenable: Vec<String>,
    /// Reading the settings report fails.
    pub no_settings: bool,
    /// When CH1 was last pulsed by a test command.
    pulse: Option<Instant>,
    started: Instant,
}

impl FakeState {
    /// A USB reset in the middle of a write: byte 15 starts over at 0. The
    /// accepted report is kept, `stored` already or later.
    #[cfg(test)]
    pub fn usb_reset(&mut self, stored: bool) {
        if stored && let Some(parked) = self.parked.take() {
            self.settings = parked;
        }
        self.write_status = 0;
    }
}

impl Default for FakeState {
    fn default() -> Self {
        let mut settings = [0u8; 12];
        settings.copy_from_slice(&Settings::default().report()[1..]);
        Self {
            plugged: Vec::new(),
            denied: false,
            version: String::new(),
            commands: Vec::new(),
            settings,
            stall: false,
            write_status: 0,
            parked: None,
            seen: false,
            busy: false,
            unopenable: Vec::new(),
            no_settings: false,
            pulse: None,
            started: Instant::now(),
        }
    }
}

#[derive(Clone, Default)]
pub struct FakeBus(pub Arc<Mutex<FakeState>>);

impl FakeBus {
    pub fn pro2(serials: &[&str]) -> Self {
        let bus = Self::default();
        {
            let mut st = bus.state();
            st.plugged = serials.iter().map(|s| s.to_string()).collect();
            st.version = "1.8U+3f2a1c9".into();
        }
        bus
    }

    pub fn state(&self) -> MutexGuard<'_, FakeState> {
        self.0.lock().unwrap_or_else(|e| e.into_inner())
    }

    #[cfg(test)]
    pub fn commands(&self) -> Vec<(String, u8)> {
        self.state().commands.clone()
    }
}

impl Bus for FakeBus {
    fn scan(&mut self) -> Result<Vec<Found>, String> {
        let st = self.state();
        Ok(st
            .plugged
            .iter()
            .map(|s| Found {
                serial: s.clone(),
                path: std::ffi::CString::new(s.as_str()).unwrap_or_default(),
            })
            .collect())
    }

    fn open(&mut self, found: &Found) -> Result<Box<dyn Device>, OpenError> {
        let st = self.state();
        if st.denied {
            return Err(OpenError::AccessDenied("permission denied".into()));
        }
        if st.unopenable.contains(&found.serial) {
            return Err(OpenError::Other("device busy".into()));
        }
        drop(st);
        Ok(Box::new(FakeDevice {
            serial: found.serial.clone(),
            state: self.0.clone(),
        }))
    }
}

struct FakeDevice {
    serial: String,
    state: Arc<Mutex<FakeState>>,
}

impl FakeDevice {
    fn present(&self) -> Result<MutexGuard<'_, FakeState>, String> {
        let st = self.state.lock().unwrap_or_else(|e| e.into_inner());
        if !st.plugged.contains(&self.serial) {
            return Err("device disconnected".into());
        }
        Ok(st)
    }
}

impl Device for FakeDevice {
    fn write(&mut self, report: &[u8]) -> Result<(), String> {
        let serial = self.serial.clone();
        let mut st = self.present()?;
        if report.len() != 2 || report[0] != 1 {
            return Err(format!("bad output report {report:02X?}"));
        }
        if (2..=5).contains(&report[1]) {
            st.pulse = Some(Instant::now());
        }
        st.commands.push((serial, report[1]));
        Ok(())
    }

    fn feature(&mut self, id: u8, len: usize) -> Result<Vec<u8>, String> {
        let mut st = self.present()?;
        let mut r = vec![0u8; len];
        r[0] = id;
        match id {
            2 => r[1..=st.version.len()].copy_from_slice(st.version.as_bytes()),
            4 => {
                // The main loop stores an accepted report a moment later.
                if let Some(parked) = st.parked {
                    if std::mem::take(&mut st.seen) {
                        st.parked = None;
                        if std::mem::take(&mut st.busy) {
                            st.write_status = 3;
                        } else {
                            st.settings = parked;
                            st.write_status = 2;
                        }
                    } else {
                        st.seen = true;
                    }
                }
                r[15] = st.write_status;
                // CH1 closes for a moment after a test, like the reset pulse.
                r[1] = u8::from(st.pulse.is_none_or(|t| t.elapsed().as_millis() > 1500));
                r[2] = 1;
                r[3] = 3;
                // 21.25 °C, drifting slowly in the demo.
                let drift = (st.started.elapsed().as_secs() / 10 % 4) as i16 * 625;
                r[4] = 21;
                r[5..7].copy_from_slice(&(2500 + drift).to_le_bytes());
            }
            5 if st.no_settings => return Err("stall".into()),
            5 => {
                r[1..13].copy_from_slice(&st.settings);
                r.truncate(13);
            }
            _ => return Err("stall".into()),
        }
        Ok(r)
    }

    fn set_feature(&mut self, report: &[u8]) -> Result<(), String> {
        let mut st = self.present()?;
        let stall = Err("stall".to_string());
        if std::mem::take(&mut st.stall)
            || report.first() != Some(&5)
            || !(13..=17).contains(&report.len())
        {
            return stall;
        }
        if st.parked.is_some() {
            return stall;
        }
        let mut body = [0u8; 12];
        body.copy_from_slice(&report[1..13]);
        if Settings::parse(&report[..13]).is_none() || body[11] > 1 {
            st.write_status = 4;
            return stall;
        }
        // Set before the transfer is acknowledged.
        st.write_status = 1;
        st.parked = Some(body);
        Ok(())
    }
}
