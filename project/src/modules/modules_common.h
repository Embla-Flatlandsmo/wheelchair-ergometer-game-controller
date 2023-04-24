/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _MODULES_COMMON_H_
#define _MODULES_COMMON_H_

/**@file
 *@brief Modules common library header.
 */

#include <zephyr/kernel.h>

/**
 * @defgroup modules_common Modules common library
 * @{
 * @brief A Library that exposes generic functionality shared between modules.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Structure that contains module metadata. */
struct module_data {
	/* Variable used to construct a linked list of module metadata. */
	sys_snode_t header;
	/* ID specific to each module. Internally assigned when calling module_start(). */
	uint32_t id;
	/* The ID of the module thread. */
	k_tid_t thread_id;
	/* Name of the module. */
	char *name;
	/* Pointer to the internal message queue in the module. */
	struct k_msgq *msg_q;
	/* Flag signifying if the module supports shutdown. */
	bool supports_shutdown;
};

/** @brief Purge a module's queue.
 *
 *  @param[in] module Pointer to a structure containing module metadata.
 *
 *  @return 0 if successful, otherwise a negative error code.
 */
void module_purge_queue(struct module_data *module);

/** @brief Get the next message in a modules's queue.
 *
 *  @param[in] module Pointer to a structure containing module metadata.
 *  @param[out] msg Pointer to a message buffer that the output will be written to.
 *
 *  @return 0 if successful, otherwise a negative error code.
 */
int module_get_next_msg(struct module_data *module, void *msg);


/** @brief Non-blocking get of next message in a module's queue.
 *
 *  @param[in] module Pointer to a structure containing module metadata.
 *  @param[out] msg Pointer to a message buffer that the output will be written to.
 *
 *  @return 0 if a message was read, -EAGAIN if no message was received.
 */
int module_get_next_msg_no_wait(struct module_data *module, void *msg);

/** @brief Enqueue message to a module's queue.
 *
 *  @param[in] module Pointer to a structure containing module metadata.
 *  @param[in] msg Pointer to a message that will be enqueued.
 *
 *  @return 0 if successful, otherwise a negative error code.
 */
int module_enqueue_msg(struct module_data *module, void *msg);

/** @brief Enqueue message to a module's queue.
 *
 *  @param[in] module Pointer to a structure containing module metadata.
 *  @param[in] msg Pointer to a message that will be enqueued.
 *  @param[in] delay_msec Number of milliseconds to wait before submitting message 
 *
 *  @return 0 if successful, otherwise a negative error code.
 */
int module_enqueue_msg_with_delay(struct module_data *module, void *msg, k_timeout_t delay_msec);
#endif /* _MODULES_COMMON_H_ */
