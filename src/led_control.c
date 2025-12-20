#include "led_control.h"
#include <zephyr/drivers/led.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led_control, CONFIG_LOG_DEFAULT_LEVEL);

/* Auto-detect LED driver (PWM or GPIO) */
#if defined(CONFIG_LED_INDICATION_PWM)
const static struct device *led_device = DEVICE_DT_GET_ANY(pwm_leds);
#else
const static struct device *led_device = DEVICE_DT_GET_ANY(gpio_leds);
#endif

/* RGB Channels on Thingy:91 */
#define LED_RED 0
#define LED_GRN 1
#define LED_BLU 2

void led_init(void) {
  if (!device_is_ready(led_device)) {
    LOG_ERR("LED device not ready");
    return;
  }
  /* Default to Not Connected (RED) on startup */
  led_set_connection_status(false);
}

void led_set_connection_status(bool connected) {
  if (!device_is_ready(led_device)) {
    return;
  }

  if (connected) {
    /* CONNECTED: Turn EVERYTHING OFF to save battery */
    led_off(led_device, LED_RED);
    led_off(led_device, LED_GRN);
    led_off(led_device, LED_BLU);
  } else {
    /* NOT CONNECTED: Turn RED ON */
    led_off(led_device, LED_GRN);
    led_off(led_device, LED_BLU);

    /* Set Red. If PWM, use brightness 20% to save even more power while still
     * visible */
#if defined(CONFIG_LED_INDICATION_PWM)
    led_set_brightness(led_device, LED_RED, 20);
#else
    led_on(led_device, LED_RED);
#endif
  }
}
