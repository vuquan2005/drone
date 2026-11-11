#include "app.h"
#include "app_imu.h"
#include "app_receiver.h"
#include "bsp_board.h"
#include "bsp_pwm.h"
#include "bsp_rtt.h"

static app_imu_t imu;
static bool pwm_ready;

void app_init(void) {
    bsp_rtt_init();
    pwm_ready = bsp_pwm_init();
    app_imu_init(&imu, bsp_board_imu_config());
    app_receiver_init();
}

void app_process(void) {
    app_receiver_poll();
    app_imu_poll(&imu);
}
