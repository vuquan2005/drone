cmake_minimum_required(VERSION 3.22)
project(imu_calibration_tests C)
set(CMAKE_C_STANDARD 11)
enable_testing()

set(FIRMWARE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
include(${FIRMWARE_ROOT}/cmake/imu.cmake)
add_executable(
    imu_calibration_tests
    test_imu_calibration.c ${FIRMWARE_ROOT}/App/Src/imu_calibration.c
    ${FIRMWARE_ROOT}/App/Src/imu_calibration_store.c ${FIRMWARE_ROOT}/Control/Src/gyro_bias.c
    ${FIRMWARE_ROOT}/Control/Src/accel_calibration.c)
drone_imu_headers(imu_calibration_tests ${DRONE_IMU})
target_include_directories(
    imu_calibration_tests
    PRIVATE ../imu/mocks ../logging/mocks ${FIRMWARE_ROOT}/App/Inc ${FIRMWARE_ROOT}/BSP/Inc
            ${FIRMWARE_ROOT}/Control/Inc ${FIRMWARE_ROOT}/Drivers/RTT)
target_compile_options(imu_calibration_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_link_libraries(imu_calibration_tests PRIVATE m)
add_test(NAME imu_calibration_tests COMMAND imu_calibration_tests)
