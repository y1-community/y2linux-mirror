//! An explicit Bluetooth power action against the automatic reconnect owner: what
//! tests/test_platform_bluetooth.py checked of the Python platform (bt_control.power).
// SPDX-License-Identifier: GPL-2.0-only
use super::*;
use crate::testing::{failed, ok, Fixture};

#[test]
fn manual_operation_cannot_overlap_auto_and_unknown_completion_inhibits() {
    let fixture = Fixture::new();
    fixture.mkdir("/run/y2").put(
        "/proc/sys/kernel/random/boot_id",
        "11111111-1111-1111-1111-111111111111",
    );
    let answers = || vec![failed(&["bluetoothctl", "power", "on"], "command_timeout")];
    let automatic = y2p_core::lock::exclusive(&fixture.path("/run/y2/bt-operation.lock")).unwrap();
    let root = fixture.root.clone().with_answers(answers());
    let busy = power(&root, "on").unwrap();
    assert_eq!(
        busy.get("reason"),
        Some(&Value::from("platform_bluetooth_operation_in_progress"))
    );
    assert!(root.calls().is_empty());
    drop(automatic);
    let root = fixture.root.clone().with_answers(answers());
    let result = power(&root, "on").unwrap();
    assert_eq!(result.get("ok"), Some(&Value::from(false)));
    assert_eq!(root.calls().len(), 1);
    assert!(fixture
        .read("/run/y2/bt-intent/bt-control.ini")
        .contains("operation=uncertain"));
}

#[test]
fn off_does_not_wait_for_a_page_and_on_asks_the_reconnect_owner_to_give_way() {
    // The owner paged headphones that were off for ≈20 s and this controller does not end a page
    // on request: Bluetooth off waited that long (owner's player, 9.2.11; the sleep helper in test
    // build 190). Off goes through at once and holds the owner off; on still waits for the lock.
    let fixture = Fixture::new();
    fixture.mkdir("/run/y2").put(
        "/proc/sys/kernel/random/boot_id",
        "11111111-1111-1111-1111-111111111111",
    );
    let automatic = y2p_core::lock::exclusive(&fixture.path("/run/y2/bt-operation.lock")).unwrap();
    let busy = power(&fixture.root, "on").unwrap();
    assert_eq!(
        busy.get("reason"),
        Some(&Value::from("platform_bluetooth_operation_in_progress"))
    );
    assert!(fixture.root.calls().is_empty());
    assert!(fixture
        .read("/run/y2/bt-intent/bt-control.ini")
        .contains("operation=power_pending"));
    let root = fixture
        .root
        .clone()
        .with_answers(vec![ok(&["bluetoothctl", "power", "off"], "")]);
    let off = power(&root, "off").unwrap();
    assert_eq!(off.get("ok"), Some(&Value::from(true)));
    let calls = root.calls();
    assert_eq!(calls.len(), 1);
    assert_eq!(calls[0].0, ["bluetoothctl", "power", "off"]);
    assert!(fixture
        .read("/run/y2/bt-intent/bt-control.ini")
        .contains("operation=power_off"));
    drop(automatic);
}
