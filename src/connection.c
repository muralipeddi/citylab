#include <date_time.h>
#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

#include "connection.h"
#include "led_control.h" // Uses new API

LOG_MODULE_REGISTER(connection, CONFIG_LOG_DEFAULT_LEVEL);

/* Flow control event identifiers */
#define LTE_CONNECTED (1 << 1)
#define LTE_DISCONNECTED (1 << 2)
#define DATE_TIME_KNOWN (1 << 1)

static K_EVENT_DEFINE(lte_connection_events);
static K_EVENT_DEFINE(datetime_connection_events);

/* ... (Keep notify_lte_connected, clear_lte_connected, await_lte_connection,
 * etc. as they were) ... */
/* NOTE: I am abbreviating helper functions here to save space.
   Keep your existing 'static void notify_lte_connected',
   'await_lte_connection', etc. They do not change.
*/
static void notify_lte_connected(void) {
  k_event_post(&lte_connection_events, LTE_CONNECTED);
}
static void clear_lte_connected(void) {
  k_event_set(&lte_connection_events, 0);
}
bool await_lte_connection(k_timeout_t timeout) {
  return k_event_wait_all(&lte_connection_events, LTE_CONNECTED, false,
                          timeout) != 0;
}
static void notify_date_time_known(void) {
  k_event_post(&datetime_connection_events, DATE_TIME_KNOWN);
}
bool await_date_time_known(k_timeout_t timeout) {
  return k_event_wait(&datetime_connection_events, DATE_TIME_KNOWN, false,
                      timeout) != 0;
}
static void date_time_evt_handler(const struct date_time_evt *date_time_evt) {
  if (date_time_is_valid()) {
    notify_date_time_known();
  }
}

/* ... (Keep lte_event_handler and setup_modem as they were) ... */
static void lte_event_handler(const struct lte_lc_evt *const evt) {
  /* Copy your existing lte_event_handler logic here */
  switch (evt->type) {
  case LTE_LC_EVT_NW_REG_STATUS:
    if ((evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
        (evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
      clear_lte_connected();
      k_event_post(&lte_connection_events, LTE_DISCONNECTED);

      /* LINK DROP: Turn LED RED */
      led_set_connection_status(false);
    } else {
      notify_lte_connected();
    }
    break;
  default:
    break;
  }
}

static int setup_modem(void) {
  int ret = nrf_modem_lib_init();
  if (ret == 0)
    date_time_register_handler(date_time_evt_handler);
  return ret;
}

static int setup_lte(void) {
  /* Copy your existing setup_lte logic here */
  if (IS_ENABLED(CONFIG_POWER_SAVING_MODE_ENABLE))
    lte_lc_psm_req(true);
  lte_lc_modem_events_enable();
  return lte_lc_init_and_connect_async(lte_event_handler);
}

void connection_management_thread_fn(void) {
  /* STARTUP: Turn LED RED */
  led_set_connection_status(false);

  LOG_INF("Setting up modem...");
  if (setup_modem()) {
    LOG_ERR("Modem setup failed");
    return;
  }

  LOG_INF("Setting up LTE...");
  if (setup_lte()) {
    LOG_ERR("LTE setup failed");
    return;
  }

  LOG_INF("Connecting to LTE network...");
  while (true) {
    /* Wait for connection */
    (void)await_lte_connection(K_FOREVER);

    /* NOTE: We do NOT turn the LED off here.
     * We wait for application.c to confirm everything (Cloud + Time) is ready.
     */

    k_event_set_masked(&lte_connection_events, 0, LTE_DISCONNECTED);
    (void)k_event_wait(&lte_connection_events, LTE_DISCONNECTED, false,
                       K_FOREVER);

    LOG_INF("Disconnected. Re-connecting...");
    /* RETRYING: Turn LED RED */
    led_set_connection_status(false);
  }
}
