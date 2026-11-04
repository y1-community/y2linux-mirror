//! The daemon's main thread: hands Bluetooth requests to the controller and keeps what both radios
//! share — Wi-Fi's power, the wake order (a headset that was connected before the sleep comes back
//! before Wi-Fi starts, so they do not fight over the one antenna), the codec ceiling while Wi-Fi
//! is on, and the published state (subscribers and `/run/y2/radiod-state.json`).
// SPDX-License-Identifier: GPL-2.0-only
use crate::{bt, desired::Desired, journal::Journal, server::Client, wifi};
use std::{
    sync::mpsc::{channel, Receiver, RecvTimeoutError},
    time::{Duration, Instant},
};
use y2p_contract::radiod::{
    Bluetooth, Link, Message, Radio, Reply, Request, Service, State, WifiPower, SCHEMA,
};
use y2p_core::{record, Root};

pub enum Event {
    Request(Request, Client),
    Gone(u64),
    Bluetooth(Box<Bluetooth>),
}

/// How long Wi-Fi waits at a wake for the headset that was connected before the sleep.
const WAKE_HOLD: Duration = Duration::from_secs(12);
const CEILING: &str = "/run/y2/bt-codec-ceiling";
const STATE: &str = "/run/y2/radiod-state.json";

struct Arbiter {
    root: Root,
    journal: Journal,
    bt: bt::Handle,
    desired_wifi: bool,
    bluetooth: Bluetooth,
    wifi: WifiPower,
    /// Wi-Fi waits for this headset at a wake, until then.
    hold: Option<(String, Instant)>,
    /// The connected headset when the sleep began.
    asleep_with: Option<String>,
    subscribers: Vec<Client>,
    sequence: u64,
    /// A Bluetooth stream played at the last look (Wi-Fi's power save follows it).
    streaming: bool,
}

/// `y2-radiod`: exit status 1 when another instance runs or the socket cannot be made.
pub fn serve(root: &Root) -> i32 {
    let runtime = root.path("/run/y2");
    if record::private_directory(&runtime).is_err() {
        return 1;
    }
    let Ok(Some(_lock)) = y2p_core::lock::try_exclusive(&runtime.join("radiod.lock")) else {
        eprintln!("y2-radiod: already running");
        return 1;
    };
    let journal = Journal::new(root);
    let desired = Desired::load(root);
    let (events, received) = channel();
    let desired_wifi = desired.wifi;
    let Ok(bt) = bt::spawn(root, journal.clone(), events.clone(), desired) else {
        return 1;
    };
    if crate::server::spawn(root, crate::SOCKET, events).is_err() {
        return 1;
    }
    let mut arbiter = Arbiter {
        root: root.clone(),
        journal,
        bt,
        desired_wifi,
        bluetooth: Bluetooth::default(),
        wifi: WifiPower::default(),
        hold: None,
        asleep_with: None,
        subscribers: Vec::new(),
        sequence: 0,
        streaming: false,
    };
    arbiter.start();
    arbiter.run(received);
    0
}

impl Arbiter {
    fn start(&mut self) {
        self.wifi.wanted = self.desired_wifi;
        self.apply_wifi(self.desired_wifi);
        self.journal.event(
            "radiod_start",
            serde_json::json!({"wifi": self.desired_wifi}),
        );
        self.publish();
    }

    fn run(&mut self, events: Receiver<Event>) {
        loop {
            // Nothing to wake for but events, and the end of a wake's Wi-Fi hold.
            let received = match &self.hold {
                Some((_, at)) => events.recv_timeout(at.saturating_duration_since(Instant::now())),
                None => events.recv().map_err(|_| RecvTimeoutError::Disconnected),
            };
            match received {
                Ok(event) => self.event(event),
                Err(RecvTimeoutError::Timeout) => {}
                Err(RecvTimeoutError::Disconnected) => return,
            }
            self.release_hold(Instant::now());
        }
    }

    fn event(&mut self, event: Event) {
        match event {
            Event::Request(request, client) => self.request(request, client),
            Event::Gone(id) => self.subscribers.retain(|c| c.id != id),
            Event::Bluetooth(state) => {
                self.bluetooth = *state;
                self.publish();
                self.follow_streaming();
            }
        }
    }

    fn request(&mut self, request: Request, client: Client) {
        match request {
            Request::Subscribe => {
                client.send(&Message::State(Box::new(self.state())));
                self.subscribers.push(client);
            }
            Request::State => {
                client.send(&Message::State(Box::new(self.state())));
            }
            Request::WifiPower { on, persist } => {
                self.wifi.wanted = on;
                if persist {
                    self.desired_wifi = on;
                    let mut desired = Desired::load(&self.root);
                    desired.wifi = on;
                    desired.save_wifi(&self.root);
                }
                self.hold = None;
                self.apply_wifi(on);
                client.reply(Reply::done());
            }
            Request::WifiRestart => {
                let gone = wifi::restart(&self.root);
                self.journal
                    .event("wifi_restart", serde_json::json!({"gone": gone}));
                self.refresh_wifi();
                client.reply(Reply::done());
            }
            Request::Quiesce { radio: Radio::Wifi } => {
                self.hold = None;
                self.apply_wifi(false);
                client.reply(Reply::done());
            }
            Request::Restore { radio: Radio::Wifi } => {
                self.restore_wifi();
                client.reply(Reply::done());
            }
            Request::Quiesce {
                radio: Radio::Bluetooth,
            } => {
                self.asleep_with = self
                    .bluetooth
                    .devices
                    .iter()
                    .find(|d| d.connected && d.audio)
                    .map(|d| d.address.clone());
                self.bt.send(request, client);
            }
            other => self.bt.send(other, client),
        }
    }

    /// At a wake: Wi-Fi on now, or once the headset that was connected before the sleep is back
    /// (at most [`WAKE_HOLD`]).
    fn restore_wifi(&mut self) {
        if !self.wifi.wanted {
            return;
        }
        match self.asleep_with.take() {
            Some(address) if self.bluetooth.wanted => {
                self.hold = Some((address, Instant::now() + WAKE_HOLD));
                self.wifi.held = true;
                self.journal.event("wifi_held", serde_json::json!({}));
                self.publish();
            }
            _ => self.apply_wifi(true),
        }
    }

    fn release_hold(&mut self, now: Instant) {
        let Some((address, until)) = &self.hold else {
            return;
        };
        let back = self
            .bluetooth
            .devices
            .iter()
            .any(|d| &d.address == address && d.link == Link::Ready);
        let gone = !self.bluetooth.wanted || self.bluetooth.service == Service::Failed;
        if back || gone || now >= *until {
            self.journal.event(
                "wifi_released",
                serde_json::json!({"headset_back": back, "waited_out": now >= *until}),
            );
            self.hold = None;
            self.apply_wifi(self.wifi.wanted);
        }
    }

    /// Wi-Fi's power save follows Bluetooth streaming at once (the service daemon's policy pass
    /// comes only every 30 s): the policy reads this daemon's state. Its own thread, never waited for.
    fn follow_streaming(&mut self) {
        let streaming = self
            .bluetooth
            .pcms
            .iter()
            .any(|pcm| pcm.running == Some(true));
        if streaming == self.streaming {
            return;
        }
        self.streaming = streaming;
        if !self.wifi.on || !self.root.is_real() {
            return;
        }
        let root = self.root.clone();
        let _ = std::thread::Builder::new()
            .name("radiod-wifi-power".into())
            .spawn(move || {
                y2p_core::command::run(
                    &root,
                    &["/usr/sbin/y2-platform", "wifi-power-policy"],
                    10.0,
                );
            });
        self.journal.event(
            "wifi_power_follows",
            serde_json::json!({"streaming": streaming}),
        );
    }

    fn apply_wifi(&mut self, on: bool) {
        wifi::set(&self.root, on);
        self.wifi.held = false;
        self.refresh_wifi();
    }

    /// Read Wi-Fi's switch back; the codec ceiling follows it (BlueALSA patches 0002–0004 read it).
    fn refresh_wifi(&mut self) {
        self.wifi.on = wifi::is_on(&self.root);
        let ceiling = self.root.path(CEILING);
        if self.wifi.on {
            let _ = std::fs::write(&ceiling, "wifi\n");
        } else {
            let _ = std::fs::remove_file(&ceiling);
        }
        self.publish();
    }

    fn state(&self) -> State {
        State {
            schema: SCHEMA.into(),
            sequence: self.sequence,
            bluetooth: self.bluetooth.clone(),
            wifi: self.wifi.clone(),
        }
    }

    fn publish(&mut self) {
        self.sequence += 1;
        let state = self.state();
        let message = Message::State(Box::new(state.clone()));
        self.subscribers.retain(|client| client.send(&message));
        let _ = record::atomic_json(&self.root.path(STATE), &state, false, 0o644);
    }
}
