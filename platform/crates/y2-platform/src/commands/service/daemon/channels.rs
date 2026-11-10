//! Bluetooth keeps off the Wi-Fi network's channel: while Wi-Fi is connected on 2.4 GHz, the
//! controller is told which of its 79 channels that network covers (AFH host channel
//! classification, sent by `y2-bt-afh`), and told again that all are free once Wi-Fi leaves.
//!
//! Both radios share one antenna. The chip takes turns between its own two radios, but the access
//! point transmits whenever it likes, and Bluetooth packets on its channel are lost and sent
//! again. On a player next to its router (2427 MHz, 2026-11-10) Bluetooth used 66-70 channels and
//! left out only 4 of the router's 23; with the classification it left all 23 within 2 s, and
//! took them back after `clear`. Phones do this; BlueZ has no interface for it.
// SPDX-License-Identifier: GPL-2.0-only
use super::{atomic_json, json, Root};
use crate::commands::connectivity::{direct, radiod};
use crate::commands::observation::snapshot::Wifi;
use y2p_core::command;

pub(super) const TOOL: &str = "/usr/sbin/y2-bt-afh";
pub(super) const RECORD: &str = "/run/y2/bt-channel-mask.json";
/// While a network is masked the classification is sent again this often: the controller forgets
/// it when it is reset (a recovery step, a restart of BlueZ) without the adapter's switch being
/// seen off here, and an access point may change its channel without a new association.
const REFRESH_S: f64 = 30.0;

/// What the controller was last told, for which network (None: every channel is free), and when.
#[derive(Default)]
pub(super) struct ChannelMask {
    told: Option<(Option<String>, f64)>,
}

impl ChannelMask {
    /// One pass: tell the controller when the network changed, when Bluetooth came on, or when a
    /// mask is due again. Nothing runs while Bluetooth is off, or while nothing is masked and
    /// nothing changed.
    pub(super) fn follow(&mut self, root: &Root, wifi: &Wifi, now: f64) {
        let powered = radiod::written(root).is_some_and(|state| state.bluetooth.powered);
        if !powered {
            // A controller that was off has no classification left.
            self.told = None;
            return;
        }
        let network = (wifi.association.as_deref() == Some("COMPLETED"))
            .then(|| wifi.association_id.clone())
            .flatten();
        let due = match &self.told {
            None => true,
            Some((told, at)) => *told != network || (network.is_some() && now - at >= REFRESH_S),
        };
        if !due {
            return;
        }
        let mhz = network.as_ref().and_then(|_| wifi_mhz(root));
        let argument = mhz.map_or("clear".to_owned(), |mhz| mhz.to_string());
        let answer = command::run(root, &[TOOL, &argument], 3.0);
        self.told = Some((network, now));
        let _ = atomic_json(
            &root.path(RECORD),
            &json!({
                "schema": 1,
                "monotonic_s": now,
                "wifi_mhz": mhz,
                "answer": answer.output.as_deref().and_then(|text| {
                    serde_json::from_str::<serde_json::Value>(text.trim()).ok()
                }),
            }),
            false,
        );
    }
}

/// The connected network's centre frequency when it is in the 2.4 GHz band (wpa_supplicant's
/// `freq=` in its status). A 5 GHz network, or no answer, masks nothing.
fn wifi_mhz(root: &Root) -> Option<u32> {
    let status = direct::wpa_cli(root, "status", 2.0);
    frequency(status.output.as_deref()?)
}

pub(super) fn frequency(status: &str) -> Option<u32> {
    status
        .lines()
        .find_map(|line| line.strip_prefix("freq="))
        .and_then(|mhz| mhz.trim().parse::<u32>().ok())
        .filter(|mhz| (2412..=2484).contains(mhz))
}
