"""POWER-FINAL-16 G3: stock boot-time DCM (FM kernel dcm_enable(0x1ff), 0xc003c154).

Starts from 15's real sleep-entry values (archive 109, golden capture of boot
e2952adf) and checks which mt6582_power_gs_suspend_data rows the stock DCM
sequence brings to golden.
"""
from pathlib import Path
import unittest

from test_power import run_c

ROOT = Path(__file__).resolve().parents[1]


class Dcm(unittest.TestCase):
    def test_core_groups_reach_the_golden_rows(self):
        run_c(r'''
#include <assert.h>
#include "dcm-policy.h"
struct reg { unsigned phys, value; };
static struct reg regs[] = {
 {0x10208000, 0x0}, {0x1020005c, 0x9c000000}, {0x10000200, 0x3ff}, {0x10000204, 0x11},
 {0x10001008, 0x0}, {0x10001010, 0x1}, {0x10001014, 0x0}, {0x10001050, 0x0},
 {0x10001054, 0x7f}, {0x10001058, 0xf0703}, {0x100041dc, 0x51643842},
 {0x10003050, 0xf2}, {0x10003054, 0xff}, {0x10003058, 0x0},
 {0x14011300, 0}, {0x14000120, 5}, {0x14000124, 5}, {0x14000128, 0}, {0x1400012c, 5},
 {0x14000130, 5}, {0x14000134, 0}, {0x14010014, 0},
};
static unsigned writes;
static struct reg *find(unsigned phys) {
 for (unsigned i = 0; i < sizeof regs / sizeof regs[0]; i++) if (regs[i].phys == phys) return &regs[i];
 assert(!"unexpected register"); return 0;
}
static unsigned rd(void *c, unsigned phys) { (void)c; return find(phys)->value; }
static void wr(void *c, unsigned phys, unsigned v) { (void)c; find(phys)->value = v; writes++; }
static int golden(unsigned phys, unsigned mask, unsigned value) { return (find(phys)->value & mask) == value; }
int main(void) {
 struct y2_dcm_io io = { 0, rd, wr };
 y2_dcm_core(&io);
 /* Golden rows (mask, golden) from y2-stock-suspend-golden-20261011.json. */
 assert(golden(0x1020005c, 0x200, 0x200));
 assert(golden(0x10001008, 0x1f, 0x0));
 assert(golden(0x10001010, 0x7, 0x7));
 assert(golden(0x10001014, 0x1, 0x1));
 assert(golden(0x10001050, 0x303, 0x303));
 assert(golden(0x10001054, 0x37f, 0x300));
 assert(golden(0x10001058, 0x1f0f07, 0x100000));
 assert(golden(0x100041dc, 0x3000000, 0x3000000));
 assert(golden(0x10003050, 0x1ff3, 0xf3));
 assert(golden(0x10003058, 0x1f0f07, 0x0));
 /* Stock code writes PERI DCMDBC to 0x80 (the reference golden says 0xf0):
  * the Y2 follows its own stock firmware, not the generic checklist. */
 assert(find(0x10003054)->value == 0x80);
 /* Unrelated bits survive every read-modify-write. */
 assert(find(0x1020005c)->value == 0x9c000200);
 assert(find(0x100041dc)->value == 0x53643842);
 assert(find(0x10208000)->value == 0x1000);
 y2_dcm_mm(&io);
 assert(find(0x14011300)->value == 1 && find(0x14000128)->value == 0xffffffff &&
        find(0x14000120)->value == 0 && find(0x14010014)->value == 0x8000);
 /* POWER-FINAL-47: DSI/DPI (group 1) keep DCM off; its clear register is never written. */
 assert(find(0x1400012c)->value == 0xf && find(0x14000130)->value == 0xf && find(0x14000134)->value == 0);
 return 0;
}
''')

    def test_wiring(self):
        spm = (ROOT / 'kernel/platform/spm.c').read_text()
        self.assertIn('static bool dcm = true;', spm)
        self.assertIn('y2_dcm_core(&io);', spm)
        mm = (ROOT / 'kernel/platform/mm-clocks.c').read_text()
        apply = mm[mm.index('void y2_mm_dcm_apply'):mm.index('int y2_mm_idle_blockers')]
        self.assertIn('y2_spm_disp_status() != 1', apply)
        patch = (ROOT / 'kernel/patches/0011-mt6582-mutex.patch').read_text()
        prepare = patch[patch.index('clk_bulk_prepare_enable'):]
        # Only after SMI common/LARB0 and the mutex clock are running.
        self.assertLess(prepare.index('clk_prepare_enable(mtx->clk)'), prepare.index('y2_mm_dcm_apply()'))


if __name__ == '__main__':
    unittest.main()
