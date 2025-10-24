#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <stdlib.h>
#include <string.h> // We need this for memcpy

#include "vibration.h"

#define ACCELEROMETER_CHANNELS 3

LOG_MODULE_REGISTER(vibration, CONFIG_LOG_DEFAULT_LEVEL);

static const struct device *accel_sensor = DEVICE_DT_GET(DT_ALIAS(accelerometer));

/* Static variable to hold the latest sensor reading.
 * Protected by the trigger handler context, so no mutex needed
 * if we just do a simple copy.
 */
static double latest_sample[ACCELEROMETER_CHANNELS] = {0, 0, 0};

static struct sensor_trigger accel_sensor_trigger = {
    .chan = SENSOR_CHAN_ACCEL_XYZ,
    .type = SENSOR_TRIG_DATA_READY
};

static void accel_trigger_handler(const struct device *dev,
                   const struct sensor_trigger *trig)
{
    struct sensor_value data[ACCELEROMETER_CHANNELS];
    int err;

    if (sensor_sample_fetch(dev) < 0) {
        LOG_ERR("Sample fetch error");
        return;
    }

    err = sensor_channel_get(dev, SENSOR_CHAN_ACCEL_XYZ, data);
    if (err) {
        LOG_ERR("sensor_channel_get, error: %d", err);
        return;
    }

    /* Convert and store the latest X, Y, Z values */
    latest_sample[0] = sensor_value_to_double(&data[0]);
    latest_sample[1] = sensor_value_to_double(&data[1]);
    latest_sample[2] = sensor_value_to_double(&data[2]);
}

void start_vibration_tracking(void)
{
    if (!device_is_ready(accel_sensor)) {
        LOG_ERR("Accelerometer device is not ready");
    } else {
        int err = sensor_trigger_set(accel_sensor, &accel_sensor_trigger, accel_trigger_handler);
        if (err) {
            LOG_ERR("Could not set trigger for device %s, error: %d",
                accel_sensor->name, err);
        }
    }
}

/**
 * @brief Get the latest raw accelerometer data.
 */
void get_vibration_xyz(double *x, double *y, double *z)
{
    /* Copy the latest stored values */
    *x = latest_sample[0];
    *y = latest_sample[1];
    *z = latest_sample[2];
}