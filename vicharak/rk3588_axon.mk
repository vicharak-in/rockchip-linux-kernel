# shellcheck shell=bash
# SPDX-License-Identifier: MIT
# Copyright (C) 2023 Utsav Balar

# Makefile for rk3588_axon
# This file contains variables used for building rk3588_axon kernel
# To disable build options, comment the line or set it to false

# Device specific
DEVICE_NAME="rk3588_axon"
DEVICE_DTB_FILE="rk3588-axon-linux"
DEVICE_DEFCONFIG="rockchip_linux_defconfig"
DEVICE_CONFIG_FRAGMENT="rk3588_axon.config"
DEVICE_ARCH="arm64"
DEVICE_KERNEL_IMAGE_FILE="${OUT_DIR}/arch/${DEVICE_ARCH}/boot/Image"
DEVICE_DTB_DIR="${OUT_DIR}/arch/${DEVICE_ARCH}/boot/dts/rockchip"

# Build options
# To build kernel with performance configuration
PERF_BUILD=false
# To build kernel with clang
CLANG_BUILD=true
# Build modules along with kernel
MODULES_BUILD=true
# Build debian package
DEB_BUILD=false
# Pack kernel image using extlinux
PACK_KERNEL_BUILD=true
