# shellcheck shell=bash

# Makefile for rk3399_vaaman
# This file contains variables used for building rk3399_vaaman kernel
# To disable build options, comment the line or set it to false

# Device specific
DEVICE_NAME="rk3399_vaaman"
DEVICE_DTB_FILE="rk3399-vaaman-linux"
DEVICE_DEFCONFIG="rockchip_linux_defconfig"
DEVICE_CONFIG_FRAGMENT="rk3399_vaaman.config"

# Build options
# To build kernel with performance configuration
PERF_BUILD=false
# To build kernel with clang
CLANG_BUILD=true
# Build modules along with kernel
MODULES_BUILD=true
# Build debian package
DEB_BUILD=false
