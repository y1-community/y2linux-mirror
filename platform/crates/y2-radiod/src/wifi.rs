//! Wi-Fi's power and the shared radio core: the per-device rfkill switch (never the type-wide one,
//! F37), the core's failure record and its reset. Association stays wpa_supplicant's.
// SPDX-License-Identifier: GPL-2.0-only
use std::{
    fs, thread,
    time::{Duration, Instant},
};
use y2p_core::Root;

const CORE: &str = "/sys/devices/platform/18070000.connectivity";

/// The core's `status` line: `activated=… powered=… functions=… error=…`.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Core {
    pub powered: bool,
    pub functions: u32,
    pub error: i64,
    pub known: bool,
}

pub fn core(root: &Root) -> Core {
    let Some(text) = root.read(&format!("{CORE}/status")) else {
        return Core::default();
    };
    let mut core = Core {
        known: true,
        ..Core::default()
    };
    for field in text.split_whitespace() {
        let Some((name, value)) = field.split_once('=') else {
            continue;
        };
        match name {
            "powered" => core.powered = value == "1",
            "functions" => {
                let hex = value.strip_prefix("0x");
                core.functions = match hex {
                    Some(hex) => u32::from_str_radix(hex, 16).unwrap_or(0),
                    None => value.parse().unwrap_or(0),
                };
            }
            "error" => core.error = value.parse().unwrap_or(0),
            _ => {}
        }
    }
    core
}

/// A core whose last failure stopped it (every function off, an error left): reset it before a
/// radio starts, as `y2-radio` did, and wait up to 15 s for the error to clear.
pub fn recover_if_failed(root: &Root) -> bool {
    let state = core(root);
    if !state.known || state.functions != 0 || state.error == 0 {
        return false;
    }
    reset(root)
}

/// Write `recover` (both functions must be off) and wait for the error to clear.
pub fn reset(root: &Root) -> bool {
    if fs::write(root.path(&format!("{CORE}/recover")), "1").is_err() {
        return false;
    }
    let deadline = Instant::now() + Duration::from_secs(15);
    while Instant::now() < deadline {
        if core(root).error == 0 {
            return true;
        }
        thread::sleep(Duration::from_millis(100));
    }
    false
}

/// Every Wi-Fi rfkill switch (the driver's own and cfg80211's phy).
fn switches(root: &Root) -> Vec<std::path::PathBuf> {
    let mut found: Vec<_> = root
        .glob("/sys/class/rfkill/rfkill*")
        .into_iter()
        .filter(|device| {
            fs::read_to_string(device.join("type")).is_ok_and(|kind| kind.trim() == "wlan")
        })
        .collect();
    found.sort();
    found
}

/// Whether any Wi-Fi switch is unblocked.
pub fn is_on(root: &Root) -> bool {
    switches(root)
        .iter()
        .any(|device| fs::read_to_string(device.join("soft")).is_ok_and(|soft| soft.trim() == "0"))
}

/// Switch Wi-Fi on or off. Per device, never the type: blocking the type also sets cfg80211's
/// default, and the phy made at the next switch-on started blocked (F37).
pub fn set(root: &Root, on: bool) {
    if on {
        recover_if_failed(root);
    }
    for device in switches(root) {
        // The phy switch may vanish meanwhile: blocking the driver's removes the card.
        let _ = fs::write(device.join("soft"), if on { "0" } else { "1" });
    }
}

/// Off and on once: block, wait (at most 3 s) for the driver to take wlan0 away, unblock (F60).
pub fn restart(root: &Root) -> bool {
    set(root, false);
    let netdev = root.path("/sys/class/net/wlan0");
    let deadline = Instant::now() + Duration::from_secs(3);
    while netdev.exists() && Instant::now() < deadline {
        thread::sleep(Duration::from_millis(100));
    }
    let gone = !netdev.exists();
    set(root, true);
    gone
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn core_status_reads_hex_and_decimal_functions() {
        let dir = std::env::temp_dir().join(format!("y2-radiod-core-{}", std::process::id()));
        let status = dir.join("sys/devices/platform/18070000.connectivity");
        fs::create_dir_all(&status).expect("fixture");
        fs::write(
            status.join("status"),
            "activated=1 powered=1 functions=0x3 error=0\n",
        )
        .expect("fixture");
        let root = Root::new(&dir);
        assert_eq!(
            core(&root),
            Core {
                powered: true,
                functions: 3,
                error: 0,
                known: true
            }
        );
        fs::write(
            status.join("status"),
            "activated=1 powered=0 functions=0 error=-5\n",
        )
        .expect("fixture");
        let failed = core(&root);
        assert_eq!((failed.functions, failed.error), (0, -5));
        fs::remove_dir_all(dir).expect("cleanup");
    }
}
