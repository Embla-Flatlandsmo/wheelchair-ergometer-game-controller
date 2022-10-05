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
#include "modules_common.h"
#include "events/qdec_module_event.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_QDEC_MODULE_LOG_LEVEL);

static struct sensor_trigger trig_a;
static struct sensor_trigger trig_b;

static float qdec_a_rot_speed;
static float qdec_b_rot_speed;
static double rot_a;
static double rot_b;
static int64_t qdec_a_delta_time;
static int64_t qdec_b_delta_time;


static void send_data_evt(void)
{
	struct qdec_module_event *qdec_module_event = new_qdec_module_event();
	qdec_module_event->type = QDEC_EVT_DATA_READY;
	qdec_module_event->rot_speed_a = qdec_a_rot_speed;
	qdec_module_event->rot_speed_b = qdec_b_rot_speed;
	APP_EVENT_SUBMIT(qdec_module_event);
}


void qdec_a_timeout_work_handler(struct k_work *work)
{
    k_uptime_delta(&qdec_a_delta_time);
	qdec_a_rot_speed = 0.0;
	send_data_evt();
}

K_WORK_DEFINE(qdec_a_timeout_work, qdec_a_timeout_work_handler);

void qdec_a_timeout_handler(struct k_timer *dummy)
{
    k_work_submit(&qdec_a_timeout_work);
}

K_TIMER_DEFINE(qdec_a_timeout, qdec_a_timeout_handler, NULL);

void qdec_b_timeout_work_handler(struct k_work *work)
{
    k_uptime_delta(&qdec_b_delta_time);
	qdec_b_rot_speed = 0.0;
	send_data_evt();
}

K_WORK_DEFINE(qdec_b_timeout_work, qdec_b_timeout_work_handler);

void qdec_b_timeout_handler(struct k_timer *dummy)
{
    k_work_submit(&qdec_b_timeout_work);
}

K_TIMER_DEFINE(qdec_b_timeout, qdec_b_timeout_handler, NULL);

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
	int64_t dt = k_uptime_delta(&qdec_a_delta_time);
	if (dt < CONFIG_QDEC_MINIMUM_DELTA_TIME_MSEC) {
		return;
	}
	long double delta_time = dt;
	qdec_a_rot_speed = ((new_rot_a-rot_a)*1000.0/delta_time);
	rot_a = new_rot_a;
	send_data_evt();
	k_timer_start(&qdec_a_timeout, K_MSEC(CONFIG_QDEC_TIMEOUT_DURATION_MSEC), K_NO_WAIT);
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
	int64_t dt = k_uptime_delta(&qdec_b_delta_time);
	if (dt < CONFIG_QDEC_MINIMUM_DELTA_TIME_MSEC) {
		return;
	}
	long double delta_time = dt;
	qdec_b_rot_speed = (1000.0*(new_rot_b-rot_b)/delta_time);
	rot_b = new_rot_b;
	send_data_evt();
	
	k_timer_start(&qdec_b_timeout, K_MSEC(CONFIG_QDEC_TIMEOUT_DURATION_MSEC), K_NO_WAIT);
}

static int module_init(void)
{
	const struct device* qdeca_dev = device_get_binding(DT_LABEL(DT_NODELABEL(qdeca)));
	const struct device* qdecb_dev = device_get_binding(DT_LABEL(DT_NODELABEL(qdecb)));
	if (qdeca_dev == NULL || qdecb_dev == NULL)
	{
		LOG_ERR("Failed to get bindings for qdec devices");
		return -ENODEV;
	}

	trig_a.type = SENSOR_TRIG_DATA_READY;
	trig_a.chan = SENSOR_CHAN_ROTATION;

	trig_b.type = SENSOR_TRIG_DATA_READY;
	trig_b.chan = SENSOR_CHAN_ROTATION;
	int err;
	err = sensor_trigger_set(qdeca_dev, &trig_a, trigger_a_handler);
	if (err)
	{
		LOG_ERR("sensor_trigger_set for qdecb error: %d", err);
		return err;
	}

	err = sensor_trigger_set(qdecb_dev, &trig_b, trigger_b_handler);
	if (err)
	{
		LOG_ERR("sensor_trigger_set for qdecb error: %d", err);
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
                LOG_ERR("QDEC module init failed");

                return false;
            }
            LOG_INF("QDEC module initialized");
            int64_t current_time = k_uptime_get();
            qdec_a_delta_time = current_time;
            qdec_b_delta_time = current_time;
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