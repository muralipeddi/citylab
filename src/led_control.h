#ifndef LED_CONTROL_H_
#define LED_CONTROL_H_

#include <stdbool.h>

/**
 * @brief Initialize the LED device.
 */
void led_init(void);

/**
 * @brief Set the connection status LED.
 * * @param connected
 * false = Not Connected (RED LED ON)
 * true  = Connected (LED OFF - Save Battery)
 */
void led_set_connection_status(bool connected);

#endif /* LED_CONTROL_H_ */
