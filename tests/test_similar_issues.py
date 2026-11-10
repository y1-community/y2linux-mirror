"""The duplicate hint for new issues (.github/scripts/similar-issues.js): which earlier issues it shows."""
import json
import shutil
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / '.github/scripts/similar-issues.js'

# Real titles from the two trackers.
EARLIER = [
    'mini request (it\'s really a small detail, not something truly important)',
    'Battery request',
    'Bluetooth headphone battery bar',
    'Ability to show battery charging status in top status bar on battery icon?',
    'Option for pressing center wheel button wake the device?',
    'Unable to connect to certain WiFi with security newer types (WPA3)',
    'Ability to reorder/customize the Now Playing screen\'s menu?',
    'Scroll wheel audio click - feature request',
    'Feature Request: Video Playback',
    'Feature request - increase font size?',
    'Option to re-order home menu',
]


def similar(title):
    program = ('const h = require(process.argv[1]);'
               'const earlier = JSON.parse(process.argv[3]).map((t, i) => ({title: t, state: i % 2 ? "open" : "closed",'
               ' html_url: "https://example.invalid/" + i}));'
               'const found = h.similar(process.argv[2], earlier);'
               'console.log(JSON.stringify({titles: found.map((i) => i.title), comment: found.length ? h.comment(found) : ""}));')
    out = subprocess.run(['node', '-e', program, str(SCRIPT), title, json.dumps(EARLIER)],
                         check=True, capture_output=True, text=True).stdout
    return json.loads(out)


@unittest.skipUnless(shutil.which('node'), 'node is not installed')
class SimilarIssues(unittest.TestCase):
    def test_a_request_asked_before_is_pointed_at_the_earlier_one(self):
        # Pairs that were closed as duplicates by hand (Y2Linux #77 and #79).
        found = similar('Possible to give font/display size customization regardless of skin?')
        self.assertEqual(found['titles'], ['Feature request - increase font size?'])
        self.assertIn('NOT-PLANNED.md', found['comment'])
        self.assertIn('automatic hint', found['comment'])
        self.assertEqual(similar('Option to put link to EQ screen on home page? And re-ordering of home menu items?')['titles'],
                         ['Option to re-order home menu'])
        self.assertEqual(similar('Wake the device with the center wheel button')['titles'],
                         ['Option for pressing center wheel button wake the device?'])
        self.assertEqual(similar('Can\'t connect to WPA3 wifi')['titles'],
                         ['Unable to connect to certain WiFi with security newer types (WPA3)'])
        self.assertEqual(similar('Video playback please')['titles'], ['Feature Request: Video Playback'])

    def test_one_shared_word_or_an_unrelated_title_gets_no_comment(self):
        self.assertEqual(similar('Battery drains heavily on standby')['titles'], [])
        self.assertEqual(similar('Wi-Fi network on channel 13 not visible until router changed to channel 6')['titles'], [])
        self.assertEqual(similar('Dynamic playlists')['titles'], [])
        self.assertEqual(similar('Help')['titles'], [])
        # The same part of the player is not the same request: no hint (a title cannot tell that Y2Linux #109
        # repeats Y2Reborn #72, "mini request"; the list of declined ideas and the tick box are for that).
        self.assertEqual(similar('Possible to have device show current battery percentage/level when it is charging and powered off?')['titles'], [])

    def test_at_most_three_are_shown_best_first(self):
        found = similar('Bluetooth headphone battery bar and battery charging status bar icon')['titles']
        self.assertLessEqual(len(found), 3)
        # Both are wholly inside the new title; the one that shares more words comes first.
        self.assertEqual(found[:2], ['Ability to show battery charging status in top status bar on battery icon?',
                                     'Bluetooth headphone battery bar'])

    def test_the_workflow_only_comments(self):
        workflow = (ROOT / '.github/workflows/similar-issues.yml').read_text()
        self.assertIn('types: [opened]', workflow)
        self.assertIn('issues: write', workflow)
        self.assertNotIn('pull_request_target', workflow)
        script = SCRIPT.read_text()
        self.assertIn('issues.createComment', script)
        for call in ('issues.update', 'issues.addLabels', 'issues.lock'):
            self.assertNotIn(call, script)


if __name__ == '__main__':
    unittest.main()
