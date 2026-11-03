/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef Y2_USB_L1_H
#define Y2_USB_L1_H
/* MT6582 MUSB level-1 interrupt mask ownership (USB_L1INTS/L1INTM, +0xa0/+0xa4).
 *
 * Y2 services exactly bits 0..3: TX, RX, USBCOM and DMA. Bits 8..11
 * (VBUSVALID, IDDIG, DRVVBUS, POWERDWN) are level conditions reported by the
 * PHY and cannot be cleared by software; POWERDWN is asserted whenever the
 * controller is idle without VBUS and the line is in SE0. If any of them is ever
 * left enabled in L1INTM, the handler sees an interrupt that explains none of the
 * serviced bits, never clears it, and after 512 repetitions the storm guard
 * latches the controller off until reboot (POWER-FINAL-05 overnight, 01:34:
 * `l1=800/f00` with every L2 status register zero).
 *
 * Two rules keep that impossible:
 *  - a saved mask is only ever restored with its owned bits;
 *  - an unowned bit that asserts is masked at its source and counted, never
 *    treated as a stuck serviced interrupt.
 */
#define Y2_USB_L1_OWNED 0x0000000fU
static inline unsigned y2_usb_l1_restore_mask(unsigned saved)
{
    return saved & Y2_USB_L1_OWNED;
}
/* Unowned bits that are both asserted and unmasked right now. */
static inline unsigned y2_usb_l1_unowned_pending(unsigned status, unsigned mask)
{
    return status & mask & ~Y2_USB_L1_OWNED;
}
/* The mask to write back after masking those bits at the source. */
static inline unsigned y2_usb_l1_mask_after(unsigned mask, unsigned unowned)
{
    return mask & ~unowned;
}
#endif
