cmake_minimum_required(VERSION 3.13)

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(wifi_handover C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(wifi_handover
    main.c
    controls.c
    ble_provision.c
    provision_protocol.c
    wifi_transport.c
    udp_protocol.c
    ui_render.c
    ui_display.c
    ../../platform/badger/display.c
    ../../platform/badger/graphics.c
    ../../tools/diagnostics_logic.c
)
target_include_directories(wifi_handover PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../../platform/badger
)
target_compile_options(wifi_handover PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(wifi_handover PRIVATE BADGER_DISPLAY_SDK_TIMING=1)
target_link_libraries(wifi_handover
    pico_stdlib
    pico_multicore
    pico_rand
    pico_cyw43_arch_lwip_poll
    pico_btstack_ble
    pico_btstack_cyw43
)
pico_btstack_make_gatt_header(wifi_handover PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/wifi_handover.gatt
)
pico_enable_stdio_usb(wifi_handover 0)
pico_enable_stdio_uart(wifi_handover 0)
pico_add_extra_outputs(wifi_handover)
