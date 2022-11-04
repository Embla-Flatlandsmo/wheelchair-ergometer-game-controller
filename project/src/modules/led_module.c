/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */


#include <zephyr/kernel.h>

#define MODULE led_module
#include <caf/events/module_state_event.h>
#include <caf/events/ble_common_event.h>
#include <app_event_manager.h>
#include <zephyr/settings/settings.h>
#include <drivers/sensor.h>
#include <drivers/led_strip.h>
#include <drivers/spi.h>
#include "modules_common.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_LED_MODULE_LOG_LEVEL);

const float light_intensity = 0.2;
static const struct led_rgb colors[] = {
	{ .r = 255*light_intensity, .g = 0,						.b = 0,						}, /* red */
	{ .r = 0,					.g = 255*light_intensity,	.b = 0,						}, /* green */
	{ .r = 0,					.g = 0,						.b = 255*light_intensity,	}, /* blue */
	{ .r = 255*light_intensity,	.g = 165*light_intensity,	.b = 0,						}, /* orange */
	{ .r = 0,					.g = 0,						.b = 0,						}, /* black */
};

typedef enum
{
	RED,
	GREEN,
	BLUE,
	ORANGE,
	BLACK,
} led_color_t;

#define SHORT_BLINK		CONFIG_LED_MODULE_BLINK_DURATION_SHORT_MSEC
#define MEDIUM_BLINK	CONFIG_LED_MODULE_BLINK_DURATION_MEDIUM_MSEC
#define LONG_BLINK		CONFIG_LED_MODULE_BLINK_DURATION_LONG_MSEC

/* LED module message queue. */
#define LED_QUEUE_ENTRY_COUNT		20
#define LED_QUEUE_BYTE_ALIGNMENT	4

struct led_msg_data {
	int num_blinks;
	int blink_duration_msec;
	led_color_t blink_color;
};


K_MSGQ_DEFINE(msgq_led, sizeof(struct led_msg_data),
	      LED_QUEUE_ENTRY_COUNT, LED_QUEUE_BYTE_ALIGNMENT);

static struct module_data self = {
	.name = "led",
	.msg_q = &msgq_led,
	.supports_shutdown = true
};

static const struct device* led_dev = DEVICE_DT_GET_ANY(apa_apa102);

void set_light_color(led_color_t color)
{
	struct led_rgb cols[1] = {
		colors[color],
	};
	led_strip_update_rgb(led_dev, cols, 1);
}

void led_blink(int num_blinks, int blink_length_msec, led_color_t color)
{
	// set_light_color(color);
	// k_sleep(K_MSEC(blink_length_msec));
	// set_light_color(BLACK)
	// // k_sleep(K_MSEC((int)(blink_length_msec/2.0)));
	// struct led_msg_data msg;
	// if (num_blinks == BLINK_INDEFINITE)
	// {
	// 	msg.num_blinks = num_blinks;
	// } else if (num_blinks == 0) {
	// 	return;
	// } else {
	// 	msg.num_blinks = num_blinks-1;
	// }
	// msg.blink_duration_msec = blink_length_msec;
	// msg.blink_color = color;

	// int err = module_enqueue_msg_with_delay(&self, &msg);
	for (int i = 0; i < num_blinks; i++)
	{
		set_light_color(color);
		k_sleep(K_MSEC(blink_length_msec));
		set_light_color(BLACK);
		k_sleep(K_MSEC((int)(blink_length_msec/2.0)));
	}
	return;
}

/*================= EVENT HANDLERS =================*/
static struct led_msg_data blink_data_from_peer_event(const struct ble_peer_event *event)
{
	struct led_msg_data blink_data;
	switch (event->state)
	{
		case PEER_STATE_DISCONNECTED:
			blink_data.blink_color = RED;
			blink_data.num_blinks = 2;
			blink_data.blink_duration_msec = SHORT_BLINK;
			break;
		case PEER_STATE_CONNECTED:
			blink_data.blink_color = ORANGE;
			blink_data.num_blinks = 2;
			blink_data.blink_duration_msec = MEDIUM_BLINK;
			break;
		case PEER_STATE_SECURED:
			blink_data.blink_color = GREEN;
			blink_data.num_blinks = 2;
			blink_data.blink_duration_msec = MEDIUM_BLINK;
			break;
		case PEER_STATE_CONN_FAILED:
			blink_data.blink_color = RED;
			blink_data.num_blinks = 5;
			blink_data.blink_duration_msec = SHORT_BLINK;
			break;
		default:
			blink_data.blink_color = BLACK;
			blink_data.num_blinks = 0;
			blink_data.blink_duration_msec = 0;
			break;
	}
	return blink_data;
}


static struct led_msg_data blink_data_from_peer_search_event(const struct ble_peer_search_event *event)
{
	struct led_msg_data blink_data;
	if (event->active)
	{
		blink_data.blink_color = BLUE;
		blink_data.num_blinks = 2;
		blink_data.blink_duration_msec = LONG_BLINK;
	} else {
		blink_data.blink_color = BLACK;
		blink_data.num_blinks = 0;
		blink_data.blink_duration_msec = 0;
	}
	return blink_data;
}

static bool app_event_handler(const struct app_event_header *aeh)
{
	struct led_msg_data msg = {0};
	bool enqueue_msg = false;

	if (is_ble_peer_event(aeh))
	{
		msg = blink_data_from_peer_event(cast_ble_peer_event(aeh));
		enqueue_msg = true;
	}
	if (is_ble_peer_search_event(aeh))
	{
		msg = blink_data_from_peer_search_event( cast_ble_peer_search_event(aeh));
		enqueue_msg = true;
	}

	if (is_module_state_event(aeh)) {
		const struct module_state_event *event = cast_module_state_event(aeh);

		if (check_state(event, MODULE_ID(main), MODULE_STATE_READY)) {
			/* No setup necessary since it is done in module_thread_fn */
			module_set_state(MODULE_STATE_READY);
		}
		return false;
	}

	if (enqueue_msg) {
		int err = module_enqueue_msg(&self, &msg);

		if (err) {
			LOG_ERR("Message could not be enqueued");
		}
		return false;
	}


	/* Event not handled but subscribed. */
	__ASSERT_NO_MSG(false);
	return false;
}


/*================= MODULE THREAD =================*/
static int setup(void)
{
	if (!led_dev) {
		LOG_ERR("LED Device not found");
		return -ENODEV;
	}
	if (!device_is_ready(led_dev))
	{
		LOG_ERR("LED Device not ready");
		return -ENODEV;
	}
	set_light_color(BLACK);
	return 0;
}

static void module_thread_fn(void)
{

	int err;
	struct led_msg_data msg;

	self.thread_id = k_current_get();

    LOG_DBG("Initializing LED module");
	err = setup();
	if (err) {
		LOG_ERR("setup, error: %d", err);
	}
	LOG_DBG("LED Module initialized");
	led_blink(3, SHORT_BLINK, GREEN);
	while (true) {
		module_get_next_msg(&self, &msg);
		led_blink(msg.num_blinks, msg.blink_duration_msec, msg.blink_color);
	}
}


K_THREAD_DEFINE(led_module_thread, CONFIG_LED_MODULE_THREAD_STACK_SIZE,
		module_thread_fn, NULL, NULL, NULL,
		K_HIGHEST_APPLICATION_THREAD_PRIO, 0, 0);

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
APP_EVENT_SUBSCRIBE(MODULE, ble_peer_event);
APP_EVENT_SUBSCRIBE(MODULE, ble_peer_search_event);