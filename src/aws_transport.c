#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <net/aws_iot.h>
#include <stdio.h>
#include "aws_transport.h"

LOG_MODULE_REGISTER(aws_transport, CONFIG_LOG_DEFAULT_LEVEL);

// UPDATED TOPIC
#define AWS_TOPIC "thingy91/thingy91_XX/data"

static struct k_work_delayable connect_work;
static bool aws_connected = false;

void aws_iot_event_handler(const struct aws_iot_evt *const evt)
{
    switch (evt->type) {
    case AWS_IOT_EVT_CONNECTED:
        LOG_INF("AWS IoT Connected!");
        aws_connected = true;
        break;
    case AWS_IOT_EVT_DISCONNECTED:
        LOG_WRN("AWS IoT Disconnected! Reconnecting in 5s...");
        aws_connected = false;
        k_work_schedule(&connect_work, K_SECONDS(5));
        break;
    case AWS_IOT_EVT_ERROR:
        LOG_ERR("AWS IoT Error: %d", evt->data.err);
        break;
    default:
        break;
    }
}

static void connect_work_fn(struct k_work *work)
{
    LOG_INF("Connecting to AWS IoT...");
    
    struct aws_iot_config config = {0};
    config.client_id = NULL; 
    
    int err = aws_iot_connect(&config);
    if (err) {
        LOG_ERR("AWS Connect failed: %d. Retrying in 5s...", err);
        k_work_schedule(&connect_work, K_SECONDS(5));
    }
}

void start_aws_transport(void)
{
    int err = aws_iot_init(NULL, aws_iot_event_handler);
    if (err) {
        LOG_ERR("AWS Init failed: %d", err);
        return;
    }

    k_work_init_delayable(&connect_work, connect_work_fn);
    k_work_schedule(&connect_work, K_NO_WAIT);
}

int aws_send_telemetry(const char *payload)
{
    if (!aws_connected) {
        return -ENOTCONN;
    }

    struct aws_iot_data tx_data = {
        .ptr = (char *)payload,
        .len = strlen(payload),
        .message_id = 0,
        .qos = MQTT_QOS_0_AT_MOST_ONCE,
        .topic.str = AWS_TOPIC,
        .topic.len = strlen(AWS_TOPIC)
    };
    
    return aws_iot_send(&tx_data);
}

/* Helper to let application.c know if we are ready */
bool is_aws_connected(void)
{
    return aws_connected;
}
