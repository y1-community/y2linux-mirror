//! BlueZ (`bluetoothd`, started by its init script) and BlueALSA (`bluealsad`, started here with
//! the build's codec arguments) stay running: a dead one starts again (at most five times in ten
//! minutes, five seconds apart), and a recovery step may restart either. Their pidfds wake the
//! controller.
// SPDX-License-Identifier: GPL-2.0-only
use crate::journal::Journal;
use std::{
    collections::VecDeque,
    fs,
    os::fd::{AsRawFd, OwnedFd},
    time::{Duration, Instant},
};
use y2p_core::{command, sys, Root};

/// How long BlueALSA keeps a headset's stream started after the player closed it: a pause and Play
/// within it reuse the stream instead of an AVDTP Suspend and Start. Headsets answer those late or
/// not at all and BlueZ then ends the link (F61, the 10-31 drop of #87; owner's AirPods Pro on
/// 2026-11-03: "No reply to Suspend request" and a dropped link after one pause in three). As long
/// as a paused player stays awake with headphones (15 min), so no Suspend is sent while they stay
/// connected; a sleep takes Bluetooth down without one.
pub const KEEP_ALIVE_S: u32 = 900;
const SPACING: Duration = Duration::from_secs(5);
const WINDOW: Duration = Duration::from_secs(600);
const BUDGET: usize = 5;

/// The codec arguments `bluealsad` accepts from `codec-runtime` (never anything else).
const CODEC_ARGUMENTS: [&str; 19] = [
    "--codec=-AAC",
    "--codec=-aptX",
    "--codec=-aptX-HD",
    "--codec=-LDAC",
    "--codec=AAC",
    "--codec=aptX",
    "--codec=aptX-HD",
    "--codec=LDAC",
    "--sbc-quality=high",
    "--sbc-quality=xq",
    "--sbc-quality=xq+",
    "--sbc-abr",
    "--ldac-quality=mobile",
    "--ldac-quality=standard",
    "--ldac-quality=high",
    "--ldac-abr",
    "--aac-afterburner",
    "--aac-bitrate=320000",
    "--aac-abr",
];

struct Daemon {
    name: &'static str,
    pidfile: &'static str,
    binary: &'static str,
    pidfd: Option<(i32, OwnedFd)>,
    starts: VecDeque<Instant>,
    next: Option<Instant>,
}

pub struct Supervise {
    root: Root,
    journal: Journal,
    bluez: Daemon,
    bluealsa: Daemon,
    /// Gave up on a daemon: Bluetooth off and on tries again.
    pub exhausted: Option<&'static str>,
}

impl Supervise {
    pub fn new(root: &Root, journal: Journal) -> Supervise {
        Supervise {
            root: root.clone(),
            journal,
            bluez: Daemon {
                name: "bluetoothd",
                pidfile: "/var/run/bluetoothd.pid",
                binary: "/usr/libexec/bluetooth/bluetoothd",
                pidfd: None,
                starts: VecDeque::new(),
                next: None,
            },
            bluealsa: Daemon {
                name: "bluealsad",
                pidfile: "/run/y2/bluealsa.pid",
                binary: "/usr/bin/bluealsad",
                pidfd: None,
                starts: VecDeque::new(),
                next: None,
            },
            exhausted: None,
        }
    }

    /// Start whichever is not running (when its turn has come).
    pub fn check(&mut self, now: Instant) {
        if !self.root.is_real() {
            return;
        }
        for which in [Which::Bluez, Which::Bluealsa] {
            let alive = self.alive(which);
            let daemon = self.daemon(which);
            if alive {
                continue;
            }
            daemon.pidfd = None;
            if daemon.next.is_some_and(|at| now < at) {
                continue;
            }
            while daemon
                .starts
                .front()
                .is_some_and(|at| now.duration_since(*at) > WINDOW)
            {
                daemon.starts.pop_front();
            }
            if daemon.starts.len() >= BUDGET {
                daemon.next = None;
                self.exhausted = Some(daemon.name);
                continue;
            }
            daemon.starts.push_back(now);
            daemon.next = Some(now + SPACING);
            let name = daemon.name;
            let started = self.start(which);
            self.journal.event(
                "daemon_start",
                serde_json::json!({"daemon": name, "started": started}),
            );
        }
    }

    fn daemon(&mut self, which: Which) -> &mut Daemon {
        match which {
            Which::Bluez => &mut self.bluez,
            Which::Bluealsa => &mut self.bluealsa,
        }
    }

    /// Its pid file names a live process running its binary (keeping a pidfd to be woken by its
    /// end).
    fn alive(&mut self, which: Which) -> bool {
        let root = self.root.clone();
        let daemon = self.daemon(which);
        let Some(pid) = root
            .integer(daemon.pidfile)
            .and_then(|p| i32::try_from(p).ok())
        else {
            return false;
        };
        let runs = pid > 1
            && sys::alive(i64::from(pid))
            && fs::read_link(root.path(&format!("/proc/{pid}/exe"))).is_ok_and(|exe| {
                // Both resolved: a symlinked binary must not look like another program.
                fs::canonicalize(root.path(daemon.binary)).is_ok_and(|binary| exe == binary)
            });
        if runs && daemon.pidfd.as_ref().is_none_or(|(known, _)| *known != pid) {
            daemon.pidfd = sys::pidfd_open(pid).ok().map(|fd| (pid, fd));
        }
        runs
    }

    fn start(&mut self, which: Which) -> bool {
        match which {
            Which::Bluez => {
                command::run(&self.root, &["/etc/init.d/S40bluetoothd", "start"], 10.0).ok
            }
            Which::Bluealsa => {
                let Some(codecs) = self.codec_arguments() else {
                    return false;
                };
                let keep_alive = format!("--keep-alive={KEEP_ALIVE_S}");
                let mut argv = vec![
                    "start-stop-daemon",
                    "-S",
                    "-b",
                    "-m",
                    "-p",
                    "/run/y2/bluealsa.pid",
                    "-x",
                    "/usr/bin/bluealsad",
                    "--",
                    "-p",
                    "a2dp-source",
                    "--loglevel=warning",
                    &keep_alive,
                ];
                argv.extend(codecs.iter().map(String::as_str));
                command::run(&self.root, &argv, 5.0).ok
            }
        }
    }

    /// The build's codec arguments (`codec-runtime --record` also writes this boot's receipt). None
    /// when it refuses or prints something not on the list: BlueALSA stays down rather than run
    /// with a codec set nobody approved.
    fn codec_arguments(&self) -> Option<Vec<String>> {
        let outcome = command::run(
            &self.root,
            &["/usr/sbin/y2-platform", "codec-runtime", "--record"],
            10.0,
        );
        if !outcome.ok {
            return None;
        }
        let arguments: Vec<String> = outcome
            .output
            .unwrap_or_default()
            .split_whitespace()
            .map(str::to_owned)
            .collect();
        arguments
            .iter()
            .all(|a| CODEC_ARGUMENTS.contains(&a.as_str()))
            .then_some(arguments)
    }

    fn stop(&mut self, which: Which) {
        // Below a fixture root nothing is started or stopped (the paths are the player's).
        if !self.root.is_real() {
            return;
        }
        let daemon = self.daemon(which);
        let (pidfile, binary) = (daemon.pidfile, daemon.binary);
        daemon.pidfd = None;
        daemon.next = None;
        command::run(
            &self.root,
            &["start-stop-daemon", "-K", "-p", pidfile, "-x", binary],
            5.0,
        );
    }

    /// Recovery: BlueALSA again (players see a new stream generation).
    pub fn restart_bluealsa(&mut self) {
        self.stop(Which::Bluealsa);
        self.check(Instant::now());
    }

    /// Recovery: BlueZ again (its init script stops it and waits for it to go).
    pub fn restart_bluez(&mut self) {
        if !self.root.is_real() {
            return;
        }
        self.bluez.pidfd = None;
        self.bluez.next = Some(Instant::now() + SPACING);
        command::run(&self.root, &["/etc/init.d/S40bluetoothd", "restart"], 20.0);
    }

    /// The owner's restart: both budgets start over.
    pub fn reset(&mut self) {
        self.exhausted = None;
        self.bluez.starts.clear();
        self.bluealsa.starts.clear();
        self.bluez.next = None;
        self.bluealsa.next = None;
    }

    pub fn pollfds(&self) -> Vec<libc::pollfd> {
        [&self.bluez, &self.bluealsa]
            .iter()
            .filter_map(|d| d.pidfd.as_ref())
            .map(|(_, fd)| libc::pollfd {
                fd: fd.as_raw_fd(),
                events: libc::POLLIN,
                revents: 0,
            })
            .collect()
    }

    /// A pidfd became readable: that daemon ended (the next check starts it again).
    pub fn note_ready(&mut self, fds: &[libc::pollfd]) {
        for daemon in [&mut self.bluez, &mut self.bluealsa] {
            let ended = daemon.pidfd.as_ref().is_some_and(|(_, fd)| {
                fds.iter().any(|p| p.fd == fd.as_raw_fd() && p.revents != 0)
            });
            if ended {
                daemon.pidfd = None;
            }
        }
    }

    pub fn next(&self) -> Option<Instant> {
        [&self.bluez, &self.bluealsa]
            .iter()
            .filter(|d| d.pidfd.is_none())
            .filter_map(|d| d.next)
            .min()
    }
}

#[derive(Clone, Copy)]
enum Which {
    Bluez,
    Bluealsa,
}
