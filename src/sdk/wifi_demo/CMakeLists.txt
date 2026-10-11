cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/../../../vendor/pico-sdk" CACHE PATH "Pico SDK path")
set(PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../../vendor/tufty2350/board" CACHE STRING "Badger/Tufty board header path")
set(PICO_BOARD "pimoroni_tufty2350" CACHE STRING "Badger/Tufty RP2350 board")
set(PICO_NUM_GPIOS 48 CACHE STRING "RP2350B GPIO count")
add_compile_definitions(CYW43_PIO_CLOCK_DIV_DYNAMIC=1)

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(badger_wifi_demo C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(wifi_demo main.c)
target_include_directories(wifi_demo PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${BADGER_BUILD_FIRMWARE_DIR})
target_link_libraries(wifi_demo pico_stdlib pico_multicore pico_cyw43_arch_lwip_threadsafe_background)

pico_enable_stdio_usb(wifi_demo 0)
pico_enable_stdio_uart(wifi_demo 0)
pico_add_extra_outputs(wifi_demo)