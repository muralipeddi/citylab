

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <math.h>
#include <math.h>

#define WINDOW_SIZE 50        /* 50 samples @ 200 Hz = 0.25 s */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
LOG_MODULE_REGISTER(vibration, LOG_LEVEL_INF);

/* ====== Sampling / Window ====== */
#define SAMPLE_RATE_HZ        200
#define SAMPLE_INTERVAL_MS    (1000 / SAMPLE_RATE_HZ)   /* 5 ms */
#define WINDOW_SIZE           256                        /* ~1.28 s */

/* ====== High-pass filter cutoff (removes gravity/slow tilt) ====== */
#define HPF_CUTOFF_HZ         0.5f

/* ====== Classification thresholds (accel RMS in m/s^2) ======
 * TUNE for your use-case:
 *   SAFE    < 0.5  m/s^2
 *   WARNING < 2.0  m/s^2
 *   DANGER  >= 2.0 m/s^2
 */
#define THRESH_SAFE_MPS2      0.5f
#define THRESH_DANGER_MPS2    2.0f

static const struct device *imu;

/* Simple RMS on a buffer */
static float compute_rms(const float *buf, int n) {
    float s2 = 0.f;
    for (int i = 0; i < n; i++) s2 += buf[i] * buf[i];
    return sqrtf(s2 / (float)n);
}

void main(void)
{
    imu = DEVICE_DT_GET_ONE(bosch_bmi270);
    if (!device_is_ready(imu)) {
        LOG_ERR("BMI270 not ready");
        return;
    }

    /* ---- Configure BMI270 for ~200 Hz, 2 g accel, 500 dps gyro ---- */
    struct sensor_value fs, odr, ovs;

    /* Accelerometer */
    fs.val1 = 2; fs.val2 = 0;                 /* ±2 g range (driver returns m/s^2) */
    odr.val1 = 200; odr.val2 = 0;             /* 200 Hz */
    ovs.val1 = 1; ovs.val2 = 0;               /* Normal mode */
    (void)sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_FULL_SCALE, &fs);
    (void)sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_OVERSAMPLING, &ovs);
    (void)sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);

    /* Gyro */
    fs.val1 = 500; fs.val2 = 0;               /* ±500 dps */
    odr.val1 = 200; odr.val2 = 0;             /* 200 Hz */
    ovs.val1 = 1;  ovs.val2 = 0;
    (void)sensor_attr_set(imu, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_FULL_SCALE, &fs);
    (void)sensor_attr_set(imu, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_OVERSAMPLING, &ovs);
    (void)sensor_attr_set(imu, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);

    LOG_INF("BMI270 configured. Sampling at %d Hz", SAMPLE_RATE_HZ);

    /* ---- High-pass filter state (per-axis) ----
     * y[n] = a * (y[n-1] + x[n] - x[n-1])
     */
    const float Ts   = 1.0f / (float)SAMPLE_RATE_HZ;
    const float tau  = 1.0f / (2.0f * (float)M_PI * HPF_CUTOFF_HZ);
    const float a    = tau / (tau + Ts);

    float ax_prev = 0.f, ay_prev = 0.f, az_prev = 0.f;
    float ax_hp_prev = 0.f, ay_hp_prev = 0.f, az_hp_prev = 0.f;

    struct sensor_value acc[3], gyr[3];

    float a_buf[WINDOW_SIZE];   /* filtered accel magnitude (m/s^2) */
    float g_buf[WINDOW_SIZE];   /* gyro magnitude (dps) */
    int   idx = 0;

    while (1) {
        if (sensor_sample_fetch(imu) != 0) {
            /* No fresh sample yet */
            k_sleep(K_MSEC(SAMPLE_INTERVAL_MS));
            continue;
        }

        sensor_channel_get(imu, SENSOR_CHAN_ACCEL_XYZ, acc);
        sensor_channel_get(imu, SENSOR_CHAN_GYRO_XYZ,  gyr);

        /* Convert to float (Zephyr: val1 + val2 * 1e-6) */
        float ax = (float)acc[0].val1 + (float)acc[0].val2 / 1e6f;  /* m/s^2 */
        float ay = (float)acc[1].val1 + (float)acc[1].val2 / 1e6f;
        float az = (float)acc[2].val1 + (float)acc[2].val2 / 1e6f;

        float gx = (float)gyr[0].val1 + (float)gyr[0].val2 / 1e6f;  /* dps */
        float gy = (float)gyr[1].val1 + (float)gyr[1].val2 / 1e6f;
        float gz = (float)gyr[2].val1 + (float)gyr[2].val2 / 1e6f;

        /* High-pass each accel axis to remove gravity & slow tilt */
        float ax_hp = a * (ax_hp_prev + ax - ax_prev);
        float ay_hp = a * (ay_hp_prev + ay - ay_prev);
        float az_hp = a * (az_hp_prev + az - az_prev);

        ax_prev = ax; ay_prev = ay; az_prev = az;
        ax_hp_prev = ax_hp; ay_hp_prev = ay_hp; az_hp_prev = az_hp;

        /* Magnitudes */
        float a_mag_hp = sqrtf(ax_hp*ax_hp + ay_hp*ay_hp + az_hp*az_hp); /* m/s^2 */
        float g_mag    = sqrtf(gx*gx + gy*gy + gz*gz);                   /* dps */

        a_buf[idx] = a_mag_hp;
        g_buf[idx] = g_mag;
        idx++;

        // if (idx >= WINDOW_SIZE) {
        //     idx = 0;
        //     /* RMS over the window */
        //     float a_rms = compute_rms(a_buf, WINDOW_SIZE);  /* m/s^2 */
        //     float g_rms = compute_rms(g_buf, WINDOW_SIZE);  /* dps   */

        //     const char *status = "SAFE";
        //     if (a_rms >= THRESH_DANGER_MPS2) {
        //         status = "DANGER";
        //     } else if (a_rms >= THRESH_SAFE_MPS2) {
        //         status = "WARNING";
        //     }

        //     /* Print both vibration and rotation context */
        //     //printk("Accel_vib_RMS = %.3f m/s^2, Gyro_RMS = %.2f dps → %s\n", a_rms, g_rms, status);
        //     LOG_INF("Accel_vib_RMS=%.3f m/s^2, Gyro_RMS=%.2f dps, Status=%s",
        //             a_rms, g_rms, status);
        // }
        if (idx >= WINDOW_SIZE) {
            idx = 0;  // optional if you want a circular buffer, else shift manually
        }

        /* Compute RMS on whatever # of samples you have (up to WINDOW_SIZE) */
        float a_rms = compute_rms(a_buf, MIN(idx, WINDOW_SIZE));
        float g_rms = compute_rms(g_buf, MIN(idx, WINDOW_SIZE));

        const char *status = "SAFE";
        if (a_rms >= THRESH_DANGER_MPS2) {
            status = "DANGER";
        } else if (a_rms >= THRESH_SAFE_MPS2) {
            status = "WARNING";
        }

        printk("Accel_vib_RMS = %.3f m/s^2, Gyro_RMS = %.2f dps → %s\n",
            a_rms, g_rms, status);

        k_sleep(K_MSEC(SAMPLE_INTERVAL_MS));
    }
}