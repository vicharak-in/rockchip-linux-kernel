#!/usr/bin/env bash
# shellcheck disable=SC2199,SC2086
# shellcheck source=/dev/null

#
# SPDX-License-Identifier: MIT
# Copyright (C) 2020-23 Utsav Balar
#

DATE=$(date +"%d.%m.%y")

OUT_DIR=$(pwd)/out

# Packages required to compile using gcc are:
# bc bison flex build-essential gcc libssl-dev libncurses5-dev
#
# Packages required to compile using clang are same.
# Except the llvm and clang are used from a standalone toolchain

FILENAME="${OUT_DIR}/boot-${DATE}.img"
KERNEL_IMAGE="${OUT_DIR}/arch/arm64/boot/Image"
DEFCONFIG="rockchip_linux_defconfig"

# check if -axon flag is passed for axon
if [[ "$@" =~ "axon"* ]]; then
	echo -e "###### USING axon DTB #####"
	DTB_FILE="${OUT_DIR}/arch/arm64/boot/dts/rockchip/rk3588-axon.dtb"
else
	echo -e "###### USING VAAMAN DTB #####"
	DTB_FILE="${OUT_DIR}/arch/arm64/boot/dts/rockchip/rk3399-vaaman.dtb"
fi

# check if -c flag is passed for clang
while getopts "c" opt; do
	case "${opt}" in
	c)
		CLANG=1
		echo "###### BUILDING KERNEL USING CLANG! ######"
		;;
	*)
		CLANG=0
		echo "###### BUILDING KERNEL USING GCC! ######"
		;;
	esac
done

# Pac1k Image into boot image
function pack_image() {
	if [[ "$@" =~ "axon"* ]]; then
		mkfs.vfat -n "boot" -S 512 -C "${FILENAME}" $((60 * 1024))
	else
		mkfs.vfat -n "boot" -S 512 -C "${FILENAME}" $((50 * 1024))
	fi

	if [ -d "$(pwd)"/extlinux ]; then
		cp -r "$(pwd)"/extlinux/ "${OUT_DIR}"
	else
		echo "Extlinux directory does not exist!"
	fi

	cd "${OUT_DIR}" || exit

	if [ -f "$(which mmd)" ]; then
		mmd -i "${FILENAME}" ::/extlinux
		mcopy -i "${FILENAME}" -s $EXTLINUX_CONF ::/extlinux/extlinux.conf
		mcopy -i "${FILENAME}" -s "${KERNEL_IMAGE}" ::
		mcopy -i "${FILENAME}" -s "${DTB_FILE}" ::/vicharak.dtb
	else
		echo "please install mtools!"
	fi
}

# Cleanup previous build files
function cleanup() {
	if [ -f "${FILENAME}" ]; then
		rm -f "${FILENAME}"
	fi
	if [ -f "${KERNEL_IMAGE}" ]; then
		rm -f "${KERNEL_IMAGE}"
	fi
	if [ -f "${DTB_FILE}" ]; then
		rm -f "${DTB_FILE}"
	fi
	if [ -d "${OUT_DIR}"/extlinux ]; then
		rm -rf "${OUT_DIR}"/extlinux
	fi
}

# check if gcc is installed
if [ -z "$(which gcc)" ]; then
	echo "gcc is not installed!"
	exit 1
fi

if [ -e "${CLANG}" ]; then
	echo "clang is installed!"
	if [ ! -d "$(pwd)"/../clang ]; then
		# check if prebuilt clang is installed
		if [ -n "$(which clang)" ]; then
			git clone \
				https://github.com/crdroidandroid/android_prebuilts_clang_host_linux-x86_clang-6364210 \
				-b 10.0 \
				"$(pwd)"/../clang

			PATH="$(pwd)"/../clang/bin:${PATH}
			export PATH
		fi
	fi

	ARGS="ARCH=arm64 \
		O=${OUT_DIR} \
		LLVM=1 \
		CROSS_COMPILE=aarch64-linux-gnu- \
		-j$(nproc --all)"

else
	PATH=$(pwd)/../gcc-arm64/bin:$(pwd)/../gcc-arm/bin:${PATH}
	ARGS="ARCH=arm64 \
		O=${OUT_DIR} \
		CROSS_COMPILE=aarch64-elf- \
		CROSS_COMPILE_ARM32=arm-eabi- \
		-j$(nproc --all)"

fi

if [[ "$@" =~ "debian"* ]]; then
	echo -e "###### USING DEBIAN EXTLINUX CONFIG #####"
	EXTLINUX_CONF=extlinux/vicharak-debian.conf
elif [[ "$@" =~ "debian11"* ]]; then
	echo -e "###### USING DEBIAN 11 EXTLINUX CONFIG #####"
	EXTLINUX_CONF=extlinux/vicharak.conf
elif [[ "$@" =~ "mate"* ]]; then
	echo -e "###### USING MANJARO MATE EXTLINUX CONFIG #####"
	EXTLINUX_CONF=extlinux/vicharak-manjaro-mate.conf
elif [[ "$@" =~ "reborn"* ]]; then
	echo -e "###### USING REBORN OS EXTLINUX CONFIG #####"
	EXTLINUX_CONF=extlinux/vicharak-reborn.conf
else
	echo -e "###### USING MANJARO EXTLINUX CONFIG #####"
	EXTLINUX_CONF=extlinux/vicharak-manjaro.conf
fi

# Cleanup previous build
cleanup

# Make defconfig
make ${ARGS} ${DEFCONFIG}

# Make kernel image
if [ -f "$(which ccache)" ]; then
	if [ "$CLANG" == "1" ]; then
		make ${ARGS} LLVM=1 CC="ccache clang"
	else
		make ${ARGS}
	fi
else
	make ${ARGS}
fi

# Create boot image
pack_image "$@"

# check $FILENAME size
if [ -f "${FILENAME}" ] && [ "$(stat -c%s "${FILENAME}")" -lt 50000000 ]; then
	echo "Kernel Compilation failed!"
	exit 1
else
	echo "Kernel compiled successfully!"
fi
