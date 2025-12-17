/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <math.h>
#include <stdlib.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "vibration.h"

#define ACCELEROMETER_CHANNELS 3

/* EMA FILTER (0.1)
 * Smoothing factor to remove high-frequency noise.
 */
#define EMA_ALPHA 0.1

/*
 * --- CALIBRATION VALUES ---
 * Based on your specific mounting position.
 */
#define CAL_OFFSET_X (0.37)
#define CAL_GRAVITY_Y (9.0)
#define CAL_OFFSET_Z (-0.74)

#define REAL_GRAVITY (9.80665)

LOG_MODULE_REGISTER(vibration, CONFIG_LOG_DEFAULT_LEVEL);

static const struct device *accel_sensor =
    DEVICE_DT_GET(DT_ALIAS(accelerometer));
static double filtered_sample[ACCELEROMETER_CHANNELS] = {0};

static struct sensor_trigger accel_sensor_trigger = {
    .chan = SENSOR_CHAN_ACCEL_XYZ, .type = SENSOR_TRIG_DATA_READY};

static void accel_trigger_handler(const struct device *dev,
                                  const struct sensor_trigger *trig) {
  struct sensor_value data[ACCELEROMETER_CHANNELS];

  if (sensor_sample_fetch(dev) < 0)
    return;
  if (sensor_channel_get(dev, SENSOR_CHAN_ACCEL_XYZ, data) < 0)
    return;

  double raw_x = sensor_value_to_double(&data[0]);
  double raw_y = sensor_value_to_double(&data[1]);
  double raw_z = sensor_value_to_double(&data[2]);

  /* Apply EMA Smoothing */
  /* Initialize if first run */
  if (filtered_sample[0] == 0.0 && filtered_sample[1] == 0.0 &&
      filtered_sample[2] == 0.0) {
    filtered_sample[0] = raw_x;
    filtered_sample[1] = raw_y;
    filtered_sample[2] = raw_z;
  } else {
    filtered_sample[0] =
        (EMA_ALPHA * raw_x) + (1.0 - EMA_ALPHA) * filtered_sample[0];
    filtered_sample[1] =
        (EMA_ALPHA * raw_y) + (1.0 - EMA_ALPHA) * filtered_sample[1];
    filtered_sample[2] =
        (EMA_ALPHA * raw_z) + (1.0 - EMA_ALPHA) * filtered_sample[2];
  }
}

void start_vibration_tracking(void) {
  if (device_is_ready(accel_sensor)) {
    sensor_trigger_set(accel_sensor, &accel_sensor_trigger,
                       accel_trigger_handler);
  } else {
    LOG_ERR("Accelerometer device not ready");
  }
}

void get_vibration_xyz(double *x, double *y, double *z) {
  double raw_x = filtered_sample[0];
  double raw_y = filtered_sample[1];
  double raw_z = filtered_sample[2];

  /* Apply Calibration: Subtract static offsets */
  *x = raw_x - CAL_OFFSET_X;
  *z = raw_z - CAL_OFFSET_Z;

  /* Normalize Gravity on Y (Optional) */
  *y = (raw_y / CAL_GRAVITY_Y) * REAL_GRAVITY;
}