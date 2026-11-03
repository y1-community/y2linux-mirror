/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef Y2_USB_SAVECURRENT_H
#define Y2_USB_SAVECURRENT_H
/* POWER-FINAL-13: the stock usb_phy_savecurrent() across system sleep.
 *
 * Exact sequence of the Y2 FM kernel's usb_phy_savecurrent_internal
 * (0xc04d98f0; PHY at 0x11210800, offsets below are from that base),
 * identical to vendor mt6582 usb20_phy.c. Stock runs it whenever no cable is
 * attached; Y2 only ever ran the recover half (recover.h) at boot, so the PHY
 * (480 MHz PLL, HS transceiver bias) stayed powered through every sleep.
 *
 * Here it runs only across system sleep with no cable and no live session,
 * and the wake side writes every touched byte back to its saved value in the
 * vendor usb_phy_recover order, so the existing reconnect checks (0x68..0x6e,
 * session 0x6c/0x6d) see exactly the recovered state they were proven with. */
struct y2_phy_io {
	void *context;
	unsigned (*read)(void *context, unsigned offset);
	void (*write)(void *context, unsigned offset, unsigned value);
	void (*delay)(void *context, unsigned us);
};
#define Y2_PHY_SAVE_REGS 9
static const unsigned char y2_phy_save_offsets[Y2_PHY_SAVE_REGS] = {
	0x1a, 0x63, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e,
};
static inline void y2_phy_rmw(const struct y2_phy_io *io, unsigned offset,
			      unsigned clear, unsigned set)
{
	io->write(io->context, offset, (io->read(io->context, offset) & ~clear & 0xff) | set);
}
static inline void y2_phy_save(const struct y2_phy_io *io, unsigned char saved[Y2_PHY_SAVE_REGS])
{
	unsigned i;
	for (i = 0; i < Y2_PHY_SAVE_REGS; i++)
		saved[i] = io->read(io->context, y2_phy_save_offsets[i]);
}
/* usb_phy_savecurrent_internal, step for step (printks omitted). */
static inline void y2_phy_savecurrent(const struct y2_phy_io *io)
{
	y2_phy_rmw(io, 0x6b, 0x04, 0);    /* force_uart_en = 0 */
	y2_phy_rmw(io, 0x6e, 0x01, 0);    /* RG_UART_EN = 0 */
	y2_phy_rmw(io, 0x6a, 0x04, 0);    /* release force_suspendm */
	y2_phy_rmw(io, 0x68, 0, 0xc0);    /* RG_DPPULLDOWN/RG_DMPULLDOWN */
	y2_phy_rmw(io, 0x68, 0x30, 0);    /* RG_XCVRSEL[1:0] = 01 ... */
	y2_phy_rmw(io, 0x68, 0, 0x10);
	y2_phy_rmw(io, 0x68, 0, 0x04);    /* RG_TERMSEL = 1 */
	y2_phy_rmw(io, 0x69, 0x3c, 0);    /* RG_DATAIN[3:0] = 0 */
	y2_phy_rmw(io, 0x6a, 0, 0xba);    /* force pulldowns, xcvrsel, termsel, datain */
	y2_phy_rmw(io, 0x1a, 0x80, 0);    /* RG_USB20_BC11_SW_EN = 0 */
	y2_phy_rmw(io, 0x1a, 0x10, 0);    /* RG_USB20_OTG_VBUSSCMP_EN = 0 */
	io->delay(io->context, 800);
	y2_phy_rmw(io, 0x63, 0, 0x02);    /* rg_usb20_pll_stable = 1 */
	io->delay(io->context, 1);
	y2_phy_rmw(io, 0x6a, 0, 0x04);    /* force_suspendm = 1: PHY and its PLL off */
	io->delay(io->context, 1);
	y2_phy_rmw(io, 0x6c, 0x10, 0);    /* Y2 stock tail: IDDIG/VBUS forcing */
	y2_phy_rmw(io, 0x6c, 0, 0x2e);
	y2_phy_rmw(io, 0x6d, 0, 0x3e);
}
/* usb_phy_recover order with the saved bytes: release force_suspendm first,
 * then the forced values, then the forces, BC1.1/OTG, PLL-stable, the
 * vendor 800 us settle, and the session forcing last. Returns the number of
 * bytes that did not read back as saved (0 = exact). */
static inline unsigned y2_phy_restore(const struct y2_phy_io *io,
				      const unsigned char saved[Y2_PHY_SAVE_REGS])
{
	/* Indices into y2_phy_save_offsets: 0x6b 0x6e 0x6a(suspendm) 0x68 0x69
	 * 0x6a 0x1a 0x63, then after the settle 0x6c 0x6d. */
	static const unsigned char order[8] = {5, 8, 4, 2, 3, 4, 0, 1};
	unsigned i, value, mismatches = 0;
	io->delay(io->context, 50);
	for (i = 0; i < 8; i++) {
		unsigned index = order[i], offset = y2_phy_save_offsets[index];
		if (i == 2) { /* 0x6a: force_suspendm only, before the other forces */
			value = io->read(io->context, 0x6a);
			io->write(io->context, 0x6a, (value & ~0x04U) | (saved[index] & 0x04U));
			continue;
		}
		io->write(io->context, offset, saved[index]);
	}
	io->delay(io->context, 800);
	io->write(io->context, 0x6c, saved[6]);
	io->write(io->context, 0x6d, saved[7]);
	for (i = 0; i < Y2_PHY_SAVE_REGS; i++)
		if (io->read(io->context, y2_phy_save_offsets[i]) != saved[i])
			mismatches++;
	return mismatches;
}
#endif
