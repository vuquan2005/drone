/**
 * @file bsp_board.h
 * @brief Board-specific IMU bus and chip-select mapping.
 */
#ifndef BSP_BOARD_H
#define BSP_BOARD_H

#include "bsp_bmi270.h"

/**
 * @brief Get the BMI270 bus and pin mapping for this board.
 * @pre CubeMX clock, GPIO and SPI initialization has completed.
 * @return Static board-owned configuration for SPI1 and IMU_CS (PD7).
 * @note Call from the foreground. Enables the DWT delay provider before
 * returning. Does not initialize SPI/GPIO or probe the sensor.
 */
const bsp_bmi270_config_t *bsp_board_imu_config(void);

#endif
