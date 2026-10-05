#!/usr/bin/bash

set -xe

WORKDIR=`pwd`
DEFCONFIG="aiot-3588ied_defconfig"
JOBS=$(nproc)
TIMESTAMP=$(date +%Y%m%d)

cd "$WORKDIR"
export CROSS_COMPILE=aarch64-linux-gnu-
export RKBIN_DIR="${WORKDIR}/rkbin/bin/rk35"
export BL31="${WORKDIR}/rkbin/bin/rk35/rk3588_bl31_v1.54.elf"
export ROCKCHIP_TPL="${WORKDIR}/rkbin/bin/rk35/rk3588_ddr_lp4_1800MHz_lp5_2400MHz_v1.21.bin"
# export TEE="${WORKDIR}/rkbin/bin/rk35/rk3588_bl32_v1.20.bin"

ls -alh ${BL31}
ls -alh ${ROCKCHIP_TPL}
sha256sum ${BL31}
sha256sum ${ROCKCHIP_TPL}

sed -i "s#AIoT 3588IED Compiled By yifengyou.*#AIoT 3588IED Compiled By yifengyou v$(date +%Y.%m.%d-%H:%M:%S)\";#" ./dts/upstream/src/arm64/rockchip/rk3588-aiot-3588ied.dts

cd "$WORKDIR"
rm -rf output
mkdir -p output

cd "$WORKDIR"
make mrproper
make "$DEFCONFIG"
make -j"$JOBS"

cp -a idbloader.img ${WORKDIR}/output/idbloader.img
cp -a u-boot.itb ${WORKDIR}/output/uboot.img
cp -a u-boot.itb ${WORKDIR}/output/uboot_${TIMESTAMP}.img

ls -alh   output/
ls -alh   dts/upstream/src/arm64/rockchip/rk3588-aiot-3588ied.dts
ls -alh   configs/aiot-3588ied_defconfig

echo "All done!"
