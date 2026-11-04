//! Group `connectivity`: `connectivity-daemon` (the radio helpers' supervisor) and
//! `wifi-power-policy`, with what the service daemon runs in process (supplicant events, the F52
//! password latch and self-heal, the DNS probe, the Wi-Fi journal). Callers in the production overlay:
//! `usr/libexec/y2/connectivity` (`exec y2-platform connectivity-daemon`), `usr/libexec/y2/wpa-event`
//! (`wifi-event`). Bluetooth and the radios' power belong to `y2-radiod` (platform/crates/y2-radiod).
// SPDX-License-Identifier: GPL-2.0-only
#[cfg(test)]
mod bluetooth_tests;
#[cfg(test)]
pub(crate) mod crosscheck;
pub mod daemon;
pub mod direct;
pub mod observe_wifi;
pub mod published;
pub mod radio_policy;
pub mod radiod;
#[cfg(test)]
mod scenarios;
#[cfg(test)]
pub(crate) mod steps;
pub mod wifi;

use super::Command;

/// The Rust implementation of these exact arguments, if this group has one.
pub fn route(args: &[&str]) -> Option<Command> {
    Some(match args {
        ["connectivity-daemon"] => Box::new(daemon::serve),
        ["wifi-power-policy"] => Box::new(radio_policy::command),
        _ => return None,
    })
}
