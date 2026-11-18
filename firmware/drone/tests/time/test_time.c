#include "bsp_time.h"
#include "stm32f1xx_hal.h"
#include <assert.h>
#include <stdio.h>

CoreDebug_Type mock_core_debug;
DWT_Type mock_dwt;
SCB_Type mock_scb;
SysTick_Type mock_systick;
uint32_t SystemCoreClock = 72000000U;
static uint32_t tick_ms;
static unsigned calls;
static int simulate_boundary;

uint32_t HAL_GetTick(void) {
    calls++;
    if (simulate_boundary && calls == 2U) {
        tick_ms++;
        mock_systick.VAL = mock_systick.LOAD;
        mock_scb.ICSR = 0U;
    }
    return tick_ms;
}

int main(void) {
    mock_systick.LOAD = 71999U;
    mock_systick.VAL = 71999U - 72U * 123U;
    tick_ms = 10U;
    assert(bsp_time_now_us() == 10123U);
    mock_systick.VAL = 0U;
    assert(bsp_time_now_us() == 10999U);
    /* Roll between the two HAL reads: retry rather than combining two ticks. */
    calls = 0;
    simulate_boundary = 1;
    assert(bsp_time_now_us() == 11000U && calls == 4U);
    /* Pending SysTick must be serviced before using its new countdown. */
    mock_scb.ICSR = SCB_ICSR_PENDSTSET_Msk;
    calls = 0;
    assert(bsp_time_now_us() == 12000U && calls == 4U);
    simulate_boundary = 0;
    tick_ms = 4294967U;
    mock_systick.VAL = 71999U - 72U * 295U;
    assert(bsp_time_now_us() == UINT32_MAX);
    mock_systick.VAL -= 72U;
    assert(bsp_time_now_us() == 0U);
    tick_ms = UINT32_MAX;
    mock_systick.VAL = mock_systick.LOAD;
    uint32_t before = bsp_time_now_us();
    tick_ms = 0U;
    assert((uint32_t)(bsp_time_now_us() - before) == 1000U);
    mock_dwt.CYCCNT = 1234U;
    bsp_time_init();
    assert(mock_dwt.CYCCNT == 1234U);
    assert(mock_dwt.CTRL & DWT_CTRL_CYCCNTENA_Msk);
    puts("time tests passed");
    return 0;
}
