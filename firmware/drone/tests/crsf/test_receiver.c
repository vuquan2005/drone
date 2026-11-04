#include "app_receiver.h"
#include "bsp_receiver.h"
#include "main.h"
#include <assert.h>
#include <stdio.h>

UART_HandleTypeDef huart1;
static uint8_t *rx_target;
static uint32_t tick;
static uint32_t mask;
static HAL_StatusTypeDef next_result;
static unsigned aborts;
static const uint8_t frame[] = {0xc8, 0x18, 0x16, 0xe0, 0x03, 0x1f, 0xf8,
                                0xc0, 0x07, 0x3e, 0xf0, 0x81, 0x0f, 0x7c,
                                0xe0, 0x03, 0x1f, 0xf8, 0xc0, 0x07, 0x3e,
                                0xf0, 0x81, 0x0f, 0x7c, 0xad};
uint32_t HAL_GetTick(void) {
    return tick;
}
uint32_t __get_PRIMASK(void) {
    return mask;
}
void __disable_irq(void) {
    mask = 1;
}
void __set_PRIMASK(uint32_t value) {
    mask = value;
}
void HAL_NVIC_SetPriority(int irq, int priority, int subpriority) {
    assert(irq == USART1_IRQn && priority == 5 && subpriority == 0);
}
void HAL_NVIC_EnableIRQ(int irq) {
    assert(irq == USART1_IRQn);
}
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *uart, uint8_t *byte,
                                      uint16_t length) {
    assert(uart == &huart1 && length == 1);
    if (next_result == HAL_OK) {
        rx_target = byte;
        uart->ErrorCode = HAL_UART_ERROR_NONE;
    }
    return next_result;
}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *uart) {
    assert(uart == &huart1);
    aborts++;
    return HAL_OK;
}
static void send(uint8_t byte) {
    assert(rx_target);
    *rx_target = byte;
    HAL_UART_RxCpltCallback(&huart1);
}
static void send_frame(void) {
    for (unsigned i = 0; i < sizeof(frame); i++)
        send(frame[i]);
}
int main(void) {
    app_receiver_sample_t sample;
    app_receiver_init();
    assert(!app_receiver_get_sample(NULL));
    assert(app_receiver_get_sample(&sample) && !sample.connected);
    for (unsigned i = 0; i < CRSF_CHANNEL_COUNT; i++)
        assert(sample.channels[i] == 0);
    tick = UINT32_MAX - 50;
    send_frame();
    tick =
        48; // 99 ms old across wrap, timestamp belongs to reception, not poll.
    assert(app_receiver_get_sample(&sample) && sample.connected);
    assert(sample.last_frame_ms == UINT32_MAX - 50);
    for (unsigned i = 0; i < CRSF_CHANNEL_COUNT; i++)
        assert(sample.channels[i] == 992);
    tick = 49;
    assert(app_receiver_get_sample(&sample) && !sample.connected);
    send_frame();
    assert(app_receiver_get_sample(&sample) && sample.connected);
    for (unsigned i = 0; i < 300; i++)
        send(0);
    assert(app_receiver_get_sample(&sample) && !sample.connected);
    send_frame();
    assert(app_receiver_get_sample(&sample) && sample.connected);
    // HAL can deliver RxCplt before ErrorCallback; an errored byte must not
    // rearm reception and erase the error before HAL handles it.
    huart1.ErrorCode = 8;
    send(0xc8);
    assert(huart1.ErrorCode == 8);
    unsigned previous_aborts = aborts;
    HAL_UART_ErrorCallback(&huart1);
    assert(aborts == previous_aborts + 1);
    assert(app_receiver_get_sample(&sample) && !sample.connected);
    send_frame();
    assert(app_receiver_get_sample(&sample) && sample.connected);
    next_result = HAL_ERROR;
    send(0);
    assert(app_receiver_get_sample(&sample) && !sample.connected);
    next_result = HAL_OK;
    app_receiver_poll();
    send_frame();
    assert(app_receiver_get_sample(&sample) && sample.connected);
    UART_HandleTypeDef other = {0};
    previous_aborts = aborts;
    HAL_UART_ErrorCallback(&other);
    HAL_UART_RxCpltCallback(&other);
    assert(aborts == previous_aborts);
    app_receiver_init();
    assert(app_receiver_get_sample(&sample) && !sample.connected);
    mask = 1;
    app_receiver_poll();
    assert(mask == 1);
    puts("Receiver tests passed");
}
