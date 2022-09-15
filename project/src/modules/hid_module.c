/*
 * Copyright (c) 2018 - 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <assert.h>
#include <limits.h>

#include <zephyr/kernel.h>
#include <zephyr/types.h>

#include <zephyr/sys/util.h>

#include <bluetooth/services/hids.h>

#include <caf/events/ble_common_event.h>
#include "hid_report_desc.h"

#define MODULE hid_module
#include <caf/events/module_state_event.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_HID_MODULE_LOG_LEVEL);

#define BASE_USB_HID_SPEC_VERSION 0x0101

/* Number of input reports in this application. */
#define INPUT_REPORT_COUNT 1
/* Length of Game Pad Input Report containing movement data. */
#define INPUT_REP_MOVEMENT_NUM_BYTES 4
/* Index of Game pad Input Report containing movement data. */
#define INPUT_REP_MOVEMENT_INDEX 0
/* Id of reference to Game pad Input Report containing movement data. */
#define INPUT_REP_REF_MOVEMENT_ID 1

/* HIDS instance. */
BT_HIDS_DEF(hids_obj,
            INPUT_REP_MOVEMENT_NUM_BYTES);

static int module_init(void)
{
    /* HID service configuration */
    struct bt_hids_init_param hids_init_param = {0};

    hids_init_param.info.bcd_hid = BASE_USB_HID_SPEC_VERSION;
    hids_init_param.info.b_country_code = 0x00;
    hids_init_param.info.flags = BT_HIDS_REMOTE_WAKE |
                                 BT_HIDS_NORMALLY_CONNECTABLE;

    /* Attach report map */
    hids_init_param.rep_map.data = hid_report_desc;
    hids_init_param.rep_map.size = hid_report_desc_size;

    /* Declare HID reports */
    struct bt_hids_inp_rep *input_report =
        &hids_init_param.inp_rep_group_init.reports[0];

    return bt_hids_init(&hids_obj, &hids_init_param);
}

static void send_hid_report(const struct hid_report_event *event)
{
    // static void (*const report_sent_cb[REPORT_ID_COUNT])(struct bt_conn * conn, void *user_data) = {
    //     [REPORT_ID_MOUSE] = mouse_report_sent_cb,
    //     [REPORT_ID_KEYBOARD_KEYS] = keyboard_keys_report_sent_cb,
    //     [REPORT_ID_SYSTEM_CTRL] = system_ctrl_report_sent_cb,
    //     [REPORT_ID_CONSUMER_CTRL] = consumer_ctrl_report_sent_cb,
    //     [REPORT_ID_BOOT_MOUSE] = boot_mouse_report_sent_cb,
    //     [REPORT_ID_BOOT_KEYBOARD] = boot_keyboard_report_sent_cb,
    // };

    if (!cur_conn || (cur_conn != event->subscriber))
    {
        /* It's not us */
        return;
    }

    // __ASSERT_NO_MSG(event->dyndata.size > 0);

    // uint8_t report_id = event->dyndata.data[0];

    // __ASSERT_NO_MSG(report_id < ARRAY_SIZE(report_index));
    // __ASSERT_NO_MSG(report_sent_cb[report_id]);

    // if (!subscribed[report_id])
    // {
    //     /* Notification disabled */
    //     LOG_WRN("Notification disabled");
    //     hid_report_sent(cur_conn, report_id, true);
    //     return;
    // }
    
    const uint8_t *buffer = &event->dyndata.data[sizeof(report_id)];
    size_t size = event->dyndata.size - sizeof(report_id);
    int err;

    if (protocol_boot)
    {
        err = -EBADF;
    }
    else
    {
        err = bt_hids_inp_rep_send(&hids_obj, cur_conn,
                                    report_index[report_id],
                                    buffer, size,
                                    report_sent_cb[report_id]);
    }
    }

    if (err)
    {
        if (err == -ENOTCONN)
        {
            LOG_WRN("Cannot send report: device disconnected");
        }
        else if (err == -EBADF)
        {
            LOG_WRN("Cannot send report: incompatible mode");
        }
        else
        {
            LOG_ERR("Cannot send report (%d)", err);
        }
        hid_report_sent(cur_conn, report_id, true);
    }
}

static void notify_secured_fn(struct k_work *work)
{
    secured = true;

    for (size_t r_id = 0; r_id < REPORT_ID_COUNT; r_id++)
    {
        bool enabled = report_enabled[r_id];
        broadcast_subscription_change(r_id, enabled);
    }
}

static void notify_hids(const struct ble_peer_event *event)
{
    int err = 0;

    switch (event->state)
    {
    case PEER_STATE_CONNECTED:
        __ASSERT_NO_MSG(cur_conn == NULL);
        cur_conn = event->id;
        err = bt_hids_connected(&hids_obj, event->id);

        if (err)
        {
            LOG_ERR("Failed to notify the HID Service about the"
                    " connection");
        }
        break;

    case PEER_STATE_DISCONNECTED:
        __ASSERT_NO_MSG(cur_conn == event->id);
        err = bt_hids_disconnected(&hids_obj, event->id);

        if (err)
        {
            LOG_ERR("Connection context was not allocated");
        }

        if (IS_ENABLED(CONFIG_DESKTOP_CONFIG_CHANNEL_ENABLE))
        {
            config_channel_transport_disconnect(
                &cfg_chan_transport);
        }

        cur_conn = NULL;
        secured = false;
        protocol_boot = false;
        if (CONFIG_DESKTOP_HIDS_FIRST_REPORT_DELAY > 0)
        {
            /* Cancel cannot fail if executed from another work's context. */
            (void)k_work_cancel_delayable(&notify_secured);
        }
        break;

    case PEER_STATE_SECURED:
        __ASSERT_NO_MSG(cur_conn == event->id);

        if (CONFIG_DESKTOP_HIDS_FIRST_REPORT_DELAY > 0)
        {
            k_work_reschedule(&notify_secured,
                              K_MSEC(CONFIG_DESKTOP_HIDS_FIRST_REPORT_DELAY));
        }
        else
        {
            notify_secured_fn(NULL);
        }

        break;

    case PEER_STATE_DISCONNECTING:
    case PEER_STATE_CONN_FAILED:
        /* No action */
        break;

    default:
        __ASSERT_NO_MSG(false);
        break;
    }
}

static bool app_event_handler(const struct app_event_header *aeh)
{
    if (is_qdec_module_event(aeh))
    {
        LOG_DBG("Got qdec event");
        send_hid_report(cast_qdec_module_event(aeh));
        return false;
    }
    // if (is_hid_report_event(aeh))
    // {
    //     send_hid_report(cast_hid_report_event(aeh));

    //     return false;
    // }

    if (is_ble_peer_event(aeh))
    {
        notify_hids(cast_ble_peer_event(aeh));

        return false;
    }

    if (is_hid_notification_event(aeh))
    {
        sync_notif_handler(cast_hid_notification_event(aeh));

        return false;
    }

    if (is_module_state_event(aeh))
    {
        struct module_state_event *event = cast_module_state_event(aeh);

        if (check_state(event, MODULE_ID(ble_state),
                        MODULE_STATE_READY))
        {
            static bool initialized;

            __ASSERT_NO_MSG(!initialized);
            initialized = true;

            // if (CONFIG_DESKTOP_HIDS_FIRST_REPORT_DELAY > 0)
            // {
            //     k_work_init_delayable(&notify_secured,
            //                           notify_secured_fn);
            // }

            if (module_init())
            {
                LOG_ERR("Service init failed");

                return false;
            }
            LOG_INF("Service initialized");

            module_set_state(MODULE_STATE_READY);
        }
        return false;
    }

    /* If event is unhandled, unsubscribe. */
    __ASSERT_NO_MSG(false);

    return false;
}
APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, qdec_module_event);
APP_EVENT_SUBSCRIBE(MODULE, hid_notification_event);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, ble_peer_event);
