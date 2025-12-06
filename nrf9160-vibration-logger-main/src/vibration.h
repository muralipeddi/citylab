/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _VIBRATION_H_
#define _VIBRATION_H_

/**
 * @brief Start the accelerometer sensor and data trigger.
 */
void start_vibration_tracking(void);

/**
 * @brief Get the latest calibrated and smoothed accelerometer data.
 *
 * @param[out] x Pointer to store the final calibrated X-axis value.
 * @param[out] y Pointer to store the final calibrated Y-axis value.
 * @param[out] z Pointer to store the final calibrated Z-axis value.
 */
void get_vibration_xyz(double *x, double *y, double *z);

#endif /* _VIBRATION_H_ */