// SPDX-License-Identifier: GPL-2.0+
/*
 * Board specific initialization for BDY G98 RK3588 (AIoT-3588IED)
 *
 * This file handles early initialization of the NCA9555 GPIO expander
 * (I2C bus 6, address 0x20) to ensure that USB host power (GPIO 0)
 * and PCIe reset (GPIO 15) are in a known good state.
 *
 * The NCA9555 (Novosense, PCA9555-compatible) does not reset its pins
 * when the board is only partially powered down (e.g. Type-C still
 * connected).  On the next 12V power-up the pins retain their previous
 * values, which can leave USB and M.2 peripherals unpowered.  By
 * explicitly configuring the expander here we guarantee a clean state
 * regardless of the previous power cycle.
 *
 * The retry logic is borrowed from the vendor smdt_wdt.c userspace
 * tool: every I2C transaction is retried multiple times with a short
 * delay, because the I2C bus and the devices on it (NCA9555, SMDT MCU)
 * may not be immediately responsive right after power-on.
 *
 * Key design decision: we bypass i2c_get_chip_for_busnum() because it
 * does an I2C probe (zero-length write) which always NACKs when the
 * bus is stuck.  Instead we get the bus, deblock it, then use
 * i2c_get_chip() which only does a DM-level probe (no I2C transaction).
 * We then attempt the actual register writes directly — the write
 * itself will succeed or fail, telling us if the device is there.
 */

#include <stdio.h>
#include <dm.h>
#include <i2c.h>
#include <linux/delay.h>
#include <log.h>

/*
 * NCA9555 / PCA9555 register map (1-byte register addresses)
 *
 *  0x00  Input Port 0      (read-only)
 *  0x01  Input Port 1      (read-only)
 *  0x02  Output Port 0
 *  0x03  Output Port 1
 *  0x04  Polarity Inv. Port 0
 *  0x05  Polarity Inv. Port 1
 *  0x06  Configuration Port 0   (1 = input, 0 = output)
 *  0x07  Configuration Port 1   (1 = input, 0 = output)
 */
#define NCA9555_BUS		6
#define NCA9555_ADDR		0x20

#define NCA9555_REG_IN0		0x00
#define NCA9555_REG_IN1		0x01
#define NCA9555_REG_OUT0	0x02
#define NCA9555_REG_OUT1	0x03
#define NCA9555_REG_POL0	0x04
#define NCA9555_REG_POL1	0x05
#define NCA9555_REG_CFG0	0x06
#define NCA9555_REG_CFG1	0x07

/*
 * Pin assignments on this board (from factory DTS rk3588-aiot-3588a.dts):
 *
 *  GPIO 0  (Port 0 bit 0) - vcc5v0_usb0/host : USB 5V power, ACTIVE LOW
 *  GPIO 1  (Port 0 bit 1) - vcc5v0_usb2      : USB 5V power, ACTIVE LOW
 *  GPIO 7  (Port 0 bit 7) - vcc5v0_usb1      : USB 5V power, ACTIVE LOW
 *  GPIO 15 (Port 1 bit 7) - pcie3x4 reset    : PCIe reset, ACTIVE HIGH
 *
 * For USB power (active-low): output LOW = power ON, output HIGH = power OFF.
 * For PCIe reset (active-high): output HIGH = reset asserted.
 *
 * We set USB power pins LOW (ON) and leave PCIe reset HIGH (asserted)
 * so the PCIe driver can later deassert it.
 *
 * Other GPIO pins are left as inputs (default) to avoid driving
 * anything we don't understand.
 */
#define NCA9555_GPIO_USB_POWER		0
#define NCA9555_GPIO_PCIE_RESET		15

/*
 * Retry parameters borrowed from vendor smdt_wdt.c:
 *   - Each I2C read/write gets I2C_OP_RETRIES attempts
 *   - Delay I2C_OP_RETRY_DELAY_MS between retries
 *   - The whole init sequence gets INIT_MAX_RETRIES rounds
 *   - Delay INIT_RETRY_DELAY_MS between rounds
 */
#define I2C_OP_RETRIES		5
#define I2C_OP_RETRY_DELAY_MS	10
#define INIT_MAX_RETRIES	200
#define INIT_RETRY_DELAY_MS	100

static int nca9555_read(struct udevice *dev, u8 reg, u8 *val)
{
	int i, ret;

	for (i = 0; i < I2C_OP_RETRIES; i++) {
		ret = dm_i2c_read(dev, reg, val, 1);
		if (!ret)
			return 0;
		debug("NCA9555: read reg 0x%02x attempt %d/%d failed (ret=%d)\n",
		      reg, i + 1, I2C_OP_RETRIES, ret);
		udelay(I2C_OP_RETRY_DELAY_MS * 1000);
	}

	printf("NCA9555: read reg 0x%02x failed after %d retries (ret=%d)\n",
	       reg, I2C_OP_RETRIES, ret);
	return ret;
}

static int nca9555_write(struct udevice *dev, u8 reg, u8 val)
{
	int i, ret;

	for (i = 0; i < I2C_OP_RETRIES; i++) {
		ret = dm_i2c_write(dev, reg, &val, 1);
		if (!ret)
			return 0;
		debug("NCA9555: write reg 0x%02x=0x%02x attempt %d/%d failed (ret=%d)\n",
		      reg, val, i + 1, I2C_OP_RETRIES, ret);
		udelay(I2C_OP_RETRY_DELAY_MS * 1000);
	}

	printf("NCA9555: write reg 0x%02x=0x%02x failed after %d retries (ret=%d)\n",
	       reg, val, I2C_OP_RETRIES, ret);
	return ret;
}

static void nca9555_dump_regs(struct udevice *dev, const char *label)
{
	u8 in0, in1, out0, out1, pol0, pol1, cfg0, cfg1;

	if (nca9555_read(dev, NCA9555_REG_IN0, &in0) ||
	    nca9555_read(dev, NCA9555_REG_IN1, &in1) ||
	    nca9555_read(dev, NCA9555_REG_OUT0, &out0) ||
	    nca9555_read(dev, NCA9555_REG_OUT1, &out1) ||
	    nca9555_read(dev, NCA9555_REG_POL0, &pol0) ||
	    nca9555_read(dev, NCA9555_REG_POL1, &pol1) ||
	    nca9555_read(dev, NCA9555_REG_CFG0, &cfg0) ||
	    nca9555_read(dev, NCA9555_REG_CFG1, &cfg1)) {
		printf("NCA9555: %s - failed to dump registers\n", label);
		return;
	}

	printf("NCA9555: %s register dump:\n", label);
	printf("  Input   : 0x%02x 0x%02x\n", in0, in1);
	printf("  Output  : 0x%02x 0x%02x\n", out0, out1);
	printf("  Polarity: 0x%02x 0x%02x\n", pol0, pol1);
	printf("  Config  : 0x%02x 0x%02x  (1=input, 0=output)\n", cfg0, cfg1);
}

/*
 * Get the NCA9555 I2C device handle without doing an I2C probe.
 *
 * i2c_get_chip_for_busnum() does i2c_probe_chip() which sends a
 * zero-length write — this always NACKs when the bus is stuck or
 * the device hasn't settled.  We bypass that by getting the bus
 * ourselves, attempting a deblock, then calling i2c_get_chip()
 * which only does a DM-level device probe (no I2C transaction).
 */
static int nca9555_get_device(struct udevice **devp)
{
	struct udevice *bus;
	int ret;

	ret = uclass_get_device_by_seq(UCLASS_I2C, NCA9555_BUS, &bus);
	if (ret) {
		printf("NCA9555: cannot find I2C bus %d (ret=%d)\n",
		       NCA9555_BUS, ret);
		return ret;
	}

	printf("NCA9555: I2C bus %d found: %s\n", NCA9555_BUS, bus->name);

	/* Try to deblock the bus in case SDA is held low */
	ret = i2c_deblock(bus);
	if (ret) {
		printf("NCA9555: i2c_deblock failed or not available (ret=%d), "
		       "continuing anyway\n", ret);
	} else {
		printf("NCA9555: i2c_deblock done\n");
	}

	/* Get the chip device — no I2C probe, just DM bind+probe */
	ret = i2c_get_chip(bus, NCA9555_ADDR, 1, devp);
	if (ret) {
		printf("NCA9555: i2c_get_chip failed (ret=%d)\n", ret);
		return ret;
	}

	printf("NCA9555: chip device: %s\n", (*devp)->name);
	return 0;
}

/*
 * Initialize the NCA9555 GPIO expander to a known-good state.
 *
 * Steps:
 *   1. Clear polarity inversion (both ports) - no inversion.
 *   2. Set Output Port 0: clear bits 0,1,7 (USB power ON, active-low).
 *   3. Set Output Port 1: set bit 7 (PCIe reset asserted, active-high).
 *   4. Set Configuration Port 0: clear bits 0,1,7 (GPIO 0,1,7 = output).
 *   5. Set Configuration Port 1: clear bit 7 (GPIO 15 = output).
 *
 * All other pins are left in their default (input) mode.
 *
 * The whole sequence is wrapped in a retry loop because the I2C bus
 * may not be fully ready on the first attempt right after power-on.
 */
static int nca9555_early_init(void)
{
	struct udevice *dev;
	int ret, round;
	u8 val;

	printf("NCA9555: starting early init (bus=%d addr=0x%02x)\n",
	       NCA9555_BUS, NCA9555_ADDR);

	for (round = 0; round < INIT_MAX_RETRIES; round++) {
		printf("NCA9555: init round %d/%d\n", round + 1, INIT_MAX_RETRIES);

		/* Get device handle — bypasses I2C probe */
		ret = nca9555_get_device(&dev);
		if (ret) {
			printf("NCA9555: cannot get device (round %d, ret=%d), "
			       "retrying in %dms\n",
			       round + 1, ret, INIT_RETRY_DELAY_MS);
			udelay(INIT_RETRY_DELAY_MS * 1000);
			continue;
		}

		nca9555_dump_regs(dev, "before init");

		/* Step 1: Clear polarity inversion on both ports */
		ret = nca9555_write(dev, NCA9555_REG_POL0, 0x00);
		if (ret)
			goto retry;
		ret = nca9555_write(dev, NCA9555_REG_POL1, 0x00);
		if (ret)
			goto retry;
		printf("NCA9555: polarity inversion cleared\n");

		/* Step 2: Power-cycle USB to reset stuck devices on warm-boot */
		ret = nca9555_read(dev, NCA9555_REG_OUT0, &val);
		if (ret)
			goto retry;
		printf("NCA9555: Output Port 0 before: 0x%02x\n", val);

		val |= BIT(0);    /* GPIO 0: vcc5v0_usb0, active-low, HIGH=OFF */
		val |= BIT(1);    /* GPIO 1: vcc5v0_usb2, active-low, HIGH=OFF */
		val |= BIT(7);    /* GPIO 7: vcc5v0_usb1, active-low, HIGH=OFF */
		ret = nca9555_write(dev, NCA9555_REG_OUT0, val);
		if (ret)
			goto retry;
		printf("NCA9555: USB power OFF (Output Port 0: 0x%02x)\n", val);

		udelay(100000);   /* 100ms power-off to let USB devices discharge */

		val &= ~BIT(0);   /* GPIO 0: LOW = ON */
		val &= ~BIT(1);   /* GPIO 1: LOW = ON */
		val &= ~BIT(7);   /* GPIO 7: LOW = ON */
		ret = nca9555_write(dev, NCA9555_REG_OUT0, val);
		if (ret)
			goto retry;
		printf("NCA9555: USB power ON  (Output Port 0: 0x%02x)\n", val);

		/* Step 3: Set GPIO 15 (PCIe reset) output HIGH (asserted) */
		ret = nca9555_read(dev, NCA9555_REG_OUT1, &val);
		if (ret)
			goto retry;
		printf("NCA9555: Output Port 1 before: 0x%02x\n", val);
		val |= BIT(NCA9555_GPIO_PCIE_RESET % 8);
		ret = nca9555_write(dev, NCA9555_REG_OUT1, val);
		if (ret)
			goto retry;
		printf("NCA9555: Output Port 1 after:  0x%02x  (PCIe reset HIGH)\n",
		       val);

		/* Step 4: Configure GPIO 0,1,7 as output (clear config bits) */
		ret = nca9555_read(dev, NCA9555_REG_CFG0, &val);
		if (ret)
			goto retry;
		printf("NCA9555: Config Port 0 before: 0x%02x\n", val);
		val &= ~BIT(0);   /* GPIO 0 = output */
		val &= ~BIT(1);   /* GPIO 1 = output */
		val &= ~BIT(7);   /* GPIO 7 = output */
		ret = nca9555_write(dev, NCA9555_REG_CFG0, val);
		if (ret)
			goto retry;
		printf("NCA9555: Config Port 0 after:  0x%02x  (GPIO 0,1,7 = output)\n",
		       val);

		/* Step 5: Configure GPIO 15 as output */
		ret = nca9555_read(dev, NCA9555_REG_CFG1, &val);
		if (ret)
			goto retry;
		printf("NCA9555: Config Port 1 before: 0x%02x\n", val);
		val &= ~BIT(NCA9555_GPIO_PCIE_RESET % 8);
		ret = nca9555_write(dev, NCA9555_REG_CFG1, val);
		if (ret)
			goto retry;
		printf("NCA9555: Config Port 1 after:  0x%02x  (GPIO %d = output)\n",
		       val, NCA9555_GPIO_PCIE_RESET);

		/* Let outputs settle */
		udelay(10000);

		nca9555_dump_regs(dev, "after init");

		printf("NCA9555: early init complete (round %d)\n", round + 1);
		return 0;

retry:
		printf("NCA9555: init failed at round %d (ret=%d), "
		       "retrying in %dms\n",
		       round + 1, ret, INIT_RETRY_DELAY_MS);
		udelay(INIT_RETRY_DELAY_MS * 1000);
	}

	printf("NCA9555: early init FAILED after %d rounds\n", INIT_MAX_RETRIES);
	return -EIO;
}

/*
 * board_init() runs early in the init_r sequence, before initr_watchdog
 * and before misc_init_r.  We hook NCA9555 init here so that the I2C
 * GPIO expander is configured before the SMDT watchdog driver tries to
 * probe the MCU on the same I2C bus 6.
 *
 * This overrides the default board_init() in arch/arm/mach-rockchip/board.c
 * which just returns 0.
 */
int board_init(void)
{
	int ret;

	printf("BDY G98: board_init() entered\n");

	ret = nca9555_early_init();
	if (ret)
		printf("BDY G98: NCA9555 init failed (ret=%d), continuing boot\n",
		       ret);
	else
		printf("BDY G98: NCA9555 init OK\n");

	return 0;
}
