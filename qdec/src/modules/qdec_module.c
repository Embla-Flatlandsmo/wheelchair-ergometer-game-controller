/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */


#include <zephyr/kernel.h>

#define MODULE qdec_module
#include <caf/events/module_state_event.h>
#include <app_event_manager.h>
#include <zephyr/settings/settings.h>
#include <drivers/sensor.h>
#include <drivers/gpio.h>
#include "modules_common.h"
#include "events/qdec_module_event.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_QDEC_MODULE_LOG_LEVEL);

static struct sensor_trigger trig_a;

static float dt = 0.05;

bool led_a_on = false;
bool led_b_on = false;

static struct gpio_dt_spec led_a = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios,
						     {0});

static struct gpio_dt_spec led_b = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led1), gpios,
						     {0});

static float qdec_a_rot_speed = 0.0;
static double rot_a = 0.0;
static double qdec_a_rot_delta = 0.0;
static const float alpha = ((float)CONFIG_QDEC_VELOCITY_LOW_PASS_FILTER_COEFFICIENT)/1000.0;

static void send_data_evt(void)
{
	struct qdec_module_event *qdec_module_event = new_qdec_module_event();
	qdec_module_event->type = QDEC_EVT_DATA_READY;
	qdec_module_event->rot_speed_a = qdec_a_rot_speed;
	qdec_module_event->rot_speed_b = 0.0;
	APP_EVENT_SUBMIT(qdec_module_event);
}

static float moving_avg_filter(float y_prev, float y)
{
	return alpha*y_prev + (1-alpha)*y;
}

static void data_evt_timeout_work_handler(struct k_work *work);
K_WORK_DEFINE(data_evt_timeout_work, data_evt_timeout_work_handler);

void data_evt_timeout_handler(struct k_timer *dummy)
{
    k_work_submit(&data_evt_timeout_work);
}

K_TIMER_DEFINE(data_evt_timeout, data_evt_timeout_handler, NULL);

void data_evt_timeout_work_handler(struct k_work *work)
{
	float qdec_a_current_speed = qdec_a_rot_delta/dt;
	qdec_a_rot_delta = 0.0;
	qdec_a_rot_speed = moving_avg_filter(qdec_a_rot_speed, qdec_a_current_speed);
	// float qdec_b_current_speed = qdec_b_rot_delta/dt;
	// qdec_b_rot_delta = 0.0;
	// qdec_b_rot_speed = moving_avg_filter(qdec_b_rot_speed, qdec_b_current_speed);

	send_data_evt();
	
	// k_timer_start(&data_evt_timeout, K_MSEC((int)(1000.0*dt)), K_NO_WAIT);
}
// void qdec_a_timeout_work_handler(struct k_work *work)
// {
//     k_uptime_delta(&qdec_a_delta_time);
// 	qdec_a_rot_speed = 0.0;
// 	send_data_evt();
// }

// K_WORK_DEFINE(qdec_a_timeout_work, qdec_a_timeout_work_handler);

// void qdec_a_timeout_handler(struct k_timer *dummy)
// {
//     k_work_submit(&qdec_a_timeout_work);
// }

// K_TIMER_DEFINE(qdec_a_timeout, qdec_a_timeout_handler, NULL);

// void qdec_b_timeout_work_handler(struct k_work *work)
// {
//     k_uptime_delta(&qdec_b_delta_time);
// 	qdec_b_rot_speed = 0.0;
// 	send_data_evt();
// }

// K_WORK_DEFINE(qdec_b_timeout_work, qdec_b_timeout_work_handler);

// void qdec_b_timeout_handler(struct k_timer *dummy)
// {
//     k_work_submit(&qdec_b_timeout_work);
// }

// K_TIMER_DEFINE(qdec_b_timeout, qdec_b_timeout_handler, NULL);

static void trigger_a_handler(const struct device *dev, const struct sensor_trigger* trig)
{
	struct sensor_value rot;
	int err;
	err = sensor_sample_fetch(dev);
	if (err != 0)
	{
		LOG_ERR("sensor_sample_fetch error: %d\n", err);
		return;
	}
	err = sensor_channel_get(dev, SENSOR_CHAN_ROTATION, &rot);
	if (err != 0)
	{
		LOG_ERR("sensor_channel_get error: %d\n", err);
		return;
	}

	double new_rot_a = sensor_value_to_double(&rot);
	qdec_a_rot_delta += new_rot_a;
	gpio_pin_set_dt(&led_a, led_a_on);
	led_a_on = !led_a_on;
}

// static void trigger_b_handler(const struct device *dev, const struct sensor_trigger* trig)
// {
// 	struct sensor_value rot;
// 	int err;
// 	err = sensor_sample_fetch(dev);
// 	if (err != 0)
// 	{
// 		LOG_ERR("sensor_sample_fetch error: %d\n", err);
// 		return;
// 	}
// 	err = sensor_channel_get(dev, SENSOR_CHAN_ROTATION, &rot);
// 	if (err != 0)
// 	{
// 		LOG_ERR("sensor_channel_get error: %d\n", err);
// 		return;
// 	}
	
// 	double new_rot_b = sensor_value_to_double(&rot);
// 	qdec_b_rot_delta += new_rot_b;
// }

static int setup_led(void)
{
	int err = 0;
	err = !device_is_ready(led_a.port);
	/* LED A */
	if (led_a.port && !device_is_ready(led_a.port)) {
		printk("Error %d: LED_a device %s is not ready; ignoring it\n",
		       err, led_a.port->name);
		led_a.port = NULL;
	}
	if (led_a.port) {
		err = gpio_pin_configure_dt(&led_a, GPIO_OUTPUT);
		if (err != 0) {
			printk("Error %d: failed to configure LED_a device %s pin %d\n",
			       err, led_a.port->name, led_a.pin);
			led_a.port = NULL;
		} else {
			printk("Set up LED_a at %s pin %d\n", led_a.port->name, led_a.pin);
		}
	}

	/* LED B */
	if (led_b.port && !device_is_ready(led_b.port)) {
		printk("Error %d: LED_b device %s is not ready; ignoring it\n",
		       err, led_b.port->name);
		led_b.port = NULL;
	}
	if (led_b.port) {
		err = gpio_pin_configure_dt(&led_b, GPIO_OUTPUT);
		if (err != 0) {
			printk("Error %d: failed to configure LED_b device %s pin %d\n",
			       err, led_b.port->name, led_b.pin);
			led_b.port = NULL;
		} else {
			printk("Set up LED_b at %s pin %d\n", led_b.port->name, led_b.pin);
		}
	}
	return 0;
}

static int module_init(void)
{
	const struct device* qdeca_dev = device_get_binding(DT_LABEL(DT_NODELABEL(qdeca)));
	// const struct device* qdecb_dev = device_get_binding(DT_LABEL(DT_NODELABEL(qdecb)));
	if (qdeca_dev == NULL)
	{
		LOG_ERR("Failed to get bindings for qdec devices");
		return -ENODEV;
	}

	trig_a.type = SENSOR_TRIG_DATA_READY;
	trig_a.chan = SENSOR_CHAN_ROTATION;
	// trig_b.type = SENSOR_TRIG_DATA_READY;
	// trig_b.chan = SENSOR_CHAN_ROTATION;
	int err;
	err = sensor_trigger_set(qdeca_dev, &trig_a, trigger_a_handler);
	if (err)
	{
		LOG_ERR("sensor_trigger_set for qdecb error: %d", err);
		return err;
	}

	err = setup_led();
	if (err)
	{
		return err;
	}
	gpio_pin_set_dt(&led_a, led_a_on);
	gpio_pin_set_dt(&led_b, led_b_on);
	k_timer_start(&data_evt_timeout, K_NO_WAIT, K_MSEC((int)(1000.0*dt)));
	// err = sensor_trigger_set(qdecb_dev, &trig_b, trigger_b_handler);
	// if (err)
	// {
	// 	LOG_ERR("sensor_trigger_set for qdecb error: %d", err);
	// 	return err;
	// }

	return 0;
}

static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_module_state_event(aeh)) {
		const struct module_state_event *event = cast_module_state_event(aeh);

		if (check_state(event, MODULE_ID(main), MODULE_STATE_READY)) {
			
            if (module_init())
            {
                LOG_ERR("QDEC module init failed");

                return false;
            }
            LOG_INF("QDEC module initialized");
            module_set_state(MODULE_STATE_READY);
		}

		return false;
	}
	/* Event not handled but subscribed. */
	__ASSERT_NO_MSG(false);
	return false;
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);