#include "application.h"
#include "connection.h"
#include "led_control.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* Main Application Thread */
K_THREAD_DEFINE(app_thread, CONFIG_APPLICATION_THREAD_STACK_SIZE,
                main_application_thread_fn, NULL, NULL, NULL, 0, 0, 0);

/* Connection Management Thread (-1 Priority = Highest/Pre-emptive) */
K_THREAD_DEFINE(con_thread, CONFIG_CONNECTION_THREAD_STACK_SIZE,
                connection_management_thread_fn, NULL, NULL, NULL, -1, 0, 0);

int main(void) {
  /* Init LED hardware immediately */
  led_init();

  LOG_INF("nRF Cloud MQTT multi-service sample has started.");
  return 0;
}
