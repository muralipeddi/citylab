/* Copyright (c) 2022 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <date_time.h>
#include <math.h>
#include <modem/modem_info.h>
#include <stdio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/task_wdt/task_wdt.h>

#if defined(CONFIG_ADP536X)
#include <adp536x.h>
#endif

#include "application.h"
#include "connection.h"
#include "led_control.h"
#include "temperature.h"
#include "vibration.h"

#ifdef CONFIG_CLOUD_THINGSBOARD_COAP
#include "tb_coap.h"
#endif

LOG_MODULE_REGISTER(application, CONFIG_LOG_DEFAULT_LEVEL);

static K_TIMER_DEFINE(sensor_sample_timer, NULL, NULL);

#define POLL_INTERVAL_MS 100 /* Sample every 100ms */
#define BATCH_SIZE 10        /* Collect 10 samples */
#define MIN_SEND_INTERVAL_MS 900
#define MOTION_DIFF_THRESHOLD 0.2
#define HEARTBEAT_INTERVAL_SECONDS 120
#define WDT_TIMEOUT_MS 300000

static int consec_fail_count = 0;
#define MAX_FAILURES_BEFORE_REBOOT 5

struct accel_data {
  int64_t ts;
  double x;
  double y;
  double z;
};

static struct accel_data vibration_buffer[BATCH_SIZE];
static int buffer_index = 0;
static bool motion_detected_in_batch = false;

static uint8_t map_battery_percentage(uint8_t raw_percentage) {
  if (raw_percentage >= 95)
    return 100;
  return (uint8_t)((float)raw_percentage / 95.0f * 100.0f);
}

static bool check_transmission_health(int ret_code) {
  if (ret_code < 0) {
    consec_fail_count++;
    LOG_ERR("CoAP Failed: %d. Count: %d/%d", ret_code, consec_fail_count,
            MAX_FAILURES_BEFORE_REBOOT);

    if (consec_fail_count >= MAX_FAILURES_BEFORE_REBOOT) {
      LOG_ERR("Connection dead. Rebooting...");
      k_sleep(K_SECONDS(1));
      sys_reboot(SYS_REBOOT_COLD);
      return false;
    }
  } else {
    consec_fail_count = 0;
  }
  return true;
}

static void send_heartbeat_data(void) {
  int err;
  int16_t modem_rsrp = 0;
  double temp = 0.0;

  LOG_INF("--- Sending Heartbeat ---");

#if defined(CONFIG_TEMP_TRACKING)
  if (get_temperature(&temp) == 0) {
    LOG_INF("Temperature: %d.%d C", (int)temp, (int)(fabs(temp) * 10) % 10);
#ifdef CONFIG_CLOUD_THINGSBOARD_COAP
    int ret = tb_coap_send_telemetry_double("temp", temp);
    check_transmission_health(ret);
#endif
  }
#endif

  err = modem_info_short_get(MODEM_INFO_RSRP, &modem_rsrp);
  if (err == sizeof(modem_rsrp)) {
    LOG_INF("Signal: %d dBm", modem_rsrp);
#ifdef CONFIG_CLOUD_THINGSBOARD_COAP
    tb_coap_send_telemetry_int("rsrp", modem_rsrp);
#endif
  }

#if defined(CONFIG_ADP536X)
  uint8_t raw_p;
  if (adp536x_fg_soc(&raw_p) == 0) {
    uint8_t mapped_p = map_battery_percentage(raw_p);
    LOG_INF("Battery: %d%% (Raw: %d%%)", mapped_p, raw_p);
#ifdef CONFIG_CLOUD_THINGSBOARD_COAP
    tb_coap_send_telemetry_int("batp", mapped_p);
#endif
  }
  uint16_t volt;
  if (adp536x_fg_volts(&volt) == 0) {
    LOG_INF("Voltage: %d mV", volt);
#ifdef CONFIG_CLOUD_THINGSBOARD_COAP
    tb_coap_send_telemetry_int("batv", volt);
#endif
  }
#endif
  LOG_INF("-------------------------");
}

void main_application_thread_fn(void) {
  int err;
  int wdt_channel_id;

  task_wdt_init(NULL);
  wdt_channel_id = task_wdt_add(WDT_TIMEOUT_MS, NULL, NULL);

  modem_info_init();
  LOG_INF("Connecting to LTE...");
  await_lte_connection(K_FOREVER);
  LOG_INF("Connected.");

  LOG_INF("Waiting for network time...");
  await_date_time_known(K_SECONDS(60));
  LOG_INF("Time acquired.");

  task_wdt_feed(wdt_channel_id);

  start_vibration_tracking();

#ifdef CONFIG_CLOUD_THINGSBOARD_COAP
  start_tb_coap();
#endif

  int64_t last_heartbeat_time_ms = k_uptime_get();
  int64_t last_vibration_send_ms = 0;

  static double prev_x = 0.0;
  static double prev_z = 0.0;
  static bool is_first_run = true;

  send_heartbeat_data();

  while (true) {
    task_wdt_feed(wdt_channel_id);
    k_timer_start(&sensor_sample_timer, K_MSEC(POLL_INTERVAL_MS), K_FOREVER);

    double curr_x, curr_y, curr_z;
    get_vibration_xyz(&curr_x, &curr_y, &curr_z);

    int64_t current_ts = 0;
    date_time_now(&current_ts);

    double diff_x = fabs(curr_x - prev_x);
    double diff_z = fabs(curr_z - prev_z);

    if (!is_first_run &&
        (diff_x > MOTION_DIFF_THRESHOLD || diff_z > MOTION_DIFF_THRESHOLD)) {
      motion_detected_in_batch = true;
    }
    prev_x = curr_x;
    prev_z = curr_z;
    is_first_run = false;

    vibration_buffer[buffer_index].ts = current_ts;
    vibration_buffer[buffer_index].x = curr_x;
    vibration_buffer[buffer_index].y = curr_y;
    vibration_buffer[buffer_index].z = curr_z;
    buffer_index++;

    if (buffer_index >= BATCH_SIZE) {
      if (motion_detected_in_batch) {
        int64_t now = k_uptime_get();
        if ((now - last_vibration_send_ms) > MIN_SEND_INTERVAL_MS) {
          LOG_INF("Sending Batch of 10 samples...");

#ifdef CONFIG_CLOUD_THINGSBOARD_COAP
          char payload_buf[1024];
          int offset = 0;
          offset +=
              snprintf(payload_buf + offset, sizeof(payload_buf) - offset, "[");
          for (int i = 0; i < BATCH_SIZE; i++) {
            offset +=
                snprintf(payload_buf + offset, sizeof(payload_buf) - offset,
                         "{\"ts\":%lld,\"values\":{\"ax\":%.2f,\"ay\":%.2f,"
                         "\"az\":%.2f}}%s",
                         vibration_buffer[i].ts, vibration_buffer[i].x,
                         vibration_buffer[i].y, vibration_buffer[i].z,
                         (i < BATCH_SIZE - 1) ? "," : "");
          }
          offset +=
              snprintf(payload_buf + offset, sizeof(payload_buf) - offset, "]");

          int ret = tb_coap_send_telemetry_payload_string("accel", payload_buf);
          check_transmission_health(ret);
#endif
          last_vibration_send_ms = now;
        } else {
          LOG_WRN("Rate limit skipped send.");
        }
      }
      buffer_index = 0;
      motion_detected_in_batch = false;
    }

    int64_t current_time_ms = k_uptime_get();
    if ((current_time_ms - last_heartbeat_time_ms) >=
        (HEARTBEAT_INTERVAL_SECONDS * 1000LL)) {
      send_heartbeat_data();
      last_heartbeat_time_ms = current_time_ms;
    }

    k_timer_status_sync(&sensor_sample_timer);
  }
}