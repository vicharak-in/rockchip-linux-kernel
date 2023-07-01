# SPDX-License-Identifier: GPL-2.0
dtbo-$(CONFIG_ARCH_ROCKCHIP) += \
	rk3399-vaaman-cdn-dp.dtbo \
	rk3399-vaaman-camera-imx219.dtbo \
	rk3399-vaaman-camera-ov5647.dtbo \
	rk3399-vaaman-dwc3-0-host.dtbo \
	rk3399-vaaman-dwc3-0-otg.dtbo \
	rk3399-vaaman-edp.dtbo \
	rk3399-vaaman-hdmi.dtbo \
	rk3399-vaaman-i2c2.dtbo \
	rk3399-vaaman-i2c6.dtbo \
	rk3399-vaaman-leds.dtbo \
	rk3399-vaaman-opp-2016.dtbo \
	rk3399-vaaman-pwm0.dtbo \
	rk3399-vaaman-pwm1.dtbo \
	rk3399-vaaman-uart2.dtbo \
	rk3399-vaaman-uart4.dtbo

dtbotxt-$(CONFIG_ARCH_ROCKCHIP) += \
	README.rockchip-overlays

targets += $(dtbo-y) $(dtbotxt-y)

always			:= $(dtbo-y) $(dtbotxt-y)
clean-files		:= *.dtbo
