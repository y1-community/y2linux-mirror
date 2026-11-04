//! Recovery in steps, cheapest first, each with a budget (three times in ten minutes): cancel a
//! stuck page, switch the adapter off and on, restart BlueALSA, restart BlueZ. A step whose budget
//! is spent gives way to the next; when the last one is spent Bluetooth shows Failed until the
//! owner switches Bluetooth off and on, which starts every budget over.
// SPDX-License-Identifier: GPL-2.0-only
use std::{
    collections::VecDeque,
    time::{Duration, Instant},
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Rung {
    CancelPage,
    PowerCycle,
    BluealsaRestart,
    BluezRestart,
}

impl Rung {
    pub fn name(self) -> &'static str {
        match self {
            Rung::CancelPage => "cancel_page",
            Rung::PowerCycle => "power_cycle",
            Rung::BluealsaRestart => "bluealsa_restart",
            Rung::BluezRestart => "bluez_restart",
        }
    }

    fn next(self) -> Option<Rung> {
        match self {
            Rung::CancelPage => Some(Rung::PowerCycle),
            Rung::PowerCycle => Some(Rung::BluezRestart),
            Rung::BluealsaRestart => Some(Rung::BluezRestart),
            Rung::BluezRestart => None,
        }
    }
}

const WINDOW: Duration = Duration::from_secs(600);
const BUDGET: usize = 3;
/// How long a step shows as the current recovery.
const SHOWN: Duration = Duration::from_secs(20);

#[derive(Default)]
pub struct Ladder {
    taken: VecDeque<(Rung, Instant)>,
    last: Option<(Rung, Instant)>,
    pub failed: bool,
}

impl Ladder {
    /// The step to take for a problem that first calls for `wanted`: it, or the next one up whose
    /// budget is not spent; None (and Failed) when none is left.
    pub fn choose(&mut self, wanted: Rung, now: Instant) -> Option<Rung> {
        while self
            .taken
            .front()
            .is_some_and(|(_, at)| now.duration_since(*at) > WINDOW)
        {
            self.taken.pop_front();
        }
        let mut rung = Some(wanted);
        while let Some(step) = rung {
            if self.taken.iter().filter(|(r, _)| *r == step).count() < BUDGET {
                self.taken.push_back((step, now));
                self.last = Some((step, now));
                self.failed = false;
                return Some(step);
            }
            rung = step.next();
        }
        self.failed = true;
        None
    }

    /// The step shown as running (the last one, for a while after it was taken).
    pub fn current(&self, now: Instant) -> Option<&'static str> {
        self.last
            .filter(|(_, at)| now.duration_since(*at) < SHOWN)
            .map(|(rung, _)| rung.name())
    }

    pub fn reset(&mut self) {
        *self = Ladder::default();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_spent_step_gives_way_to_the_next_and_the_last_one_fails() {
        let now = Instant::now();
        let mut ladder = Ladder::default();
        for _ in 0..3 {
            assert_eq!(ladder.choose(Rung::CancelPage, now), Some(Rung::CancelPage));
        }
        assert_eq!(ladder.choose(Rung::CancelPage, now), Some(Rung::PowerCycle));
        assert_eq!(ladder.current(now), Some("power_cycle"));
        for _ in 0..2 {
            ladder.choose(Rung::PowerCycle, now);
        }
        assert_eq!(
            ladder.choose(Rung::CancelPage, now),
            Some(Rung::BluezRestart)
        );
        for _ in 0..2 {
            ladder.choose(Rung::BluezRestart, now);
        }
        assert_eq!(
            ladder.choose(Rung::BluealsaRestart, now),
            Some(Rung::BluealsaRestart)
        );
        assert!(!ladder.failed);
        assert_eq!(ladder.choose(Rung::PowerCycle, now), None);
        assert!(ladder.failed);
        let later = now + Duration::from_secs(700);
        assert_eq!(
            ladder.choose(Rung::CancelPage, later),
            Some(Rung::CancelPage)
        );
        assert_eq!(ladder.current(later + Duration::from_secs(30)), None);
        ladder.reset();
        assert!(!ladder.failed);
    }
}
