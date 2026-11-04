#include "bsp_board.h"
#include "bsp_time.h"
#include "main.h"

/* Owned and initialized by CubeMX before app_init(). */
extern SPI_HandleTypeDef hspi1;

const bsp_bmi270_config_t *bsp_board_imu_config(void) {
    static const bsp_bmi270_config_t config = {
        .spi = &hspi1,
        .cs_port = IMU_CS_GPIO_Port,
        .cs_pin = IMU_CS_Pin,
        .timeout_ms = 100U,
        .delay_us = bsp_time_delay_us,
    };
    bsp_time_init();
    return &config;
}
