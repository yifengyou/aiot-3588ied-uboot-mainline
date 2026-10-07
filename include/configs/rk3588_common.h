/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * (C) Copyright 2021 Rockchip Electronics Co., Ltd
 * Copyright (c) 2023 Edgeble AI Technologies Pvt. Ltd.
 */

#ifndef __CONFIG_RK3588_COMMON_H
#define __CONFIG_RK3588_COMMON_H

#include "rockchip-common.h"

#define CFG_IRAM_BASE			0xff000000

#define CFG_SYS_SDRAM_BASE		0
#define SDRAM_MAX_SIZE			0xf0000000

#ifndef ROCKCHIP_DEVICE_SETTINGS
#define ROCKCHIP_DEVICE_SETTINGS
#endif

#define ENV_MEM_LAYOUT_SETTINGS		\
	"scriptaddr=0x00c00000\0"	\
	"script_offset_f=0xffe000\0"	\
	"script_size_f=0x2000\0"	\
	"pxefile_addr_r=0x00e00000\0"	\
	"kernel_addr_r=0x02000000\0"	\
	"kernel_comp_addr_r=0x0a000000\0"	\
	"fdt_addr_r=0x12000000\0"	\
	"fdtoverlay_addr_r=0x12100000\0"	\
	"ramdisk_addr_r=0x12180000\0"	\
	"kernel_comp_size=0x8000000\0"

#define CFG_EXTRA_ENV_SETTINGS		\
	"fdtfile=" CONFIG_DEFAULT_FDT_FILE "\0"	\
	ENV_MEM_LAYOUT_SETTINGS		\
	ROCKCHIP_DEVICE_SETTINGS	\
       "boot_targets=" BOOT_TARGETS "\0" \
       "button_cmd_0_name=Recovery key\0" \
       "button_cmd_0=run bootcmd_recovery\0" \
       "bootcmd_recovery=" \
               "sysboot mmc 0:2 any ${scriptaddr} /recovery.conf;\0" \
	"try_bootscr_boot=" \
		"for distro_bootpart in 1 2 3 4 8 5 6 7 9; do " \
			"for prefix in / /boot/; do " \
				"echo Try ${devtype} ${devnum}:${distro_bootpart} ${prefix}boot.scr; " \
				"if test -e ${devtype} ${devnum}:${distro_bootpart} ${prefix}boot.scr; then " \
					"echo Found boot.scr on ${devtype} ${devnum}:${distro_bootpart}; " \
					"load ${devtype} ${devnum}:${distro_bootpart} ${scriptaddr} ${prefix}boot.scr; " \
					"source ${scriptaddr}; " \
					"echo boot.scr returned, trying next...; " \
				"fi; " \
			"done; " \
		"done; \0" \
	"try_extlinux_boot=" \
		"for distro_bootpart in 1 2 3 4 8 5 6 7 9; do " \
			"for extlinux_path in /boot/extlinux/extlinux.conf /extlinux/extlinux.conf /extlinux.conf; do " \
				"echo Try ${devtype} ${devnum}:${distro_bootpart} ${extlinux_path}; " \
				"if test -e ${devtype} ${devnum}:${distro_bootpart} ${extlinux_path}; then " \
					"echo Found extlinux.conf on ${devtype} ${devnum}:${distro_bootpart}; " \
					"sysboot ${devtype} ${devnum}:${distro_bootpart} any ${scriptaddr} ${extlinux_path}; " \
					"echo sysboot returned, trying next...; " \
				"fi; " \
			"done; " \
		"done; \0"                                                                                      \
	"try_recovery_boot="                                                                                    \
		"echo Recovery: scanning ${devtype} ${devnum}; "                                                \
		"if test -e ${devtype} ${devnum}:1 /recovery.conf; then "                                       \
		"echo Found recovery.conf on ${devtype} ${devnum}:1; "                                          \
		"sysboot ${devtype} ${devnum}:1 any ${scriptaddr} /recovery.conf; "                             \
		"echo sysboot returned, trying next...; "                                                       \
		"fi; "                                                                                          \
		"if test -e ${devtype} ${devnum}:1 /boot/recovery.conf; then "                                  \
		"echo Found recovery.conf on ${devtype} ${devnum}:1; "                                          \
		"sysboot ${devtype} ${devnum}:1 any ${scriptaddr} /boot/recovery.conf; "                        \
		"echo sysboot returned, trying next...; "                                                       \
		"fi; "                                                                                          \
		"if test -e ${devtype} ${devnum}:1 /recovery/recovery.conf; then "                              \
		"echo Found recovery.conf on ${devtype} ${devnum}:1; "                                          \
		"sysboot ${devtype} ${devnum}:1 any ${scriptaddr} /recovery/recovery.conf; "                    \
		"echo sysboot returned, trying next...; "                                                       \
		"fi; "                                                                                          \
		"if test -e ${devtype} ${devnum}:2 /recovery.conf; then "                                       \
		"echo Found recovery.conf on ${devtype} ${devnum}:2; "                                          \
		"sysboot ${devtype} ${devnum}:2 any ${scriptaddr} /recovery.conf; "                             \
		"echo sysboot returned, trying next...; "                                                       \
		"fi; "                                                                                          \
		"if test -e ${devtype} ${devnum}:2 /boot/recovery.conf; then "                                  \
		"echo Found recovery.conf on ${devtype} ${devnum}:2; "                                          \
		"sysboot ${devtype} ${devnum}:2 any ${scriptaddr} /boot/recovery.conf; "                        \
		"echo sysboot returned, trying next...; "                                                       \
		"fi; "                                                                                          \
		"if test -e ${devtype} ${devnum}:2 /recovery/recovery.conf; then "                              \
		"echo Found recovery.conf on ${devtype} ${devnum}:2; "                                          \
		"sysboot ${devtype} ${devnum}:2 any ${scriptaddr} /recovery/recovery.conf; "                    \
		"echo sysboot returned, trying next...; "                                                       \
		"fi; "                                                                                          \
		"if test -e ${devtype} ${devnum}:3 /recovery.conf; then "                                       \
		"echo Found recovery.conf on ${devtype} ${devnum}:3; "                                          \
		"sysboot ${devtype} ${devnum}:3 any ${scriptaddr} /recovery.conf; "                             \
		"echo sysboot returned, trying next...; "                                                       \
		"fi; "                                                                                          \
		"if test -e ${devtype} ${devnum}:3 /boot/recovery.conf; then "                                  \
		"echo Found recovery.conf on ${devtype} ${devnum}:3; "                                          \
		"sysboot ${devtype} ${devnum}:3 any ${scriptaddr} /boot/recovery.conf; "                        \
		"echo sysboot returned, trying next...; "                                                       \
		"fi; "                                                                                          \
		"if test -e ${devtype} ${devnum}:3 /recovery/recovery.conf; then "                              \
		"echo Found recovery.conf on ${devtype} ${devnum}:3; "                                          \
		"sysboot ${devtype} ${devnum}:3 any ${scriptaddr} /recovery/recovery.conf; "                    \
		"echo sysboot returned, trying next...; "                                                       \
		"fi; "                                                                                          \
		"if test -e ${devtype} ${devnum}:4 /recovery.conf; then "                                       \
		"echo Found recovery.conf on ${devtype} ${devnum}:4; "                                          \
		"sysboot ${devtype} ${devnum}:4 any ${scriptaddr} /recovery.conf; "                             \
		"echo sysboot returned, trying next...; "                                                       \
		"fi; "                                                                                          \
		"if test -e ${devtype} ${devnum}:4 /boot/recovery.conf; then "                                  \
		"echo Found recovery.conf on ${devtype} ${devnum}:4; "                                          \
		"sysboot ${devtype} ${devnum}:4 any ${scriptaddr} /boot/recovery.conf; "                        \
		"echo sysboot returned, trying next...; "                                                       \
		"fi; "                                                                                          \
		"if test -e ${devtype} ${devnum}:4 /recovery/recovery.conf; then "                              \
		"echo Found recovery.conf on ${devtype} ${devnum}:4; "                                          \
		"sysboot ${devtype} ${devnum}:4 any ${scriptaddr} /recovery/recovery.conf; "                    \
		"echo sysboot returned, trying next...; "                                                       \
		"fi; "                                                                                          \
		"echo Recovery scan complete, no valid recovery.conf found; \0"                                 \
	"boot_one_dev=" \
		"run try_extlinux_boot; " \
		"run try_bootscr_boot; " \
		"run try_rockchip_fw; \0" \
	"try_rockchip_fw=" \
		"mw.l 0x01fffff8 0 1; mw.l 0x04fffff8 0 1; mw.l 0x07000000 0 1; " \
		"read ${devtype} ${devnum}:5 0x01fffff8 0 0x14000; " \
		"if itest.l *0x01fffff8 == 0x4c4e524b; then " \
			"echo RKFW: KRNL kernel found on ${devtype} ${devnum}:5; " \
			"read ${devtype} ${devnum}:6 0x04fffff8 0 0x10000; " \
			"read ${devtype} ${devnum}:4 0x07000000 0 0x1000; " \
			"if itest.l *0x04fffff8 == 0x4c4e524b && itest.l *0x07000000 == 0x45435352 && itest.l *0x07000800 == 0xedfe0dd0; then " \
				"echo RKFW: booting rockchip firmware from ${devtype} ${devnum}; " \
				"if part uuid ${devtype} ${devnum}:8 rkfw_uuid; then " \
					"setenv rkfw_root root=PARTUUID=${rkfw_uuid}; " \
				"elif test ${devtype} = nvme; then " \
					"setenv rkfw_root root=/dev/nvme0n1p8; " \
				"else " \
					"setenv rkfw_root root=/dev/mmcblk${devnum}p8; " \
				"fi; " \
				"setenv bootargs ${rkfw_root} rootfstype=ext4 rootwait rw console=ttyS2,1500000n8 earlycon=uart8250,mmio32,0xfeb50000; " \
				"booti 0x02000000 0x05000000:0x2000000 0x07000800; " \
			"fi; " \
		"fi; \0" \
	"bootcmd_nvme=" \
		"echo NVMe: pci enum; pci enum; " \
		"nvme scan; " \
		"setenv devtype nvme; " \
		"setenv devnum 0; if nvme dev 0; then run boot_one_dev; fi; " \
		"setenv devnum 1; if nvme dev 1; then run boot_one_dev; fi; " \
		"echo NVMe: no nvme bootable media; \0" \
	"bootcmd_usb=" \
		"echo USB: start; usb start; usb info; " \
		"setenv devtype usb; " \
		"setenv devnum 0; if usb dev 0; then run boot_one_dev; fi; " \
		"setenv devnum 1; if usb dev 1; then run boot_one_dev; fi; " \
		"echo USB: no usb bootable media; \0" \
	"bootcmd_scsi=" \
		"echo SCSI: scsi scan; scsi scan; " \
		"setenv devtype scsi; " \
		"setenv devnum 0; if scsi dev 0; then run boot_one_dev; fi; " \
		"setenv devnum 1; if scsi dev 1; then run boot_one_dev; fi; " \
		"echo SCSI: no scsi bootable media; \0" \
	"bootcmd_emmc=" \
		"echo EMMC: scanning; " \
		"setenv devtype mmc; "  \
		"mmc rescan; mmc info; " \
		"setenv devnum 0; if mmc dev 0; then run boot_one_dev; fi; " \
		"setenv devnum 1; if mmc dev 1; then run boot_one_dev; fi; " \
		"setenv devnum 2; if mmc dev 2; then run boot_one_dev; fi; " \
		"echo EMMC: no emmc bootable media; \0" \
	"bootcmd=run bootcmd_usb; run bootcmd_emmc;  run bootcmd_nvme; run bootcmd_scsi; " \
		"echo ERROR: No bootable device found! Enter loader mode; " \
		"rockusb 0 mtd 2; \0"

#undef BOOT_TARGETS
#ifndef BOOT_TARGETS
#define BOOT_TARGETS	"usb nvme scsi"
#endif


#endif /* __CONFIG_RK3588_COMMON_H */
