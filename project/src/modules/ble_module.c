/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */


#include <zephyr/kernel.h>
#include <app_event_manager.h>
#include <zephyr/settings/settings.h>

#include "modules_common.h"
#include "events/ble_module_event.h"
#include "events/qdec_module_event.h"

#define MODULE ble_module
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_BLE_MODULE_LOG_LEVEL);

struct ble_msg_data {
	union {
		struct qdec_module_event qdec;
	} module;
};
/* Data module super states. */
static enum state_type {
	STATE_BLE_DISCONNECTED,
	STATE_BLE_CONNECTED,
	STATE_SHUTDOWN
} state;

/* Data module message queue. */
#define BLE_QUEUE_ENTRY_COUNT		10
#define BLE_QUEUE_BYTE_ALIGNMENT	4

K_MSGQ_DEFINE(msgq_data, sizeof(struct ble_msg_data),
	      BLE_QUEUE_ENTRY_COUNT, BLE_QUEUE_BYTE_ALIGNMENT);



static struct module_data self = {
	.name = "ble",
	.msg_q = &msgq_data,
	// .supports_shutdown = true,
};

/* Convenience functions used in internal state handling. */
static char *state2str(enum state_type new_state)
{
	switch (new_state) {
	case STATE_BLE_DISCONNECTED:
		return "STATE_BLE_DISCONNECTED";
	case STATE_BLE_CONNECTED:
		return "STATE_BLE_CONNECTED";
	case STATE_SHUTDOWN:
		return "STATE_SHUTDOWN";
	default:
		return "Unknown";
	}
}


static void state_set(enum state_type new_state)
{
	if (new_state == state) {
		LOG_DBG("State: %s", state2str(state));
		return;
	}

	LOG_DBG("State transition %s --> %s",
		state2str(state),
		state2str(new_state));

	state = new_state;
}

static bool app_event_handler(const struct app_event_header *aeh)
{

	struct ble_msg_data msg = {0};
	bool enqueue_msg = false;

	
	if (is_qdec_module_event(aeh)) {
		struct qdec_module_event *event = cast_qdec_module_event(aeh);
		msg.module.qdec = *event;
		enqueue_msg = true;
	}

	if (enqueue_msg) {
		int err = module_enqueue_msg(&self, &msg);

		if (err) {
			LOG_ERR("Message could not be enqueued");
			SEND_ERROR(ble, BLE_EVT_ERROR, err);
		}
	}

	return false;
}
/*		State handlers		*/

static void on_ble_state_disconnected(struct ble_msg_data *msg)
{
	return;
}

static void on_ble_state_connected(struct ble_msg_data *msg)
{
	return;
}

static void on_all_states(struct ble_msg_data *msg)
{
	if (IS_EVENT(msg, qdec, QDEC_EVT_DATA_SEND)) {
		LOG_DBG("Data received: %d", msg->module.qdec.data.rot_speed_val);
	}
}

static int setup(void)
{
    //TODO
    return 0;
}

static void module_thread_fn(void)
{
	int err;
	struct ble_msg_data msg;
	self.thread_id = k_current_get();

	err = module_start(&self);
	if (err) {
		LOG_ERR("Failed starting module, error: %d", err);
		SEND_ERROR(ble, BLE_EVT_ERROR, err);
	}

	err = setup();
	if (err) {
		LOG_ERR("setup, error: %d", err);
		SEND_ERROR(ble, BLE_EVT_ERROR, err);
	}

	while (true) {
		module_get_next_msg(&self, &msg);

		switch (state) {
		case STATE_BLE_DISCONNECTED:
			on_ble_state_disconnected(&msg);
			break;
		case STATE_BLE_CONNECTED:
			on_ble_state_connected(&msg);
			break;
		case STATE_SHUTDOWN:
			/* The shutdown state has no transition. */
			break;
		default:
			LOG_WRN("Unknown sub state.");
			break;
		}

		on_all_states(&msg);
	}
}

K_THREAD_DEFINE(ble_module_thread, CONFIG_BLE_THREAD_STACK_SIZE,
		module_thread_fn, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, qdec_module_event);