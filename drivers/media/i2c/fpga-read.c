/*
 * Driver for FPGA Read for VAAMAN
 *
 * Copyright (C) 2014, Andrew Chew <achew@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * V0.0X01.0X01 add enum_frame_interval function.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <linux/gpio/consumer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-mediabus.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x1)

/* FPGA supported geometry */
#define FPGA_TABLE_END		0xffff
#define FPGA_ANALOGUE_GAIN_MULTIPLIER	256
#define FPGA_ANALOGUE_GAIN_MIN	(1 * FPGA_ANALOGUE_GAIN_MULTIPLIER)
#define FPGA_ANALOGUE_GAIN_MAX	(11 * FPGA_ANALOGUE_GAIN_MULTIPLIER)
#define FPGA_ANALOGUE_GAIN_DEFAULT	(2 * FPGA_ANALOGUE_GAIN_MULTIPLIER)

/* In dB*256 */
#define FPGA_DIGITAL_GAIN_MIN		256
#define FPGA_DIGITAL_GAIN_MAX		43663
#define FPGA_DIGITAL_GAIN_DEFAULT	256

#define FPGA_DIGITAL_EXPOSURE_MIN	0
#define FPGA_DIGITAL_EXPOSURE_MAX	4095
#define FPGA_DIGITAL_EXPOSURE_DEFAULT	1575

#define FPGA_EXP_LINES_MARGIN	4

#define FPGA_NAME			"EFINIX"

static const s64 link_freq_menu_items[] = {
	600000000
};

struct fpga_reg {
	u16 addr;
	u8 val;
};

struct fpga_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	const struct fpga_reg *reg_list;
};

/* MCLK:24MHz  1920x1080  30fps   MIPI LANE2 */
static const struct fpga_reg fpga_init_tab_1920_1080_30fps[] = {
	{0x30EB, 0x05},
	{0x30EB, 0x0C},
	{0x300A, 0xFF},
	{0x300B, 0xFF},
	{0x30EB, 0x05},
	{0x30EB, 0x09},
	{0x0114, 0x01},
	{0x0128, 0x00},
	{0x012A, 0x18},
	{0x012B, 0x00},
	{0x0160, 0x06},
	{0x0161, 0xE6},
	{0x0162, 0x0D},
	{0x0163, 0x78},
	{0x0164, 0x02},
	{0x0165, 0xA8},
	{0x0166, 0x0A},
	{0x0167, 0x27},
	{0x0168, 0x02},
	{0x0169, 0xB4},
	{0x016A, 0x06},
	{0x016B, 0xEB},
	{0x016C, 0x07},
	{0x016D, 0x80},
	{0x016E, 0x04},
	{0x016F, 0x38},
	{0x0170, 0x01},
	{0x0171, 0x01},
	{0x0174, 0x00},
	{0x0175, 0x00},
	{0x018C, 0x0A},
	{0x018D, 0x0A},
	{0x0301, 0x05},
	{0x0303, 0x01},
	{0x0304, 0x03},
	{0x0305, 0x03},
	{0x0306, 0x00},
	{0x0307, 0x39},
	{0x0309, 0x0A},
	{0x030B, 0x01},
	{0x030C, 0x00},
	{0x030D, 0x72},
	{0x455E, 0x00},
	{0x471E, 0x4B},
	{0x4767, 0x0F},
	{0x4750, 0x14},
	{0x4540, 0x00},
	{0x47B4, 0x14},
	{FPGA_TABLE_END, 0x00}
};

static const struct fpga_reg start[] = {
	{0x0100, 0x01},		/* mode select streaming on */
	{FPGA_TABLE_END, 0x00}
};

static const struct fpga_reg stop[] = {
	{0x0100, 0x00},		/* mode select streaming off */
	{FPGA_TABLE_END, 0x00}
};

enum {
	TEST_PATTERN_DISABLED,
	TEST_PATTERN_SOLID_BLACK,
	TEST_PATTERN_SOLID_WHITE,
	TEST_PATTERN_SOLID_RED,
	TEST_PATTERN_SOLID_GREEN,
	TEST_PATTERN_SOLID_BLUE,
	TEST_PATTERN_COLOR_BAR,
	TEST_PATTERN_FADE_TO_GREY_COLOR_BAR,
	TEST_PATTERN_PN9,
	TEST_PATTERN_16_SPLIT_COLOR_BAR,
	TEST_PATTERN_16_SPLIT_INVERTED_COLOR_BAR,
	TEST_PATTERN_COLUMN_COUNTER,
	TEST_PATTERN_INVERTED_COLUMN_COUNTER,
	TEST_PATTERN_PN31,
	TEST_PATTERN_MAX
};

static const char *const tp_qmenu[] = {
	"Disabled",
	"Solid Black",
	"Solid White",
	"Solid Red",
	"Solid Green",
	"Solid Blue",
	"Color Bar",
	"Fade to Grey Color Bar",
	"PN9",
	"16 Split Color Bar",
	"16 Split Inverted Color Bar",
	"Column Counter",
	"Inverted Column Counter",
	"PN31",
};

#define SIZEOF_I2C_TRANSBUF 32

struct fpga {
	struct v4l2_subdev subdev;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct clk *clk;
	struct gpio_desc *pwdn_gpio;
	struct v4l2_rect crop_rect;
	int hflip;
	int vflip;
	u8 analogue_gain;
	u16 digital_gain;	/* bits 11:0 */
	u16 exposure_time;
	u16 test_pattern;
	u16 test_pattern_solid_color_r;
	u16 test_pattern_solid_color_gr;
	u16 test_pattern_solid_color_b;
	u16 test_pattern_solid_color_gb;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *pixel_rate;
	const struct fpga_mode *cur_mode;
	u32 cfg_num;
	u16 cur_vts;
	u32 module_index;
	const char *module_facing;
	const char *module_name;
	const char *len_name;
};

static const struct fpga_mode supported_modes[] = {
	{
		.width =  640, //H
		.height = 480, //V
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 640+180,//+88+128+40,
		.vts_def = 480+90,//+23+1+128,
		.reg_list = fpga_init_tab_1920_1080_30fps,
	},
};

static struct fpga *to_fpga(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct fpga, subdev);
}

/* V4L2 subdev video operations */
static int fpga_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

/* V4L2 subdev core operations */
static int fpga_s_power(struct v4l2_subdev *sd, int on)
{
	return 0;
}

/* V4L2 ctrl operations */
static int fpga_s_ctrl_test_pattern(struct v4l2_ctrl *ctrl)
{
	struct fpga *priv =
		container_of(ctrl->handler, struct fpga, ctrl_handler);

	switch (ctrl->val) {
		case TEST_PATTERN_DISABLED:
			priv->test_pattern = 0x0000;
			break;
		case TEST_PATTERN_SOLID_BLACK:
			priv->test_pattern = 0x0001;
			priv->test_pattern_solid_color_r = 0x0000;
			priv->test_pattern_solid_color_gr = 0x0000;
			priv->test_pattern_solid_color_b = 0x0000;
			priv->test_pattern_solid_color_gb = 0x0000;
			break;
		case TEST_PATTERN_SOLID_WHITE:
			priv->test_pattern = 0x0001;
			priv->test_pattern_solid_color_r = 0x0fff;
			priv->test_pattern_solid_color_gr = 0x0fff;
			priv->test_pattern_solid_color_b = 0x0fff;
			priv->test_pattern_solid_color_gb = 0x0fff;
			break;
		case TEST_PATTERN_SOLID_RED:
			priv->test_pattern = 0x0001;
			priv->test_pattern_solid_color_r = 0x0fff;
			priv->test_pattern_solid_color_gr = 0x0000;
			priv->test_pattern_solid_color_b = 0x0000;
			priv->test_pattern_solid_color_gb = 0x0000;
			break;
		case TEST_PATTERN_SOLID_GREEN:
			priv->test_pattern = 0x0001;
			priv->test_pattern_solid_color_r = 0x0000;
			priv->test_pattern_solid_color_gr = 0x0fff;
			priv->test_pattern_solid_color_b = 0x0000;
			priv->test_pattern_solid_color_gb = 0x0fff;
			break;
		case TEST_PATTERN_SOLID_BLUE:
			priv->test_pattern = 0x0001;
			priv->test_pattern_solid_color_r = 0x0000;
			priv->test_pattern_solid_color_gr = 0x0000;
			priv->test_pattern_solid_color_b = 0x0fff;
			priv->test_pattern_solid_color_gb = 0x0000;
			break;
		case TEST_PATTERN_COLOR_BAR:
			priv->test_pattern = 0x0002;
			break;
		case TEST_PATTERN_FADE_TO_GREY_COLOR_BAR:
			priv->test_pattern = 0x0003;
			break;
		case TEST_PATTERN_PN9:
			priv->test_pattern = 0x0004;
			break;
		case TEST_PATTERN_16_SPLIT_COLOR_BAR:
			priv->test_pattern = 0x0005;
			break;
		case TEST_PATTERN_16_SPLIT_INVERTED_COLOR_BAR:
			priv->test_pattern = 0x0006;
			break;
		case TEST_PATTERN_COLUMN_COUNTER:
			priv->test_pattern = 0x0007;
			break;
		case TEST_PATTERN_INVERTED_COLUMN_COUNTER:
			priv->test_pattern = 0x0008;
			break;
		case TEST_PATTERN_PN31:
			priv->test_pattern = 0x0009;
			break;
		default:
			return -EINVAL;
	}

	return 0;
}

static int fpga_g_frame_interval(struct v4l2_subdev *sd,
		struct v4l2_subdev_frame_interval *fi)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct fpga *priv = to_fpga(client);
	const struct fpga_mode *mode = priv->cur_mode;

	fi->interval = mode->max_fps;

	return 0;
}	

static int fpga_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct fpga *priv =
		container_of(ctrl->handler, struct fpga, ctrl_handler);
	int ret;
	u16 gain = 256;
	u16 a_gain = 256;
	u16 d_gain = 1;

	switch (ctrl->id) {
		case V4L2_CID_HFLIP:
			priv->hflip = ctrl->val;
			break;

		case V4L2_CID_VFLIP:
			priv->vflip = ctrl->val;
			break;

		case V4L2_CID_ANALOGUE_GAIN:
		case V4L2_CID_GAIN:
			/*
			 * hal transfer (gain * 256)  to kernel
			 * than divide into analog gain & digital gain in kernel
			 */

			gain = ctrl->val;
			if (gain < 256)
				gain = 256;
			if (gain > 43663)
				gain = 43663;
			if (gain >= 256 && gain <= 2728) {
				a_gain = gain;
				d_gain = 1 * 256;
			} else {
				a_gain = 2728;
				d_gain = (gain * 256) / a_gain;
			}

			/*
			 * Analog gain, reg range[0, 232], gain value[1, 10.66]
			 * reg = 256 - 256 / again
			 * a_gain here is 256 multify
			 * so the reg = 256 - 256 * 256 / a_gain
			 */
			priv->analogue_gain = (256 - (256 * 256) / a_gain);
			if (a_gain < 256)
				priv->analogue_gain = 0;
			if (priv->analogue_gain > 232)
				priv->analogue_gain = 232;

			/*
			 * Digital gain, reg range[256, 4095], gain rage[1, 16]
			 * reg = dgain * 256
			 */
			priv->digital_gain = d_gain;
			if (priv->digital_gain < 256)
				priv->digital_gain = 256;
			if (priv->digital_gain > 4095)
				priv->digital_gain = 4095;

			/*
			 * for bank A and bank B switch
			 * exposure time , gain, vts must change at the same time
			 * so the exposure & gain can reflect at the same frame
			 */

			return ret;

		case V4L2_CID_EXPOSURE:
			priv->exposure_time = ctrl->val;

			return ret;

		case V4L2_CID_TEST_PATTERN:
			return fpga_s_ctrl_test_pattern(ctrl);

		case V4L2_CID_VBLANK:
			if (ctrl->val < priv->cur_mode->vts_def)
				ctrl->val = priv->cur_mode->vts_def;
			if ((ctrl->val - FPGA_EXP_LINES_MARGIN) != priv->cur_vts)
				priv->cur_vts = ctrl->val - FPGA_EXP_LINES_MARGIN;
			return ret;

		default:
			return -EINVAL;
	}
	/* If enabled, apply settings immediately */
	fpga_s_stream(&priv->subdev, 1);

	return 0;
}

static int fpga_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	return 0;
}

static int fpga_get_reso_dist(const struct fpga_mode *mode,
		struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) + 
		abs(mode->height - framefmt->height);
}

static const struct fpga_mode *fpga_find_best_fit(
		struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = fpga_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int fpga_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *fmt)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct fpga *priv = to_fpga(client);
	const struct fpga_mode *mode;
	s64 h_blank, v_blank, pixel_rate;
	u32 fps = 0;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;	

	mode = fpga_find_best_fit(fmt);
	fmt->format.code = MEDIA_BUS_FMT_RGB888_1X24;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	priv->cur_mode = mode;
	h_blank = mode->hts_def - mode->width;
	__v4l2_ctrl_modify_range(priv->hblank, h_blank,
			h_blank, 1, h_blank);
	v_blank = mode->vts_def - mode->height;
	__v4l2_ctrl_modify_range(priv->vblank, v_blank,
			v_blank,
			1, v_blank);
	fps = DIV_ROUND_CLOSEST(mode->max_fps.denominator,
			mode->max_fps.numerator);
	pixel_rate =mode->vts_def * mode->hts_def * fps;

	__v4l2_ctrl_modify_range(priv->pixel_rate, pixel_rate,
			pixel_rate, 1, pixel_rate);

	/* reset crop window */
	priv->crop_rect.left = 1640 - (mode->width / 2);
	if (priv->crop_rect.left < 0)
		priv->crop_rect.left = 0;
	priv->crop_rect.top = 1232 - (mode->height / 2);
	if (priv->crop_rect.top < 0)
		priv->crop_rect.top = 0;
	priv->crop_rect.width = mode->width;
	priv->crop_rect.height = mode->height;

	return 0;
}

static int fpga_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *fmt)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct fpga *priv = to_fpga(client);
	const struct fpga_mode *mode = priv->cur_mode;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.code = MEDIA_BUS_FMT_RGB888_1X24;
	fmt->format.field = V4L2_FIELD_NONE;

	return 0;
}

static long fpga_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	long ret = 0;
	return ret;
}

#ifdef CONFIG_COMPAT
static long fpga_compat_ioctl32(struct v4l2_subdev *sd,
		unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	long ret;

	switch (cmd) {
		case RKMODULE_GET_MODULE_INFO:
			inf = kzalloc(sizeof(*inf), GFP_KERNEL);
			if (!inf) {
				ret = -ENOMEM;
				return ret;
			}

			ret = fpga_ioctl(sd, cmd, inf);
			if (!ret)
				ret = copy_to_user(up, inf, sizeof(*inf));
			kfree(inf);
			break;
		case RKMODULE_AWB_CFG:
			cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
			if (!cfg) {
				ret = -ENOMEM;
				return ret;
			}

			ret = copy_from_user(cfg, up, sizeof(*cfg));
			if (!ret)
				ret = fpga_ioctl(sd, cmd, cfg);
			kfree(cfg);
			break;
		default:
			ret = -ENOIOCTLCMD;
			break;
	}

	return ret;
}
#endif

static int fpga_enum_frame_interval(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_frame_interval_enum *fie)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct fpga *priv = to_fpga(client);

	if (fie->index >= priv->cfg_num)
		return -EINVAL;

	if (fie->code != MEDIA_BUS_FMT_SRGGB10_1X10)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

/* Various V4L2 operations tables */
static struct v4l2_subdev_video_ops fpga_subdev_video_ops = {
	.s_stream = fpga_s_stream,
	.g_frame_interval = fpga_g_frame_interval,
};

static struct v4l2_subdev_core_ops fpga_subdev_core_ops = {
	.s_power = fpga_s_power,
	.ioctl = fpga_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = fpga_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_pad_ops fpga_subdev_pad_ops = {
	.enum_mbus_code = fpga_enum_mbus_code,
	.enum_frame_interval = fpga_enum_frame_interval,
	.set_fmt = fpga_set_fmt,
	.get_fmt = fpga_get_fmt,
};

static struct v4l2_subdev_ops fpga_subdev_ops = {
	.core = &fpga_subdev_core_ops,
	.video = &fpga_subdev_video_ops,
	.pad = &fpga_subdev_pad_ops,
};

static const struct v4l2_ctrl_ops fpga_ctrl_ops = {
	.s_ctrl = fpga_s_ctrl,
};

static int fpga_ctrls_init(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct fpga *priv = to_fpga(client);
	const struct fpga_mode *mode = priv->cur_mode;
	s64 pixel_rate, h_blank, v_blank;
	int ret;
	u32 fps = 0;

	v4l2_ctrl_handler_init(&priv->ctrl_handler, 7);

	/* exposure */
	v4l2_ctrl_new_std(&priv->ctrl_handler, &fpga_ctrl_ops,
			V4L2_CID_ANALOGUE_GAIN,
			FPGA_ANALOGUE_GAIN_MIN,
			FPGA_ANALOGUE_GAIN_MAX,
			1, FPGA_ANALOGUE_GAIN_DEFAULT);
	v4l2_ctrl_new_std(&priv->ctrl_handler, &fpga_ctrl_ops,
			V4L2_CID_GAIN,
			FPGA_DIGITAL_GAIN_MIN,
			FPGA_DIGITAL_GAIN_MAX, 1,
			FPGA_DIGITAL_GAIN_DEFAULT);
	v4l2_ctrl_new_std(&priv->ctrl_handler, &fpga_ctrl_ops,
			V4L2_CID_EXPOSURE,
			FPGA_DIGITAL_EXPOSURE_MIN,
			FPGA_DIGITAL_EXPOSURE_MAX, 1,
			FPGA_DIGITAL_EXPOSURE_DEFAULT);

	/* blank */
	h_blank = mode->hts_def - mode->width;

	priv->hblank = v4l2_ctrl_new_std(&priv->ctrl_handler, NULL, V4L2_CID_HBLANK,
			h_blank, h_blank, 1, h_blank);
	v_blank = mode->vts_def - mode->height;
	priv->vblank = v4l2_ctrl_new_std(&priv->ctrl_handler, NULL, V4L2_CID_VBLANK,
			v_blank, v_blank, 1, v_blank);

	/* freq */
	v4l2_ctrl_new_int_menu(&priv->ctrl_handler, NULL, V4L2_CID_LINK_FREQ,
			0, 0, link_freq_menu_items);
	fps = DIV_ROUND_CLOSEST(mode->max_fps.denominator,
			mode->max_fps.numerator);
	pixel_rate = mode->vts_def * mode->hts_def * fps;
	dev_info(&client->dev, "Pixel Rate: %lld\n",pixel_rate);
	dev_info(&client->dev, "FPS Rate: %d\n",fps);
	dev_info(&client->dev, "h_blank: %lld, v_blank : %lld \n", h_blank, v_blank);
	dev_info(&client->dev, "hts_def: %d, vts_def : %d\n",mode->hts_def,mode->vts_def);
	dev_info(&client->dev, "width: %d, Height : %d\n",mode->width,mode->height);
	priv->pixel_rate = v4l2_ctrl_new_std(&priv->ctrl_handler, NULL, V4L2_CID_PIXEL_RATE,
			0, pixel_rate, 1, pixel_rate);

	priv->subdev.ctrl_handler = &priv->ctrl_handler;
	if (priv->ctrl_handler.error) {
		dev_info(&client->dev, "error %d adding controls\n",
				priv->ctrl_handler.error);
		ret = priv->ctrl_handler.error; 
		goto error;
	}

	ret = v4l2_ctrl_handler_setup(&priv->ctrl_handler);
	if (ret < 0) {
		dev_info(&client->dev, "Error %d setting default controls\n",
				ret);
		goto error; 
	}

	return 0;
error:
	v4l2_ctrl_handler_free(&priv->ctrl_handler);
	return ret;
}

static int fpga_probe(struct i2c_client *client,
		const struct i2c_device_id *did)
{
	struct fpga *priv;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
			DRIVER_VERSION >> 16,
			(DRIVER_VERSION & 0xff00) >> 8,
			DRIVER_VERSION & 0x00ff);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_warn(&adapter->dev,
				"I2C-Adapter doesn't support I2C_FUNC_SMBUS_BYTE\n");
		return -EIO;
	}
	dev_info(dev, "fx check ok");
	priv = devm_kzalloc(&client->dev, sizeof(struct fpga), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
			&priv->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
			&priv->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
			&priv->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
			&priv->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	dev_info(dev, "read property done...\n");

	priv->clk = devm_clk_get(&client->dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_info(&client->dev, "Error %ld getting clock\n",
				PTR_ERR(priv->clk));
		return -EPROBE_DEFER;
	}

	dev_info(dev, "got clk\n");
	priv->cur_mode = &supported_modes[0];
	priv->cfg_num = ARRAY_SIZE(supported_modes);

	priv->crop_rect.left = 80;
	priv->crop_rect.top = 80;
	priv->crop_rect.width = priv->cur_mode->width;
	priv->crop_rect.height = priv->cur_mode->height;

	v4l2_i2c_subdev_init(&priv->subdev, client, &fpga_subdev_ops);
	dev_info(dev, "subdev initialized\n");
	ret = fpga_ctrls_init(&priv->subdev);
	if (ret < 0)
		dev_info(dev, "error over here\n");

	dev_info(dev, "FPGA ctrls initialized\n");
	priv->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	priv->pad.flags = MEDIA_PAD_FL_SOURCE;
	priv->subdev.entity.type = MEDIA_ENT_T_V4L2_SUBDEV_SENSOR;
	ret = media_entity_init(&priv->subdev.entity, 1, &priv->pad, 0);
	if (ret < 0)
		return ret;
	dev_info(dev, "media entity init done\n");
	sd = &priv->subdev;
	memset(facing, 0, sizeof(facing));
	if (strcmp(priv->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
			priv->module_index, facing,
			FPGA_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);

	if (ret < 0)
		return ret;
	dev_info(dev, "subdev register done..\n");
	return ret;
}

static int fpga_remove(struct i2c_client *client)
{
	struct fpga *priv = to_fpga(client);

	v4l2_async_unregister_subdev(&priv->subdev);
	media_entity_cleanup(&priv->subdev.entity);
	v4l2_ctrl_handler_free(&priv->ctrl_handler);

	return 0;
}

static const struct i2c_device_id fpga_id[] = {
	{"fpga", 0},
	{}
};

static const struct of_device_id fpga_of_match[] = {
	{.compatible = "efinix,fpga-read" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, fpga_of_match);

MODULE_DEVICE_TABLE(i2c, fpga_id);
static struct i2c_driver fpga_i2c_driver = {
	.driver = {
		.of_match_table = of_match_ptr(fpga_of_match),
		.name = FPGA_NAME,
	},
	.probe = fpga_probe,
	.remove = fpga_remove,
	.id_table = fpga_id,
};

module_i2c_driver(fpga_i2c_driver);
MODULE_DESCRIPTION("FPGA read driver");
MODULE_AUTHOR("djkabutar <d.kabutarwala@yahoo.com>");
MODULE_LICENSE("GPL v2");
