cmake_minimum_required(VERSION 3.22)
project(rate_tests C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_testing()
set(FIRMWARE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(rate_tests test_rate.c ${FIRMWARE_ROOT}/Control/Src/rate_controller.c
                          ${FIRMWARE_ROOT}/Control/Src/mixer.c)
target_include_directories(rate_tests PRIVATE ${FIRMWARE_ROOT}/Control/Inc)
target_compile_options(rate_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_link_libraries(rate_tests PRIVATE m)
add_test(NAME rate_tests COMMAND rate_tests)

add_executable(
    app_rate_tests
    test_app_rate.c ${FIRMWARE_ROOT}/App/Src/app_rate.c ${FIRMWARE_ROOT}/App/Src/app_command.c
    ${FIRMWARE_ROOT}/Control/Src/rate_controller.c ${FIRMWARE_ROOT}/Control/Src/mixer.c)
target_include_directories(app_rate_tests PRIVATE ${FIRMWARE_ROOT}/App/Inc
                                                  ${FIRMWARE_ROOT}/Control/Inc)
target_compile_options(app_rate_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_link_libraries(app_rate_tests PRIVATE m)
add_test(NAME app_rate_tests COMMAND app_rate_tests)

add_executable(safety_tests test_safety.c ${FIRMWARE_ROOT}/App/Src/app_safety.c)
target_include_directories(safety_tests PRIVATE ${FIRMWARE_ROOT}/App/Inc
                                                ${FIRMWARE_ROOT}/Control/Inc)
target_compile_options(safety_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME safety_tests COMMAND safety_tests)
