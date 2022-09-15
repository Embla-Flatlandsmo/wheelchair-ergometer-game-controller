/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/types.h>

#include "hid_report_desc.h"

const uint8_t hid_report_desc[] = {
	0x05, 0x01, /* Usage Page (Generic Desktop) */
	0x09, 0x05, /* Usage (Game Pad) */

	0xA1, 0x01, /* Collection (Application) */
	0xa1, 0x00, /* Collection (Physical) */
	0x85, 0x01, /* Report Id 1 */
	0x09, 0x01, /* Usage (Pointer) */
	// 0x05, 0x01, /* Usage Page (Generic Desktop) */
	0x15, 0x00,        /* Logical Minimum (0) */
	0x26, 0xFF, 0x00,  /* Logical Maximum (255) */
	0x09, 0x30, /* Usage (X) */
	0x09, 0x31, /* Usage (Y) */
	0x75, 0x08, /* Report Size (8) */
	0x95, 0x02, /* Report Count (2) */
	0x81, 0x02, /* Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position) */
	0xC0,		/* End Collection (Physical) */
	0xC0,		/* End Collection (Application) */
};

const size_t hid_report_desc_size = sizeof(hid_report_desc);
