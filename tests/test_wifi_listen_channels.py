"""Y2Linux #110: a network on channel 12 or 13 is found and joined without a country.

Without a country the world domain applies: channels 12 and 13 may be listened on, not sent on,
until an access point was heard there (cfg80211's NO_IR). The driver left such channels out of the
firmware's domain and of every scan, and disabled cfg80211's beacon hints, so the network of a
router on channel 13 never appeared (reporter in Australia, router GL-MT6000). Now each scan is
followed by a pass that only listens on them, and a beacon heard lifts the restriction.
"""
import unittest
from pathlib import Path

from test_power import run_c

ROOT = Path(__file__).resolve().parents[1]
WIFI = ROOT / 'kernel/platform/connectivity/wifi'

SCAN = r'''
#include <assert.h>
#include <string.h>
/* One scan as regulatory.c and ais_fsm.c run it: build a request, and when it ends ask whether a
 * listen pass follows. Returns the passes; `sent[pass]` is a bit mask of the channels, and
 * `passive[pass]` whether that pass only listens. */
static int scan(struct y2_listen *l, unsigned sent[4], int passive[4]) {
 int passes = 0;
 for (;;) {
  assert(passes < 4);
  sent[passes] = 0;
  for (unsigned n = 1; n <= 13; n++) if (y2_listen_takes(l, n)) sent[passes] |= 1u << n;
  passive[passes] = l->pass;
  l->listened = l->pass;
  passes++;
  if (!y2_listen_follows(l)) { l->listened = 0; break; }
  l->pass = 1;
  /* the state machine builds the next request here */
  sent[passes] = 0;
  for (unsigned n = 1; n <= 13; n++) if (y2_listen_takes(l, n)) sent[passes] |= 1u << n;
  passive[passes] = l->pass;
  l->listened = l->pass;
  l->pass = 0;
  passes++;
  if (!y2_listen_follows(l)) { l->listened = 0; break; }
 }
 return passes;
}
'''


class ListenOnlyChannels(unittest.TestCase):
    def test_a_scan_listens_on_them_in_a_second_pass_and_never_sends_there(self):
        run_c((WIFI / 'y2_listen.h').read_text() + SCAN + r'''
int main(void) {
 struct y2_listen l; unsigned sent[4]; int passive[4];
 /* The world domain: 1-11 ordinary, 12 and 13 listen-only, 14 off. */
 memset(&l, 0, sizeof l);
 for (unsigned n = 1; n <= 14; n++) {
  enum y2_channel_use use = y2_channel_use(n, n == 14, 0, n >= 12);
  assert(use == (n <= 11 ? Y2_CHANNEL_ALLOWED : n <= 13 ? Y2_CHANNEL_LISTEN : Y2_CHANNEL_OFF));
  l.channel[n - 1] = use == Y2_CHANNEL_LISTEN;
 }
 for (int round = 0; round < 3; round++) {          /* every scan, not only the first */
  assert(scan(&l, sent, passive) == 2);
  assert(sent[0] == 0x0ffe && !passive[0]);          /* channels 1-11, as the request says */
  assert(sent[1] == 0x3000 && passive[1]);           /* channels 12-13, listening only */
  assert(!l.pass && !l.listened);
 }
 /* A beacon was heard on 13: cfg80211 lifts its restriction, it is an ordinary channel now. */
 l.channel[12] = 0;
 assert(scan(&l, sent, passive) == 2);
 assert(sent[0] == 0x2ffe && !passive[0] && sent[1] == 0x1000 && passive[1]);
 /* With a country that allows 1-13 there is one pass, as before. */
 memset(&l, 0, sizeof l);
 assert(scan(&l, sent, passive) == 1 && sent[0] == 0x3ffe && !passive[0]);
 /* A disabled or radar channel is never used, whatever else it says. */
 assert(y2_channel_use(5, 1, 0, 0) == Y2_CHANNEL_OFF && y2_channel_use(5, 0, 1, 1) == Y2_CHANNEL_OFF);
 assert(y2_channel_use(0, 0, 0, 0) == Y2_CHANNEL_OFF && y2_channel_use(14, 0, 0, 0) == Y2_CHANNEL_OFF);
 assert(!y2_listen_takes(&l, 0) && !y2_listen_takes(&l, 15));
}
''')

    def test_the_driver_scans_in_passes_and_lets_cfg80211_lift_the_restriction(self):
        regulatory = (WIFI / 'regulatory.c').read_text()
        # The listen pass sends nothing, and only the pass decides which channels a request takes.
        self.assertIn('if (y2_listen.pass) scan->eScanType=SCAN_TYPE_PASSIVE_SCAN;', regulatory)
        self.assertIn('if (y2_listen_takes(&y2_listen,all[i].ucChannelNum)) scan->arChnlInfoList[count++]=all[i];',
                      regulatory)
        # The flag is never left set: a later scan must start with its ordinary pass.
        steps = regulatory.index('aisFsmSteps(adapter,state);')
        self.assertEqual(regulatory[steps:].split('\n')[1].strip(), 'y2_listen.pass=0;')
        # A scan is done only when no listen pass follows.
        ais = (WIFI / 'mgmt/ais_fsm.c').read_text()
        done = ais[ais.index('aisFsmRunEventScanDone ('):]
        self.assertLess(done.index('y2_wifi_scan_listen(prAdapter, prAisFsmInfo->eCurrentState)'),
                        done.index('kalScanDone('))
        # Beacon hints are on; each new device starts from nothing.
        init = (WIFI / 'os/linux/gl_init.c').read_text()
        self.assertIn('prWdev->wiphy->regulatory_flags = 0;', init)
        # The notifier waits for the driver's thread and cfg80211 calls it under a spinlock for a
        # hint ("BUG: scheduling while atomic" on build 231): it is not asked to run for one, and
        # the flags a hint changes are read before every ordinary pass instead.
        self.assertIn('prWdev->wiphy->flags            = WIPHY_FLAG_SUPPORTS_FW_ROAM;', init)
        self.assertIn('if (!y2_listen.pass) y2_listen_refresh(adapter);', regulatory)
        self.assertLess(init.index('y2_wifi_reg_reset();'), init.index('wiphy->reg_notifier = y2_wifi_reg_notifier;'))


if __name__ == '__main__':
    unittest.main()
