#ifndef TIME_MOCK_HAL_H
#define TIME_MOCK_HAL_H
#include <stdint.h>
typedef struct {
    volatile uint32_t DEMCR;
} CoreDebug_Type;
typedef struct {
    volatile uint32_t CTRL, CYCCNT;
} DWT_Type;
typedef struct {
    volatile uint32_t ICSR;
} SCB_Type;
typedef struct {
    volatile uint32_t CTRL, LOAD, VAL;
} SysTick_Type;
extern CoreDebug_Type mock_core_debug;
extern DWT_Type mock_dwt;
extern SCB_Type mock_scb;
extern SysTick_Type mock_systick;
extern uint32_t SystemCoreClock;
#define CoreDebug (&mock_core_debug)
#define DWT (&mock_dwt)
#define SCB (&mock_scb)
#define SysTick (&mock_systick)
#define CoreDebug_DEMCR_TRCENA_Msk (1UL << 24)
#define DWT_CTRL_CYCCNTENA_Msk 1UL
#define SCB_ICSR_PENDSTSET_Msk (1UL << 26)
#define __DSB() ((void)0)
#define __ISB() ((void)0)
#define __NOP() ((void)0)
uint32_t HAL_GetTick(void);
#endif
