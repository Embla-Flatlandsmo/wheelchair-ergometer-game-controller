/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/types.h>

#include "hid_report_desc.h"

const uint8_t hid_report_desc[]= {
	0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
	0x09, 0x05,                    // USAGE (Game Pad)
	0xA1, 0x01,                    // COLLECTION (Application)
	0xA1, 0x00,                    //   COLLECTION (Physical)
	0x85, 0x01,                	   //     REPORT_ID (1)
	0x05, 0x09,                    //     USAGE_PAGE (Button)
	0x19, 0x01,                    //     USAGE_MINIMUM (Button 1)
	0x29, 0x10,                    //     USAGE_MAXIMUM (Button 16)
	0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
	0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
	0x75, 0x01,                    //     REPORT_SIZE (1)
	0x95, 0x10,                    //     REPORT_COUNT (16)
	0x81, 0x02,                    //     INPUT (Data,Var,Abs)
	0x85, 0x02,                	   //     REPORT_ID (2)
	0x05, 0x01,                    //     USAGE_PAGE (Generic Desktop)
	0x09, 0x30,                    //     USAGE (X)
	0x09, 0x31,                    //     USAGE (Y)
 	0x09, 0x32,					   //     Usage (Z)
 	0x09, 0x33,					   //     Usage (Rx)
	0x15, 0x00,       			   //	  Logical Minimum (0)
	0x26, 0xFF, 0x00, 			   //	  Logical Maximum (255)
	0x75, 0x08,                    //     REPORT_SIZE (8)
	0x95, 0x04,                    //     REPORT_COUNT (4)
	0x81, 0x02,                    //     INPUT (Data,Var,Abs)
	0xC0,                          //     END_COLLECTION
	0xC0                           // END_COLLECTION
};

const size_t hid_report_desc_size = sizeof(hid_report_desc);
