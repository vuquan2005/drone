#include "bsp_pwm.h"
#include "stm32f1xx_hal.h"
#include <assert.h>
#include <stddef.h>

/* Use the real HAL register macros with RAM standing in for TIM4 registers.
 * Start/stop are mocked; this does not model timer update timing or GPIO. */
static TIM_TypeDef timer;
TIM_HandleTypeDef htim4 = {.Instance = &timer};
static unsigned enabled;
static uint32_t fail_channel = UINT32_MAX;
static const uint32_t channels[] = {TIM_CHANNEL_1, TIM_CHANNEL_2, TIM_CHANNEL_3,
                                    TIM_CHANNEL_4};

static void assert_pulses(const uint16_t expected[4]) {
    for (unsigned i = 0; i < 4; ++i) {
        assert(__HAL_TIM_GET_COMPARE(&htim4, channels[i]) == expected[i]);
    }
}

HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *handle,
                                    uint32_t channel) {
    assert(handle == &htim4);
    const uint16_t minimum[] = {1000, 1000, 1000, 1000};
    assert_pulses(minimum);
    assert(timer.CNT == 0);
    assert(timer.EGR == TIM_EGR_UG);
    assert((timer.CCMR1 & (TIM_CCMR1_OC1PE | TIM_CCMR1_OC2PE)) ==
           (TIM_CCMR1_OC1PE | TIM_CCMR1_OC2PE));
    assert((timer.CCMR2 & (TIM_CCMR2_OC3PE | TIM_CCMR2_OC4PE)) ==
           (TIM_CCMR2_OC3PE | TIM_CCMR2_OC4PE));
    if (channel == fail_channel) {
        return HAL_ERROR;
    }
    enabled |= 1U << (channel / 4U);
    return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *handle,
                                   uint32_t channel) {
    assert(handle == &htim4);
    enabled &= ~(1U << (channel / 4U));
    return HAL_OK;
}

int main(void) {
    const uint16_t minimum[] = {1000, 1000, 1000, 1000};
    const uint16_t widths[] = {1000, 1250, 1750, 2000};
    assert(!bsp_pwm_set_pulse_us(1, 1000));
    assert(!bsp_pwm_set_all_us(widths));
    assert(bsp_pwm_init());
    assert(enabled == 15);
    assert_pulses(minimum);

    for (uint8_t channel = 1; channel <= 4; ++channel) {
        assert(bsp_pwm_set_pulse_us(channel, widths[channel - 1]));
    }
    assert_pulses(widths);
    assert(!bsp_pwm_set_pulse_us(0, 1500));
    assert(!bsp_pwm_set_pulse_us(5, 1500));
    assert(!bsp_pwm_set_pulse_us(255, 1500));
    assert(!bsp_pwm_set_pulse_us(1, 999));
    assert(!bsp_pwm_set_pulse_us(1, 2001));
    assert(!bsp_pwm_set_pulse_us(1, UINT16_MAX));
    assert(!bsp_pwm_set_all_us(NULL));
    for (unsigned i = 0; i < 4; ++i) {
        uint16_t invalid[] = {1500, 1500, 1500, 1500};
        invalid[i] = 2001;
        assert(!bsp_pwm_set_all_us(invalid));
        assert_pulses(widths);
    }
    assert_pulses(widths);

    timer.CR1 = TIM_CR1_ARPE;
    assert(bsp_pwm_set_all_us(minimum));
    assert_pulses(minimum);
    assert(timer.CR1 == TIM_CR1_ARPE);
    assert(bsp_pwm_set_all_us(widths));
    assert_pulses(widths);

    timer.CNT = 12345;
    assert(bsp_pwm_init());
    assert(enabled == 15);
    assert_pulses(minimum);
    bsp_pwm_stop();
    bsp_pwm_stop();
    assert(enabled == 0);
    assert(!bsp_pwm_set_pulse_us(1, 1500));
    assert(!bsp_pwm_set_all_us(widths));

    for (unsigned i = 0; i < 4; ++i) {
        fail_channel = channels[i];
        assert(!bsp_pwm_init());
        assert(enabled == 0);
        assert(!bsp_pwm_set_pulse_us(1, 1500));
        assert(!bsp_pwm_set_all_us(widths));
        fail_channel = UINT32_MAX;
        assert(bsp_pwm_init());
        assert(enabled == 15);
    }
    bsp_pwm_stop();
    return 0;
}
