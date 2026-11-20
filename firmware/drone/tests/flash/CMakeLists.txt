cmake_minimum_required(VERSION 3.22)
project(flash_journal_tests C)
set(CMAKE_C_STANDARD 11)
enable_testing()

set(FIRMWARE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
include(${FIRMWARE_ROOT}/cmake/imu.cmake)
foreach(RTT IN ITEMS 0 1)
    set(TARGET flash_journal_rtt_${RTT}_tests)
    add_executable(
        ${TARGET}
        test_journal.c ${FIRMWARE_ROOT}/App/Src/app_flash_journal.c
        ${FIRMWARE_ROOT}/App/Src/app_flash_recorder.c ${FIRMWARE_ROOT}/App/Src/app_telemetry.c
        ${FIRMWARE_ROOT}/App/Src/app_command.c ${FIRMWARE_ROOT}/App/Src/app_config.c)
    drone_imu_headers(${TARGET} ${DRONE_IMU})
    target_include_directories(
        ${TARGET} PRIVATE ../imu/mocks ${FIRMWARE_ROOT}/App/Inc ${FIRMWARE_ROOT}/BSP/Inc
                          ${FIRMWARE_ROOT}/Control/Inc)
    target_compile_definitions(${TARGET} PRIVATE DRONE_FLASH_RECORD=1 DRONE_RTT=${RTT}
                                                 DRONE_TELEMETRY=${RTT})
    target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME ${TARGET} COMMAND ${TARGET})
endforeach()
