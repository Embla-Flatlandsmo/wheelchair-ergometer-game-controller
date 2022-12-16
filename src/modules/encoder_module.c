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

struct device* encoder_a_dev;
struct device* encoder_b_dev;

static float encoder_a_rot_speed = 0.0;
static float encoder_b_rot_speed = 0.0;

#define DT_MSEC CONFIG_ENCODER_DELTA_TIME_MSEC
static const float alpha = ((float)CONFIG_ENCODER_MOVING_AVERAGE_ALPHA)/1000.0;
static const float dt = (float)DT_MSEC/1000.0;

static void send_data_evt(void)
{
	struct encoder_module_event *encoder_module_event = new_encoder_module_event();
	encoder_module_event->type = ENCODER_EVT_DATA_READY;
	encoder_module_event->rot_speed_a = encoder_a_rot_speed;
	encoder_module_event->rot_speed_b = encoder_b_rot_speed;
	APP_EVENT_SUBMIT(encoder_module_event);
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

/**
 * @brief Processes data upon timeout of the periodic poller
 * 
 * @param work 
 */
void data_evt_timeout_work_handler(struct k_work *work)
{
	struct sensor_value rot_a, rot_b;
	int err;
	err = sensor_sample_fetch(encoder_a_dev);
	if (err != 0)
	{
		LOG_ERR("Encoder A sensor_sample_fetch error: %d\n", err);
		return;
	}

	err = sensor_sample_fetch(encoder_b_dev);
	if (err != 0)
	{
		LOG_ERR("Encoder B sensor_sample_fetch error: %d\n", err);
		return;
	}

	err = sensor_channel_get(encoder_a_dev, SENSOR_CHAN_ROTATION, &rot_a);
	if (err != 0)
	{
		LOG_ERR("Encoder A sensor_channel_get error: %d\n", err);
		return;
	}

	float encoder_a_rot_delta = sensor_value_to_double(&rot_a);
	float encoder_a_current_speed = encoder_a_rot_delta/dt;
	encoder_a_rot_speed = moving_avg_filter(encoder_a_rot_speed, encoder_a_current_speed);

	err = sensor_channel_get(encoder_b_dev, SENSOR_CHAN_ROTATION, &rot_b);
	if (err != 0)
	{
		LOG_ERR("Encoder B sensor_channel_get error: %d\n", err);
		return;
	}

	float encoder_b_rot_delta = sensor_value_to_double(&rot_b);
	float encoder_b_current_speed = encoder_b_rot_delta/dt;
	encoder_b_rot_speed = moving_avg_filter(encoder_b_rot_speed, encoder_b_current_speed);
	LOG_DBG("Encoder B rot speed: %f", encoder_b_rot_speed);
	send_data_evt();
}


static int module_init(void)
{
	encoder_a_dev = device_get_binding(DT_LABEL(DT_NODELABEL(qdeca)));
	encoder_b_dev = device_get_binding(DT_LABEL(DT_NODELABEL(qdecb)));
	if (encoder_a_dev == NULL || encoder_b_dev == NULL)
	{
		LOG_ERR("Failed to get bindings for encoder devices");
		return -ENODEV;
	}
	k_timer_start(&data_evt_timeout, K_NO_WAIT, K_MSEC(DT_MSEC));
	return 0;
}

/**
 * @brief Main event listener for the module. Receives and processes all events.
 */
static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_module_state_event(aeh)) {
		// Device startup event
		const struct module_state_event *event = cast_module_state_event(aeh);

		if (check_state(event, MODULE_ID(main), MODULE_STATE_READY)) {
			
            if (module_init())
            {
                LOG_ERR("ENCODER module init failed");

                return false;
            }
            LOG_INF("ENCODER module initialized");
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