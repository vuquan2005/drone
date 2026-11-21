cmake_minimum_required(VERSION 3.22)
project(logging_tests C)
set(CMAKE_C_STANDARD 11)
enable_testing()
set(FIRMWARE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
include(${FIRMWARE_ROOT}/cmake/imu.cmake)
foreach(level IN ITEMS 0 3 4)
    set(TARGET logging_${level}_tests)
    add_executable(${TARGET} test_logging.c)
    drone_imu_headers(${TARGET} ${DRONE_IMU})
    target_include_directories(
        ${TARGET} PRIVATE ../imu/mocks ${FIRMWARE_ROOT}/App/Inc ${FIRMWARE_ROOT}/Control/Inc
                          ${FIRMWARE_ROOT}/BSP/Inc ${FIRMWARE_ROOT}/Drivers/RTT)
    target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_compile_definitions(
        ${TARGET}
        PRIVATE
            DRONE_LOG_LEVEL=${level}
            RATE_FIXTURE_PATH="${FIRMWARE_ROOT}/../../protocol/fixtures/rate_v1.hex"
            ATTITUDE_FIXTURE_PATH="${FIRMWARE_ROOT}/../../protocol/fixtures/attitude_v1.hex"
            ATTITUDE_CONTROL_FIXTURE_PATH="${FIRMWARE_ROOT}/../../protocol/fixtures/attitude_control_v1.hex"
            CONFIG_FIXTURE_PATH="${FIRMWARE_ROOT}/../../protocol/fixtures/config_v1.hex"
            IMU_BATCH_FIXTURE_PATH="${FIRMWARE_ROOT}/../../protocol/fixtures/imu_batch_v1.hex"
            RC_FIXTURE_PATH="${FIRMWARE_ROOT}/../../protocol/fixtures/rc_compact_v1.hex"
            RTT_STATS_FIXTURE_PATH="${FIRMWARE_ROOT}/../../protocol/fixtures/rtt_stats_v1.hex")
    if(level GREATER 0)
        target_sources(
            ${TARGET}
            PRIVATE ${FIRMWARE_ROOT}/App/Src/app_command.c ${FIRMWARE_ROOT}/App/Src/app_config.c
                    ${FIRMWARE_ROOT}/App/Src/app_log.c ${FIRMWARE_ROOT}/App/Src/app_telemetry.c
                    ${FIRMWARE_ROOT}/BSP/Src/bsp_rtt.c ${FIRMWARE_ROOT}/Drivers/RTT/SEGGER_RTT.c)
        target_compile_definitions(${TARGET} PRIVATE DRONE_RTT=1 DRONE_TELEMETRY=1)
    endif()
    add_test(NAME ${TARGET} COMMAND ${TARGET})
endforeach()

set(APP_LOGGING_SOURCES
    test_app.c
    ${FIRMWARE_ROOT}/Control/Src/attitude_controller.c
    ${FIRMWARE_ROOT}/Control/Src/rate_controller.c
    ${FIRMWARE_ROOT}/Control/Src/mahony.c
    ${FIRMWARE_ROOT}/Control/Src/mixer.c
    ${FIRMWARE_ROOT}/App/Src/runtime.c
    ${FIRMWARE_ROOT}/App/Src/esc_calibration.c
    ${FIRMWARE_ROOT}/App/Src/app.c
    ${FIRMWARE_ROOT}/App/Src/app_rate.c
    ${FIRMWARE_ROOT}/App/Src/app_safety.c
    ${FIRMWARE_ROOT}/App/Src/app_command.c
    ${FIRMWARE_ROOT}/App/Src/app_motor.c
    ${FIRMWARE_ROOT}/App/Src/app_log.c
    ${FIRMWARE_ROOT}/BSP/Src/bsp_rtt.c
    ${FIRMWARE_ROOT}/Drivers/RTT/SEGGER_RTT.c)
foreach(TARGET IN ITEMS app_logging_tests app_logging_without_telemetry_tests)
    add_executable(${TARGET} ${APP_LOGGING_SOURCES})
    drone_imu_headers(${TARGET} ${DRONE_IMU})
    target_include_directories(
        ${TARGET} PRIVATE mocks ../imu/mocks ${FIRMWARE_ROOT}/App/Inc ${FIRMWARE_ROOT}/Control/Inc
                          ${FIRMWARE_ROOT}/BSP/Inc ${FIRMWARE_ROOT}/Drivers/RTT)
    target_compile_definitions(${TARGET} PRIVATE DRONE_RTT=1 DRONE_LOG_LEVEL=3)
    if("${TARGET}" STREQUAL "app_logging_tests")
        target_sources(${TARGET} PRIVATE ${FIRMWARE_ROOT}/App/Src/app_config.c
                                         ${FIRMWARE_ROOT}/App/Src/app_telemetry.c)
        target_compile_definitions(${TARGET} PRIVATE DRONE_TELEMETRY=1)
    else()
        target_compile_definitions(${TARGET} PRIVATE DRONE_RATE_CONTROL=1)
    endif()
    target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_link_libraries(${TARGET} PRIVATE m)
    add_test(NAME ${TARGET} COMMAND ${TARGET})
endforeach()
