//! The service daemon's connectivity and clock work: the coexistence record the Wi-Fi power policy
//! takes as activity (radio_policy.Coexistence) and the hourly time floor (timekeeping.remember), with
//! `time-remember` (every=0). What tests/test_radio_policy.py and tests/test_platform_network_time.py
//! checked of the Python platform. The clocks are the real ones: the anchor is written as it would
//! have been that long ago.
// SPDX-License-Identifier: GPL-2.0-only
use super::*;
use crate::commands::observation::snapshot::Wifi;
use crate::testing::Fixture;
use y2p_core::clock;

#[test]
fn heavy_transfer_is_observed() {
    let fixture = Fixture::new();
    fixture.mkdir("/run/y2");
    let mut observe = Coexistence::default();
    let online: Wifi = serde_json::from_value(json!({"state": "Online"})).unwrap();
    let first = observe.observe(&fixture.root, &online, 1.0).unwrap();
    assert!(first["wifi_bytes_per_second"].is_null());
    let mut busy = online.clone();
    busy.traffic_counters
        .insert("rx_bytes".into(), Some(3_000_000));
    let value = observe.observe(&fixture.root, &busy, 2.0).unwrap();
    assert_eq!(value["wifi_heavy_transfer"], true);
}

#[test]
fn the_record_reports_the_radio_daemons_codec_ceiling() {
    // Y2Reborn #87: y2-radiod keeps the ceiling while Wi-Fi is on; this record only reports it.
    let fixture = Fixture::new();
    fixture.mkdir("/run/y2");
    let mut observe = Coexistence::default();
    let online: Wifi = serde_json::from_value(json!({"state": "Online"})).unwrap();
    let value = observe.observe(&fixture.root, &online, 1.0).unwrap();
    assert!(value["codec_bitrate_override"].is_null());
    assert!(!fixture.exists(CODEC_CEILING));
    fixture.put(CODEC_CEILING, "wifi\n");
    let value = observe.observe(&fixture.root, &online, 2.0).unwrap();
    assert_eq!(
        value["codec_bitrate_override"]["aac_max_bps"],
        json!(192_000)
    );
    assert_eq!(value["codec_bitrate_override"]["ldac"], json!("mobile"));
}

/// The clock as the owner set it `ago` seconds of wall time earlier, `awake` of them awake, `asleep`
/// of them in CLOCK_BOOTTIME (time.json as set_manual writes it).
fn anchor(fixture: &Fixture, ago: f64, awake: f64, asleep: f64) {
    fixture.put_json(
        "/run/y2/time.json",
        &json!({"schema": 1, "boot_id": "boot-one", "epoch": time::wall() - ago,
                "monotonic_s": clock::monotonic() - awake, "boottime_s": (clock::boottime_ns() as f64 / 1e9) - asleep,
                "source": "manual"}),
    );
}

/// The durable floor as written `ago` seconds earlier.
fn remembered(fixture: &Fixture, ago: f64) {
    fixture.put_json(
        time::LAST,
        &json!({"epoch": time::wall() - ago, "source": "manual"}),
    );
}

fn last(fixture: &Fixture) -> Option<f64> {
    fixture
        .exists(time::LAST)
        .then(|| fixture.json(time::LAST)["epoch"].as_f64().unwrap())
}

/// Whether the durable floor now reads the running clock.
fn current(fixture: &Fixture) -> bool {
    last(fixture).is_some_and(|epoch| (time::wall() - epoch).abs() < 5.0)
}

#[test]
fn running_clock_is_remembered_hourly_only_when_set() {
    let fixture = Fixture::new();
    fixture
        .put("/proc/sys/kernel/random/boot_id", "boot-one")
        .put(
            "/etc/y2linux/build-epoch",
            &(time::wall() as i64 - 86_400).to_string(),
        );
    // The build date alone is never remembered as a time.
    fixture.put_json(
        "/run/y2/time.json",
        &json!({"schema": 1, "boot_id": "boot-one", "epoch": time::wall(),
                "monotonic_s": clock::monotonic(), "boottime_s": (clock::boottime_ns() as f64 / 1e9),
                "source": "build_floor"}),
    );
    remember_time(&fixture.root).unwrap();
    assert_eq!(last(&fixture), None);
    // Set by hand 1800 s ago (and remembered then): not yet again.
    anchor(&fixture, 1800.0, 1800.0, 1800.0);
    remembered(&fixture, 1800.0);
    remember_time(&fixture.root).unwrap();
    assert!(!current(&fixture));
    // 3700 s ago: written, then not again within the hour.
    anchor(&fixture, 3700.0, 3700.0, 3700.0);
    remembered(&fixture, 3700.0);
    remember_time(&fixture.root).unwrap();
    assert!(current(&fixture));
    assert_eq!(fixture.json(time::LAST)["source"], "manual");
    let written = fixture.read(time::LAST);
    remember_time(&fixture.root).unwrap();
    assert_eq!(fixture.read(time::LAST), written);
    // Asleep for two hours: the wall clock and CLOCK_BOOTTIME both ran on.
    anchor(&fixture, 11_000.0, 3900.0, 11_000.0);
    remembered(&fixture, 3800.0);
    remember_time(&fixture.root).unwrap();
    assert!(current(&fixture));
    // Someone stepped the clock since the anchor: not written.
    anchor(&fixture, 30_000.0, 4000.0, 15_000.0);
    remembered(&fixture, 3800.0);
    remember_time(&fixture.root).unwrap();
    assert!(!current(&fixture));
    // Orderly shutdown asks for it now (`time-remember`: every=0).
    anchor(&fixture, 600.0, 600.0, 600.0);
    remembered(&fixture, 10.0);
    let reply = time::remember(&fixture.root).unwrap();
    assert!(
        reply.stdout.contains("\"source\":\"manual\""),
        "{}",
        reply.stdout
    );
    assert!(current(&fixture));
    // Another boot's anchor is not this clock.
    fixture.put("/proc/sys/kernel/random/boot_id", "boot-two");
    remembered(&fixture, 10.0);
    let reply = time::remember(&fixture.root).unwrap();
    assert_eq!(reply.stdout, "{\"state\":\"Unchanged\"}");
    assert!(!current(&fixture));
}

#[test]
fn counters_that_are_no_numbers_count_as_none() {
    let fixture = Fixture::new();
    fixture.mkdir("/run/y2");
    let mut observe = Coexistence::default();
    // A counter that did not read counts as none.
    for (now, counters) in [
        (1.0, json!({})),
        (2.0, json!({"rx_bytes": null, "tx_bytes": 5})),
    ] {
        let wifi: Wifi =
            serde_json::from_value(json!({"state": "Online", "traffic_counters": counters}))
                .unwrap();
        assert!(observe.observe(&fixture.root, &wifi, now).is_ok());
    }
    assert_eq!(
        fixture.json("/run/y2/coexistence.json")["wifi_bytes_per_second"],
        json!(5.0)
    );
}

/// The channel-mask passes of one test: the tool's and wpa_cli's commands a fixture recorded.
fn channel_commands(fixture: &Fixture) -> Vec<String> {
    fixture
        .calls()
        .into_iter()
        .map(|(argv, _)| argv.join(" "))
        .filter(|line| line.starts_with(channels::TOOL) || line.starts_with("wpa_cli"))
        .collect()
}

fn channel_fixture(freq: &'static str) -> Fixture {
    let fixture = Fixture::new().responding(move |argv, _| y2p_core::command::Outcome {
        ok: true,
        reason: None,
        output: Some(match argv {
            ["wpa_cli", "-i", "wlan0", "status"] => format!("wpa_state=COMPLETED\nfreq={freq}\n"),
            [_, "clear"] => "{\"ok\":true,\"wifi_mhz\":0,\"masked\":0}\n".into(),
            _ => "{\"ok\":true,\"wifi_mhz\":2427,\"masked\":23}\n".into(),
        }),
    });
    fixture.mkdir("/run/y2");
    fixture
}

/// The radio daemon's published state with the adapter switched `on`.
fn bluetooth_power(fixture: &Fixture, on: bool) {
    use y2p_contract::radiod::{Bluetooth, State, WifiPower, SCHEMA};
    let state = State {
        schema: SCHEMA.into(),
        sequence: 1,
        bluetooth: Bluetooth {
            powered: on,
            ..Default::default()
        },
        wifi: WifiPower::default(),
    };
    fixture.put_json(
        "/run/y2/radiod-state.json",
        &serde_json::to_value(&state).unwrap(),
    );
}

fn wifi_on(network: Option<&str>) -> Wifi {
    serde_json::from_value(match network {
        Some(id) => json!({"state": "Online", "association": "COMPLETED", "association_id": id}),
        None => json!({"state": "Disconnected", "association": "DISCONNECTED"}),
    })
    .unwrap()
}

#[test]
fn bluetooth_is_told_the_wifi_networks_channel_and_told_again_when_it_changes() {
    let fixture = channel_fixture("2427");
    bluetooth_power(&fixture, true);
    let mut mask = ChannelMask::default();
    let home = wifi_on(Some("78:c5:7d:a9:79:41"));
    mask.follow(&fixture.root, &home, 10.0);
    let tool = channels::TOOL;
    assert_eq!(
        channel_commands(&fixture),
        ["wpa_cli -i wlan0 status".to_owned(), format!("{tool} 2427")]
    );
    assert_eq!(fixture.json(channels::RECORD)["wifi_mhz"], json!(2427));
    assert_eq!(
        fixture.json(channels::RECORD)["answer"]["masked"],
        json!(23)
    );
    // Nothing more while nothing changes, until the mask is due again (a controller reset that was
    // not seen here, an access point that changed its channel).
    mask.follow(&fixture.root, &home, 13.0);
    mask.follow(&fixture.root, &home, 39.0);
    assert_eq!(channel_commands(&fixture).len(), 2);
    mask.follow(&fixture.root, &home, 40.0);
    assert_eq!(channel_commands(&fixture).len(), 4);
    // Another network: at once.
    mask.follow(&fixture.root, &wifi_on(Some("00:11:22:33:44:55")), 41.0);
    assert_eq!(channel_commands(&fixture).len(), 6);
    // Wi-Fi leaves: every channel is free again, said once.
    mask.follow(&fixture.root, &wifi_on(None), 42.0);
    mask.follow(&fixture.root, &wifi_on(None), 500.0);
    let commands = channel_commands(&fixture);
    assert_eq!(commands.len(), 7);
    assert_eq!(commands[6], format!("{tool} clear"));
    assert!(fixture.json(channels::RECORD)["wifi_mhz"].is_null());
}

#[test]
fn nothing_is_sent_while_bluetooth_is_off_and_a_controller_that_came_on_is_told_again() {
    let fixture = channel_fixture("2462");
    let mut mask = ChannelMask::default();
    let home = wifi_on(Some("78:c5:7d:a9:79:41"));
    // No radio daemon state, then Bluetooth off: no command at all.
    mask.follow(&fixture.root, &home, 1.0);
    bluetooth_power(&fixture, false);
    mask.follow(&fixture.root, &home, 2.0);
    assert!(channel_commands(&fixture).is_empty());
    bluetooth_power(&fixture, true);
    mask.follow(&fixture.root, &home, 3.0);
    assert_eq!(channel_commands(&fixture).len(), 2);
    // Off and on again (a sleep, the owner's switch): the controller forgot, so it is told at once.
    bluetooth_power(&fixture, false);
    mask.follow(&fixture.root, &home, 4.0);
    bluetooth_power(&fixture, true);
    mask.follow(&fixture.root, &home, 5.0);
    let commands = channel_commands(&fixture);
    assert_eq!(commands.len(), 4);
    assert_eq!(commands[3], format!("{} 2462", channels::TOOL));
}

#[test]
fn a_5_ghz_network_or_an_unreadable_frequency_masks_nothing() {
    assert_eq!(
        channels::frequency("bssid=aa\nfreq=2412\nssid=x\n"),
        Some(2412)
    );
    assert_eq!(channels::frequency("freq=2484\n"), Some(2484));
    assert_eq!(channels::frequency("freq=5180\n"), None);
    assert_eq!(channels::frequency("freq=2400\n"), None);
    assert_eq!(channels::frequency("freq=\nwpa_state=COMPLETED\n"), None);
    assert_eq!(channels::frequency("wpa_state=COMPLETED\n"), None);
    let fixture = channel_fixture("5180");
    bluetooth_power(&fixture, true);
    let mut mask = ChannelMask::default();
    mask.follow(&fixture.root, &wifi_on(Some("78:c5:7d:a9:79:41")), 1.0);
    assert_eq!(
        channel_commands(&fixture)[1],
        format!("{} clear", channels::TOOL)
    );
}
