#!/bin/sh
#CROSS_COMPILE=/opt/toolchain/gcc-linaro-aarch64-linux-gnu-4.8-2014.04_linux/bin/aarch64-linux-gnu-
#BL33
#rm -fr build/lcb
make clean
make PLAT=lcb
cp -f build/lcb/release/bl1.bin /opt/workspace/boot/lboot/
