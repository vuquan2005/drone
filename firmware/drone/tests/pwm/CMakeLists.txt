cmake_minimum_required(VERSION 3.22)
project(pwm_tests C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_testing()
set(FIRMWARE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(pwm_tests test_pwm.c ${FIRMWARE_ROOT}/BSP/Src/bsp_pwm.c)
target_include_directories(pwm_tests PRIVATE ${FIRMWARE_ROOT}/BSP/Inc)
target_include_directories(
    pwm_tests SYSTEM
    PRIVATE ${FIRMWARE_ROOT}/Core/Inc ${FIRMWARE_ROOT}/Drivers/STM32F1xx_HAL_Driver/Inc
            ${FIRMWARE_ROOT}/Drivers/CMSIS/Include
            ${FIRMWARE_ROOT}/Drivers/CMSIS/Device/ST/STM32F1xx/Include)
target_compile_definitions(pwm_tests PRIVATE USE_HAL_DRIVER STM32F103xE)
target_compile_options(pwm_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME pwm_tests COMMAND pwm_tests)
