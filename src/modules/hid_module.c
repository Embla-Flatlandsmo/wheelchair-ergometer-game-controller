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

/**
 * @brief IIR coefficient of turning sensitivity vs translational speed.
 *        y[t]=alpha*y[t-1]+(1-alpha)*x[t]
 */
const float sensitivity_alpha = 0.4;

/* Values that were tweaked for device iterations */
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

#define HID_TURN_SCALING (float)CONFIG_HID_MODULE_TURN_SCALING_MULTIPLIER_THOUSANDTHS/1000.0f

#define BASE_USB_HID_SPEC_VERSION 0x0101

/* Report ID of the button (see hid_report_desc.c)*/
#define INPUT_REP_REF_BUTTONS_ID 1
/* Report ID of the joystick (see hid_report_desc.c)*/
#define INPUT_REP_REF_JOYSTICK_ID 2
/* Length of Game Pad Input Report containing button data. */
#define INPUT_REP_BUTTONS_NUM_BYTES 2
/* Length of Game Pad Input Report containing joystick data. */
#define INPUT_REP_JOYSTICK_NUM_BYTES 4
/* Index of Game pad Input Report containing buttons data. */
#define INPUT_REP_BUTTON_INDEX 0
/* Index of Game pad Input Report containing joystick data. */
#define INPUT_REP_JOYSTICK_INDEX 1

const uint8_t joystick_neutral = 128;

/* Logging utils */
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

/**
 * @brief Physical model for turning rotational speeds of rollers into
 *        a player turn rate.
 *
 * @param enc_a_rad_per_sec Angular velocity of right-hand rollers (rad/s)
 * @param enc_b_rad_per_sec Angular velocity of left-hand rollers (rad/s)
 * @return float angular velocity about upward axis of the player (clockwise=positive)
 */
static float rot_speeds_to_turn_rate(float enc_a_rad_per_sec, float enc_b_rad_per_sec)
{
    return r_c*(enc_b_rad_per_sec-enc_a_rad_per_sec)/r_p;
}

/**
 * @brief Physical model for turning rotational speeds of rollers into
 *        player velocity.
 *
 * @param enc_a_rad_per_sec Angular velocity of right-hand rollers (rad/s)
 * @param enc_b_rad_per_sec Angular velocity of left-hand rollers (rad/s)
 * @return float Velocity of player
 */
static float rot_speeds_to_translational_speed(float enc_a_rad_per_sec, float enc_b_rad_per_sec)
{
    return r_c*(enc_a_rad_per_sec+enc_b_rad_per_sec);
}

/**
 * @brief maps a floating-point range to another floating-point range
 *          If the value is outside of the input range, it will be clamped.
 * 
 * @param value Value to map
 * @param input_start Start range of the input value
 * @param input_end End of range of the input value
 * @param output_start Start of range for the output value
 * @param output_end End of range for the input value
 * @return float the value mapped to the new range
 */
static float map_range_f(float value, float input_start, float input_end, float output_start, float output_end)
{
    float clamped_input = clamp_f(value, input_start, input_end);
    float input_decimal = (clamped_input - input_start) / (input_end - input_start);
    float output_without_start_offset = input_decimal * (output_end - output_start);
    return output_start+output_without_start_offset;
}
/**
 * @brief Maps a floating-point range to an uint8 range.
 *        If the value is outside of the input range
 *
 * @param value Value to map
 * @param input_start Start range of the input value
 * @param input_end End of range of the input value
 * @param output_start Start of range for the output value
 * @param output_end End of range for the input value
 * @return uint8_t the value mapped to the new range
 */
static uint8_t map_range(float value, float input_start, float input_end, uint8_t output_start, uint8_t output_end)
{
    float clamped_input = clamp_f(value, input_start, input_end);
    float input_decimal = (clamped_input - input_start) / (input_end - input_start);
    uint8_t output_without_start_offset = (uint8_t)(input_decimal * (float)(output_end - output_start) + 0.5);
    return output_start + output_without_start_offset;
}

/**
 * @brief Convert encoder readings into a mapped uint8_t, ready for transmission
 *
 * @param enc_a_rad_per_sec Angular velocity of right-hand rollers (rad/s)
 * @param enc_b_rad_per_sec Angular velocity of left-hand rollers (rad/s)
 * @return uint8_t A value in range 0-255 which describes how fast the player is to move forwards.
 */
static uint8_t rot_speeds_to_hid_move_value(float enc_a_rad_per_sec, float enc_b_rad_per_sec)
{
    float translational_speed = rot_speeds_to_translational_speed(enc_a_rad_per_sec, enc_b_rad_per_sec);
    translational_speed *= -1; // y-axis seems to be inverted on game controllers, i.e. 0=positive, max and 255=negative
    return map_range(translational_speed, -max_translational_speed_m_per_sec, max_translational_speed_m_per_sec, 0, 255);
}

/**
 * @brief Ensure that the sensitivity multiplier rises slightly slower
 *        than the translational speed of the wheels.
 * @param sensitivity output from sensitivity mapping function
 * @return float filtered sensitivity
 */
static float filter_sensitivity(float sensitivity)
{
    static float prev_sensitivity_value = 1.0;
    float filtered_sensitivity = sensitivity_alpha*prev_sensitivity_value+(1.0-sensitivity_alpha)*sensitivity;
    prev_sensitivity_value = MIN(filtered_sensitivity, sensitivity);
    return prev_sensitivity_value;
}

/**
 * @brief Slow start mapping of turning signal
 * 
 * @param turn_rate Unprocessed turning signal
 * @return float Turn signal, mapped to slow start
 */
static float slow_start(float turn_rate)
{
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

/**
 * @brief Turns rotational speeds into a HID turn value
 *
 * @param enc_a_rad_per_sec Angular velocity of right-hand rollers (rad/s)
 * @param enc_b_rad_per_sec Angular velocity of left-hand rollers (rad/s)
 * @return uint8_t HID turning value between 0-255
 */
static uint8_t rot_speeds_to_hid_turn_value(float enc_a_rad_per_sec, float enc_b_rad_per_sec)
{
    float speed = rot_speeds_to_translational_speed(enc_a_rad_per_sec, enc_b_rad_per_sec);
    float speed_signed = speed;
    speed = speed > 0.0 ? speed : -speed;
    float difference_sensitivity = map_range_f(speed, max_translational_speed_m_per_sec * difference_sensitivity_start, max_translational_speed_m_per_sec * difference_sensitivity_end, 1.0, 0.6);
    float filtered_difference_sensitivity = filter_sensitivity(difference_sensitivity);

    float turn_rate = rot_speeds_to_turn_rate(enc_a_rad_per_sec, enc_b_rad_per_sec) * HID_TURN_SCALING;
    turn_rate = radian_to_degree(turn_rate);
    float turn_rate_sign = turn_rate >= 0.0 ? 1.0 : -1.0;
    turn_rate *= turn_rate_sign; // must be positive;
    float filtered_turn_rate = turn_rate_sign * slow_start(turn_rate);
    
    float output_turn_rate = filtered_turn_rate * filtered_difference_sensitivity;
#if IS_ENABLED(CONFIG_HID_MODULE_LOG_FOR_PLOT)
    if (message_counter % readings_per_log == 0) {
        LOG_DBG("S, SC, UTR, DS, FDS, FTR, FRTR = (%f, %f, %f, %f, %f, %f, %f)", speed_signed, CLAMP(speed_signed, -max_translational_speed_m_per_sec, max_translational_speed_m_per_sec), turn_rate, difference_sensitivity, filtered_difference_sensitivity, filtered_turn_rate, output_turn_rate);
    }
#endif
    return map_range(output_turn_rate, -max_turn_rate_deg_per_sec, max_turn_rate_deg_per_sec, 0, 255);
}

/**============================================
 *         HID and connectivity-specific
 *=============================================**/

/**
 * @brief Formats and transmits the HID report over bluetooth
 * 
 * @param x_axis x-axis for joystick
 * @param y_axis y-axis for joystick
 */
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
    if (message_counter % readings_per_log == 0)
    {
        LOG_DBG("x_axis, y_axis: (%d, %d)", x_axis, y_axis);
    }
}

/**========================================================================
 *                           Event handlers
 *========================================================================**/
static int module_init(void)
{
    LOG_INF("r_c: %f[m], r_p: %f[m]", r_c, r_p);
    LOG_INF("Max translational speed: +-%f [m/s]", max_translational_speed_m_per_sec);
    LOG_INF("Max turn rate: +-%f [deg/s]", max_turn_rate_deg_per_sec);
    LOG_INF("Alpha for encoder: %f", ((float)(CONFIG_ENCODER_MOVING_AVERAGE_ALPHA)/1000.0));
    LOG_INF("dt: %f[ms]", (float)CONFIG_ENCODER_DELTA_TIME_MSEC);
    LOG_INF("Encoder readings per log output: %d", readings_per_log);
    LOG_INF("Difference sensitivty start threshold: %f*max trans speed", difference_sensitivity_start);
    LOG_INF("Difference sensitivity end threshold: %f*max trans speed", difference_sensitivity_end);

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

    hids_input_report->size = INPUT_REP_BUTTONS_NUM_BYTES;
    hids_input_report->id = INPUT_REP_REF_BUTTONS_ID;
    hids_input_report->rep_mask = NULL;
    hids_init_param.inp_rep_group_init.cnt++;

    hids_input_report++;
    hids_input_report->size = INPUT_REP_JOYSTICK_NUM_BYTES;
    hids_input_report->id = INPUT_REP_REF_JOYSTICK_ID;
    hids_input_report->rep_mask = NULL;
    hids_init_param.inp_rep_group_init.cnt++;

    return bt_hids_init(&hids_obj, &hids_init_param);
}

/**
 * @brief Handle the encoder event, creating HID turn rate 
 * and translational speeds from encoder rotational speeds-
 * 
 * @param event Encoder module event containing rotational speeds from encoder A and B
 * @param turn_rate [output] HID turn rate value
 * @param trans_speed [output] HID translational speed value
 */
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
}

/**
 * @brief Update hids library when bluetooth is (dis)connected
 * 
 * @param event CAF bluetooth event
 */
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
        cur_conn = NULL;
        secured = false;
        protocol_boot = false;
        if (err)
        {
            LOG_ERR("Connection context was not allocated");
        }
        break;

    case PEER_STATE_SECURED:
        __ASSERT_NO_MSG(cur_conn == event->id);
        secured = true;
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

/**
 * @brief Main event handler
 * 
 * @param aeh event header for event to which the handler shall respond
 * @return true the event is consumed
 * @return false the event is not consumed
 */
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
