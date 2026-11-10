//! When to connect the headset the owner uses, and when to stop trying. One episode per reason to
//! try (Bluetooth came on, a wake, the owner's tap, a lost link), each with its first try and its
//! budget; a spent episode parks the headset until something new happens (it connects on its own, a
//! wake, a tap). Pure: the controller feeds it the clock and the outcomes.
// SPDX-License-Identifier: GPL-2.0-only
use std::time::{Duration, Instant};
use y2p_contract::radiod::Ended;

/// Why the daemon wants to connect now.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Trigger {
    /// Bluetooth came on (boot, the owner's switch, a wake with a headset connected before it).
    PowerOn,
    /// A wake with no headset connected before the sleep: one look. Three pages of an absent
    /// headset kept the shared radio busy for about a minute, and Wi-Fi could neither scan nor
    /// join meanwhile (Y2Linux #83: every failed join after a wake overlapped them). A headset
    /// switched on later connects by itself.
    Wake,
    /// The owner asked for this headset.
    User,
    /// The link ended this way.
    Lost(Ended),
}

/// How long headphones get to come back on their own before the daemon pages them: most headsets
/// reconnect to their last source when they or it come on, and a page racing that connection left
/// BlueZ answering InProgress for minutes (Y2Reborn #92, F65).
pub const ADOPTION: Duration = Duration::from_secs(3);
/// After a failed try: the next one this much later.
const SPACING: [Duration; 3] = [
    Duration::from_secs(3),
    Duration::from_secs(8),
    Duration::from_secs(15),
];
/// A connect call BlueZ has not answered in this long is stuck (an absent headset answers "Host is
/// down" after ≈ 5–20 s with the 5.12 s page timeout).
pub const CONNECT_DEADLINE: Duration = Duration::from_secs(35);
/// BlueZ said InProgress this long: cancel.
pub const STUCK: Duration = Duration::from_secs(12);
/// Connected without its services this long: cancel and connect again. Services follow the link in
/// 1–2 s on the owner's player; a headset that reconnects by itself after a lost link sometimes
/// never gets them (AirPods Pro, BlueALSA refusing its configuration).
pub const HALF_CONNECTED: Duration = Duration::from_secs(8);
/// A link that ends this soon after it came up (on our side, the headset's or unexplained) counts as
/// a quick drop; this many in a row stop the reconnects until something new happens. A headset that
/// refuses our link key (BlueZ `Permission denied`, it was paired again elsewhere) connected and
/// dropped 26 times in 2 minutes (install 319dab53, 9.3.12), each drop starting a new episode.
pub const QUICK_DROP: Duration = Duration::from_secs(10);
pub const QUICK_DROPS: u32 = 3;

/// Whether a link that lasted `lasted` and ended `ended` was a quick drop.
pub fn quick_drop(lasted: Option<Duration>, ended: Ended) -> bool {
    lasted.is_some_and(|lasted| lasted < QUICK_DROP) && !matches!(ended, Ended::Sleep | Ended::User)
}

#[derive(Clone, Debug, PartialEq)]
pub struct Episode {
    pub trigger: Trigger,
    pub attempts: u32,
    pub limit: u32,
    pub next: Instant,
}

impl Episode {
    /// The episode a trigger starts, or none (the owner's own disconnect, a sleep: those wait for a
    /// wake or a tap).
    pub fn start(trigger: Trigger, now: Instant) -> Option<Episode> {
        let (delay, limit) = match trigger {
            Trigger::PowerOn => (ADOPTION, 3),
            Trigger::Wake => (ADOPTION, 1),
            Trigger::User => (Duration::ZERO, 3),
            Trigger::Lost(Ended::Timeout | Ended::Local | Ended::Unknown) => {
                (Duration::from_secs(2), 3)
            }
            // Switched off, most likely: one look, not a page every few seconds.
            Trigger::Lost(Ended::Remote) => (Duration::from_secs(4), 1),
            Trigger::Lost(Ended::Sleep | Ended::User) => return None,
        };
        Some(Episode {
            trigger,
            attempts: 0,
            limit,
            next: now + delay,
        })
    }

    pub fn due(&self, now: Instant) -> bool {
        self.attempts < self.limit && now >= self.next
    }

    /// A try begins.
    pub fn begin(&mut self) {
        self.attempts += 1;
    }

    /// The try failed: whether another one is left (and when).
    pub fn failed(&mut self, now: Instant) -> bool {
        if self.attempts >= self.limit {
            return false;
        }
        let index = (self.attempts as usize)
            .saturating_sub(1)
            .min(SPACING.len() - 1);
        self.next = now + SPACING[index];
        true
    }
}

/// A BlueZ disconnect reason (`org.bluez.Device1.Disconnected`, BlueZ ≥ 5.82) as whose side ended
/// it. `quiescing`: our sleep is taking Bluetooth down (BlueZ reports the power-off as reason 21,
/// "unknown").
pub fn ended(reason: &str, quiescing: bool) -> Ended {
    if quiescing {
        return Ended::Sleep;
    }
    match reason.rsplit('.').next().unwrap_or(reason) {
        "Timeout" => Ended::Timeout,
        "Remote" => Ended::Remote,
        "Local" => Ended::Local,
        "Suspend" => Ended::Sleep,
        // Authentication or an unknown value: like a local failure.
        _ => Ended::Unknown,
    }
}

/// A connect error that means BlueZ is already connecting this device.
pub fn in_progress(error: &str) -> bool {
    ["InProgress", "br-connection-busy", "already in progress"]
        .iter()
        .any(|needle| error.contains(needle))
}

/// A connect error that means it is connected already.
pub fn already(error: &str) -> bool {
    error.contains("AlreadyConnected")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn power_on_waits_for_the_headset_then_tries_three_times() {
        let now = Instant::now();
        let mut episode = Episode::start(Trigger::PowerOn, now).expect("episode");
        assert!(!episode.due(now));
        assert!(episode.due(now + ADOPTION));
        episode.begin();
        assert!(episode.failed(now + ADOPTION));
        assert!(!episode.due(now + ADOPTION + Duration::from_secs(2)));
        assert!(episode.due(now + ADOPTION + Duration::from_secs(3)));
        episode.begin();
        assert!(episode.failed(now));
        episode.begin();
        assert!(!episode.failed(now));
        assert!(!episode.due(now + Duration::from_secs(600)));
    }

    #[test]
    fn a_wake_without_a_headset_looks_once() {
        // Y2Linux #83: three pages of absent earbuds after every wake kept Wi-Fi from joining.
        let now = Instant::now();
        let mut wake = Episode::start(Trigger::Wake, now).expect("one look");
        assert_eq!(wake.limit, 1);
        assert!(
            !wake.due(now) && wake.due(now + ADOPTION),
            "headsets get to come first"
        );
        wake.begin();
        assert!(!wake.failed(now + ADOPTION), "no second page");
    }

    #[test]
    fn the_owners_disconnect_and_a_sleep_start_nothing_a_switched_off_headset_gets_one_look() {
        let now = Instant::now();
        assert!(Episode::start(Trigger::Lost(Ended::User), now).is_none());
        assert!(Episode::start(Trigger::Lost(Ended::Sleep), now).is_none());
        let remote = Episode::start(Trigger::Lost(Ended::Remote), now).expect("one look");
        assert_eq!(remote.limit, 1);
        let user = Episode::start(Trigger::User, now).expect("tap");
        assert!(user.due(now));
    }

    #[test]
    fn reasons_name_whose_side_ended_the_link() {
        assert_eq!(ended("org.bluez.Reason.Timeout", false), Ended::Timeout);
        assert_eq!(ended("org.bluez.Reason.Remote", false), Ended::Remote);
        assert_eq!(ended("org.bluez.Reason.Local", false), Ended::Local);
        assert_eq!(ended("org.bluez.Reason.Unknown", true), Ended::Sleep);
        assert_eq!(ended("org.bluez.Reason.Unknown", false), Ended::Unknown);
        assert!(in_progress("org.bluez.Error.InProgress: In Progress"));
        assert!(already("org.bluez.Error.AlreadyConnected"));
    }
}
