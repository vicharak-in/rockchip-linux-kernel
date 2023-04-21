#!/usr/bin/env bash

# SPDX-License-Identifier: MIT
# Copyright (C) 2023 Utsav Balar
#

OUT_DIR=out

ARGS="
	O=$OUT_DIR \
	ARCH=arm64 \
	CROSS_COMPILE=aarch64-linux-gnu- \
	-j$(nproc --all)
"

# Build the defconfig
make $ARGS rockchip_linux_defconfig

# Build the savedefconfig to get the updated defconfig
make $ARGS savedefconfig

# Move the updated defconfig to the configs directory
mv "${OUT_DIR}/defconfig" "arch/arm64/configs/rockchip_linux_defconfig"
