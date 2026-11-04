//! What the owner wants, kept across restarts: Bluetooth and Wi-Fi on or off (the files the
//! platform has always used), the headset to reconnect (`preferred-audio`, as `y2-bt-reconnect`
//! wrote it), the codec choice and which devices only connect when asked (`radiod.json`).
// SPDX-License-Identifier: GPL-2.0-only
use serde::{Deserialize, Serialize};
use std::{collections::BTreeSet, fs, io::Write, os::unix::fs::OpenOptionsExt};
use y2p_core::{record, Root};

pub const BLUETOOTH: &str = "/data/bluetooth/enabled";
pub const WIFI: &str = "/data/network/enabled";
pub const PREFERRED: &str = "/data/bluetooth/preferred-audio";
pub const POLICY: &str = "/data/bluetooth/radiod.json";

/// The codec choices a client may ask for.
pub const PREFERENCES: [&str; 7] = ["Auto", "SBC", "SBC-XQ", "AAC", "aptX", "aptX-HD", "LDAC"];

#[derive(Clone, Debug, PartialEq)]
pub struct Desired {
    pub bluetooth: bool,
    pub wifi: bool,
    pub preferred: Option<String>,
    pub policy: Policy,
}

/// `radiod.json`.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct Policy {
    pub schema: u32,
    pub preference: String,
    /// Devices that connect only when asked (Y2Reborn #78).
    #[serde(default)]
    pub manual: BTreeSet<String>,
}

impl Default for Policy {
    fn default() -> Policy {
        Policy {
            schema: 1,
            preference: "Auto".into(),
            manual: BTreeSet::new(),
        }
    }
}

/// A device address as BlueZ prints it (`AA:BB:CC:DD:EE:FF`).
pub fn valid_address(address: &str) -> bool {
    address.len() == 17
        && address.split(':').count() == 6
        && address
            .split(':')
            .all(|part| part.len() == 2 && part.bytes().all(|b| b.is_ascii_hexdigit()))
}

impl Desired {
    pub fn load(root: &Root) -> Desired {
        let on = |name: &str| root.read(name).is_some_and(|text| text.trim() == "1");
        let preferred = root
            .read(PREFERRED)
            .map(|text| text.trim().to_ascii_uppercase())
            .filter(|text| valid_address(text));
        let policy = root
            .read(POLICY)
            .and_then(|text| serde_json::from_str::<Policy>(&text).ok())
            .filter(|policy| policy.schema == 1)
            .map(|mut policy| {
                if !PREFERENCES.contains(&policy.preference.as_str()) {
                    policy.preference = "Auto".into();
                }
                policy.manual.retain(|address| valid_address(address));
                policy
            })
            .unwrap_or_default();
        Desired {
            bluetooth: on(BLUETOOTH),
            wifi: on(WIFI),
            preferred,
            policy,
        }
    }

    pub fn save_bluetooth(&self, root: &Root) {
        write(
            root,
            BLUETOOTH,
            if self.bluetooth { b"1\n" } else { b"0\n" },
        );
    }

    pub fn save_wifi(&self, root: &Root) {
        write(root, WIFI, if self.wifi { b"1\n" } else { b"0\n" });
    }

    /// The headset to reconnect: 17 bytes, no newline (what `y2-bt-reconnect` read and wrote).
    pub fn save_preferred(&self, root: &Root) {
        match &self.preferred {
            Some(address) => write(root, PREFERRED, address.as_bytes()),
            None => {
                let _ = fs::remove_file(root.path(PREFERRED));
            }
        }
    }

    pub fn save_policy(&self, root: &Root) {
        let path = root.path(POLICY);
        let _ = record::atomic_json(&path, &self.policy, true, 0o600);
    }

    /// Whether this paired device reconnects on its own.
    pub fn auto_connect(&self, address: &str) -> bool {
        !self.policy.manual.contains(address)
    }
}

/// A small owner file, replaced whole and synced (0600; the directories are the platform's 0700).
fn write(root: &Root, name: &str, data: &[u8]) {
    let path = root.path(name);
    let Some(parent) = path.parent() else {
        return;
    };
    let temporary = parent.join(format!(
        ".{}.radiod.tmp",
        path.file_name()
            .map(|n| n.to_string_lossy())
            .unwrap_or_default()
    ));
    let written = fs::OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .mode(0o600)
        .custom_flags(libc::O_NOFOLLOW)
        .open(&temporary)
        .and_then(|mut file| {
            file.write_all(data)?;
            file.sync_all()
        })
        .and_then(|()| fs::rename(&temporary, &path))
        .and_then(|()| fs::File::open(parent)?.sync_all());
    if written.is_err() {
        let _ = fs::remove_file(&temporary);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn root() -> (Root, std::path::PathBuf) {
        let dir = std::env::temp_dir().join(format!(
            "y2-radiod-desired-{}-{}",
            std::process::id(),
            y2p_core::clock::monotonic_ns()
        ));
        fs::create_dir_all(dir.join("data/bluetooth")).expect("fixture");
        fs::create_dir_all(dir.join("data/network")).expect("fixture");
        (Root::new(&dir), dir)
    }

    #[test]
    fn owner_files_round_trip_and_bad_ones_read_as_defaults() {
        let (root, dir) = root();
        let mut desired = Desired::load(&root);
        assert!(!desired.bluetooth && !desired.wifi && desired.preferred.is_none());
        assert_eq!(desired.policy.preference, "Auto");
        desired.bluetooth = true;
        desired.preferred = Some("AA:BB:CC:DD:EE:FF".into());
        desired.policy.manual.insert("11:22:33:44:55:66".into());
        desired.policy.preference = "LDAC".into();
        desired.save_bluetooth(&root);
        desired.save_preferred(&root);
        desired.save_policy(&root);
        assert_eq!(
            fs::read(dir.join("data/bluetooth/preferred-audio")).expect("written"),
            b"AA:BB:CC:DD:EE:FF"
        );
        let again = Desired::load(&root);
        assert_eq!(again, desired);
        assert!(!again.auto_connect("11:22:33:44:55:66"));
        fs::write(
            dir.join("data/bluetooth/radiod.json"),
            r#"{"schema":1,"preference":"Opus","manual":["nope"]}"#,
        )
        .expect("fixture");
        let odd = Desired::load(&root);
        assert_eq!(odd.policy.preference, "Auto");
        assert!(odd.policy.manual.is_empty());
        fs::remove_dir_all(dir).expect("cleanup");
    }
}
