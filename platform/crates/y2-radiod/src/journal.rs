//! The Bluetooth journal: one row per state change, call, recovery step and disconnect reason,
//! durable on /data (error reports carry it) and never a device name.
// SPDX-License-Identifier: GPL-2.0-only
use serde::Serialize;
use serde_json::Value;
use y2p_core::{clock::wall, json::dumps, mounts::data_ready, power_journal::append_bounded, Root};

pub const JOURNAL: &str = "/data/system/platform/bluetooth-journal.jsonl";
const LIMIT: u64 = 262_144;

#[derive(Clone)]
pub struct Journal {
    root: Root,
}

#[derive(Serialize)]
struct Row<'a> {
    schema: u8,
    event: &'a str,
    wall_time: f64,
    monotonic_s: f64,
    #[serde(flatten)]
    fields: &'a Value,
}

impl Journal {
    pub fn new(root: &Root) -> Journal {
        Journal { root: root.clone() }
    }

    /// Append one row (bounded, rotated once); a /data that is not ours keeps no journal.
    pub fn event(&self, event: &str, fields: Value) {
        if !data_ready(&self.root) {
            return;
        }
        let fields = match fields {
            Value::Object(_) => fields,
            _ => Value::Object(Default::default()),
        };
        let row = Row {
            schema: 1,
            event,
            wall_time: (wall() * 1000.0).round() / 1000.0,
            monotonic_s: (self.root.clock().monotonic() * 1000.0).round() / 1000.0,
            fields: &fields,
        };
        if let Ok(text) = dumps(&row) {
            let _ = append_bounded(
                &self.root,
                JOURNAL,
                format!("{text}\n").as_bytes(),
                LIMIT,
                false,
            );
        }
    }
}
