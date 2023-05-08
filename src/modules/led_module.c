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

typedef enum
{
	RED,
	GREEN,
	BLUE,
	ORANGE,
	BLACK,
} led_color_t;

const float light_intensity = 0.1;

static const struct led_rgb colors[] = {
	{ .r = 25, 			.g = 0,			.b = 0,		}, /* red */
	{ .r = 0,			.g = 25,		.b = 0,		}, /* green */
	{ .r = 0,			.g = 0,			.b = 25,	}, /* blue */
	{ .r = 25,			.g = 17,		.b = 0,		}, /* orange */
	{ .r = 0,			.g = 0,			.b = 0,		}, /* black */
};

enum blink_type_index {
	ACTIVE,
	BACKGROUND,
};

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
	int blink_type;
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

void led_blink_once(int blink_length_msec, led_color_t color)
{
	set_light_color(color);
	k_sleep(K_MSEC(blink_length_msec));
	set_light_color(BLACK);
	k_sleep(K_MSEC((int)(blink_length_msec/2.0)));
}

/**
 * @brief Blink the LED with a number of repeats, length and color-
 * 		The black period is always half the length of the blink length.
 * 
 * @param num_blinks Number of times to blink the LED
 * @param blink_length_msec msec the LED should blink
 * @param color color to blink the LED
 */
void led_blink(int num_blinks, int blink_length_msec, led_color_t color)
{
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

/**
 * @brief Mapping for blinking Bluetooth peer events
 * 
 * @param event bluetooth peer event
 * @return struct led_msg_data color, number of blinks and length to blink
 */
static struct led_msg_data blink_data_from_peer_event(const struct ble_peer_event *event)
{
	struct led_msg_data blink_data;
	blink_data.blink_type = ACTIVE;
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
			blink_data.blink_duration_msec = MEDIUM_BLINK;
			break;
		default:
			blink_data.blink_color = BLACK;
			blink_data.num_blinks = 0;
			blink_data.blink_duration_msec = 0;
			break;
	}
	return blink_data;
}

/**
 * @brief Mapping for bluetooth search events and blinking
 *
 * @param event CAF ble peer search event
 * @return struct led_msg_data color, number of blinks and length to blink
 */
static struct led_msg_data blink_data_from_peer_search_event(const struct ble_peer_search_event *event)
{
	struct led_msg_data blink_data;
	blink_data.blink_type = BACKGROUND;
	if (event->active)
	{
		blink_data.blink_color = BLUE;
		blink_data.num_blinks = 10;
		blink_data.blink_duration_msec = LONG_BLINK;
	} else {
		blink_data.blink_color = BLACK;
		blink_data.num_blinks = 0;
		blink_data.blink_duration_msec = 0;
	}
	return blink_data;
}

/**
 * @brief Main event handler for module
 * 
 * @param aeh one of the subscribed events
 * @return true the event is consumed
 * @return false the event is not consumed (default)
 */
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

/**
 * @brief Main thread of module. Receives messages and blinks.
 * 
 */
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
	led_blink(1, LONG_BLINK, GREEN);
	int current_blink_type = BACKGROUND;
	int remaining_blinks = 0;
	while (true) {
		if (current_blink_type == BACKGROUND)
		{
			err = module_get_next_msg_no_wait(&self, &msg);
			if (!err)
			{
			current_blink_type = msg.blink_type;
			remaining_blinks = msg.num_blinks;
			}
		}
		if (remaining_blinks == 0)
		{
			module_get_next_msg(&self, &msg);
			current_blink_type = msg.blink_type;
			remaining_blinks = msg.num_blinks;
		}
		led_blink_once(msg.blink_duration_msec, msg.blink_color);
		remaining_blinks--;
	}
}

K_THREAD_DEFINE(led_module_thread, CONFIG_LED_MODULE_THREAD_STACK_SIZE,
		module_thread_fn, NULL, NULL, NULL,
		K_HIGHEST_APPLICATION_THREAD_PRIO, 0, 0);

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
APP_EVENT_SUBSCRIBE(MODULE, ble_peer_event);
APP_EVENT_SUBSCRIBE(MODULE, ble_peer_search_event);