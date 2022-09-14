/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _QDEC_MODULE_EVENT_H_
#define _QDEC_MODULE_EVENT_H_

/**
 * @brief QDEC module event
 * @defgroup data_module_event Qdec module event
 * @{
 */

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief QDEC event types submitted by QDEC module. */
enum qdec_module_event_type {
	/** All data has been received for a given sample request. */
	QDEC_EVT_DATA_READY,

	/** Send newly sampled data.
	 *  The event has an associated payload of type @ref data_module_data_buffers in
	 *  the `data.buffer` member.
	 *
	 *  If a non LwM2M build is used the data is heap allocated and must be freed after use by
	 *  calling k_free() on `data.buffer.buf`.
	 */
	QDEC_EVT_DATA_SEND,

	/** The data module has performed all procedures to prepare for
	 *  a shutdown of the system. The event carries the ID (id) of the module.
	 */
	QDEC_EVT_SHUTDOWN_READY,

	/** An irrecoverable error has occurred in the data module. Error details are
	 *  attached in the event structure.
	 */
	QDEC_EVT_ERROR
};

/** @brief Data module event. */
struct qdec_module_event {
	/** Data module application event header. */
	struct app_event_header header;
	/** Data module event type. */
	enum qdec_module_event_type type;

	union {
		uint8_t rot_speed_val;
		/** Module ID, used when acknowledging shutdown requests. */
		uint32_t id;
		/** Code signifying the cause of error. */
		int err;
	} data;
};

APP_EVENT_TYPE_DECLARE(qdec_module_event);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _QDEC_MODULE_EVENT_H_ */
