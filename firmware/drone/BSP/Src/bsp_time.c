#include "bsp_time.h"
#include "stm32f1xx_hal.h"

void bsp_time_init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();
}

uint32_t bsp_time_now_us(void) {
    uint32_t ms;
    uint32_t value;
    uint32_t pending;
    uint32_t after;
    do {
        ms = HAL_GetTick();
        value = SysTick->VAL;
        pending = SCB->ICSR & SCB_ICSR_PENDSTSET_Msk;
        after = HAL_GetTick();
    } while (ms != after || pending != 0U);
    const uint32_t cycles_per_us = SystemCoreClock / 1000000U;
    return ms * 1000U + (SysTick->LOAD - value) / cycles_per_us;
}

void bsp_time_delay_us(uint32_t period_us) {
    /* Short chunks keep multiplication and elapsed-counter arithmetic bounded.
     * Ceiling division avoids rounding a microsecond down on other clocks. */
    const uint32_t cycles_per_us = (SystemCoreClock + 999999U) / 1000000U;
    while (period_us != 0U) {
        const uint32_t chunk = period_us > 1000U ? 1000U : period_us;
        const uint32_t cycles = chunk * cycles_per_us;
        const uint32_t start = DWT->CYCCNT;
        while ((uint32_t)(DWT->CYCCNT - start) < cycles) {
            __NOP();
        }
        period_us -= chunk;
    }
}
