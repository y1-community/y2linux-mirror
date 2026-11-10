//! `y2-radiod ctl …`: what `y2-radio` and the sleep helper ask, one request, the answer as the exit
//! status (0 done, 1 refused or unanswered, 2 usage). When the daemon is not running (a crash
//! between two supervisor looks), sleep and wake still work: Wi-Fi switches its rfkill and
//! Bluetooth asks BlueZ directly, as before the daemon.
//!
//!   y2-radiod ctl wifi|bluetooth on|off            the owner's switch (kept across restarts)
//!   y2-radiod ctl wifi|bluetooth on-runtime|off-runtime   wake and sleep
//!   y2-radiod ctl wifi restart                     off and on once (F60)
//!   y2-radiod ctl state                            the daemon's state, as JSON
// SPDX-License-Identifier: GPL-2.0-only
use std::{
    io::{BufRead, BufReader, Write},
    os::unix::net::UnixStream,
    time::Duration,
};
use y2p_contract::radiod::{Message, Radio, Request};
use y2p_core::{command, Root};

pub fn run(root: &Root, args: &[String]) -> i32 {
    let args: Vec<&str> = args.iter().map(String::as_str).collect();
    let Some(request) = request(&args) else {
        eprintln!(
            "usage: y2-radiod ctl wifi|bluetooth on|off|on-runtime|off-runtime \
             | wifi restart | state"
        );
        return 2;
    };
    // A sleep's quiesce waits for the adapter to go down (≤ 15 s); everything else answers at once.
    let wait = match request {
        Request::Quiesce { .. } => Duration::from_secs(20),
        _ => Duration::from_secs(5),
    };
    match ask(root, &request, wait) {
        Ok(Message::Reply(reply)) if reply.ok => 0,
        Ok(Message::State(state)) => {
            println!("{}", serde_json::to_string(&state).unwrap_or_default());
            0
        }
        Ok(Message::Reply(reply)) => {
            eprintln!("{}", reply.reason.unwrap_or_default());
            1
        }
        Err(Unreachable) => fallback(root, &request),
    }
}

fn request(args: &[&str]) -> Option<Request> {
    let radio = match *args.first()? {
        "wifi" => Radio::Wifi,
        "bluetooth" => Radio::Bluetooth,
        "state" => return Some(Request::State),
        _ => return None,
    };
    Some(match (radio, *args.get(1)?) {
        (_, "on-runtime") => Request::Restore { radio },
        (_, "off-runtime") => Request::Quiesce { radio },
        (Radio::Wifi, "restart") => Request::WifiRestart,
        (Radio::Wifi, value @ ("on" | "off")) => Request::WifiPower {
            on: value == "on",
            persist: true,
        },
        (Radio::Bluetooth, value @ ("on" | "off")) => Request::BluetoothPower {
            on: value == "on",
            persist: true,
        },
        _ => return None,
    })
}

/// The daemon did not answer (not running, or not in time).
pub struct Unreachable;

/// Send one request to the daemon and read its answer (a state for `State`, else the reply).
pub fn ask(root: &Root, request: &Request, wait: Duration) -> Result<Message, Unreachable> {
    let mut stream = UnixStream::connect(root.path(crate::SOCKET)).map_err(|_| Unreachable)?;
    stream
        .set_read_timeout(Some(wait))
        .map_err(|_| Unreachable)?;
    let mut line = serde_json::to_string(request).map_err(|_| Unreachable)?;
    line.push('\n');
    stream.write_all(line.as_bytes()).map_err(|_| Unreachable)?;
    let mut answer = String::new();
    BufReader::new(stream)
        .read_line(&mut answer)
        .map_err(|_| Unreachable)?;
    serde_json::from_str(&answer).map_err(|_| Unreachable)
}

/// Without the daemon: what `y2-radio` did before it.
fn fallback(root: &Root, request: &Request) -> i32 {
    let bluetooth = |on: bool| {
        let value = if on { "on" } else { "off" };
        command::run(root, &["bluetoothctl", "power", value], 15.0)
    };
    match request {
        Request::Quiesce { radio: Radio::Wifi } => crate::wifi::set(root, false),
        Request::Restore { radio: Radio::Wifi } => {
            if root
                .read(crate::desired::WIFI)
                .is_some_and(|v| v.trim() == "1")
            {
                crate::wifi::set(root, true);
            }
        }
        Request::WifiRestart => {
            crate::wifi::restart(root);
        }
        Request::Quiesce {
            radio: Radio::Bluetooth,
        } => {
            // Bluetooth that does not run is off already. On a player whose radios never start
            // (no radio data: the connectivity start gives up before this daemon is started)
            // BlueZ has no adapter, `bluetoothctl power off` fails, and every sleep failed at
            // this step: 80 attempts in one night, one every 103 s, 79 % to 11 % by morning
            // (Y2Reborn #116, 9.3.17).
            if !bluetooth_runs(root) {
                return 0;
            }
            return i32::from(!bluetooth(false).ok);
        }
        Request::Restore {
            radio: Radio::Bluetooth,
        } => {
            if root
                .read(crate::desired::BLUETOOTH)
                .is_some_and(|v| v.trim() == "1")
            {
                crate::wifi::recover_if_failed(root);
                return i32::from(!bluetooth(true).ok);
            }
        }
        // The owner's switches and the state need the daemon.
        _ => {
            eprintln!("radio_daemon_unavailable");
            return 1;
        }
    }
    0
}

/// The radio core's Bluetooth function (`functions` bit 0 in its status). Unknown counts as
/// running: then BlueZ is asked, as before.
fn bluetooth_runs(root: &Root) -> bool {
    let core = crate::wifi::core(root);
    !core.known || core.functions & 1 != 0
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A root whose radio core reports `status`.
    fn root_with_core(name: &str, status: Option<&str>) -> (std::path::PathBuf, Root) {
        let dir = std::env::temp_dir().join(format!("y2-radiod-ctl-{name}-{}", std::process::id()));
        let core = dir.join("sys/devices/platform/18070000.connectivity");
        std::fs::create_dir_all(&core).expect("fixture");
        if let Some(status) = status {
            std::fs::write(core.join("status"), status).expect("fixture");
        }
        let root = Root::new(&dir);
        (dir, root)
    }

    #[test]
    fn a_sleep_without_the_daemon_does_not_fail_on_bluetooth_that_never_started() {
        // Y2Reborn #116: the radios never started, so there is no daemon and no adapter.
        let quiesce = Request::Quiesce {
            radio: Radio::Bluetooth,
        };
        let (dir, root) = root_with_core(
            "never",
            Some("activated=0 powered=0 functions=0x0 calibrated=0 error=0\n"),
        );
        assert!(!bluetooth_runs(&root));
        assert_eq!(fallback(&root, &quiesce), 0);
        std::fs::remove_dir_all(dir).expect("cleanup");
        // Wi-Fi alone running is not Bluetooth running.
        let (dir, root) = root_with_core(
            "wifi",
            Some("activated=1 powered=1 functions=0x8 error=0\n"),
        );
        assert_eq!(fallback(&root, &quiesce), 0);
        std::fs::remove_dir_all(dir).expect("cleanup");
        // Bluetooth runs, or nothing is known: BlueZ is asked (and here cannot answer).
        let (dir, root) =
            root_with_core("on", Some("activated=1 powered=1 functions=0x1 error=0\n"));
        assert!(bluetooth_runs(&root));
        assert_eq!(fallback(&root, &quiesce), 1);
        std::fs::remove_dir_all(dir).expect("cleanup");
        let (dir, root) = root_with_core("unknown", None);
        assert!(bluetooth_runs(&root));
        assert_eq!(fallback(&root, &quiesce), 1);
        std::fs::remove_dir_all(dir).expect("cleanup");
    }

    #[test]
    fn script_words_become_requests() {
        assert_eq!(
            request(&["bluetooth", "off-runtime"]),
            Some(Request::Quiesce {
                radio: Radio::Bluetooth
            })
        );
        assert_eq!(
            request(&["wifi", "on"]),
            Some(Request::WifiPower {
                on: true,
                persist: true
            })
        );
        assert_eq!(request(&["wifi", "restart"]), Some(Request::WifiRestart));
        assert_eq!(request(&["bluetooth", "restart"]), None);
        assert_eq!(request(&["state"]), Some(Request::State));
        assert_eq!(request(&[]), None);
    }
}
