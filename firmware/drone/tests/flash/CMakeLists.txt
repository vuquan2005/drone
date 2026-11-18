cmake_minimum_required(VERSION 3.22)
project(flash_journal_tests C)
set(CMAKE_C_STANDARD 11)
enable_testing()

set(FIRMWARE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(flash_journal_tests test_journal.c ${FIRMWARE_ROOT}/App/Src/app_flash_journal.c)
target_include_directories(flash_journal_tests PRIVATE ${FIRMWARE_ROOT}/App/Inc
                                                       ${FIRMWARE_ROOT}/BSP/Inc)
target_compile_options(flash_journal_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME flash_journal_tests COMMAND flash_journal_tests)
