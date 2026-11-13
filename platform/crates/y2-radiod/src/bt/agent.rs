//! The pairing agent (`DisplayYesNo`): BlueZ's questions become the owner's yes/no on the screen;
//! an unanswered one is refused after 60 s, other kinds (PIN, passkey entry) are refused at once.
//! Pairing also needs the adapter Pairable for a bounded window (Linux strips bonding otherwise).
//! A search for devices also makes the player visible for a while, so that a device which pairs
//! from its own side (a car, Y2Linux #116) finds it; the owner confirms its number here as well.
// SPDX-License-Identifier: GPL-2.0-only
use super::{bus::Kind, Controller};
use dbus::{blocking::Proxy, Message};
use std::time::{Duration, Instant};
use y2p_contract::radiod::Pairing;

pub const PATH: &str = "/org/y2/agent";
const ANSWER_WITHIN: Duration = Duration::from_secs(60);
/// How long a search for devices leaves the player visible and pairable: time to open a car's
/// "add device" list, pick the player and confirm the number on both screens.
pub const VISIBLE_FOR: Duration = Duration::from_secs(120);
/// How long BlueZ may take to bond a device the owner let pair from its side, and to learn
/// whether it plays audio.
const WELCOME_WITHIN: Duration = Duration::from_secs(60);

#[derive(Default)]
pub struct Agent {
    question: Option<(Message, Pairing, Instant)>,
    /// The adapter's Pairable and PairableTimeout before a pairing opened the window.
    window: Option<(String, bool, u32)>,
    /// Until when the player is visible to other devices (`Controller::show_to_others`).
    visible_until: Option<Instant>,
    /// A device that paired from its own side with the owner's yes: its path, when the owner said
    /// yes, and whether it is trusted yet (`Controller::adopt_welcomed`).
    welcomed: Option<(String, Instant, bool)>,
}

impl Agent {
    pub fn deadline(&self) -> Option<Instant> {
        let question = self.question.as_ref().map(|(_, _, at)| *at + ANSWER_WITHIN);
        let welcome = self
            .welcomed
            .as_ref()
            .map(|(_, at, _)| *at + WELCOME_WITHIN);
        [question, self.visible_until, welcome]
            .into_iter()
            .flatten()
            .min()
    }

    pub fn shown(&self) -> Option<Pairing> {
        self.question
            .as_ref()
            .map(|(_, pairing, _)| pairing.clone())
    }
}

fn rejected(message: &Message) -> Message {
    message.error(
        &"org.bluez.Error.Rejected".into(),
        c"Pairing requires explicit user confirmation",
    )
}

impl Controller {
    /// Register the agent with the BlueZ that runs now (again after BlueZ restarted).
    pub(super) fn register_agent(&mut self) {
        let Some(owner) = self.world.bluez.clone() else {
            return;
        };
        let Some(bus) = self.bus.as_mut() else {
            return;
        };
        if bus.agent_owner.as_deref() == Some(owner.as_str()) {
            return;
        }
        let Ok(path) = dbus::Path::new(PATH) else {
            return;
        };
        let registered: Result<(), dbus::Error> = Proxy::new(
            owner.as_str(),
            "/org/bluez",
            Duration::from_secs(3),
            &bus.channel,
        )
        .method_call(
            "org.bluez.AgentManager1",
            "RegisterAgent",
            (path, "DisplayYesNo"),
        );
        bus.agent_owner = Some(owner);
        if let Err(error) = registered {
            let name = error.name().unwrap_or("no answer").to_owned();
            self.journal
                .event("agent_unavailable", serde_json::json!({"error": name}));
        }
    }

    /// A question from BlueZ to the agent.
    pub(super) fn agent_call(&mut self, message: Message) {
        let Some(bus) = &self.bus else {
            return;
        };
        if message.path().is_none_or(|p| &*p != PATH) {
            bus.send(message.error(
                &"org.freedesktop.DBus.Error.UnknownObject".into(),
                c"No such object",
            ));
            return;
        }
        let member = message.member().map(|m| m.to_string()).unwrap_or_default();
        match member.as_str() {
            "Release" | "Cancel" => {
                if let Some((old, _, _)) = self.pairing.question.take() {
                    bus.send(rejected(&old));
                }
                bus.send(message.method_return());
            }
            "RequestConfirmation" | "RequestAuthorization" | "AuthorizeService" => {
                let device = message
                    .read1::<dbus::Path>()
                    .map(|p| p.to_string())
                    .unwrap_or_default();
                // The device the owner just let pair (a car) opens its audio right after: the
                // owner's yes covers that, a second question would only be in the way.
                let welcomed = self
                    .pairing
                    .welcomed
                    .as_ref()
                    .is_some_and(|(path, _, _)| *path == device);
                if member == "AuthorizeService" && welcomed {
                    bus.send(message.method_return());
                    return;
                }
                if self.pairing.question.is_some() {
                    bus.send(rejected(&message));
                    return;
                }
                let display = if member == "RequestConfirmation" {
                    message
                        .read2::<dbus::Path, u32>()
                        .map(|(_, n)| format!("Confirm {n:06}"))
                        .unwrap_or_else(|_| "Confirm pairing".into())
                } else {
                    "Allow audio device?".into()
                };
                self.pairing.question.replace((
                    message,
                    Pairing { device, display },
                    Instant::now(),
                ));
            }
            _ => bus.send(rejected(&message)),
        }
    }

    /// The owner's answer to the question shown.
    pub(super) fn confirm(&mut self, accept: bool) -> bool {
        let Some((message, pairing, _)) = self.pairing.question.take() else {
            return false;
        };
        // A pairing this player did not start: the other device pairs from its side.
        let pairing_question = matches!(
            message.member().as_deref(),
            Some("RequestConfirmation" | "RequestAuthorization")
        );
        if accept && pairing_question && !self.in_flight(|k| matches!(k, Kind::Pair(_))) {
            self.pairing.welcomed = Some((pairing.device, Instant::now(), false));
        }
        if let Some(bus) = &self.bus {
            bus.send(if accept {
                message.method_return()
            } else {
                rejected(&message)
            });
        }
        true
    }

    /// Refuse a question nobody answered in time; end a visibility or a welcome that ran out.
    pub(super) fn expire_question(&mut self, now: Instant) {
        let asked = self.pairing.question.as_ref();
        if asked.is_some_and(|(_, _, at)| *at + ANSWER_WITHIN <= now) {
            self.confirm(false);
            self.journal
                .event("pairing_unanswered", serde_json::json!({}));
        }
        if self.pairing.visible_until.is_some_and(|at| at <= now) {
            self.hide();
            // A pairing of our own closes its window when it is answered.
            if !self.in_flight(|k| matches!(k, Kind::Pair(_))) {
                self.close_pairing_window();
            }
        }
        let welcome = self.pairing.welcomed.as_ref();
        if welcome.is_some_and(|(_, at, _)| *at + WELCOME_WITHIN <= now) {
            self.pairing.welcomed = None;
            self.journal
                .event("welcome_unfinished", serde_json::json!({}));
        }
    }

    /// A device that paired from its own side with the owner's yes (a car, Y2Linux #116): trusted
    /// once BlueZ bonded it (it reconnects without a question), and used for audio like headphones
    /// paired here once its services show that it plays audio.
    pub(super) fn adopt_welcomed(&mut self) {
        let Some((path, _, trusted)) = self.pairing.welcomed.clone() else {
            return;
        };
        let Some(dev) = self.world.devices.values().find(|d| d.path == path) else {
            return;
        };
        let (address, bonded, audio) = (dev.address.clone(), dev.paired && dev.bonded, dev.audio);
        if !trusted {
            if !bonded {
                return;
            }
            self.close_pairing_window();
            if let Err(message) = self.trust(&address) {
                self.pairing.welcomed = None;
                self.fail("pair_failed", &message, Some(&address));
                return;
            }
            if let Some(welcome) = self.pairing.welcomed.as_mut() {
                welcome.2 = true;
            }
            self.journal
                .event("paired_from_device", serde_json::json!({}));
        }
        if audio {
            self.pairing.welcomed = None;
            self.use_paired(&address);
        }
    }

    /// Visible and pairable to other devices for `VISIBLE_FOR` (BlueZ's own timers end both even if
    /// this daemon ends): a car's search lists the player and pairs from its side.
    pub(super) fn show_to_others(&mut self) {
        let shown = self.open_pairing_window(VISIBLE_FOR).and_then(|()| {
            let owner = self.world.bluez.clone().ok_or("Bluetooth is not ready")?;
            let adapter = self.pairing.window.as_ref().map(|w| w.0.clone());
            let adapter = adapter.ok_or("Bluetooth is not ready")?;
            let bus = self.bus.as_ref().ok_or("Bluetooth is not ready")?;
            let target = |name| (owner.as_str(), adapter.as_str(), "org.bluez.Adapter1", name);
            bus.set_now(target("DiscoverableTimeout"), VISIBLE_FOR.as_secs() as u32)?;
            bus.set_now(target("Discoverable"), true)
        });
        match shown {
            Ok(()) => self.pairing.visible_until = Some(Instant::now() + VISIBLE_FOR),
            Err(error) => self
                .journal
                .event("visible_failed", serde_json::json!({"error": error})),
        }
    }

    /// No longer visible to other devices.
    fn hide(&mut self) {
        if self.pairing.visible_until.take().is_none() {
            return;
        }
        let (Some(owner), Some(adapter), Some(bus)) = (
            self.world.bluez.clone(),
            self.world.adapter.as_ref().map(|a| a.path.clone()),
            self.bus.as_ref(),
        ) else {
            return;
        };
        let _ = bus.set_now(
            (&owner, &adapter, "org.bluez.Adapter1", "Discoverable"),
            false,
        );
    }

    /// Make the adapter Pairable for `open` (BlueZ's own timer closes it even if this daemon ends).
    pub(super) fn open_pairing_window(&mut self, open: Duration) -> Result<(), String> {
        let (Some(owner), Some(adapter)) = (
            self.world.bluez.clone(),
            self.world.adapter.as_ref().map(|a| a.path.clone()),
        ) else {
            return Err("Bluetooth is not ready".into());
        };
        let bus = self.bus.as_ref().ok_or("Bluetooth is not ready")?;
        if self.pairing.window.is_none() {
            let pairable: bool = super::world::get(
                &bus.channel,
                &owner,
                &adapter,
                "org.bluez.Adapter1",
                "Pairable",
            )?;
            let timeout: u32 = super::world::get(
                &bus.channel,
                &owner,
                &adapter,
                "org.bluez.Adapter1",
                "PairableTimeout",
            )?;
            self.pairing.window = Some((adapter.clone(), pairable, timeout));
        }
        let target = |name| (owner.as_str(), adapter.as_str(), "org.bluez.Adapter1", name);
        bus.set_now(target("PairableTimeout"), open.as_secs() as u32)?;
        bus.set_now(target("Pairable"), true)
    }

    /// Put Pairable back as it was, and no longer visible.
    pub(super) fn close_pairing_window(&mut self) {
        self.hide();
        let Some((adapter, pairable, timeout)) = self.pairing.window.take() else {
            return;
        };
        let (Some(owner), Some(bus)) = (self.world.bluez.clone(), self.bus.as_ref()) else {
            return;
        };
        let target = |name| (owner.as_str(), adapter.as_str(), "org.bluez.Adapter1", name);
        let _ = bus.set_now(target("Pairable"), pairable);
        let _ = bus.set_now(target("PairableTimeout"), timeout);
    }
}
