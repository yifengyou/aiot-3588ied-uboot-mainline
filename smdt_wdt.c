/*
 * smdt_wdt.c - SMDT STM8 MCU watchdog feeder (userspace)
 *
 * Target: 视美泰 AIOT-3588A / AIOT-3588IE (RK3588) 板载 STM8S003F3P6 MCU
 *
 * The MCU sits on i2c6 (i2c@fec80000) at address 0x62.
 * Register protocol (same as vendor smdt_wdt):
 *   0x3a read  -> 0x89          MCU presence/check value
 *   0x32 write <- 0x01 / 0x00   enable / disable watchdog
 *   0x51 write <- 0x33          watchdog config
 *   0x33 write <- 0xab          feed
 *
 * Differences vs the vendor sample:
 *   - fixed  for (retries == 2; ...)  typo
 *   - does not hard-code /dev/i2c-6: prefers bus 6, otherwise scans all
 *     /dev/i2c-* and identifies the MCU by reading reg 0x3a == 0x89
 *   - retries the enable sequence until it succeeds, and keeps feeding
 *   - handles SIGTERM/SIGINT by disabling the watchdog (so a clean
 *     shutdown/reboot is not turned into an MCU reset)
 *   - logs to /dev/kmsg as well as stderr, because on fnOS /dev/console may
 *     point at tty1 (console=... console=tty1) where nothing is visible
 *
 * Build:
 *   aarch64-linux-gnu-gcc -O2 -Wall -static -o smdt_wdt smdt_wdt.c
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>

#define MCU_I2C_ADDR     0x62

#define MCU_CHECK_REG    0x3a
#define MCU_CHECK_VAL    0x89

#define WDT_CTRL_REG     0x32
#define WDT_CTRL_ON      0x01
#define WDT_CTRL_OFF     0x00

#define WDT_CFG_REG      0x51
#define WDT_CFG_VAL      0x33

#define WDT_FEED_REG     0x33
#define WDT_FEED_VAL     0xab

#define DEFAULT_BUS      6
#define MAX_BUS          32
#define FEED_INTERVAL_S  20

/* nca9555(i2c6 @0x20) 的 pca953x 驱动开机早期 probe 因 i2c6 未就绪而失败(-110)，
 * 导致 USB 供电(gpio-hog)与 pcie reset(NVMe) 全失效。系统稳定后手动 bind 可成功，
 * 这里在启动后延时反复触发 bind 直到绑定成功。 */
#define PCA_BIND_ADDR       "6-0020"
#define PCA_BIND_START_S    8
#define PCA_BIND_RETRY_S    8

/*
 * Peripheral power-on is deliberately delayed: doing it at ~2-3 s into boot
 * (right after the watchdog is enabled) makes the board reset in a loop.
 * It is safe once the system has settled, so we wait this many feed rounds
 * (each FEED_INTERVAL_S) before switching usb0_power on.
 */
#define PERIPH_DELAY_ROUNDS  1

/*
 * On-board peripheral power switches live on the same i2c bus as the MCU,
 * on the nca9555 IO expander at 0x20:
 *   usb0_power = nca9555 pin0  -> USB 5V supply enable
 * The vendor (5.10) kernel enables this from its "smdtio" driver, which fnOS
 * does not have.  We do it here with single-register writes, because this
 * nca9555 does not accept the multi-byte write that the in-kernel pca953x
 * driver issues (that is why pca953x probe fails with -110).
 */
#define PERIPH_I2C_ADDR      0x20
#define NCA9555_OUT0_REG     0x02
#define NCA9555_CFG0_REG     0x06
#define NCA9555_CFG0_PIN0_OUT 0xfe	/* pin0 = output, rest = input */
#define NCA9555_OUT0_PIN0_HI  0x01	/* pin0 = high */

static int g_fd = -1;
static int g_kmsg = -1;
static volatile sig_atomic_t g_stop = 0;

/* log to both stderr and /dev/kmsg (visible in dmesg even when /dev/console
 * points at an invisible tty) */
static void klogf(const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	if (n > (int)sizeof(buf) - 1)
		n = (int)sizeof(buf) - 1;
	while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
		n--;

	if (g_kmsg < 0)
		g_kmsg = open("/dev/kmsg", O_WRONLY);
	if (g_kmsg >= 0 && n > 0) {
		char rec[260];

		memcpy(rec, buf, n);
		rec[n++] = '\n';
		if (write(g_kmsg, rec, n) < 0)
			; /* ignore */
	}

	fputs(buf, stderr);
	fflush(stderr);
}

static void on_signal(int sig)
{
	(void)sig;
	g_stop = 1;
}

static int i2c_open_bus(int bus)
{
	char path[32];
	int fd;

	snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
	fd = open(path, O_RDWR);
	if (fd < 0)
		return -1;
	if (ioctl(fd, I2C_SLAVE, MCU_I2C_ADDR) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static int i2c_write_reg(int fd, uint8_t reg, uint8_t val)
{
	uint8_t buf[2] = { reg, val };

	if (write(fd, buf, sizeof(buf)) == (ssize_t)sizeof(buf))
		return 0;
	return -1;
}

static int i2c_read_reg(int fd, uint8_t reg, uint8_t *val)
{
	if (write(fd, &reg, 1) != 1)
		return -1;
	if (read(fd, val, 1) != 1)
		return -1;
	return 0;
}

/* Return bus number of the SMDT MCU, or -1. Prefer bus 6. */
static int find_mcu_bus(void)
{
	uint8_t v;
	int bus, fd;

	fd = i2c_open_bus(DEFAULT_BUS);
	if (fd >= 0) {
		if (i2c_read_reg(fd, MCU_CHECK_REG, &v) == 0 && v == MCU_CHECK_VAL) {
			close(fd);
			klogf("smdt_wdt: MCU found on /dev/i2c-%d (0x%02x)\n",
				DEFAULT_BUS, v);
			return DEFAULT_BUS;
		}
		close(fd);
	}

	for (bus = 0; bus < MAX_BUS; bus++) {
		if (bus == DEFAULT_BUS)
			continue;
		fd = i2c_open_bus(bus);
		if (fd < 0)
			continue;
		if (i2c_read_reg(fd, MCU_CHECK_REG, &v) == 0 && v == MCU_CHECK_VAL) {
			close(fd);
			klogf("smdt_wdt: MCU found on /dev/i2c-%d (0x%02x)\n",
				bus, v);
			return bus;
		}
		close(fd);
	}
	return -1;
}

static int wdt_write(uint8_t reg, uint8_t val, int retries)
{
	int i;

	for (i = 0; i < retries; i++) {
		if (i2c_write_reg(g_fd, reg, val) == 0)
			return 0;
		usleep(10 * 1000);
	}
	return -1;
}

static int wdt_disable(void)
{
	return wdt_write(WDT_CTRL_REG, WDT_CTRL_OFF, 5);
}

static int wdt_prepare(void)
{
	wdt_disable();
	if (wdt_write(WDT_CTRL_REG, WDT_CTRL_ON, 5) < 0)
		return -1;
	usleep(9 * 1000);
	if (wdt_write(WDT_CFG_REG, WDT_CFG_VAL, 5) < 0)
		return -1;
	usleep(8 * 1000);
	return 0;
}

static int wdt_feed(void)
{
	return wdt_write(WDT_FEED_REG, WDT_FEED_VAL, 5);
}

/* ---- on-board peripheral power-on (nca9555 @0x20 on the same bus) ---- */

/* single-register write: [reg, val] in one i2c transaction */
static int periph_write_reg(int bus, uint8_t reg, uint8_t val)
{
	char path[32];
	uint8_t buf[2] = { reg, val };
	int fd, ret;

	snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
	fd = open(path, O_RDWR);
	if (fd < 0)
		return -1;
	if (ioctl(fd, I2C_SLAVE, PERIPH_I2C_ADDR) < 0) {
		close(fd);
		return -1;
	}
	ret = (write(fd, buf, sizeof(buf)) == (ssize_t)sizeof(buf)) ? 0 : -1;
	close(fd);
	return ret;
}

static int periph_write_retry(int bus, uint8_t reg, uint8_t val)
{
	int i;

	for (i = 0; i < 5; i++) {
		if (periph_write_reg(bus, reg, val) == 0)
			return 0;
		usleep(20 * 1000);
	}
	return -1;
}

/*
 * Switch on the peripherals that are gated by the nca9555.  Currently only
 * usb0_power (pin0, USB 5V).  Safe to call repeatedly.
 */
static int periph_power_on(int bus)
{
	uint8_t v;

	if (periph_write_retry(bus, NCA9555_CFG0_REG, NCA9555_CFG0_PIN0_OUT) < 0) {
		klogf("smdt_wdt: nca9555 cfg0 write failed: %s\n", strerror(errno));
		return -1;
	}
	usleep(10 * 1000);
	if (periph_write_retry(bus, NCA9555_OUT0_REG, NCA9555_OUT0_PIN0_HI) < 0) {
		klogf("smdt_wdt: nca9555 out0 write failed: %s\n", strerror(errno));
		return -1;
	}

	/* read back for confirmation */
	{
		char path[32];
		int fd;
		uint8_t cfg = 0, out = 0;

		snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
		fd = open(path, O_RDWR);
		if (fd >= 0 && ioctl(fd, I2C_SLAVE, PERIPH_I2C_ADDR) == 0) {
			if (i2c_read_reg(fd, NCA9555_CFG0_REG, &cfg) < 0)
				cfg = 0xff;
			if (i2c_read_reg(fd, NCA9555_OUT0_REG, &out) < 0)
				out = 0xff;
		}
		if (fd >= 0)
			close(fd);
		(void)v;
		klogf("smdt_wdt: usb0_power on (nca9555 pin0 high, cfg=0x%02x out=0x%02x)\n",
			cfg, out);
	}
	return 0;
}

/* best-effort readback of a few MCU registers, for diagnostics only */
static void wdt_status(const char *tag)
{
	uint8_t v;
	char line[160];
	int n = 0;

	line[0] = '\0';
	if (i2c_read_reg(g_fd, 0xb2, &v) == 0)
		n += snprintf(line + n, sizeof(line) - (size_t)n, " 0xb2=0x%02x", v);
	if (i2c_read_reg(g_fd, 0x3a, &v) == 0)
		n += snprintf(line + n, sizeof(line) - (size_t)n, " 0x3a=0x%02x", v);
	if (i2c_read_reg(g_fd, 0x32, &v) == 0)
		n += snprintf(line + n, sizeof(line) - (size_t)n, " 0x32=0x%02x", v);
	klogf("smdt_wdt: %s:%s\n", tag, line);
}

/* ---- nca9555 / pca953x 延时绑定 ---- */
static int pca_is_bound(void)
{
	struct stat st;
	return lstat("/sys/bus/i2c/devices/" PCA_BIND_ADDR "/driver", &st) == 0;
}

static int pca_try_bind(void)
{
	int fd;
	/* switch_root 后 initramfs 的 /sys 可能被移走，重新挂一次(失败可忽略) */
	mount("sysfs", "/sys", "sysfs", 0, NULL);
	fd = open("/sys/bus/i2c/drivers/pca953x/bind", O_WRONLY);
	if (fd < 0)
		return -1;
	if (write(fd, PCA_BIND_ADDR, sizeof(PCA_BIND_ADDR) - 1) < 0) {
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	int bus, enabled = 0, announced = 0, fed = 0, periph_done = 0;
	int pca_bound = 0;
	time_t pca_next = 0;
	int periph_rounds = 0;

	(void)argc;
	(void)argv;

	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	signal(SIGHUP, on_signal);

	klogf("smdt_wdt: starting (preferred bus %d, addr 0x%02x)\n",
		DEFAULT_BUS, MCU_I2C_ADDR);

	/* The i2c adapter/MCU may not be ready yet when the initramfs starts us,
	 * so keep looking for it instead of exiting. */
	for (;;) {
		bus = find_mcu_bus();
		if (bus >= 0)
			break;
		if (!announced) {
			klogf("smdt_wdt: SMDT MCU not found yet, retrying\n");
			announced = 1;
		}
		if (g_stop)
			return 0;
		sleep(1);
	}

	g_fd = i2c_open_bus(bus);
	if (g_fd < 0) {
		klogf("smdt_wdt: cannot open /dev/i2c-%d: %s\n",
			bus, strerror(errno));
		return 1;
	}
	klogf("smdt_wdt: using /dev/i2c-%d\n", bus);

	pca_next = time(NULL) + PCA_BIND_START_S;

	if (wdt_prepare() == 0) {
		enabled = 1;
		klogf("smdt_wdt: watchdog enabled, feeding every %ds\n",
			FEED_INTERVAL_S);
		wdt_feed();		/* feed immediately after enabling */
	} else {
		klogf("smdt_wdt: watchdog enable failed, will retry\n");
	}

	/*
	 * NOTE: peripheral power-on is intentionally delayed (PERIPH_DELAY_ROUNDS
	 * feed rounds) and run in a child process.  Doing the nca9555 write at
	 * ~3 s into boot made the board reset in a loop, and this also keeps a
	 * stuck i2c transaction from stopping the watchdog feeding.
	 */

	while (!g_stop) {
		if (!enabled) {
			if (wdt_prepare() == 0) {
				enabled = 1;
				klogf("smdt_wdt: watchdog enabled\n");
				wdt_feed();
				wdt_status("after enable");
			}
		} else if (wdt_feed() < 0) {
			klogf("smdt_wdt: feed failed: %s\n",
				strerror(errno));
			/* keep the previous enabled state; retry next round */
		} else if (!fed) {
			fed = 1;
			klogf("smdt_wdt: first feed ok\n");
		}

		/* sleep in 1s slices so signals are handled promptly;
		 * 每秒顺便检查/触发 nca9555 的 pca953x 绑定 */
		{
			int i;

			for (i = 0; i < FEED_INTERVAL_S && !g_stop; i++) {
				if (!pca_bound) {
					time_t now = time(NULL);

					if (pca_is_bound()) {
						pca_bound = 1;
						klogf("smdt_wdt: nca9555 (pca953x) bound\n");
					} else if (now >= pca_next) {
						pca_next = now + PCA_BIND_RETRY_S;
						if (pca_try_bind() == 0)
							klogf("smdt_wdt: triggered pca953x bind (6-0020)\n");
					}
				}
				sleep(1);
			}
		}
	}

	klogf("smdt_wdt: signal received, disabling watchdog\n");
	wdt_disable();
	close(g_fd);
	return 0;
}
