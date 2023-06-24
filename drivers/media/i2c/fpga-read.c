// SPDX-License-Identifier: GPL-2.0
/*
 * FPGA Read driver
 *
 * Copyright (C) 2023, Vicharak Computers LLP
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/rk-camera-module.h>
#include <linux/version.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>

#define DRIVER_VERSION KERNEL_VERSION(0, 0x01, 0x2)

/* FPGA supported geometry */
#define FPGA_ANALOGUE_GAIN_MULTIPLIER 256
#define FPGA_ANALOGUE_GAIN_MIN (1 * FPGA_ANALOGUE_GAIN_MULTIPLIER)
#define FPGA_ANALOGUE_GAIN_MAX (11 * FPGA_ANALOGUE_GAIN_MULTIPLIER)
#define FPGA_ANALOGUE_GAIN_DEFAULT (2 * FPGA_ANALOGUE_GAIN_MULTIPLIER)

/* In dB*256 */
#define FPGA_DIGITAL_GAIN_MIN 256
#define FPGA_DIGITAL_GAIN_MAX 43663
#define FPGA_DIGITAL_GAIN_DEFAULT 256

#define FPGA_DIGITAL_EXPOSURE_MIN 0
#define FPGA_DIGITAL_EXPOSURE_MAX 4095
#define FPGA_DIGITAL_EXPOSURE_DEFAULT 1575

#define FPGA_EXP_LINES_MARGIN 4
#define FPGA_NAME "EFINIX"
#define FPGA_LANES 4

static const s64 fpga_link_freq[] = { 600000000 };

struct fpga_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
};

struct fpga {
	struct v4l2_subdev subdev;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct clk *clk;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *pixel_rate;
	const struct fpga_mode *cur_mode;
	u32 cfg_num;
	u32 module_index;
	const char *module_facing;
	const char *module_name;
	const char *len_name;
};

static const struct fpga_mode supported_modes[] = {
	{
		.width =  640, // H
		.height = 480, // V
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 640 + 180, //+88+128+40,
		.vts_def = 480 + 90, //+23+1+128,
	},
};

static struct fpga *to_fpga(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct fpga, subdev);
}

static int fpga_s_ctrl(struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int fpga_enum_mbus_code(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_mbus_code_enum *code)
{
	code->code = MEDIA_BUS_FMT_RGB888_1X24;

	return 0;
}

static int fpga_get_reso_dist(const struct fpga_mode *mode,
			      struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct fpga_mode *
fpga_find_best_fit(struct v4l2_subdev_format *fmt)
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
	__v4l2_ctrl_modify_range(priv->hblank, h_blank, h_blank, 1, h_blank);
	v_blank = mode->vts_def - mode->height;
	__v4l2_ctrl_modify_range(priv->vblank, v_blank, v_blank, 1, v_blank);
	fps = DIV_ROUND_CLOSEST(mode->max_fps.denominator,
				mode->max_fps.numerator);
	pixel_rate = mode->vts_def * mode->hts_def * fps;

	__v4l2_ctrl_modify_range(priv->pixel_rate, pixel_rate, pixel_rate, 1,
				 pixel_rate);

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

static int fpga_enum_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_frame_interval_enum *fie)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct fpga *priv = to_fpga(client);

	if (fie->index >= priv->cfg_num)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

static const struct v4l2_subdev_pad_ops fpga_subdev_pad_ops = {
	.enum_mbus_code = fpga_enum_mbus_code,
	.enum_frame_interval = fpga_enum_frame_interval,
	.set_fmt = fpga_set_fmt,
	.get_fmt = fpga_get_fmt,
};

static struct v4l2_subdev_ops fpga_subdev_ops = {
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

	v4l2_ctrl_handler_init(&priv->ctrl_handler, 10);

	v4l2_ctrl_new_std(&priv->ctrl_handler, &fpga_ctrl_ops, V4L2_CID_HFLIP,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->ctrl_handler, &fpga_ctrl_ops, V4L2_CID_VFLIP,
			  0, 1, 1, 0);

	/* exposure */
	v4l2_ctrl_new_std(&priv->ctrl_handler, &fpga_ctrl_ops,
			  V4L2_CID_ANALOGUE_GAIN, FPGA_ANALOGUE_GAIN_MIN,
			  FPGA_ANALOGUE_GAIN_MAX, 1,
			  FPGA_ANALOGUE_GAIN_DEFAULT);
	v4l2_ctrl_new_std(&priv->ctrl_handler, &fpga_ctrl_ops, V4L2_CID_GAIN,
			  FPGA_DIGITAL_GAIN_MIN, FPGA_DIGITAL_GAIN_MAX, 1,
			  FPGA_DIGITAL_GAIN_DEFAULT);
	v4l2_ctrl_new_std(&priv->ctrl_handler, &fpga_ctrl_ops,
			  V4L2_CID_EXPOSURE, FPGA_DIGITAL_EXPOSURE_MIN,
			  FPGA_DIGITAL_EXPOSURE_MAX, 1,
			  FPGA_DIGITAL_EXPOSURE_DEFAULT);

	/* blank */
	h_blank = mode->hts_def - mode->width;

	priv->hblank =
		v4l2_ctrl_new_std(&priv->ctrl_handler, NULL, V4L2_CID_HBLANK,
				  h_blank, h_blank, 1, h_blank);
	v_blank = mode->vts_def - mode->height;
	priv->vblank =
		v4l2_ctrl_new_std(&priv->ctrl_handler, NULL, V4L2_CID_VBLANK,
				  v_blank, v_blank, 1, v_blank);

	/* freq */
	v4l2_ctrl_new_int_menu(&priv->ctrl_handler, NULL, V4L2_CID_LINK_FREQ, 0,
			       0, fpga_link_freq);
	fps = DIV_ROUND_CLOSEST(mode->max_fps.denominator,
				mode->max_fps.numerator);
	pixel_rate = mode->vts_def * mode->hts_def * fps;
	pr_info("Pixel Rate: %lld\n", pixel_rate);
	pr_info("FPS Rate: %d\n", fps);
	pr_info("h_blank: %lld, v_blank : %lld\n", h_blank, v_blank);
	pr_info("hts_def: %d, vts_def : %d\n", mode->hts_def, mode->vts_def);
	pr_info("width: %d, Height : %d\n", mode->width, mode->height);
	priv->pixel_rate = v4l2_ctrl_new_std(&priv->ctrl_handler, NULL,
					     V4L2_CID_PIXEL_RATE, 0, pixel_rate,
					     1, pixel_rate);

	priv->subdev.ctrl_handler = &priv->ctrl_handler;
	if (priv->ctrl_handler.error) {
		pr_info("error %d adding controls\n", priv->ctrl_handler.error);
		ret = priv->ctrl_handler.error;
		goto error;
	}

	ret = v4l2_ctrl_handler_setup(&priv->ctrl_handler);
	if (ret < 0) {
		pr_info("Error %d setting default controls\n", ret);
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

	pr_info("driver version: %02x.%02x.%02x", DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8, DRIVER_VERSION & 0x00ff);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		pr_warn("I2C-Adapter doesn't support I2C_FUNC_SMBUS_BYTE\n");
		return -EIO;
	}

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
		pr_err("could not get module information!\n");
		return -EINVAL;
	}

	priv->clk = devm_clk_get(&client->dev, NULL);
	if (IS_ERR(priv->clk)) {
		pr_info("Error %ld getting clock\n", PTR_ERR(priv->clk));
		return -EPROBE_DEFER;
	}

	priv->cur_mode = &supported_modes[0];
	priv->cfg_num = ARRAY_SIZE(supported_modes);

	v4l2_i2c_subdev_init(&priv->subdev, client, &fpga_subdev_ops);

	ret = fpga_ctrls_init(&priv->subdev);
	if (ret < 0)
		pr_err("error setting sensor ctrls init.\n");

	priv->subdev.flags |=
		V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	priv->pad.flags = MEDIA_PAD_FL_SOURCE;
	priv->subdev.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	ret = media_entity_pads_init(&priv->subdev.entity, 1, &priv->pad);
	if (ret < 0) {
		pr_err("error setting media entity pads init.\n");
		return ret;
	}

	sd = &priv->subdev;
	memset(facing, 0, sizeof(facing));
	if (strcmp(priv->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 priv->module_index, facing, FPGA_NAME, dev_name(sd->dev));

	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret < 0) {
		pr_err("error setting async register subdev sensor common.\n");
		return ret;
	}

	pr_info("subdev register done.\n");

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

static const struct i2c_device_id fpga_id[] = { { "fpga", 0 }, {} };

static const struct of_device_id fpga_of_match[] = {
	{ .compatible = "efinix,fpga-read" },
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
MODULE_DESCRIPTION("Vicharak FPGA read driver");
