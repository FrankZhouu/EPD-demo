/*
 * Copyright (c) 2018 Jan Van Winkel <jan.van_winkel@dxplore.eu>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "core/lv_obj_pos.h"
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <lvgl_input_device.h>
#include <zephyr/shell/shell.h>

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app);

extern uint8_t rx_data[384];
extern bool data_received;
extern int bt_init(void);

static lv_obj_t *label;
static bool refresh = 0;
uint8_t button_pressed = 0;/* 0 means sw0,1 means sw1 */

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

#ifdef CONFIG_GPIO
static struct gpio_dt_spec button0_gpio = GPIO_DT_SPEC_GET_OR(
		DT_ALIAS(sw0), gpios, {0});
static struct gpio_callback button0_callback;

static struct gpio_dt_spec button1_gpio = GPIO_DT_SPEC_GET_OR(
	DT_ALIAS(sw1), gpios, {0});
static struct gpio_callback button1_callback;

static void button_isr_callback(const struct device *port,
				struct gpio_callback *cb,
				uint32_t pins)
{
	if (pins & BIT(button0_gpio.pin)) {
		button_pressed = 0;
	} else if (pins & BIT(button1_gpio.pin)) {
		button_pressed = 1;
	}
	refresh = 1;

}
#endif /* CONFIG_GPIO */

int button_init(const struct gpio_dt_spec *spec, struct gpio_callback *callback,
				gpio_callback_handler_t handler)
{
	if (!gpio_is_ready_dt(spec)) {
		LOG_ERR("Error: %s is not a valid GPIO device", spec->port->name);
		return -ENODEV;
	}

	int err;

	err = gpio_pin_configure_dt(spec, GPIO_INPUT);
	if (err) {
		LOG_ERR("failed to configure button gpio: %d", err);
		return -1;
	}

	gpio_init_callback(callback, handler, BIT(spec->pin));

	err = gpio_add_callback(spec->port, callback);
	if (err) {
		LOG_ERR("failed to add button callback: %d", err);
		return -1;
	}

	err = gpio_pin_interrupt_configure_dt(spec, GPIO_INT_EDGE_TO_ACTIVE);
	if (err) {
		LOG_ERR("failed to enable button callback: %d", err);
		return -1;
	}

	return 0;
}

int main(void)
{
	LOG_INF("LVGL Zephyr application started V0.1");

	int ret;

	if (!gpio_is_ready_dt(&led)) {
		return 0;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return 0;
	}

	const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device not ready, aborting test");
		return 0;
	}

	struct display_capabilities cap;
	display_get_capabilities(display_dev, &cap);
	LOG_INF("Display resolution: %dx%d", cap.x_resolution, cap.y_resolution);

#ifdef CONFIG_BT
	int err;
	err = bt_init();
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return 0;
	}
#endif

#ifdef CONFIG_GPIO
	ret = button_init(&button0_gpio, &button0_callback, button_isr_callback);
	if (ret) {
		LOG_ERR("Failed to initialize button0");
		return -1;
	}

	ret = button_init(&button1_gpio, &button1_callback, button_isr_callback);
	if (ret) {
		LOG_ERR("Failed to initialize button1");
		return -1;
	}
#endif /* CONFIG_GPIO */

	label = lv_label_create(lv_scr_act());
	static lv_style_t style;
	lv_style_init(&style);
	lv_style_set_text_font(&style, &lv_font_montserrat_16);
	lv_obj_add_style(label, &style, LV_PART_MAIN);

	lv_label_set_text(label, "This is a simple EPD demo.");
	lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
	lv_obj_set_width(label, cap.x_resolution);
	lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);

	lv_task_handler();
	display_blanking_off(display_dev);

	while(1)
	{
		if (data_received) {
            data_received = false;
			lv_label_set_text(label, rx_data);
			lv_task_handler();
			memset(rx_data, 0, sizeof(rx_data));
        }

		if (refresh) {
			refresh = 0;
			if (button_pressed == 0) {
				lv_label_set_text(label, "Button 0 pressed");
			} else {
				lv_label_set_text(label, "Button 1 pressed");
			}
			lv_task_handler();
		}
		k_sleep(K_MSEC(10));
	}
}

static int cmd_refresh(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	memset(rx_data, 0, sizeof(rx_data));
	strncpy(rx_data, argv[1], sizeof(argv) - 1);
	rx_data[sizeof(rx_data) - 1] = '\0';
	lv_label_set_text(label, rx_data);
	lv_task_handler();

	shell_print(shell, "refresh screen: %s", rx_data);
	return 0;
}

SHELL_CMD_REGISTER(refresh, NULL, "refresh the screen", cmd_refresh);
