/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>

#include "qdec_module_event.h"
#include "common_module_event.h"

static char *get_evt_type_str(enum qdec_module_event_type type)
{
	switch (type) {
	case QDEC_A_EVT_DATA_SEND:
		return "QDEC_A_EVT_DATA_SEND";
	case QDEC_B_EVT_DATA_SEND:
		return "QDEC_B_EVT_DATA_SEND";
	case QDEC_EVT_DATA_READY:
		return "QDEC_EVT_DATA_READY";
	case QDEC_EVT_SHUTDOWN_READY:
		return "QDEC_EVT_SHUTDOWN_READY";
	case QDEC_EVT_ERROR:
		return "QDEC_EVT_ERROR";
	default:
		return "Unknown event";
	}
}

static void log_event(const struct app_event_header *aeh)
{
	const struct qdec_module_event *event = cast_qdec_module_event(aeh);

	if (event->type == QDEC_EVT_ERROR) {
		APP_EVENT_MANAGER_LOG(aeh, "%s - Error code %d",
				get_evt_type_str(event->type), event->data.err);
	} else if (event->type == QDEC_EVT_DATA_READY) {
		APP_EVENT_MANAGER_LOG(aeh, "%s - (QDEC_A, QDEC_B)[deg/s] = (%f, %f)",
			get_evt_type_str(event->type), event->rot_speed_a, 
			event->rot_speed_b);
	}
	else {
		APP_EVENT_MANAGER_LOG(aeh, "%s", get_evt_type_str(event->type));
	}
}

#if defined(CONFIG_NRF_PROFILER)

static void profile_event(struct log_event_buf *buf,
			 const struct app_event_header *aeh)
{
	const struct qdec_module_event *event = cast_qdec_module_event(aeh);

#if defined(CONFIG_NRF_PROFILER_EVENT_TYPE_STRING)
	nrf_profiler_log_encode_string(buf, get_evt_type_str(event->type));
#else
	nrf_profiler_log_encode_uint8(buf, event->type);
#endif
}

COMMON_APP_EVENT_INFO_DEFINE(qdec_module_event,
			 profile_event);

#endif /* CONFIG_NRF_PROFILER */

COMMON_APP_EVENT_TYPE_DEFINE(qdec_module_event,
			 log_event,
			 &qdec_module_event_info,
			 APP_EVENT_FLAGS_CREATE(
				IF_ENABLED(CONFIG_QDEC_EVENTS_LOG,
					(APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE))));
