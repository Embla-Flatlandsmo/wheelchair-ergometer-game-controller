/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _HID_REPORT_DESC_H_
#define _HID_REPORT_DESC_H_

#include <stddef.h>
#include <zephyr/types.h>
#include <zephyr/toolchain/common.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t hid_report_desc[];
extern const size_t hid_report_desc_size;

#ifdef __cplusplus
}
#endif

#endif /* _HID_REPORT_DESC_H_ */
