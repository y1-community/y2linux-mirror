//! The clients' socket: one thread accepts, one thread per client reads its requests (one JSON
//! object per line) and hands them to the arbiter with a [`Client`] to answer on. Only root may
//! connect.
// SPDX-License-Identifier: GPL-2.0-only
use crate::arbiter::Event;
use std::{
    fs,
    io::{BufRead, BufReader, Write},
    os::unix::{
        fs::PermissionsExt,
        net::{UnixListener, UnixStream},
    },
    sync::{
        atomic::{AtomicU64, Ordering},
        mpsc::Sender,
        Arc, Mutex,
    },
    thread,
    time::Duration,
};
use y2p_contract::radiod::{Message, Reply, Request};
use y2p_core::Root;

/// Where to answer one client. Replies and published states go out whole lines under one lock, so
/// they never interleave.
#[derive(Clone)]
pub struct Client {
    stream: Arc<Mutex<UnixStream>>,
    pub id: u64,
}

static NEXT_CLIENT: AtomicU64 = AtomicU64::new(1);

impl Client {
    fn new(stream: UnixStream) -> Client {
        Client {
            stream: Arc::new(Mutex::new(stream)),
            id: NEXT_CLIENT.fetch_add(1, Ordering::Relaxed),
        }
    }

    /// A client on one end of a socket pair (tests).
    #[cfg(test)]
    pub fn for_tests(stream: UnixStream) -> Client {
        Client::new(stream)
    }

    /// Send one message; false when the client is gone or too slow (a 1 s write timeout).
    pub fn send(&self, message: &Message) -> bool {
        let Ok(mut line) = serde_json::to_string(message) else {
            return false;
        };
        line.push('\n');
        match self.stream.lock() {
            Ok(mut stream) => stream.write_all(line.as_bytes()).is_ok(),
            Err(_) => false,
        }
    }

    pub fn reply(&self, reply: Reply) {
        self.send(&Message::Reply(reply));
    }
}

/// Listen on `path` (replacing a stale socket) and hand every request to `events`.
pub fn spawn(root: &Root, path: &str, events: Sender<Event>) -> std::io::Result<()> {
    let path = root.path(path);
    let _ = fs::remove_file(&path);
    let listener = UnixListener::bind(&path)?;
    fs::set_permissions(&path, fs::Permissions::from_mode(0o600))?;
    thread::Builder::new()
        .name("radiod-accept".into())
        .spawn(move || accept(listener, events))?;
    Ok(())
}

fn accept(listener: UnixListener, events: Sender<Event>) {
    for stream in listener.incoming() {
        let Ok(stream) = stream else {
            continue;
        };
        let events = events.clone();
        let _ = thread::Builder::new()
            .name("radiod-client".into())
            .spawn(move || serve(stream, events));
    }
}

fn serve(stream: UnixStream, events: Sender<Event>) {
    let _ = stream.set_write_timeout(Some(Duration::from_secs(1)));
    let Ok(reader) = stream.try_clone() else {
        return;
    };
    let client = Client::new(stream);
    for line in BufReader::new(reader).lines() {
        let Ok(line) = line else {
            break;
        };
        if line.len() > 4096 {
            client.reply(Reply::refused("request_too_long"));
            break;
        }
        match serde_json::from_str::<Request>(&line) {
            Ok(request) => {
                if events
                    .send(Event::Request(request, client.clone()))
                    .is_err()
                {
                    break;
                }
            }
            Err(_) => client.reply(Reply::refused("request_not_understood")),
        }
    }
    let _ = events.send(Event::Gone(client.id));
}
