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
                                                 DRONE_TELEMETRY=${RTT} DRONE_LOG_LEVEL=3)
    target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME ${TARGET} COMMAND ${TARGET})
endforeach()

add_executable(flash_chip_erase_tests test_chip_erase.c ${FIRMWARE_ROOT}/BSP/Src/bsp_flash.c)
target_include_directories(flash_chip_erase_tests PRIVATE mocks ${FIRMWARE_ROOT}/BSP/Inc)
target_compile_options(flash_chip_erase_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME flash_chip_erase_tests COMMAND flash_chip_erase_tests)

add_executable(
    flash_export_tests
    test_export.c ${FIRMWARE_ROOT}/Maintenance/Src/flash_export.c
    ${FIRMWARE_ROOT}/App/Src/flash_erase.c ${FIRMWARE_ROOT}/App/Src/app_flash_journal.c)
target_include_directories(
    flash_export_tests PRIVATE ${FIRMWARE_ROOT}/Maintenance/Inc ${FIRMWARE_ROOT}/App/Inc
                               ${FIRMWARE_ROOT}/BSP/Inc ${FIRMWARE_ROOT}/Control/Inc)
target_compile_definitions(flash_export_tests PRIVATE DRONE_RTT=1)
target_compile_options(flash_export_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME flash_export_tests COMMAND flash_export_tests)
