cmake_minimum_required(VERSION 3.22)
project(logging_tests C)
set(CMAKE_C_STANDARD 11)
enable_testing()
set(FIRMWARE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
foreach(level IN ITEMS 0 3 4)
    set(TARGET logging_${level}_tests)
    add_executable(${TARGET} test_logging.c)
    target_include_directories(
        ${TARGET} PRIVATE ../imu/mocks ${FIRMWARE_ROOT}/App/Inc ${FIRMWARE_ROOT}/BSP/Inc
                          ${FIRMWARE_ROOT}/Drivers/BMI270 ${FIRMWARE_ROOT}/Drivers/RTT)
    target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_compile_definitions(
        ${TARGET}
        PRIVATE DRONE_LOG_LEVEL=${level}
                TELEMETRY_FIXTURE_PATH="${FIRMWARE_ROOT}/../../protocol/fixtures/imu_v1.hex"
                RTT_STATS_FIXTURE_PATH="${FIRMWARE_ROOT}/../../protocol/fixtures/rtt_stats_v1.hex")
    if(level GREATER 0)
        target_sources(
            ${TARGET}
            PRIVATE ${FIRMWARE_ROOT}/App/Src/app_log.c ${FIRMWARE_ROOT}/App/Src/app_telemetry.c
                    ${FIRMWARE_ROOT}/BSP/Src/bsp_rtt.c ${FIRMWARE_ROOT}/Drivers/RTT/SEGGER_RTT.c)
        target_compile_definitions(${TARGET} PRIVATE DRONE_RTT=1 DRONE_TELEMETRY=1)
    endif()
    add_test(NAME ${TARGET} COMMAND ${TARGET})
endforeach()

add_executable(
    app_logging_tests
    test_app.c ${FIRMWARE_ROOT}/App/Src/app.c ${FIRMWARE_ROOT}/App/Src/app_log.c
    ${FIRMWARE_ROOT}/App/Src/app_telemetry.c ${FIRMWARE_ROOT}/BSP/Src/bsp_rtt.c
    ${FIRMWARE_ROOT}/Drivers/RTT/SEGGER_RTT.c)
target_include_directories(
    app_logging_tests PRIVATE mocks ../imu/mocks ${FIRMWARE_ROOT}/App/Inc ${FIRMWARE_ROOT}/BSP/Inc
                              ${FIRMWARE_ROOT}/Drivers/BMI270 ${FIRMWARE_ROOT}/Drivers/RTT)
target_compile_definitions(app_logging_tests PRIVATE DRONE_RTT=1 DRONE_LOG_LEVEL=3
                                                     DRONE_TELEMETRY=1)
target_compile_options(app_logging_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME app_logging_tests COMMAND app_logging_tests)
