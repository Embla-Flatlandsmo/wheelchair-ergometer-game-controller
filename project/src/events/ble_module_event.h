/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _BLE_MODULE_EVENT_H_
#define _BLE_MODULE_EVENT_H_

/**
 * @brief BLE module event
 * @defgroup ble_module_event BLE module event
 * @{
 */

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief BLE event types submitted by BLE module. */
enum ble_module_event_type {
    BLE_EVT_CONNECTED,
    BLE_EVT_PAIRING,
    BLE_EVT_DISCONNECTED,

	/** The data module has performed all procedures to prepare for
	 *  a shutdown of the system. The event carries the ID (id) of the module.
	 */
	BLE_EVT_SHUTDOWN_READY,

	/** An irrecoverable error has occurred in the data module. Error details are
	 *  attached in the event structure.
	 */
	BLE_EVT_ERROR
};

/** @brief Data module event. */
struct ble_module_event {
	/** Data module application event header. */
	struct app_event_header header;
	/** Data module event type. */
	enum ble_module_event_type type;

	union {
		/** Module ID, used when acknowledging shutdown requests. */
		uint32_t id;
		/** Code signifying the cause of error. */
		int err;
	} data;
};

APP_EVENT_TYPE_DECLARE(ble_module_event);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _BLE_MODULE_EVENT_H_ */
