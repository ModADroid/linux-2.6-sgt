#!/bin/bash

DATE=$(date +%m%d)
CONFIG="galaxytab"
rm "$DATE"_test_*.zip
rm "$DATE"_test_*.tar
rm -rf ../galaxytab_initramfs/.git

make mrproper
make clean
rm update/*.zip update/kernel_update/zImage

make ARCH=arm jt1134_"$CONFIG"_defconfig
make -j8 CROSS_COMPILE=../arm-2009q3/bin/arm-none-linux-gnueabi- \
	ARCH=arm HOSTCFLAGS="-g -O3"

cp arch/arm/boot/zImage update/kernel_update/zImage
cd update
zip -r kernel_update.zip . 
mv kernel_update.zip ../"$DATE"_test_"$CONFIG".zip

cd kernel_update
tar --format=ustar -cf kernel_update.tar zImage
mv kernel_update.tar ../../"$DATE"_test_"$CONFIG".tar
cd ../..

