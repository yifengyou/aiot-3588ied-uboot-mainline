// SPDX-License-Identifier: GPL-2.0+
/*
 * Watchdog driver for SMDT MCU connected via I2C.
 *
 * The MCU is on I2C bus 6 at address 0x62. It implements a watchdog
 * that must be periodically fed by writing 0xab to register 0x33.
 *
 * Boot flow (wdt_simulator_lite from the vendor tool):
 *   1. Write 0x01 to reg 0x32  (enable watchdog, ~9ms delay)
 *   2. Write 0x33 to reg 0x51  (set timeout parameter, ~8ms delay)
 *
 * Feed the dog:
 *   - Write 0xab to reg 0x33
 *
 * Disable the dog:
 *   - Write 0x00 to reg 0x32
 */

#include <dm.h>
#include <i2c.h>
#include <wdt.h>
#include <linux/delay.h>

#define SMDT_WDT_REG_ENABLE	0x32
#define SMDT_WDT_REG_FEED	0x33
#define SMDT_WDT_REG_PARAM	0x51

#define SMDT_WDT_ENABLE_VAL	0x01
#define SMDT_WDT_DISABLE_VAL	0x00
#define SMDT_WDT_FEED_VAL	0xab
#define SMDT_WDT_PARAM_VAL	0x33

static int smdt_wdt_i2c_write(struct udevice *dev, u8 reg, u8 val)
{
	return dm_i2c_reg_write(dev, reg, val);
}

static int smdt_wdt_start(struct udevice *dev, u64 timeout_ms, ulong flags)
{
	int ret;

	ret = smdt_wdt_i2c_write(dev, SMDT_WDT_REG_ENABLE, SMDT_WDT_ENABLE_VAL);
	if (ret)
		return ret;

	udelay(9000);

	ret = smdt_wdt_i2c_write(dev, SMDT_WDT_REG_PARAM, SMDT_WDT_PARAM_VAL);
	if (ret)
		return ret;

	udelay(8000);

	return 0;
}

static int smdt_wdt_stop(struct udevice *dev)
{
	return smdt_wdt_i2c_write(dev, SMDT_WDT_REG_ENABLE, SMDT_WDT_DISABLE_VAL);
}

static int smdt_wdt_reset(struct udevice *dev)
{
	return smdt_wdt_i2c_write(dev, SMDT_WDT_REG_FEED, SMDT_WDT_FEED_VAL);
}

static int smdt_wdt_expire_now(struct udevice *dev, ulong flags)
{
	int ret;

	ret = smdt_wdt_stop(dev);
	if (ret)
		return ret;

	ret = smdt_wdt_start(dev, 1, 0);
	if (ret)
		return ret;

	hang();

	return 0;
}

static const struct wdt_ops smdt_wdt_ops = {
	.start		= smdt_wdt_start,
	.stop		= smdt_wdt_stop,
	.reset		= smdt_wdt_reset,
	.expire_now	= smdt_wdt_expire_now,
};

static const struct udevice_id smdt_wdt_ids[] = {
	{ .compatible = "smdt,mcu-wdt" },
	{}
};

U_BOOT_DRIVER(smdt_wdt) = {
	.name		= "smdt_wdt",
	.id		= UCLASS_WDT,
	.of_match	= smdt_wdt_ids,
	.ops		= &smdt_wdt_ops,
};
