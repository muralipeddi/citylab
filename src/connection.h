/* Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _CONNECTION_H_
#define _CONNECTION_H_

/**
 * @brief Await a connection to an LTE carrier.
 *
 * @param timeout - The time to wait before timing out.
 * @return true if occurred.
 * @return false if timed out.
 */
bool await_lte_connection(k_timeout_t timeout);

/**
 * @brief Await the determination of current date and time by the modem.
 *
 * @param timeout - The time to wait before timing out.
 * @return true if occurred.
 * @return false if timed out.
 */
bool await_date_time_known(k_timeout_t timeout);

/**
 * @brief The connection management thread function.
 * Manages our connection to nRF Cloud, resetting and restablishing as necessary.
 */
void connection_management_thread_fn(void);

#endif /* _CONNECTION_H_ */
