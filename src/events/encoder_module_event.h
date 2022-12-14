/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _ENCODER_MODULE_EVENT_H_
#define _ENCODER_MODULE_EVENT_H_

/**
 * @brief ENCODER module event
 * @defgroup data_module_event Encoder module event
 * @{
 */

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief ENCODER event types submitted by ENCODER module. */
enum encoder_module_event_type {
	/** All data has been received for a given sample request. */
	ENCODER_EVT_DATA_READY,

	/** Send newly sampled data.
	 *  The event has an associated payload of type @ref data_module_data_buffers in
	 *  the `data.buffer` member.
	 *
	 *  If a non LwM2M build is used the data is heap allocated and must be freed after use by
	 *  calling k_free() on `data.buffer.buf`.
	 */
	ENCODER_A_EVT_DATA_SEND,
	ENCODER_B_EVT_DATA_SEND,

	/** The data module has performed all procedures to prepare for
	 *  a shutdown of the system. The event carries the ID (id) of the module.
	 */
	ENCODER_EVT_SHUTDOWN_READY,

	/** An irrecoverable error has occurred in the data module. Error details are
	 *  attached in the event structure.
	 */
	ENCODER_EVT_ERROR
};

/** @brief Data module event. */
struct encoder_module_event {
	/** Data module application event header. */
	struct app_event_header header;
	/** Data module event type. */
	enum encoder_module_event_type type;

	float rot_speed_a;
	float rot_speed_b;
	union {
		/** Module ID, used when acknowledging shutdown requests. */
		uint32_t id;
		/** Code signifying the cause of error. */
		int err;
	} data;
};

APP_EVENT_TYPE_DECLARE(encoder_module_event);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _ENCODER_MODULE_EVENT_H_ */
