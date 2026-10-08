// SPDX-License-Identifier: GPL-2.0+
/*
 * Watchdog driver for SMDT MCU connected via I2C.
 *
 * The MCU (STM8S003F3P6) is on I2C bus 6 at address 0x62. It implements
 * a watchdog that must be periodically fed by writing 0xab to register
 * 0x33.
 *
 * Boot flow (wdt_prepare from the vendor userspace tool smdt_wdt.c):
 *   1. Write 0x00 to reg 0x32  (disable watchdog first — reset sequence)
 *   2. Write 0x01 to reg 0x32  (enable watchdog, ~9ms delay)
 *   3. Write 0x33 to reg 0x51  (set timeout parameter, ~8ms delay)
 *
 * Feed the dog:
 *   - Write 0xab to reg 0x33
 *
 * Disable the dog:
 *   - Write 0x00 to reg 0x32
 *
 * MCU presence check:
 *   - Read reg 0x3a, expect 0x89
 *
 * Retry strategy (borrowed from vendor smdt_wdt.c):
 *   Every I2C write is retried up to I2C_WRITE_RETRIES times with a
 *   I2C_WRITE_RETRY_DELAY_MS delay between attempts, because the MCU
 *   may not ACK immediately after power-on.  The start sequence as a
 *   whole is also retried up to START_MAX_RETRIES times.
 */

#include <dm.h>
#include <hang.h>
#include <i2c.h>
#include <log.h>
#include <wdt.h>
#include <linux/delay.h>
#include <asm/global_data.h>

DECLARE_GLOBAL_DATA_PTR;

#define SMDT_WDT_REG_CHECK	0x3a
#define SMDT_WDT_REG_ENABLE	0x32
#define SMDT_WDT_REG_FEED	0x33
#define SMDT_WDT_REG_PARAM	0x51

#define SMDT_WDT_CHECK_VAL	0x89
#define SMDT_WDT_ENABLE_VAL	0x01
#define SMDT_WDT_DISABLE_VAL	0x00
#define SMDT_WDT_FEED_VAL	0xab
#define SMDT_WDT_PARAM_VAL	0x33

#define I2C_WRITE_RETRIES		5
#define I2C_WRITE_RETRY_DELAY_MS	10
#define START_MAX_RETRIES		10
#define START_RETRY_DELAY_MS		100

struct smdt_wdt_priv {
	u32 feed_cnt;
	u32 feed_fail_cnt;
};

/*
 * Write one byte to an MCU register with retry.
 *
 * Mirrors the i2c_write() pattern from the vendor userspace tool:
 * try up to I2C_WRITE_RETRIES times, sleeping I2C_WRITE_RETRY_DELAY_MS
 * between failed attempts.  After a successful write, optionally sleep
 * delay_ms to let the MCU process the command internally.
 */
static int smdt_wdt_i2c_write(struct udevice *dev, u8 reg, u8 val, int delay_ms)
{
	int i, ret;

	for (i = 0; i < I2C_WRITE_RETRIES; i++) {
		ret = dm_i2c_reg_write(dev, reg, val);
		if (!ret) {
			if (delay_ms > 0)
				udelay(delay_ms * 1000);
			return 0;
		}
		debug("SMDT WDT: write reg 0x%02x=0x%02x attempt %d/%d failed (ret=%d)\n",
		      reg, val, i + 1, I2C_WRITE_RETRIES, ret);
		if (i < I2C_WRITE_RETRIES - 1)
			udelay(I2C_WRITE_RETRY_DELAY_MS * 1000);
	}

	printf("SMDT WDT: write reg 0x%02x=0x%02x failed after %d retries (ret=%d)\n",
	       reg, val, I2C_WRITE_RETRIES, ret);
	return ret;
}

/*
 * Read one byte from an MCU register with retry.
 */
static int smdt_wdt_i2c_read(struct udevice *dev, u8 reg, u8 *val)
{
	int i, ret;

	for (i = 0; i < I2C_WRITE_RETRIES; i++) {
		ret = dm_i2c_reg_read(dev, reg);
		if (ret >= 0) {
			*val = (u8)ret;
			return 0;
		}
		debug("SMDT WDT: read reg 0x%02x attempt %d/%d failed (ret=%d)\n",
		      reg, i + 1, I2C_WRITE_RETRIES, ret);
		if (i < I2C_WRITE_RETRIES - 1)
			udelay(I2C_WRITE_RETRY_DELAY_MS * 1000);
	}

	printf("SMDT WDT: read reg 0x%02x failed after %d retries (ret=%d)\n",
	       reg, I2C_WRITE_RETRIES, ret);
	return ret;
}

/*
 * Check MCU presence by reading reg 0x3a and comparing to 0x89.
 */
static int smdt_wdt_check_mcu(struct udevice *dev)
{
	u8 val;
	int ret;

	ret = smdt_wdt_i2c_read(dev, SMDT_WDT_REG_CHECK, &val);
	if (ret)
		return ret;

	if (val != SMDT_WDT_CHECK_VAL) {
		printf("SMDT WDT: MCU check failed, reg 0x%02x = 0x%02x (expected 0x%02x)\n",
		       SMDT_WDT_REG_CHECK, val, SMDT_WDT_CHECK_VAL);
		return -EIO;
	}

	printf("SMDT WDT: MCU check OK (reg 0x%02x = 0x%02x)\n",
	       SMDT_WDT_REG_CHECK, val);
	return 0;
}

/*
 * Start the watchdog.
 *
 * Follows the wdt_prepare() sequence from the vendor tool:
 *   1. Disable first (reset sequence)
 *   2. Enable with 9ms delay
 *   3. Set timeout parameter with 8ms delay
 *
 * The entire sequence is retried up to START_MAX_RETRIES times because
 * the MCU may not be ready on the first attempt right after power-on.
 */
static int smdt_wdt_start(struct udevice *dev, u64 timeout_ms, ulong flags)
{
	struct udevice *bus = dev_get_parent(dev);
	int ret, round;

	for (round = 0; round < START_MAX_RETRIES; round++) {
		printf("SMDT WDT: start round %d/%d\n", round + 1, START_MAX_RETRIES);

		/* Try to deblock the I2C bus before each round */
		if (round > 0 && bus) {
			ret = i2c_deblock(bus);
			if (ret)
				debug("SMDT WDT: i2c_deblock failed (ret=%d)\n", ret);
			else
				printf("SMDT WDT: i2c_deblock done\n");
		}

		/* Step 1: Disable first — reset sequence from wdt_prepare() */
		ret = smdt_wdt_i2c_write(dev, SMDT_WDT_REG_ENABLE,
					 SMDT_WDT_DISABLE_VAL, 0);
		if (ret)
			goto retry;

		/* Step 2: Enable with 9ms delay */
		ret = smdt_wdt_i2c_write(dev, SMDT_WDT_REG_ENABLE,
					 SMDT_WDT_ENABLE_VAL, 9);
		if (ret)
			goto retry;

		/* Step 3: Set timeout parameter with 8ms delay */
		ret = smdt_wdt_i2c_write(dev, SMDT_WDT_REG_PARAM,
					 SMDT_WDT_PARAM_VAL, 8);
		if (ret)
			goto retry;

		printf("SMDT WDT: started, timeout=%llums (round %d)\n",
		       timeout_ms, round + 1);
		return 0;

retry:
		printf("SMDT WDT: start failed at round %d (ret=%d), retrying in %dms\n",
		       round + 1, ret, START_RETRY_DELAY_MS);
		udelay(START_RETRY_DELAY_MS * 1000);
	}

	printf("SMDT WDT: start FAILED after %d rounds\n", START_MAX_RETRIES);
	return ret;
}

static int smdt_wdt_stop(struct udevice *dev)
{
	int ret;

	ret = smdt_wdt_i2c_write(dev, SMDT_WDT_REG_ENABLE,
				 SMDT_WDT_DISABLE_VAL, 0);
	if (ret)
		printf("SMDT WDT: failed to stop (ret=%d)\n", ret);
	else
		printf("SMDT WDT: stopped\n");

	return ret;
}

static int smdt_wdt_reset(struct udevice *dev)
{
	struct smdt_wdt_priv *priv = dev_get_priv(dev);
	int ret;

	ret = smdt_wdt_i2c_write(dev, SMDT_WDT_REG_FEED, SMDT_WDT_FEED_VAL, 0);
	if (ret) {
		priv->feed_fail_cnt++;
		printf("SMDT WDT: feed failed (ret=%d, total_fail=%u)\n",
		       ret, priv->feed_fail_cnt);
		return ret;
	}

	priv->feed_cnt++;

	return 0;
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

static int smdt_wdt_probe(struct udevice *dev)
{
	struct smdt_wdt_priv *priv = dev_get_priv(dev);
	int ret;

	priv->feed_cnt = 0;
	priv->feed_fail_cnt = 0;

	printf("SMDT WDT: probed %s\n", dev->name);

	/* Check MCU presence — non-fatal if it fails */
	ret = smdt_wdt_check_mcu(dev);
	if (ret)
		printf("SMDT WDT: MCU not ready yet, will retry during start\n");

	return 0;
}

static const struct udevice_id smdt_wdt_ids[] = {
	{ .compatible = "smdt,mcu-wdt" },
	{}
};

U_BOOT_DRIVER(smdt_wdt) = {
	.name		= "smdt_wdt",
	.id		= UCLASS_WDT,
	.of_match	= smdt_wdt_ids,
	.probe		= smdt_wdt_probe,
	.priv_auto	= sizeof(struct smdt_wdt_priv),
	.ops		= &smdt_wdt_ops,
};
