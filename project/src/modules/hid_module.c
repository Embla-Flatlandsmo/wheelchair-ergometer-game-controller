/*
 * Copyright (c) 2018 - 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <assert.h>
#include <limits.h>
#include <math.h>

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

/* Wheelchair configuration values */

const float max_wheel_speed_m_per_sec = ((float)CONFIG_HID_MODULE_MAX_OUTPUT_SPEED_MM_PER_SEC) / 1000.0f;
/**
 * @brief The radius [m] of one of the cylinders of the ergometer
 */
const float r_c = ((float)CONFIG_APP_CYLINDER_DIAMETER_MM) / (2.0*1000.0);

/**
 * @brief Half of the distance [m] between the wheelchair wheels.
*/
const float r_p = ((float)CONFIG_APP_INTER_WHEEL_DISTANCE_MM) / (2.0*1000.0);

/* Values needed for conversion from float->uint8 */
/**----------------------
 *!    ROUND 1
 *------------------------**/
// const float max_translational_speed_m_per_sec = 3.5;
// const float min_translational_speed_m_per_sec = 0.0;
// const float max_turn_rate_deg_per_sec = 150.0;
// const float min_turn_rate_deg_per_sec = 0.0;
// const float difference_sensitivity_start = 0.3;
// const float difference_sensitivity_end = 0.65;
/**----------------------
 *!    ROUND 2
 *------------------------**/
// const float max_translational_speed_m_per_sec = 3.5f;
// const float max_turn_rate_deg_per_sec = 60.0f;
// const float difference_sensitivity_start = 0.3f;
// const float difference_sensitivity_end = 0.65f;

/**----------------------
 *!    ROUND 3
 *------------------------**/
const float max_translational_speed_m_per_sec = 3.5f;
const float max_turn_rate_deg_per_sec = 150.0f;
const float difference_sensitivity_start = 0.3f;
const float difference_sensitivity_end = 0.65f;

#define BASE_USB_HID_SPEC_VERSION 0x0101

// /* Number of input reports in this application. */
// #define INPUT_REPORT_COUNT 1
// /* Length of Game Pad Input Report containing movement data. */
// #define INPUT_REP_MOVEMENT_NUM_BYTES 5
// /* Index of Game pad Input Report containing movement data. */
// #define INPUT_REP_MOVEMENT_INDEX 0
// /* Id of reference to Game pad Input Report containing movement data. */
// #define INPUT_REP_REF_MOVEMENT_ID 1

#define INPUT_REP_REF_BUTTONS_ID 1
#define INPUT_REP_REF_JOYSTICK_ID 2
/* Length of Game Pad Input Report containing button data. */
#define INPUT_REP_BUTTONS_NUM_BYTES 2
/* Length of Game Pad Input Report containing joystick data. */
#define INPUT_REP_JOYSTICK_NUM_BYTES 4
/* Index of Game pad Input Report containing buttons data. */
#define INPUT_REP_BUTTON_INDEX 0
/* Index of Game pad Input Report containing joystick data. */
#define INPUT_REP_JOYSTICK_INDEX 1

// #define INPUT_REP_REF_JOYSTICK_ID 2
// /* Length of Game Pad Input Report containing joystick data. */
// #define INPUT_REP_JOYSTICK_NUM_BYTES 4
// /* Index of Game pad Input Report containing joystick data. */
// #define INPUT_REP_JOYSTICK_INDEX 0
const uint8_t joystick_neutral = 128;
const int readings_per_log = 1;
static int message_counter = 0;
/* HIDS instance. */
BT_HIDS_DEF(hids_obj,
            INPUT_REP_BUTTONS_NUM_BYTES,
            INPUT_REP_JOYSTICK_NUM_BYTES
            );

static struct bt_conn *cur_conn;
static bool secured;
static bool protocol_boot;


/**========================================================================
 *                     Encoder values to HID report
 *========================================================================**/

static float clamp_f(float val, float min, float max)
{
    if (min > max)
    {
        return CLAMP(val, max, min);
    }
    return CLAMP(val, min, max);
}

static float degree_to_radian(float degrees)
{
    return degrees*M_PI/180.0;
}

static float radian_to_degree(float radians)
{
    return radians*180.0/M_PI;
}

static float rot_speeds_to_turn_rate(float enc_a_rad_per_sec, float enc_b_rad_per_sec)
{
    return r_c*(enc_b_rad_per_sec-enc_a_rad_per_sec)/r_p;
}

static float rot_speeds_to_translational_speed(float enc_a_rad_per_sec, float enc_b_rad_per_sec)
{
    return r_c*(enc_a_rad_per_sec+enc_b_rad_per_sec);
}

static float map_range_f(float value, float input_start, float input_end, float output_start, float output_end)
{
    // float clamped_input = 0.0f;
    // if (input_start > input_end) {
    //     clamped_input = CLAMP(value, input_end, input_start);
    // } else {
    //     clamped_input = CLAMP(value, input_start, input_end);
    // }
    float clamped_input = clamp_f(value, input_start, input_end);
    float input_decimal = (clamped_input - input_start) / (input_end - input_start);
    float output_without_start_offset = input_decimal * (output_end - output_start);
    return output_start+output_without_start_offset;
    // return clamp_f(output_start+output_without_start_offset, output_start, output_end);
    // if (output_start > output_end)
    // {
    //     return CLAMP(output_start + output_without_start_offset, output_end, output_start);
    // }
    // return CLAMP(output_start + output_without_start_offset, output_start, output_end);
    // return CLAMP(output_start + output_without_start_offset, output_start, output_end);
}
static uint8_t map_range(float value, float input_start, float input_end, uint8_t output_start, uint8_t output_end)
{
    float clamped_input = clamp_f(value, input_start, input_end);
    float input_decimal = (clamped_input - input_start) / (input_end - input_start);
    uint8_t output_without_start_offset = (uint8_t)(input_decimal * (float)(output_end - output_start) + 0.5);
    return output_start + output_without_start_offset;
    // if (output_start > output_end)
    // {
    //     return CLAMP(output_start + output_without_start_offset, output_end, output_start);
    // }
    // return CLAMP(output_start + output_without_start_offset, output_start, output_end);
}


static uint8_t rot_speeds_to_hid_move_value(float enc_a_rad_per_sec, float enc_b_rad_per_sec)
{
    float translational_speed = rot_speeds_to_translational_speed(enc_a_rad_per_sec, enc_b_rad_per_sec);
// #if IS_ENABLED(CONFIG_HID_MODULE_LOG_FOR_PLOT)
//     if (message_counter % readings_per_log == 0) {
//     LOG_DBG("Unclamped translational speed: %f [m/s]", translational_speed);
//     LOG_DBG("Clamped translational speed: %f [m/s]", CLAMP(translational_speed, -max_translational_speed_m_per_sec, max_translational_speed_m_per_sec));
//     }
// #endif
    // if (IN_RANGE(translational_speed, -min_translational_speed_m_per_sec, min_translational_speed_m_per_sec))
    // {
    //     translational_speed = 0.0;
    // }
    translational_speed *= -1; // y-axis seems to be inverted on game controllers, i.e. 0=positive, max and 255=negative
    // float speed_sign = translational_speed < 0.0 ? -1.0 : 1.0;
    // bool is_negative = turn_rate < 0.0;
    // uint8_t deadzoned_value = map_range(translational_speed * speed_sign, min_translational_speed_m_per_sec, max_translational_speed_m_per_sec, 0, 127);
    // uint8_t output_value = 128 + speed_sign * deadzoned_value;
    // return output_value;
    return map_range(translational_speed, -max_translational_speed_m_per_sec, max_translational_speed_m_per_sec, 0, 255);
}

const float sensitivity_alpha = 0.4;
static float filter_sensitivity(float sensitivity)
{
    static float prev_sensitivity_value = 1.0;
    // if (sensitivity >= 0.95) {
    //     prev_sensitivity_value = 1.0;
    //     return prev_sensitivity_value;
    // }
    float filtered_sensitivity = sensitivity_alpha*prev_sensitivity_value+(1.0-sensitivity_alpha)*sensitivity;
    prev_sensitivity_value = MIN(filtered_sensitivity, sensitivity);
    return prev_sensitivity_value;
}

static float filter_turn_rate(float turn_rate)
{
    // __ASSERT_NO_MSG(turn_rate >= 0.0)
    // float filter_start = 0.2*max_turn_rate_deg_per_sec;
    // if (turn_rate == max_turn_rate_deg_per_sec) {
    //     return turn_rate;
    // }
    // deadzone_width = CLAMP(deadzone_width, 0.0, 0.8);
    float filter_start = 0.0;
    float filter_end = 30.0; 
    if (turn_rate <= filter_start)
    {
        return 0.0;
    }
    if (turn_rate >= filter_end) {
        return turn_rate;   
    }
    return filter_end*pow((turn_rate-filter_start)/filter_end, 2);
}

static uint8_t rot_speeds_to_hid_turn_value(float enc_a_rad_per_sec, float enc_b_rad_per_sec)
{
    float turn_rate = rot_speeds_to_turn_rate(enc_a_rad_per_sec, enc_b_rad_per_sec)/4.0;
    turn_rate = radian_to_degree(turn_rate);
    // LOG_DBG("Unclamped turn rate: %f [deg/s]", turn_rate);
    // LOG_DBG("Clamped turn rate: %f [deg/s]", CLAMP(turn_rate, -max_turn_rate_deg_per_sec, max_turn_rate_deg_per_sec));
    float speed = rot_speeds_to_translational_speed(enc_a_rad_per_sec, enc_b_rad_per_sec);
    
    // APPROACH 1
    // speed = speed > 0.0 ? speed : -speed;
    // float difference_sensitivity = map_range_f(speed, max_translational_speed_m_per_sec * difference_sensitivity_start, max_translational_speed_m_per_sec * difference_sensitivity_end, 1.0, 0.0);
    // turn_rate = turn_rate*difference_sensitivity;
    // LOG_DBG("Difference sensitivity: %f",difference_sensitivity);    
    // LOG_DBG("Sensitivity-adjusted turn rate: %f [deg/s]", turn_rate);

    // APPROACH 2
    // float turn_rate_sign = turn_rate >= 0.0 ? 1.0 : -1.0;
    // turn_rate *= turn_rate_sign; // must be positive
    // // turn_rate = turn_rate_sign*filter_turn_rate(turn_rate);
    // float output_turn_rate = turn_rate_sign*filter_turn_rate(turn_rate);
    // APPROACH 3: same as 2 but with less sensitivity for higher speeds
    float turn_rate_sign = turn_rate >= 0.0 ? 1.0 : -1.0;
    turn_rate *= turn_rate_sign; // must be positive
    float speed_signed = speed;
    speed = speed > 0.0 ? speed : -speed;
    float difference_sensitivity = map_range_f(speed, max_translational_speed_m_per_sec * difference_sensitivity_start, max_translational_speed_m_per_sec * difference_sensitivity_end, 1.0, 0.6);
    float filtered_difference_sensitivity = filter_sensitivity(difference_sensitivity);
    float filtered_turn_rate = turn_rate_sign * filter_turn_rate(turn_rate);
    float output_turn_rate = filtered_turn_rate * filtered_difference_sensitivity;
#if IS_ENABLED(CONFIG_HID_MODULE_LOG_FOR_PLOT)
    // if (message_counter % readings_per_log == 0) {
        LOG_DBG("S, SC, UTR, DS, FDS, FTR, FRTR = (%f, %f, %f, %f, %f, %f, %f)", speed_signed, CLAMP(speed_signed, -max_translational_speed_m_per_sec, max_translational_speed_m_per_sec), turn_rate, difference_sensitivity, filtered_difference_sensitivity, filtered_turn_rate, output_turn_rate);
        // LOG_DBG("Unclamped turn rate: %f [deg/s]", turn_rate);
        // // LOG_DBG("Clamped turn rate: %f [deg/s]", CLAMP(turn_rate, -max_turn_rate_deg_per_sec, max_turn_rate_deg_per_sec));
        // LOG_DBG("Difference sensitivity: %f", difference_sensitivity);
        // LOG_DBG("Filtered difference sensitivity: %f", filtered_difference_sensitivity);
        // LOG_DBG("Filtered turn rate: %f [deg/s]", filtered_turn_rate);
        // LOG_DBG("Filtered and reduced (final) turn rate: %f [deg/s]", output_turn_rate);
        // LOG_DBG("Sensitivity-adjusted turn rate: %f [deg/s]", turn_rate);
    // }
#endif
    return map_range(output_turn_rate, -max_turn_rate_deg_per_sec, max_turn_rate_deg_per_sec, 0, 255);
}

/**============================================
 *         HID and connectivity-specific
 *=============================================**/
static void send_hid_report(uint8_t x_axis, uint8_t y_axis)
{
    if (!cur_conn || !secured)
    {
        return;
    }
    int err;

    uint8_t send_buffer[INPUT_REP_JOYSTICK_NUM_BYTES];

    if (IS_ENABLED(CONFIG_HID_MODULE_CONTROLLER_OUTPUT_A))
    {
        const uint8_t output[INPUT_REP_JOYSTICK_NUM_BYTES] = {x_axis, y_axis, joystick_neutral, joystick_neutral};
        memcpy(send_buffer, output, sizeof(output));
    }
    else {
        const uint8_t output[INPUT_REP_JOYSTICK_NUM_BYTES] = {joystick_neutral, y_axis, x_axis, joystick_neutral};
        memcpy(send_buffer, output, sizeof(output));
    }

    err = bt_hids_inp_rep_send(&hids_obj, cur_conn,
                    INPUT_REP_JOYSTICK_INDEX,
                    send_buffer, INPUT_REP_JOYSTICK_NUM_BYTES, NULL);
    if (err)
    {
        LOG_ERR("Cannot send left joystick report (%d)", err);
        return;
    }

    // uint8_t btns[2] = {y_axis,y_axis};

    //     err = bt_hids_inp_rep_send(&hids_obj, cur_conn,
    //                 INPUT_REP_BUTTON_INDEX,
    //                 btns, sizeof(btns), NULL);

    // if (err)
    // {
    //     LOG_ERR("Cannot send buttons report (%d)", err);
    //     return;
    // }

    if (message_counter % readings_per_log == 0)
    {
        LOG_DBG("x_axis, y_axis: (%d, %d)", x_axis, y_axis);
    }
        // LOG_DBG("HID report successfully sent. x_axis: %d, y_axis: %d", x_axis, y_axis);
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

/**========================================================================
 *                           Event handlers
 *========================================================================**/
static int module_init(void)
{
    LOG_INF("r_c: %f[m], r_p: %f[m]", r_c, r_p);
    LOG_INF("Max translational speed: +-%f [m/s]", max_translational_speed_m_per_sec);
    // LOG_INF("Translational speed deadzone: +-%f [m/s]", min_translational_speed_m_per_sec);
    LOG_INF("Max turn rate: +-%f [deg/s]", max_turn_rate_deg_per_sec);
    LOG_INF("Alpha for encoder: %f", ((float)(CONFIG_ENCODER_MOVING_AVERAGE_ALPHA)/1000.0));
    LOG_INF("dt: %f[ms]", (float)CONFIG_ENCODER_DELTA_TIME_MSEC);
    LOG_INF("Encoder readings per log output: %d", readings_per_log);
    LOG_INF("Difference sensitivty start threshold: %f*max trans speed", difference_sensitivity_start);
    LOG_INF("Difference sensitivity end threshold: %f*max trans speed", difference_sensitivity_end);
    // LOG_INF("Turn rate deadzone: +-%f [deg/s]", min_turn_rate_deg_per_sec);
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

    // const uint8_t buttons_rep_mask[] = {0b11000000};
    hids_input_report->size = INPUT_REP_BUTTONS_NUM_BYTES;
    hids_input_report->id = INPUT_REP_REF_BUTTONS_ID;
    hids_input_report->rep_mask = NULL;
    hids_init_param.inp_rep_group_init.cnt++;

    // const uint8_t joystick_rep_mask[] = {0b00111100};
    hids_input_report++;
    hids_input_report->size = INPUT_REP_JOYSTICK_NUM_BYTES;
    hids_input_report->id = INPUT_REP_REF_JOYSTICK_ID;
    hids_input_report->rep_mask = NULL;
    hids_init_param.inp_rep_group_init.cnt++;

    // hids_input_report++;
    // hids_input_report->size = INPUT_REP_RIGHT_JOYSTICK_NUM_BYTES;
    // hids_input_report->id = INPUT_REP_REF_RIGHT_JOYSTICK_ID;
    // hids_input_report->rep_mask = NULL;
    // hids_init_param.inp_rep_group_init.cnt++;
    // hids_init_param.pm_evt_handler = hids_pm_evt_handler;
    return bt_hids_init(&hids_obj, &hids_init_param);
}

static void encoder_event_to_hid_value(const struct encoder_module_event *event, uint8_t *turn_rate, uint8_t *trans_speed)
{
    if (event->type != ENCODER_EVT_DATA_READY)
    {
        return;
    }
    float enc_a_rad_per_sec = degree_to_radian(event->rot_speed_a);
    float enc_b_rad_per_sec = degree_to_radian(event->rot_speed_b);
    (*turn_rate) = rot_speeds_to_hid_turn_value(enc_a_rad_per_sec, enc_b_rad_per_sec);
    (*trans_speed) = rot_speeds_to_hid_move_value(enc_a_rad_per_sec, enc_b_rad_per_sec);
    // LOG_DBG("Move Val: %d, Turn Val: %d", *trans_speed, *turn_rate);
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
        uint8_t trans_speed = 0;
        uint8_t turn_rate = 0;
        
        encoder_event_to_hid_value(cast_encoder_module_event(aeh), &turn_rate, &trans_speed);
        send_hid_report(turn_rate, trans_speed);

        message_counter++;
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
