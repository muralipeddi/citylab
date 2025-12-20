/* vibration.h */

#ifndef _VIBRATION_H_
#define _VIBRATION_H_

#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>

/**
 * @brief Initializes the accelerometer and starts the trigger-based sampling.
 */
void start_vibration_tracking(void);

/**
 * @brief Gets the latest raw accelerometer data.
 *
 * @param[out] x Pointer to be filled with the X-axis value (in m/s^2).
 * @param[out] y Pointer to be filled with the Y-axis value (in m/s^2).
 * @param[out] z Pointer to be filled with the Z-axis value (in m/s^2).
 */
void get_vibration_xyz(double *x, double *y, double *z);

#endif /* _VIBRATION_H_ */