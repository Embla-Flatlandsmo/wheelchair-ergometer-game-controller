/*
 * Copyright (c) 2016 Open-RnD Sp. z o.o.
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <device.h>
#include <drivers/gpio.h>
#include <sys/util.h>
#include <sys/printk.h>
#include <inttypes.h>

#define SLEEP_TIME_MS	1

/*
 * Get button configuration from the devicetree sw0 alias. This is mandatory.
 */
#define SW0_NODE	DT_ALIAS(sw0)
#define SW1_NODE	DT_ALIAS(sw1)
#if !DT_NODE_HAS_STATUS(SW0_NODE, okay)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif
static const struct gpio_dt_spec button_a = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios,
							      {0});
static struct gpio_callback button_a_cb_data;

static const struct gpio_dt_spec button_b = GPIO_DT_SPEC_GET_OR(SW1_NODE, gpios,
							      {0});
static struct gpio_callback button_b_cb_data;

static bool button_a_en = false;
static bool button_b_en = false;

/*
 * The led0 devicetree alias is optional. If present, we'll use it
 * to turn on the LED whenever the button is pressed.
 */
static struct gpio_dt_spec led_a = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios,
						     {0});

static struct gpio_dt_spec led_b = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led1), gpios,
						     {0});

void button_a_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	button_a_en = !button_a_en;
	printk("Button a pressed at %" PRIu32 "\n", k_cycle_get_32());
}

void button_b_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	button_b_en = ! button_b_en;
	printk("Button b pressed at %" PRIu32 "\n", k_cycle_get_32());
}

void main(void)
{
	int ret;

	/* BUTTON A*/
	if (!device_is_ready(button_a.port)) {
		printk("Error: button_a device %s is not ready\n",
		       button_a.port->name);
		return;
	}

	ret = gpio_pin_configure_dt(&button_a, GPIO_INPUT | GPIO_PULL_UP);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n",
		       ret, button_a.port->name, button_a.pin);
		return;
	}

	ret = gpio_pin_interrupt_configure_dt(&button_a,
					      GPIO_INT_EDGE_BOTH);
	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n",
			ret, button_a.port->name, button_a.pin);
		return;
	}

	gpio_init_callback(&button_a_cb_data, button_a_pressed, BIT(button_a.pin));
	gpio_add_callback(button_a.port, &button_a_cb_data);
	printk("Set up button_a at %s pin %d\n", button_a.port->name, button_a.pin);
	/* BUTTON B*/
	if (!device_is_ready(button_b.port)) {
		printk("Error: button_b device %s is not ready\n",
		       button_b.port->name);
		return;
	}

	ret = gpio_pin_configure_dt(&button_b, GPIO_INPUT | GPIO_PULL_UP);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n",
		       ret, button_b.port->name, button_b.pin);
		return;
	}

	ret = gpio_pin_interrupt_configure_dt(&button_b,
					      GPIO_INT_EDGE_BOTH);
	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n",
			ret, button_b.port->name, button_b.pin);
		return;
	}

	gpio_init_callback(&button_b_cb_data, button_b_pressed, BIT(button_b.pin));
	gpio_add_callback(button_b.port, &button_b_cb_data);
	printk("Set up button_b at %s pin %d\n", button_b.port->name, button_b.pin);

	/* LED A */
	if (led_a.port && !device_is_ready(led_a.port)) {
		printk("Error %d: LED_a device %s is not ready; ignoring it\n",
		       ret, led_a.port->name);
		led_a.port = NULL;
	}
	if (led_a.port) {
		ret = gpio_pin_configure_dt(&led_a, GPIO_OUTPUT);
		if (ret != 0) {
			printk("Error %d: failed to configure LED_a device %s pin %d\n",
			       ret, led_a.port->name, led_a.pin);
			led_a.port = NULL;
		} else {
			printk("Set up LED_a at %s pin %d\n", led_a.port->name, led_a.pin);
		}
	}

	/* LED B */
	if (led_b.port && !device_is_ready(led_b.port)) {
		printk("Error %d: LED_b device %s is not ready; ignoring it\n",
		       ret, led_b.port->name);
		led_b.port = NULL;
	}
	if (led_b.port) {
		ret = gpio_pin_configure_dt(&led_b, GPIO_OUTPUT);
		if (ret != 0) {
			printk("Error %d: failed to configure LED_b device %s pin %d\n",
			       ret, led_b.port->name, led_b.pin);
			led_b.port = NULL;
		} else {
			printk("Set up LED_b at %s pin %d\n", led_b.port->name, led_b.pin);
		}
	}

	printk("Press the button\n");
	while (1)
	{
		gpio_pin_set_dt(&led_a, button_a_en);
		gpio_pin_set_dt(&led_b, button_b_en);
		k_msleep(SLEEP_TIME_MS);
	}
	// if (led.port) {
	// 	while (1) {
	// 		/* If we have an LED, match its state to the button's. */
	// 		int new_val = gpio_pin_get_dt(&button);
	// 		if (new_val != val) {
	// 			printf("old_val: %d, new_val: %d\n", val, new_val);
	// 			val = new_val;
	// 		}
	// 		if (val >= 0) {
	// 			gpio_pin_set_dt(&led, val);
	// 		}
	// 		k_msleep(SLEEP_TIME_MS);
	// 	}
	// }
}
