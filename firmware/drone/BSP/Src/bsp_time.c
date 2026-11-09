#include "bsp_time.h"
#include "stm32f1xx_hal.h"

void bsp_time_init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();
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
