// SPDX-License-Identifier: GPL-2.0
/*
 * Adapted from Chris Hendrickson, artificery-dev/linux 53fb57bb99c9.
 * Innioasis Y2 APT32F click-wheel - scroll/rotation input driver.
 *
 * The APT32F touch-wheel controller (i2c @0x51) reports its 5 nav zones as
 * dedicated GPIO/EINT lines (handled by gpio-keys); wheel ROTATION is reported
 * over i2c instead. The controller pulses a data-ready line (GPIO/EINT 55) when
 * a new frame is ready; the frame lives in the chip's register file and is read
 * back starting at register 0:
 *
 *     reg0=0xAA reg1=0x55(valid) reg2=class reg3=nav-idx reg4=scroll-code ...
 *
 * We only handle class 3 (wheel scroll); reg4 selects the direction. Frame
 * layout + the data-ready line were reverse-engineered from the vendor kernel.
 */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>

#define APT32F_FRAME_LEN 9
#define APT32F_VALID 0x55     /* reg1 */
#define APT32F_CLASS_SCROLL 3 /* reg2 */

struct apt32f_wheel {
	struct i2c_client *client;
	struct input_dev *input;
	struct gpio_desc *reset; /* GPIO56, active low; NULL = no reset owner */
	int version;		 /* reg6 at probe, <0 = read error */
	bool sleep_enabled;	 /* owner switch, default on (stock behaviour) */
	enum { APT32F_AWAKE, APT32F_I2C_SLEEP, APT32F_RESET_HELD } state;
	unsigned sleeps, wakes, wake_retries, failures;
	int last_error;
};

/* reg4 scroll-code -> key. Tap = press+release, like a scroll wheel notch. */
static const unsigned short apt32f_scroll_keys[] = {
    [1] = KEY_UP,
    [2] = KEY_PAGEUP,
    [3] = KEY_DOWN,
    [4] = KEY_PAGEDOWN,
};

static irqreturn_t apt32f_irq(int irq, void *dev_id)
{
	struct apt32f_wheel *w = dev_id;
	u8 reg = 0;
	u8 f[APT32F_FRAME_LEN];
	struct i2c_msg msgs[2] = {
	    {.addr = w->client->addr, .flags = 0, .len = 1, .buf = &reg},
	    {.addr = w->client->addr, .flags = I2C_M_RD, .len = APT32F_FRAME_LEN, .buf = f},
	};
	unsigned short key;

	/*
	 * Read the frame aligned to reg0: write the register pointer (0) then
	 * repeated-start read the register file. A plain read starts at the
	 * chip's wandering internal pointer and mostly misses the AA/55 header.
	 */
	int ret = i2c_transfer(w->client->adapter, msgs, 2);
	if (ret != 2) {
		dev_warn_ratelimited(&w->client->dev, "frame transfer: %d\n", ret < 0 ? ret : -EIO);
		return IRQ_HANDLED;
	}

	if (f[0] != 0xaa || f[1] != APT32F_VALID || f[2] != APT32F_CLASS_SCROLL)
		return IRQ_HANDLED;

	if (f[4] >= ARRAY_SIZE(apt32f_scroll_keys))
		return IRQ_HANDLED;

	key = apt32f_scroll_keys[f[4]];
	if (!key)
		return IRQ_HANDLED;

	input_report_key(w->input, key, 1);
	input_sync(w->input);
	input_report_key(w->input, key, 0);
	input_sync(w->input);

	return IRQ_HANDLED;
}


/*
 * Stock tpd_suspend/tpd_resume (Y2 FM kernel 0xc04a41c0 / 0xc04a3088):
 * suspend reads reg6; firmware 1..19 gets the sleep command reg5=1, any
 * other version is held in reset (GPIO56 low). Resume always pulses reset
 * (high, 10 ms, low, 20 ms, high) and then writes reg5=0. The controller
 * otherwise keeps scanning the touch ring during system sleep.
 */
#define APT32F_REG_MODE 5
#define APT32F_REG_VERSION 6
static bool apt32f_i2c_sleep_capable(int version)
{
	return version >= 1 && version <= 19;
}
static int apt32f_enter_sleep(struct apt32f_wheel *w)
{
	int ret = 0;
	w->state = APT32F_AWAKE;
	if (!w->sleep_enabled)
		return 0;
	if (apt32f_i2c_sleep_capable(w->version)) {
		ret = i2c_smbus_write_byte_data(w->client, APT32F_REG_MODE, 1);
		if (!ret)
			w->state = APT32F_I2C_SLEEP;
	} else if (w->reset) {
		ret = gpiod_set_value_cansleep(w->reset, 1);
		if (!ret)
			w->state = APT32F_RESET_HELD;
	}
	if (ret) {
		w->failures++;
		w->last_error = ret;
		return ret;
	}
	if (w->state != APT32F_AWAKE)
		w->sleeps++;
	return 0;
}
static int apt32f_leave_sleep(struct apt32f_wheel *w)
{
	unsigned attempt;
	int ret = 0;
	if (w->state == APT32F_AWAKE)
		return 0;
	if (w->reset) {
		gpiod_set_value_cansleep(w->reset, 0);
		msleep(10);
		gpiod_set_value_cansleep(w->reset, 1);
		msleep(20);
		gpiod_set_value_cansleep(w->reset, 0);
	}
	/* Stock writes once right after reset release and ignores the result;
	 * the controller may still be booting, so allow a bounded retry. */
	for (attempt = 0; attempt < 5; attempt++) {
		ret = i2c_smbus_write_byte_data(w->client, APT32F_REG_MODE, 0);
		if (!ret)
			break;
		w->wake_retries++;
		msleep(20);
	}
	w->state = APT32F_AWAKE;
	if (ret) {
		w->failures++;
		w->last_error = ret;
		return ret;
	}
	w->wakes++;
	return 0;
}

static int apt32f_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct apt32f_wheel *w;
	int i, ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;

	if (client->irq <= 0)
		return dev_err_probe(dev, -EINVAL, "no data-ready irq\n");

	w = devm_kzalloc(dev, sizeof(*w), GFP_KERNEL);
	if (!w)
		return -ENOMEM;
	w->client = client;
	w->sleep_enabled = true;
	w->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(w->reset))
		return dev_err_probe(dev, PTR_ERR(w->reset), "reset gpio\n");
	w->version = i2c_smbus_read_byte_data(client, APT32F_REG_VERSION);
	i2c_set_clientdata(client, w);

	w->input = devm_input_allocate_device(dev);
	if (!w->input)
		return -ENOMEM;

	w->input->name = "APT32F click-wheel";
	w->input->id.bustype = BUS_I2C;
	w->input->dev.parent = dev;
	for (i = 0; i < ARRAY_SIZE(apt32f_scroll_keys); i++)
		if (apt32f_scroll_keys[i])
			input_set_capability(w->input, EV_KEY, apt32f_scroll_keys[i]);

	ret = input_register_device(w->input);
	if (ret)
		return ret;

	ret = devm_request_threaded_irq(dev, client->irq, NULL, apt32f_irq, IRQF_ONESHOT,
					"apt32f-wheel", w);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request irq\n");

	dev_info(dev, "firmware %d: suspend uses %s\n", w->version,
		 apt32f_i2c_sleep_capable(w->version) ? "sleep command" :
		 w->reset ? "reset hold" : "nothing");
	return 0;
}

static const struct of_device_id apt32f_of_match[] = {
    {.compatible = "innioasis,apt32f-wheel"},
    {},
};
MODULE_DEVICE_TABLE(of, apt32f_of_match);

/* Wheel is not a wake source. Drain the threaded transfer before the I2C
 * adapter suspends; resume the IRQ only after the parent adapter is ready.
 * The controller sleeps like stock; a failed sleep command never blocks
 * system sleep (it only costs power) and is counted. */
static int apt32f_suspend(struct device *dev)
{
	struct apt32f_wheel *w = i2c_get_clientdata(to_i2c_client(dev));
	disable_irq(to_i2c_client(dev)->irq);
	apt32f_enter_sleep(w);
	return 0;
}
static int apt32f_resume(struct device *dev)
{
	struct apt32f_wheel *w = i2c_get_clientdata(to_i2c_client(dev));
	if (apt32f_leave_sleep(w))
		dev_warn(dev, "wake: %d after %u retries\n", w->last_error, w->wake_retries);
	enable_irq(to_i2c_client(dev)->irq);
	return 0;
}
static ssize_t sleep_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct apt32f_wheel *w = i2c_get_clientdata(to_i2c_client(dev));
	return sysfs_emit(buf, "enabled=%u firmware=%d method=%s sleeps=%u wakes=%u wake_retries=%u failures=%u last_error=%d\n",
			  w->sleep_enabled, w->version,
			  apt32f_i2c_sleep_capable(w->version) ? "sleep_command" :
			  w->reset ? "reset_hold" : "none",
			  w->sleeps, w->wakes, w->wake_retries, w->failures, w->last_error);
}
static ssize_t sleep_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct apt32f_wheel *w = i2c_get_clientdata(to_i2c_client(dev));
	bool value;
	int ret = kstrtobool(buf, &value);
	if (ret)
		return ret;
	w->sleep_enabled = value;
	return count;
}
static DEVICE_ATTR_RW(sleep);
static struct attribute *apt32f_attrs[] = {&dev_attr_sleep.attr, NULL};
ATTRIBUTE_GROUPS(apt32f);
static DEFINE_SIMPLE_DEV_PM_OPS(apt32f_pm, apt32f_suspend, apt32f_resume);

static struct i2c_driver apt32f_driver = {
    .probe = apt32f_probe,
    .driver =
	{
	    .name = "apt32f-wheel",
	    .of_match_table = apt32f_of_match,
	    .pm = pm_sleep_ptr(&apt32f_pm),
	    .dev_groups = apt32f_groups,
	},
};
module_i2c_driver(apt32f_driver);

MODULE_DESCRIPTION("Innioasis Y2 APT32F click-wheel scroll driver");
MODULE_LICENSE("GPL");
