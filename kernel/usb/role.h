/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef Y2_USB_ROLE_H
#define Y2_USB_ROLE_H
/* The Y2 is a USB device only (dr_mode "peripheral", g.is_otg 0, the role
 * switch refuses host). Upstream musb still keeps an OTG state machine, and
 * musb_g_disconnect() treats every state outside its B-device list as an
 * "unhandled disconnect": it sets A_IDLE and MUSB_HST_MODE (is_host = true).
 * The state is OTG_STATE_UNDEFINED whenever no gadget driver is bound
 * (musb_gadget_stop), e.g. while the platform restarts the MTP responder, so a
 * cable removal, role change or sleep in that window turned the controller
 * into a host. Nothing upstream clears is_host again (musb_gadget_start only
 * sets B_IDLE), and musb_handle_intr_reset() then takes every bus reset of the
 * computer for host babble: "musb-hdrc: Babble" after each plug-in and the
 * player never enumerated again until a restart (Y2Linux #102, report e17c1a4d).
 *
 * Every disconnect therefore goes through y2_musb_disconnect(), and a
 * reconnect starts from y2_musb_peripheral(): the controller is a peripheral,
 * idle as a B-device when a gadget is bound, undefined (as musb_gadget_stop
 * leaves it) when none is. Called with musb->lock held. */
static inline void y2_musb_peripheral(struct musb *musb)
{
	enum usb_otg_state state = musb_get_state(musb);

	musb->is_host = false;
	if (state == OTG_STATE_B_IDLE || state == OTG_STATE_B_PERIPHERAL)
		return;
	musb_set_state(musb, musb->gadget_driver ? OTG_STATE_B_IDLE :
		       OTG_STATE_UNDEFINED);
}

static inline void y2_musb_disconnect(struct musb *musb)
{
	musb_g_disconnect(musb);
	y2_musb_peripheral(musb);
}
#endif
