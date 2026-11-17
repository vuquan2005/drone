cmake_minimum_required(VERSION 3.22)
project(runtime_tests C)
set(CMAKE_C_STANDARD 11)
enable_testing()
set(FIRMWARE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(runtime_tests test_runtime.c ${FIRMWARE_ROOT}/App/Src/runtime.c
                             ${FIRMWARE_ROOT}/App/Src/app_command.c)
target_include_directories(
    runtime_tests
    PRIVATE ../logging/mocks ../imu/mocks ${FIRMWARE_ROOT}/Drivers/RTT ${FIRMWARE_ROOT}/App/Inc
            ${FIRMWARE_ROOT}/BSP/Inc ${FIRMWARE_ROOT}/Control/Inc)
target_compile_options(runtime_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME runtime_tests COMMAND runtime_tests)

include(${FIRMWARE_ROOT}/cmake/imu.cmake)
drone_imu_headers(runtime_tests ${DRONE_IMU})
