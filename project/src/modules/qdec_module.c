/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */


#include <zephyr/kernel.h>
#include <app_event_manager.h>
#include <zephyr/settings/settings.h>

#include "modules_common.h"
#include "events/qdec_module_event.h"

#define MODULE qdec_module
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_QDEC_MODULE_LOG_LEVEL);



static struct module_data self = {
	.name = "qdec",
	// .msg_q = &msgq_data,
	// .supports_shutdown = true,
};

static bool app_event_handler(const struct app_event_header *aeh)
{
	/* If event is unhandled, unsubscribe. */
	__ASSERT_NO_MSG(false);

	return false;
}

static int setup(void)
{
    //TODO
    return 0;
}

static void module_thread_fn(void)
{
	int err;

	self.thread_id = k_current_get();

	err = module_start(&self);
	if (err) {
		LOG_ERR("Failed starting module, error: %d", err);
		SEND_ERROR(qdec, QDEC_EVT_ERROR, err);
	}

	err = setup();
	if (err) {
		LOG_ERR("setup, error: %d", err);
		SEND_ERROR(qdec, QDEC_EVT_ERROR, err);
	}
    uint8_t simulated_val = 0;
	while (true) {
        simulated_val += 1;
		struct qdec_module_event *qdec_module_event = new_qdec_module_event();
		qdec_module_event->type = QDEC_EVT_DATA_SEND;
		qdec_module_event->data.rot_speed_val = simulated_val;
        APP_EVENT_SUBMIT(qdec_module_event);
        k_sleep(K_MSEC(500));
	}
}

K_THREAD_DEFINE(qdec_module_thread, CONFIG_QDEC_THREAD_STACK_SIZE,
		module_thread_fn, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

APP_EVENT_LISTENER(MODULE, app_event_handler);