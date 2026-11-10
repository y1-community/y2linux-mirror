import unittest
from pathlib import Path

from test_power import run_c

ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT / 'buildroot/patches/bluez-alsa/0009-y2-bounded-catch-up-after-a-stall.patch'


def header():
    """y2-catchup.h exactly as the BlueALSA patch adds it."""
    text = PATCH.read_text()
    body = text.split('+++ b/src/shared/y2-catchup.h\n', 1)[1]
    lines = []
    for line in body.splitlines():
        if line.startswith('@@'):
            continue
        if not line.startswith('+'):
            break
        lines.append(line[1:])
    return '\n'.join(lines) + '\n'


# The transfer loop of an encoder thread on a fake clock, paced as the patched asrsync_sync()
# paces it: on time it sleeps to its slot; behind, y2_catchup() decides. One packet of FRAMES per
# pass, 1 ms of work per pass, and the write blocks for the stalls given.
LOOP = r'''
#include <assert.h>
#include <stdio.h>
#define MS INT64_C(1000000)
#define RATE 44100
static int64_t now, start, last, frames;
static void sync_(int64_t n) {
 frames += n;
 int64_t due = start + frames * INT64_C(1000000000) / RATE;
 if (now < due) now = due;
 else {
  int64_t shift, sleep = y2_catchup(now - due, n * INT64_C(1000000000) / RATE, now - last, &shift);
  assert(shift >= 0 && sleep >= 0);
  start += shift; now += sleep;
 }
 last = now;
}
static int64_t sent[400000]; static int count;
/* Runs `total` ms; stall[i] blocks the write at at[i] for len[i] ms. */
static void run(int64_t n, int64_t total, int stalls, const int64_t *at, const int64_t *len) {
 now = start = last = 1000 * MS; frames = 0; count = 0; int next = 0; const int64_t zero = now;
 while (now - zero < total * MS) {
  now += MS;
  if (next < stalls && now - zero >= at[next] * MS) now += len[next++] * MS;
  sent[count++] = now - zero;
  sync_(n);
 }
}
/* The most packets sent in any window of `ms`. */
static int densest(int64_t ms) {
 int worst = 0;
 for (int i = 0, j = 0; i < count; i++) { while (j < count && sent[j] < sent[i] + ms * MS) j++; if (j - i > worst) worst = j - i; }
 return worst;
}
static double audio_ms(int64_t n) { return (double)count * n * 1000 / RATE; }
'''


class BoundedCatchUp(unittest.TestCase):
    def test_a_long_stall_is_not_followed_by_a_burst(self):
        # The stall measured on the player: 2.88 s without a packet (AAC, 1024 frames a packet).
        run_c(header() + LOOP + r'''
int main(void) {
 const int64_t at[] = {20000}, len[] = {2880};
 run(1024, 60000, 1, at, len);
 /* Real time is 21.5 packets in 500 ms; 4/3 of it is 28.7. Unbounded it was 146. */
 assert(densest(500) <= 30);
 /* 200 ms of the stall is made up, the rest is forgotten: nothing else is lost or gained. */
 double behind = 60000 - audio_ms(1024);
 assert(behind > 2880 - 200 - 50 && behind < 2880 - 200 + 50);
}
''')

    def test_short_stalls_are_made_up_completely_but_never_faster_than_four_thirds(self):
        # Scheduling hiccups and short radio stalls: the headphones get everything, their buffer
        # returns to its level, and no window carries more than 4/3 of real time.
        run_c(header() + LOOP + r'''
int main(void) {
 const int64_t at[] = {5000, 10000, 15000, 15400}, len[] = {60, 150, 90, 90};
 run(1024, 30000, 4, at, len);
 double behind = 30000 - audio_ms(1024);
 assert(behind > -30 && behind < 30);
 assert(densest(500) <= 30);
 /* SBC packs fewer frames (5 x 128): same bound in time. */
 run(640, 30000, 4, at, len);
 behind = 30000 - audio_ms(640);
 assert(behind > -20 && behind < 20);
 assert(densest(500) <= 47);   /* real time 34.5, 4/3 = 45.9 */
}
''')

    def test_a_stream_on_time_or_with_ordinary_jitter_keeps_real_time(self):
        run_c(header() + LOOP + r'''
int main(void) {
 /* No stall at all: exactly real time for ten minutes. */
 run(1024, 600000, 0, 0, 0);
 double behind = 600000 - audio_ms(1024);
 assert(behind > -30 && behind < 30);
 assert(densest(500) <= 22);
 /* Every pass a few milliseconds late (a busy system): no slowdown, no drift. */
 int64_t at[2000], len[2000];
 for (int i = 0; i < 2000; i++) { at[i] = 100 + i * 100; len[i] = 3 + i % 5; }
 run(1024, 210000, 2000, at, len);
 behind = 210000 - audio_ms(1024);
 assert(behind > -30 && behind < 30);
 assert(densest(500) <= 23);
 /* A pass that takes longer than its own audio (an overloaded player) is never delayed more. */
 int64_t shift;
 assert(y2_catchup(50 * MS, 23 * MS, 40 * MS, &shift) == 0 && shift == 0);
 assert(y2_catchup(Y2_CATCHUP_MAX_NS + 7, 23 * MS, 0, &shift) == 23 * MS / 4 * 3 && shift == 7);
 assert(y2_catchup(0, 23 * MS, -5, &shift) == 23 * MS / 4 * 3 && shift == 0);
}
''')


if __name__ == '__main__':
    unittest.main()
