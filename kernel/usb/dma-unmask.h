/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef Y2_USB_DMA_UNMASK_H
#define Y2_USB_DMA_UNMASK_H
/* MT6582 MUSB DMA_INTR (MAC + 0x200), the MediaTek layout also used by upstream
 * drivers/usb/musb/mediatek.c: bits 7..0 channel status (write 1 to clear),
 * 15..8 the current unmask (read back 0x0000ff00 on a fresh 16 boot, observed
 * 2026-10-12), 23..16 write 1 to clear and 31..24 write 1 to set the unmask. Stock and
 * upstream write 0xff0000ff exactly once, when the DMA controller is created.
 *
 * Defect B (13, 14, 15, archives 104/106/108/109): after every SPM sleep the
 * gadget configures and RX (PIO) works, but transmit DMA is programmed and its
 * completion interrupt never arrives. A lost unmask explains exactly that, and
 * nothing rewrites it: reconnect rewrites L1INTM, restore W1C-clears only the
 * status byte. The re-arm writes only the set field, so it can never clear a
 * pending completion. */
#define Y2_USB_DMA_INTR             0x200
#define Y2_USB_DMA_UNMASK_SET       0xff000000U
#define Y2_USB_DMA_UNMASK_STATE     0x0000ff00U

static inline int y2_usb_dma_unmask_lost(unsigned value)
{
	return (value & Y2_USB_DMA_UNMASK_STATE) != Y2_USB_DMA_UNMASK_STATE;
}
#endif
