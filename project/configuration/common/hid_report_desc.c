/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/types.h>

#include "hid_report_desc.h"

// const uint8_t hid_report_desc[] = {
// 	0x05, 0x01, /* Usage Page (Generic Desktop) */
// 	0x09, 0x05, /* Usage (Game Pad) */

// 	0xA1, 0x01, /* Collection (Application) */
// 	0xa1, 0x00, /* Collection (Physical) */
// 	0x85, 0x01, /* Report Id 1 */
// 	0x09, 0x01, /* Usage (Pointer) */
// 	// 0x05, 0x01, /* Usage Page (Generic Desktop) */
// 	0x15, 0x00,        /* Logical Minimum (0) */
// 	0x26, 0xFF, 0x00,  /* Logical Maximum (255) */
// 	0x09, 0x30, /* Usage (X) */
// 	0x09, 0x31, /* Usage (Y) */
// 	0x75, 0x08, /* Report Size (8) */
// 	0x95, 0x02, /* Report Count (2) */
// 	0x81, 0x02, /* Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position) */
// 	0xC0,		/* End Collection (Physical) */
// 	0xC0,		/* End Collection (Application) */
// };

// const uint8_t hid_report_desc[] = {
// 	0x05, 0x01,			// Usage Page (Generic Desktop)
// 	0x09, 0x05,			// Usage (Game Pad)
// 	0xA1, 0x01,			// Collection (Application)
// 	0x85, 0x01,			// Report Id 1
	
// 	/* JOYSTICK 1 */
// 	0x09, 0x01,			// Usage (Pointer)
// 	// 0xA1, 0x00,		// Collection (Physical)
// 	0x09, 0x30,			// Usage (X)
// 	0x09, 0x31,			// Usage (Y)
// 	0x09, 0x32,         // Usage (Z)
// 	0x09, 0x35,         // Usage (Rz)
// 	0x15, 0x00,       	// Logical Minimum (0)
// 	0x26, 0xFF, 0x00, 	// Logical Maximum (255)
// 	0x75, 0x08,			// Report Size (8)
// 	0x95, 0x04,			// Report Count (4)
// 	0x81, 0x02,			// Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
// 	// 0xC0,				// End Collection (Physical)

// 	// /* JOYSTICK 2 */
// 	// 0x09, 0x01,        // Usage (Pointer)
// 	// 0xA1, 0x00,        // Collection (Physical)
// 	// 0x09, 0x32,        // Usage (Z)
// 	// 0x09, 0x35,        // Usage (Rz)
// 	// 0x15, 0x00,        // Logical Minimum (0)
// 	// 0x26, 0xFF, 0x00,  // Logical Maximum (255)
// 	// 0x75, 0x08,		   // Report Size (8)
// 	// 0x95, 0x02,		   // Report Count (2)
// 	// 0x81, 0x02,        // Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
// 	// 0xC0,              // End Collection (Physical)

// 	0x05, 0x09,		   // USAGE_PAGE (Button)
// 	0x19, 0x01,		   // USAGE_MINIMUM (Button 1)
// 	0x29, 0x08,		   // USAGE_MAXIMUM (Button 8)
// 	0x15, 0x00,		   // LOGICAL_MINIMUM (0)
// 	0x25, 0x01,		   // LOGICAL_MAXIMUM(1)
// 	0x95, 0x08,		   // REPORT_COUNT (8)
// 	0x75, 0x01,		   // REPORT_SIZE (1)
// 	0x81, 0x02,		   // INPUT(Data, Var, Abs)

// 	0xC0,				// End Collection (Application)
// };

// const uint8_t hid_report_desc[] = {
// 	0x05, 0x01,				// Usage Page (Generic Desktop)
// 	0x09, 0x05,				// Usage (Game Pad)
// 	0xA1, 0x01,				// Collection (Application)
// 	0x85, 0x01,				// Report ID (1)

// 	/* JOYSTICKS */
// 	0x09, 0x30,				// Usage (X)
// 	0x09, 0x31,				// Usage (Y)
// 	0x09, 0x32,				// Usage (Z)
// 	0x09, 0x35,				// Usage (Rz)
// 	0x15, 0x00,				// Logical Minimum (0)
// 	0x26, 0xFF, 0x00,		// Logical Maximum (255)
// 	0x75, 0x08,				// Report Size (8)
// 	0x95, 0x04,				// Report Count (4)
// 	0x81, 0x02,				// Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	
// 	/* D-PAD */
// 	0x09, 0x39,				// Usage (Hat switch)
// 	0x15, 0x00,				// Logical Minimum (0)
// 	0x25, 0x07,				// Logical Maximum (7)
// 	0x35, 0x00,				// Physical Minimum (0)
// 	0x46, 0x3B, 0x01,		// Physical Maximum (315)
// 	0x65, 0x14,				// Unit (System: English Rotation, Length: Centimeter)
// 	0x75, 0x04,				// Report Size (4)
// 	0x95, 0x01,				// Report Count (1)
// 	0x81, 0x42,				// Input (Data,Var,Abs,No Wrap,Linear,Preferred State,Null State)

// 	/* FACE BUTTONS */
// 	0x65, 0x00,				// Unit (None)
// 	0x05, 0x09,				// Usage Page (Button)
// 	0x19, 0x01,				// Usage Minimum (1)
// 	0x29, 0x0E,				// Usage Maximum (14)
// 	0x15, 0x00,				// Logical Minimum (0)
// 	0x25, 0x01,				// Logical Maximum (1)
// 	0x75, 0x01,				// Report Size (1)
// 	0x95, 0x0E,				// ReportCount (14)
// 	0x81, 0x02,				// Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	
// 	/* COUNTER */
// 	0x06, 0x00, 0xFF,		// Usage Page (Vendor Defined 0xFF00)
// 	0x09, 0x20,				// Usage (0x20) (counter)
// 	0x75, 0x06,				// Report Size (6)
// 	0x95, 0x01,				// Report Count (1)
// 	0x15, 0x00,				// Logical Minimum (0)
// 	0x25, 0x7F,				// Logical Maximum (127)
// 	0x81, 0x02,				// Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

// 	/* SHOULDERPADS TRIGGER */

// };


// const uint8_t hid_report_desc[] = {
// 	0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
// 	0x09, 0x04,        // Usage (Joystick)
// 	0xA1, 0x01,        // Collection (Physical)
// 	0xA1, 0x02,        //   Collection (Application)
// 	0x85, 0x01,        //     Report ID (1)
// 	0x75, 0x08,        //     Report Size (8)
// 	0x95, 0x01,        //     Report Count (1)
// 	0x15, 0x00,        //     Logical Minimum (0)
// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
// 	0x81, 0x03,        //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
// 					   //     NOTE: reserved byte

// 	0x75, 0x01,        //     Report Size (1)
// 	0x95, 0x13,        //     Report Count (19)
// 	0x15, 0x00,        //     Logical Minimum (0)
// 	0x25, 0x01,        //     Logical Maximum (1)
// 	0x35, 0x00,        //     Physical Minimum (0)
// 	0x45, 0x01,        //     Physical Maximum (1)
// 	0x05, 0x09,        //     Usage Page (Button)
// 	0x19, 0x01,        //     Usage Minimum (0x01)
// 	0x29, 0x13,        //     Usage Maximum (0x13)
// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

// 	0x75, 0x01,        //     Report Size (1)
// 	0x95, 0x0D,        //     Report Count (13)
// 	0x06, 0x00, 0xFF,  //     Usage Page (Vendor Defined 0xFF00)
// 	0x81, 0x03,        //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
// 					   //     NOTE: 32 bit integer, where 0:18 are buttons and 19:31 are reserved

// 	0x15, 0x00,        //     Logical Minimum (0)
// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
// 	0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
// 	0x09, 0x01,        //     Usage (Pointer)
// 	0xA1, 0x00,        //     Collection (Undefined)
// 	0x75, 0x08,        //       Report Size (8)
// 	0x95, 0x04,        //       Report Count (4)
// 	0x35, 0x00,        //       Physical Minimum (0)
// 	0x46, 0xFF, 0x00,  //       Physical Maximum (255)
// 	0x09, 0x30,        //       Usage (X)
// 	0x09, 0x31,        //       Usage (Y)
// 	0x09, 0x32,        //       Usage (Z)
// 	0x09, 0x35,        //       Usage (Rz)
// 	0x81, 0x02,        //       Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
// 					   //       NOTE: four joysticks
// 	0xC0,              //       End Collection
// 	0xC0,			   //	   End Collection


// 	// 0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
// 	// 0x75, 0x08,        //     Report Size (8)
// 	// 0x95, 0x27,        //     Report Count (39)
// 	// 0x09, 0x01,        //     Usage (Pointer)
// 	// 0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
// 	// 0x75, 0x08,        //     Report Size (8)
// 	// 0x95, 0x30,        //     Report Count (48)
// 	// 0x09, 0x01,        //     Usage (Pointer)
// 	// 0x91, 0x02,        //     Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
// 	// 0x75, 0x08,        //     Report Size (8)
// 	// 0x95, 0x30,        //     Report Count (48)
// 	// 0x09, 0x01,        //     Usage (Pointer)
// 	// 0xB1, 0x02,        //     Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
// 	// 0xC0,              //   End Collection
// };



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
