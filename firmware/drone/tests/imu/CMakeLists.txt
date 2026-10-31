cmake_minimum_required(VERSION 3.22)
project(imu_tests C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_testing()

set(FIRMWARE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(
    imu_tests
    test_imu.c ${FIRMWARE_ROOT}/BSP/Src/bsp_bmi270.c ${FIRMWARE_ROOT}/BSP/Src/bsp_board.c
    ${FIRMWARE_ROOT}/App/Src/app_imu.c ${FIRMWARE_ROOT}/Drivers/BMI270/bmi2.c
    ${FIRMWARE_ROOT}/Drivers/BMI270/bmi270.c)
target_include_directories(
    imu_tests PRIVATE mocks ${FIRMWARE_ROOT}/BSP/Inc ${FIRMWARE_ROOT}/App/Inc
                      ${FIRMWARE_ROOT}/Drivers/BMI270)
target_compile_options(imu_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_link_libraries(imu_tests PRIVATE m)
add_test(NAME imu_tests COMMAND imu_tests)
