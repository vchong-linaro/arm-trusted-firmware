#!/bin/sh
#CROSS_COMPILE=/opt/toolchain/gcc-linaro-aarch64-linux-gnu-4.8-2014.04_linux/bin/aarch64-linux-gnu-
#BL33
DEBUG_VERSION=1
BL33=/opt/workspace/boot/uefi/edk2/Build/Lcb/DEBUG_GCC49/FV/BL33_AP_UEFI.fd

make clean
rm -fr build/lcb

if [ $DEBUG_VERSION ]; then
	make PLAT=lcb DEBUG=1 LOG_LEVEL=50 CRASH_REPORTING=1
	BUILD_PATH=debug
	# Use BL2 instead to test whether BL33 could run
	#BL33=build/lcb/$BUILD_PATH/bl2.bin
	#BL33=/opt/workspace/boot/lboot/bl33/bl33.bin
else
	make PLAT=lcb
	BUILD_PATH=release
fi
if [ $? -ne 0 ]; then
	exit 1
fi

make -C tools/fip_create
if [ $? -ne 0 ]; then
	exit 1
fi

./tools/fip_create/fip_create fip.bin --dump --bl2 build/lcb/$BUILD_PATH/bl2.bin --bl31 build/lcb/$BUILD_PATH/bl31.bin --bl33 $BL33
if [ $? -ne 0 ]; then
	exit 1
fi

cp -f build/lcb/$BUILD_PATH/bl1.bin /opt/workspace/boot/lboot/
cp -f fip.bin /opt/workspace/boot/lboot/
