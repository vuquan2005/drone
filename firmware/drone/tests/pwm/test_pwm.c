#include "app_motor.h"
#include "bsp_motor_pwm.h"
#include "mixer.h"
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
    /* Expected register order is TIM4 CH1..CH4, independent of BSP mapping. */
    const uint16_t channel_widths[] = {1750, 2000, 1000, 1250};
    assert(!bsp_motor_pwm_set_pulse_us(1, 1000));
    assert(!bsp_motor_pwm_set_all_us(widths));
    assert(bsp_motor_pwm_init());
    assert(enabled == 15);
    assert_pulses(minimum);

    for (uint8_t motor = 1; motor <= 4; ++motor) {
        assert(bsp_motor_pwm_set_pulse_us(motor, widths[motor - 1]));
    }
    assert_pulses(channel_widths);
    assert(!bsp_motor_pwm_set_pulse_us(0, 1500));
    assert(!bsp_motor_pwm_set_pulse_us(5, 1500));
    assert(!bsp_motor_pwm_set_pulse_us(255, 1500));
    assert(!bsp_motor_pwm_set_pulse_us(1, 999));
    assert(!bsp_motor_pwm_set_pulse_us(1, 2001));
    assert(!bsp_motor_pwm_set_pulse_us(1, UINT16_MAX));
    assert(!bsp_motor_pwm_set_all_us(NULL));
    for (unsigned i = 0; i < 4; ++i) {
        uint16_t invalid[] = {1500, 1500, 1500, 1500};
        invalid[i] = 2001;
        assert(!bsp_motor_pwm_set_all_us(invalid));
        assert_pulses(channel_widths);
    }
    assert_pulses(channel_widths);

    timer.CR1 = TIM_CR1_ARPE;
    assert(bsp_motor_pwm_set_all_us(minimum));
    assert_pulses(minimum);
    assert(timer.CR1 == TIM_CR1_ARPE);
    assert(bsp_motor_pwm_set_all_us(widths));
    assert_pulses(channel_widths);

    timer.CNT = 12345;
    assert(bsp_motor_pwm_init());
    assert(enabled == 15);
    assert_pulses(minimum);
    bsp_motor_pwm_stop();
    bsp_motor_pwm_stop();
    assert(enabled == 0);
    assert(!bsp_motor_pwm_set_pulse_us(1, 1500));
    assert(!bsp_motor_pwm_set_all_us(widths));

    for (unsigned i = 0; i < 4; ++i) {
        fail_channel = channels[i];
        assert(!bsp_motor_pwm_init());
        assert(enabled == 0);
        assert(!bsp_motor_pwm_set_pulse_us(1, 1500));
        assert(!bsp_motor_pwm_set_all_us(widths));
        fail_channel = UINT32_MAX;
        assert(bsp_motor_pwm_init());
        assert(enabled == 15);
    }
    bsp_motor_pwm_stop();
    const uint16_t motor_widths[] = {1750, 2000, 1000, 1250};
    const norm_q15_t commands[] = {0, 8192, 24576, NORM_Q15_ONE};
    assert(!app_motor_set_all(commands));
    assert(app_motor_init());
    assert_pulses(minimum);
    assert(app_motor_set_all(commands));
    assert_pulses(motor_widths);
    assert(!app_motor_set_all(NULL));
    const norm_q15_t invalid_values[] = {-1, NORM_Q15_ONE + 1, INT32_MIN,
                                         INT32_MAX};
    for (unsigned i = 0; i < APP_MOTOR_COUNT; ++i) {
        for (unsigned j = 0;
             j < sizeof(invalid_values) / sizeof(invalid_values[0]); ++j) {
            norm_q15_t invalid[] = {16384, 16384, 16384, 16384};
            invalid[i] = invalid_values[j];
            assert(!app_motor_set_all(invalid));
            assert_pulses(motor_widths);
        }
    }
    const norm_q15_t fractional[] = {16397, 16404, 0, NORM_Q15_ONE};
    const uint16_t rounded[] = {1000, 2000, 1500, 1501};
    assert(app_motor_set_all(fractional));
    assert_pulses(rounded);
    /* Every representable ESC command stays within half a timer tick of the
     * ideal linear map; endpoint coverage also catches accidental narrowing. */
    for (norm_q15_t q = 0; q <= NORM_Q15_ONE; ++q) {
        const norm_q15_t frame[] = {q, q, q, q};
        assert(app_motor_set_all(frame));
        for (unsigned i = 0; i < 4; ++i) {
            const int32_t pulse =
                (int32_t)__HAL_TIM_GET_COMPARE(&htim4, channels[i]);
            const int32_t error = (pulse - 1000) * NORM_Q15_ONE - q * 1000;
            assert(error >= -NORM_Q15_ONE / 2 && error <= NORM_Q15_ONE / 2);
        }
    }
    const norm_q15_t ties[] = {2047, 2048, 2049, NORM_Q15_ONE};
    const uint16_t ties_expected[] = {1063, 2000, 1062, 1063};
    assert(app_motor_set_all(ties));
    assert_pulses(ties_expected);
    /* Verify the mixer motor order reaches the wired TIM4 channels.
     * Identity thrust-to-ESC calibration remains a test assumption. */
    const mixer_command_t request = {0.5f, 0.5f, 0, 0};
    mixer_result_t mixed;
    assert(mixer_quad_x(&request, &mixed));
    assert(app_motor_set_all(mixed.motor));
    const uint16_t mixed_widths[] = {1750, 1750, 1250, 1250};
    assert_pulses(mixed_widths);
    assert(app_motor_init());
    assert_pulses(minimum);
    app_motor_stop();
    assert(enabled == 0);
    assert(!app_motor_set_all(commands));
    fail_channel = TIM_CHANNEL_3;
    assert(!app_motor_init());
    assert(enabled == 0);
    assert(!app_motor_set_all(commands));
    return 0;
}
