"""POWER-FINAL-13 read-only stock suspend golden comparison. Synthetic evidence only.

STOCK is the Y2 FM kernel's mt6582_power_gs_suspend_data (0xc0759c98), decoded from
the retained stock image (evidence-private/20260908-ram-map/fm-decompressed.bin):
virtual 0xf0xxxxxx/0xf1../0xf3../0xf4../0xf5../0xf6.. map to physical 0x10../0x11../0x13..
0x14../0x15../0x16.."""
from pathlib import Path
import re
import unittest
from test_power import run_c

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'kernel/platform'
STOCK = [
    (0x10000004, 0x0000ff9f, 0x00000080), (0x1000000c, 0x00000001, 0x00000000),
    (0x10000040, 0x87818107, 0x00810106), (0x10000050, 0x80808080, 0x80808080),
    (0x10000060, 0x80808080, 0x80808080), (0x10000070, 0x80808080, 0x80808080),
    (0x10000080, 0x000083ef, 0x00000300), (0x10000200, 0x000003ff, 0x000003ff),
    (0x10000204, 0x00000011, 0x00000011), (0x10001008, 0x0000001f, 0x00000000),
    (0x10001010, 0x00000007, 0x00000007), (0x10001014, 0x00000001, 0x00000001),
    (0x10001048, 0x0091bfe7, 0x0080bfe6), (0x10001050, 0x00000303, 0x00000303),
    (0x10001054, 0x0000037f, 0x00000300), (0x10001058, 0x001f0f07, 0x00100000),
    (0x10003018, 0x03ffffff, 0x02feffff), (0x10003030, 0x03ffffff, 0x03ffffff),
    (0x10003050, 0x00001ff3, 0x000000f3), (0x10003054, 0x000000ff, 0x000000f0),
    (0x10003058, 0x001f0f07, 0x00000000), (0x100041dc, 0x03000000, 0x03000000),
    (0x1000f800, 0x00000001, 0x00000000), (0x1020005c, 0x00000200, 0x00000200),
    (0x10208000, 0x00001000, 0x00001000), (0x10209004, 0x00ff03f3, 0x00000000),
    (0x10209008, 0x00000007, 0x00000000), (0x1020900c, 0x0079ce7f, 0x00780000),
    (0x10209018, 0x00ff00ff, 0x00050001), (0x10209024, 0x000fffff, 0x00088888),
    (0x10209210, 0x00000001, 0x00000000), (0x10209220, 0x00000001, 0x00000000),
    (0x10209230, 0x00000001, 0x00000000), (0x10209240, 0x00000001, 0x00000000),
    (0x1020924c, 0x00000003, 0x00000002), (0x11220000, 0x00000044, 0x00000044),
]
POWERED_ELSEWHERE = (0x13, 0x14, 0x15, 0x16)  # MFG, MMSYS, IMG, VDEC: never read


class Golden(unittest.TestCase):
    def setUp(self):
        header = (PLATFORM / 'golden-policy.h').read_text()
        body = header.split('y2_golden_rows[] = {')[1].split('};')[0]
        self.rows = [tuple(int(x, 16) for x in m) for m in
                     re.findall(r'\{0x([0-9a-f]+), 0x([0-9a-f]+), 0x([0-9a-f]+),', body)]

    def test_rows_are_exact_stock_entries_and_every_skip_is_justified(self):
        stock = {phys: (mask, golden) for phys, mask, golden in STOCK}
        for phys, mask, golden in self.rows:
            self.assertEqual(stock.get(phys), (mask, golden), hex(phys))
        skipped = set(stock) - {row[0] for row in self.rows}
        # The unidentified 0x10208000 block and AUDIO (clock-gated when idle) only.
        self.assertEqual(skipped, {0x10208000, 0x11220000})
        self.assertFalse(any(phys >> 24 in POWERED_ELSEWHERE for phys, _, _ in self.rows))

    def test_every_row_maps_to_a_block_and_matching_is_masked(self):
        run_c(r'''
#include <assert.h>
#include <stddef.h>
#include "golden-policy.h"
int main(void) {
 for (unsigned i = 0; i < Y2_GOLDEN_ROWS; i++) {
  int b = y2_golden_block(y2_golden_rows[i].phys);
  assert(b >= 0 && y2_golden_rows[i].phys + 4 <= y2_golden_blocks[b].phys + y2_golden_blocks[b].size);
  assert(y2_golden_rows[i].mask && !(y2_golden_rows[i].golden & ~y2_golden_rows[i].mask));
 }
 assert(y2_golden_block(0x14000100) < 0 && y2_golden_block(0x11220000) < 0);
 assert(y2_golden_match(&y2_golden_rows[0], 0xffff0080) && !y2_golden_match(&y2_golden_rows[0], 0x81));
 return 0;
}
''')

    def test_capture_is_read_only_and_precedes_the_vproc_and_pcm_arm(self):
        spm = (PLATFORM / 'spm.c').read_text()
        capture = spm[spm.index('static void __iomem *golden_blocks'):spm.index('static int y2_spm_enter(')]
        self.assertNotIn('writel', capture)
        self.assertNotIn('spm_write', capture)
        enter = spm[spm.index('static int y2_spm_enter('):spm.index('int y2_spm_dormant_idle(')]
        self.assertLess(enter.index('y2_golden_capture();'), enter.index('y2_pmic_vproc_sleep_begin();'))
        self.assertIn('DEVICE_ATTR_ADMIN_RO(golden)', spm)
        self.assertIn('devm_ioremap(&pdev->dev, y2_golden_blocks[i].phys', spm)
        self.assertNotIn('request_mem_region', capture)


if __name__ == '__main__':
    unittest.main()
