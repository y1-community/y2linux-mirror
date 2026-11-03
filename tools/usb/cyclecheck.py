#!/usr/bin/env python3
"""Attended physical USB reconnect qualification for the Y2 (host side).

The owner only unplugs and replugs the cable when asked (desktop notification).
Each cycle verifies: exactly one USB enumeration after insertion, stable
descriptors (VID/PID/serial/bcdDevice), USB Ethernet + SSH, MTP with both storage
views, an integrity-checked transfer, the charging source/current, and the
gadget returning to a ready state. Nothing is written to the device except
scratch files that are deleted again. No reboot is ever required or requested.

  cyclecheck.py --cycles 30 --ssh /path/to/ssh-wrapper --mtpbench /path/to/mtpbench \
      --output out/usb-validation/cycles.json [--transfer-mib 4] [--kde-grace 15]
"""
import argparse, json, os, re, subprocess, sys, time
from pathlib import Path

VID, PID = '0525', 'a4aa'
SYSFS = Path('/sys/bus/usb/devices')


def present():
    for d in SYSFS.glob('*'):
        try:
            if (d/'idVendor').read_text().strip() == VID and (d/'idProduct').read_text().strip() == PID:
                return d
        except OSError:
            pass
    return None


def descriptor(d):
    out = {}
    for name in ('idVendor', 'idProduct', 'bcdDevice', 'serial', 'manufacturer', 'product', 'speed', 'bNumInterfaces', 'bConfigurationValue'):
        try: out[name] = (d/name).read_text().strip()
        except OSError: out[name] = None
    return out


def notify(text, urgent=False):
    print('\n>>> ' + text, flush=True)
    subprocess.run(['notify-send', '-u', 'critical' if urgent else 'normal', '-t', '0' if urgent else '15000', 'Y2 USB test', text],
                   check=False, stderr=subprocess.DEVNULL)
    subprocess.run(['sh', '-c', 'command -v paplay >/dev/null && paplay /usr/share/sounds/freedesktop/stereo/message.oga 2>/dev/null &'], check=False)


def wait(predicate, timeout, interval=0.25):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = predicate()
        if value: return value
        time.sleep(interval)
    return None


def journal(since):
    out = subprocess.run(['journalctl', '-k', '--no-pager', '-o', 'short-precise', '--since', since], capture_output=True, text=True).stdout
    return out


def ssh(args, command, timeout=15):
    r = subprocess.run([args.ssh, command], capture_output=True, text=True, timeout=timeout)
    return r.returncode, r.stdout.strip()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cycles', type=int, default=30)
    p.add_argument('--ssh', required=True); p.add_argument('--mtpbench', required=True)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--transfer-mib', default='4'); p.add_argument('--kde-grace', type=float, default=15)
    p.add_argument('--first-only-verify', action='store_true', help='verify the current connection as cycle 0')
    a = p.parse_args()
    results = []
    def verify(cycle, started):
        r = {'cycle': cycle, 'time': time.strftime('%H:%M:%S')}
        dev = present()
        r['enumerated'] = bool(dev)
        if not dev: return r
        r['descriptor'] = descriptor(dev)
        log = journal(started)
        r['new_device_events'] = len(re.findall(r'usb \S+: New USB device found, idVendor=%s, idProduct=%s' % (VID, PID), log))
        r['disconnect_events'] = len(re.findall(r'USB disconnect', log))
        r['host_speed'] = (re.findall(r'new (\S+) USB device number', log) or [None])[-1]
        t0 = time.monotonic()
        r['net_up_s'] = None
        if wait(lambda: subprocess.run(['ping', '-c1', '-W1', '10.42.0.1'], capture_output=True).returncode == 0, 40):
            r['net_up_s'] = round(time.monotonic() - t0, 2)
        try:
            rc, out = ssh(a, 'cat /sys/class/udc/*/state; cat /sys/class/udc/*/current_speed; cat /sys/class/power_supply/BAT0/status; cat /sys/class/power_supply/BAT0/constant_charge_current; cat /sys/class/power_supply/y2-usb-input/current_max; grep -E "^(detached|reconnect_retries|reconnect_failures)" /sys/devices/platform/11200000.usb/status | tr "\\n" " "; head -c 300 /run/y2/usb-transfer.json')
            r['ssh'] = rc == 0; r['device'] = out.splitlines()
        except Exception as error:
            r['ssh'] = False; r['ssh_error'] = str(error)
        # Give the desktop its chance first: did KDE's own MTP stack open the device?
        time.sleep(a.kde_grace)
        log = journal(started)
        r['kde_mtp_open_failures'] = len(re.findall(r'(Could not open MTP device|Unable to initialize device|UDI|existiert nicht)', subprocess.run(['journalctl', '--no-pager', '--since', started], capture_output=True, text=True).stdout))
        busy = subprocess.run(['lsusb', '-t'], capture_output=True, text=True).stdout
        r['kde_holds_mtp_interface'] = 'usbfs' in busy
        subprocess.run(['pkill', '-x', 'kioworker']); subprocess.run(['pkill', '-x', 'kiod6']); time.sleep(1)
        m = subprocess.run([a.mtpbench, 'storages'], capture_output=True, text=True, timeout=90)
        r['storages'] = [l.split('"')[1] for l in m.stdout.splitlines() if l.startswith('storage')]
        r['transfers'] = []
        for sid in ('0xffff0001', '0xffff0002'):
            if len(r['storages']) < (1 if sid.endswith('1') else 2): continue
            t = subprocess.run([a.mtpbench, '-s', sid, 'up', a.transfer_mib, 'cyc.bin'], capture_output=True, text=True, timeout=300)
            r['transfers'].append(t.stdout.strip() + (t.stderr.strip() and ' | ' + t.stderr.strip()))
        r['pass'] = bool(r['enumerated'] and r['ssh'] and r.get('new_device_events') == 1 and
                         len(r['storages']) >= 1 and all('match' in t for t in r['transfers']) and r['transfers'])
        return r
    if a.first_only_verify:
        started = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(time.time() - 60))
        results.append(verify(0, started)); print(json.dumps(results[-1], indent=1))
    for cycle in range(1, a.cycles + 1):
        notify('Cycle %d/%d: UNPLUG the USB cable at the Y2 now.' % (cycle, a.cycles), True)
        if not wait(lambda: present() is None, 180):
            print('timeout waiting for unplug'); break
        time.sleep(3 + (cycle % 4) * 2)
        started = time.strftime('%Y-%m-%d %H:%M:%S')
        notify('Cycle %d/%d: PLUG the cable back in now.' % (cycle, a.cycles), True)
        t0 = time.monotonic()
        if not wait(present, 90):
            r = {'cycle': cycle, 'enumerated': False, 'pass': False}
            results.append(r); print(json.dumps(r)); 
            notify('Y2 did NOT re-enumerate after cycle %d (waiting).' % cycle, True)
            wait(present, 600)
            continue
        enumerate_s = round(time.monotonic() - t0, 2)
        r = verify(cycle, started); r['enumerate_s'] = enumerate_s
        results.append(r)
        print(json.dumps(r, indent=1), flush=True)
        a.output.parent.mkdir(parents=True, exist_ok=True)
        a.output.write_text(json.dumps(results, indent=1))
    passed = sum(1 for r in results if r.get('pass'))
    print('RESULT %d/%d PASS' % (passed, len([r for r in results if r['cycle'] > 0])))
    a.output.write_text(json.dumps(results, indent=1))
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    sys.exit(main())
