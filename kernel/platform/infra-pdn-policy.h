/* SPDX-License-Identifier: GPL-2.0-only */
/* INFRASYS power-down state for the MT6582 full-suspend path.
 *
 * Vendor `spm_go_to_sleep(cpu_pdn=1, infra_pdn=1)` (BSP d53dd75c, confirmed in the
 * retained FM Y2 kernel: slp_infra_pdn defaults to 1) lets the PCM remove power
 * from the INFRASYS and DDRPHY MTCMOS domains. The PCM itself puts DRAM into
 * self-refresh and restores DDRPHY. The kernel is responsible for the rest: the
 * vendor restores the console UART (`mtk_uart_restore`) and every other driver
 * re-initialises from its own resume callback.
 *
 * MT6582 reference manual 2.3.2 ("INFRASYS MTCMOS domain") places two register
 * blocks inside the powered-down domain: INFRACFG (0x10201000) and PERICFG
 * (0x10003000). PERICFG carries the peripheral clock gates (PDN0), software
 * resets, DCM and AXI/AHB bandwidth limiter configuration, USB wake control and
 * the UART clock source. Their reset values are not what Linux or the bootloader
 * programmed (PDN0 resets to 0: every peripheral clock ungated). They are saved
 * before the PCM is kicked and written back immediately after the CPU returns,
 * before any other driver touches a peripheral.
 *
 * Only registers that are plain read/write (or the SET/CLR/STA gate triple) are
 * written back. INFRACFG's AXI fabric controls are observed read-only so that
 * the first real power-down reports whether the domain really lost state;
 * nothing is guessed. Peripheral register windows (UART, I2C, MSDC, USB, ...) are
 * not read here: by the time the platform enters sleep their drivers have
 * suspended them and gated their clocks, and each is restored by its own
 * resume path (the vendor restores nothing but the console UART either).
 *
 * The caller serialises suspend, owns the MMIO windows and runs with interrupts
 * off. The helpers perform only MMIO reads and, for registers that actually
 * changed, MMIO writes.
 */
#ifndef Y2_INFRA_PDN_POLICY_H
#define Y2_INFRA_PDN_POLICY_H
#include "spm-policy.h"

enum y2_infra_kind {
	Y2_INFRA_PLAIN,		/* read/write; restored and verified */
	Y2_INFRA_PLAIN_UNVERIFIED, /* read/write, may carry hardware-updated bits */
	Y2_INFRA_PDN0,		/* gate status at offset; SET at -0x10, CLR at -0x08 */
};
struct y2_infra_reg {
	unsigned short offset;
	unsigned char kind;
};
/* Documented read/write PERICFG registers (MT6582 manual, PERICFG, +0x10003000). */
static const struct y2_infra_reg y2_pericfg_regs[] = {
	{ 0x000, Y2_INFRA_PLAIN },		/* PERI_GLOBALCON_RST0 */
	{ 0x004, Y2_INFRA_PLAIN },		/* PERI_GLOBALCON_RST1 */
	{ 0x018, Y2_INFRA_PDN0 },		/* PERI_GLOBALCON_PDN0_STA (SET 0x08, CLR 0x10) */
	{ 0x050, Y2_INFRA_PLAIN_UNVERIFIED },	/* PERI_GLOBALCON_DCMCTL */
	{ 0x054, Y2_INFRA_PLAIN },		/* PERI_GLOBALCON_DCMDBC */
	{ 0x058, Y2_INFRA_PLAIN },		/* PERI_GLOBALCON_DCMFSEL */
	{ 0x05c, Y2_INFRA_PLAIN },		/* PERI_GLOBALCON_CKSEL */
	{ 0x204, Y2_INFRA_PLAIN_UNVERIFIED },	/* PERIAXI_BUS_CTL2 */
	{ 0x304, Y2_INFRA_PLAIN },		/* AHB bus 0 bandwidth limiter */
	{ 0x314, Y2_INFRA_PLAIN },		/* AHB bus 2 bandwidth limiter */
	{ 0x320, Y2_INFRA_PLAIN_UNVERIFIED },	/* PERIAXI_AXI_LMT_CON1 */
	{ 0x324, Y2_INFRA_PLAIN_UNVERIFIED },	/* PERIAXI_AXI_LMT_CON2 */
	{ 0x328, Y2_INFRA_PLAIN_UNVERIFIED },	/* PERIAXI_AXI_LMT_CON3 */
	{ 0x32c, Y2_INFRA_PLAIN_UNVERIFIED },	/* PERIAXI_AXI_LMT_CON4 (AP DMA) */
	{ 0x400, Y2_INFRA_PLAIN },		/* PERI USB wakeup control 0 */
	{ 0x40c, Y2_INFRA_PLAIN },		/* PERI UART clock source selection */
};
#define Y2_PERICFG_REGS ((unsigned)(sizeof(y2_pericfg_regs) / sizeof(y2_pericfg_regs[0])))
#define Y2_INFRA_PDN0_SET_OFFSET(sta) ((sta) - 0x10U)
#define Y2_INFRA_PDN0_CLR_OFFSET(sta) ((sta) - 0x08U)
#define Y2_INFRA_MAX_REGS 32U

struct y2_infra_result {
	unsigned lost;		/* bit n: register n differed after the wake */
	unsigned failed;	/* bit n: register n still differs after restore */
};
static inline void y2_infra_save(const struct y2_spm_io *io,
				 const struct y2_infra_reg *regs, unsigned count,
				 unsigned *values)
{
	unsigned i;
	for (i = 0; i < count; i++)
		values[i] = io->read(io->context, regs[i].offset);
}
/* Write back only what changed, then verify. A register that is already equal
 * (an aborted entry, or a domain that kept its state) is never written. */
static inline int y2_infra_restore(const struct y2_spm_io *io,
				   const struct y2_infra_reg *regs, unsigned count,
				   const unsigned *values, unsigned *woke,
				   struct y2_infra_result *result)
{
	unsigned i, now, clear, set;
	result->lost = result->failed = 0;
	for (i = 0; i < count; i++) {
		now = io->read(io->context, regs[i].offset);
		woke[i] = now;
		if (now == values[i])
			continue;
		result->lost |= 1U << i;
		if (regs[i].kind == Y2_INFRA_PDN0) {
			clear = now & ~values[i];	/* gated now, was running */
			set = values[i] & ~now;		/* running now, was gated */
			if (clear)
				io->write(io->context, Y2_INFRA_PDN0_CLR_OFFSET(regs[i].offset), clear);
			if (set)
				io->write(io->context, Y2_INFRA_PDN0_SET_OFFSET(regs[i].offset), set);
		} else {
			io->write(io->context, regs[i].offset, values[i]);
		}
		if (regs[i].kind != Y2_INFRA_PLAIN_UNVERIFIED &&
		    io->read(io->context, regs[i].offset) != values[i])
			result->failed |= 1U << i;
	}
	return result->failed ? -EIO : 0;
}

#endif
