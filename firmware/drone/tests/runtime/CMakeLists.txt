cmake_minimum_required(VERSION 3.22)
project(runtime_tests C)
set(CMAKE_C_STANDARD 11)
enable_testing()
set(FIRMWARE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
include(${FIRMWARE_ROOT}/cmake/imu.cmake)
foreach(test_target IN ITEMS runtime_tests runtime_export_tests)
    add_executable(
        ${test_target}
        test_runtime.c ${FIRMWARE_ROOT}/App/Src/runtime.c ${FIRMWARE_ROOT}/App/Src/status_led.c
        ${FIRMWARE_ROOT}/App/Src/app_command.c ${FIRMWARE_ROOT}/App/Src/flash_erase.c
        ${FIRMWARE_ROOT}/App/Src/app_flash_journal.c)
    target_include_directories(
        ${test_target}
        PRIVATE ../logging/mocks ../imu/mocks ${FIRMWARE_ROOT}/Drivers/RTT ${FIRMWARE_ROOT}/App/Inc
                ${FIRMWARE_ROOT}/BSP/Inc ${FIRMWARE_ROOT}/Control/Inc)
    target_compile_options(${test_target} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    drone_imu_headers(${test_target} ${DRONE_IMU})
    add_test(NAME ${test_target} COMMAND ${test_target})
endforeach()
target_include_directories(runtime_export_tests PRIVATE ${FIRMWARE_ROOT}/Maintenance/Inc)
target_compile_definitions(runtime_export_tests PRIVATE DRONE_RUNTIME_FLASH_EXPORT=1 DRONE_RTT=1)
