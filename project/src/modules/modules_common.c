/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <app_event_manager.h>
#include "modules_common.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(modules_common, CONFIG_MODULES_COMMON_LOG_LEVEL);

struct event_prototype {
	struct app_event_header header;
	uint8_t event_id;
};

/* Structure containing general information about the modules in the application. */
static struct modules_info {
	/* Modules that support shutdown. */
	atomic_t shutdown_supported_count;
	/* Number of active modules in the application. */
	atomic_t active_modules_count;
} modules_info;

/* Public interface */
void module_purge_queue(struct module_data *module)
{
	k_msgq_purge(module->msg_q);
}

int module_get_next_msg(struct module_data *module, void *msg)
{
	int err = k_msgq_get(module->msg_q, msg, K_FOREVER);

	if (err == 0 && IS_ENABLED(CONFIG_MODULES_COMMON_LOG_LEVEL_DBG)) {
		struct event_prototype *evt_proto =
			(struct event_prototype *)msg;
		struct event_type *event =
			(struct event_type *)evt_proto->header.type_id;

		if (event->log_event_func) {
			event->log_event_func(&evt_proto->header);
		}
#ifdef CONFIG_APP_EVENT_MANAGER_USE_DEPRECATED_LOG_FUN
		else if (event->log_event_func_dep) {
			char buf[50];

			event->log_event_func_dep(&evt_proto->header, buf, sizeof(buf));
			LOG_DBG("%s module: Dequeued %s",
				module->name,
				log_strdup(buf));
		}
#endif
	}
	return err;
}


int module_get_next_msg_no_wait(struct module_data *module, void *msg)
{
	int err = k_msgq_get(module->msg_q, msg, K_NO_WAIT);
	if (err)
	{
		return err;
	}

	if (err == 0 && IS_ENABLED(CONFIG_MODULES_COMMON_LOG_LEVEL_DBG)) {
		struct event_prototype *evt_proto =
			(struct event_prototype *)msg;
		struct event_type *event =
			(struct event_type *)evt_proto->header.type_id;

		if (event->log_event_func) {
			event->log_event_func(&evt_proto->header);
		}
	#ifdef CONFIG_APP_EVENT_MANAGER_USE_DEPRECATED_LOG_FUN
			else if (event->log_event_func_dep) {
				char buf[50];

				event->log_event_func_dep(&evt_proto->header, buf, sizeof(buf));
				LOG_DBG("%s module: Dequeued %s",
					module->name,
					log_strdup(buf));
			}
	#endif
		}
	return err;
}

int module_enqueue_msg_with_delay(struct module_data *module, void *msg, k_timeout_t delay_msec)
{
	int err;

	err = k_msgq_put(module->msg_q, msg, delay_msec);
	if (err) {
		LOG_WRN("%s: Message could not be enqueued, error code: %d",
			module->name, err);
			/* Purge message queue before reporting an error. This
			 * makes sure that the calling module can
			 * enqueue and process new events and is not being
			 * blocked by a full message queue.
			 *
			 * This error is concidered irrecoverable and should be
			 * rebooted on.
			 */
			module_purge_queue(module);
		return err;
	}

	if (IS_ENABLED(CONFIG_MODULES_COMMON_LOG_LEVEL_DBG)) {
		struct event_prototype *evt_proto = (struct event_prototype *)msg;
		struct event_type *event = (struct event_type *)evt_proto->header.type_id;

		if (event->log_event_func) {
			event->log_event_func(&evt_proto->header);
		}
#ifdef CONFIG_APP_EVENT_MANAGER_USE_DEPRECATED_LOG_FUN
		else if (event->log_event_func_dep) {
			char buf[50];

			event->log_event_func_dep(&evt_proto->header, buf, sizeof(buf));
			LOG_DBG("%s module: Dequeued %s",
				module->name,
				log_strdup(buf));
		}
#endif
	}

	return 0;
}

int module_enqueue_msg(struct module_data *module, void *msg)
{
	return module_enqueue_msg_with_delay(module, msg, K_NO_WAIT);
}