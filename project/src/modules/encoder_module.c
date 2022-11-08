/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */


#include <zephyr/kernel.h>

#define MODULE encoder_module
#include <caf/events/module_state_event.h>
#include <app_event_manager.h>
#include <zephyr/settings/settings.h>
#include <drivers/sensor.h>
#include "modules_common.h"
#include "events/encoder_module_event.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_ENCODER_MODULE_LOG_LEVEL);

static struct sensor_trigger trig_a;
static struct sensor_trigger trig_b;

static float encoder_a_rot_speed;
static float encoder_b_rot_speed;
static double rot_a;
static double rot_b;
static int64_t encoder_a_delta_time;
static int64_t encoder_b_delta_time;
static const float alpha = ((float)CONFIG_ENCODER_VELOCITY_LOW_PASS_FILTER_COEFFICIENT)/1000.0;

static void send_data_evt(void)
{
	struct encoder_module_event *encoder_module_event = new_encoder_module_event();
	encoder_module_event->type = ENCODER_EVT_DATA_READY;
	encoder_module_event->rot_speed_a = encoder_a_rot_speed;
	encoder_module_event->rot_speed_b = encoder_b_rot_speed;
	APP_EVENT_SUBMIT(encoder_module_event);
}


void encoder_a_timeout_work_handler(struct k_work *work)
{
    k_uptime_delta(&encoder_a_delta_time);
	encoder_a_rot_speed = 0.0;
	send_data_evt();
}

K_WORK_DEFINE(encoder_a_timeout_work, encoder_a_timeout_work_handler);

void encoder_a_timeout_handler(struct k_timer *dummy)
{
    k_work_submit(&encoder_a_timeout_work);
}

K_TIMER_DEFINE(encoder_a_timeout, encoder_a_timeout_handler, NULL);

void encoder_b_timeout_work_handler(struct k_work *work)
{
    k_uptime_delta(&encoder_b_delta_time);
	encoder_b_rot_speed = 0.0;
	send_data_evt();
}

K_WORK_DEFINE(encoder_b_timeout_work, encoder_b_timeout_work_handler);

void encoder_b_timeout_handler(struct k_timer *dummy)
{
    k_work_submit(&encoder_b_timeout_work);
}

K_TIMER_DEFINE(encoder_b_timeout, encoder_b_timeout_handler, NULL);

static float moving_avg_filter(float y_prev, float y)
{
	return alpha*y_prev + (1-alpha)*y;
}

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
	int64_t dt = k_uptime_delta(&encoder_a_delta_time);
	if (dt < CONFIG_ENCODER_MINIMUM_DELTA_TIME_MSEC) {
		return;
	}
	long double delta_time = dt;
	double rot_speed = (new_rot_a-rot_a)*1000.0/delta_time;
	
	encoder_a_rot_speed = moving_avg_filter(encoder_a_rot_speed, rot_speed);
	rot_a = new_rot_a;
	send_data_evt();
	k_timer_start(&encoder_a_timeout, K_MSEC(CONFIG_ENCODER_TIMEOUT_DURATION_MSEC), K_NO_WAIT);
}

static void trigger_b_handler(const struct device *dev, const struct sensor_trigger* trig)
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
	
	double new_rot_b = sensor_value_to_double(&rot);
	int64_t dt = k_uptime_delta(&encoder_b_delta_time);
	if (dt < CONFIG_ENCODER_MINIMUM_DELTA_TIME_MSEC) {
		return;
	}
	long double delta_time = dt;
	double rot_speed = (new_rot_b-rot_b)*1000.0/delta_time;
	
	encoder_b_rot_speed = moving_avg_filter(encoder_b_rot_speed, rot_speed);
	rot_b = new_rot_b;
	send_data_evt();
	
	k_timer_start(&encoder_b_timeout, K_MSEC(CONFIG_ENCODER_TIMEOUT_DURATION_MSEC), K_NO_WAIT);
}

static int module_init(void)
{
	const struct device* encoder_a_dev = device_get_binding(DT_LABEL(DT_NODELABEL(qdeca)));
	const struct device* encoder_b_dev = device_get_binding(DT_LABEL(DT_NODELABEL(qdecb)));
	if (encoder_a_dev == NULL || encoder_b_dev == NULL)
	{
		LOG_ERR("Failed to get bindings for encoder devices");
		return -ENODEV;
	}

	trig_a.type = SENSOR_TRIG_DATA_READY;
	trig_a.chan = SENSOR_CHAN_ROTATION;

	trig_b.type = SENSOR_TRIG_DATA_READY;
	trig_b.chan = SENSOR_CHAN_ROTATION;
	int err;
	err = sensor_trigger_set(encoder_a_dev, &trig_a, trigger_a_handler);
	if (err)
	{
		LOG_ERR("sensor_trigger_set for encoder_b error: %d", err);
		return err;
	}

	err = sensor_trigger_set(encoder_b_dev, &trig_b, trigger_b_handler);
	if (err)
	{
		LOG_ERR("sensor_trigger_set for encoder_b error: %d", err);
		return err;
	}

	return 0;
}

static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_module_state_event(aeh)) {
		const struct module_state_event *event = cast_module_state_event(aeh);

		if (check_state(event, MODULE_ID(main), MODULE_STATE_READY)) {
			
            if (module_init())
            {
                LOG_ERR("ENCODER module init failed");

                return false;
            }
            LOG_INF("ENCODER module initialized");
            int64_t current_time = k_uptime_get();
            encoder_a_delta_time = current_time;
            encoder_b_delta_time = current_time;
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