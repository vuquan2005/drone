#include "bsp_status_led.h"
#include "main.h"

void bsp_status_led_write(bool on) {
    HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
