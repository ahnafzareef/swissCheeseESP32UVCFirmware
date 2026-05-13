#include "status_led.h"

#include "driver/gpio.h"
#include "sdkconfig.h"

static constexpr gpio_num_t STATUS_LED_GPIO = static_cast<gpio_num_t>(CONFIG_BLINK_GPIO);

void status_led_init()
{
    gpio_reset_pin(STATUS_LED_GPIO);
    gpio_set_direction(STATUS_LED_GPIO, GPIO_MODE_OUTPUT);
}

void status_led_set(uint8_t on)
{
    gpio_set_level(STATUS_LED_GPIO, on);
}

