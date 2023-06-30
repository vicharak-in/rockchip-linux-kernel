#!/usr/bin/env bash
# shellcheck disable=SC1090,SC1091
#
# SPDX-License-Identifier: MIT
# Copyright (C) 2020-23 Utsav Balar <utsavbalar1231@gmail.com>
# Version: 4.0
#

# Set bash shell options
set -eE

# Set locale to C to avoid issues with some build scripts
export LC_ALL=C

# Command used for this script
CMD=$(realpath "${0}")
# Kernel directory path
KERNEL_DIR=$(dirname "${CMD}")

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

OPTIONS=("${@:-info}")
for option in "${OPTIONS[@]}"; do
	print "processing option: $option"
	case ${option} in
	*.mk)
		if [ -f "${option}" ]; then
			config=${option}
		else
			config=$(find "${CFG_DIR}" -name "${option}")
			print "switching to board: ${config}"
			if [ ! -f "${config}" ]; then
				print "not exist!"
				exit 1
			fi
		fi
		DEVICE_MAKEFILE="${config}"
		export DEVICE_MAKEFILE

		if [ -f "${KERNEL_DIR}"/vicharak/.device.mk ]; then
			rm -rf "${KERNEL_DIR}"/vicharak/.device.mk
		fi

		print_info
		;;
	lunch) lunch_device ;;
	info) print_info ;;
	clean) cleanup ;;
	kernel) build_kernel ;;
	update_defconfig) update_defconfig ;;
	*)
		usage
		exit_with_error "Invalid option: ${option}"
		;;
	esac
done
