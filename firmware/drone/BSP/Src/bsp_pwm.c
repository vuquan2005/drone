#include "bsp_pwm.h"
#include "stm32f1xx_hal.h"
#include <stddef.h>

/* Owned and initialized by CubeMX before app_init(). */
extern TIM_HandleTypeDef htim4;

static const uint32_t channels[BSP_PWM_CHANNEL_COUNT] = {
    TIM_CHANNEL_1, TIM_CHANNEL_2, TIM_CHANNEL_3, TIM_CHANNEL_4};
static bool running;

static bool pulse_valid(uint16_t pulse_us) {
    return pulse_us >= BSP_PWM_MIN_PULSE_US && pulse_us <= BSP_PWM_MAX_PULSE_US;
}

void bsp_pwm_stop(void) {
    running = false;
    for (uint32_t i = 0; i < BSP_PWM_CHANNEL_COUNT; ++i) {
        (void)HAL_TIM_PWM_Stop(&htim4, channels[i]);
    }
}

bool bsp_pwm_init(void) {
    bsp_pwm_stop();
    for (uint32_t i = 0; i < BSP_PWM_CHANNEL_COUNT; ++i) {
        __HAL_TIM_ENABLE_OCxPRELOAD(&htim4, channels[i]);
        __HAL_TIM_SET_COMPARE(&htim4, channels[i], BSP_PWM_MIN_PULSE_US);
    }
    /* Load the preload registers before enabling any output, including restart
     * after a previous non-minimum command. UG also resets the prescaler. */
    __HAL_TIM_SET_COUNTER(&htim4, 0U);
    htim4.Instance->EGR = TIM_EGR_UG;
    __HAL_TIM_CLEAR_FLAG(&htim4, (uint32_t)TIM_FLAG_UPDATE);
    for (uint32_t i = 0; i < BSP_PWM_CHANNEL_COUNT; ++i) {
        if (HAL_TIM_PWM_Start(&htim4, channels[i]) != HAL_OK) {
            bsp_pwm_stop();
            return false;
        }
    }
    running = true;
    return true;
}

bool bsp_pwm_set_pulse_us(uint8_t channel, uint16_t pulse_us) {
    if (!running || channel == 0U || channel > BSP_PWM_CHANNEL_COUNT ||
        !pulse_valid(pulse_us)) {
        return false;
    }
    __HAL_TIM_SET_COMPARE(&htim4, channels[channel - 1U], pulse_us);
    return true;
}

bool bsp_pwm_set_all_us(const uint16_t pulse_us[BSP_PWM_CHANNEL_COUNT]) {
    if (!running || pulse_us == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < BSP_PWM_CHANNEL_COUNT; ++i) {
        if (!pulse_valid(pulse_us[i])) {
            return false;
        }
    }
    /* Keep a timer overflow from latching only part of the new frame. */
    SET_BIT(htim4.Instance->CR1, TIM_CR1_UDIS);
    for (uint32_t i = 0; i < BSP_PWM_CHANNEL_COUNT; ++i) {
        __HAL_TIM_SET_COMPARE(&htim4, channels[i], pulse_us[i]);
    }
    CLEAR_BIT(htim4.Instance->CR1, TIM_CR1_UDIS);
    return true;
}
