// SPDX-License-Identifier: GPL-2.0-only
/* Temporary MT6582 AFE register/DMA laboratory (whitelisted, restorable).
 *
 * Purpose: find out, on the physical Y2, which AFE memory-interface and
 * interconnect features exist in this exact silicon. Development instrument,
 * not product code, never part of a candidate.
 *
 * Safety model (there is deliberately NO generic MMIO write primitive):
 *   - writes only to registers in wl[] and only to bits in that entry's mask;
 *   - every register's pre-experiment value is snapshotted on first touch and
 *     restore_all puts all of them back (reverse order);
 *   - writes are refused unless: this module holds the only runtime-PM
 *     reference of the AFE (no PCM stream open), DL1 is off and the external
 *     I2S output (AFE_I2S_CON3.EN) is off;
 *   - after each write DAC_CON0 and the IRQ status are checked; an unexpected
 *     change restores the register and reports ABORT;
 *   - every write/probe appends one JSON line to the "log" file.
 * Register access is limited to the AFE window (0x11220000, 0x000-0x7fc).
 * CONFIG_MODULE_UNLOAD=n on the installed kernel: stay resident until reboot.
 */
#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/utsname.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>

#define AFE_WINDOW	0x800
#define SLOTS		4
#define OUT_MAX		(64 * 1024)
#define LOG_MAX		(256 * 1024)
#define MAX_DMA		(512 * 1024)
#define GPIO_DIN_PHYS	0x10005500 /* GPIO DIN block, read-only; group 2 (GPIO32-47) at +0x20 */
#define SLA_MAX		(128 * 1024)

#define DAC_CON0	0x010
#define I2S_CON3	0x04c
#define IRQ_STATUS	0x3a4
#define TOP_CON3	0x00c
#define DL1_ON		BIT(1)
#define DAC_STATE_MASK	0x000000ff /* enables; *_RETM monitor bits excluded */

struct wl_entry {
	u16 off;
	const char *name;
	u32 mask;	/* the only bits this entry may ever modify */
};

static const struct wl_entry wl[] = {
	/* documented control */
	{ 0x010, "AFE_DAC_CON0",	0x00000046 }, /* DL1/DL2/AWB enable */
	{ 0x014, "AFE_DAC_CON1",	0x0360f0ff }, /* rates, mono, per-memif */
	{ 0x020, "AFE_CONN0",		0xffffffff },
	{ 0x024, "AFE_CONN1",		0xffffffff },
	{ 0x028, "AFE_CONN2",		0xffffffff },
	{ 0x02c, "AFE_CONN3",		0xffffffff },
	{ 0x030, "AFE_CONN4",		0xffffffff },
	{ 0x04c, "AFE_I2S_CON3",	0x80000f2a }, /* EN (bit0) excluded */
	{ 0x040, "AFE_DL1_BASE",	0xfffffff8 },
	{ 0x048, "AFE_DL1_END",		0xfffffff8 },
	{ 0x050, "AFE_DL2_BASE",	0xfffffff8 },
	{ 0x058, "AFE_DL2_END",		0xfffffff8 },
	{ 0x070, "AFE_AWB_BASE",	0xfffffff8 },
	{ 0x078, "AFE_AWB_END",		0xfffffff8 },
	{ 0x3d8, "AFE_MEMIF_PBUF_SIZE",	0x00ff00ff },
	{ 0x410, "AFE_GAIN1_CON0",	0x0000ffff },
	{ 0x414, "AFE_GAIN1_CON1",	0x000fffff },
	{ 0x418, "AFE_GAIN1_CON2",	0x000fffff },
	{ 0x41c, "AFE_GAIN1_CON3",	0x000fffff },
	{ 0x420, "AFE_GAIN1_CONN",	0x03ffffff },
	{ 0x424, "AFE_GAIN1_CUR",	0x000fffff },
	{ 0x43c, "AFE_GAIN2_CUR",	0x000fffff },
	{ 0x428, "AFE_GAIN2_CON0",	0x0000ffff },
	{ 0x42c, "AFE_GAIN2_CON1",	0x000fffff },
	{ 0x430, "AFE_GAIN2_CON2",	0x000fffff },
	{ 0x434, "AFE_GAIN2_CON3",	0x000fffff },
	{ 0x438, "AFE_GAIN2_CONN",	0xffffffff },
	{ 0x440, "AFE_GAIN2_CONN2",	0x0000007f },
	/* candidates defined only by later/related MediaTek sources (hypotheses) */
	{ 0x05c, "CAND_AFE_CONN5",	0x0000ffff },
	{ 0x06c, "CAND_AFE_CONN_24BIT",	0x0007ffff },
	{ 0x0bc, "CAND_AFE_CONN6",	0x0000ffff },
	{ 0x0cc, "CAND_AFE_MEMIF_MSB",	0x00ffffff },
	{ 0x340, "CAND_AFE_DL1_D2_BASE", 0x0000fff8 },
	{ 0x348, "CAND_AFE_DL1_D2_END",	0x0000fff8 },
	{ 0x350, "CAND_AFE_VUL_D2_BASE", 0x0000fff8 },
	{ 0x358, "CAND_AFE_VUL_D2_END",	0x0000fff8 },
	{ 0x370, "CAND_AFE_HDMI_OUT_CON0", 0xfffffffe }, /* enable bit excluded */
	{ 0x374, "CAND_AFE_HDMI_BASE",	0x0000fff8 },
	{ 0x37c, "CAND_AFE_HDMI_END",	0x0000fff8 },
	{ 0x390, "CAND_AFE_HDMI_CONN0",	0x0000ffff },
	{ 0x3d0, "AFE_MEMIF_MINLEN",	0x77777777 },
	{ 0x3d4, "AFE_MEMIF_MAXLEN",	0x77777777 },
};

static struct device *afe;
static void __iomem *base;
static struct dentry *dir;
static struct debugfs_blob_wrapper sla_blob;
static DEFINE_MUTEX(lock);
static bool held;
static void __iomem *gpio_din;
static u16 *sla_buf;
static unsigned int sla_n;
static u64 sla_ns;
static char *out, *logbuf;
static size_t out_len, log_len;

static struct {
	void *area;
	dma_addr_t addr;
	size_t bytes;
} slot[SLOTS];

/* first-touch snapshot, in touch order for reverse restore */
static struct { u16 off; u32 orig; } touched[ARRAY_SIZE(wl)];
static unsigned int ntouched;

static __printf(1, 2) void emit(const char *fmt, ...)
{
	va_list ap;

	if (out_len >= OUT_MAX - 1)
		return;
	va_start(ap, fmt);
	out_len += vscnprintf(out + out_len, OUT_MAX - out_len, fmt, ap);
	va_end(ap);
}

static __printf(1, 2) void jlog(const char *fmt, ...)
{
	va_list ap;

	if (log_len >= LOG_MAX - 1)
		return;
	va_start(ap, fmt);
	log_len += vscnprintf(logbuf + log_len, LOG_MAX - log_len, fmt, ap);
	va_end(ap);
}

static u32 rd(unsigned int off)
{
	return readl(base + off);
}

static const struct wl_entry *find_wl(unsigned long off)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(wl); i++)
		if (wl[i].off == off)
			return &wl[i];
	return NULL;
}

static bool read_ok(unsigned long off)
{
	return off < AFE_WINDOW && !(off & 3);
}

static int need_hold(void)
{
	if (!held) {
		emit("error: need 'hold 1' first (AFE clocks must be on)\n");
		return -EAGAIN;
	}
	return 0;
}

/* Preconditions for any write. Returns 0 if safe. */
static int preconditions(void)
{
	int usage = atomic_read(&afe->power.usage_count);

	if (need_hold())
		return -EAGAIN;
	if (usage != 1) {
		emit("error: refused, AFE has %d runtime-PM references (PCM open?)\n", usage);
		return -EBUSY;
	}
	if (rd(DAC_CON0) & DL1_ON) {
		emit("error: refused, DL1 enabled\n");
		return -EBUSY;
	}
	if (rd(I2S_CON3) & 1) {
		emit("error: refused, external I2S output enabled\n");
		return -EBUSY;
	}
	return 0;
}

static void snap(unsigned int off)
{
	unsigned int i;

	for (i = 0; i < ntouched; i++)
		if (touched[i].off == off)
			return;
	touched[ntouched].off = off;
	touched[ntouched].orig = rd(off);
	ntouched++;
}

struct state { u32 dac0, i2s3, irq, top3; int usage; };

static void get_state(struct state *s)
{
	s->dac0 = rd(DAC_CON0);
	s->i2s3 = rd(I2S_CON3);
	s->irq = rd(IRQ_STATUS);
	s->top3 = rd(TOP_CON3);
	s->usage = atomic_read(&afe->power.usage_count);
}

/* Write masked bits, verify no unexpected side effect; returns readback. */
static int do_write(const struct wl_entry *e, u32 req, const char *kind, u32 *rb_out,
		    bool restore_now, u32 *restored_out, u32 *rrb_out)
{
	struct state a, b;
	u32 orig, val, rb = 0, rrb = 0, restored = 0;
	bool abort = false;
	u64 ts = ktime_get_real_ns();

	snap(e->off);
	orig = rd(e->off);
	val = (orig & ~e->mask) | (req & e->mask);
	get_state(&a);
	writel(val, base + e->off);
	rb = rd(e->off);
	get_state(&b);
	/* side-effect check: enables must not change except in DAC_CON0 itself,
	 * IRQ status must not appear, decode flags must not change. */
	if (e->off != DAC_CON0 && ((a.dac0 ^ b.dac0) & DAC_STATE_MASK))
		abort = true;
	if (e->off == DAC_CON0 && ((a.dac0 ^ b.dac0) & DAC_STATE_MASK & ~e->mask))
		abort = true;
	if (b.irq != a.irq || b.top3 != a.top3)
		abort = true;
	if (abort || restore_now) {
		writel(orig, base + e->off);
		restored = rd(e->off);
		rrb = restored;
		if (abort)
			emit("ABORT: unexpected state change at %s; restored\n", e->name);
	}
	if (rb_out)
		*rb_out = rb;
	if (restored_out)
		*restored_out = restored;
	if (rrb_out)
		*rrb_out = rrb;
	jlog("{\"ts_ns\":%llu,\"kernel\":\"%s\",\"kind\":\"%s\",\"reg\":\"0x%03x\","
	     "\"name\":\"%s\",\"mask\":\"0x%08x\",\"orig\":\"0x%08x\",\"req\":\"0x%08x\","
	     "\"write\":\"0x%08x\",\"readback\":\"0x%08x\",\"restored\":%s,"
	     "\"restore_readback\":\"0x%08x\",\"pm_usage\":%d,\"dac_con0\":\"0x%08x\","
	     "\"i2s_con3\":\"0x%08x\",\"irq_status\":\"0x%08x\",\"top_con3\":\"0x%08x\","
	     "\"irq_delta\":%u,\"abort\":%s}\n",
	     ts, utsname()->release, kind, e->off, e->name, e->mask, orig, req, val, rb,
	     (abort || restore_now) ? "true" : "false", rrb, b.usage, b.dac0, b.i2s3,
	     b.irq, b.top3, b.irq != a.irq, abort ? "true" : "false");
	return abort ? -EIO : 0;
}

static void restore_all(void)
{
	int i;

	for (i = ntouched - 1; i >= 0; i--) {
		const struct wl_entry *e = find_wl(touched[i].off);
		u32 now = rd(touched[i].off), rb;

		writel(touched[i].orig, base + touched[i].off);
		rb = rd(touched[i].off);
		jlog("{\"ts_ns\":%llu,\"kernel\":\"%s\",\"kind\":\"restore_all\",\"reg\":\"0x%03x\","
		     "\"name\":\"%s\",\"before\":\"0x%08x\",\"restored\":\"0x%08x\","
		     "\"restore_readback\":\"0x%08x\",\"ok\":%s}\n",
		     ktime_get_real_ns(), utsname()->release, touched[i].off,
		     e ? e->name : "?", now, touched[i].orig, rb,
		     rb == touched[i].orig ? "true" : "false");
		emit("%03x: %08x -> %08x readback %08x%s\n", touched[i].off, now,
		     touched[i].orig, rb, rb == touched[i].orig ? "" : " MISMATCH");
	}
	ntouched = 0;
}


/* ---------------------------------------------------------------------------
 * i2s_capture: dedicated, bounded, self-restoring serializer-pad experiment.
 * NOT a relaxation of the generic guard: generic writes are still refused while
 * I2S is enabled. This fixed sequence is the only place the lab asserts
 * AFE_I2S_CON3.EN, always at 8 kHz (documented rate code 0), always torn down
 * before the command returns, with every touched register snapshotted and
 * verified after restore. One-shot arm; headphone output is never driven
 * because no ALSA stream is open (DAPM powers the amplifier only for streams).
 * ------------------------------------------------------------------------ */
#define KNOWN_KERNEL "6.18.0-y2linux-platform-v1-baseline-01"
#define CAP_MAX_REGS 24

static bool armed;

static const u16 cap_snapshot[] = {
	0x010, 0x014, 0x020, 0x024, 0x028, 0x02c, 0x030, 0x040, 0x048, 0x04c,
	0x050, 0x058, 0x410, 0x414, 0x418, 0x41c, 0x420, 0x424, 0x428, 0x42c,
	0x430, 0x434, 0x438, 0x43c, 0x440,
};

/* Registers a fixture may program (all also in wl[]): routing and gain only.
 * Memif base/end, rates, DAC_CON0, DAC_CON1 and I2S_CON3 belong to the op. */
static bool cap_fixture_reg_ok(unsigned int off)
{
	switch (off) {
	case 0x020: case 0x024: case 0x028: case 0x02c: case 0x030:
	case 0x410: case 0x414: case 0x418: case 0x41c: case 0x420:
	case 0x424: case 0x428: case 0x42c: case 0x430: case 0x434: case 0x438:
	case 0x43c: case 0x440:
		return true;
	}
	return false;
}

static void i2s_capture(char *line)
{
	struct { u16 off; u32 val; } regs[CAP_MAX_REGS];
	u32 saved[ARRAY_SIZE(cap_snapshot)], pat[64], pat2[64];
	unsigned int src = 0, nsamp = 65536, npat = 0, npat2 = 0, nregs = 0, rate = 0, i;
	unsigned long a, flags;
	u32 dl_base, dl_end, en_bit, rb, cur_dl = 0, mon0 = 0, mon1 = 0;
	u32 irq_after, dac_after, i2s_after;
	bool restore_ok = true, ok = false;
	void *buf = NULL, *buf2 = NULL;
	dma_addr_t addr = 0, addr2 = 0;
	u64 t0 = 0;
	char *tok;
	int ret = 0;

	/* ---- parse (no hardware access) ---- */
	while ((tok = strsep(&line, " \n")) && *tok) {
		if (!strncmp(tok, "src=", 4)) {
			if (kstrtouint(tok + 4, 0, &src) || src < 1 || src > 3)
				goto badarg;
		} else if (!strncmp(tok, "rate=", 5)) {
			/* documented AFE rate codes only: 0=8k 1=11.025k 2=12k 4=16k */
			if (kstrtouint(tok + 5, 0, &rate) || (rate != 0 && rate != 1 && rate != 2 && rate != 4))
				goto badarg;
		} else if (!strncmp(tok, "n=", 2)) {
			if (kstrtouint(tok + 2, 0, &nsamp) || nsamp < 1024 || nsamp > SLA_MAX)
				goto badarg;
		} else if (!strncmp(tok, "pat=", 4)) {
			char *p = tok + 4, *w;

			while ((w = strsep(&p, ",")) && *w && npat < ARRAY_SIZE(pat)) {
				if (kstrtoul(w, 0, &a))
					goto badarg;
				pat[npat++] = a;
			}
			if (!npat || (npat & (npat - 1)))
				goto badarg; /* power of two so the 1024-frame ring wraps cleanly */
		} else if (!strncmp(tok, "pat2=", 5)) {
			char *p = tok + 5, *w;

			while ((w = strsep(&p, ",")) && *w && npat2 < ARRAY_SIZE(pat2)) {
				if (kstrtoul(w, 0, &a))
					goto badarg;
				pat2[npat2++] = a;
			}
			if (!npat2 || (npat2 & (npat2 - 1)))
				goto badarg;
		} else if (!strncmp(tok, "w=", 2)) {
			char *p = tok + 2, *w, *colon;
			unsigned long off, val;

			while ((w = strsep(&p, ",")) && *w && nregs < CAP_MAX_REGS) {
				colon = strchr(w, ':');
				if (!colon)
					goto badarg;
				*colon++ = 0;
				if (kstrtoul(w, 0, &off) || kstrtoul(colon, 0, &val) ||
				    !cap_fixture_reg_ok(off))
					goto badarg;
				regs[nregs].off = off;
				regs[nregs].val = val;
				nregs++;
			}
		} else {
			goto badarg;
		}
	}
	if (!src || !npat || (src == 3 && !npat2) || (src != 3 && npat2))
		goto badarg;

	/* ---- refusal checks (still no hardware access) ---- */
	if (!armed) {
		emit("refused: not armed (use 'arm 1' immediately before)\n");
		return;
	}
	armed = false; /* one-shot */
	if (strcmp(utsname()->release, KNOWN_KERNEL)) {
		emit("refused: unknown kernel %s\n", utsname()->release);
		return;
	}
	if (held || ntouched) {
		emit("refused: another lab experiment is active (hold/touched)\n");
		return;
	}
	if (atomic_read(&afe->power.usage_count) != 0 || !pm_runtime_suspended(afe)) {
		emit("refused: AFE not idle (normal stream/PM reference present)\n");
		return;
	}
	ret = pm_runtime_resume_and_get(afe);
	if (ret < 0) {
		emit("refused: AFE clocks %d\n", ret);
		return;
	}
	if (atomic_read(&afe->power.usage_count) != 1 ||
	    (rd(DAC_CON0) & 0x5f) != 0x01 || (rd(I2S_CON3) & 1)) {
		emit("refused: AFE not in expected idle state (DAC_CON0=%08x I2S_CON3=%08x)\n",
		     rd(DAC_CON0), rd(I2S_CON3));
		pm_runtime_put(afe);
		return;
	}
	for (i = 0; i < nregs; i++) {
		const struct wl_entry *e = find_wl(regs[i].off);
		u32 cur = rd(regs[i].off);

		if (!e || ((cur ^ regs[i].val) & ~e->mask)) {
			emit("refused: fixture value %03x=%08x outside mask\n", regs[i].off, regs[i].val);
			pm_runtime_put(afe);
			return;
		}
	}
	buf = dma_alloc_coherent(afe, 4096, &addr, GFP_KERNEL);
	if (!buf) {
		emit("error: dma alloc\n");
		pm_runtime_put(afe);
		return;
	}
	for (i = 0; i < 4096 / 4; i++)
		((u32 *)buf)[i] = pat[i % npat];
	if (src == 3) {
		buf2 = dma_alloc_coherent(afe, 4096, &addr2, GFP_KERNEL);
		if (!buf2) {
			emit("error: dma alloc 2\n");
			dma_free_coherent(afe, 4096, buf, addr);
			pm_runtime_put(afe);
			return;
		}
		for (i = 0; i < 4096 / 4; i++)
			((u32 *)buf2)[i] = pat2[i % npat2];
	}
	dl_base = lower_32_bits(addr);
	dl_end = dl_base + 4096 - 1;
	en_bit = src == 1 ? BIT(1) : src == 2 ? BIT(2) : (BIT(1) | BIT(2));

	/* ---- snapshot, program, run, capture ---- */
	for (i = 0; i < ARRAY_SIZE(cap_snapshot); i++)
		saved[i] = rd(cap_snapshot[i]);

	if (src == 1 || src == 3) {
		writel(dl_base, base + 0x040);
		writel(dl_end, base + 0x048);
	}
	if (src == 2) {
		writel(dl_base, base + 0x050);
		writel(dl_end, base + 0x058);
	}
	if (src == 3) {
		writel(lower_32_bits(addr2), base + 0x050);
		writel(lower_32_bits(addr2) + 4096 - 1, base + 0x058);
	}
	/* both memif rates = selected code (default 8 kHz), stereo */
	writel((rd(0x014) & ~(0xfU | 0xf0U | BIT(21) | BIT(22))) | rate | (rate << 4), base + 0x014);
	for (i = 0; i < nregs; i++)
		writel(regs[i].val, base + regs[i].off);
	/* I2S: format I2S (bit3), 32-bit word (bit1), rate code 0 (8 kHz), then enable */
	writel(BIT(3) | BIT(1) | (rate << 8), base + I2S_CON3);
	writel(BIT(3) | BIT(1) | (rate << 8) | 1, base + I2S_CON3);
	writel(rd(DAC_CON0) | en_bit, base + DAC_CON0);
	msleep(8); /* a few frames to settle */

	local_irq_save(flags);
	t0 = ktime_get_ns();
	for (i = 0; i < nsamp; i++)
		sla_buf[i] = readl(gpio_din + 0x20);
	sla_ns = ktime_get_ns() - t0;
	local_irq_restore(flags);
	sla_n = nsamp;
	cur_dl = rd(src == 2 ? 0x054 : 0x044);
	mon0 = rd(0x0d0);
	mon1 = rd(0x0d4);

	/* ---- teardown (always) ---- */
	writel(rd(DAC_CON0) & ~(BIT(1) | BIT(2) | BIT(6)), base + DAC_CON0);
	writel(rd(I2S_CON3) & ~1U, base + I2S_CON3);
	for (i = 0; i < ARRAY_SIZE(cap_snapshot); i++) {
		u32 want = saved[i];

		if (cap_snapshot[i] == 0x010)
			want = (want & ~0x5eU) | 0x01; /* keep AFE_ON, all memif enables off */
		writel(want, base + cap_snapshot[i]);
	}
	for (i = 0; i < ARRAY_SIZE(cap_snapshot); i++) {
		rb = rd(cap_snapshot[i]);
		if (cap_snapshot[i] == 0x010) {
			if ((rb & 0x5f) != (saved[i] & 0x01))
				restore_ok = false;
		} else if (rb != saved[i]) {
			restore_ok = false;
		}
	}
	irq_after = rd(IRQ_STATUS);
	dac_after = rd(DAC_CON0);
	i2s_after = rd(I2S_CON3);
	ok = restore_ok && !(i2s_after & 1) && !(dac_after & 0x5e) && !irq_after;
	dma_free_coherent(afe, 4096, buf, addr);
	if (buf2)
		dma_free_coherent(afe, 4096, buf2, addr2);
	pm_runtime_put(afe);

	{
		unsigned int rise = 0, lr = 0;

		for (i = 1; i < nsamp; i++) {
			if (((sla_buf[i] >> 11) & 1) && !((sla_buf[i - 1] >> 11) & 1))
				rise++;
			if (((sla_buf[i] ^ sla_buf[i - 1]) >> 12) & 1)
				lr++;
		}
		emit("i2s_capture src=%u rate=%u n=%u elapsed_ns=%llu bclk_rising=%u lrck_toggles=%u "
		     "dl_cur=%08x mon0=%08x mon1=%08x restore_ok=%d i2s_off=%d dac_off=%d irq=%08x\n",
		     src, rate, nsamp, sla_ns, rise, lr, cur_dl, mon0, mon1, restore_ok,
		     !(i2s_after & 1), !(dac_after & 0x5e), irq_after);
	}
	jlog("{\"ts_ns\":%llu,\"kernel\":\"%s\",\"kind\":\"i2s_capture\",\"src\":%u,\"rate_code\":%u,\"n\":%u,"
	     "\"npat\":%u,\"npat2\":%u,\"pat0\":\"0x%08x\",\"nregs\":%u,\"elapsed_ns\":%llu,"
	     "\"dl_cur\":\"0x%08x\",\"restore_ok\":%s,\"final_ok\":%s,\"irq_after\":\"0x%08x\","
	     "\"dac_con0_after\":\"0x%08x\",\"i2s_con3_after\":\"0x%08x\"}\n",
	     ktime_get_real_ns(), utsname()->release, src, rate, nsamp, npat, npat2, pat[0], nregs, sla_ns,
	     cur_dl, restore_ok ? "true" : "false", ok ? "true" : "false", irq_after, dac_after,
	     i2s_after);
	for (i = 0; i < nregs; i++)
		jlog("{\"kind\":\"i2s_capture_fixture_reg\",\"reg\":\"0x%03x\",\"value\":\"0x%08x\"}\n",
		     regs[i].off, regs[i].val);
	return;
badarg:
	emit("usage: i2s_capture src=1|2|3 [rate=0|1|2|4] [n=<samples>] pat=<w0,..(pow2)> [pat2=<..> for src=3] "
	     "[w=<off>:<val>,...]\n");
}

static void do_cmd(char *line)
{
	char *tok = strsep(&line, " \t\n");
	char *s1, *s2;
	unsigned long a, b;
	unsigned int n, i;
	const struct wl_entry *e;

	if (!tok || !*tok)
		return;
	if (!strcmp(tok, "i2s_capture")) {
		i2s_capture(line);
		return;
	}
	s1 = strsep(&line, " \n");
	s2 = strsep(&line, " \n");

	if (!strcmp(tok, "arm")) {
		if (!s1 || kstrtoul(s1, 0, &a))
			goto usage;
		armed = !!a;
		emit("armed=%d (one-shot, consumed by i2s_capture)\n", armed);
	} else if (!strcmp(tok, "hold")) {
		int ret;

		if (!s1 || kstrtoul(s1, 0, &a))
			goto usage;
		if (a && !held) {
			ret = pm_runtime_resume_and_get(afe);
			if (ret < 0) {
				emit("error: resume %d\n", ret);
				return;
			}
			held = true;
		} else if (!a && held) {
			if (ntouched)
				restore_all();
			pm_runtime_put(afe);
			held = false;
		}
		emit("hold=%d usage=%d\n", held, atomic_read(&afe->power.usage_count));
	} else if (!strcmp(tok, "r")) {
		if (!s1 || kstrtoul(s1, 0, &a) || !read_ok(a))
			goto usage;
		n = 1;
		if (s2 && !kstrtoul(s2, 0, &b) && b && b < 256)
			n = b;
		if (need_hold())
			return;
		for (i = 0; i < n && read_ok(a + 4 * i); i++)
			emit("%04lx=%08x\n", a + 4 * i, rd(a + 4 * i));
	} else if (!strcmp(tok, "dump")) {
		if (!s1 || !s2 || kstrtoul(s1, 0, &a) || kstrtoul(s2, 0, &b) ||
		    !read_ok(a) || !read_ok(b) || b < a)
			goto usage;
		if (need_hold())
			return;
		for (; a <= b; a += 4)
			emit("%04lx=%08x\n", a, rd(a));
	} else if (!strcmp(tok, "wl")) {
		for (i = 0; i < ARRAY_SIZE(wl); i++)
			emit("%03x %s mask=%08x\n", wl[i].off, wl[i].name, wl[i].mask);
	} else if (!strcmp(tok, "state")) {
		struct state s;

		if (need_hold())
			return;
		get_state(&s);
		emit("dac_con0=%08x i2s_con3=%08x irq=%08x top_con3=%08x usage=%d touched=%u\n",
		     s.dac0, s.i2s3, s.irq, s.top3, s.usage, ntouched);
	} else if (!strcmp(tok, "probe_bits")) {
		/* Single-bit toggle of each mask bit, restoring after every bit. */
		u32 toggled = 0, orig, rb, bit;

		if (!s1 || kstrtoul(s1, 0, &a))
			goto usage;
		e = find_wl(a);
		if (!e) {
			emit("error: 0x%lx not whitelisted\n", a);
			return;
		}
		if (preconditions())
			return;
		snap(e->off);
		orig = rd(e->off);
		for (bit = 1; bit; bit <<= 1) {
			u32 restored, rrb;
			int ret;

			if (!(e->mask & bit))
				continue;
			ret = do_write(e, orig ^ bit, "probe_bit", &rb, true, &restored, &rrb);
			if (ret || restored != orig) {
				emit("ABORT at bit 0x%08x (ret=%d restored=%08x)\n", bit, ret, restored);
				break;
			}
			if ((rb ^ orig) & bit)
				toggled |= bit;
		}
		emit("%03x %s orig=%08x mask=%08x toggled=%08x fixed=%08x now=%08x\n", e->off,
		     e->name, orig, e->mask, toggled, e->mask & ~toggled, rd(e->off));
	} else if (!strcmp(tok, "probe")) {
		u32 rb, restored, rrb;

		if (!s1 || !s2 || kstrtoul(s1, 0, &a) || kstrtoul(s2, 0, &b))
			goto usage;
		e = find_wl(a);
		if (!e) {
			emit("error: 0x%lx not whitelisted\n", a);
			return;
		}
		if (preconditions())
			return;
		do_write(e, b, "probe", &rb, true, &restored, &rrb);
		emit("%03x %s req=%08lx readback=%08x restored=%08x\n", e->off, e->name, b, rb,
		     rrb);
	} else if (!strcmp(tok, "w")) {
		u32 rb;
		u32 cur;

		if (!s1 || !s2 || kstrtoul(s1, 0, &a) || kstrtoul(s2, 0, &b))
			goto usage;
		e = find_wl(a);
		if (!e) {
			emit("error: 0x%lx not whitelisted\n", a);
			return;
		}
		if (preconditions())
			return;
		cur = rd(e->off);
		if ((cur ^ b) & ~e->mask) {
			emit("error: value touches bits outside mask %08x (cur %08x)\n", e->mask, cur);
			return;
		}
		do_write(e, b, "exp_write", &rb, false, NULL, NULL);
		emit("%03x %s <= %08lx readback %08x\n", e->off, e->name, b, rb);
	} else if (!strcmp(tok, "sla")) {
		/* Read-only pad sampler: poll GPIO32-47 DIN (I2S pads are 43/44/46). */
		unsigned long flags, k;
		u64 t0;

		if (!s1 || kstrtoul(s1, 0, &a) || !a || a > SLA_MAX)
			goto usage;
		local_irq_save(flags);
		t0 = ktime_get_ns();
		for (k = 0; k < a; k++)
			sla_buf[k] = readl(gpio_din + 0x20);
		sla_ns = ktime_get_ns() - t0;
		local_irq_restore(flags);
		sla_n = a;
		emit("sla n=%lu elapsed_ns=%llu\n", a, sla_ns);
	} else if (!strcmp(tok, "sla_dump")) {
		char *s3 = strsep(&line, " \n");

		if (!s1 || !s2 || kstrtoul(s1, 0, &a) || kstrtoul(s2, 0, &b) || a + b > sla_n || b > 8192)
			goto usage;
		(void)s3;
		for (i = 0; i < b; i++)
			emit("%04x%c", sla_buf[a + i], (i & 15) == 15 ? '\n' : ' ');
		emit("\n");
	} else if (!strcmp(tok, "curtrace")) {
		/* curtrace <off> <count> <usec>: timestamped CUR reads (read-only). */
		char *s3 = strsep(&line, " \n");
		unsigned long usec, cnt;

		if (!s1 || !s2 || !s3 || kstrtoul(s1, 0, &a) || !read_ok(a) ||
		    kstrtoul(s2, 0, &cnt) || kstrtoul(s3, 0, &usec) || !cnt || cnt > 256 || usec > 100000)
			goto usage;
		if (need_hold())
			return;
		for (i = 0; i < cnt; i++) {
			emit("%llu %08x\n", ktime_get_ns(), rd(a));
			udelay(usec);
		}
	} else if (!strcmp(tok, "restore_all")) {
		if (need_hold())
			return;
		restore_all();
		emit("restored\n");
	} else if (!strcmp(tok, "dma_alloc")) {
		if (!s1 || !s2 || kstrtouint(s1, 0, &n) || n >= SLOTS ||
		    kstrtoul(s2, 0, &a) || !a || a > MAX_DMA || (a & 15))
			goto usage;
		if (slot[n].area) {
			emit("error: slot %u in use\n", n);
			return;
		}
		slot[n].area = dma_alloc_coherent(afe, a, &slot[n].addr, GFP_KERNEL);
		if (!slot[n].area) {
			emit("error: alloc\n");
			return;
		}
		slot[n].bytes = a;
		memset(slot[n].area, 0, a);
		emit("slot%u addr=0x%08llx bytes=%lu\n", n, (unsigned long long)slot[n].addr, a);
	} else if (!strcmp(tok, "dma_free")) {
		if (!s1 || kstrtouint(s1, 0, &n) || n >= SLOTS || !slot[n].area)
			goto usage;
		dma_free_coherent(afe, slot[n].bytes, slot[n].area, slot[n].addr);
		slot[n].area = NULL;
		emit("freed slot%u\n", n);
	} else if (!strcmp(tok, "dma_fill32")) {
		/* dma_fill32 <slot> <w0> [w1 ...]: repeat a little-endian word pattern. */
		char *w;
		u32 pat[64];
		unsigned int count = 0;
		size_t off;

		if (!s1 || kstrtouint(s1, 0, &n) || n >= SLOTS || !slot[n].area)
			goto usage;
		w = s2;
		while (w && *w && count < ARRAY_SIZE(pat)) {
			if (kstrtoul(w, 0, &a))
				goto usage;
			pat[count++] = a;
			w = strsep(&line, " \n");
		}
		if (!count)
			goto usage;
		for (off = 0, i = 0; off + 4 <= slot[n].bytes; off += 4, i = (i + 1) % count)
			((u32 *)slot[n].area)[off / 4] = pat[i];
		emit("filled slot%u with %u-word pattern\n", n, count);
	} else if (!strcmp(tok, "dma_zero")) {
		if (!s1 || kstrtouint(s1, 0, &n) || n >= SLOTS || !slot[n].area)
			goto usage;
		memset(slot[n].area, 0, slot[n].bytes);
		emit("zeroed slot%u\n", n);
	} else if (!strcmp(tok, "dma_peek")) {
		char *s3 = strsep(&line, " \n");

		if (!s1 || !s2 || !s3 || kstrtouint(s1, 0, &n) || n >= SLOTS ||
		    !slot[n].area || kstrtoul(s2, 0, &a) || kstrtoul(s3, 0, &b) ||
		    (a & 3) || a + b > slot[n].bytes || b > 8192)
			goto usage;
		for (i = 0; i < b; i += 4)
			emit("%06lx=%08x\n", a + i, ((u32 *)slot[n].area)[(a + i) / 4]);
	} else if (!strcmp(tok, "dma_info")) {
		for (i = 0; i < SLOTS; i++)
			if (slot[i].area)
				emit("slot%u addr=0x%08llx bytes=%zu\n", i,
				     (unsigned long long)slot[i].addr, slot[i].bytes);
	} else {
		goto usage;
	}
	return;
usage:
	emit("usage: hold 0|1 | r <off> [n] | dump <a> <b> | wl | state\n"
	     "       sla <n> | sla_dump <start> <n> | curtrace <off> <count> <usec>\n"
	     "       probe_bits <off> | probe <off> <val> | w <off> <val> | restore_all\n"
	     "       dma_alloc <slot> <bytes> | dma_free <slot> | dma_fill32 <slot> <w...>\n"
	     "       dma_zero <slot> | dma_peek <slot> <off> <len> | dma_info\n");
}

static ssize_t cmd_write(struct file *f, const char __user *ubuf, size_t len, loff_t *pos)
{
	char *kbuf, *p, *next;

	if (len > 2048)
		return -E2BIG;
	kbuf = memdup_user_nul(ubuf, len);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);
	mutex_lock(&lock);
	out_len = 0;
	out[0] = 0;
	for (p = kbuf; p; p = next) {
		next = strchr(p, '\n');
		if (next)
			*next++ = 0;
		do_cmd(p);
	}
	mutex_unlock(&lock);
	kfree(kbuf);
	return len;
}

static ssize_t out_read(struct file *f, char __user *ubuf, size_t len, loff_t *pos)
{
	ssize_t ret;

	mutex_lock(&lock);
	ret = simple_read_from_buffer(ubuf, len, pos, out, out_len);
	mutex_unlock(&lock);
	return ret;
}

static ssize_t log_read(struct file *f, char __user *ubuf, size_t len, loff_t *pos)
{
	ssize_t ret;

	mutex_lock(&lock);
	ret = simple_read_from_buffer(ubuf, len, pos, logbuf, log_len);
	mutex_unlock(&lock);
	return ret;
}

static const struct file_operations cmd_fops = { .owner = THIS_MODULE, .write = cmd_write,
						  .llseek = noop_llseek };
static const struct file_operations out_fops = { .owner = THIS_MODULE, .read = out_read,
						  .llseek = default_llseek };
static const struct file_operations log_fops = { .owner = THIS_MODULE, .read = log_read,
						  .llseek = default_llseek };

static int __init lab_init(void)
{
	struct resource *res;

	afe = bus_find_device_by_name(&platform_bus_type, NULL, "11220000.audio-controller");
	if (!afe || !of_device_is_compatible(afe->of_node, "mediatek,mt6582-afe"))
		return -ENODEV;
	res = platform_get_resource(to_platform_device(afe), IORESOURCE_MEM, 0);
	if (!res || res->start != 0x11220000)
		return -ENODEV;
	out = kzalloc(OUT_MAX, GFP_KERNEL);
	logbuf = kzalloc(LOG_MAX, GFP_KERNEL);
	base = ioremap(res->start, AFE_WINDOW);
	gpio_din = ioremap(GPIO_DIN_PHYS, 0x40);
	sla_buf = vmalloc(SLA_MAX * sizeof(*sla_buf));
	if (!out || !logbuf || !base || !gpio_din || !sla_buf) {
		kfree(out);
		kfree(logbuf);
		return -ENOMEM;
	}
	dir = debugfs_create_dir("y2_afe_lab", NULL);
	debugfs_create_file("cmd", 0200, dir, NULL, &cmd_fops);
	debugfs_create_file("out", 0400, dir, NULL, &out_fops);
	debugfs_create_file("log", 0400, dir, NULL, &log_fops);
	sla_blob.data = sla_buf;
	sla_blob.size = SLA_MAX * sizeof(*sla_buf);
	debugfs_create_blob("sla.bin", 0400, dir, &sla_blob);
	return 0;
}
module_init(lab_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Temporary Y2 MT6582 AFE whitelisted register/DMA laboratory (no unload)");
