cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME drone)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

set(DRONE_RTT_DEFAULT OFF)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DRONE_RTT_DEFAULT ON)
endif()
option(DRONE_ENABLE_RTT "Build the two-channel RTT transport" ${DRONE_RTT_DEFAULT})
set(DRONE_LOG_DEFAULT OFF)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DRONE_LOG_DEFAULT INFO)
endif()
set(DRONE_LOG_LEVEL
    ${DRONE_LOG_DEFAULT}
    CACHE STRING "Log level: OFF, ERROR, WARN, INFO, DEBUG")
set_property(CACHE DRONE_LOG_LEVEL PROPERTY STRINGS OFF ERROR WARN INFO DEBUG)
set(DRONE_LOG_LEVELS OFF ERROR WARN INFO DEBUG)
list(FIND DRONE_LOG_LEVELS "${DRONE_LOG_LEVEL}" DRONE_LOG_LEVEL_VALUE)
if(DRONE_LOG_LEVEL_VALUE LESS 0)
    message(FATAL_ERROR "Invalid DRONE_LOG_LEVEL: ${DRONE_LOG_LEVEL}")
endif()
option(DRONE_ENABLE_TELEMETRY "Build binary IMU, statistics and RC telemetry over RTT"
       ${DRONE_RTT_DEFAULT})

option(DRONE_DEBUG_FORCE_FLIGHT "Enter FLIGHT without waiting for RC boot selection" OFF)

option(DRONE_ENABLE_FLASH_RECORD "Record flight telemetry to external SPI flash" ON)

option(DRONE_FLASH_BOARD_SMOKE "Run only the SWD SPI flash board check" OFF)

option(DRONE_ENABLE_RATE_CONTROL "Configure rate controller with initial bench gains" ON)
option(DRONE_ENABLE_BENCH_IDLE_DISARM "Disarm after 5 s of low throttle on the bench" ON)

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})
set_property(
    TARGET ${CMAKE_PROJECT_NAME}
    APPEND
    PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/STM32F103xx_FLASH.ld)

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Match STM32F103xx_FLASH.ld: interrupts must use the application's Flash
# vectors even when a debugger starts it with boot ROM mapped at address zero.
target_compile_definitions(STM32_Drivers PRIVATE USER_VECT_TAB_ADDRESS)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE # Add user defined library search paths
)

include(cmake/imu.cmake)
drone_imu_sources(${CMAKE_PROJECT_NAME} ${DRONE_IMU})

# Add user sources to executable when needed:
target_sources(
    ${CMAKE_PROJECT_NAME}
    PRIVATE Control/Src/attitude_controller.c
            Control/Src/mixer.c
            Control/Src/rate_controller.c
            Control/Src/mahony.c
            Control/Src/gyro_bias.c
            Control/Src/accel_calibration.c
            BSP/Src/bsp_board.c
            BSP/Src/bsp_flash.c
            BSP/Src/bsp_imu_flash.c
            BSP/Src/bsp_time.c
            BSP/Src/bsp_receiver.c
            BSP/Src/bsp_motor_pwm.c
            App/Src/runtime.c
            App/Src/app_flash_journal.c
            App/Src/esc_calibration.c
            App/Src/flash_erase.c
            App/Src/imu_calibration.c
            App/Src/imu_calibration_store.c
            App/Src/app_config.c
            App/Src/app.c
            App/Src/app_rate.c
            App/Src/app_safety.c
            App/Src/app_command.c
            App/Src/app_imu.c
            App/Src/app_motor.c
            App/Src/app_receiver.c
            App/Src/crsf.c)

if(DRONE_FLASH_BOARD_SMOKE)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE tests/flash/board_smoke.c)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE DRONE_FLASH_BOARD_SMOKE=1)
endif()

# Add include paths
target_include_directories(
    ${CMAKE_PROJECT_NAME}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Inc ${CMAKE_CURRENT_SOURCE_DIR}/App/Inc
            ${CMAKE_CURRENT_SOURCE_DIR}/Control/Inc)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE DRONE_RTT=$<BOOL:${DRONE_ENABLE_RTT}>)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE DRONE_LOG_LEVEL=${DRONE_LOG_LEVEL_VALUE})
if(DRONE_ENABLE_RTT)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE BSP/Src/bsp_rtt.c Drivers/RTT/SEGGER_RTT.c)
    target_include_directories(${CMAKE_PROJECT_NAME}
                               PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/RTT)
    if(DRONE_LOG_LEVEL_VALUE GREATER 0)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE App/Src/app_log.c)
    endif()
    if(DRONE_ENABLE_TELEMETRY)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE DRONE_TELEMETRY=1)
    endif()
endif()

if(DRONE_ENABLE_FLASH_RECORD)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE App/Src/app_flash_recorder.c)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE DRONE_FLASH_RECORD=1)
endif()
if(DRONE_ENABLE_FLASH_RECORD OR (DRONE_ENABLE_RTT AND DRONE_ENABLE_TELEMETRY))
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE App/Src/app_telemetry.c)
endif()

# Add user project symbols (macros) when needed:
# target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ...)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME} stm32cubemx # Add user defined libraries
)

target_compile_definitions(${CMAKE_PROJECT_NAME}
                           PRIVATE DRONE_RATE_CONTROL=$<BOOL:${DRONE_ENABLE_RATE_CONTROL}>)
if(NOT DRONE_ENABLE_BENCH_IDLE_DISARM)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE APP_ARM_IDLE_TIMEOUT_MS=0U)
endif()

target_compile_definitions(${CMAKE_PROJECT_NAME}
                           PRIVATE DRONE_DEBUG_FORCE_FLIGHT=$<BOOL:${DRONE_DEBUG_FORCE_FLIGHT}>)
