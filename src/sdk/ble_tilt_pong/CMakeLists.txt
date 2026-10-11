cmake_minimum_required(VERSION 3.13)

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(ble_tilt_pong C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(ble_tilt_pong
    main.c
    ../../platform/badger/i2c_bitbang.c
    ../../platform/badger/lsm6ds3.c
)

target_include_directories(ble_tilt_pong PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../../platform/badger
)

target_compile_options(ble_tilt_pong PRIVATE
    -Wall
    -Wextra
    -Werror
)

target_link_libraries(ble_tilt_pong
    pico_stdlib
    pico_multicore
    pico_btstack_ble
    pico_btstack_cyw43
    pico_cyw43_arch_none
)

pico_btstack_make_gatt_header(ble_tilt_pong PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/ble_tilt_pong.gatt
)

pico_enable_stdio_usb(ble_tilt_pong 0)
pico_enable_stdio_uart(ble_tilt_pong 0)
pico_add_extra_outputs(ble_tilt_pong)
