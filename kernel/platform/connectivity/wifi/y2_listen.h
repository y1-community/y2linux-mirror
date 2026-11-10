/* SPDX-License-Identifier: GPL-2.0-only */
/* Channels the regulatory domain allows only to listen on (cfg80211's NO_IR: "passive scan").
 *
 * Without a country the world domain applies, where channels 12 and 13 may be received on but
 * not transmitted on until an access point was heard there. The driver used to leave such
 * channels out completely, so a network on channel 13 never appeared (Y2Linux #110, Australia;
 * the same holds for most of Europe and Asia). Now every scan is followed by a second pass that
 * only listens on them. A beacon heard there makes cfg80211 lift the restriction for that
 * channel (its beacon hint); the flags are read again before every scan, and from then on the
 * channel is an ordinary one: scanned actively and joined. Nothing is ever sent on a
 * listen-only channel before that. Pure helpers; the state lives in regulatory.c. */
#ifndef Y2_LISTEN_H
#define Y2_LISTEN_H

enum y2_channel_use { Y2_CHANNEL_OFF, Y2_CHANNEL_ALLOWED, Y2_CHANNEL_LISTEN };

struct y2_listen {
	unsigned char channel[14];  /* 1: listen-only (index: channel number - 1) */
	unsigned char pass;         /* the scan request being built is the listen pass */
	unsigned char listened;     /* the last scan request built was a listen pass */
};

/* What 2.4 GHz channel `number` may be used for, from cfg80211's flags. Channel 14 (Japan,
 * 802.11b only) stays out, as before. */
static inline enum y2_channel_use y2_channel_use(unsigned number, int disabled, int radar, int no_ir)
{
	if (!number || number > 13 || disabled || radar)
		return Y2_CHANNEL_OFF;
	return no_ir ? Y2_CHANNEL_LISTEN : Y2_CHANNEL_ALLOWED;
}

static inline int y2_listen_any(const struct y2_listen *listen)
{
	for (unsigned i = 0; i < 14; i++)
		if (listen->channel[i])
			return 1;
	return 0;
}

/* Whether channel `number` belongs in the scan request being built: the listen-only channels in
 * the listen pass, all others in the ordinary pass. */
static inline int y2_listen_takes(const struct y2_listen *listen, unsigned number)
{
	if (!number || number > 14)
		return 0;
	return !listen->channel[number - 1] == !listen->pass;
}

/* A scan pass ended: whether a listen pass follows it before the scan is reported done. Never
 * after a listen pass, so a scan is two passes at most. */
static inline int y2_listen_follows(const struct y2_listen *listen)
{
	return !listen->listened && y2_listen_any(listen);
}

#endif
