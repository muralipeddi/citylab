#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <date_time.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <modem/modem_info.h>

#if defined(CONFIG_ADP536X)
#include <adp536x.h>
#endif

#include "application.h"
#include "connection.h"
#include "led_control.h"
#include "vibration.h"
#include "aws_transport.h" 

LOG_MODULE_REGISTER(application, CONFIG_LOG_DEFAULT_LEVEL);

static K_TIMER_DEFINE(sensor_sample_timer, NULL, NULL);

/* --- CONFIGURATION --- */
#define SAMPLING_RATE_MS 100   
#define BATCH_SIZE       500   
#define VIB_THRESHOLD    0.0   
#define HEARTBEAT_INTERVAL_MS 60000
#define ERROR_BACKOFF_TIME_MS 5000
#define PACKET_OVERHEAD_BYTES 350 
#define JSON_BUFFER_SIZE 12288

struct VibrationData {
    float ax;
    float az;
};

static struct VibrationData batch_buffer[BATCH_SIZE];
static int batch_index = 0;
static int64_t batch_start_time = 0;
static int64_t total_bytes_sent = 0;
static char huge_payload[JSON_BUFFER_SIZE];

void main_application_thread_fn(void)
{
    int err = modem_info_init();
    if (err) LOG_ERR("Modem init error: %d", err);

    LOG_INF("Waiting for LTE...");
    (void)await_lte_connection(K_FOREVER);
    
    LOG_INF("Waiting for Time...");
    await_date_time_known(K_SECONDS(20));

    start_vibration_tracking();
    start_aws_transport(); 

    while (!is_aws_connected()) {
        k_sleep(K_SECONDS(1));
    }
    LOG_INF("AWS Connected!");
    led_set_connection_status(true);

    k_timer_start(&sensor_sample_timer, K_MSEC(SAMPLING_RATE_MS), K_MSEC(SAMPLING_RATE_MS));

    int64_t last_heartbeat = 0;
    int64_t backoff_until = 0;
    double peak_x = 0, peak_z = 0;
    bool significant_motion = false;

    while (true) {
        k_timer_status_sync(&sensor_sample_timer);
        int64_t now = k_uptime_get();

        /* --- 1. HEARTBEAT WITH BATTERY MAPPING --- */
        if (now - last_heartbeat >= HEARTBEAT_INTERVAL_MS) {
            if (is_aws_connected()) {
                uint8_t raw_bat = 0;
#if defined(CONFIG_ADP536X)
                adp536x_fg_soc(&raw_bat);
#endif
                /* Map 94% to 100% */
                int mapped_bat = (int)(raw_bat * 1.064);
                if (mapped_bat > 100) mapped_bat = 100;

                int64_t hb_time = 0;
                date_time_now(&hb_time);

                char beat_payload[128];
                /* FIX: Cast timestamp to (long) and use %ld to avoid "ld" error */
                snprintf(beat_payload, sizeof(beat_payload), 
                         "{\"t\":%ld,\"type\":\"beat\",\"b\":%d}", 
                         (long)(hb_time / 1000), mapped_bat);
                
                aws_send_telemetry(beat_payload);
                total_bytes_sent += (strlen(beat_payload) + PACKET_OVERHEAD_BYTES);
                LOG_INF("Heartbeat: %d%% (Raw: %d%%). Total: %lld KB", 
                        mapped_bat, raw_bat, total_bytes_sent / 1024);
                
                last_heartbeat = now;
            }
        }

        /* --- 2. VIBRATION READ --- */
        double curr_x, curr_y, curr_z;
        get_vibration_xyz(&curr_x, &curr_y, &curr_z);
        if (fabs(curr_x) > fabs(peak_x)) peak_x = curr_x;
        if (fabs(curr_z) > fabs(peak_z)) peak_z = curr_z;
        if (fabs(curr_x) > VIB_THRESHOLD || fabs(curr_z) > VIB_THRESHOLD) significant_motion = true;

        /* --- 3. STORE --- */
        if (batch_index == 0) date_time_now(&batch_start_time);
        batch_buffer[batch_index].ax = (float)peak_x;
        batch_buffer[batch_index].az = (float)peak_z;
        batch_index++;
        peak_x = 0; peak_z = 0; 

        /* --- 4. OPTIMIZED UPLOAD --- */
        if (batch_index >= BATCH_SIZE) {
            if (significant_motion && (now >= backoff_until) && is_aws_connected()) {
                int offset = 0;
                
                /* FIX: Cast to (long) and use %ld. 
                   Old %lld caused 'ld' text to appear in JSON */
                offset += snprintf(huge_payload + offset, JSON_BUFFER_SIZE - offset, 
                                   "{\"t\":%ld,\"x\":[", (long)(batch_start_time / 1000));
                
                for (int i = 0; i < BATCH_SIZE; i++) {
                    offset += snprintf(huge_payload + offset, JSON_BUFFER_SIZE - offset, "%.2f", batch_buffer[i].ax);
                    if (i < BATCH_SIZE - 1) huge_payload[offset++] = ',';
                }
                
                offset += snprintf(huge_payload + offset, JSON_BUFFER_SIZE - offset, "],\"z\":[");
                
                for (int i = 0; i < BATCH_SIZE; i++) {
                    offset += snprintf(huge_payload + offset, JSON_BUFFER_SIZE - offset, "%.2f", batch_buffer[i].az);
                    if (i < BATCH_SIZE - 1) huge_payload[offset++] = ',';
                }
                
                offset += snprintf(huge_payload + offset, JSON_BUFFER_SIZE - offset, "]}");

                if (aws_send_telemetry(huge_payload) != 0) {
                    backoff_until = now + ERROR_BACKOFF_TIME_MS;
                } else {
                    total_bytes_sent += (offset + PACKET_OVERHEAD_BYTES);
                    LOG_INF("Upload Success. Total Session: %lld KB", total_bytes_sent / 1024);
                }
            }
            batch_index = 0;
            significant_motion = false;
        }
    }
}
