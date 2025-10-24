/* Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <date_time.h>
//#include <cJSON.h>
#include <stdio.h>
#include <modem/modem_info.h>

#if defined(CONFIG_ADP536X)
#include <adp536x.h>
#endif

#include "application.h"
#include "temperature.h"
#include "connection.h"

#include "led_control.h"

#include "vibration.h"

#ifdef CONFIG_CLOUD_THINGSBOARD_COAP
#include "tb_coap.h"	
#endif // CONFIG_CLOUD_THINGSBOARD_COAP

LOG_MODULE_REGISTER(application, CONFIG_LOG_DEFAULT_LEVEL);

/* Timer used to time the sensor sampling rate. */
static K_TIMER_DEFINE(sensor_sample_timer, NULL, NULL);


void main_application_thread_fn(void)
{
	int err = modem_info_init();
	if (err) {
		LOG_ERR("Failed initializing modem info module, error: %d", err);
	}

    LOG_INF("Waiting for LTE connection...");
    (void)await_lte_connection(K_FOREVER);
    LOG_INF("LTE connected.");
    
    /* Wait for the date and time to become known.
     * This is needed for secure CoAP communication (DTLS).
     */
    LOG_INF("Waiting for modem to determine current date and time");
    if (!await_date_time_known(K_SECONDS(CONFIG_DATE_TIME_ESTABLISHMENT_TIMEOUT_SECONDS))) {
        LOG_WRN("Failed to determine valid date time. Proceeding anyways");
    } else {
        LOG_INF("Current date and time determined");
    }

	int counter = 0;

	LOG_INF("Start vibration tracking");
	start_vibration_tracking();
#ifdef CONFIG_CLOUD_THINGSBOARD_COAP
	LOG_INF("Start TB CoAP cloud transport");
	start_tb_coap();
#endif // CONFIG_CLOUD_THINGSBOARD_COAP

	/* Begin sampling sensors. */
	int iteration = 0;
	const int detailedIterationInterval = 50; // 50x for production use. move to config?
	while (true) {
        bool detailedIteration = (iteration++) % detailedIterationInterval == 0;
        /* Start the sensor sample interval timer. */
        k_timer_start(&sensor_sample_timer,
            K_SECONDS(CONFIG_SENSOR_SAMPLE_INTERVAL_SECONDS), K_FOREVER);

        if (IS_ENABLED(CONFIG_TEMP_TRACKING)) {
            double temp = -1;

            if (get_temperature(&temp) == 0) {
                LOG_INF("Temperature is %d degrees C", (int)temp);
                if (detailedIteration)
                {
                    /* This is the only line that should send temperature */
                    tb_coap_send_telemetry_double("temp", temp);
                }
            }
        }

        /* Test counter is fine to keep, but it uses no cloud function */
        if (IS_ENABLED(CONFIG_TEST_COUNTER)) {
            LOG_INF("Sent test counter = %d", counter);
            counter++;
        }

        /* Get the latest raw accelerometer data */
        double accel_x, accel_y, accel_z;
        get_vibration_xyz(&accel_x, &accel_y, &accel_z);
        LOG_INF("Accel X: %.2f, Y: %.2f, Z: %.2f", accel_x, accel_y, accel_z);

        if (detailedIteration)
        {
            /* Send iteration counter */
            tb_coap_send_telemetry_int("it", iteration);

            /* Request "Reference Signal Receive Power" from modem */
            int16_t modem_rsrp = 0;
            err = modem_info_short_get(MODEM_INFO_RSRP, &modem_rsrp);
            if (err != sizeof(modem_rsrp)) {
                LOG_ERR("modem_info_short_get, error: %d", err);
            }
            tb_coap_send_telemetry_int("rsrp", modem_rsrp);

            /* measure battery */
#if defined(CONFIG_ADP536X)
            uint8_t percentage;
            int err = adp536x_fg_soc(&percentage);
            if (err) {
                LOG_ERR("Failed to get battery level: %d", err);
            }
            else
            {
                tb_coap_send_telemetry_int("batp", percentage);
            }
            uint16_t voltage;
            err = adp536x_fg_volts(&voltage);
            if (err) {
                LOG_ERR("Failed to get battery voltage: %d", err);
            }
            else
            {
                tb_coap_send_telemetry_int("batv", voltage);
            }
#endif
        }

#ifdef CONFIG_CLOUD_THINGSBOARD_COAP
        /* This is your high-frequency accelerometer send. This is PERFECT. */
        char payload_buf[128]; 
        snprintf(payload_buf, sizeof(payload_buf),
                 "{\"accel_x\":%.2f, \"accel_y\":%.2f, \"accel_z\":%.2f}",
                 accel_x, accel_y, accel_z);
        tb_coap_send_telemetry_payload_string("accel", payload_buf);
#endif // CONFIG_CLOUD_THINGSBOARD_COAP

        /* Wait out any remaining time on the sample interval timer. */
        k_timer_status_sync(&sensor_sample_timer);
    }
}
