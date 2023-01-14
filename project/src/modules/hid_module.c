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
#include "events/encoder_module_event.h"

#define MODULE hid_module
#include <caf/events/module_state_event.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_HID_MODULE_LOG_LEVEL);

#define M_PI   3.14159265358979323846264338327950288

const float max_speed_m_per_sec = ((float)CONFIG_APP_MAX_SPEED_MM_PER_SEC)/1000.0f;
const float min_speed_m_per_sec = ((float)CONFIG_APP_MIN_SPEED_MM_PER_SEC)/1000.0f;
const float cylinder_diameter_m = ((float)CONFIG_APP_CYLINDER_DIAMETER_MM)/1000.0f;
const float max_speed_diff_m_per_sec = ((float)CONFIG_APP_MAX_SPEED_DIFF_MM_PER_SEC)/1000.0f;
const float min_speed_diff_m_per_sec = ((float)CONFIG_APP_MIN_SPEED_DIFF_MM_PER_SEC)/1000.0f;


#define BASE_USB_HID_SPEC_VERSION 0x0101

// /* Number of input reports in this application. */
// #define INPUT_REPORT_COUNT 1
// /* Length of Game Pad Input Report containing movement data. */
// #define INPUT_REP_MOVEMENT_NUM_BYTES 5
// /* Index of Game pad Input Report containing movement data. */
// #define INPUT_REP_MOVEMENT_INDEX 0
// /* Id of reference to Game pad Input Report containing movement data. */
// #define INPUT_REP_REF_MOVEMENT_ID 1

// #define INPUT_REP_REF_BUTTONS_ID 1
// #define INPUT_REP_REF_JOYSTICK_ID 2
// /* Length of Game Pad Input Report containing button data. */
// #define INPUT_REP_BUTTONS_NUM_BYTES 2
// /* Length of Game Pad Input Report containing joystick data. */
// #define INPUT_REP_JOYSTICK_NUM_BYTES 4
// /* Index of Game pad Input Report containing buttons data. */
// #define INPUT_REP_BUTTON_INDEX 0
// /* Index of Game pad Input Report containing joystick data. */
// #define INPUT_REP_JOYSTICK_INDEX 1

#define INPUT_REP_REF_JOYSTICK_ID 2
/* Length of Game Pad Input Report containing joystick data. */
#define INPUT_REP_JOYSTICK_NUM_BYTES 2
/* Index of Game pad Input Report containing joystick data. */
#define INPUT_REP_JOYSTICK_INDEX 0



/* HIDS instance. */
BT_HIDS_DEF(hids_obj,
            // INPUT_REP_BUTTONS_NUM_BYTES,
            INPUT_REP_JOYSTICK_NUM_BYTES
            );

static struct bt_conn *cur_conn;
static bool secured;
static bool protocol_boot;

static float rot_speed_to_meters_per_second(float rot_speed_deg_per_second)
{
    float rot_speed_rad_per_second = rot_speed_deg_per_second * M_PI / 180.0;
    return cylinder_diameter_m/2.0 * rot_speed_rad_per_second;
}

static float wheel_speed_avg(float wheel_a_speed, float wheel_b_speed)
{
    return (wheel_a_speed + wheel_b_speed) / 2.0;
}

static float wheel_speed_difference(float wheel_a_speed, float wheel_b_speed)
{
    return wheel_a_speed - wheel_b_speed;
}

static float speed_limits(float speed_m_per_sec)
{
    if (IN_RANGE(speed_m_per_sec, -min_speed_m_per_sec, min_speed_m_per_sec)) {
        return 0.0;
    }
    return CLAMP(speed_m_per_sec, -max_speed_m_per_sec, max_speed_m_per_sec);
}

static uint8_t map_range(float value, float input_start, float input_end, uint8_t output_start, uint8_t output_end)
{
    float input_decimal = (value-input_start)/(input_end-input_start);
    uint8_t output_without_start_offset = (uint8_t)(input_decimal*(float)(output_end-output_start)+0.5);
    return CLAMP(output_start + output_without_start_offset, output_start, output_end);
}

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
    struct bt_hids_inp_rep *hids_input_report =
        &hids_init_param.inp_rep_group_init.reports[0];

    // hids_input_report->size = INPUT_REP_BUTTONS_NUM_BYTES;
	// hids_input_report->id = INPUT_REP_REF_BUTTONS_ID;
	// hids_input_report->rep_mask = NULL;
	// hids_init_param.inp_rep_group_init.cnt++;

    // hids_input_report++;
    hids_input_report->size = INPUT_REP_JOYSTICK_NUM_BYTES;
	hids_input_report->id = INPUT_REP_REF_JOYSTICK_ID;
	hids_input_report->rep_mask = NULL;
	hids_init_param.inp_rep_group_init.cnt++;
	// hids_init_param.pm_evt_handler = hids_pm_evt_handler;
    return bt_hids_init(&hids_obj, &hids_init_param);
}

static void encoder_event_to_speed(const struct encoder_module_event *event, uint8_t* wheel_difference, uint8_t* wheel_avg)
{
    if (event->type != ENCODER_EVT_DATA_READY)
    {
        return;
    }
    
    float wheel_a_speed = rot_speed_to_meters_per_second(event->rot_speed_a);
    float wheel_b_speed = rot_speed_to_meters_per_second(event->rot_speed_b);

    wheel_a_speed = speed_limits(wheel_a_speed);
    wheel_b_speed = speed_limits(wheel_b_speed);

    float speed_diff = wheel_speed_difference(wheel_a_speed, wheel_b_speed);
    float speed_avg = wheel_speed_avg(wheel_a_speed, wheel_b_speed);

    (*wheel_difference) = map_range(speed_diff, -max_speed_diff_m_per_sec, max_speed_diff_m_per_sec, 0, 255);
    (*wheel_avg) = map_range(speed_avg, -max_speed_m_per_sec, max_speed_m_per_sec, 0, 255);
    LOG_DBG("Wheel_avg: %d, wheel_diff: %d", *wheel_avg, *wheel_difference);
}

static void send_hid_report(uint8_t x_axis, uint8_t y_axis)
{
    // for (size_t i = 0; i < CONFIG_BT_HIDS_MAX_CLIENT_COUNT; i++) {
    if (!cur_conn || !secured)
    {
        return;
    }
    uint8_t buffer[2] = {0, 0};

    int err;
    err = bt_hids_inp_rep_send(&hids_obj, cur_conn,
                    INPUT_REP_JOYSTICK_INDEX,
                    buffer, sizeof(buffer), NULL);
    if (err)
    {
        LOG_ERR("Cannot send report (%d)", err);
        return;
        // hid_report_sent(cur_conn, report_id, true);
    }

    // uint8_t btns[2] = {0,0};

    //     err = bt_hids_inp_rep_send(&hids_obj, cur_conn,
    //                 INPUT_REP_BUTTON_INDEX,
    //                 btns, sizeof(btns), NULL);

    // if (err)
    // {
    //     LOG_ERR("Cannot send report (%d)", err);
    //     return;
    // }
    
    // LOG_DBG("HID report successfully sent. x-axis val: %d, y-axis val: %d", buffer[0], buffer[1]);
    // }
}

// static void notify_secured_fn(struct k_work *work)
// {
//     secured = true;

//     for (size_t r_id = 0; r_id < REPORT_ID_COUNT; r_id++)
//     {
//         bool enabled = report_enabled[r_id];
//         broadcast_subscription_change(r_id, enabled);
//     }
// }

static void notify_hids(const struct ble_peer_event *event)
{
    int err = 0;

    switch (event->state)
    {
    case PEER_STATE_CONNECTED:
        __ASSERT_NO_MSG(cur_conn == NULL);
        cur_conn = event->id;
        err = bt_hids_connected(&hids_obj, event->id);
        // insert_conn_object(event->id);
        if (err)
        {
            LOG_ERR("Failed to notify the HID Service about the"
                    " connection");
        }
        break;

    case PEER_STATE_DISCONNECTED:
        __ASSERT_NO_MSG(cur_conn == event->id);
        err = bt_hids_disconnected(&hids_obj, event->id);
		cur_conn = NULL;
		secured = false;
		protocol_boot = false;
        if (err)
        {
            LOG_ERR("Connection context was not allocated");
        }

        // cur_conn = NULL;
        // secured = false;
        // protocol_boot = false;
        // if (CONFIG_DESKTOP_HIDS_FIRST_REPORT_DELAY > 0)
        // {
        //     /* Cancel cannot fail if executed from another work's context. */
        //     (void)k_work_cancel_delayable(&notify_secured);
        // }
        break;

    case PEER_STATE_SECURED:
        __ASSERT_NO_MSG(cur_conn == event->id);
        secured = true;

        // if (CONFIG_DESKTOP_HIDS_FIRST_REPORT_DELAY > 0)
        // {
        //     k_work_reschedule(&notify_secured,
        //                       K_MSEC(CONFIG_DESKTOP_HIDS_FIRST_REPORT_DELAY));
        // }
        // else
        // {
        //     notify_secured_fn(NULL);
            
        // }

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
    if (is_encoder_module_event(aeh))
    {
        // k_timer_start(&encoder_timeout, K_MSEC(CONFIG_HID_TIMEOUT_DURATION_MSEC), K_NO_WAIT);
        uint8_t speed_diff = 0;
        uint8_t speed_avg = 0;
        encoder_event_to_speed(cast_encoder_module_event(aeh), &speed_diff, &speed_avg);
        send_hid_report(speed_diff, speed_avg);
        return false;
    }

    if (is_ble_peer_event(aeh))
    {
        notify_hids(cast_ble_peer_event(aeh));

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
        }
        return false;
    }

    /* If event is unhandled, unsubscribe. */
    __ASSERT_NO_MSG(false);

    return false;
}
APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, encoder_module_event);
APP_EVENT_SUBSCRIBE(MODULE, hid_notification_event);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, ble_peer_event);
