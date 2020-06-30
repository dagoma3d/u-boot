// SPDX-License-Identifier: GPL-2.0
/*
 * LCD: st7789v, TFT 2.8", 240x320, RGB24
 * LCD initialization via SPI
 *
 */
#define DEBUG
#include <common.h>
// #include <backlight.h>
#include <command.h>
#include <display.h>
#include <dm.h>
#include <log.h>
#include <dm/read.h>
#include <dm/uclass-internal.h>
#include <errno.h>
#include <spi.h>
#include <asm/gpio.h>
#include <linux/delay.h>
#include <mipi_display.h>

#define PWR_ON_DELAY_MSECS  120
#define HSD20_IPS 1

enum st7789v_command {
	PORCTRL = 0xB2,
	GCTRL = 0xB7,
	VCOMS = 0xBB,
	VDVVRHEN = 0xC2,
	VRHS = 0xC3,
	VDVS = 0xC4,
	VCMOFSET = 0xC5,
	PWCTRL1 = 0xD0,
	PVGAMCTRL = 0xE0,
	NVGAMCTRL = 0xE1,
};

#define MADCTL_BGR BIT(3) /* bitmask for RGB/BGR order */
#define MADCTL_MV BIT(5) /* bitmask for page/column order */
#define MADCTL_MX BIT(6) /* bitmask for column address order */
#define MADCTL_MY BIT(7) /* bitmask for page address order */


static int spi_write_u8(struct spi_slave *slave, u8 val)
{
	unsigned short buf8 = htons(val);
	int ret = 0;

	ret = spi_xfer(slave, 8, &buf8, NULL,
		       SPI_XFER_BEGIN | SPI_XFER_END);
	if (ret)
		debug("%s: Failed to send: %d\n", __func__, ret);

	return ret;
}

static void spi_write_u8_array(struct spi_slave *slave, u8 *buff,
					int size)
{
	int i;

	for (i = 0; i < size; i++)
		spi_write_u8(slave, buff[i]);
}

static void init_display(struct spi_slave *slave)
{
	/* turn off sleep mode */
	spi_write_u8(slave, MIPI_DCS_EXIT_SLEEP_MODE);
	mdelay(120);

	/* set pixel format to RGB-565 */
	static u8 _pixel_format[] = {
	  MIPI_DCS_SET_PIXEL_FORMAT,
	  MIPI_DCS_PIXEL_FMT_16BIT,
	};
	spi_write_u8_array(slave, _pixel_format,
				    ARRAY_SIZE(_pixel_format));
	
	static u8 _portctrl[] = {
		PORCTRL, 
		HSD20_IPS ? 0x05 : 0x08, 
		HSD20_IPS ? 0x05 : 0x08, 
		0x00, 
		HSD20_IPS ? 0x33 : 0x22, 
		HSD20_IPS ? 0x33 : 0x22,
	};

	spi_write_u8_array(slave, _portctrl,
				ARRAY_SIZE(_portctrl));

	/*
	 * VGH = 13.26V
	 * VGL = -10.43V
	 */
	static u8 _gctrl[] = {
		GCTRL, 
		HSD20_IPS ? 0x75 : 0x35,
	};
	spi_write_u8_array(slave, _gctrl,
				ARRAY_SIZE(_gctrl));

	/*
	 * VDV and VRH register values come from command write
	 * (instead of NVM)
	 */
	static u8 _vdvvrhen[] = {
		VDVVRHEN, 
		0x01, 
		0xFF,
	};
	spi_write_u8_array(slave, _vdvvrhen,
				ARRAY_SIZE(_vdvvrhen));
	/*
	 * VAP =  4.1V + (VCOM + VCOM offset + 0.5 * VDV)
	 * VAN = -4.1V + (VCOM + VCOM offset + 0.5 * VDV)
	 */
	static u8 _vrhs[] = {
		VRHS, 
		HSD20_IPS ? 0x13 : 0x0B,
	};
	spi_write_u8_array(slave, _vrhs,
			ARRAY_SIZE(_vrhs));

	/* VDV = 0V */
	static u8 _vdvs[] = {
		VDVS, 
		0x20,
	};
	spi_write_u8_array(slave, _vdvs,
			ARRAY_SIZE(_vdvs));

	/* VCOM = 0.9V */
	static u8 _vcoms[] = {
		VCOMS, 
		HSD20_IPS ? 0x22: 0x20,
	};
	spi_write_u8_array(slave, _vcoms,
			ARRAY_SIZE(_vcoms));

	/* VCOM offset = 0V */
	static u8 _vcmofset[] = {
		VCMOFSET, 
		0x20,
	};
	spi_write_u8_array(slave, _vcmofset,
			ARRAY_SIZE(_vcmofset));

	/*
	 * AVDD = 6.8V
	 * AVCL = -4.8V
	 * VDS = 2.3V
	 */
	static u8 _pwctrl1[] = {
		PWCTRL1, 
		0xA4,
		0xA1,
	};
	spi_write_u8_array(slave, _pwctrl1,
			ARRAY_SIZE(_pwctrl1));

	spi_write_u8(slave, MIPI_DCS_SET_DISPLAY_ON);

	if (HSD20_IPS)
		spi_write_u8(slave, MIPI_DCS_ENTER_INVERT_MODE);
}

/**
 * set_var() - apply LCD properties like rotation and BGR mode
 *
 * @par: FBTFT parameter object
 *
 * Return: 0 on success, < 0 if error occurred.
 */
static void set_var(struct spi_slave *slave){
}

static int st7789v_spi_startup(struct spi_slave *slave)
{
	int ret;

	ret = spi_claim_bus(slave);
	if (ret)
		return ret;

	init_display(slave);
	set_var(slave);

	spi_release_bus(slave);
	return 0;
}

static int do_sitronixset(struct cmd_tbl *cmdtp, int flag, int argc,
		    char *const argv[])
{
	struct spi_slave *slave;
	struct udevice *dev;
	int ret;

	ret = uclass_get_device_by_driver(UCLASS_VIDEO_CONSOLE,
					  DM_GET_DRIVER(st7789v_lcd), &dev);
	if (ret) {
		printf("%s: Could not get st7789v device\n", __func__);
		return ret;
	}
	slave = dev_get_parent_priv(dev);
	if (!slave) {
		printf("%s: No slave data\n", __func__);
		return -ENODEV;
	}
	st7789v_spi_startup(slave);

	return 0;
}

U_BOOT_CMD(
	sitronixset,	2,	1,	do_sitronixset,
	"set sitronixdisplay",
	""
);

static int st7789v_bind(struct udevice *dev)
{
	printf("%s: binding\n", __func__);
	return 0;
}

static int st7789v_probe(struct udevice *dev)
{
	printf("%s: probing\n", __func__);
	return 0;
}

static const struct udevice_id st7789v_ids[] = {
	{ .compatible = "sitronix,st7789v" },
	{ }
};

struct st7789v_lcd_priv {
	struct display_timing timing;
	// struct udevice *backlight;
	struct gpio_desc enable;
	int panel_bpp;
	u32 power_on_delay;
};

static int st7789v_lcd_read_timing(struct udevice *dev,
				  struct display_timing *timing)
{
	struct st7789v_lcd_priv *priv = dev_get_priv(dev);

	memcpy(timing, &priv->timing, sizeof(struct display_timing));

	return 0;
}

static int st7789v_lcd_enable(struct udevice *dev, int bpp,
			     const struct display_timing *edid)
{
	struct spi_slave *slave = dev_get_parent_priv(dev);
	struct st7789v_lcd_priv *priv = dev_get_priv(dev);
	int ret = 0;

	dm_gpio_set_value(&priv->enable, 1);
	// ret = backlight_enable(priv->backlight);

	mdelay(priv->power_on_delay);
	st7789v_spi_startup(slave);

	return ret;
};

static const struct dm_display_ops st7789v_lcd_ops = {
	.read_timing = st7789v_lcd_read_timing,
	.enable = st7789v_lcd_enable,
};

static int st7789v_ofdata_to_platdata(struct udevice *dev)
{
	struct st7789v_lcd_priv *priv = dev_get_priv(dev);
	int ret;

	// ret = uclass_get_device_by_phandle(UCLASS_PANEL_BACKLIGHT, dev,
	// 				   "backlight", &priv->backlight);
	// if (ret) {
	// 	debug("%s: Cannot get backlight: ret=%d\n", __func__, ret);
	// 	return log_ret(ret);
	// }
	ret = gpio_request_by_name(dev, "enable-gpios", 0, &priv->enable,
				   GPIOD_IS_OUT);
	if (ret) {
		debug("%s: Warning: cannot get enable GPIO: ret=%d\n",
		      __func__, ret);
		if (ret != -ENOENT)
			return log_ret(ret);
	}

	priv->power_on_delay = dev_read_u32_default(dev, "power-on-delay", 10);

	return 0;
}

U_BOOT_DRIVER(st7789v_lcd) = {
	.name   = "st7789v",
	.id     = UCLASS_VIDEO_CONSOLE,
	.ops    = &st7789v_lcd_ops,
	.ofdata_to_platdata	= st7789v_ofdata_to_platdata,
	.of_match = st7789v_ids,
	.bind   = st7789v_bind,
	.probe  = st7789v_probe,
	.priv_auto_alloc_size = sizeof(struct st7789v_lcd_priv),
};
