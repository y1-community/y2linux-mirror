//! What each asynchronous call's answer means for the controller.
// SPDX-License-Identifier: GPL-2.0-only
use super::{
    bus::{Answer, Call, Kind},
    links, Controller,
};
use std::time::{Duration, Instant};
use y2p_contract::radiod::Scan;

impl Controller {
    pub(super) fn answer(&mut self, call: Call, answer: Answer) {
        if let Err((name, message)) = &answer {
            self.journal.event(
                "call_failed",
                serde_json::json!({"call": label(&call.kind), "error": name, "message": message}),
            );
        }
        match call.kind {
            Kind::Power(on) => self.powered(on, answer),
            Kind::Connect(address) => self.connect_answer(&address, answer),
            Kind::Cancel(_) | Kind::Volume | Kind::Quiet | Kind::Profile => {}
            Kind::Disconnect(address) => {
                if answer.is_err() {
                    let message = "Couldn't disconnect. Try again.";
                    self.fail("disconnect_failed", message, Some(&address));
                }
            }
            Kind::Pair(address) => self.paired(&address, answer),
            Kind::Discovery(on) => self.discovery_answer(on, answer),
            Kind::Select { address, codec, .. } => self.selected(&address, &codec, answer),
            Kind::Remove(address) => {
                if answer.is_err() {
                    self.fail(
                        "forget_failed",
                        "Couldn't forget the device.",
                        Some(&address),
                    );
                }
            }
        }
    }

    fn powered(&mut self, on: bool, answer: Answer) {
        self.power.in_flight = false;
        match answer {
            Ok(()) => self.power.failures = 0,
            Err(_) => {
                self.power.failures += 1;
                self.power.next =
                    Instant::now() + Duration::from_secs(5 * u64::from(self.power.failures));
                if on && self.power.failures >= 3 {
                    self.power.failures = 0;
                    self.recover(super::ladder::Rung::BluezRestart, None);
                }
            }
        }
    }

    fn connect_answer(&mut self, address: &str, answer: Answer) {
        let now = Instant::now();
        let error = match answer {
            Ok(()) => {
                let record = self.record(address);
                record.connecting = None;
                record.episode = None;
                record.parked = false;
                return;
            }
            Err((name, message)) => format!("{name} {message}"),
        };
        if links::already(&error) {
            self.record(address).connecting = None;
            return;
        }
        if links::in_progress(&error) {
            // BlueZ already connects it (the headset came back on its own): give it time.
            let record = self.record(address);
            record.in_progress = true;
            record.connecting.get_or_insert(now);
            return;
        }
        if error.starts_with("timeout") {
            self.cancel_connect(address);
            return;
        }
        if self.world.devices.get(address).is_some_and(|d| d.connected) {
            // The link is up although the call failed (a profile after it did, #94): nothing to tell
            // the owner; a link that never sets up is cancelled as half connected.
            let record = self.record(address);
            record.connecting = None;
            record.in_progress = false;
            return;
        }
        let asked = self.asked.as_deref() == Some(address);
        let name = self
            .world
            .devices
            .get(address)
            .map(|d| d.name.clone())
            .unwrap_or_default();
        let record = self.record(address);
        record.connecting = None;
        record.in_progress = false;
        let more = record
            .episode
            .as_mut()
            .is_some_and(|episode| episode.failed(now));
        if !more {
            record.parked = true;
            record.episode = None;
            if asked {
                let message =
                    format!("Couldn't connect to {name}. Make sure they're on and nearby.");
                self.fail("connect_failed", &message, Some(address));
            }
        }
    }

    fn paired(&mut self, address: &str, answer: Answer) {
        let result = match answer {
            // Paired already (a second tap): go on as if this pairing made it.
            Err((name, _)) if name.ends_with("AlreadyExists") => Ok(()),
            other => other.map_err(|(name, message)| pairing_message(&name, &message)),
        };
        let result = result.and_then(|()| self.trust(address));
        self.close_pairing_window();
        match result {
            Ok(()) => self.use_paired(address),
            Err(message) => {
                self.cancel_pairing(address);
                self.fail("pair_failed", &message, Some(address));
            }
        }
    }

    /// A new pairing is the owner's audio device from now on, connected at once.
    pub(super) fn use_paired(&mut self, address: &str) {
        self.asked = Some(address.to_owned());
        self.desired.preferred = Some(address.to_owned());
        self.desired.save_preferred(&self.root);
        self.trigger(address, links::Trigger::User);
    }

    /// A bond was saved: trust it so it may reconnect.
    pub(super) fn trust(&mut self, address: &str) -> Result<(), String> {
        let (Some(owner), Some(path)) = (
            self.world.bluez.clone(),
            self.world.devices.get(address).map(|d| d.path.clone()),
        ) else {
            return Err("The device went away while pairing.".into());
        };
        let bus = self.bus.as_ref().ok_or("Bluetooth is not ready")?;
        let paired: bool =
            super::world::get(&bus.channel, &owner, &path, "org.bluez.Device1", "Paired")?;
        let bonded: bool =
            super::world::get(&bus.channel, &owner, &path, "org.bluez.Device1", "Bonded")?;
        if !paired || !bonded {
            return Err(
                "Pairing did not save the headphones. Put them in pairing mode and try again."
                    .into(),
            );
        }
        bus.set_now((&owner, &path, "org.bluez.Device1", "Trusted"), true)
    }

    fn cancel_pairing(&mut self, address: &str) {
        let (Some(owner), Some(path)) = (
            self.world.bluez.clone(),
            self.world.devices.get(address).map(|d| d.path.clone()),
        ) else {
            return;
        };
        if let Some(bus) = self.bus.as_mut() {
            let target = (
                owner.as_str(),
                path.as_str(),
                "org.bluez.Device1",
                "CancelPairing",
            );
            bus.call(target, (), Kind::Quiet, Duration::from_secs(3));
        }
    }

    fn discovery_answer(&mut self, on: bool, answer: Answer) {
        let Err((name, _)) = answer else {
            return;
        };
        // Already discovering, or an answer later than our wait while the adapter discovers: both
        // mean it runs (reports 685c003a, 09cabe08, dbe84d46).
        let runs = name.ends_with("InProgress")
            || (name == "timeout" && self.world.adapter.as_ref().is_some_and(|a| a.discovering));
        if on && !runs && self.discovery.is_some() {
            self.discovery = None;
            self.scan = Scan::Failed {
                message: "Couldn't search for devices. Try again.".into(),
            };
        }
    }
}

/// The owner's words for a failed pairing.
fn pairing_message(name: &str, message: &str) -> String {
    if name.ends_with("AuthenticationRejected") || name.ends_with("AuthenticationCanceled") {
        "Pairing was declined.".into()
    } else if name == "timeout" || name.ends_with("AuthenticationTimeout") {
        "Pairing took too long. Put the headphones in pairing mode and try again.".into()
    } else if name.ends_with("AlreadyExists") {
        "These headphones are already paired.".into()
    } else if message.contains("Page Timeout") || message.contains("Host is down") {
        "Couldn't reach the headphones. Put them in pairing mode and try again.".into()
    } else {
        "Pairing failed. Put the headphones in pairing mode and try again.".into()
    }
}

fn label(kind: &Kind) -> &'static str {
    match kind {
        Kind::Power(_) => "power",
        Kind::Connect(_) => "connect",
        Kind::Cancel(_) => "cancel",
        Kind::Disconnect(_) => "disconnect",
        Kind::Pair(_) => "pair",
        Kind::Discovery(_) => "discovery",
        Kind::Select { .. } => "select_codec",
        Kind::Volume => "volume",
        Kind::Quiet => "quiet",
        Kind::Profile => "a2dp_profile",
        Kind::Remove(_) => "remove",
    }
}
