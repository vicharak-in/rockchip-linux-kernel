# SPDX-License-Identifier: GPL-2.0
dtbo-$(CONFIG_ARCH_ROCKCHIP) += \
	rk3399-vaaman-edp.dtbo \
	rk3399-vaaman-csi-camera.dtbo \
	rk3399-vaaman-leds.dtbo

dtbotxt-$(CONFIG_ARCH_ROCKCHIP) += \
	README.rockchip-overlays

targets += $(dtbo-y) $(dtbotxt-y)

always			:= $(dtbo-y) $(dtbotxt-y)
clean-files		:= *.dtbo
