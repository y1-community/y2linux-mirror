import unittest
from pathlib import Path

from test_power import run_c

ROOT = Path(__file__).resolve().parents[1]
# The header is pasted into each test; not every test calls every helper.
UNUSED_OK = '#pragma clang diagnostic ignored "-Wunused-function"\n'
PATCH = ROOT / 'buildroot/patches/bluez-alsa/0003-y2-sbc-adaptive-bitpool.patch'


def header():
    """y2-sbc-abr.h exactly as the BlueALSA patch adds it."""
    text = PATCH.read_text()
    body = text.split('+++ b/src/y2-sbc-abr.h\n', 1)[1]
    lines = []
    for line in body.splitlines():
        if line.startswith('@@'):
            continue
        if not line.startswith('+'):
            break
        lines.append(line[1:])
    return '\n'.join(lines) + '\n'


class SbcAdaptiveBitpool(unittest.TestCase):
    def test_steps_down_under_congestion_and_recovers_only_when_quiet(self):
        run_c(UNUSED_OK + header() + r'''
#include <assert.h>
/* 44.1 kHz, 5 frames of 128 samples per packet (672-byte MTU at bit-pool 53). */
#define P 640u
int main(void) {
 struct y2_sbc_abr a;
 y2_sbc_abr_init(&a, 53, 2, 44100, true);
 assert(a.enabled && a.levels == 3 && a.bitpools[0] == 53 && a.bitpools[1] == 45 && a.bitpools[2] == 35);
 unsigned hold = (a.hold + P - 1) / P, calm = (a.calm_needed + P - 1) / P;
 assert(hold == 14 && calm == 690);   /* ~0.2 s and ~10 s of packets */
 /* An idle link never changes anything. */
 for (int i = 0; i < 5000; i++) assert(y2_sbc_abr_update(&a, i % 2, false, P) == 0);
 assert(a.level == 0);
 /* Two queued packets: step down, then hold ~0.2 s between steps. */
 assert(y2_sbc_abr_update(&a, 2, false, P) == 45);
 for (unsigned i = 0; i < hold - 1; i++) assert(y2_sbc_abr_update(&a, 3, false, P) == 0);
 assert(y2_sbc_abr_update(&a, 0, true, P) == 35);   /* a blocked write counts */
 for (int i = 0; i < 100; i++) assert(y2_sbc_abr_update(&a, 9, true, P) == 0);  /* floor */
 assert(a.level == 2);
 /* One queued packet is not calm: no recovery. */
 for (unsigned i = 0; i < 2 * calm; i++) assert(y2_sbc_abr_update(&a, 1, false, P) == 0);
 /* A quiet stretch interrupted by congestion starts over. */
 for (unsigned i = 0; i < calm - 1; i++) assert(y2_sbc_abr_update(&a, 0, false, P) == 0);
 assert(y2_sbc_abr_update(&a, 2, false, P) == 0 && a.level == 2);
 for (unsigned i = 0; i < calm - 1; i++) assert(y2_sbc_abr_update(&a, 0, false, P) == 0);
 assert(y2_sbc_abr_update(&a, 0, false, P) == 45);
 for (unsigned i = 0; i < calm - 1; i++) assert(y2_sbc_abr_update(&a, 0, false, P) == 0);
 assert(y2_sbc_abr_update(&a, 0, false, P) == 53 && a.level == 0);
 /* A long calm stretch at full rate does not bank credit. */
 for (int i = 0; i < 100000; i++) assert(y2_sbc_abr_update(&a, 0, false, P) == 0);
 assert(a.calm <= a.calm_needed + P);
 /* Off: never a change. */
 y2_sbc_abr_init(&a, 53, 2, 44100, false);
 assert(!a.enabled && y2_sbc_abr_update(&a, 9, true, P) == 0);
}
''')

    def test_levels_respect_the_peers_negotiated_minimum(self):
        run_c(UNUSED_OK + header() + r'''
#include <assert.h>
int main(void) {
 struct y2_sbc_abr a;
 /* 48 kHz "high" is 51: 51 -> 43 -> 34. */
 y2_sbc_abr_init(&a, 51, 2, 48000, true);
 assert(a.levels == 3 && a.bitpools[1] == 43 && a.bitpools[2] == 34);
 /* XQ+ / XQ dual channel. */
 y2_sbc_abr_init(&a, 47, 2, 44100, true);
 assert(a.levels == 3 && a.bitpools[1] == 40 && a.bitpools[2] == 31);
 /* A peer minimum above a level raises it; equal levels fold together. */
 y2_sbc_abr_init(&a, 53, 40, 44100, true);
 assert(a.levels == 3 && a.bitpools[1] == 45 && a.bitpools[2] == 40);
 y2_sbc_abr_init(&a, 53, 46, 44100, true);
 assert(a.enabled && a.levels == 2 && a.bitpools[1] == 46);
 assert(y2_sbc_abr_update(&a, 2, false, 10000) == 46);
 assert(y2_sbc_abr_update(&a, 2, true, 10000) == 0 && a.level == 1);
 /* Nothing to adapt when the peer's minimum is the configured bit-pool. */
 y2_sbc_abr_init(&a, 53, 53, 44100, true);
 assert(!a.enabled && a.levels == 1);
}
''')

    def test_the_ceiling_keeps_off_the_top_level(self):
        run_c(UNUSED_OK + header() + r'''
#include <assert.h>
#define P 640u
int main(void) {
 struct y2_sbc_abr a;
 y2_sbc_abr_init(&a, 53, 2, 44100, true);
 unsigned calm = (a.calm_needed + P - 1) / P;
 /* Wi-Fi on: 53 -> 45 at once; congestion still steps further down. */
 assert(y2_sbc_abr_ceiling(&a, true) == 45 && a.level == 1);
 assert(y2_sbc_abr_ceiling(&a, true) == 0);
 assert(y2_sbc_abr_update(&a, 2, false, 10000) == 35);
 /* Quiet: back up to the ceiling, never to the top while it is on. */
 for (unsigned i = 0; i < calm - 1; i++) assert(y2_sbc_abr_update(&a, 0, false, P) == 0);
 assert(y2_sbc_abr_update(&a, 0, false, P) == 45);
 for (unsigned i = 0; i < 3 * calm; i++) assert(y2_sbc_abr_update(&a, 0, false, P) == 0);
 assert(a.level == 1);
 /* Wi-Fi off after a quiet stretch: the top level at the next packet. */
 assert(y2_sbc_abr_ceiling(&a, false) == 0);
 assert(y2_sbc_abr_update(&a, 0, false, P) == 53 && a.level == 0);
 /* Wi-Fi off right after congestion: back up only after ~10 s of quiet. */
 assert(y2_sbc_abr_ceiling(&a, true) == 45);
 for (int i = 0; i < 20; i++) y2_sbc_abr_update(&a, 0, false, P);
 assert(y2_sbc_abr_update(&a, 2, false, P) == 35 && a.level == 2);
 assert(y2_sbc_abr_ceiling(&a, false) == 0);
 for (unsigned i = 0; i < calm - 1; i++) assert(y2_sbc_abr_update(&a, 0, false, P) == 0);
 assert(y2_sbc_abr_update(&a, 0, false, P) == 45);
 for (unsigned i = 0; i < calm - 1; i++) assert(y2_sbc_abr_update(&a, 0, false, P) == 0);
 assert(y2_sbc_abr_update(&a, 0, false, P) == 53 && a.level == 0);
 /* Not adapting (--sbc-abr off): the ceiling applies and lifts at once. */
 y2_sbc_abr_init(&a, 53, 2, 44100, false);
 assert(y2_sbc_abr_ceiling(&a, true) == 45 && y2_sbc_abr_update(&a, 9, true, P) == 0);
 assert(y2_sbc_abr_ceiling(&a, false) == 53);
 /* A single level (peer minimum = configured): nothing to cap. */
 y2_sbc_abr_init(&a, 53, 53, 44100, true);
 assert(y2_sbc_abr_ceiling(&a, true) == 0 && a.level == 0);
 /* Two levels: the ceiling is the lower one. */
 y2_sbc_abr_init(&a, 53, 46, 44100, true);
 assert(y2_sbc_abr_ceiling(&a, true) == 46);
}
''')

    def test_encoder_uses_the_decisions_once_per_packet(self):
        patch = PATCH.read_text()
        sbc = patch.split('+++ b/src/a2dp-sbc.c\n', 1)[1].split('\n--- ', 1)[0]
        added = '\n'.join(line[1:] for line in sbc.splitlines() if line.startswith('+'))
        self.assertEqual(added.count('y2_sbc_abr_update('), 1)
        # Initial and ESTALE re-initialisation both start from the configured
        # bit-pool and the peer's negotiated minimum.
        self.assertEqual(added.count(
            'y2_sbc_abr_init(&abr, sbc.bitpool, configuration->min_bitpool, rate, config.sbc_abr)'), 2)
        self.assertIn('sbc.bitpool = (uint8_t)next;', added)
        # The ceiling: first packet, then about once a second, also after a re-init.
        self.assertEqual(added.count('ceiling_elapsed = rate;'), 2)
        self.assertIn('y2_sbc_abr_ceiling(&abr, y2_codec_ceiling())', added)
        self.assertIn('errno == EAGAIN', added)
        main = patch.split('+++ b/src/main.c\n', 1)[1].split('\n--- ', 1)[0]
        self.assertIn('config.sbc_abr = true;', main)


if __name__ == '__main__':
    unittest.main()
