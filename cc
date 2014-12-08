#!/bin/sh
#CROSS_COMPILE=/opt/toolchain/gcc-linaro-aarch64-linux-gnu-4.8-2014.04_linux/bin/aarch64-linux-gnu-
#BL33
make clean
rm -fr build/lcb
make PLAT=lcb

make -C tools/fip_create
./tools/fip_create/fip_create fip.bin --dump --bl2 build/lcb/release/bl2.bin

cp -f build/lcb/release/bl1.bin /opt/workspace/boot/lboot/
cp -f fip.bin /opt/workspace/boot/lboot/
