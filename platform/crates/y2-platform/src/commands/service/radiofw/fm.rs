//! The FM tuner's two files, an optional pair beside the radio files.
//!
//! The MT6627 tuner inside the radio chip takes a patch and a coefficient table when it is switched
//! on (kernel/platform/connectivity/fm.c reads them only then). They are MediaTek's, so they come from
//! the owner's `system.img` like the radio files: taken along when that image installs the radio
//! firmware, and from a later copy on a player that has its radio firmware already (the leftover
//! pass, which deletes that copy anyway). Wi-Fi and Bluetooth never depend on them; a player
//! without them has no FM radio and says how to get it.
// SPDX-License-Identifier: GPL-2.0-only
use super::{fs, sha256_hex, Ext4, Image, OpenOptions, Path, Root, TARGET};
use std::io::Write;
use std::os::unix::fs::OpenOptionsExt;

/// Stock `/etc/firmware/mt6627` (identical in the original and the v3.2.0 FM package; v2 to v5
/// there are empty). The thresholds of `/etc/fmr/mt6627_fm_cust.cfg` equal the kernel's defaults,
/// so that file is not taken.
pub const FM_FILES: [(&str, u64, &str); 2] = [
    (
        "mt6627_fm_v1_patch.bin",
        1028,
        "ba35c74d26660729caf83b41a567e700f633b6c99384dfa6cbb3c17ceb767e26",
    ),
    (
        "mt6627_fm_v1_coeff.bin",
        1092,
        "d32f4a16b35f950ed1a57f2ae88f77ac4b5674d5c4210b65f6fe7912901fd8db",
    ),
];
const STOCK_DIRECTORY: &str = "/etc/firmware/mt6627";

fn reviewed(name: &str, data: &[u8]) -> bool {
    FM_FILES
        .iter()
        .any(|(n, size, sha)| *n == name && data.len() as u64 == *size && sha256_hex(data) == *sha)
}

/// Both files in `directory`, with the reviewed sizes and hashes.
fn pair(directory: &Path) -> Option<Vec<(&'static str, Vec<u8>)>> {
    FM_FILES
        .iter()
        .map(|(name, size, _)| {
            let path = directory.join(name);
            let info = path.symlink_metadata().ok()?;
            (info.file_type().is_file() && info.len() == *size).then_some(())?;
            let data = fs::read(&path).ok()?;
            reviewed(name, &data).then_some((*name, data))
        })
        .collect()
}

/// Cheap check: both files there with their sizes.
pub fn present(root: &Root) -> bool {
    let target = root.path(TARGET);
    FM_FILES.iter().all(|(name, size, _)| {
        target
            .join(name)
            .symlink_metadata()
            .is_ok_and(|m| m.len() == *size && m.file_type().is_file())
    })
}

fn write(directory: &Path, files: &[(&'static str, Vec<u8>)]) -> std::io::Result<()> {
    for (name, data) in files {
        let staging = directory.join(format!(".{name}.{}", std::process::id()));
        let _ = fs::remove_file(&staging);
        let mut out = OpenOptions::new()
            .write(true)
            .create_new(true)
            .mode(0o600)
            .custom_flags(libc::O_NOFOLLOW)
            .open(&staging)?;
        out.write_all(data).and_then(|()| out.sync_all())?;
        fs::rename(&staging, directory.join(name))?;
    }
    fs::File::open(directory)?.sync_all()
}

/// Take the pair from a stock `system.img` into the installed radio firmware's directory. True
/// when both are in place afterwards. Never an error: FM is optional.
pub(super) fn take(root: &Root, image: &Path) -> bool {
    if present(root) {
        return true;
    }
    let target = root.path(TARGET);
    let read = || -> Option<Vec<(&'static str, Vec<u8>)>> {
        let mut image = Ext4::new(Image::open(image).ok()?).ok()?;
        FM_FILES
            .iter()
            .map(|(name, size, _)| {
                let data = image
                    .read(&format!("{STOCK_DIRECTORY}/{name}"), *size)
                    .ok()?;
                reviewed(name, &data).then_some((*name, data))
            })
            .collect()
    };
    target.is_dir() && read().is_some_and(|files| write(&target, &files).is_ok())
}

/// The radio firmware's directory is replaced as a whole when it is installed again: the pair
/// moves along from the old one.
pub(super) fn carry(old: &Path, new: &Path) {
    if let Some(files) = pair(old) {
        let _ = write(new, &files);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testing::Fixture;

    #[test]
    fn only_the_reviewed_pair_counts_and_it_moves_with_a_new_install() {
        let fixture = Fixture::new();
        let old = fixture.path("/old");
        let new = fixture.path("/new");
        fs::create_dir_all(&old).unwrap();
        fs::create_dir_all(&new).unwrap();
        // Right names and sizes, wrong bytes: not the pair.
        for (name, size, _) in FM_FILES {
            fs::write(old.join(name), vec![1u8; size as usize]).unwrap();
        }
        assert!(pair(&old).is_none());
        carry(&old, &new);
        assert!(fs::read_dir(&new).unwrap().next().is_none());
        // The size check alone is what status polling uses.
        fs::create_dir_all(fixture.path(TARGET)).unwrap();
        assert!(!present(&fixture.root));
        for (name, size, _) in FM_FILES {
            fs::write(fixture.path(TARGET).join(name), vec![0u8; size as usize]).unwrap();
        }
        assert!(present(&fixture.root));
        // An image that is not one: nothing taken, nothing raised.
        fixture.put("/image", "not a system image");
        fs::remove_file(fixture.path(TARGET).join(FM_FILES[0].0)).unwrap();
        assert!(!take(&fixture.root, &fixture.path("/image")));
    }
}
