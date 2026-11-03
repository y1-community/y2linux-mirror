/* SPDX-License-Identifier: GPL-2.0-only */
/* POWER-FINAL-17: gate the never-used modem's (MD1) peripheral clock
 * requests. Stock suspend golden PERI_PDN0_MD1_STA (0x10003030) is all 26
 * bits gated; the Y2 read 0 at every 16 sleep entry (archive 110). Layout and
 * harmlessness proven by the owner-authorised live test on 16 (ledger F11):
 * MD1_SET 0x20 / MD1_CLR 0x28 write-one, STA 0x30; all bits set left the AP
 * PDN0 gates, USB, Wi-Fi and the charger untouched. */
#ifndef Y2_MD1_GATE_POLICY_H
#define Y2_MD1_GATE_POLICY_H
#include "dcm-policy.h"

#define Y2_MD1_SET 0x10003020U
#define Y2_MD1_CLR 0x10003028U
#define Y2_MD1_STA 0x10003030U
#define Y2_MD1_ALL 0x03ffffffU

/* Gate (or release) every MD1 request and read the status back. Returns 0
 * when the readback matches, -EIO when it does not. The IO context is the
 * dcm status int; a mapping failure is reported through it. */
static inline int y2_md1_gate(const struct y2_dcm_io *io, int gate, unsigned *sta)
{
	io->write(io->context, gate ? Y2_MD1_SET : Y2_MD1_CLR, Y2_MD1_ALL);
	*sta = io->read(io->context, Y2_MD1_STA);
	if (*(int *)io->context) return *(int *)io->context;
	return (*sta & Y2_MD1_ALL) == (gate ? Y2_MD1_ALL : 0) ? 0 : -EIO;
}

/* At sleep entry: re-gate when the requests were released behind our back
 * (e.g. INFRA really lost power). Returns 1 when it re-gated. */
static inline int y2_md1_regate(const struct y2_dcm_io *io, unsigned *sta)
{
	*sta = io->read(io->context, Y2_MD1_STA);
	if ((*sta & Y2_MD1_ALL) == Y2_MD1_ALL) return 0;
	y2_md1_gate(io, 1, sta);
	return 1;
}
#endif
