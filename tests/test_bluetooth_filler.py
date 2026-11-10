import unittest
from pathlib import Path

from test_power import run_c

ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT / 'buildroot/patches/bluez-alsa/0010-y2-silence-while-a-client-is-late.patch'
# The header and the loop are pasted into each test; not every test calls every helper.
UNUSED_OK = '#pragma clang diagnostic ignored "-Wunused-function"\n'


def header():
    """y2-filler.h exactly as the BlueALSA patch adds it."""
    text = PATCH.read_text()
    body = text.split('+++ b/src/y2-filler.h\n', 1)[1]
    lines = []
    for line in body.splitlines():
        if line.startswith('@@'):
            continue
        if not line.startswith('+'):
            break
        lines.append(line[1:])
    return '\n'.join(lines) + '\n'


# An encoder thread on a fake clock: it waits for the client's audio with the filler's timeout,
# sends one packet of 23 ms per transfer and is paced to real time. audio_at[i] says whether the
# client has audio ready in millisecond i.
LOOP = r'''
#include <assert.h>
#define PACKET 23
static int packets[100000], kinds[100000], count;   /* send time in ms; 1 music, 0 silence */
static void run(const unsigned char *audio_at, int total, bool a2dp_source, int drain_at) {
 int now = 0, due = 0; bool played = false, filling = false; count = 0;
 while (now < total) {
  if (drain_at >= 0 && now >= drain_at) { played = false; filling = false; drain_at = -1; }
  int timeout = y2_filler_timeout(-1, played && a2dp_source, filling), waited = 0;
  while (now < total && !audio_at[now] && (timeout < 0 || waited < timeout)) { now++; waited++; }
  if (now >= total) break;
  if (audio_at[now]) { played = true; filling = false; kinds[count] = 1; }
  else { filling = true; kinds[count] = 0; }
  packets[count++] = now;
  due += PACKET; if (now < due) now = due;           /* real-time pacing */
 }
}
static int longest_gap(int from, int to) {
 int gap = 0, last = from;
 for (int i = 0; i < count; i++) if (packets[i] >= from && packets[i] < to) { if (packets[i] - last > gap) gap = packets[i] - last; last = packets[i]; }
 return to - last > gap ? to - last : gap;
}
static int sent(int from, int to, int kind) { int n = 0; for (int i = 0; i < count; i++) n += packets[i] >= from && packets[i] < to && kinds[i] == kind; return n; }
'''


class SilenceWhileAClientIsLate(unittest.TestCase):
    def test_a_late_client_is_covered_after_100_ms_and_music_takes_over_again(self):
        run_c(UNUSED_OK + header() + LOOP + r'''
int main(void) {
 static unsigned char audio[5000];
 for (int i = 0; i < 5000; i++) audio[i] = (i >= 500 && i < 1500) || (i >= 2500 && i < 3500);
 run(audio, 5000, true, -1);
 /* Before the client's first audio nothing is sent: the stream has not played yet. */
 assert(sent(0, 500, 0) == 0 && sent(0, 500, 1) == 0);
 /* While it plays: music only. */
 assert(sent(500, 1500, 0) == 0 && sent(500, 1500, 1) > 35);
 /* Late for a second: silence after Y2_FILLER_AFTER_MS, then a packet every 23 ms. */
 assert(longest_gap(1500, 2500) <= Y2_FILLER_AFTER_MS + PACKET);
 assert(sent(1500, 2500, 1) <= 1 && sent(1500, 2500, 0) >= 38);
 /* Its audio takes over at once and nothing of the silence follows it. */
 assert(sent(2500, 3500, 0) <= 1 && sent(2500, 3500, 1) > 35);
 /* It stays away: the stream is kept fed until it closes. */
 assert(longest_gap(3500, 5000) <= Y2_FILLER_AFTER_MS + PACKET);
}
''')

    def test_no_silence_after_the_end_of_playback_or_on_other_streams(self):
        run_c(UNUSED_OK + header() + LOOP + r'''
int main(void) {
 static unsigned char audio[3000];
 for (int i = 0; i < 3000; i++) audio[i] = i < 1000;
 /* A drain ends the playback: nothing follows until the client plays again. */
 run(audio, 3000, true, 1000);
 assert(sent(1000, 3000, 0) == 0);
 /* A call or a capture stream is never filled. */
 run(audio, 3000, false, -1);
 assert(sent(0, 3000, 0) == 0 && sent(0, 1000, 1) > 35);
 /* A drain's own timeouts are kept as they are. */
 assert(y2_filler_timeout(100, true, false) == 100 && y2_filler_timeout(0, true, true) == 0);
 assert(y2_filler_timeout(-1, false, true) == -1);
 assert(y2_filler_timeout(-1, true, false) == Y2_FILLER_AFTER_MS && y2_filler_timeout(-1, true, true) == 0);
 /* Whole frames only. */
 assert(y2_filler_samples(2048, 2) == 2048 && y2_filler_samples(2047, 2) == 2046);
 assert(y2_filler_samples(1, 2) == 0 && y2_filler_samples(7, 0) == 0);
}
''')

    def test_the_encoder_thread_asks_the_rule_and_stops_filling_for_drain_and_close(self):
        patch = PATCH.read_text()
        added = [l[1:].strip() for l in patch.splitlines() if l.startswith('+') and not l.startswith('+++')]
        self.assertIn('const int y2_timeout = y2_filler_timeout(io->timeout, y2_armed, io->y2_filling);', added)
        self.assertIn('pcm->t->profile == BA_TRANSPORT_PROFILE_A2DP_SOURCE;', added)
        # Cleared when a drain completes and when the client is gone; set by real audio only.
        self.assertEqual(added.count('io->y2_played = false;'), 2)
        self.assertEqual(added.count('io->y2_played = true;'), 1)


if __name__ == '__main__':
    unittest.main()
