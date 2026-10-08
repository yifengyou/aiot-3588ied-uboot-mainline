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
 * Key design decision: we use raw bus-level I2C transfers
 * (i2c_get_ops(bus)->xfer) instead of i2c_get_chip() + dm_i2c_read/write().
 * This is because i2c_get_chip() calls device_probe() on the child node,
 * which—when the DTS node has compatible="nxp,pca9555"—triggers
 * pca953x_probe().  That probe does I2C register reads that can fail
 * if the I2C bus is not yet stable, permanently marking the DM device
 * as failed (-EREMOTEIO).  Once failed, no subsequent retry can recover
 * it, and the pca953x GPIO controller becomes unavailable for PCIe
 * reset-gpios.
 *
 * By doing raw bus-level transfers we avoid device_probe() entirely.
 * The pca953x driver will later be probed by the DM framework when the
 * PCIe driver calls gpio_request_by_name("reset-gpios"), at which point
 * the I2C bus is fully stable and the probe succeeds.
 *
 * We run in misc_init_r() rather than board_init() because the I2C bus
 * is not ready for data transfers during board_init() — the controller
 * probes but actual I2C transactions return -EREMOTEIO (-121).
 * misc_init_r() runs much later in the init sequence, after all DM
 * devices have been fully initialised and the I2C bus is stable.
 */

#include <stdio.h>
#include <dm.h>
#include <i2c.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <log.h>

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

#define NCA9555_GPIO_USB_POWER		0
#define NCA9555_GPIO_PCIE_RESET		15

#define I2C_OP_RETRIES		5
#define I2C_OP_RETRY_DELAY_MS	10
#define INIT_MAX_RETRIES	10
#define INIT_RETRY_DELAY_MS	100

static struct udevice *nca9555_bus;

static int nca9555_read(u8 reg, u8 *val)
{
	struct i2c_msg msgs[2];
	u8 reg_buf = reg;
	int i, ret;
	struct dm_i2c_ops *ops;

	if (!nca9555_bus)
		return -ENODEV;

	ops = i2c_get_ops(nca9555_bus);
	if (!ops || !ops->xfer)
		return -ENOSYS;

	msgs[0].addr = NCA9555_ADDR;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &reg_buf;

	msgs[1].addr = NCA9555_ADDR;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].buf = val;

	for (i = 0; i < I2C_OP_RETRIES; i++) {
		ret = ops->xfer(nca9555_bus, msgs, 2);
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

static int nca9555_write(u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2];
	int i, ret;
	struct dm_i2c_ops *ops;

	if (!nca9555_bus)
		return -ENODEV;

	ops = i2c_get_ops(nca9555_bus);
	if (!ops || !ops->xfer)
		return -ENOSYS;

	buf[0] = reg;
	buf[1] = val;
	msg.addr = NCA9555_ADDR;
	msg.flags = 0;
	msg.len = 2;
	msg.buf = buf;

	for (i = 0; i < I2C_OP_RETRIES; i++) {
		ret = ops->xfer(nca9555_bus, &msg, 1);
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

static void nca9555_dump_regs(const char *label)
{
	u8 in0, in1, out0, out1, pol0, pol1, cfg0, cfg1;

	if (nca9555_read(NCA9555_REG_IN0, &in0) ||
	    nca9555_read(NCA9555_REG_IN1, &in1) ||
	    nca9555_read(NCA9555_REG_OUT0, &out0) ||
	    nca9555_read(NCA9555_REG_OUT1, &out1) ||
	    nca9555_read(NCA9555_REG_POL0, &pol0) ||
	    nca9555_read(NCA9555_REG_POL1, &pol1) ||
	    nca9555_read(NCA9555_REG_CFG0, &cfg0) ||
	    nca9555_read(NCA9555_REG_CFG1, &cfg1)) {
		printf("NCA9555: %s - failed to dump registers\n", label);
		return;
	}

	printf("NCA9555: %s register dump:\n", label);
	printf("  Input   : 0x%02x 0x%02x\n", in0, in1);
	printf("  Output  : 0x%02x 0x%02x\n", out0, out1);
	printf("  Polarity: 0x%02x 0x%02x\n", pol0, pol1);
	printf("  Config  : 0x%02x 0x%02x  (1=input, 0=output)\n", cfg0, cfg1);
}

static int nca9555_get_bus(void)
{
	int ret;

	ret = uclass_get_device_by_seq(UCLASS_I2C, NCA9555_BUS, &nca9555_bus);
	if (ret) {
		printf("NCA9555: cannot find I2C bus %d (ret=%d)\n",
		       NCA9555_BUS, ret);
		return ret;
	}

	printf("NCA9555: I2C bus %d found: %s\n", NCA9555_BUS, nca9555_bus->name);

	ret = i2c_deblock(nca9555_bus);
	if (ret)
		printf("NCA9555: i2c_deblock failed or not available (ret=%d), "
		       "continuing anyway\n", ret);
	else
		printf("NCA9555: i2c_deblock done\n");

	return 0;
}

static int nca9555_early_init(void)
{
	int ret, round;
	u8 val;

	printf("NCA9555: starting early init (bus=%d addr=0x%02x)\n",
	       NCA9555_BUS, NCA9555_ADDR);

	for (round = 0; round < INIT_MAX_RETRIES; round++) {
		printf("NCA9555: init round %d/%d\n", round + 1, INIT_MAX_RETRIES);

		if (!nca9555_bus) {
			ret = nca9555_get_bus();
			if (ret) {
				printf("NCA9555: cannot get bus (round %d, ret=%d), "
				       "retrying in %dms\n",
				       round + 1, ret, INIT_RETRY_DELAY_MS);
				udelay(INIT_RETRY_DELAY_MS * 1000);
				continue;
			}
		}

		nca9555_dump_regs("before init");

		ret = nca9555_write(NCA9555_REG_POL0, 0x00);
		if (ret)
			goto retry;
		ret = nca9555_write(NCA9555_REG_POL1, 0x00);
		if (ret)
			goto retry;
		printf("NCA9555: polarity inversion cleared\n");

		ret = nca9555_read(NCA9555_REG_OUT0, &val);
		if (ret)
			goto retry;
		printf("NCA9555: Output Port 0 before: 0x%02x\n", val);

		val |= BIT(0);
		val |= BIT(1);
		val |= BIT(7);
		ret = nca9555_write(NCA9555_REG_OUT0, val);
		if (ret)
			goto retry;
		printf("NCA9555: USB power OFF (Output Port 0: 0x%02x)\n", val);

		udelay(100000);

		val &= ~BIT(0);
		val &= ~BIT(1);
		val &= ~BIT(7);
		ret = nca9555_write(NCA9555_REG_OUT0, val);
		if (ret)
			goto retry;
		printf("NCA9555: USB power ON  (Output Port 0: 0x%02x)\n", val);

		ret = nca9555_read(NCA9555_REG_OUT1, &val);
		if (ret)
			goto retry;
		printf("NCA9555: Output Port 1 before: 0x%02x\n", val);
		val |= BIT(NCA9555_GPIO_PCIE_RESET % 8);
		ret = nca9555_write(NCA9555_REG_OUT1, val);
		if (ret)
			goto retry;
		printf("NCA9555: Output Port 1 after:  0x%02x  (PCIe reset HIGH)\n",
		       val);

		ret = nca9555_read(NCA9555_REG_CFG0, &val);
		if (ret)
			goto retry;
		printf("NCA9555: Config Port 0 before: 0x%02x\n", val);
		val &= ~BIT(0);
		val &= ~BIT(1);
		val &= ~BIT(7);
		ret = nca9555_write(NCA9555_REG_CFG0, val);
		if (ret)
			goto retry;
		printf("NCA9555: Config Port 0 after:  0x%02x  (GPIO 0,1,7 = output)\n",
		       val);

		ret = nca9555_read(NCA9555_REG_CFG1, &val);
		if (ret)
			goto retry;
		printf("NCA9555: Config Port 1 before: 0x%02x\n", val);
		val &= ~BIT(NCA9555_GPIO_PCIE_RESET % 8);
		ret = nca9555_write(NCA9555_REG_CFG1, val);
		if (ret)
			goto retry;
		printf("NCA9555: Config Port 1 after:  0x%02x  (GPIO %d = output)\n",
		       val, NCA9555_GPIO_PCIE_RESET);

		udelay(10000);

		nca9555_dump_regs("after init");

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

int board_init(void)
{
	return 0;
}

int misc_init_r(void)
{
	int ret;

	printf("BDY G98: misc_init_r() entered\n");

	ret = nca9555_early_init();
	if (ret)
		printf("BDY G98: NCA9555 init failed (ret=%d), continuing boot\n",
		       ret);
	else
		printf("BDY G98: NCA9555 init OK\n");

	return 0;
}
