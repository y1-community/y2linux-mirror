"""y2-bt-afh: which Bluetooth channels a Wi-Fi channel masks (AFH host channel classification).

Measured on the owner's player on 2026-11-10 (router on 2427 MHz, AirPods Pro): without the
classification the link hopped over 66-70 of the 79 channels and left out only 4 of the router's;
with `y2-bt-afh 2427` it left all 23 within 2 s (56 in use), and `clear` gave them back.
"""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'tools/connectivity/bt-afh.c'

TEST = r'''
#define Y2_BT_AFH_TEST
#include "%s"
#include <assert.h>
static int used(const uint8_t *map, unsigned n) { return map[n / 8] >> (n %% 8) & 1; }
int main(void) {
 uint8_t map[10];
 /* The router of the measurement: 2427 MHz covers Bluetooth channels 14 to 36 (2416-2438 MHz). */
 assert(y2_afh_map(2427, map) == 23);
 for (unsigned n = 0; n < 79; n++) assert(used(map, n) == (n < 14 || n > 36));
 /* Wi-Fi channels 1, 6, 11 and 13/14 at the band's edges: fewer channels fall inside the band. */
 assert(y2_afh_map(2412, map) == 22 && !used(map, 0) && !used(map, 21) && used(map, 22));
 assert(y2_afh_map(2437, map) == 23 && used(map, 23) && !used(map, 24) && !used(map, 46) && used(map, 47));
 assert(y2_afh_map(2462, map) == 23 && used(map, 48) && !used(map, 49) && !used(map, 71) && used(map, 72));
 assert(y2_afh_map(2472, map) == 20 && used(map, 58) && !used(map, 59) && !used(map, 78));
 assert(y2_afh_map(2484, map) == 8 && used(map, 70) && !used(map, 71) && !used(map, 78));
 /* The controller needs at least 20 channels left, and the reserved bit 79 stays clear. */
 for (unsigned mhz = 2412; mhz <= 2484; mhz++) {
  unsigned masked = y2_afh_map(mhz, map), left = 0;
  for (unsigned n = 0; n < 79; n++) left += (unsigned)used(map, n);
  assert(left == 79 - masked && left >= 56 && !(map[9] & 0x80));
 }
 /* Clear: every channel may be used. */
 assert(y2_afh_map(0, map) == 0);
 for (unsigned n = 0; n < 79; n++) assert(used(map, n));
 assert(!(map[9] & 0x80));
 /* Only a 2.4 GHz centre frequency is taken. */
 assert(y2_afh_wifi_mhz("2412") == 2412 && y2_afh_wifi_mhz("2484") == 2484);
 assert(!y2_afh_wifi_mhz("5180") && !y2_afh_wifi_mhz("2411") && !y2_afh_wifi_mhz("2427.0"));
 assert(!y2_afh_wifi_mhz("") && !y2_afh_wifi_mhz("clear") && !y2_afh_wifi_mhz("-2427") && !y2_afh_wifi_mhz("99999999999999999999"));
 return 0;
}
'''


class ChannelClassification(unittest.TestCase):
    def test_the_wifi_channel_and_nothing_else_is_masked(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            (directory / 'test.c').write_text(TEST % SOURCE)
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-fsanitize=undefined',
                            '-fno-sanitize-recover=undefined', str(directory / 'test.c'), '-o', str(directory / 'test')],
                           check=True)
            subprocess.run([str(directory / 'test')], check=True)

    def test_the_tool_builds_without_warnings_and_refuses_bad_arguments(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'y2-bt-afh'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', str(SOURCE), '-o', str(binary)], check=True)
            for arguments in ([], ['5180'], ['2427', 'extra'], ['wifi']):
                result = subprocess.run([str(binary), *arguments], capture_output=True, text=True)
                self.assertEqual(result.returncode, 1)
                self.assertTrue(result.stdout.startswith('{"ok":false,"reason":"'), result.stdout)

    def test_the_image_carries_the_tool_the_service_daemon_runs(self):
        channels = (ROOT / 'platform/crates/y2-platform/src/commands/service/daemon/channels.rs').read_text()
        self.assertIn('pub(super) const TOOL: &str = "/usr/sbin/y2-bt-afh";', channels)
        package = (ROOT / 'buildroot/package/y2-connectivity/y2-connectivity.mk').read_text()
        self.assertIn('$(@D)/y2-bt-afh $(TARGET_DIR)/usr/sbin/y2-bt-afh', package)
        self.assertIn('y2-bt-afh', (ROOT / 'tools/connectivity/Makefile').read_text())


if __name__ == '__main__':
    unittest.main()
