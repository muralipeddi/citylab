/* Copyright (c) 2022 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <date_time.h>
#include <math.h>
#include <modem/modem_info.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if defined(CONFIG_ADP536X)
#include <adp536x.h>
#endif

#include "application.h"
#include "connection.h"
#include "led_control.h"
#include "tb_coap.h"
#include "vibration.h"

LOG_MODULE_REGISTER(application, CONFIG_LOG_DEFAULT_LEVEL);

static K_TIMER_DEFINE(sensor_sample_timer, NULL, NULL);

/* --- CONFIGURATION --- */

/* 1. SENSOR SPEED: Run the loop every 200ms (5Hz) to catch spikes */
#define SAMPLING_RATE_MS 200

/* 2. VIBRATION UPLOAD SPEED: 5 loops * 200ms = 1000ms (1 Second) */
#define LOOPS_PER_UPLOAD 5

/* 3. BATTERY REPORT SPEED: 1 Hour (in milliseconds) */
/* 60 min * 60 sec * 1000 ms = 3600000 */
#define BATTERY_REPORT_INTERVAL_MS 3600000 / 5 // change

/* 4. ERROR BACKOFF: Wait 10s if the cloud rejects us */
#define ERROR_BACKOFF_TIME_MS 10000

float threshold = 0.0; // change

void main_application_thread_fn(void) {
  int err = modem_info_init();
  if (err)
    LOG_ERR("Modem init error: %d", err);

  LOG_INF("Waiting for LTE...");
  (void)await_lte_connection(K_FOREVER);
  LOG_INF("LTE connected.");

  /* Wait for IP stack to settle */
  LOG_INF("Waiting 5s for network IP stack...");
  k_sleep(K_SECONDS(5));

  LOG_INF("Start vibration tracking");
  start_vibration_tracking();

  LOG_INF("Start TB CoAP");
  start_tb_coap();

  led_set_connection_status(true);

  k_timer_start(&sensor_sample_timer, K_MSEC(SAMPLING_RATE_MS),
                K_MSEC(SAMPLING_RATE_MS));

  /* LOGIC VARIABLES */
  int64_t backoff_until = 0;     // Timestamp for error cooldown
  int64_t last_battery_time = 0; // Timestamp for last battery report

  /* Force battery report on first loop (by setting this to 0) */
  bool first_battery_sent = false;

  int loop_counter = 0;
  double peak_x = 0;
  double peak_z = 0;
  bool vibration_detected = false;

  while (true) {
    k_timer_status_sync(&sensor_sample_timer);
    int64_t now = k_uptime_get();

    /* --- PART 1: BATTERY REPORTING (Every 1 Hour) --- */
    /* Logic: If it's been 1 hour OR this is the very first run */
    if (!first_battery_sent ||
        (now - last_battery_time >= BATTERY_REPORT_INTERVAL_MS)) {

      /* Check Backoff first */
      if (now >= backoff_until) {
        uint8_t bat_percent = 0;
#if defined(CONFIG_ADP536X)
        adp536x_fg_soc(&bat_percent);
#endif
        char bat_payload[64];
        snprintf(bat_payload, sizeof(bat_payload), "{\"batp\":%d}",
                 bat_percent);

        LOG_INF("Sending Hourly Battery Report: %d%%", bat_percent);
        int ret = tb_coap_send_telemetry_payload_string("bat", bat_payload);

        if (ret != 0) {
          LOG_ERR("Battery send failed. Backing off...");
          backoff_until = now + ERROR_BACKOFF_TIME_MS;
          tb_coap_force_disconnect();
        } else {
          last_battery_time = now;
          first_battery_sent = true;
        }
      }
    }

    /* --- PART 2: VIBRATION TRACKING (Every 200ms) --- */
    double curr_x, curr_y, curr_z;
    get_vibration_xyz(&curr_x, &curr_y, &curr_z);

    /* Peak Hold Logic */
    if (fabs(curr_x) > fabs(peak_x))
      peak_x = curr_x;
    if (fabs(curr_z) > fabs(peak_z))
      peak_z = curr_z;

    if (fabs(curr_x) > threshold || fabs(curr_z) > threshold) {
      vibration_detected = true;
    }

    loop_counter++;

    /* --- PART 3: VIBRATION UPLOAD (Every 1 Second) --- */
    if (loop_counter >= LOOPS_PER_UPLOAD) {

      if (vibration_detected) {
        if (now >= backoff_until) {

          char vib_payload[128];

          /* NOTE: Removed "batp" from here. Sending ONLY Ax, Az */
          snprintf(vib_payload, sizeof(vib_payload),
                   "{\"ax\":%.2f, \"az\":%.2f}", peak_x, peak_z);

          int ret = tb_coap_send_telemetry_payload_string("data", vib_payload);

          if (ret != 0) {
            LOG_ERR("Vibration send failed (%d). Backing off...", ret);
            backoff_until = now + ERROR_BACKOFF_TIME_MS;
            led_set_connection_status(false);
            tb_coap_force_disconnect();
          } else {
            led_set_connection_status(true);
          }
        }
      }

      /* Reset for next 1-second window */
      loop_counter = 0;
      peak_x = 0;
      peak_z = 0;
      vibration_detected = false;
    }
  }
}