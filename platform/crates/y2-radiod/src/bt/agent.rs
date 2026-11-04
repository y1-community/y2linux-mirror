//! The pairing agent (`DisplayYesNo`): BlueZ's questions become the owner's yes/no on the screen;
//! an unanswered one is refused after 60 s, other kinds (PIN, passkey entry) are refused at once.
//! Pairing also needs the adapter Pairable for a bounded window (Linux strips bonding otherwise).
// SPDX-License-Identifier: GPL-2.0-only
use super::Controller;
use dbus::{blocking::Proxy, Message};
use std::time::{Duration, Instant};
use y2p_contract::radiod::Pairing;

pub const PATH: &str = "/org/y2/agent";
const ANSWER_WITHIN: Duration = Duration::from_secs(60);

#[derive(Default)]
pub struct Agent {
    question: Option<(Message, Pairing, Instant)>,
    /// The adapter's Pairable and PairableTimeout before a pairing opened the window.
    window: Option<(String, bool, u32)>,
}

impl Agent {
    pub fn deadline(&self) -> Option<Instant> {
        self.question.as_ref().map(|(_, _, at)| *at + ANSWER_WITHIN)
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
                if self.pairing.question.is_some() {
                    bus.send(rejected(&message));
                    return;
                }
                let device = message
                    .read1::<dbus::Path>()
                    .map(|p| p.to_string())
                    .unwrap_or_default();
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
        let Some((message, _, _)) = self.pairing.question.take() else {
            return false;
        };
        if let Some(bus) = &self.bus {
            bus.send(if accept {
                message.method_return()
            } else {
                rejected(&message)
            });
        }
        true
    }

    /// Refuse a question nobody answered in time.
    pub(super) fn expire_question(&mut self, now: Instant) {
        if self.pairing.deadline().is_some_and(|at| at <= now) {
            self.confirm(false);
            self.journal
                .event("pairing_unanswered", serde_json::json!({}));
        }
    }

    /// Make the adapter Pairable for 90 s (BlueZ's own timer closes it even if this daemon ends).
    pub(super) fn open_pairing_window(&mut self) -> Result<(), String> {
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
        bus.set_now(target("PairableTimeout"), 90u32)?;
        bus.set_now(target("Pairable"), true)
    }

    /// Put Pairable back as it was.
    pub(super) fn close_pairing_window(&mut self) {
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
