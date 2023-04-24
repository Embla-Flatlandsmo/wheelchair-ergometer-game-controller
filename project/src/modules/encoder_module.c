/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */


#include <zephyr/kernel.h>
#include <float.h>

#define MODULE encoder_module
#include <caf/events/module_state_event.h>
#include <app_event_manager.h>
#include <zephyr/settings/settings.h>
#include <drivers/sensor.h>
#include "modules_common.h"
#include "events/encoder_module_event.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_ENCODER_MODULE_LOG_LEVEL);

/**This code uses a left-handed coordinate system. To find the positive
 * spinning direction, point your left hand 
 * This means that when going forwards, the encoder values will read a positive
 * rotational speed but the wheels will have a (technically) negative rotational speed. 
 */

struct device* encoder_a_dev;
struct device* encoder_b_dev;

static float encoder_a_rot_speed = 0.0;
static float encoder_b_rot_speed = 0.0;
static float cumulative_encoder_b = 0.0;

#define DT_MSEC CONFIG_ENCODER_DELTA_TIME_MSEC
static const float alpha = ((float)CONFIG_ENCODER_MOVING_AVERAGE_ALPHA)/1000.0;
static const float dt = (float)DT_MSEC/1000.0;


#if CONFIG_ENCODER_SIMULATE_INPUT
#define MAX_SIMULATED_ENCODER_TICKS CONFIG_ENCODER_SIMULATE_INPUT_INTERVAL
#else
#define  MAX_SIMULATED_ENCODER_TICKS 0
#endif

static float simulated_encoder_value = 1000000.0;
static int simulated_encoder_ticks = 0;

/**
 * @brief Sends the encoder values from the encoder module to other listening modules
 */
static void send_data_evt(void)
{
	struct encoder_module_event *encoder_module_event = new_encoder_module_event();
	encoder_module_event->type = ENCODER_EVT_DATA_READY;
	encoder_module_event->rot_speed_a = encoder_a_rot_speed;
	encoder_module_event->rot_speed_b = encoder_b_rot_speed;
	APP_EVENT_SUBMIT(encoder_module_event);
}

/**
 * @brief Filters the value using first order IIR filter
 *         y[t]=alpha*y[t-1]+(1-alpha)*x[t]
 * 
 * @param y_prev Previous output value
 * @param x New sampled value
 * @return float Outut value for the current time step
 */
static float moving_avg_filter(float y_prev, float x)
{
	return alpha*y_prev + (1-alpha)*x;
}


static void data_evt_timeout_work_handler(struct k_work *work);
K_WORK_DEFINE(data_evt_timeout_work, data_evt_timeout_work_handler);

void data_evt_timeout_handler(struct k_timer *dummy)
{
    k_work_submit(&data_evt_timeout_work);
}

K_TIMER_DEFINE(data_evt_timeout, data_evt_timeout_handler, NULL);

/**
 * @brief Functions that run upon the expiration of each sampling interval
 * 
 * @param work idk
 */
void data_evt_timeout_work_handler(struct k_work *work)
{
	if (IS_ENABLED(CONFIG_ENCODER_SIMULATE_INPUT))
	{
		float encoder_a_current_speed = simulated_encoder_value/dt;
		encoder_a_rot_speed = moving_avg_filter(encoder_a_rot_speed, encoder_a_current_speed);

		float encoder_b_current_speed = simulated_encoder_value/dt;
		encoder_b_rot_speed = moving_avg_filter(encoder_b_rot_speed, encoder_b_current_speed);
		send_data_evt();

		simulated_encoder_ticks++;
		if (simulated_encoder_ticks >= MAX_SIMULATED_ENCODER_TICKS)
		{
			simulated_encoder_value *= -1.0;
			simulated_encoder_ticks = 0;
		}
		return;
	}


	struct sensor_value rot_a, rot_b;
	int err;
	err = sensor_sample_fetch(encoder_a_dev);
	if (err != 0)
	{
		LOG_ERR("Encoder A sensor_sample_fetch error: %d\n", err);
		return;
	}

	err = sensor_channel_get(encoder_a_dev, SENSOR_CHAN_ROTATION, &rot_a);
	if (err != 0)
	{
		LOG_ERR("Encoder A sensor_channel_get error: %d\n", err);
		return;
	}
	float encoder_a_rot_delta = (float)sensor_value_to_double(&rot_a);
	float encoder_a_current_speed = encoder_a_rot_delta/dt;
	encoder_a_rot_speed = moving_avg_filter(encoder_a_rot_speed, encoder_a_current_speed);
	LOG_DBG("Encoder A rot speed: %f", encoder_a_rot_speed);

	err = sensor_sample_fetch(encoder_b_dev);
	if (err != 0)
	{
		LOG_ERR("Encoder B sensor_sample_fetch error: %d\n", err);
		return;
	}

	err = sensor_channel_get(encoder_b_dev, SENSOR_CHAN_ROTATION, &rot_b);
	if (err != 0)
	{
		LOG_ERR("Encoder B sensor_channel_get error: %d\n", err);
		return;
	}

	float encoder_b_rot_delta = (float)sensor_value_to_double(&rot_b);
	float encoder_b_current_speed = encoder_b_rot_delta/dt;
	encoder_b_rot_speed = moving_avg_filter(encoder_b_rot_speed, encoder_b_current_speed);
	LOG_DBG("Encoder B rot speed: %f", encoder_b_rot_speed);
	send_data_evt();
}

static int module_init(void)
{
	if (!IS_ENABLED(CONFIG_ENCODER_SIMULATE_INPUT))
	{
		encoder_a_dev = device_get_binding(DT_LABEL(DT_NODELABEL(qdeca)));
		encoder_b_dev = device_get_binding(DT_LABEL(DT_NODELABEL(qdecb)));
		if (encoder_a_dev == NULL || encoder_b_dev == NULL)
		{
			LOG_ERR("Failed to get bindings for encoder devices");
			return -ENODEV;
		}
	} else {
		LOG_DBG("Using simulated encoder inputs");
	}

	k_timer_start(&data_evt_timeout, K_NO_WAIT, K_MSEC(DT_MSEC));
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