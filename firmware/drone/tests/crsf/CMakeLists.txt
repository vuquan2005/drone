cmake_minimum_required(VERSION 3.22)
project(crsf_tests C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_testing()
set(FIRMWARE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(crsf_tests test_crsf.c ${FIRMWARE_ROOT}/App/Src/crsf.c)
add_executable(
    receiver_tests test_receiver.c ${FIRMWARE_ROOT}/App/Src/crsf.c
                   ${FIRMWARE_ROOT}/App/Src/app_receiver.c ${FIRMWARE_ROOT}/BSP/Src/bsp_receiver.c)
foreach(target crsf_tests receiver_tests)
    target_include_directories(${target} PRIVATE mocks ${FIRMWARE_ROOT}/App/Inc
                                                 ${FIRMWARE_ROOT}/BSP/Inc)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME ${target} COMMAND ${target})
endforeach()
