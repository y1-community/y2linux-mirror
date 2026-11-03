"""POWER-FINAL-16 G3b: golden sleep entry with exact restore.

Registers start at 15's real sleep-entry values (archive 109, boot e2952adf).
Gate registers are modelled as SET/CLR write-one with a STA readback.
"""
import unittest

from test_power import run_c


class GoldenApply(unittest.TestCase):
    def test_entry_reaches_golden_and_restore_is_exact(self):
        run_c(r'''
#include <assert.h>
#include <string.h>
#include "golden-apply-policy.h"
struct reg { unsigned phys, value; };
static struct reg regs[] = {
 {0x10003018, 0x2fcfbfd}, {0x10001048, 0xb0a0},
 {0x10000050, 0x810100}, {0x10000060, 0x1010100}, {0x10000070, 0x80800101},
 {0x10209230, 0x111}, {0x1020923c, 0x1}, {0x1000f800, 0xa121}, {0x1000f80c, 0x1},
 {0x10209240, 0x121}, {0x1020924c, 0x80000001}, {0x10209220, 0xfd000001}, {0x1020922c, 0x1},
};
#define N (sizeof regs / sizeof regs[0])
static unsigned delays, order[64], n;
static struct reg *find(unsigned phys) {
 for (unsigned i = 0; i < N; i++) if (regs[i].phys == phys) return &regs[i];
 return 0;
}
static unsigned rd(void *c, unsigned phys) { (void)c; struct reg *r = find(phys); assert(r); return r->value; }
static void wr(void *c, unsigned phys, unsigned v) {
 (void)c; order[n++] = phys;
 if (phys == 0x10003008) { find(0x10003018)->value |= v; return; }   /* PERI SET */
 if (phys == 0x10003010) { find(0x10003018)->value &= ~v; return; }  /* PERI CLR */
 if (phys == 0x10001040) { find(0x10001048)->value |= v; return; }   /* INFRA SET */
 if (phys == 0x10001044) { find(0x10001048)->value &= ~v; return; }  /* INFRA CLR */
 struct reg *r = find(phys); assert(r); r->value = v;
}
static void delay(unsigned us) { delays += us; }
static int golden(unsigned phys, unsigned mask, unsigned value) { return (find(phys)->value & mask) == value; }
int main(void) {
 struct reg saved[N]; memcpy(saved, regs, sizeof regs);
 struct y2_dcm_io io = { 0, rd, wr };
 struct y2_ga_state s;
 /* At 15's real entry eMMC/SD are gated (PERI bits 12..14) and DIS is off. */
 struct y2_ga_conditions c = { .dis_off = 1 };
 y2_golden_apply(&io, Y2_GA_ALL, &c, &s);
 assert(golden(0x10003018, 0x402, 0x402));                 /* THERM, USB0 gated */
 assert(golden(0x10001048, 0x91bfe7 & ~(1U << 23), 0x80bfe6 & ~(1U << 23)));
 assert(!(find(0x10001048)->value & (1U << 23)) || (saved[1].value & (1U << 23)));
 assert(golden(0x10000050, 0x80808080, 0x80808080));
 assert(golden(0x10000060, 0x80808000, 0x80808000));
 assert(!(find(0x10000060)->value & 0x80));                /* UART mux untouched */
 assert(golden(0x10000070, 0x80808080, 0x80808080));
 assert(golden(0x10209230, 1, 0) && golden(0x1000f800, 1, 0));  /* MMPLL, VENCPLL */
 assert(golden(0x10209220, 1, 0) && !(find(0x10209220)->value & 0x1000000)); /* UNIVPLL */
 assert(golden(0x10209240, 1, 0) && golden(0x1020924c, 3, 2)); /* MSDCPLL off+iso */
 assert(s.pll_off == 0xf);
 y2_golden_restore(&io, &s, delay);
 assert(!memcmp(saved, regs, sizeof regs));                /* bit-exact restore */
 assert(delays == 4 * 11);
 /* DIS on and MSDC2 clocked: MM/VENC/MSDC PLLs untouched; USB0 still on
  * (no PERI group) so UNIVPLL stays on too. */
 find(0x10003018)->value &= ~0x4000U;
 c.dis_off = 0; n = 0;
 y2_golden_apply(&io, Y2_GA_PLLS, &c, &s);
 assert(s.pll_off == 0 && golden(0x10209230, 1, 1) && golden(0x10209240, 1, 1));
 find(0x10003018)->value |= 0x4400;
 y2_golden_apply(&io, Y2_GA_PLLS, &c, &s);
 assert(s.pll_off == 0xc && golden(0x1020924c, 3, 2) && golden(0x10209220, 1, 0));
 y2_golden_restore(&io, &s, delay);
 assert(find(0x1020924c)->value == 0x80000001 && find(0x10209240)->value == 0x121);
 /* Group 0: nothing written. */
 n = 0; y2_golden_apply(&io, 0, &c, &s); y2_golden_restore(&io, &s, delay); assert(n == 0);
 return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
