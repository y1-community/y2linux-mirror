// SPDX-License-Identifier: GPL-2.0-only
/* Temporary read-only observer. Build against the installed kernel/config.
 * No raw MMIO, register writes, clock changes, or wakeups of suspended devices.
 * The fixed allowlists come from MT6582 BSP and upstream CS43130/31 sources.
 */
#include <linux/debugfs.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>
#include <sound/pcm.h>
#include <sound/soc.h>

static struct dentry *directory;
static struct device *machine, *afe, *dac;

struct observed_register {
	const char *name;
	unsigned int address;
};

static const struct observed_register afe_registers[] = {
	{ "AFE_DAC_CON0", 0x0010 }, { "AFE_DAC_CON1", 0x0014 },
	{ "AFE_CONN0", 0x0020 }, { "AFE_DL1_BASE", 0x0040 },
	{ "AFE_DL1_CUR", 0x0044 }, { "AFE_DL1_END", 0x0048 },
	{ "AFE_I2S_CON3", 0x004c }, { "AFE_MEMIF_PBUF_SIZE", 0x03d8 },
	{ "AFE_MEMIF_MON0", 0x00d0 },
};
static const struct observed_register dac_registers[] = {
	{ "DEVID_AB", 0x010000 }, { "DEVID_CD", 0x010001 },
	{ "DEVID_E", 0x010002 }, { "SP_SRATE", 0x01000b },
	{ "SP_BITSIZE", 0x01000c }, { "ASP_LRCK_HI_TIME_1", 0x040014 },
	{ "ASP_LRCK_HI_TIME_2", 0x040015 },
	{ "ASP_LRCK_PERIOD_1", 0x040016 },
	{ "ASP_LRCK_PERIOD_2", 0x040017 }, { "ASP_CLOCK_CONF", 0x040018 },
	{ "ASP_FRAME_CONF", 0x040019 }, { "ASP_CH_1_LOC", 0x050000 },
	{ "ASP_CH_2_LOC", 0x050001 }, { "ASP_CH_1_SZ_EN", 0x05000a },
	{ "ASP_CH_2_SZ_EN", 0x05000b },
};

static void observe_registers(struct seq_file *s, struct device *dev,
			      const struct observed_register *regs, size_t count)
{
	struct regmap *map;
	unsigned int value;
	int active, ret;
	size_t i;

	device_lock(dev);
	map = dev->driver ? dev_get_regmap(dev, NULL) : NULL;
	active = map ? pm_runtime_get_if_in_use(dev) : 0;
	seq_printf(s, "device=%s active_reference=%d\n", dev_name(dev), active);
	if (active > 0) {
		for (i = 0; i < count; i++) {
			/* Bypass the cache: these are physical readbacks while powered.
			 * All listed registers are ordinary non-destructive reads. */
			ret = regmap_read_bypassed(map, regs[i].address, &value);
			if (ret)
				seq_printf(s, "%s read_error=%d\n", regs[i].name, ret);
			else
				seq_printf(s, "%s=0x%08x\n", regs[i].name, value);
		}
		pm_runtime_put(dev);
	} else {
		seq_puts(s, "registers=SKIPPED_NO_ACTIVE_REFERENCE\n");
	}
	device_unlock(dev);
}

static bool observe_pcm(struct seq_file *s, struct snd_pcm *pcm)
{
	struct snd_pcm_substream *sub;
	struct snd_pcm_runtime *rt;
	unsigned long flags;
	u8 bytes[256];
	size_t size = 0, dma_bytes = 0, i;
	unsigned int rate = 0, channels = 0, sample_bits = 0, frame_bits = 0;
	snd_pcm_uframes_t period = 0, buffer = 0;
	snd_pcm_format_t format = SNDRV_PCM_FORMAT_S16_LE;
	int state = SNDRV_PCM_STATE_OPEN;
	bool running = false;

	/* open_mutex protects runtime lifetime; buffer_mutex protects allocation.
	 * This is an instantaneous ring snapshot, not a synchronized DMA capture.
	 * Userspace mmap/writes can change bytes; use a repeating fixture. */
	mutex_lock(&pcm->open_mutex);
	sub = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
	rt = sub ? sub->runtime : NULL;
	if (rt) {
		mutex_lock(&rt->buffer_mutex);
		snd_pcm_stream_lock_irqsave(sub, flags);
		state = rt->state;
		running = state == SNDRV_PCM_STATE_RUNNING;
		format = rt->format;
		rate = rt->rate;
		channels = rt->channels;
		sample_bits = rt->sample_bits;
		frame_bits = rt->frame_bits;
		period = rt->period_size;
		buffer = rt->buffer_size;
		dma_bytes = rt->dma_bytes;
		if (rt->dma_area && dma_bytes) {
			size = min(dma_bytes, sizeof(bytes));
			memcpy(bytes, rt->dma_area, size);
		}
		snd_pcm_stream_unlock_irqrestore(sub, flags);
		mutex_unlock(&rt->buffer_mutex);
	}
	seq_printf(s, "pcm=%s runtime=%s state=%d format=%s rate=%u channels=%u "
		   "sample_bits=%u frame_bits=%u period_frames=%lu buffer_frames=%lu "
		   "dma_bytes=%zu snapshot_offset=0 snapshot_bytes=%zu\n",
		   pcm->id, rt ? "present" : "closed", state,
		   rt ? snd_pcm_format_name(format) : "NONE", rate, channels, sample_bits, frame_bits,
		   period, buffer, dma_bytes, size);
	mutex_unlock(&pcm->open_mutex);
	if (size) {
		seq_puts(s, "dma_hex=");
		for (i = 0; i < size; i++)
			seq_printf(s, "%02x", bytes[i]);
		seq_putc(s, '\n');
	}
	return running;
}

static int highres_show(struct seq_file *s, void *unused)
{
	struct snd_soc_card *card;
	struct snd_soc_pcm_runtime *rtd;
	bool running = false;

	seq_puts(s, "observer_version=1 read_only=true physical_serial_capture=false\n");
	seq_puts(s, "source_contract=MT6582_Functional_Specification_v0.91_ch53 "
		 "dl1_monitor_bits_per_channel=16 "
		 "installed_dl1_fetch_format=packed_le_s16_stereo "
		 "pbuf_bits_19_16=DL_ULTRA_TH_not_HD "
		 "wide_pcm_contract=NOT_ESTABLISHED silicon_precision_ceiling=UNPROVEN\n");
	device_lock(machine);
	card = machine->driver ? dev_get_drvdata(machine) : NULL;
	if (!card || strcmp(card->name, "Y2Audio")) {
		device_unlock(machine);
		return -ENODEV;
	}
	mutex_lock(&card->mutex);
	for_each_card_rtds(card, rtd) {
		if (rtd->pcm && !rtd->dai_link->no_pcm)
			running |= observe_pcm(s, rtd->pcm);
	}
	mutex_unlock(&card->mutex);
	device_unlock(machine);
	if (running) {
		observe_registers(s, afe, afe_registers, ARRAY_SIZE(afe_registers));
		observe_registers(s, dac, dac_registers, ARRAY_SIZE(dac_registers));
	} else {
		seq_puts(s, "registers=SKIPPED_PCM_NOT_RUNNING\n");
	}
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(highres);

static int __init observer_init(void)
{
	int ret = -ENODEV;

	machine = bus_find_device_by_name(&platform_bus_type, NULL, "sound");
	afe = bus_find_device_by_name(&platform_bus_type, NULL, "11220000.audio-controller");
	dac = bus_find_device_by_name(&i2c_bus_type, NULL, "1-0030");
	if (!machine || !afe || !dac ||
	    !of_device_is_compatible(machine->of_node, "innioasis,y2-audio") ||
	    !of_device_is_compatible(afe->of_node, "mediatek,mt6582-afe") ||
	    !of_device_is_compatible(dac->of_node, "cirrus,cs43131"))
		goto release;
	directory = debugfs_create_dir("y2_audio_observer", NULL);
	if (IS_ERR(directory)) {
		ret = PTR_ERR(directory);
		goto release;
	}
	debugfs_create_file("highres", 0400, directory, NULL, &highres_fops);
	return 0;
release:
	put_device(dac);
	put_device(afe);
	put_device(machine);
	return ret;
}

static void __exit observer_exit(void)
{
	debugfs_remove_recursive(directory);
	put_device(dac);
	put_device(afe);
	put_device(machine);
}
module_init(observer_init);
module_exit(observer_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Y2 wired audio read-only DMA and physical register observer");
