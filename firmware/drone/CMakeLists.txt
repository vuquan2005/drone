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

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE # Add user defined library search paths
)

# Add user sources to executable when needed:
target_sources(
    ${CMAKE_PROJECT_NAME}
    PRIVATE Drivers/BMI270/bmi2.c
            Drivers/BMI270/bmi270.c
            BSP/Src/bsp_bmi270.c
            BSP/Src/bsp_board.c
            BSP/Src/bsp_time.c
            BSP/Src/bsp_receiver.c
            BSP/Src/bsp_pwm.c
            App/Src/app.c
            App/Src/app_imu.c
            App/Src/app_receiver.c
            App/Src/crsf.c)

# Add include paths
target_include_directories(
    ${CMAKE_PROJECT_NAME}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/BMI270 ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Inc
            ${CMAKE_CURRENT_SOURCE_DIR}/App/Inc)

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
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE App/Src/app_telemetry.c)
    endif()
endif()
if(DRONE_ENABLE_RTT AND DRONE_ENABLE_TELEMETRY)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE DRONE_TELEMETRY=1)
endif()

# Add user project symbols (macros) when needed:
# target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ...)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME} stm32cubemx # Add user defined libraries
)
