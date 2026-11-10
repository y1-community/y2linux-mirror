"""An exFAT file whose written length ends inside a block, read with its page in high memory.

Found on the owner's player on 2026-11-10 (Beta 9.3.19, 926 MiB RAM, CONFIG_HIGHMEM): reading
every file on an SD card oopsed in exfat_get_block() ("Unable to handle kernel NULL pointer
dereference at virtual address 00000018", mmioset from exfat_get_block+0x704). The block that
holds the end of the valid data is read and its unwritten rest zeroed with memset() on
bh->b_data; for a folio in high memory b_data is only the offset in the page. The oops left the
exFAT lock held: playback and every later access to the card hung until a restart.
"""
from pathlib import Path
import hashlib
import json
import unittest

ROOT = Path(__file__).resolve().parents[1]
PATCH = '0072-exfat-zero-partial-block-highmem.patch'


class ExfatPartialBlock(unittest.TestCase):
    def setUp(self):
        self.patch = (ROOT / 'kernel/patches' / PATCH).read_text()
        self.lines = self.patch.splitlines()

    def test_the_unwritten_rest_is_zeroed_through_the_folio(self):
        removed = [l[1:].strip() for l in self.lines if l.startswith('-') and not l.startswith('---')]
        added = ' '.join(l[1:].strip() for l in self.lines if l.startswith('+') and not l.startswith('+++'))
        # The only code that goes is the write through b_data.
        self.assertEqual([l for l in removed if not l.startswith('/*')],
                         ['memset(bh_result->b_data + size, 0, bh_result->b_size - size);'])
        # Same range, from the end of the valid data to the end of the block, mapped by the folio helper.
        self.assertIn('folio_zero_segment(bh_result->b_folio, '
                      'offset_in_folio(bh_result->b_folio, pos) + size, '
                      'offset_in_folio(bh_result->b_folio, pos) + bh_result->b_size);', added)
        self.assertIn('#include <linux/highmem.h>', added)
        self.assertNotIn('b_data +', added)

    def test_the_block_is_still_read_before_it_is_zeroed(self):
        read = next(i for i, l in enumerate(self.lines) if 'goto done;' in l)
        zero = next(i for i, l in enumerate(self.lines) if l.startswith('+') and 'folio_zero_segment' in l)
        self.assertLess(read, zero)
        self.assertEqual(self.lines[zero + 4:zero + 6], [' \t\terr = 0;', ' \t\tgoto done;'])

    def test_the_manifest_carries_the_overlay(self):
        manifest = json.loads((ROOT / 'kernel/patches/manifest.json').read_text())
        entries = [o for o in manifest['overlays'] if o['path'] == 'fs/exfat/inode.c']
        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0]['patch'], PATCH)
        base = ROOT / '.cache/sources/linux-6.18/fs/exfat/inode.c'
        if base.exists():
            self.assertEqual(hashlib.sha256(base.read_bytes()).hexdigest(), entries[0]['base_sha256'])


if __name__ == '__main__':
    unittest.main()
