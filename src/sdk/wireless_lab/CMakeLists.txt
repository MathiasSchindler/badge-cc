cmake_minimum_required(VERSION 3.13)

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(wireless_lab C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(wireless_lab
    main.c
)

target_include_directories(wireless_lab PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(wireless_lab
    pico_stdlib
    pico_multicore
    pico_btstack_ble
    pico_btstack_cyw43
    pico_cyw43_arch_none
)

pico_btstack_make_gatt_header(wireless_lab PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/wireless_lab.gatt
)

pico_enable_stdio_usb(wireless_lab 0)
pico_enable_stdio_uart(wireless_lab 0)
pico_add_extra_outputs(wireless_lab)
