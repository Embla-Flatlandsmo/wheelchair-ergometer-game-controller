/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <soc.h>
#include <assert.h>

#include <zephyr/settings/settings.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <zephyr/bluetooth/services/bas.h>
#include <bluetooth/services/hids.h>
#include <zephyr/bluetooth/services/dis.h>
#include <dk_buttons_and_leds.h>

#define DEVICE_NAME     CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define BASE_USB_HID_SPEC_VERSION   0x0101

/* Number of pixels by which the cursor is moved when a button is pushed. */
#define MOVEMENT_SPEED              5
/* Number of input reports in this application. */
#define INPUT_REPORT_COUNT          1
/* Length of Game Pad Input Report containing movement data. */
#define INPUT_REP_MOVEMENT_NUM_BYTES      4
/* Index of Game pad Input Report containing movement data. */
#define INPUT_REP_MOVEMENT_INDEX    0
/* Id of reference to Game pad Input Report containing movement data. */
#define INPUT_REP_REF_MOVEMENT_ID   1

/* HIDs queue size. */
#define HIDS_QUEUE_SIZE 10

/* Key used to move cursor left */
#define KEY_LEFT_MASK   DK_BTN1_MSK
/* Key used to move cursor up */
#define KEY_UP_MASK     DK_BTN2_MSK
/* Key used to move cursor right */
#define KEY_RIGHT_MASK  DK_BTN3_MSK
/* Key used to move cursor down */
#define KEY_DOWN_MASK   DK_BTN4_MSK

/* Key used to accept or reject passkey value */
#define KEY_PAIRING_ACCEPT DK_BTN1_MSK
#define KEY_PAIRING_REJECT DK_BTN2_MSK

/* HIDS instance. */
BT_HIDS_DEF(hids_obj,
	    INPUT_REP_MOVEMENT_NUM_BYTES);

static struct k_work hids_work;

struct joystick_pos {
	int16_t x_val;
	int16_t y_val;
	// int16_t right_x_val;
	// int16_t right_y_val;
};

/* Mouse movement queue. */
K_MSGQ_DEFINE(hids_queue,
	      sizeof(struct joystick_pos),
	      HIDS_QUEUE_SIZE,
	      4);

#if CONFIG_BT_DIRECTED_ADVERTISING
/* Bonded address queue. */
K_MSGQ_DEFINE(bonds_queue,
	      sizeof(bt_addr_le_t),
	      CONFIG_BT_MAX_PAIRED,
	      4);
#endif

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE,
		      (CONFIG_BT_DEVICE_APPEARANCE >> 0) & 0xff,
		      (CONFIG_BT_DEVICE_APPEARANCE >> 8) & 0xff),
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL),
					  BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static struct conn_mode {
	struct bt_conn *conn;
	bool in_boot_mode;
} conn_mode[CONFIG_BT_HIDS_MAX_CLIENT_COUNT];

static struct k_work adv_work;

static struct k_work pairing_work;
struct pairing_data_mitm {
	struct bt_conn *conn;
	unsigned int passkey;
};

K_MSGQ_DEFINE(mitm_queue,
	      sizeof(struct pairing_data_mitm),
	      CONFIG_BT_HIDS_MAX_CLIENT_COUNT,
	      4);

#if CONFIG_BT_DIRECTED_ADVERTISING
static void bond_find(const struct bt_bond_info *info, void *user_data)
{
	int err;

	/* Filter already connected peers. */
	for (size_t i = 0; i < CONFIG_BT_HIDS_MAX_CLIENT_COUNT; i++) {
		if (conn_mode[i].conn) {
			const bt_addr_le_t *dst =
				bt_conn_get_dst(conn_mode[i].conn);

			if (!bt_addr_le_cmp(&info->addr, dst)) {
				return;
			}
		}
	}

	err = k_msgq_put(&bonds_queue, (void *) &info->addr, K_NO_WAIT);
	if (err) {
		printk("No space in the queue for the bond.\n");
	}
}
#endif

static void advertising_continue(void)
{
	struct bt_le_adv_param adv_param;

#if CONFIG_BT_DIRECTED_ADVERTISING
	bt_addr_le_t addr;

	if (!k_msgq_get(&bonds_queue, &addr, K_NO_WAIT)) {
		char addr_buf[BT_ADDR_LE_STR_LEN];

		adv_param = *BT_LE_ADV_CONN_DIR(&addr);
		adv_param.options |= BT_LE_ADV_OPT_DIR_ADDR_RPA;

		int err = bt_le_adv_start(&adv_param, NULL, 0, NULL, 0);

		if (err) {
			printk("Directed advertising failed to start\n");
			return;
		}

		bt_addr_le_to_str(&addr, addr_buf, BT_ADDR_LE_STR_LEN);
		printk("Direct advertising to %s started\n", addr_buf);
	} else
#endif
	{
		int err;

		adv_param = *BT_LE_ADV_CONN;
		adv_param.options |= BT_LE_ADV_OPT_ONE_TIME;
		err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad),
				  sd, ARRAY_SIZE(sd));
		if (err) {
			printk("Advertising failed to start (err %d)\n", err);
			return;
		}

		printk("Regular advertising started\n");
	}
}

static void advertising_start(void)
{
#if CONFIG_BT_DIRECTED_ADVERTISING
	k_msgq_purge(&bonds_queue);
	bt_foreach_bond(BT_ID_DEFAULT, bond_find, NULL);
#endif

	k_work_submit(&adv_work);
}

static void advertising_process(struct k_work *work)
{
	advertising_continue();
}

static void pairing_process(struct k_work *work)
{
	int err;
	struct pairing_data_mitm pairing_data;

	char addr[BT_ADDR_LE_STR_LEN];

	err = k_msgq_peek(&mitm_queue, &pairing_data);
	if (err) {
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(pairing_data.conn),
			  addr, sizeof(addr));

	printk("Passkey for %s: %06u\n", addr, pairing_data.passkey);
	printk("Press Button 1 to confirm, Button 2 to reject.\n");
}


static void insert_conn_object(struct bt_conn *conn)
{
	for (size_t i = 0; i < CONFIG_BT_HIDS_MAX_CLIENT_COUNT; i++) {
		if (!conn_mode[i].conn) {
			conn_mode[i].conn = conn;
			conn_mode[i].in_boot_mode = false;

			return;
		}
	}

	printk("Connection object could not be inserted %p\n", conn);
}


static bool is_conn_slot_free(void)
{
	for (size_t i = 0; i < CONFIG_BT_HIDS_MAX_CLIENT_COUNT; i++) {
		if (!conn_mode[i].conn) {
			return true;
		}
	}

	return false;
}


static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		if (err == BT_HCI_ERR_ADV_TIMEOUT) {
			printk("Direct advertising to %s timed out\n", addr);
			k_work_submit(&adv_work);
		} else {
			printk("Failed to connect to %s (%u)\n", addr, err);
		}
		return;
	}

	printk("Connected %s\n", addr);

	err = bt_hids_connected(&hids_obj, conn);

	if (err) {
		printk("Failed to notify HID service about connection\n");
		return;
	}

	insert_conn_object(conn);

	if (is_conn_slot_free()) {
		advertising_start();
	}
}


static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	int err;
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected from %s (reason %u)\n", addr, reason);

	err = bt_hids_disconnected(&hids_obj, conn);

	if (err) {
		printk("Failed to notify HID service about disconnection\n");
	}

	for (size_t i = 0; i < CONFIG_BT_HIDS_MAX_CLIENT_COUNT; i++) {
		if (conn_mode[i].conn == conn) {
			conn_mode[i].conn = NULL;
			break;
		}
	}

	advertising_start();
}


#ifdef CONFIG_BT_HIDS_SECURITY_ENABLED
static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		printk("Security changed: %s level %u\n", addr, level);
	} else {
		printk("Security failed: %s level %u err %d\n", addr, level,
			err);
	}
}
#endif


BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
#ifdef CONFIG_BT_HIDS_SECURITY_ENABLED
	.security_changed = security_changed,
#endif
};


static void hids_pm_evt_handler(enum bt_hids_pm_evt evt,
				struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];
	size_t i;

	for (i = 0; i < CONFIG_BT_HIDS_MAX_CLIENT_COUNT; i++) {
		if (conn_mode[i].conn == conn) {
			break;
		}
	}

	if (i >= CONFIG_BT_HIDS_MAX_CLIENT_COUNT) {
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	switch (evt) {
	case BT_HIDS_PM_EVT_BOOT_MODE_ENTERED:
		printk("Boot mode entered %s\n", addr);
		conn_mode[i].in_boot_mode = true;
		break;

	case BT_HIDS_PM_EVT_REPORT_MODE_ENTERED:
		printk("Report mode entered %s\n", addr);
		conn_mode[i].in_boot_mode = false;
		break;

	default:
		break;
	}
}

static void hid_init(void)
{
	int err;
	struct bt_hids_init_param hids_init_param = { 0 };
	struct bt_hids_inp_rep *hids_inp_rep;
	// static const uint8_t mouse_movement_mask[ceiling_fraction(INPUT_REP_MOVEMENT_NUM_BYTES, 8)] = {0};

	/* Single joystick */
	// static const uint8_t report_map[] = {
	// 	0x05, 0x01,     /* Usage Page (Generic Desktop) */
	// 	0x09, 0x05,     /* Usage (Game Pad) */

	// 	0xA1, 0x01,     /* Collection (Application) */
	// 	0xa1, 0x00,       /* Collection (Physical) */
	// 	// 0x85, 0x01,       /* Report Id 1 */
	// 	0x05, 0x01,       /* Usage Page (Generic Desktop) */
	// 	0x15, 0x81,  	  /* Logical Minimum (-127) */
	// 	0x25, 0x7F,  	  /* Logical Maximum (127) */
	// 	0x09, 0x30,  	  /* Usage (X) */
	// 	0x09, 0x31,  	  /* Usage (Y) */
	// 	0x75, 0x08,      /* Report Size (8) */
	// 	0x95, 0x02,      /* Report Count (2) */
	// 	0x81, 0x02,      /* Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position) */
	// 	0xC0,            /* End Collection (Physical) */
	// 	0xC0,        /* End Collection (Application) */
	// };

	/* Double joystick */
	static const uint8_t report_map[] = {
	0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
	0x09, 0x05,        // Usage (Game Pad)
	0xA1, 0x01,        // Collection (Application)
	0x85, 0x01,        //   Report ID (1)
	0x09, 0x01,        //   Usage (Pointer)
	0xA1, 0x00,        //   Collection (Physical)
	0x09, 0x30,        //     Usage (X)
	0x09, 0x31,        //     Usage (Y)
	0x15, 0x00,        //     Logical Minimum (0)
	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	0x95, 0x02,        //     Report Count (2)
	0x75, 0x08,        //     Report Size (8)
	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	0xC0,              //   End Collection
	0x09, 0x01,        //   Usage (Pointer)
	0xA1, 0x00,        //   Collection (Physical)
	0x09, 0x32,        //     Usage (Z)
	0x09, 0x35,        //     Usage (Rz)
	0x15, 0x00,        //     Logical Minimum (0)
	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	0x95, 0x02,        //     Report Count (2)
	0x75, 0x08,        //     Report Size (8)
	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	0xC0,              //   End Collection (Physical)
	0xC0,			   // End Collection (Application)
	};

	/* Full game controller */
	// static const uint8_t report_map[] = {
	// 	0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
	// 	0x09, 0x05,        // Usage (Game Pad)
	// 	0xA1, 0x01,        // Collection (Application)
	// 	0x85, 0x01,        //   Report ID (1)
	// 	0x09, 0x01,        //   Usage (Pointer)
	// 	0xA1, 0x00,        //   Collection (Physical)
	// 	0x09, 0x30,        //     Usage (X)
	// 	0x09, 0x31,        //     Usage (Y)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x27, 0xFF, 0xFF, 0x00, 0x00,  //     Logical Maximum (65534)
	// 	0x95, 0x02,        //     Report Count (2)
	// 	0x75, 0x10,        //     Report Size (16)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0xC0,              //   End Collection
	// 	0x09, 0x01,        //   Usage (Pointer)
	// 	0xA1, 0x00,        //   Collection (Physical)
	// 	0x09, 0x32,        //     Usage (Z)
	// 	0x09, 0x35,        //     Usage (Rz)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x27, 0xFF, 0xFF, 0x00, 0x00,  //     Logical Maximum (65534)
	// 	0x95, 0x02,        //     Report Count (2)
	// 	0x75, 0x10,        //     Report Size (16)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0xC0,              //   End Collection
	// 	0x05, 0x02,        //   Usage Page (Sim Ctrls)
	// 	0x09, 0xC5,        //   Usage (Brake)
	// 	0x15, 0x00,        //   Logical Minimum (0)
	// 	0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
	// 	0x95, 0x01,        //   Report Count (1)
	// 	0x75, 0x0A,        //   Report Size (10)
	// 	0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x15, 0x00,        //   Logical Minimum (0)
	// 	0x25, 0x00,        //   Logical Maximum (0)
	// 	0x75, 0x06,        //   Report Size (6)
	// 	0x95, 0x01,        //   Report Count (1)
	// 	0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x05, 0x02,        //   Usage Page (Sim Ctrls)
	// 	0x09, 0xC4,        //   Usage (Accelerator)
	// 	0x15, 0x00,        //   Logical Minimum (0)
	// 	0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
	// 	0x95, 0x01,        //   Report Count (1)
	// 	0x75, 0x0A,        //   Report Size (10)
	// 	0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x15, 0x00,        //   Logical Minimum (0)
	// 	0x25, 0x00,        //   Logical Maximum (0)
	// 	0x75, 0x06,        //   Report Size (6)
	// 	0x95, 0x01,        //   Report Count (1)
	// 	0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
	// 	0x09, 0x39,        //   Usage (Hat switch)
	// 	0x15, 0x01,        //   Logical Minimum (1)
	// 	0x25, 0x08,        //   Logical Maximum (8)
	// 	0x35, 0x00,        //   Physical Minimum (0)
	// 	0x46, 0x3B, 0x01,  //   Physical Maximum (315)
	// 	0x66, 0x14, 0x00,  //   Unit (System: English Rotation, Length: Centimeter)
	// 	0x75, 0x04,        //   Report Size (4)
	// 	0x95, 0x01,        //   Report Count (1)
	// 	0x81, 0x42,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,Null State)
	// 	0x75, 0x04,        //   Report Size (4)
	// 	0x95, 0x01,        //   Report Count (1)
	// 	0x15, 0x00,        //   Logical Minimum (0)
	// 	0x25, 0x00,        //   Logical Maximum (0)
	// 	0x35, 0x00,        //   Physical Minimum (0)
	// 	0x45, 0x00,        //   Physical Maximum (0)
	// 	0x65, 0x00,        //   Unit (None)
	// 	0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x05, 0x09,        //   Usage Page (Button)
	// 	0x19, 0x01,        //   Usage Minimum (0x01)
	// 	0x29, 0x0F,        //   Usage Maximum (0x0F)
	// 	0x15, 0x00,        //   Logical Minimum (0)
	// 	0x25, 0x01,        //   Logical Maximum (1)
	// 	0x75, 0x01,        //   Report Size (1)
	// 	0x95, 0x0F,        //   Report Count (15)
	// 	0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x15, 0x00,        //   Logical Minimum (0)
	// 	0x25, 0x00,        //   Logical Maximum (0)
	// 	0x75, 0x01,        //   Report Size (1)
	// 	0x95, 0x01,        //   Report Count (1)
	// 	0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x05, 0x0C,        //   Usage Page (Consumer)
	// 	0x0A, 0x24, 0x02,  //   Usage (AC Back)
	// 	0x15, 0x00,        //   Logical Minimum (0)
	// 	0x25, 0x01,        //   Logical Maximum (1)
	// 	0x95, 0x01,        //   Report Count (1)
	// 	0x75, 0x01,        //   Report Size (1)
	// 	0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x15, 0x00,        //   Logical Minimum (0)
	// 	0x25, 0x00,        //   Logical Maximum (0)
	// 	0x75, 0x07,        //   Report Size (7)
	// 	0x95, 0x01,        //   Report Count (1)
	// 	0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
	// 	0x09, 0x01,        //   Usage (Pointer)
	// 	0xA1, 0x00,        //   Collection (Physical)
	// 	0x09, 0x40,        //     Usage (Vx)
	// 	0x09, 0x41,        //     Usage (Vy)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x27, 0xFF, 0xFF, 0x00, 0x00,  //     Logical Maximum (65534)
	// 	0x95, 0x02,        //     Report Count (2)
	// 	0x75, 0x10,        //     Report Size (16)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0xC0,              //   End Collection
	// 	0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
	// 	0x09, 0x01,        //   Usage (Pointer)
	// 	0xA1, 0x00,        //   Collection (Physical)
	// 	0x09, 0x43,        //     Usage (Vbrx)
	// 	0x09, 0x44,        //     Usage (Vbry)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x27, 0xFF, 0xFF, 0x00, 0x00,  //     Logical Maximum (65534)
	// 	0x95, 0x02,        //     Report Count (2)
	// 	0x75, 0x10,        //     Report Size (16)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0xC0,              //   End Collection
	// 	0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
	// 	0x09, 0x42,        //   Usage (Vz)
	// 	0x15, 0x00,        //   Logical Minimum (0)
	// 	0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
	// 	0x95, 0x01,        //   Report Count (1)
	// 	0x75, 0x0A,        //   Report Size (10)
	// 	0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x15, 0x00,        //   Logical Minimum (0)
	// 	0x25, 0x00,        //   Logical Maximum (0)
	// 	0x75, 0x06,        //   Report Size (6)
	// 	0x95, 0x01,        //   Report Count (1)
	// 	0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
	// 	0x09, 0x45,        //   Usage (Vbrz)
	// 	0x15, 0x00,        //   Logical Minimum (0)
	// 	0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
	// 	0x95, 0x01,        //   Report Count (1)
	// 	0x75, 0x0A,        //   Report Size (10)
	// 	0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x15, 0x00,        //   Logical Minimum (0)
	// 	0x25, 0x00,        //   Logical Maximum (0)
	// 	0x75, 0x06,        //   Report Size (6)
	// 	0x95, 0x01,        //   Report Count (1)
	// 	0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
	// 	0x09, 0x37,        //   Usage (Dial)
	// 	0x15, 0x01,        //   Logical Minimum (1)
	// 	0x25, 0x08,        //   Logical Maximum (8)
	// 	0x35, 0x00,        //   Physical Minimum (0)
	// 	0x46, 0x3B, 0x01,  //   Physical Maximum (315)
	// 	0x66, 0x14, 0x00,  //   Unit (System: English Rotation, Length: Centimeter)
	// 	0x75, 0x04,        //   Report Size (4)
	// 	0x95, 0x01,        //   Report Count (1)
	// 	0x81, 0x42,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,Null State)
	// 	0x75, 0x04,        //   Report Size (4)
	// 	0x95, 0x01,        //   Report Count (1)
	// 	0x15, 0x00,        //   Logical Minimum (0)
	// 	0x25, 0x00,        //   Logical Maximum (0)
	// 	0x35, 0x00,        //   Physical Minimum (0)
	// 	0x45, 0x00,        //   Physical Maximum (0)
	// 	0x65, 0x00,        //   Unit (None)
	// 	0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x05, 0x09,        //   Usage Page (Button)
	// 	0x19, 0x10,        //   Usage Minimum (0x10)
	// 	0x29, 0x1E,        //   Usage Maximum (0x1E)
	// 	0x15, 0x00,        //   Logical Minimum (0)
	// 	0x25, 0x01,        //   Logical Maximum (1)
	// 	0x75, 0x01,        //   Report Size (1)
	// 	0x95, 0x0F,        //   Report Count (15)
	// 	0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x15, 0x00,        //   Logical Minimum (0)
	// 	0x25, 0x00,        //   Logical Maximum (0)
	// 	0x75, 0x01,        //   Report Size (1)
	// 	0x95, 0x01,        //   Report Count (1)
	// 	0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x05, 0x0C,        //   Usage Page (Consumer)
	// 	0x0A, 0x82, 0x00,  //   Usage (Mode Step)
	// 	0x15, 0x00,        //   Logical Minimum (0)
	// 	0x25, 0x01,        //   Logical Maximum (1)
	// 	0x95, 0x01,        //   Report Count (1)
	// 	0x75, 0x01,        //   Report Size (1)
	// 	0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x15, 0x00,        //   Logical Minimum (0)
	// 	0x25, 0x00,        //   Logical Maximum (0)
	// 	0x75, 0x07,        //   Report Size (7)
	// 	0x95, 0x01,        //   Report Count (1)
	// 	0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x05, 0x0C,        //   Usage Page (Consumer)
	// 	0x09, 0x01,        //   Usage (Consumer Control)
	// 	0xA1, 0x01,        //   Collection (Application)
	// 	0x0A, 0x81, 0x00,  //     Usage (Assign Selection)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x04,        //     Report Size (4)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x25, 0x00,        //     Logical Maximum (0)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x04,        //     Report Size (4)
	// 	0x81, 0x03,        //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x0A, 0x84, 0x00,  //     Usage (Enter Channel)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x04,        //     Report Size (4)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x25, 0x00,        //     Logical Maximum (0)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x04,        //     Report Size (4)
	// 	0x81, 0x03,        //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x0A, 0x85, 0x00,  //     Usage (Order Movie)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x0A, 0x99, 0x00,  //     Usage (Media Select Security)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x04,        //     Report Size (4)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x25, 0x00,        //     Logical Maximum (0)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x04,        //     Report Size (4)
	// 	0x81, 0x03,        //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x0A, 0x9E, 0x00,  //     Usage (Media Select SAP)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x0A, 0xA1, 0x00,  //     Usage (Once)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x0A, 0xA2, 0x00,  //     Usage (Daily)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x0A, 0xA3, 0x00,  //     Usage (Weekly)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x0A, 0xA4, 0x00,  //     Usage (Monthly)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x0A, 0xB9, 0x00,  //     Usage (Random Play)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x0A, 0xBA, 0x00,  //     Usage (Select Disc)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x0A, 0xBB, 0x00,  //     Usage (Enter Disc)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x0A, 0xBE, 0x00,  //     Usage (Track Normal)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x0A, 0xC0, 0x00,  //     Usage (Frame Forward)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x0A, 0xC1, 0x00,  //     Usage (Frame Back)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x0A, 0xC2, 0x00,  //     Usage (Mark)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x0A, 0xC3, 0x00,  //     Usage (Clear Mark)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x0A, 0xC4, 0x00,  //     Usage (Repeat From Mark)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x0A, 0xC5, 0x00,  //     Usage (Return To Mark)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x0A, 0xC6, 0x00,  //     Usage (Search Mark Forward)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x0A, 0xC7, 0x00,  //     Usage (Search Mark Backwards)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x0A, 0xC8, 0x00,  //     Usage (Counter Reset)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0xC0,              //   End Collection
	// 	0x05, 0x0C,        //   Usage Page (Consumer)
	// 	0x09, 0x01,        //   Usage (Consumer Control)
	// 	0x85, 0x02,        //   Report ID (2)
	// 	0xA1, 0x01,        //   Collection (Application)
	// 	0x05, 0x0C,        //     Usage Page (Consumer)
	// 	0x0A, 0x23, 0x02,  //     Usage (AC Home)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x25, 0x01,        //     Logical Maximum (1)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x75, 0x01,        //     Report Size (1)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x25, 0x00,        //     Logical Maximum (0)
	// 	0x75, 0x07,        //     Report Size (7)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x81, 0x03,        //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0xC0,              //   End Collection
	// 	0x05, 0x0F,        //   Usage Page (PID Page)
	// 	0x09, 0x21,        //   Usage (0x21)
	// 	0x85, 0x03,        //   Report ID (3)
	// 	0xA1, 0x02,        //   Collection (Logical)
	// 	0x09, 0x97,        //     Usage (0x97)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x25, 0x01,        //     Logical Maximum (1)
	// 	0x75, 0x04,        //     Report Size (4)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x91, 0x02,        //     Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x25, 0x00,        //     Logical Maximum (0)
	// 	0x75, 0x04,        //     Report Size (4)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x91, 0x03,        //     Output (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	// 	0x09, 0x70,        //     Usage (0x70)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x25, 0x64,        //     Logical Maximum (100)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x95, 0x04,        //     Report Count (4)
	// 	0x91, 0x02,        //     Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	// 	0x09, 0x50,        //     Usage (0x50)
	// 	0x66, 0x01, 0x10,  //     Unit (System: SI Linear, Time: Seconds)
	// 	0x55, 0x0E,        //     Unit Exponent (-2)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x91, 0x02,        //     Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	// 	0x09, 0xA7,        //     Usage (0xA7)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x91, 0x02,        //     Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	// 	0x65, 0x00,        //     Unit (None)
	// 	0x55, 0x00,        //     Unit Exponent (0)
	// 	0x09, 0x7C,        //     Usage (0x7C)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x91, 0x02,        //     Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	// 	0xC0,              //   End Collection
	// 	0x05, 0x06,        //   Usage Page (Generic Dev Ctrls)
	// 	0x09, 0x20,        //   Usage (Battery Strength)
	// 	0x85, 0x04,        //   Report ID (4)
	// 	0x15, 0x00,        //   Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //   Logical Maximum (255)
	// 	0x75, 0x08,        //   Report Size (8)
	// 	0x95, 0x01,        //   Report Count (1)
	// 	0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
	// 	0x09, 0x01,        //   Usage (0x01)
	// 	0xA1, 0x02,        //   Collection (Logical)
	// 	0x85, 0x06,        //     Report ID (6)
	// 	0x09, 0x01,        //     Usage (0x01)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x25, 0x64,        //     Logical Maximum (100)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0xB1, 0x02,        //     Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	// 	0x09, 0x02,        //     Usage (0x02)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x25, 0x64,        //     Logical Maximum (100)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0xB1, 0x02,        //     Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	// 	0x09, 0x03,        //     Usage (0x03)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0xB1, 0x02,        //     Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	// 	0x09, 0x04,        //     Usage (0x04)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x95, 0x3C,        //     Report Count (60)
	// 	0xB2, 0x02, 0x01,  //     Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile,Buffered Bytes)
	// 	0xC0,              //   End Collection
	// 	0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
	// 	0x09, 0x02,        //   Usage (0x02)
	// 	0xA1, 0x02,        //   Collection (Logical)
	// 	0x85, 0x07,        //     Report ID (7)
	// 	0x09, 0x05,        //     Usage (0x05)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x25, 0x64,        //     Logical Maximum (100)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0xB1, 0x02,        //     Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	// 	0x09, 0x06,        //     Usage (0x06)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x25, 0x64,        //     Logical Maximum (100)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0xB1, 0x02,        //     Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	// 	0x09, 0x07,        //     Usage (0x07)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x25, 0x64,        //     Logical Maximum (100)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0xB1, 0x02,        //     Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	// 	0xC0,              //   End Collection
	// 	0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
	// 	0x09, 0x03,        //   Usage (0x03)
	// 	0xA1, 0x02,        //   Collection (Logical)
	// 	0x85, 0x08,        //     Report ID (8)
	// 	0x09, 0x08,        //     Usage (0x08)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x25, 0x64,        //     Logical Maximum (100)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0xB1, 0x02,        //     Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	// 	0x09, 0x09,        //     Usage (0x09)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x25, 0x64,        //     Logical Maximum (100)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0xB1, 0x02,        //     Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	// 	0x09, 0x0A,        //     Usage (0x0A)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0xB1, 0x02,        //     Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	// 	0xC0,              //   End Collection
	// 	0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
	// 	0x09, 0x04,        //   Usage (0x04)
	// 	0xA1, 0x01,        //   Collection (Application)
	// 	0x85, 0x09,        //     Report ID (9)
	// 	0x09, 0x0B,        //     Usage (0x0B)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x25, 0x64,        //     Logical Maximum (100)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0xB1, 0x02,        //     Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	// 	0x09, 0x0C,        //     Usage (0x0C)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x25, 0x64,        //     Logical Maximum (100)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0xB1, 0x02,        //     Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	// 	0x09, 0x0D,        //     Usage (0x0D)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x25, 0x64,        //     Logical Maximum (100)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0xB1, 0x02,        //     Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	// 	0x09, 0x0E,        //     Usage (0x0E)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0xB1, 0x02,        //     Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	// 	0x09, 0x0F,        //     Usage (0x0F)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x95, 0x3C,        //     Report Count (60)
	// 	0xB2, 0x02, 0x01,  //     Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile,Buffered Bytes)
	// 	0xC0,              //   End Collection
	// 	0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
	// 	0x09, 0x05,        //   Usage (0x05)
	// 	0xA1, 0x01,        //   Collection (Application)
	// 	0x85, 0x0A,        //     Report ID (10)
	// 	0x09, 0x10,        //     Usage (0x10)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x27, 0xFF, 0xFF, 0xFF, 0x7F,  //     Logical Maximum (2147483646)
	// 	0x75, 0x20,        //     Report Size (32)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x09, 0x11,        //     Usage (0x11)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x27, 0xFF, 0xFF, 0xFF, 0x7F,  //     Logical Maximum (2147483646)
	// 	0x75, 0x20,        //     Report Size (32)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x09, 0x12,        //     Usage (0x12)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x95, 0x02,        //     Report Count (2)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x09, 0x13,        //     Usage (0x13)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0xC0,              //   End Collection
	// 	0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
	// 	0x09, 0x06,        //   Usage (0x06)
	// 	0xA1, 0x02,        //   Collection (Logical)
	// 	0x85, 0x0B,        //     Report ID (11)
	// 	0x09, 0x14,        //     Usage (0x14)
	// 	0x15, 0x00,        //     Logical Minimum (0)
	// 	0x25, 0x64,        //     Logical Maximum (100)
	// 	0x75, 0x08,        //     Report Size (8)
	// 	0x95, 0x01,        //     Report Count (1)
	// 	0xB1, 0x02,        //     Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	// 	0xC0,              //   End Collection
	// 	0xC0,              // End Collection
	// 	0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
	// 	0x09, 0x06,        // Usage (Keyboard)
	// 	0xA1, 0x01,        // Collection (Application)
	// 	0x85, 0x05,        //   Report ID (5)
	// 	0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
	// 	0x19, 0xE0,        //   Usage Minimum (0xE0)
	// 	0x29, 0xE7,        //   Usage Maximum (0xE7)
	// 	0x15, 0x00,        //   Logical Minimum (0)
	// 	0x25, 0x01,        //   Logical Maximum (1)
	// 	0x75, 0x01,        //   Report Size (1)
	// 	0x95, 0x08,        //   Report Count (8)
	// 	0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x95, 0x01,        //   Report Count (1)
	// 	0x75, 0x08,        //   Report Size (8)
	// 	0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0x95, 0x06,        //   Report Count (6)
	// 	0x75, 0x08,        //   Report Size (8)
	// 	0x15, 0x00,        //   Logical Minimum (0)
	// 	0x25, 0x65,        //   Logical Maximum (101)
	// 	0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
	// 	0x19, 0x00,        //   Usage Minimum (0x00)
	// 	0x29, 0x65,        //   Usage Maximum (0x65)
	// 	0x81, 0x00,        //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
	// 	0xC0,              // End Collection

	// };
	hids_init_param.rep_map.data = report_map;
	hids_init_param.rep_map.size = sizeof(report_map);

	hids_init_param.info.bcd_hid = BASE_USB_HID_SPEC_VERSION;
	hids_init_param.info.b_country_code = 0x00;
	hids_init_param.info.flags = (BT_HIDS_REMOTE_WAKE |
				      BT_HIDS_NORMALLY_CONNECTABLE);

	hids_inp_rep = &hids_init_param.inp_rep_group_init.reports[0];
	hids_inp_rep->size = INPUT_REP_MOVEMENT_NUM_BYTES;
	hids_inp_rep->id = INPUT_REP_REF_MOVEMENT_ID;
	// hids_inp_rep->rep_mask = mouse_movement_mask;
	hids_inp_rep->rep_mask = NULL;
	hids_init_param.inp_rep_group_init.cnt++;
	hids_init_param.pm_evt_handler = hids_pm_evt_handler;

	err = bt_hids_init(&hids_obj, &hids_init_param);
	__ASSERT(err == 0, "HIDS initialization failed\n");
}

static void gamepad_movement_send(uint8_t val)
{
	for (size_t i = 0; i < CONFIG_BT_HIDS_MAX_CLIENT_COUNT; i++) {

		if (!conn_mode[i].conn) {
			continue;
		}
		uint8_t buffer[4];

		// uint8_t x_buff[2];
		// uint8_t y_buff[2];

		// int16_t x = MAX(MIN(x_delta, 0x07ff), -0x07ff);
		// int16_t y = MAX(MIN(y_delta, 0x07ff), -0x07ff);

		// /* Convert to little-endian. */
		// sys_put_le16(x, x_buff);
		// sys_put_le16(y, y_buff);

		// /* Encode report. */
		// // BUILD_ASSERT(sizeof(buffer) == 3,
		// // 			"Only 2 axis, 12-bit each, are supported");
		// BUILD_ASSERT(sizeof(buffer) == 2,
		// 			"Only 1 joystick, 2 axis with 8bit each is supported");
		
		// buffer[0] = x_buff[0];
		// buffer[1] = (y_buff[0] << 4) | (x_buff[1] & 0x0f);
		// buffer[2] = (y_buff[1] << 4) | (y_buff[0] >> 4);
		// buffer[0] = val;
		buffer[0] = val;
		buffer[1] = val;
		buffer[2] = val;
		buffer[3] = val;
		printk("Joystick 1: %d, %d", 0, val);
		printk("Joystick 2: %d, %d", val, 0);
		// printk("Sending movement: x=%d, y=%d\n", val, val);

		bt_hids_inp_rep_send(&hids_obj, conn_mode[i].conn,
						INPUT_REP_MOVEMENT_INDEX,
						buffer, sizeof(buffer), NULL);
	}
}

static void mouse_handler(struct k_work *work)
{
	struct joystick_pos pos;
	static uint8_t val = 0x00;
	while (!k_msgq_get(&hids_queue, &pos, K_NO_WAIT)) {
		if (val)
		{
			val = 0;
		} else {
			val = UINT8_MAX;
		}
		gamepad_movement_send(val);
	}
}

#if defined(CONFIG_BT_HIDS_SECURITY_ENABLED)
static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Passkey for %s: %06u\n", addr, passkey);
}


static void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
	int err;

	struct pairing_data_mitm pairing_data;

	pairing_data.conn    = bt_conn_ref(conn);
	pairing_data.passkey = passkey;

	err = k_msgq_put(&mitm_queue, &pairing_data, K_NO_WAIT);
	if (err) {
		printk("Pairing queue is full. Purge previous data.\n");
	}

	/* In the case of multiple pairing requests, trigger
	 * pairing confirmation which needed user interaction only
	 * once to avoid display information about all devices at
	 * the same time. Passkey confirmation for next devices will
	 * be proccess from queue after handling the earlier ones.
	 */
	if (k_msgq_num_used_get(&mitm_queue) == 1) {
		k_work_submit(&pairing_work);
	}
}


static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}


static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing completed: %s, bonded: %d\n", addr, bonded);
}


static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];
	struct pairing_data_mitm pairing_data;

	if (k_msgq_peek(&mitm_queue, &pairing_data) != 0) {
		return;
	}

	if (pairing_data.conn == conn) {
		bt_conn_unref(pairing_data.conn);
		k_msgq_get(&mitm_queue, &pairing_data, K_NO_WAIT);
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing failed conn: %s, reason %d\n", addr, reason);
}


static struct bt_conn_auth_cb conn_auth_callbacks = {
	.passkey_display = auth_passkey_display,
	.passkey_confirm = auth_passkey_confirm,
	.cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed
};
#else
static struct bt_conn_auth_cb conn_auth_callbacks;
#endif


static void num_comp_reply(bool accept)
{
	struct pairing_data_mitm pairing_data;
	struct bt_conn *conn;

	if (k_msgq_get(&mitm_queue, &pairing_data, K_NO_WAIT) != 0) {
		return;
	}

	conn = pairing_data.conn;

	if (accept) {
		bt_conn_auth_passkey_confirm(conn);
		printk("Numeric Match, conn %p\n", conn);
	} else {
		bt_conn_auth_cancel(conn);
		printk("Numeric Reject, conn %p\n", conn);
	}

	bt_conn_unref(pairing_data.conn);

	if (k_msgq_num_used_get(&mitm_queue)) {
		k_work_submit(&pairing_work);
	}
}


void button_changed(uint32_t button_state, uint32_t has_changed)
{
	bool data_to_send = false;
	struct joystick_pos pos;
	uint32_t buttons = button_state & has_changed;

	memset(&pos, 0, sizeof(struct joystick_pos));

	if (k_msgq_num_used_get(&mitm_queue)) {
		if (buttons & KEY_PAIRING_ACCEPT) {
			num_comp_reply(true);

			return;
		}

		if (buttons & KEY_PAIRING_REJECT) {
			num_comp_reply(false);

			return;
		}
	}

	if (buttons & KEY_LEFT_MASK) {
		pos.x_val -= MOVEMENT_SPEED;
		printk("%s(): left\n", __func__);
		data_to_send = true;
	}
	if (buttons & KEY_UP_MASK) {
		pos.y_val -= MOVEMENT_SPEED;
		printk("%s(): up\n", __func__);
		data_to_send = true;
	}
	if (buttons & KEY_RIGHT_MASK) {
		pos.x_val += MOVEMENT_SPEED;
		printk("%s(): right\n", __func__);
		data_to_send = true;
	}
	if (buttons & KEY_DOWN_MASK) {
		pos.y_val += MOVEMENT_SPEED;
		printk("%s(): down\n", __func__);
		data_to_send = true;
	}

	if (data_to_send) {
		int err;

		err = k_msgq_put(&hids_queue, &pos, K_NO_WAIT);
		if (err) {
			printk("No space in the queue for button pressed\n");
			return;
		}
		if (k_msgq_num_used_get(&hids_queue) == 1) {
			k_work_submit(&hids_work);
		}
	}
}


void configure_buttons(void)
{
	int err;

	err = dk_buttons_init(button_changed);
	if (err) {
		printk("Cannot init buttons (err: %d)\n", err);
	}
}


static void bas_notify(void)
{
	uint8_t battery_level = bt_bas_get_battery_level();

	battery_level--;

	if (!battery_level) {
		battery_level = 100U;
	}

	bt_bas_set_battery_level(battery_level);
}


void main(void)
{
	int err;

	printk("Starting Bluetooth Peripheral HIDS mouse example\n");

	if (IS_ENABLED(CONFIG_BT_HIDS_SECURITY_ENABLED)) {
		err = bt_conn_auth_cb_register(&conn_auth_callbacks);
		if (err) {
			printk("Failed to register authorization callbacks.\n");
			return;
		}
		
		err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
		if (err) {
			printk("Failed to register authorization info callbacks.\n");
			return;
		}
	}

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	/* DIS initialized at system boot with SYS_INIT macro. */
	hid_init();

	k_work_init(&hids_work, mouse_handler);
	k_work_init(&pairing_work, pairing_process);
	k_work_init(&adv_work, advertising_process);

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	advertising_start();

	configure_buttons();

	while (1) {
		k_sleep(K_SECONDS(1));
		/* Battery level simulation */
		bas_notify();
	}
}
