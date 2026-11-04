//! A wake-up pipe: another thread writes a byte, the owner's poll sees it readable.
// SPDX-License-Identifier: GPL-2.0-only
use std::{
    io,
    os::fd::{AsFd, AsRawFd, OwnedFd, RawFd},
    sync::Arc,
};
use y2p_core::sys;

/// The reading end, owned by the thread that polls.
pub struct Wake {
    read: OwnedFd,
    write: Arc<OwnedFd>,
}

/// A writing end any thread can hold.
#[derive(Clone)]
pub struct Waker(Arc<OwnedFd>);

impl Wake {
    pub fn new() -> io::Result<Wake> {
        let (read, write) = sys::pipe(libc::O_NONBLOCK)?;
        Ok(Wake {
            read,
            write: Arc::new(write),
        })
    }

    pub fn waker(&self) -> Waker {
        Waker(self.write.clone())
    }

    pub fn fd(&self) -> RawFd {
        self.read.as_raw_fd()
    }

    /// Empty the pipe (a full pipe already wakes the poll; extra bytes mean nothing).
    pub fn drain(&self) {
        let mut buffer = [0u8; 64];
        while sys::read(self.read.as_fd(), &mut buffer).is_ok_and(|n| n > 0) {}
    }
}

impl Waker {
    pub fn wake(&self) {
        // A full non-blocking pipe is readable already.
        let _ = sys::write(self.0.as_fd(), &[1]);
    }
}
