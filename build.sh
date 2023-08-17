#!/usr/bin/env bash
# shellcheck disable=SC1090,SC1091
#
# SPDX-License-Identifier: MIT
# Copyright (C) 2020-23 Utsav Balar <utsavbalar1231@gmail.com>
# Version: 5.0
#

# Set bash shell options
set -eE

# Set locale to C to avoid issues with some build scripts
export LC_ALL=C

# Command used for this script
CMD=$(realpath "${0}")
# Kernel directory path
KERNEL_DIR=$(dirname "${CMD}")

source "${KERNEL_DIR}"/vicharak/utils
source "${KERNEL_DIR}"/vicharak/variables
source "${KERNEL_DIR}"/vicharak/functions
if [ -f "${KERNEL_DIR}"/vicharak/.device.mk ]; then
	source "${KERNEL_DIR}"/vicharak/.device.mk
fi

if echo "${@}" | grep -wqE "help|-h"; then
	if [ -n "${2}" ] && [ "$(type -t usage"${2}")" == function ]; then
		print "----------------------------------------------------------------"
		print "--- ${2} Build Command ---"
		print "----------------------------------------------------------------"
		eval usage "${2}"
	else
		usage
	fi
	exit 0
fi

# Usage function for this script to show help
function usage() {
	print "--------------------------------------------------------------------------------"
	print "Build script for Vicharak kernel"
	print "Usage: ${0} [OPTIONS]"
	print "Options:"
	print "  lunch                \tLunch device to setup environment"
	print "  info                 \tShow current kernel setup information"
	print "  clean                \tCleanup the kernel build files"
	print "  kernel               \tBuild linux kernel image"
	print "  kerneldeb            \tBuild linux kernel debian package"
	print "  update_defconfig     \tUpdate defconfig with latest changes"
	print "  help                 \tShow this help"
	print ""
	print "Example: ${0} <Option>"
	print "--------------------------------------------------------------------------------"
}

OPTIONS=("${@:-kernel}")
for option in "${OPTIONS[@]}"; do
	print "Processing Option: $option"
	case ${option} in
	*.mk)
		if [ -f "${option}" ]; then
			config=${option}
		else
			config=$(find "${CFG_DIR}" -name "${option}")
			print "switching to board: ${config}"
			if [ ! -f "${config}" ]; then
				exit_with_error "Invalid board: ${option}"
			fi
		fi
		DEVICE_MAKEFILE="${config}"
		export DEVICE_MAKEFILE

		ln -f "${DEVICE_MAKEFILE}" "${KERNEL_DIR}"/vicharak/.device.mk
		source "${KERNEL_DIR}"/vicharak/.device.mk

		print_info
		;;
	lunch) lunch_device ;;
	info) print_info ;;
	clean) cleanup ;;
	kernel) build_kernel ;;
	dtbs)
		if ! is_set "${DEVICE_ARCH}"; then
			exit_with_error "Device architecture not set!"
		fi
		if [ "${DEVICE_ARCH}" == "arm64" ]; then
			build_dtbs
		else
			exit_with_error "DTB build not supported for ${DEVICE_ARCH}"
		fi
		;;
	kerneldeb) build_kerneldeb ;;
	update_defconfig) update_defconfig ;;
	*)
		usage
		exit_with_error "Invalid option: ${option}"
		;;
	esac
done
