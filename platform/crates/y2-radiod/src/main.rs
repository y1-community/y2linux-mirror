//! `y2-radiod`: the one owner of Bluetooth and of the shared radio core's power
//! (docs/planning/BLUETOOTH-REIMPLEMENTATION-PLAN.md). Clients say what they want over
//! `/run/y2/radiod.sock` (`y2p_contract::radiod`); `y2-radiod ctl …` is the command line the boot
//! and sleep scripts use through `y2-radio`.
// SPDX-License-Identifier: GPL-2.0-only
mod arbiter;
mod bt;
mod ctl;
mod desired;
mod journal;
mod server;
mod wake;
mod wifi;

/// Where clients reach the daemon.
pub const SOCKET: &str = "/run/y2/radiod.sock";

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let code = match args.first().map(String::as_str) {
        None => arbiter::serve(&y2p_core::Root::from_env()),
        Some("ctl") => ctl::run(&y2p_core::Root::from_env(), &args[1..]),
        Some(_) => {
            eprintln!("usage: y2-radiod | y2-radiod ctl <request>");
            2
        }
    };
    std::process::exit(code);
}
