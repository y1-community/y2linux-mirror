/* SPDX-License-Identifier: GPL-2.0-only */
/* MT6582 DCM (hardware dynamic clock gating): exact Y2 FM kernel
 * mt_dcm_init -> dcm_enable(0x1ff) at 0xc003c154 (REA assembly, POWER-FINAL-16
 * ledger G3). Stock enables every group at boot; Y2 Linux had only the SLIDLE
 * bus DCM, which left 9 golden rows different at sleep entry (INFRACFG
 * 0x010/0x014/0x050/0x054/0x058, PERICFG 0x050, DRAMC 0x1dc, MCUSYS 0x05c).
 *
 * Physical addresses; the caller maps them. Order and values follow the
 * assembly. Not adopted here: ISP/VDEC groups (domains stay off), MFG (Lima
 * owns MFG registers), and the peripheral-local MSDC/USB/I2C/0x1000d000 bits
 * (their registers are only safe while each block's clock runs). */
#ifndef Y2_DCM_POLICY_H
#define Y2_DCM_POLICY_H
struct y2_dcm_io {
	void *context;
	unsigned (*read)(void *, unsigned phys);
	void (*write)(void *, unsigned phys, unsigned value);
};
static inline void y2_dcm_set(const struct y2_dcm_io *io, unsigned phys,
			      unsigned clear, unsigned set)
{
	io->write(io->context, phys, (io->read(io->context, phys) & ~clear) | set);
}

/* Groups 0x1 CPU, 0x100 TOPCKGEN, 0x2 INFRA (+DRAMC), 0x4 PERI. */
static inline void y2_dcm_core(const struct y2_dcm_io *io)
{
	/* CPU_DCM: MCU_BIU_CONTROL bit 12, MCUSYS+0x5c bit 9. */
	y2_dcm_set(io, 0x10208000, 0, 0x1000);
	y2_dcm_set(io, 0x1020005c, 0, 0x200);
	/* TOPCKGEN_DCM: CLK_SCP_CFG_0 low 10 bits, CLK_SCP_CFG_1 |= 0x11. */
	y2_dcm_set(io, 0x10000200, 0, 0x3ff);
	y2_dcm_set(io, 0x10000204, 0, 0x11);
	/* IFR_DCM. */
	y2_dcm_set(io, 0x10001008, 0x1f, 0);
	y2_dcm_set(io, 0x10001010, 0, 0x7);
	y2_dcm_set(io, 0x10001014, 0, 0x1);
	y2_dcm_set(io, 0x10001050, 0, 0x303);
	y2_dcm_set(io, 0x10001054, 0, 0x300);
	y2_dcm_set(io, 0x10001054, 0x7f, 0);
	io->write(io->context, 0x10001058, 0xfff0f0f8);
	y2_dcm_set(io, 0x100041dc, 0, 0x3000000); /* DRAMC */
	/* PER_DCM (PERICFG only). */
	y2_dcm_set(io, 0x10003050, 0x1f00, 0);
	y2_dcm_set(io, 0x10003050, 0, 0xf3);
	y2_dcm_set(io, 0x10003054, 0, 0x80);
	y2_dcm_set(io, 0x10003054, 0x7f, 0);
	y2_dcm_set(io, 0x10003058, 0x7, 0);
	y2_dcm_set(io, 0x10003058, 0xf00, 0);
	y2_dcm_set(io, 0x10003058, 0x1f0000, 0);
}

/* Groups 0x8 SMI and 0x20 DIS: MMSYS registers, lost at every DIS power-down.
 * Call only with DIS powered and SMI common/LARB0 clocked (CRTC enable). */
static inline void y2_dcm_mm(const struct y2_dcm_io *io)
{
	/* SMI_DCM (stock also sets 0x1000c014 bit 2 and 0x102055f0 bit 0;
	 * not adopted until those blocks are identified). */
	io->write(io->context, 0x14011300, 1);
	/* DIS_DCM group 0 (the CG0 engines: OVL, RDMA, COLOR, mutex, ...). */
	io->write(io->context, 0x14000120, 0);
	io->write(io->context, 0x14000124, 0);
	io->write(io->context, 0x14000128, 0xffffffff);
	/* Group 1 (CG1: DSI engine/digital, DPI) stays without DCM
	 * (POWER-FINAL-47). Stock enables it once at boot while its display is
	 * already streaming; Linux applies DCM at every CRTC enable, with the DSI
	 * idle. Three times after that (archives 143, 147's recovery, 150) the DSI
	 * register port answered every read one register late from the first
	 * modeset on, and power-cycling DSI clocks and PHY did not clear it, so
	 * no frame ever started. With the screen on the DSI streams and is never
	 * idle; with it off DIS is powered down: no power cost. */
	io->write(io->context, 0x1400012c, 0xf);
	io->write(io->context, 0x14000130, 0xf);
	y2_dcm_set(io, 0x14010014, 0, 0x8000); /* SMI LARB0 */
}
#endif
