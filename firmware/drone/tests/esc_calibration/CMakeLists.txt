cmake_minimum_required(VERSION 3.22)
project(esc_calibration_tests C)
set(CMAKE_C_STANDARD 11)
enable_testing()
set(FIRMWARE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(esc_calibration_tests test_calibration.c ${FIRMWARE_ROOT}/App/Src/esc_calibration.c)
target_include_directories(
    esc_calibration_tests PRIVATE ../logging/mocks ../imu/mocks ${FIRMWARE_ROOT}/App/Inc
                                  ${FIRMWARE_ROOT}/BSP/Inc ${FIRMWARE_ROOT}/Control/Inc)
target_compile_options(esc_calibration_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME esc_calibration_tests COMMAND esc_calibration_tests)
