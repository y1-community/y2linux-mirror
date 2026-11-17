/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef Y2_STORAGE_LAYOUT_H
#define Y2_STORAGE_LAYOUT_H

/* Stock FM mmcblk0 is a window in native EMMC_USER. Verified by the own
 * stock kernel and owner SPFT readback; see storage06-addressing-correction.md.
 * The legacy DA concatenates 8.5 MiB of non-user hardware areas before USER;
 * its MBR at 20 MiB is therefore at native USER 11.5 MiB, not 20 MiB.
 * Keep real EXT_CSD untouched. These constants describe this board/layout only.
 *
 * Stock msdc_get_offset: offset = (20 MiB - boot1 - boot2 - RPMB - GP1..4) / 512,
 * read from EXT_CSD. The first qualified chip (RPMB 512 KiB) gives 23552; a later
 * batch with RPMB 4 MiB (Y2Reborn #130) gives 16384. The disk keeps the first
 * chip's 15203328 sectors on both: the media partition ends there, and on the
 * 4 MiB chip that window still ends inside stock's own (raw - 16384 - tail).
 */
#define Y2_EMMC_RAW_SECTORS 15269888U
#define Y2_EMMC_DA_MBR_SECTORS 40960U	/* legacy DA MBR at 20 MiB */
#define Y2_EMMC_USER_OFFSET 23552U	/* the first chip's offset (RPMB 512 KiB) */
#define Y2_EMMC_TAIL_SECTORS 43008U
#define Y2_EMMC_DISK_SECTORS (Y2_EMMC_RAW_SECTORS - Y2_EMMC_USER_OFFSET - Y2_EMMC_TAIL_SECTORS)

static inline unsigned y2_emmc_raw_sectors(const unsigned char *ext)
{
	return (unsigned)ext[212] | (unsigned)ext[213] << 8 |
	       (unsigned)ext[214] << 16 | (unsigned)ext[215] << 24;
}

static inline int y2_emmc_layout_valid(const unsigned char *ext)
{
	unsigned i;

	/* BOOT_SIZE_MULT=32: 4 MiB each; RPMB_SIZE_MULT=4 or 32: 512 KiB or
	 * 4 MiB. No GP partitions: any other geometry needs independent
	 * qualification.
	 */
	if (y2_emmc_raw_sectors(ext) != Y2_EMMC_RAW_SECTORS ||
	    ext[226] != 32 || (ext[168] != 4 && ext[168] != 32))
		return 0;
	for (i = 143; i < 155; i++)
		if (ext[i])
			return 0;
	return 1;
}

/* Native USER sector of logical sector 0 on a valid layout: the DA's 20 MiB
 * minus both boot partitions and RPMB (multiples of 128 KiB = 256 sectors).
 */
static inline unsigned y2_emmc_user_offset(const unsigned char *ext)
{
	return Y2_EMMC_DA_MBR_SECTORS - 256U * (2U * ext[226] + ext[168]);
}

/* Only after the original logical request passed storage-policy.h. Do not
 * mutate cmd->arg: the MMC core may retry it, and SBC/STOP are not addresses.
 */
static inline unsigned y2_emmc_wire_arg(unsigned opcode, unsigned arg, unsigned offset)
{
	switch (opcode) {
	case 17: case 18: case 24: case 25:
		return arg + offset;
	default:
		return arg;
	}
}
#endif
