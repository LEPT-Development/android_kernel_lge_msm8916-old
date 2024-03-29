/* drivers/video/backlight/rt8542.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/gpio.h>
//#include <mach/board.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/leds.h>
#include "msm_led_flash.h"

#ifdef CONFIG_LGE_PM_FACTORY_CABLE
#include <mach/board_lge.h>
#endif

#define RT8542_FLED_EN
#define FLASH_PORTING_TEMP

#if defined(CONFIG_MACH_MSM8916_C30_TRF_US) || defined(CONFIG_MACH_MSM8916_C30C_TRF_US)
//extern int lge_lcd_id;
extern int get_lcd_id(void);
#endif

#ifdef FLASH_PORTING_TEMP
//#undef pr_debug
//#define pr_debug pr_err
#else
//#undef pr_debug
//#define pr_debug pr_err
#endif

#if defined(RT8542_FLED_EN)
#include <mach/board_lge.h>
#endif

#if defined(CONFIG_BL_CURVE)
#include <linux/ctype.h>
#endif

#if defined(RT8542_FLED_EN)
#define I2C_BL_NAME                              "qcom,led-flash"
#else
#define I2C_BL_NAME                              "backlight,rt8542"
#endif

/* Linear BLED Brightness Control - 83%*/
#define MAX_BRIGHTNESS_RT8542                    0x7D
#define MIN_BRIGHTNESS_RT8542                    0x04
#define DEFAULT_BRIGHTNESS                       0x66
#define DEFAULT_FTM_BRIGHTNESS                   0x02
#define UI_MAX_BRIGHTNESS                        0xFF

#define POWER_OFF		0x00
#ifdef FLASH_PORTING_TEMP
#define BOTH_ON			0xFF
#else
#define POWER_ON_TEST	0xFF
#endif
#define BL_ON			0xF0
#define FLASH_ON		0x0F

#define BOOT_BRIGHTNESS 1

#if defined(RT8542_FLED_EN)
static struct msm_camera_i2c_client rt8542_flash_i2c_client = {
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
};
#endif

static struct i2c_client *rt8542_i2c_client;

static int store_level_used;

struct backlight_platform_data {
	void (*platform_init)(void);
	int gpio;
	unsigned int mode;
	int max_current;
	int init_on_boot;
	int min_brightness;
	int max_brightness;
	int default_brightness;
	int factory_brightness;
	int blmap_size;
	char *blmap;
};

struct rt8542_device {
	struct i2c_client *client;
	struct backlight_device *bl_dev;
	int gpio;
	int max_current;
	int min_brightness;
	int max_brightness;
	int default_brightness;
	int factory_brightness;
	struct mutex bl_mutex;
	int blmap_size;
	char *blmap;
};
#if defined(RT8542_FLED_EN)
static struct msm_led_flash_ctrl_t fctrl;

static const struct i2c_device_id rt8542_bl_id[] = {
	{ I2C_BL_NAME, (kernel_ulong_t)&fctrl},
	{ },
};
#else
static const struct i2c_device_id rt8542_bl_id[] = {
	{ I2C_BL_NAME, 0},
	{ },
};
#endif


static int rt8542_read_reg(struct i2c_client *client, u8 reg, u8 *buf);
static int rt8542_write_reg(struct i2c_client *client,
		unsigned char reg, unsigned char val);

static int cur_main_lcd_level = DEFAULT_BRIGHTNESS;
static int saved_main_lcd_level = DEFAULT_BRIGHTNESS;

static int backlight_status = POWER_OFF;
static int rt8542_pwm_enable;
static struct rt8542_device *main_rt8542_dev;

#ifdef CONFIG_LGE_WIRELESS_CHARGER
int wireless_backlight_state(void)
{
	return backlight_status;
}
EXPORT_SYMBOL(wireless_backlight_state);
#endif

//                                                                     
int get_backlight_status(void) {
	return backlight_status;
}
///

static void rt8542_hw_reset(void)
{
	int gpio = main_rt8542_dev->gpio;
	/*            */
	if (gpio_is_valid(gpio)) {
		gpio_direction_output(gpio, 1);
		gpio_set_value_cansleep(gpio, 1);
		mdelay(2);
	} else
		pr_err("%s: gpio is not valid !!\n", __func__);

}

static int rt8542_read_reg(struct i2c_client *client, u8 reg, u8 *buf)
{
	s32 ret;

	pr_debug("%s: reg: %x\n", __func__, reg);

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0)
		pr_err("%s: i2c_smbus_read_byte_data() error\n", __func__);

	*buf = ret;
	return 0;

}

static int rt8542_write_reg(struct i2c_client *client,
		unsigned char reg, unsigned char val)
{
	int err;
	u8 buf[2];
	struct i2c_msg msg = {
		client->addr, 0, 2, buf
	};

	buf[0] = reg;
	buf[1] = val;

	err = i2c_transfer(client->adapter, &msg, 1);
	if (err < 0)
		dev_err(&client->dev, "i2c write error\n");

	return 0;
}

static int exp_min_value = 150;
static int cal_value;
static unsigned char bl_ctrl;

static void rt8542_set_main_current_level(struct i2c_client *client, int level)
{
	struct rt8542_device *dev = i2c_get_clientdata(client);
	int min_brightness = dev->min_brightness;
	int max_brightness = dev->max_brightness;

	if (level == -BOOT_BRIGHTNESS)
		level = dev->default_brightness;

	cur_main_lcd_level = level;
	dev->bl_dev->props.brightness = cur_main_lcd_level;

	store_level_used = 0;

	mutex_lock(&dev->bl_mutex);

#ifdef CONFIG_LGE_PM_FACTORY_CABLE
	if ( ( LGE_BOOT_MODE_QEM_910K == lge_get_boot_mode() || LGE_BOOT_MODE_PIF_910K == lge_get_boot_mode() ) && ( BATT_ID_UNKNOWN == read_lge_battery_id()) ){
			pr_err("Set LCD brightness minimum, No Battery or No proper Battery with 910K Cable\n");
			rt8542_write_reg(client, 0x05, 0x05);
	} else{
		if (level != 0) {
			if (level > 0 && level <= min_brightness)
				level = min_brightness;
			else if (level > max_brightness)
				level = max_brightness;

			if (dev->blmap) {
				if (level < dev->blmap_size) {
					cal_value = dev->blmap[level];
					rt8542_write_reg(client, 0x05, cal_value);
				} else
					dev_warn(&client->dev, "invalid index %d:%d\n",
							dev->blmap_size,
							level);
			} else {
				cal_value = level;
				rt8542_write_reg(client, 0x05, cal_value);
			}
		} else{
			rt8542_write_reg(client, 0x05, 0x00);
			bl_ctrl = 0;
			rt8542_read_reg(main_rt8542_dev->client, 0x0A, &bl_ctrl);
			bl_ctrl &= 0xE6;
			rt8542_write_reg(main_rt8542_dev->client, 0x0A, bl_ctrl);
		}
	}
#else
	if (level != 0) {
		if (level > 0 && level <= min_brightness)
			level = min_brightness;
		else if (level > max_brightness)
			level = max_brightness;

		if (dev->blmap) {
			if (level < dev->blmap_size) {
				cal_value = dev->blmap[level];
				rt8542_write_reg(client, 0x05, cal_value);
			} else
				dev_warn(&client->dev, "invalid index %d:%d\n",
						dev->blmap_size,
						level);
		} else {
			cal_value = level;
			rt8542_write_reg(client, 0x05, cal_value);
		}
	} else{
		rt8542_write_reg(client, 0x05, 0x00);
		bl_ctrl = 0;
		rt8542_read_reg(main_rt8542_dev->client, 0x0A, &bl_ctrl);
		bl_ctrl &= 0xE6;
		rt8542_write_reg(main_rt8542_dev->client, 0x0A, bl_ctrl);
	}
#endif
	mutex_unlock(&dev->bl_mutex);

	pr_debug("%s : backlight level=%d, cal_value=%d\n",
		__func__, level, cal_value);
}

static void rt8542_set_main_current_level_no_mapping(
		struct i2c_client *client, int level)
{
	struct rt8542_device *dev;
	dev = (struct rt8542_device *)i2c_get_clientdata(client);

	if (level > 255)
		level = 255;
	else if (level < 0)
		level = 0;

	cur_main_lcd_level = level;
	dev->bl_dev->props.brightness = cur_main_lcd_level;

	store_level_used = 1;

	mutex_lock(&main_rt8542_dev->bl_mutex);
	if (level != 0)
		rt8542_write_reg(client, 0x03, level);
	else
		rt8542_write_reg(client, 0x00, 0x00);

	mutex_unlock(&main_rt8542_dev->bl_mutex);
}

void rt8542_backlight_on(int level)
{
#ifdef FLASH_PORTING_TEMP
	if ((get_backlight_status() != BL_ON) && (get_backlight_status() != BOTH_ON)){
#else
	if (backlight_status != BL_ON) {
#endif

#if defined(CONFIG_MACH_MSM8916_C30_TRF_US) || defined(CONFIG_MACH_MSM8916_C30C_TRF_US)
		rt8542_hw_reset();

		rt8542_set_main_current_level(main_rt8542_dev->client, level);

		bl_ctrl = 0;
		rt8542_read_reg(main_rt8542_dev->client, 0x0A, &bl_ctrl);

		/* BLED2 disable */
		//bl_ctrl &= 0x77;
		//bl_ctrl |= 0x11;
		rt8542_write_reg(main_rt8542_dev->client, 0x0A, 0x11);

		/*	OVP(32V), MAX BLED(20mA), OCP(1.0A), Boost Frequency(500khz)  Ramp up rate(262.144ms)  Ramp down rate(262.144ms)*/
		rt8542_write_reg(main_rt8542_dev->client, 0x02, 0x54);

		bl_ctrl = 0;
		rt8542_read_reg(main_rt8542_dev->client, 0x03, &bl_ctrl);
		bl_ctrl &= 0x3F;
		rt8542_write_reg(main_rt8542_dev->client, 0x03, bl_ctrl);
#else
		rt8542_hw_reset();
		rt8542_write_reg(main_rt8542_dev->client, 0x05, 0x04);

#ifdef CONFIG_MACH_MSM8916_C90NAS_SPR_US
		/*OVP(32V),MAX BLED(22.3mA),OCP(1.0A),Boost Frequency(500khz)*/
		rt8542_write_reg(main_rt8542_dev->client, 0x02, 0x55);
#else
		/*OVP(32V),MAX BLED(12.1mA),OCP(1.0A),Boost Frequency(500khz)*/
		rt8542_write_reg(main_rt8542_dev->client, 0x02, 0x52);
#endif
		bl_ctrl = 0;
		rt8542_read_reg(main_rt8542_dev->client, 0x0A, &bl_ctrl);
		bl_ctrl |= 0x19;
		rt8542_write_reg(main_rt8542_dev->client, 0x0A, bl_ctrl);
#endif
		pr_info("%s: ON!\n", __func__);
	}

	mdelay(1);
	rt8542_set_main_current_level(main_rt8542_dev->client, level);
	backlight_status |= BL_ON;

	return;
}

void rt8542_backlight_off(void)
{
	int gpio = main_rt8542_dev->gpio;

	if (!(backlight_status & BL_ON))
	{
		return;
	}

	saved_main_lcd_level = cur_main_lcd_level;
	rt8542_set_main_current_level(main_rt8542_dev->client, 0);
	backlight_status &= ~BL_ON;

	if (backlight_status == POWER_OFF) {
		gpio_direction_output(gpio, 0);
		usleep(6*1000);
		pr_err("%s: OFF!\n", __func__);
	}

	return;
}

void rt8542_led_enable(void){

	int gpio = main_rt8542_dev->gpio;
	pr_err(">> %s: START\n", __func__);

	mutex_lock(&main_rt8542_dev->bl_mutex);
	if (gpio_get_value(gpio) != 1) {
		gpio_direction_output(gpio, 1);
		mdelay(10);
		pr_err("%s: ON!\n", __func__);
	}

	backlight_status |= FLASH_ON;
	mutex_unlock(&main_rt8542_dev->bl_mutex);
	pr_err("<< %s: END\n", __func__);
}

void rt8542_led_disable(void){
	int gpio = 0;

	pr_debug(">> %s START", __func__);
	//pr_debug("main_rt8542_dev: %d", (int) main_rt8542_dev);
	if (!main_rt8542_dev) {
		pr_debug("<< %s END : FAIL! (line: %d)", __func__, __LINE__);
		return;
	}

	gpio = main_rt8542_dev->gpio;

	pr_err("%s: Enter\n", __func__);

	mutex_lock(&main_rt8542_dev->bl_mutex);
	backlight_status &= ~FLASH_ON;

	if (backlight_status == POWER_OFF) {
		gpio_direction_output(gpio, 0);
		usleep(6*1000);
		pr_err("%s: OFF!\n", __func__);
	}
	mutex_unlock(&main_rt8542_dev->bl_mutex);
	pr_err("%s: Exit\n", __func__);

	pr_debug("<< %s END", __func__);
}

void rt8542_lcd_backlight_set_level(int level)
{
	unsigned int bright_per = 0;

	if (level > UI_MAX_BRIGHTNESS)
		level = UI_MAX_BRIGHTNESS;

	printk("%s: level = (%d)\n",  __func__, level);

	if (rt8542_i2c_client != NULL) {
		if (level == 0) {
			rt8542_backlight_off();
		} else {
			if(level < MIN_BRIGHTNESS_RT8542)
			{
				level = MIN_BRIGHTNESS_RT8542;
			}
			bright_per = (level / 2) - 1;
			rt8542_backlight_on(bright_per);
		}
	} else {
		pr_err("%s(): No client\n", __func__);
	}
}
EXPORT_SYMBOL(rt8542_lcd_backlight_set_level);

static int bl_set_intensity(struct backlight_device *bd)
{
	struct i2c_client *client = to_i2c_client(bd->dev.parent);

	if (bd->props.brightness == cur_main_lcd_level) {
		pr_debug("%s level is already set. skip it\n", __func__);
		return 0;
	}

	rt8542_set_main_current_level(client, bd->props.brightness);
	cur_main_lcd_level = bd->props.brightness;

	return 0;
}

static int bl_get_intensity(struct backlight_device *bd)
{
	unsigned char val = 0;
	val &= 0x1f;

	return (int)val;
}

static ssize_t lcd_backlight_show_level(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int r = 0;

	if (store_level_used == 0)
		r = snprintf(buf, PAGE_SIZE, "LCD Backlight Level is : %d\n",
				cal_value);
	else if (store_level_used == 1)
		r = snprintf(buf, PAGE_SIZE, "LCD Backlight Level is : %d\n",
				cur_main_lcd_level);

	return r;
}

static ssize_t lcd_backlight_store_level(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int level;
	struct i2c_client *client = to_i2c_client(dev);

	if (!count)
		return -EINVAL;

	level = kstrtoul(buf, 10, NULL);

	rt8542_set_main_current_level_no_mapping(client, level);
	pr_info("%s: write %d direct to backlight register\n", __func__, level);

	return count;
}

#if defined(CONFIG_BL_CURVE)
static ssize_t lcd_backlight_show_blmap(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rt8542_device *rt8542_dev = i2c_get_clientdata(client);
	int i, j;

	if (!rt8542_dev->blmap_size)
		return 0;

	buf[0] = '{';

	for (i = 0, j = 2; i < rt8542_dev->blmap_size && j < PAGE_SIZE; ++i) {
		if (!(i % 15)) {
			buf[j] = '\n';
			++j;
		}

		sprintf(&buf[j], "%d, ", rt8542_dev->blmap[i]);
		if (rt8542_dev->blmap[i] < 10)
			j += 3;
		else if (rt8542_dev->blmap[i] < 100)
			j += 4;
		else
			j += 5;
	}

	buf[j] = '\n';
	++j;
	buf[j] = '}';
	++j;

	return j;
}

static ssize_t lcd_backlight_store_blmap(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rt8542_device *rt8542_dev = i2c_get_clientdata(client);
	static char *blmap;
	int i;
	int j;
	int value;

	if (count < 1)
		return count;

	if (buf[0] != '{')
		return -EINVAL;

	blmap = kzalloc(sizeof(char) * rt8542_dev->blmap_size, GFP_KERNEL);

	for (i = 1, j = 0; i < count && j < rt8542_dev->blmap_size; ++i) {
		if (!isdigit(buf[i]))
			continue;

		sscanf(&buf[i], "%d", &value);
		blmap[j] = (char)value;

		while (isdigit(buf[i]))
			++i;
		++j;
	}

	for (i = 0; i < rt8542_dev->blmap_size; i++)
		rt8542_dev->blmap[i] = blmap[i];

	kfree(blmap);

	return count;
}
#endif

static int rt8542_bl_resume(struct i2c_client *client)
{
	pr_debug("%s\n", __func__);

	rt8542_backlight_on(saved_main_lcd_level);

	return 0;
}

static int rt8542_bl_suspend(struct i2c_client *client, pm_message_t state)
{
	pr_debug("%s: new state: %d\n", __func__, state.event);

	rt8542_backlight_off();

	return 0;
}

static ssize_t lcd_backlight_show_on_off(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int r = 0;

	pr_debug("%s received (prev backlight_status: %s)\n",
		__func__, backlight_status ? "ON" : "OFF");

	return r;
}

static ssize_t lcd_backlight_store_on_off(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int on_off;
	struct i2c_client *client = to_i2c_client(dev);

	if (!count)
		return -EINVAL;

	pr_debug("%s received (prev backlight_status: %s)\n",
		__func__, backlight_status ? "ON" : "OFF");

	on_off = kstrtoul(buf, 10, NULL);

	if (on_off == 1)
		rt8542_bl_resume(client);
	else if (on_off == 0)
		rt8542_bl_suspend(client, PMSG_SUSPEND);

	return count;

}
static ssize_t lcd_backlight_show_exp_min_value(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int r;

	r = snprintf(buf, PAGE_SIZE, "LCD Backlight  : %d\n", exp_min_value);

	return r;
}

static ssize_t lcd_backlight_store_exp_min_value(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int value;

	if (!count)
		return -EINVAL;

	value = kstrtoul(buf, 10, NULL);
	exp_min_value = value;

	return count;
}

#if defined(CONFIG_BACKLIGHT_CABC_DEBUG_ENABLE)
static ssize_t lcd_backlight_show_pwm(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int r;
	u8 level, pwm_low, pwm_high, config;

	mutex_lock(&main_rt8542_dev->bl_mutex);
	rt8542_read_reg(main_rt8542_dev->client, 0x01, &config);
	mdelay(3);
	rt8542_read_reg(main_rt8542_dev->client, 0x03, &level);
	mdelay(3);
	rt8542_read_reg(main_rt8542_dev->client, 0x12, &pwm_low);
	mdelay(3);
	rt8542_read_reg(main_rt8542_dev->client, 0x13, &pwm_high);
	mdelay(3);
	mutex_unlock(&main_rt8542_dev->bl_mutex);

	r = snprintf(buf, PAGE_SIZE, "Show PWM level: %d pwm_low: %d\n",
			level, pwm_low);

	r = snprintf(buf, PAGE_SIZE, "pwm_high: %d config: %d\n",
			pwm_high, config);

	return r;
}
static ssize_t lcd_backlight_store_pwm(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	return count;
}
#endif

DEVICE_ATTR(rt8542_level, 0644, lcd_backlight_show_level,
		lcd_backlight_store_level);
DEVICE_ATTR(rt8542_backlight_on_off, 0644, lcd_backlight_show_on_off,
		lcd_backlight_store_on_off);
DEVICE_ATTR(rt8542_exp_min_value, 0644, lcd_backlight_show_exp_min_value,
		lcd_backlight_store_exp_min_value);
#if defined(CONFIG_BACKLIGHT_CABC_DEBUG_ENABLE)
DEVICE_ATTR(rt8542_pwm, 0644, lcd_backlight_show_pwm, lcd_backlight_store_pwm);
#endif

#if defined(CONFIG_BL_CURVE)
DEVICE_ATTR(i2c_bl_blmap, 0644, lcd_backlight_show_blmap, lcd_backlight_store_blmap);
#endif

#ifdef CONFIG_OF
static int rt8542_parse_dt(struct device *dev,
		struct backlight_platform_data *pdata)
{
	int rc = 0, i;
	u32 *array;
	struct device_node *np = dev->of_node;
	pdata->gpio = of_get_named_gpio_flags(np,
			"rt8542,lcd_bl_en", 0, NULL);
	rc = of_property_read_u32(np, "rt8542,max_current",
			&pdata->max_current);
	rc = of_property_read_u32(np, "rt8542,min_brightness",
			&pdata->min_brightness);
	rc = of_property_read_u32(np, "rt8542,default_brightness",
			&pdata->default_brightness);
	rc = of_property_read_u32(np, "rt8542,factory_brightness",
			&pdata->factory_brightness);
	if (rc)
		pdata->factory_brightness = DEFAULT_FTM_BRIGHTNESS;
	rc = of_property_read_u32(np, "rt8542,max_brightness",
			&pdata->max_brightness);

	rc = of_property_read_u32(np, "rt8542,enable_pwm",
			&rt8542_pwm_enable);
	if (rc == -EINVAL)
		rt8542_pwm_enable = 1;

	rc = of_property_read_u32(np, "rt8542,blmap_size",
			&pdata->blmap_size);

	if (pdata->blmap_size) {
		array = kzalloc(sizeof(u32) * pdata->blmap_size, GFP_KERNEL);
		if (!array)
			return -ENOMEM;

		rc = of_property_read_u32_array(np, "rt8542,blmap",
			array, pdata->blmap_size);

		if (rc) {
			pr_err("%s:%d, uable to read backlight map\n",
				__func__,  __LINE__);
			return -EINVAL;
		}
		pdata->blmap =
			kzalloc(sizeof(char) * pdata->blmap_size, GFP_KERNEL);

		if (!pdata->blmap)
			return -ENOMEM;

		for (i = 0; i < pdata->blmap_size; i++)
			pdata->blmap[i] = (char)array[i];

		if (array)
			kfree(array);

	} else {
		pdata->blmap = NULL;
	}

	pr_debug("%s: gpio: %d, max_current: %d, min: %d\n ",
			__func__, pdata->gpio,
			pdata->max_current,
			pdata->min_brightness);

	pr_debug("%s: default: %d, max: %d, pwm : %d , blmap_size : %d\n",
			__func__, pdata->default_brightness,
			pdata->max_brightness,
			rt8542_pwm_enable,
			pdata->blmap_size);

	return rc;
}
#endif

static const struct backlight_ops rt8542_bl_ops = {
	.update_status = bl_set_intensity,
	.get_brightness = bl_get_intensity,
};

static int rt8542_probe(struct i2c_client *i2c_dev,
		const struct i2c_device_id *id)
{
	struct backlight_platform_data *pdata;
	struct rt8542_device *dev;
	struct backlight_device *bl_dev;
	struct backlight_properties props;
	int err;

	printk(">> %s START\n", __func__);

#ifdef CONFIG_OF
	if (&i2c_dev->dev.of_node) {
		pdata = devm_kzalloc(&i2c_dev->dev,
				sizeof(struct backlight_platform_data),
				GFP_KERNEL);
		if (!pdata) {
			pr_err("%s: Failed to allocate memory\n", __func__);

			pr_debug("<< %s END (error#1: -ENOMEM)\n", __func__);
			return -ENOMEM;
		}
		err = rt8542_parse_dt(&i2c_dev->dev, pdata);
		if (err != 0) {
			pr_debug("<< %s END (error#2: %d)\n", __func__, err);
			return err;
		}
	} else {
		pdata = i2c_dev->dev.platform_data;
	}
#else
	pdata = i2c_dev->dev.platform_data;
#endif

	pr_err("[CHECK] pdata->gpio:%d", pdata->gpio);
	if (pdata->gpio && gpio_request(pdata->gpio, "rt8542 reset") != 0) {
		pr_err("%s: Faild to request gpio", __func__);
		pr_debug("<< %s END (error#3: -ENODEV)\n", __func__);
		return -ENODEV;
	}

	rt8542_i2c_client = i2c_dev;

	dev = kzalloc(sizeof(struct rt8542_device), GFP_KERNEL);
	if (dev == NULL) {
		dev_err(&i2c_dev->dev, "fail alloc for rt8542_device\n");
		pr_debug("<< %s END (error#4)\n", __func__);
		return 0;
	}
	main_rt8542_dev = dev;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;

	props.max_brightness = MAX_BRIGHTNESS_RT8542;
	bl_dev = backlight_device_register(I2C_BL_NAME, &i2c_dev->dev,
			NULL, &rt8542_bl_ops, &props);

#if defined(CONFIG_MACH_MSM8916_C30_TRF_US) || defined(CONFIG_MACH_MSM8916_C30C_TRF_US)
		if(get_lcd_id())
		{
			pr_err("<< %s TIANMA\n", __func__);
			bl_dev->props.max_brightness = MAX_BRIGHTNESS_RT8542 + 1; /* TIANMA PANEL needs max brightness value to 0x7E */
		}
		else
		{
			pr_err("<< %s BYD\n", __func__);
			bl_dev->props.max_brightness = MAX_BRIGHTNESS_RT8542;
		}
#else
	bl_dev->props.max_brightness = MAX_BRIGHTNESS_RT8542;
#endif

	bl_dev->props.brightness = DEFAULT_BRIGHTNESS;
	bl_dev->props.power = FB_BLANK_UNBLANK;

	dev->bl_dev = bl_dev;
	dev->client = i2c_dev;

	dev->gpio = pdata->gpio;
	dev->max_current = pdata->max_current;
	dev->min_brightness = pdata->min_brightness;
	dev->default_brightness = pdata->default_brightness;
#if defined(CONFIG_MACH_MSM8916_C30_TRF_US) || defined(CONFIG_MACH_MSM8916_C30C_TRF_US)
	if(get_lcd_id())
	{
		pr_err("<< %s TIANMA\n", __func__);
		dev->max_brightness = pdata->max_brightness + 1;  /* TIANMA PANEL needs max brightness value to 0x7E */
	}
	else
	{
		pr_err("<< %s BYD\n", __func__);
		dev->max_brightness = pdata->max_brightness;
	}
#else
	dev->max_brightness = pdata->max_brightness;
#endif
	dev->blmap_size = pdata->blmap_size;
	dev->factory_brightness = pdata->factory_brightness;

	if (dev->blmap_size) {
		dev->blmap =
			kzalloc(sizeof(char) * dev->blmap_size, GFP_KERNEL);
		if (!dev->blmap) {
			pr_err("%s: Failed to allocate memory\n", __func__);
			pr_debug("<< %s END (error#5: -ENOMEM)\n", __func__);
			return -ENOMEM;
		}
		memcpy(dev->blmap, pdata->blmap, dev->blmap_size);
	} else {
		dev->blmap = NULL;
	}

	if (gpio_get_value(dev->gpio))
		backlight_status = BL_ON;
	else
		backlight_status = POWER_OFF;

	i2c_set_clientdata(i2c_dev, dev);

	mutex_init(&dev->bl_mutex);

	err = device_create_file(&i2c_dev->dev,
			&dev_attr_rt8542_level);
	err = device_create_file(&i2c_dev->dev,
			&dev_attr_rt8542_backlight_on_off);
	err = device_create_file(&i2c_dev->dev,
			&dev_attr_rt8542_exp_min_value);
#if defined(CONFIG_BACKLIGHT_CABC_DEBUG_ENABLE)
	err = device_create_file(&i2c_dev->dev,
			&dev_attr_rt8542_pwm);
#endif

#if defined(CONFIG_BL_CURVE)
	err = device_create_file(&i2c_dev->dev, &dev_attr_i2c_bl_blmap);
#endif
	/* To reduce current consumption during booting,
	  decrease the backlight level to boot well. */
#if defined(RT8542_FLED_EN)

#ifdef FLASH_PORTING_TEMP
	//Porting Temp: build error -> comment out
	//                                                 
	//	dev->default_brightness = dev->factory_brightness;
#else
	if (lge_get_boot_mode() >= LGE_BOOT_MODE_QEM_56K)
		dev->default_brightness = dev->factory_brightness;
#endif

#endif
	rt8542_backlight_on(dev->default_brightness);

#if defined(RT8542_FLED_EN)
if (!id) {
		pr_err("rt8542_probe: id is NULL");
		id = rt8542_bl_id;
	}
	return msm_flash_i2c_probe(i2c_dev, id);
#endif

	pr_debug("<< %s END (OK)\n", __func__);
	return 0;
}

static int rt8542_remove(struct i2c_client *i2c_dev)
{
	struct rt8542_device *dev;
	int gpio = main_rt8542_dev->gpio;

	pr_debug(">> %s START\n", __func__);
	device_remove_file(&i2c_dev->dev, &dev_attr_rt8542_level);
	device_remove_file(&i2c_dev->dev, &dev_attr_rt8542_backlight_on_off);
	dev = (struct rt8542_device *)i2c_get_clientdata(i2c_dev);
	backlight_device_unregister(dev->bl_dev);
	i2c_set_clientdata(i2c_dev, NULL);

	if (gpio_is_valid(gpio))
		gpio_free(gpio);

	pr_debug("<< %s END (OK)\n", __func__);
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id rt8542_match_table[] = {

#ifdef FLASH_PORTING_TEMP
	{ .compatible = "qcom,led-flash",},
	{ },
#else
	{ .compatible = "backlight,rt8542",},
	{ },
#endif

};
#endif

static struct i2c_driver main_rt8542_driver = {
	.probe = rt8542_probe,
	.remove = rt8542_remove,
	.suspend = NULL,
	.resume = NULL,

	.id_table = rt8542_bl_id,
	.driver = {
		.name = I2C_BL_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = rt8542_match_table,
#endif
	},
};

static int __init lcd_backlight_init(void)
{
	static int err;
	pr_debug(">> %s START", __func__);

	err = i2c_add_driver(&main_rt8542_driver);

	pr_debug("<< %s END (rc: %d)", __func__, err);
	return err;
}

#if defined(RT8542_FLED_EN)
static struct msm_flash_fn_t rt8542_func_tbl = {
	.flash_get_subdev_id = msm_led_i2c_trigger_get_subdev_id,
	.flash_led_config = msm_led_i2c_trigger_config,
	.flash_led_init = msm_flash_led_init,
	.flash_led_release = msm_flash_led_release,
	.flash_led_off = msm_flash_led_off,
	.flash_led_low = msm_flash_led_low,
	.flash_led_high = msm_flash_led_high,
};

static struct msm_led_flash_ctrl_t fctrl = {
	.flash_i2c_client = &rt8542_flash_i2c_client,
	.func_tbl = &rt8542_func_tbl,
};
#endif
module_init(lcd_backlight_init);

MODULE_DESCRIPTION("RT8542 Backlight Control");
MODULE_AUTHOR("daewoo kwak");
MODULE_LICENSE("GPL");
