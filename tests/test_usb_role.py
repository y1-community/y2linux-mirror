"""Y2Linux #102: the USB controller stays a peripheral after every disconnect.

Report e17c1a4d (9.3.17): the first connect to a Windows PC worked, then every
plug-in logged "musb-hdrc: Babble" and the player never enumerated again until a
restart. A disconnect while no gadget was bound (the MTP responder restarting)
reached musb_g_disconnect()'s "unhandled disconnect" case, which makes the
controller a host; nothing cleared that, so each bus reset became host babble.
"""
from pathlib import Path
import re
import unittest

from test_power import run_c

ROOT = Path(__file__).resolve().parents[1]
USB = ROOT / 'kernel/platform/usb.c'
ROLE = ROOT / 'kernel/usb/role.h'

# The OTG states and the musb_g_disconnect() state switch of Linux 6.18
# (drivers/usb/musb/musb_gadget.c), reduced to what changes role and state.
MODEL = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
enum usb_otg_state {
    OTG_STATE_UNDEFINED = 0, OTG_STATE_B_IDLE, OTG_STATE_B_SRP_INIT,
    OTG_STATE_B_PERIPHERAL, OTG_STATE_B_WAIT_ACON, OTG_STATE_B_HOST,
    OTG_STATE_A_IDLE, OTG_STATE_A_WAIT_VRISE, OTG_STATE_A_WAIT_BCON,
    OTG_STATE_A_HOST, OTG_STATE_A_SUSPEND, OTG_STATE_A_PERIPHERAL,
};
struct musb { bool is_host; enum usb_otg_state state; void *gadget_driver; };
static enum usb_otg_state musb_get_state(struct musb *m) { return m->state; }
static void musb_set_state(struct musb *m, enum usb_otg_state s) { m->state = s; }
static int gadget;
static void musb_g_disconnect(struct musb *musb)
{
    switch (musb_get_state(musb)) {
    default:
        musb_set_state(musb, OTG_STATE_A_IDLE);
        musb->is_host = true;
        break;
    case OTG_STATE_A_PERIPHERAL:
        musb_set_state(musb, OTG_STATE_A_WAIT_BCON);
        musb->is_host = true;
        break;
    case OTG_STATE_B_WAIT_ACON:
    case OTG_STATE_B_HOST:
    case OTG_STATE_B_PERIPHERAL:
    case OTG_STATE_B_IDLE:
        musb_set_state(musb, OTG_STATE_B_IDLE);
        break;
    case OTG_STATE_B_SRP_INIT:
        break;
    }
}
'''


class UsbRole(unittest.TestCase):
    def test_a_disconnect_without_a_gadget_leaves_a_peripheral(self):
        run_c(MODEL + ROLE.read_text() + r'''
int main(void) {
    /* The report: the gadget was unbound (UNDEFINED) when the cable went. */
    struct musb plain = { false, OTG_STATE_UNDEFINED, NULL };
    musb_g_disconnect(&plain);
    assert(plain.is_host && plain.state == OTG_STATE_A_IDLE);  /* upstream alone */

    struct musb m = { false, OTG_STATE_UNDEFINED, NULL };
    y2_musb_disconnect(&m);
    assert(!m.is_host && m.state == OTG_STATE_UNDEFINED);
    /* The responder binds again: musb_gadget_start sets B_IDLE; a bus reset in
     * B_IDLE as a peripheral enumerates. */
    m.gadget_driver = &gadget; m.state = OTG_STATE_B_IDLE;
    assert(!m.is_host);

    /* Every starting state, bound or not, ends as an idle peripheral. */
    for (int s = OTG_STATE_UNDEFINED; s <= OTG_STATE_A_PERIPHERAL; s++)
        for (int bound = 0; bound < 2; bound++) {
            struct musb c = { s >= OTG_STATE_A_IDLE, (enum usb_otg_state)s,
                              bound ? &gadget : NULL };
            y2_musb_disconnect(&c);
            assert(!c.is_host);
            assert(c.state == (bound ? OTG_STATE_B_IDLE : OTG_STATE_UNDEFINED) ||
                   (c.state == OTG_STATE_B_IDLE && s != OTG_STATE_B_SRP_INIT) ||
                   c.state == OTG_STATE_B_SRP_INIT);
            /* A reconnect starts from a peripheral too, even after a stray
             * host flag. */
            c.is_host = true; c.state = OTG_STATE_A_IDLE;
            y2_musb_peripheral(&c);
            assert(!c.is_host);
            assert(c.state == (bound ? OTG_STATE_B_IDLE : OTG_STATE_UNDEFINED));
        }
    /* A live connection keeps its state. */
    struct musb live = { false, OTG_STATE_B_PERIPHERAL, &gadget };
    y2_musb_peripheral(&live);
    assert(live.state == OTG_STATE_B_PERIPHERAL);
    return 0;
}
''')

    def test_every_platform_disconnect_and_reconnect_uses_the_peripheral_rule(self):
        source = USB.read_text()
        code = re.sub(r'/\*.*?\*/', '', source, flags=re.S)
        self.assertNotIn('musb_g_disconnect(', code.replace('y2_musb_disconnect(', ''))
        self.assertEqual(code.count('y2_musb_disconnect('), 3)  # sleep, stop, detach
        reconnect = code[code.index('static int y2_usb_reconnect('):]
        reconnect = reconnect[:reconnect.index('\n}\n')]
        self.assertLess(reconnect.index('y2_musb_peripheral(y2_musb);'),
                        reconnect.index('musb_start(y2_musb);'))


if __name__ == '__main__':
    unittest.main()
