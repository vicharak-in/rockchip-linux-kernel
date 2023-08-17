# shellcheck shell=bash
# SPDX-License-Identifier: MIT
# Copyright (C) 2023 Utsav Balar

# Makefile for Vicharak Galactos Ubuntu (AMD Ryzen 9 7950X)
# This file contains variables used for building Vicharak's galactos kernel
# To disable build options, comment the line or set it to false

# Device specific
DEVICE_NAME="galactos_ubuntu"
DEVICE_DTB_FILE=
DEVICE_DEFCONFIG="galactos_ubuntu_defconfig"
DEVICE_CONFIG_FRAGMENT=
DEVICE_ARCH="x86_64"
DEVICE_KERNEL_IMAGE_FILE="${OUT_DIR}/arch/${DEVICE_ARCH}/boot/bzImage"
DEVICE_DTB_DIR=

# Build options
# To build kernel with performance configuration
PERF_BUILD=false
# To build kernel with clang
CLANG_BUILD=true
# Build modules along with kernel
MODULES_BUILD=true
# Build debian package
DEB_BUILD=true
# Pack kernel image using extlinux
PACK_KERNEL_BUILD=false
