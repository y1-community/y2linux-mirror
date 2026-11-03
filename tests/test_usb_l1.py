"""POWER-FINAL-06: MUSB L1 interrupt ownership and parked-on-fault lifecycle.

POWER-FINAL-05 overnight (2026-10-08 23:34 UTC): after a maintenance wake the
resume restored L1INTM=0xf00 and the PHY POWERDWN level condition (L1 bit 0x800)
stormed the handler until the guard latched USB off ("l1=800/f00 ... usb=0/0
tx=0/0 rx=0/0"). The terminal stop then unregistered musb-hdrc, freeing the
gadget device that the ECM netdev usb0 keeps as dev.parent, and the first `ip`
run after the radios came back crashed in strlen(NULL) in rtnl_fill_ifinfo.
"""
from pathlib import Path
import unittest

from test_audio import function
from test_power import run_c

ROOT = Path(__file__).resolve().parents[1]
USB = ROOT / 'kernel/platform/usb.c'


class UsbL1(unittest.TestCase):
    def test_l1_ownership_policy_matches_the_overnight_fault(self):
        run_c(r'''
#include <assert.h>
''' + (ROOT / 'kernel/usb/l1.h').read_text() + r'''
int main(void) {
    /* The fault: status 0x800 (POWERDWN), mask 0xf00, nothing owned pending. */
    assert(y2_usb_l1_unowned_pending(0x800, 0xf00) == 0x800);
    assert(y2_usb_l1_mask_after(0xf00, 0x800) == 0x700);
    /* Restoring a polluted saved mask keeps only TX/RX/USBCOM/DMA. */
    assert(y2_usb_l1_restore_mask(0xf00) == 0);
    assert(y2_usb_l1_restore_mask(0xf0f) == 0xf);
    assert(y2_usb_l1_restore_mask(7) == 7 && y2_usb_l1_restore_mask(15) == 15);
    assert(y2_usb_l1_restore_mask(0) == 0);
    /* Owned bits are never reported as unowned, masked bits never pending. */
    for (unsigned status = 0; status < 0x1000; status += 0x11)
        for (unsigned mask = 0; mask < 0x1000; mask += 0x7) {
            unsigned u = y2_usb_l1_unowned_pending(status, mask);
            assert(!(u & Y2_USB_L1_OWNED) && !(u & ~mask) && !(u & ~status));
            assert((y2_usb_l1_mask_after(mask, u) & u) == 0);
            assert((y2_usb_l1_mask_after(mask, u) & Y2_USB_L1_OWNED) == (mask & Y2_USB_L1_OWNED));
        }
    return 0;
}
''')

    def test_interrupt_masks_unowned_level_bits_before_deciding(self):
        source = USB.read_text()
        isr = function(source, 'y2_musb_interrupt')
        self.assertIn('y2_usb_l1_unowned_pending', isr)
        self.assertIn('y2_usb_l1_mask_after', isr)
        # The mask is written at the source before the serviced bits are used.
        self.assertLess(isr.index('y2_usb_l1_mask_after'), isr.index('if(pending & 7)'))
        self.assertLess(isr.index('rc=IRQ_HANDLED;'), isr.index('if(pending & 7)'))
        before = function(source, 'y2_musb_before_restore') if 'static void y2_musb_before_restore' in source else \
            source[source.index('void y2_musb_before_restore'):source.index('void y2_musb_after_restore')]
        self.assertIn('y2_usb_l1_restore_mask(readl(musb->mregs + 0xa4))', before)

    def test_terminal_stop_parks_the_controller_and_never_unregisters_it(self):
        source = USB.read_text()
        finish = function(source, 'y2_usb_finish')
        self.assertNotIn('platform_device_unregister', finish)
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define Y2_USB_PHY_BASE 0x11210000
#define Y2_USB_PHY_BYTES 0x1000
#define MUSB_POWER 1
#define MUSB_POWER_SOFTCONN 0x40
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define spin_lock_irqsave(lock,flags) do {(flags)=0;assert(!*(lock));*(lock)=1;} while(0)
#define spin_unlock_irqrestore(lock,flags) do {(void)(flags);assert(*(lock));*(lock)=0;} while(0)
#define pr_info(...) ((void)0)
enum { Y2_USB_STOPPED = 7 };
struct y2_usb_live { int result; unsigned stage, irqs, events, configured; };
struct musb { unsigned char *mregs; int lock; };
static unsigned char mac[1024];
static struct musb controller = {mac, 0}, *y2_musb = &controller;
static struct y2_usb_live y2_live;
static bool y2_usb_finished, y2_usb_detached, y2_usb_pm_held;
static void *y2_usb_child = &controller, *y2_usb_phy = mac;
static unsigned unregistered, disconnected, stopped, iounmapped, phases, power_cleared, gets, puts;
static void y2_usb_set_power(void *a, int b) { (void)a; (void)b; ++power_cleared; }
static int y2_usb_runtime_get(void) { ++gets; y2_usb_pm_held = true; return 0; }
static void y2_usb_runtime_put(void) { if (y2_usb_pm_held) ++puts; y2_usb_pm_held = false; }
static void y2_usb_phase(unsigned stage) { y2_live.stage = stage; ++phases; }
static void musb_g_disconnect(struct musb *m) { assert(m->lock); ++disconnected; }
static void musb_stop(struct musb *m) { assert(m->lock && disconnected); ++stopped; }
static void iounmap(void *p) { (void)p; ++iounmapped; }
static void release_mem_region(unsigned a, unsigned b) { (void)a; (void)b; }
static void writel(unsigned v, void *p) { memcpy(p, &v, 4); }
static void writeb(unsigned v, void *p) { *(uint8_t *)p = v; }
static unsigned readb(const void *p) { return *(const uint8_t *)p; }
static void platform_device_unregister(void *p) { (void)p; ++unregistered; }
''' + function(source, 'y2_usb_finish') + r'''
static void reset(bool detached, int result) {
    memset(mac, 0, sizeof mac); y2_live = (struct y2_usb_live){.result = result};
    mac[MUSB_POWER] = MUSB_POWER_SOFTCONN | 0x20; mac[0xa4] = 0xf;
    y2_usb_finished = false; y2_usb_detached = detached; y2_usb_pm_held = false;
    y2_usb_child = &controller; y2_usb_phy = mac; controller.lock = 0;
    unregistered = disconnected = stopped = iounmapped = phases = power_cleared = gets = puts = 0;
}
int main(void) {
    (void)platform_device_unregister; /* present only to prove it is never called */
    /* Fault while attached: quiesce exactly once, keep the device registered. */
    reset(false, -75); y2_usb_finish();
    assert(!unregistered && y2_usb_child && y2_usb_phy && !iounmapped);
    assert(disconnected == 1 && stopped == 1 && y2_usb_detached && y2_usb_finished);
    assert(!(mac[MUSB_POWER] & MUSB_POWER_SOFTCONN) && *(uint32_t *)(mac + 0xa4) == 0);
    assert(gets == 1 && puts == 1 && !y2_usb_pm_held && phases == 0 && !y2_live.configured);
    /* Fault after a cable removal: already detached, nothing is torn down twice. */
    reset(true, -75); y2_usb_finish();
    assert(!unregistered && !disconnected && !stopped && y2_usb_child && gets == 1 && puts == 1);
    /* No controller was ever registered: the PHY mapping is released as before. */
    reset(false, -19); y2_usb_child = NULL; y2_musb = NULL; y2_usb_finish();
    assert(!unregistered && iounmapped == 1 && y2_usb_phy == NULL);
    /* A non-fault finish still announces STOPPED to PID1. */
    y2_musb = &controller; reset(false, 0); y2_usb_finish();
    assert(phases == 1 && y2_live.stage == Y2_USB_STOPPED && !unregistered);
    return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
