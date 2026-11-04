//! The radio daemon (`y2-radiod`) for platform commands: one request on its socket and the answer.
//! None when it does not run (fixtures, a crash between supervisor looks): callers fall back.
// SPDX-License-Identifier: GPL-2.0-only
use std::{
    io::{BufRead, BufReader, Write},
    os::unix::net::UnixStream,
    time::Duration,
};
use y2p_contract::radiod::{Message, Reply, Request, State};
use y2p_core::Root;

pub fn ask(root: &Root, request: &Request, wait: Duration) -> Option<Reply> {
    match message(root, request, wait)? {
        Message::Reply(reply) => Some(reply),
        Message::State(_) => None,
    }
}

/// The daemon's state now.
pub fn state(root: &Root) -> Option<State> {
    match message(root, &Request::State, Duration::from_secs(1))? {
        Message::State(state) => Some(*state),
        Message::Reply(_) => None,
    }
}

/// The daemon's state as it last wrote it (`/run/y2/radiod-state.json`), without asking.
pub fn written(root: &Root) -> Option<State> {
    root.read("/run/y2/radiod-state.json")
        .and_then(|text| serde_json::from_str(&text).ok())
}

fn message(root: &Root, request: &Request, wait: Duration) -> Option<Message> {
    let mut stream = UnixStream::connect(root.path("/run/y2/radiod.sock")).ok()?;
    stream.set_read_timeout(Some(wait)).ok()?;
    let mut line = serde_json::to_string(request).ok()?;
    line.push('\n');
    stream.write_all(line.as_bytes()).ok()?;
    let mut answer = String::new();
    BufReader::new(stream).read_line(&mut answer).ok()?;
    serde_json::from_str(&answer).ok()
}
