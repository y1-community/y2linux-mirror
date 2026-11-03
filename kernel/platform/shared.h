/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef Y2_PLATFORM_SHARED_H
#define Y2_PLATFORM_SHARED_H
#include <linux/types.h>
struct y2_pwrap_snapshot;
int y2_msdc_source(unsigned id, bool highspeed);
int y2_ccf_usb_read(unsigned address, unsigned *value);
int y2_pmic_snapshot(struct y2_pwrap_snapshot *snapshot);
int y2_pmic_cpu_voltage_ready(void);
int y2_pmic_spm_prepare(void);
void y2_pmic_golden_capture(void);
void y2_pmic_vproc_sleep_begin(void);
void y2_pmic_vproc_sleep_end(void);
void y2_pmic_sleep_begin(void);
void y2_pmic_sleep_end(void);
#endif
