//! Which codec a connected headset should run, from the owner's choice, what both sides support and
//! what has worked before (moved from Reborn's `codecs.rs` unchanged in its rules). Stable before
//! loud: Auto picks AAC, then aptX, then SBC, and never LDAC or aptX HD (Y2Reborn #87); those stay
//! the owner's explicit choice.
// SPDX-License-Identifier: GPL-2.0-only
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet};
use y2p_contract::radiod::Eligibility;
use y2p_core::Root;

pub const NAMES: [&str; 5] = ["SBC", "AAC", "aptX", "aptX-HD", "LDAC"];
pub const INVENTORY: &str = "/etc/y2linux/bluetooth-codecs.json";
const OWNER_POLICY: &str = "/data/bluetooth/codec-policy.json";
pub const HISTORY: &str = "/data/bluetooth/codec-history.json";
const COEXISTENCE: &str = "/run/y2/coexistence.json";

#[derive(Clone, Default, Deserialize)]
pub struct Capability {
    #[serde(default)]
    pub compiled_locally: bool,
    #[serde(default)]
    pub distribution_approved: bool,
    #[serde(default)]
    pub platform_qualified: bool,
    #[serde(default)]
    pub owner_private_experiment: bool,
}

#[derive(Clone, Default, Deserialize)]
pub struct Inventory {
    #[serde(default)]
    pub schema: u32,
    #[serde(default)]
    pub codecs: BTreeMap<String, Capability>,
    #[serde(default)]
    pub private_integration_enabled: bool,
}

#[derive(Deserialize)]
struct OwnerPolicy {
    #[serde(default)]
    schema: u32,
    #[serde(default)]
    experimental: bool,
}

pub fn inventory(root: &Root) -> Inventory {
    root.read(INVENTORY)
        .and_then(|text| serde_json::from_str(&text).ok())
        .unwrap_or_default()
}

/// The owner's private codec experiments (aptX builds): their policy file, else the build's switch.
pub fn experimental(root: &Root, inventory: &Inventory) -> bool {
    match root
        .read(OWNER_POLICY)
        .and_then(|text| serde_json::from_str::<OwnerPolicy>(&text).ok())
    {
        Some(policy) => policy.schema == 1 && policy.experimental,
        None => inventory.schema == 1 && inventory.private_integration_enabled,
    }
}

/// Each codec's standing for this headset: built, started in BlueALSA (`runtime`), supported by
/// both sides (`mutual`, from GetCodecs), approved and qualified.
pub fn eligibility(
    inventory: &Inventory,
    runtime: &[String],
    mutual: &[String],
    experimental: bool,
) -> BTreeMap<String, Eligibility> {
    NAMES
        .iter()
        .map(|name| {
            let cap = inventory.codecs.get(*name).cloned().unwrap_or_default();
            let runtime_enabled = runtime.iter().any(|c| c == name);
            let mutually_usable = mutual.iter().any(|c| c == name);
            let usable =
                inventory.schema == 1 && cap.compiled_locally && runtime_enabled && mutually_usable;
            let eligibility = Eligibility {
                compiled_locally: cap.compiled_locally,
                runtime_enabled,
                mutually_usable,
                distribution_approved: cap.distribution_approved,
                platform_qualified: cap.platform_qualified,
                auto_eligible: usable && cap.distribution_approved && cap.platform_qualified,
                experimental_eligible: experimental
                    && usable
                    && (cap.distribution_approved || cap.owner_private_experiment),
            };
            ((*name).to_owned(), eligibility)
        })
        .collect()
}

/// What has happened with this headset before, and the load right now.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Context {
    pub last_negotiated: Option<String>,
    pub repeated_failures: BTreeSet<String>,
    pub cpu_pressure: bool,
}

/// The codecs to try, in order (at most two preferred, then conformant SBC).
pub fn candidates(
    preference: &str,
    eligibility: &BTreeMap<String, Eligibility>,
    context: &Context,
) -> Vec<String> {
    let usable = |name: &str| {
        eligibility
            .get(name)
            .is_some_and(|e| e.auto_eligible || e.experimental_eligible)
    };
    let sbc_ok = eligibility.get("SBC").is_some_and(|e| {
        e.compiled_locally && e.runtime_enabled && e.mutually_usable && e.distribution_approved
    });
    let mut order: Vec<String> = Vec::new();
    match preference {
        "Auto" => {
            let mut ranked = vec!["AAC", "aptX"];
            if let Some(last) = context.last_negotiated.as_deref() {
                if let Some(index) = ranked.iter().position(|name| *name == last) {
                    let stable = ranked.remove(index);
                    ranked.insert(0, stable);
                }
            }
            order.extend(
                ranked
                    .into_iter()
                    .filter(|name| {
                        usable(name)
                            && !context.repeated_failures.contains(*name)
                            && !(context.cpu_pressure && *name == "AAC")
                    })
                    .take(2)
                    .map(str::to_owned),
            );
        }
        "SBC" | "SBC-XQ" => {}
        other => {
            if usable(other) {
                order.push(other.to_owned());
            }
        }
    }
    if sbc_ok {
        order.push("SBC".into());
    }
    order.dedup();
    order.truncate(3);
    order
}

/// Auto leaves a headset on the codec it connected with when Auto would run that codec itself: a
/// fresh link is not changed for a small gain, since every change is a reconfiguration a headset can
/// fail (Y2Reborn #94). Auto still moves off LDAC and aptX HD, which it never picks.
pub fn auto_keeps(preference: &str, current: &str, candidates: &[String]) -> bool {
    preference == "Auto" && candidates.iter().any(|c| c == current)
}

/// BlueALSA's `Manager1.Codecs` ("A2DP-source:SBC", …): the A2DP source codecs it runs. 5.0 spells
/// the profile "A2DP-source", so the match ignores case.
pub fn runtime_codecs(names: &[String]) -> Vec<String> {
    names
        .iter()
        .filter_map(|name| {
            let (profile, codec) = name.split_once(':')?;
            profile
                .eq_ignore_ascii_case("a2dp-source")
                .then(|| codec.to_owned())
        })
        .collect()
}

/// `/run/y2/coexistence.json`: CPU pressure (PSI some avg10 ≥ 20) written this boot within 10 s.
#[derive(Deserialize, Default)]
struct Coexistence {
    #[serde(default)]
    boot_id: Option<String>,
    #[serde(default)]
    monotonic_s: Option<f64>,
    #[serde(default)]
    cpu_pressure: Option<String>,
}

pub fn context(root: &Root, address: &str, observed: Option<&str>) -> Context {
    let history = read_history(root);
    let peer = history.peers.get(&address.to_ascii_uppercase());
    let coexistence: Coexistence = root
        .read(COEXISTENCE)
        .and_then(|text| serde_json::from_str(&text).ok())
        .unwrap_or_default();
    let fresh = coexistence.boot_id == root.boot_id()
        && coexistence
            .monotonic_s
            .is_some_and(|at| (0.0..10.0).contains(&(root.clock().monotonic() - at)));
    let pressure = fresh
        && coexistence.cpu_pressure.as_deref().is_some_and(|psi| {
            psi.lines()
                .find(|line| line.starts_with("some "))
                .and_then(|line| {
                    line.split_whitespace()
                        .find_map(|part| part.strip_prefix("avg10=")?.parse::<f64>().ok())
                })
                .is_some_and(|avg| avg >= 20.0)
        });
    Context {
        last_negotiated: observed
            .map(str::to_owned)
            .or_else(|| peer.and_then(|p| p.last_negotiated.clone())),
        repeated_failures: peer
            .map(|p| {
                p.failures
                    .iter()
                    .filter(|(_, n)| **n >= 2)
                    .map(|(name, _)| name.clone())
                    .collect()
            })
            .unwrap_or_default(),
        cpu_pressure: pressure,
    }
}

/// `codec-history.json`: per headset the codec last negotiated and how often each was refused.
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct History {
    #[serde(default)]
    pub schema: u32,
    #[serde(default)]
    pub peers: BTreeMap<String, Peer>,
}

#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct Peer {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub last_negotiated: Option<String>,
    #[serde(default)]
    pub failures: BTreeMap<String, u64>,
    /// Where its adaptive rate settled (0 = the top), per codec: the next stream starts there
    /// (BlueALSA patch 0005).
    #[serde(default, skip_serializing_if = "BTreeMap::is_empty")]
    pub start: BTreeMap<String, u32>,
}

fn read_history(root: &Root) -> History {
    root.read_limited(HISTORY, 65_536)
        .and_then(|text| serde_json::from_str::<History>(&text).ok())
        .filter(|history| history.schema == 1)
        .unwrap_or_default()
}

/// Keep only bounded, known data: 64 headsets, the five codec names, counts up to 255.
pub fn updated(
    mut history: History,
    address: &str,
    negotiated: Option<&str>,
    rejected: Option<&str>,
) -> Option<History> {
    let negotiated = negotiated.filter(|n| NAMES.contains(n));
    let rejected = rejected.filter(|n| NAMES.contains(n));
    if negotiated.is_none() && rejected.is_none() {
        return None;
    }
    history.schema = 1;
    history
        .peers
        .retain(|address, _| crate::desired::valid_address(address));
    for peer in history.peers.values_mut() {
        peer.failures
            .retain(|name, _| NAMES.contains(&name.as_str()));
        for count in peer.failures.values_mut() {
            *count = (*count).min(255);
        }
        peer.last_negotiated = peer
            .last_negotiated
            .take()
            .filter(|n| NAMES.contains(&n.as_str()));
    }
    while history.peers.len() > 64 {
        let first = history.peers.keys().next().cloned()?;
        history.peers.remove(&first);
    }
    let address = address.to_ascii_uppercase();
    if history.peers.len() >= 64 && !history.peers.contains_key(&address) {
        return None;
    }
    let peer = history.peers.entry(address).or_default();
    if let Some(codec) = negotiated {
        peer.last_negotiated = Some(codec.into());
        peer.failures.insert(codec.into(), 0);
    }
    if let Some(codec) = rejected {
        let count = peer.failures.entry(codec.into()).or_insert(0);
        *count = (*count + 1).min(255);
    }
    Some(history)
}

/// Record an observed negotiation or an explicit refusal (never a guess that a codec sounded good).
pub fn remember(root: &Root, address: &str, negotiated: Option<&str>, rejected: Option<&str>) {
    if !crate::desired::valid_address(address) {
        return;
    }
    if let Some(history) = updated(read_history(root), address, negotiated, rejected) {
        let _ = y2p_core::record::atomic_json(&root.path(HISTORY), &history, false, 0o600);
    }
}

/// The codecs whose adaptive rate can start lower (AAC levels, LDAC EQMIDs) and how many levels.
const LEARNED: [(&str, u32); 2] = [("AAC", 4), ("LDAC", 3)];
const START: &str = "/run/y2/bt-start";

/// The next start level after a stream: where it settled when it had to step, else one level
/// closer to the top (a clean stream earns the higher rate back).
pub fn next_start(previous: u32, settled: u32, adjustments: u32, levels: u32) -> u32 {
    let next = if adjustments > 0 {
        settled
    } else {
        previous.saturating_sub(1)
    };
    next.min(levels.saturating_sub(1))
}

/// A stream of `codec` to `address` ended at `settled` after `adjustments` rate changes.
pub fn learn(root: &Root, address: &str, codec: &str, settled: u32, adjustments: u32) {
    let Some((_, levels)) = LEARNED.iter().find(|(name, _)| *name == codec) else {
        return;
    };
    if !crate::desired::valid_address(address) {
        return;
    }
    let mut history = read_history(root);
    history.schema = 1;
    let address = address.to_ascii_uppercase();
    if history.peers.len() >= 64 && !history.peers.contains_key(&address) {
        return;
    }
    let peer = history.peers.entry(address.clone()).or_default();
    let previous = peer.start.get(codec).copied().unwrap_or(0);
    let next = next_start(previous, settled, adjustments, *levels);
    if next == previous && peer.start.contains_key(codec) {
        return;
    }
    peer.start.insert(codec.to_owned(), next);
    let _ = y2p_core::record::atomic_json(&root.path(HISTORY), &history, false, 0o600);
    publish_starts(root);
}

/// Write every headset's start levels where BlueALSA reads them (at the daemon's start, after
/// each change).
pub fn publish_starts(root: &Root) {
    let directory = root.path(START);
    if y2p_core::record::private_directory(&directory).is_err() {
        return;
    }
    for (address, peer) in read_history(root).peers {
        for (codec, level) in &peer.start {
            let path = directory.join(format!("{address}-{codec}"));
            let _ = std::fs::write(path, format!("{level}\n"));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn all() -> BTreeMap<String, Eligibility> {
        let names: Vec<String> = NAMES.iter().map(|n| (*n).to_owned()).collect();
        let inventory = Inventory {
            schema: 1,
            codecs: names
                .iter()
                .map(|name| {
                    (
                        name.clone(),
                        Capability {
                            compiled_locally: true,
                            distribution_approved: true,
                            platform_qualified: true,
                            owner_private_experiment: false,
                        },
                    )
                })
                .collect(),
            private_integration_enabled: false,
        };
        eligibility(&inventory, &names, &names, false)
    }

    #[test]
    fn auto_prefers_aac_never_ldac_and_learns_from_failures() {
        let e = all();
        let none = Context::default();
        assert_eq!(candidates("Auto", &e, &none), ["AAC", "aptX", "SBC"]);
        let after_aptx = Context {
            last_negotiated: Some("aptX".into()),
            ..Context::default()
        };
        assert_eq!(candidates("Auto", &e, &after_aptx), ["aptX", "AAC", "SBC"]);
        let failing = Context {
            repeated_failures: ["AAC".to_string()].into(),
            cpu_pressure: false,
            last_negotiated: Some("LDAC".into()),
        };
        assert_eq!(candidates("Auto", &e, &failing), ["aptX", "SBC"]);
        assert_eq!(candidates("LDAC", &e, &none), ["LDAC", "SBC"]);
        assert_eq!(candidates("SBC-XQ", &e, &none), ["SBC"]);
        let mut no_ldac = e.clone();
        if let Some(ldac) = no_ldac.get_mut("LDAC") {
            ldac.mutually_usable = false;
            ldac.auto_eligible = false;
        }
        assert_eq!(candidates("LDAC", &no_ldac, &none), ["SBC"]);
    }

    #[test]
    fn auto_keeps_what_it_would_run_and_leaves_ldac() {
        let ranked = candidates("Auto", &all(), &Context::default());
        assert!(auto_keeps("Auto", "SBC", &ranked), "no SBC→AAC change");
        assert!(auto_keeps("Auto", "AAC", &ranked));
        assert!(!auto_keeps("Auto", "LDAC", &ranked), "Auto moves off LDAC");
        let chosen = candidates("SBC", &all(), &Context::default());
        assert!(
            !auto_keeps("SBC", "AAC", &chosen),
            "an owner's choice is applied"
        );
    }

    #[test]
    fn history_stays_bounded_and_an_observation_clears_failures() {
        let address = "AA:BB:CC:DD:EE:FF";
        let mut odd = History::default();
        odd.peers.insert(
            address.into(),
            Peer {
                last_negotiated: Some("invented".into()),
                failures: [("LDAC".to_string(), 999_999), ("x".to_string(), 3)].into(),
                start: BTreeMap::new(),
            },
        );
        let history = updated(odd, address, None, Some("LDAC")).expect("updated");
        let peer = &history.peers[address];
        assert_eq!(peer.failures.get("LDAC"), Some(&255));
        assert!(peer.last_negotiated.is_none() && !peer.failures.contains_key("x"));
        let observed = updated(history, address, Some("LDAC"), None).expect("updated");
        assert_eq!(observed.peers[address].failures.get("LDAC"), Some(&0));
        assert!(updated(History::default(), address, Some("Opus"), None).is_none());
        let mut full = History::default();
        for id in 0..100 {
            full.peers
                .insert(format!("00:00:00:00:00:{id:02X}"), Peer::default());
        }
        assert!(updated(full, address, Some("SBC"), None).is_none());
    }

    #[test]
    fn a_headset_starts_where_it_settled_and_earns_the_top_back() {
        assert_eq!(next_start(0, 2, 5, 4), 2, "stepped down to 2");
        assert_eq!(next_start(2, 2, 0, 4), 1, "a clean stream climbs one");
        assert_eq!(next_start(1, 0, 0, 4), 0);
        assert_eq!(next_start(0, 9, 3, 3), 2, "never past the last level");
        let dir = std::env::temp_dir().join(format!("y2-radiod-start-{}", std::process::id()));
        std::fs::create_dir_all(dir.join("data/bluetooth")).expect("fixture");
        std::fs::create_dir_all(dir.join("run/y2")).expect("fixture");
        let root = Root::new(&dir);
        learn(&root, "aa:bb:cc:dd:ee:ff", "AAC", 3, 4);
        learn(&root, "aa:bb:cc:dd:ee:ff", "SBC", 2, 4);
        let file = dir.join("run/y2/bt-start/AA:BB:CC:DD:EE:FF-AAC");
        assert_eq!(std::fs::read_to_string(&file).expect("start"), "3\n");
        assert!(!dir.join("run/y2/bt-start/AA:BB:CC:DD:EE:FF-SBC").exists());
        learn(&root, "AA:BB:CC:DD:EE:FF", "AAC", 3, 0);
        assert_eq!(std::fs::read_to_string(&file).expect("start"), "2\n");
        std::fs::remove_dir_all(dir).expect("cleanup");
    }

    #[test]
    fn runtime_codecs_match_the_profile_in_any_case() {
        let names = [
            "A2DP-source:SBC",
            "a2dp-source:AAC",
            "A2DP-sink:LDAC",
            "odd",
        ]
        .map(String::from);
        assert_eq!(runtime_codecs(&names), ["SBC", "AAC"]);
    }
}
