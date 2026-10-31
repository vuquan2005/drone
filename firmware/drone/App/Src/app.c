#include "app.h"
#include "app_imu.h"
#include "bsp_board.h"

static app_imu_t imu;

void app_init(void) {
    app_imu_init(&imu, bsp_board_imu_config());
}

void app_process(void) {
    app_imu_poll(&imu);
}
