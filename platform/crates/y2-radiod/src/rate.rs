//! The Bluetooth encoder's rate over time: while a stream plays, BlueALSA's encoder state is read
//! every two seconds and each change is one row in `/run/y2/bt-encoder-rate.jsonl`, which error
//! reports carry. A report could say how often the rate stepped, never when (report 61f652cd,
//! Y2Linux #99: a stutter "when I press the screen button" could not be placed in time).
// SPDX-License-Identifier: GPL-2.0-only
use crate::journal::Journal;
use dbus::{
    arg::PropMap,
    blocking::{stdintf::org_freedesktop_dbus::Properties, Connection},
};
use std::{
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    thread,
    time::Duration,
};
use y2p_contract::radiod::Pcm;

/// In memory only: the rows matter for a report sent soon after a stutter, and a link that steps
/// all the time must neither wear the flash nor push the connection history out of the journal.
const ROWS: &str = "/run/y2/bt-encoder-rate.jsonl";
const LIMIT: u64 = 131_072;
const INTERVAL: Duration = Duration::from_secs(2);
/// The watcher looks for its stop this often, so a stream that ends is let go of quickly.
const SLICE: Duration = Duration::from_millis(250);

/// What the encoder runs at (BlueALSA patch 0001's `EncoderStats`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Sample {
    bitrate_kbps: i64,
    /// SBC's bit-pool (0 for the other codecs).
    bitpool: i64,
    /// LDAC's quality index and AAC's rate level (0 for SBC).
    quality_index: i64,
    adjustments: i64,
    errors: i64,
}

impl Sample {
    /// The encoder's state, or none while no encoder runs.
    pub fn read(stat: impl Fn(&str) -> Option<i64>) -> Option<Sample> {
        if stat("Active") != Some(1) {
            return None;
        }
        Some(Sample {
            bitrate_kbps: stat("BitrateKbps")?,
            bitpool: stat("Bitpool").unwrap_or(0),
            quality_index: stat("QualityIndex").unwrap_or(0),
            adjustments: stat("AbrAdjustments").unwrap_or(0),
            errors: stat("Errors").unwrap_or(0),
        })
    }

    fn row(&self, codec: Option<&str>) -> serde_json::Value {
        serde_json::json!({
            "codec": codec,
            "bitrate_kbps": self.bitrate_kbps,
            "bitpool": self.bitpool,
            "quality_index": self.quality_index,
            "adjustments": self.adjustments,
            "errors": self.errors,
        })
    }
}

/// The sample to write: the first one of a stream and every one that differs from the last row.
pub fn changed(last: Option<Sample>, now: Option<Sample>) -> Option<Sample> {
    now.filter(|sample| last != Some(*sample))
}

/// The watcher of the stream that plays, if one does.
#[derive(Default)]
pub struct Watch {
    stream: Option<(String, Arc<AtomicBool>)>,
}

impl Watch {
    /// Follow the playing stream: a new one gets a watcher, the one that ended loses its own.
    pub fn follow(&mut self, journal: &Journal, playing: Option<&Pcm>, real: bool) {
        // A codec change keeps the object and is another stream.
        let key =
            playing.map(|pcm| format!("{} {}", pcm.object, pcm.codec.as_deref().unwrap_or("")));
        if self.stream.as_ref().map(|(key, _)| key) == key.as_ref() {
            return;
        }
        if let Some((_, stop)) = self.stream.take() {
            stop.store(true, Ordering::Relaxed);
        }
        let (Some(pcm), Some(key), true) = (playing, key, real) else {
            return;
        };
        let stop = Arc::new(AtomicBool::new(false));
        let watcher = (
            journal.clone(),
            pcm.object.clone(),
            pcm.codec.clone(),
            stop.clone(),
        );
        let spawned = thread::Builder::new()
            .name("radiod-encoder-rate".into())
            .spawn(move || watch(&watcher.0, &watcher.1, watcher.2.as_deref(), &watcher.3));
        if spawned.is_ok() {
            self.stream = Some((key, stop));
        }
    }
}

/// Its own bus connection and thread: the controller never waits for a reading.
fn watch(journal: &Journal, object: &str, codec: Option<&str>, stop: &AtomicBool) {
    let Ok(connection) = Connection::new_system() else {
        return;
    };
    let proxy = connection.with_proxy("org.bluealsa", object, Duration::from_secs(2));
    let mut last = None;
    while !stop.load(Ordering::Relaxed) {
        let stats: Result<PropMap, _> = proxy.get("org.bluealsa.PCM1", "EncoderStats");
        let now = stats.ok().and_then(|stats| {
            Sample::read(|key| stats.get(key).and_then(|value| value.0.as_i64()))
        });
        if let Some(sample) = changed(last, now) {
            journal.runtime(ROWS, LIMIT, "encoder_rate", sample.row(codec));
            last = Some(sample);
        }
        let mut waited = Duration::ZERO;
        while waited < INTERVAL && !stop.load(Ordering::Relaxed) {
            thread::sleep(SLICE);
            waited += SLICE;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn stats(pairs: &'static [(&'static str, i64)]) -> impl Fn(&str) -> Option<i64> {
        move |key| pairs.iter().find(|(name, _)| *name == key).map(|(_, v)| *v)
    }

    /// Report 61f652cd: SBC stepped down to bit-pool 35 (228 kbit/s) after two adjustments.
    const STEPPED: &[(&str, i64)] = &[
        ("Active", 1),
        ("BitrateKbps", 228),
        ("Bitpool", 35),
        ("AbrAdjustments", 2),
        ("Errors", 0),
    ];

    #[test]
    fn a_running_encoder_is_a_sample_and_an_idle_one_is_not() {
        let sample = Sample::read(stats(STEPPED)).unwrap();
        assert_eq!(
            sample.row(Some("SBC")),
            serde_json::json!({
                "codec": "SBC", "bitrate_kbps": 228, "bitpool": 35, "quality_index": 0,
                "adjustments": 2, "errors": 0
            })
        );
        assert_eq!(
            Sample::read(stats(&[("Active", 0), ("BitrateKbps", 228)])),
            None
        );
        assert_eq!(Sample::read(stats(&[("Active", 1)])), None);
    }

    #[test]
    fn only_the_first_sample_and_changes_are_written() {
        let stepped = Sample::read(stats(STEPPED));
        let again = Sample::read(stats(&[
            ("Active", 1),
            ("BitrateKbps", 190),
            ("Bitpool", 29),
            ("AbrAdjustments", 3),
        ]));
        assert_eq!(changed(None, stepped), stepped);
        assert_eq!(changed(stepped, stepped), None);
        assert_eq!(changed(stepped, again), again);
        assert_eq!(changed(stepped, None), None);
    }

    #[test]
    fn a_watch_without_a_real_player_starts_no_watcher() {
        let root = y2p_core::Root::new(std::env::temp_dir());
        let mut watch = Watch::default();
        let pcm = Pcm {
            object: "/org/bluealsa/hci0/dev_X/a2dpsrc/sink".into(),
            ..Pcm::default()
        };
        watch.follow(&Journal::new(&root), Some(&pcm), false);
        assert!(watch.stream.is_none());
    }
}
