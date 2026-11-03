/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef Y2_INFRA_PDN_H
#define Y2_INFRA_PDN_H
#include <linux/errno.h>
#include <linux/kconfig.h>
#include <linux/types.h>
#if IS_ENABLED(CONFIG_Y2_POWER)
bool y2_infra_pdn_wanted(void);
int y2_infra_pdn_save(void);
int y2_infra_pdn_restore(void);
#else
static inline bool y2_infra_pdn_wanted(void) { return false; }
static inline int y2_infra_pdn_save(void) { return -ENODEV; }
static inline int y2_infra_pdn_restore(void) { return -ENODEV; }
#endif
#endif
