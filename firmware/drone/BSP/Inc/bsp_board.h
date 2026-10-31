#ifndef BSP_BOARD_H
#define BSP_BOARD_H

#include "bsp_bmi270.h"

/* Call after CubeMX initializes the clock, GPIO and SPI.
 * NULL means the board wiring has not been configured yet. */
const bsp_bmi270_config_t *bsp_board_imu_config(void);

#endif
