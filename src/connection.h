#ifndef CONNECTION_H_
#define CONNECTION_H_

#include <stdbool.h>       // Required for bool
#include <zephyr/kernel.h> // Required for k_timeout_t

/**
 * @brief Thread entry point for connection management.
 */
void connection_management_thread_fn(void);

/**
 * @brief Wait for the LTE link to be established.
 * * @param timeout How long to wait.
 * @return true if connected, false if timed out.
 */
bool await_lte_connection(k_timeout_t timeout);

/**
 * @brief Wait for the modem to resolve the current date and time.
 * * @param timeout How long to wait.
 * @return true if time is known, false if timed out.
 */
bool await_date_time_known(k_timeout_t timeout);

#endif /* CONNECTION_H_ */
