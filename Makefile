BUILD_DIR := build
.DEFAULT_GOAL := all
COMPILER_TEST_DIR := src/tools/compiler_test
TARGET_DIR := $(BUILD_DIR)/universe26
FIRMWARE_DIR := $(TARGET_DIR)/firmware
UF2_DIR := $(TARGET_DIR)/uf2
HOST_TOOLS_DIR := $(BUILD_DIR)/host-tools
WIFI_BUILD_DIR := $(BUILD_DIR)/wifi-demo
WIFI_MAC_FLASH_BUILD_DIR := $(BUILD_DIR)/wifi-mac-flash
WIRELESS_LAB_BUILD_DIR := $(BUILD_DIR)/wireless-lab
BLE_TILT_PONG_BUILD_DIR := $(BUILD_DIR)/ble-tilt-pong
WIFI_HANDOVER_BUILD_DIR := $(BUILD_DIR)/wifi-handover
PICOTOOL_USB_BUILD_DIR := $(BUILD_DIR)/picotool-usb
PICOTOOL_SRC ?= build/wifi-handover/_deps/picotool-src

HOST_CC ?= cc
HOST_PYTHON ?= python3
RP2350_CC ?= clang
RP2350_OBJCOPY ?= llvm-objcopy
RP2350_NM ?= llvm-nm
RP2350_SIZE ?= llvm-size

FLASH_BASE := 0x10000000
RP2350_FAMILY_ID := 0xe48bff59

ARCH_DIR := src/arch/rp2350
SHARED_DIR := src/shared
TOOLS_DIR := src/tools
HOST_TOOLS_SRC_DIR := src/host/tools
PLATFORM_DIR := src/platform/badger
RADIO_DIR := src/radio
WIFI_SRC_DIR := src/sdk/wifi_demo
WIFI_MAC_FLASH_SRC_DIR := src/sdk/wifi_mac_flash
WIRELESS_LAB_SRC_DIR := src/sdk/wireless_lab
BLE_TILT_PONG_SRC_DIR := src/sdk/ble_tilt_pong
WIFI_HANDOVER_SRC_DIR := src/sdk/wifi_handover
PICO_SDK_DIR := vendor/pico-sdk
BADGER_BOARD_DIR := vendor/tufty2350/board
WIFI_CREDENTIALS_TXT := ssid-data/ssid.txt
WIFI_CREDENTIALS_H := $(FIRMWARE_DIR)/wifi_credentials.h

BIN2UF2 := $(HOST_TOOLS_DIR)/bin2uf2
COMMON_FIRMWARE_OBJS := \
	$(FIRMWARE_DIR)/start.o \
	$(FIRMWARE_DIR)/picobin.o
HELLO_ELF := $(FIRMWARE_DIR)/hello_idle.elf
HELLO_UF2 := $(UF2_DIR)/hello_idle.uf2
SCREEN_TEST_ELF := $(FIRMWARE_DIR)/screen_test.elf
SCREEN_TEST_UF2 := $(UF2_DIR)/screen_test.uf2
RED_SQUARE_ELF := $(FIRMWARE_DIR)/red_square.elf
RED_SQUARE_UF2 := $(UF2_DIR)/red_square.uf2
RED_SQUARE_SHARED_ELF := $(FIRMWARE_DIR)/red_square_shared.elf
RED_SQUARE_SHARED_UF2 := $(UF2_DIR)/red_square_shared.uf2
BUTTON_MAPPER_ELF := $(FIRMWARE_DIR)/button_mapper.elf
BUTTON_MAPPER_UF2 := $(UF2_DIR)/button_mapper.uf2
GPIO_EVENTS_ELF := $(FIRMWARE_DIR)/gpio_events.elf
GPIO_EVENTS_UF2 := $(UF2_DIR)/gpio_events.uf2
PERIPHERAL_PROBE_ELF := $(FIRMWARE_DIR)/peripheral_probe.elf
PERIPHERAL_PROBE_UF2 := $(UF2_DIR)/peripheral_probe.uf2
LOWER_MAPPER_ELF := $(FIRMWARE_DIR)/lower_mapper.elf
LOWER_MAPPER_UF2 := $(UF2_DIR)/lower_mapper.uf2
GPIO_MONITOR_ELF := $(FIRMWARE_DIR)/gpio_monitor.elf
GPIO_MONITOR_UF2 := $(UF2_DIR)/gpio_monitor.uf2
IMU_DEMO_ELF := $(FIRMWARE_DIR)/imu_demo.elf
IMU_DEMO_UF2 := $(UF2_DIR)/imu_demo.uf2
TILT_BREAKOUT_ELF := $(FIRMWARE_DIR)/tilt_breakout.elf
TILT_BREAKOUT_UF2 := $(UF2_DIR)/tilt_breakout.uf2
TILT_MAZE_ELF := $(FIRMWARE_DIR)/tilt_maze.elf
TILT_MAZE_UF2 := $(UF2_DIR)/tilt_maze.uf2
TILT_MAZE_OBJS := $(addprefix $(FIRMWARE_DIR)/,display.o graphics.o diagnostic_hw.o diagnostics_logic.o i2c_bitbang.o lsm6ds3.o tilt_maze.o)
TILT_MAZE_FLAGS ?=
DIAGNOSTICS_ELF := $(FIRMWARE_DIR)/diagnostics.elf
DIAGNOSTICS_UF2 := $(UF2_DIR)/diagnostics.uf2
DIAGNOSTICS_OBJS := $(addprefix $(FIRMWARE_DIR)/,display.o graphics.o diagnostic_hw.o i2c_bitbang.o lsm6ds3.o cap1208.o diagnostics_logic.o diagnostics.o)
WIFI_BRINGUP_ELF := $(FIRMWARE_DIR)/wifi_bringup.elf
WIFI_BRINGUP_UF2 := $(UF2_DIR)/wifi_bringup.uf2
WIFI_BRINGUP_OBJS := $(addprefix $(FIRMWARE_DIR)/,display.o graphics.o diagnostics_logic.o wifi_board.o wifi_pio.o wifi_gspi.o wifi_probe.o wifi_bringup_view.o wifi_bringup.o)
USB_REPORT_ELF := $(FIRMWARE_DIR)/usb_report_test.elf
USB_REPORT_UF2 := $(UF2_DIR)/usb_report_test.uf2
USB_REPORT_BUILD_H := $(FIRMWARE_DIR)/usb_report_build.h
USB_REPORT_OBJS := $(addprefix $(FIRMWARE_DIR)/,display.o graphics.o diagnostics_logic.o usb_report.o bootrom_reboot.o usb_report_test.o)
USB_REPORT_BUILD_INPUTS := $(TOOLS_DIR)/usb_report_test.c $(SHARED_DIR)/usb_report.c $(SHARED_DIR)/usb_report.h $(ARCH_DIR)/bootrom_reboot.c $(ARCH_DIR)/bootrom_reboot.h $(ARCH_DIR)/start.c $(ARCH_DIR)/picobin.S $(ARCH_DIR)/memmap_report.ld $(TOOLS_DIR)/diagnostics_logic.c $(PLATFORM_DIR)/display.c $(PLATFORM_DIR)/graphics.c $(SHARED_DIR)/stdint.h
USB_REPORT_BUILD_INPUTS += $(SHARED_DIR)/bytes.h
USB_LIVE_ELF := $(FIRMWARE_DIR)/usb_live_report.elf
USB_LIVE_UF2 := $(UF2_DIR)/usb_live_report.uf2
USB_LIVE_BUILD_H := $(FIRMWARE_DIR)/usb_live_build.h
USB_LIVE_OBJS := $(addprefix $(FIRMWARE_DIR)/,display.o graphics.o diagnostics_logic.o usb_report.o bootrom_reboot.o live_usb_control.o live_usb_hw.o usb_live_report.o wifi_board.o wifi_pio.o wifi_gspi.o wifi_probe.o wifi_usb_report.o wifi_trace.o)
USB_LIVE_BUILD_INPUTS := src/usb/live_usb.h src/usb/live_usb_control.c src/usb/live_usb_hw.c src/usb/live_usb_hw.h $(TOOLS_DIR)/usb_live_report.c $(SHARED_DIR)/usb_report.c $(SHARED_DIR)/usb_report.h $(ARCH_DIR)/bootrom_reboot.c $(ARCH_DIR)/bootrom_reboot.h $(ARCH_DIR)/start.c $(ARCH_DIR)/picobin.S $(ARCH_DIR)/memmap_report.ld $(TOOLS_DIR)/diagnostics_logic.c $(PLATFORM_DIR)/display.c $(PLATFORM_DIR)/graphics.c $(SHARED_DIR)/stdint.h $(PLATFORM_DIR)/wifi_board.c $(PLATFORM_DIR)/wifi_board.h $(RADIO_DIR)/wifi_gspi.c $(RADIO_DIR)/wifi_gspi.h $(RADIO_DIR)/wifi_probe.c $(RADIO_DIR)/wifi_probe.h $(RADIO_DIR)/wifi_usb_report.c $(RADIO_DIR)/wifi_usb_report.h
WIFI_USB_HOST_SOURCES := $(RADIO_DIR)/wifi_usb_report.c $(RADIO_DIR)/wifi_probe.c $(RADIO_DIR)/wifi_gspi.c $(RADIO_DIR)/wifi_trace.c
WIFI_USB_HEADERS := $(RADIO_DIR)/wifi_usb_report.h $(RADIO_DIR)/wifi_probe.h $(RADIO_DIR)/wifi_gspi.h $(RADIO_DIR)/wifi_trace.h
USB_LIVE_BUILD_INPUTS += $(RADIO_DIR)/wifi_trace.c $(RADIO_DIR)/wifi_trace.h $(PLATFORM_DIR)/wifi_pio.c $(PLATFORM_DIR)/wifi_pio.h
USB_LIVE_BUILD_INPUTS += $(PLATFORM_DIR)/display.h $(PLATFORM_DIR)/graphics.h $(TOOLS_DIR)/diagnostics_logic.h
USB_LIVE_BUILD_INPUTS += $(SHARED_DIR)/bytes.h
USB_LIVE_BUILD_INPUTS += $(PLATFORM_DIR)/wifi_pio_fifo.h
WIFI_ASSET_DIR := $(BUILD_DIR)/wifi-assets
WIFI_ASSET_SOURCE_DIR := $(PICO_SDK_DIR)/lib/cyw43-driver/firmware
WIFI_ASSET_INPUTS := $(WIFI_ASSET_SOURCE_DIR)/w43439A0_7_95_49_00_combined.h $(WIFI_ASSET_SOURCE_DIR)/wifi_nvram_43439.h
WIFI_ASSET_FILES := $(addprefix $(WIFI_ASSET_DIR)/,wifi_firmware.bin wifi_nvram.bin wifi_clm.bin wifi_assets_generated.h)
WIFI_FW_SOURCES := $(RADIO_DIR)/wifi_firmware.c $(RADIO_DIR)/wifi_control.c
WIFI_FW_HEADERS := $(RADIO_DIR)/wifi_firmware.h $(RADIO_DIR)/wifi_control.h
USB_WIFI_BUILD_H := $(FIRMWARE_DIR)/usb_wifi_build.h
USB_WIFI_OBJS := $(filter-out $(FIRMWARE_DIR)/usb_live_report.o $(FIRMWARE_DIR)/live_usb_control.o,$(USB_LIVE_OBJS)) \
	$(addprefix $(FIRMWARE_DIR)/,usb_wifi_firmware.o live_usb_firmware_control.o wifi_firmware.o wifi_control.o wifi_assets.o wifi_assets_blob.o)
USB_WIFI_INPUTS := $(USB_LIVE_BUILD_INPUTS) $(WIFI_FW_SOURCES) $(WIFI_FW_HEADERS) \
	$(RADIO_DIR)/wifi_assets.h $(RADIO_DIR)/wifi_assets.c $(RADIO_DIR)/wifi_assets.S $(WIFI_ASSET_INPUTS) $(HOST_TOOLS_SRC_DIR)/wifi_pack_assets.c
WIFI_LINK_SOURCES := $(RADIO_DIR)/wifi_link.c $(RADIO_DIR)/wifi_link_report.c
WIFI_LINK_HEADERS := $(RADIO_DIR)/wifi_link.h $(RADIO_DIR)/wifi_link_report.h
USB_LINK_BUILD_H := $(FIRMWARE_DIR)/usb_link_build.h
USB_LINK_INPUTS := $(USB_WIFI_INPUTS) $(TOOLS_DIR)/usb_wifi_link.c $(TOOLS_DIR)/wifi_link_view.h $(WIFI_LINK_SOURCES) $(WIFI_LINK_HEADERS)
USB_LINK_OBJS := $(filter-out $(FIRMWARE_DIR)/usb_wifi_firmware.o $(FIRMWARE_DIR)/live_usb_firmware_control.o,$(USB_WIFI_OBJS)) \
	$(addprefix $(FIRMWARE_DIR)/,usb_wifi_link.o live_usb_link_control.o wifi_link.o wifi_link_report.o)
WIFI_UDP_SOURCES := $(RADIO_DIR)/wifi_udp.c $(RADIO_DIR)/wifi_udp_report.c
WIFI_UDP_HEADERS := $(RADIO_DIR)/wifi_udp.h $(RADIO_DIR)/wifi_udp_report.h
USB_UDP_BUILD_H := $(FIRMWARE_DIR)/usb_udp_build.h
USB_UDP_INPUTS := $(USB_LINK_INPUTS) $(WIFI_UDP_SOURCES) $(WIFI_UDP_HEADERS)
USB_UDP_OBJS := $(filter-out $(FIRMWARE_DIR)/usb_wifi_link.o $(FIRMWARE_DIR)/live_usb_link_control.o,$(USB_LINK_OBJS)) \
	$(addprefix $(FIRMWARE_DIR)/,usb_wifi_udp.o live_usb_udp_control.o wifi_udp.o wifi_udp_report.o)
UF2S := $(HELLO_UF2) $(SCREEN_TEST_UF2) $(BUTTON_MAPPER_UF2) $(GPIO_EVENTS_UF2) $(PERIPHERAL_PROBE_UF2) $(LOWER_MAPPER_UF2) $(GPIO_MONITOR_UF2) $(IMU_DEMO_UF2) $(TILT_BREAKOUT_UF2) $(TILT_MAZE_UF2) $(DIAGNOSTICS_UF2)

RP2350_CFLAGS := \
	-std=c11 -Wall -Wextra -Werror -Oz \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-fno-unwind-tables -fno-asynchronous-unwind-tables \
	-ffunction-sections -fdata-sections \
	--target=arm-none-eabi -mcpu=cortex-m33 -mthumb \
	-I$(SHARED_DIR)
RP2350_ASFLAGS := --target=arm-none-eabi -mcpu=cortex-m33 -mthumb
COMPILER_TEST_MODULES := suite arithmetic strings memory control abi abi_helpers graphics kernels kernel_timing
COMPILER_TEST_TIMING ?= 0
COMPILER_TEST_APPLICATION ?= 0
ifneq ($(filter $(COMPILER_TEST_APPLICATION),0 1),$(COMPILER_TEST_APPLICATION))
$(error COMPILER_TEST_APPLICATION must be 0 or 1)
endif
ifeq ($(COMPILER_TEST_APPLICATION),1)
COMPILER_TEST_TIMING := 1
COMPILER_TEST_MODULES := suite application
endif
ifneq ($(COMPILER_TEST_TIMING),0)
ifneq ($(COMPILER_TEST_TIMING),1)
$(error COMPILER_TEST_TIMING must be 0 or 1)
endif
endif
COMPILER_TEST_FLAGS := $(if $(filter 1,$(COMPILER_TEST_TIMING)),-DCOMPILER_TEST_TIMING) \
	$(if $(filter 1,$(COMPILER_TEST_APPLICATION)),-DCOMPILER_TEST_APPLICATION -I$(PLATFORM_DIR) -I$(TOOLS_DIR))
COMPILER_TEST_SOURCES := $(addprefix $(COMPILER_TEST_DIR)/,$(addsuffix .c,$(COMPILER_TEST_MODULES)))
COMPILER_TEST_OBJS := $(addprefix $(FIRMWARE_DIR)/compiler_,$(addsuffix .o,$(COMPILER_TEST_MODULES))) \
	$(addprefix $(FIRMWARE_DIR)/,compiler_badge.o compiler_usb_control.o live_usb_hw.o display.o graphics.o diagnostics_logic.o usb_report.o bootrom_reboot.o)
COMPILER_TEST_LINKER := badge-compiler/scripts/memmap_compiler_test.ld
COMPILER_TEST_BUILD_H := $(FIRMWARE_DIR)/compiler_test_build.h
COMPILER_TEST_INPUTS := $(COMPILER_TEST_SOURCES) $(COMPILER_TEST_DIR)/badge.c $(COMPILER_TEST_DIR)/suite.h $(COMPILER_TEST_DIR)/kernels.h \
	$(PLATFORM_DIR)/graphics.c $(PLATFORM_DIR)/graphics.h $(PLATFORM_DIR)/display.c $(PLATFORM_DIR)/display.h \
	$(TOOLS_DIR)/diagnostics_logic.c $(TOOLS_DIR)/diagnostics_logic.h $(SHARED_DIR)/usb_report.c \
	$(SHARED_DIR)/usb_report.h $(SHARED_DIR)/bytes.h $(SHARED_DIR)/stdint.h \
	$(ARCH_DIR)/start.c $(ARCH_DIR)/picobin.S $(ARCH_DIR)/bootrom_reboot.c $(ARCH_DIR)/bootrom_reboot.h \
	$(COMPILER_TEST_LINKER) src/usb/live_usb.h src/usb/live_usb_control.c \
	src/usb/live_usb_hw.c src/usb/live_usb_hw.h Makefile
ifeq ($(COMPILER_TEST_APPLICATION),1)
COMPILER_TEST_INPUTS += $(COMPILER_TEST_DIR)/application.h $(TOOLS_DIR)/tilt_maze.c \
	$(PLATFORM_DIR)/lsm6ds3.h $(PLATFORM_DIR)/diagnostic_hw.h $(PLATFORM_DIR)/i2c_bitbang.h
endif
RP2350_LDFLAGS := \
	--target=arm-none-eabi -mcpu=cortex-m33 -mthumb \
	-nostdlib -fuse-ld=lld -Wl,--gc-sections \
	-Wl,--fatal-warnings -T$(ARCH_DIR)/memmap_flash.ld

.PHONY: all clean force wifi-demo wifi-mac-flash wireless-lab ble-tilt-pong diagnostics test-diagnostics wifi-handover test-wifi-handover wifi-bringup test-wifi-bringup usb-report-test test-usb-report picotool-usb
.SECONDARY:
.PHONY: usb-live-report test-live-usb
.PHONY: clean-rx-badge
clean-rx-badge:
	$(MAKE) -C clean-rx/badge all

MICRO_SIGNAL_DIR := $(BUILD_DIR)/micro-signal
MICRO_SIGNAL_CONTROLLER := $(BUILD_DIR)/clean-rx/cyw43_rx_fw.bin
MICRO_SIGNAL_SOURCES := $(TOOLS_DIR)/micro-signal.c $(TOOLS_DIR)/diagnostics_logic.c \
	clean-rx/badge/loader.c clean-rx/badge/assets.S \
	$(ARCH_DIR)/start.c $(ARCH_DIR)/picobin.S $(ARCH_DIR)/bootrom_reboot.c \
	$(PLATFORM_DIR)/home.c $(PLATFORM_DIR)/display.c $(PLATFORM_DIR)/graphics.c \
	$(PLATFORM_DIR)/wifi_board.c $(PLATFORM_DIR)/wifi_pio.c \
	$(RADIO_DIR)/wifi_gspi.c $(RADIO_DIR)/wifi_probe.c
MICRO_SIGNAL_HEADERS := $(TOOLS_DIR)/micro-signal.h $(TOOLS_DIR)/diagnostics_logic.h \
	clean-rx/badge/loader.h clean-rx/badge/frames.h clean-rx/tx_diag.h clean-rx/src/sdio_ring.h clean-rx/sdio_test.h \
	$(wildcard $(ARCH_DIR)/*.h $(SHARED_DIR)/*.h $(PLATFORM_DIR)/*.h $(RADIO_DIR)/*.h)
MICRO_SIGNAL_TEST_USB := src/usb/live_usb_control.c src/usb/live_usb_hw.c \
	$(SHARED_DIR)/usb_report.c $(RADIO_DIR)/wifi_usb_report.c $(RADIO_DIR)/wifi_trace.c

.PHONY: micro-signal micro-signal-self-test test-micro-signal
micro-signal: $(UF2_DIR)/micro-signal.uf2
micro-signal-self-test: $(UF2_DIR)/micro-signal-self-test.uf2
.PHONY: micro-signal-transport-test micro-signal-transport-paced-test micro-signal-transport-window-test micro-signal-transport-window32-test
micro-signal-transport-test: $(UF2_DIR)/micro-signal-transport-test.uf2 $(MICRO_SIGNAL_DIR)/collect
micro-signal-transport-paced-test: $(UF2_DIR)/micro-signal-transport-paced-test.uf2 $(MICRO_SIGNAL_DIR)/collect
micro-signal-transport-window-test: $(UF2_DIR)/micro-signal-transport-window-test.uf2 $(MICRO_SIGNAL_DIR)/collect
micro-signal-transport-window32-test: $(UF2_DIR)/micro-signal-transport-window32-test.uf2 $(MICRO_SIGNAL_DIR)/collect

$(BUILD_DIR)/clean-rx-sdio-test/cyw43_rx_fw.bin: force
	$(MAKE) -C clean-rx/src CC=$(RP2350_CC) BUILD=../../$(BUILD_DIR)/clean-rx-sdio-test \
		EXTRA_CFLAGS=-DCLEAN_SDIO_SELF_TEST

$(BUILD_DIR)/clean-rx-sdio-paced-test/cyw43_rx_fw.bin: force
	$(MAKE) -C clean-rx/src CC=$(RP2350_CC) BUILD=../../$(BUILD_DIR)/clean-rx-sdio-paced-test \
		EXTRA_CFLAGS='-DCLEAN_SDIO_SELF_TEST -DCLEAN_SDIO_SELF_TEST_PACED'

$(BUILD_DIR)/clean-rx-sdio-window-test/cyw43_rx_fw.bin: force
	$(MAKE) -C clean-rx/src CC=$(RP2350_CC) BUILD=../../$(BUILD_DIR)/clean-rx-sdio-window-test \
		EXTRA_CFLAGS='-DCLEAN_SDIO_SELF_TEST -DCLEAN_SDIO_SELF_TEST_WINDOW=1u -DCLEAN_SDIO_SELF_TEST_TRANSITION'

$(BUILD_DIR)/clean-rx-sdio-window32-test/cyw43_rx_fw.bin: force
	$(MAKE) -C clean-rx/src CC=$(RP2350_CC) BUILD=../../$(BUILD_DIR)/clean-rx-sdio-window32-test \
		EXTRA_CFLAGS='-DCLEAN_SDIO_SELF_TEST -DCLEAN_SDIO_SELF_TEST_WINDOW=1u -DCLEAN_SDIO_SELF_TEST_TRANSITION -DCLEAN_SDIO_SELF_TEST_DMA32'

$(MICRO_SIGNAL_DIR)/micro-signal-transport-test.elf: MICRO_SIGNAL_TRANSPORT_CONTROLLER = $(BUILD_DIR)/clean-rx-sdio-test/cyw43_rx_fw.bin
$(MICRO_SIGNAL_DIR)/micro-signal-transport-paced-test.elf: MICRO_SIGNAL_TRANSPORT_CONTROLLER = $(BUILD_DIR)/clean-rx-sdio-paced-test/cyw43_rx_fw.bin
$(MICRO_SIGNAL_DIR)/micro-signal-transport-window-test.elf: MICRO_SIGNAL_TRANSPORT_CONTROLLER = $(BUILD_DIR)/clean-rx-sdio-window-test/cyw43_rx_fw.bin
$(MICRO_SIGNAL_DIR)/micro-signal-transport-window32-test.elf: MICRO_SIGNAL_TRANSPORT_CONTROLLER = $(BUILD_DIR)/clean-rx-sdio-window32-test/cyw43_rx_fw.bin
$(MICRO_SIGNAL_DIR)/micro-signal-transport-test.elf: $(BUILD_DIR)/clean-rx-sdio-test/cyw43_rx_fw.bin
$(MICRO_SIGNAL_DIR)/micro-signal-transport-paced-test.elf: $(BUILD_DIR)/clean-rx-sdio-paced-test/cyw43_rx_fw.bin
$(MICRO_SIGNAL_DIR)/micro-signal-transport-window-test.elf: $(BUILD_DIR)/clean-rx-sdio-window-test/cyw43_rx_fw.bin
$(MICRO_SIGNAL_DIR)/micro-signal-transport-window32-test.elf: $(BUILD_DIR)/clean-rx-sdio-window32-test/cyw43_rx_fw.bin
$(MICRO_SIGNAL_DIR)/micro-signal-transport-test.elf $(MICRO_SIGNAL_DIR)/micro-signal-transport-paced-test.elf \
		$(MICRO_SIGNAL_DIR)/micro-signal-transport-window-test.elf $(MICRO_SIGNAL_DIR)/micro-signal-transport-window32-test.elf: \
		$(MICRO_SIGNAL_SOURCES) $(MICRO_SIGNAL_HEADERS) \
		$(MICRO_SIGNAL_TEST_USB) \
		src/usb/live_usb.h src/usb/live_usb_hw.h $(ARCH_DIR)/memmap_flash.ld Makefile | $(MICRO_SIGNAL_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -flto -DMICRO_SIGNAL_SELF_TEST -DMICRO_SIGNAL_TRANSPORT_TEST \
		-DCLEAN_RX_STRICT_TEST \
		-DWIFI_TRACE_SIZE=1024u \
		-DCLEAN_CONTROLLER_PATH='"$(abspath $(MICRO_SIGNAL_TRANSPORT_CONTROLLER))"' \
		$(RP2350_LDFLAGS) $(MICRO_SIGNAL_SOURCES) $(MICRO_SIGNAL_TEST_USB) \
		-Wl,-Map=$(@:.elf=.map) -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

$(MICRO_SIGNAL_CONTROLLER): force
	$(MAKE) -C clean-rx/src CC=$(RP2350_CC) BUILD=../../$(BUILD_DIR)/clean-rx

$(MICRO_SIGNAL_DIR):
	mkdir -p $@

$(MICRO_SIGNAL_DIR)/micro-signal-self-test.elf: MICRO_SIGNAL_FLAGS = -DMICRO_SIGNAL_SELF_TEST -DWIFI_TRACE_SIZE=1024u
$(MICRO_SIGNAL_DIR)/micro-signal-self-test.elf: MICRO_SIGNAL_EXTRA_SOURCES = $(MICRO_SIGNAL_TEST_USB)
$(MICRO_SIGNAL_DIR)/micro-signal-self-test.elf: $(MICRO_SIGNAL_TEST_USB) \
	src/usb/live_usb.h src/usb/live_usb_hw.h
$(MICRO_SIGNAL_DIR)/micro-signal.elf $(MICRO_SIGNAL_DIR)/micro-signal-self-test.elf: \
		$(MICRO_SIGNAL_SOURCES) $(MICRO_SIGNAL_HEADERS) $(MICRO_SIGNAL_CONTROLLER) \
		$(ARCH_DIR)/memmap_flash.ld Makefile | $(MICRO_SIGNAL_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -flto $(MICRO_SIGNAL_FLAGS) \
		-DCLEAN_CONTROLLER_PATH='"$(abspath $(MICRO_SIGNAL_CONTROLLER))"' \
		$(RP2350_LDFLAGS) $(MICRO_SIGNAL_SOURCES) $(MICRO_SIGNAL_EXTRA_SOURCES) \
		-Wl,-Map=$(@:.elf=.map) -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

$(MICRO_SIGNAL_DIR)/%.bin: $(MICRO_SIGNAL_DIR)/%.elf
	$(RP2350_OBJCOPY) -O binary $< $@

$(UF2_DIR)/micro-signal.uf2 $(UF2_DIR)/micro-signal-self-test.uf2 \
		$(UF2_DIR)/micro-signal-transport-test.uf2 $(UF2_DIR)/micro-signal-transport-paced-test.uf2 \
		$(UF2_DIR)/micro-signal-transport-window-test.uf2 $(UF2_DIR)/micro-signal-transport-window32-test.uf2: \
		$(UF2_DIR)/%.uf2: $(MICRO_SIGNAL_DIR)/%.bin $(BIN2UF2) | $(UF2_DIR)
	$(BIN2UF2) $< $@ $(FLASH_BASE) $(RP2350_FAMILY_ID)

$(MICRO_SIGNAL_DIR)/micro-signal-test: $(TOOLS_DIR)/micro-signal_test.c \
		$(TOOLS_DIR)/diagnostics_logic.c $(MICRO_SIGNAL_HEADERS) | $(MICRO_SIGNAL_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O2 \
		$(TOOLS_DIR)/micro-signal_test.c $(TOOLS_DIR)/diagnostics_logic.c -o $@

test-micro-signal: $(MICRO_SIGNAL_DIR)/micro-signal-test
	$(MICRO_SIGNAL_DIR)/micro-signal-test
	$(MAKE) -C clean-rx/src BUILD=../../$(BUILD_DIR)/clean-rx test

micro-signal-self-test: $(MICRO_SIGNAL_DIR)/collect
$(MICRO_SIGNAL_DIR)/collect: $(HOST_TOOLS_SRC_DIR)/micro_signal_collect.c \
		clean-rx/sdio_test.h \
		$(HOST_TOOLS_SRC_DIR)/usb_live_transport.h $(TOOLS_DIR)/diagnostics_logic.c \
		$(TOOLS_DIR)/diagnostics_logic.h $(SHARED_DIR)/bytes.h $(SHARED_DIR)/usb_report.h \
		$(RADIO_DIR)/wifi_trace.h src/usb/live_usb.h | $(MICRO_SIGNAL_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O2 $$(pkg-config --cflags libusb-1.0) \
		$(HOST_TOOLS_SRC_DIR)/micro_signal_collect.c $(TOOLS_DIR)/diagnostics_logic.c \
		$$(pkg-config --libs libusb-1.0) -o $@

.PHONY: usb-wifi-firmware test-wifi-firmware
.PHONY: usb-wifi-link test-wifi-link
.PHONY: usb-wifi-udp test-wifi-udp
.PHONY: tilt-maze test-tilt-maze
.PHONY: test-nametag-control nametag-assets nametag-stills nametag-preview nametag-preview-binary test-nametag test-nametag-target test-nametag-preview

NAMETAG_COVERAGE ?= 8
ifneq ($(filter $(NAMETAG_COVERAGE),4 8),$(NAMETAG_COVERAGE))
$(error NAMETAG_COVERAGE must be 4 or 8)
endif
FONTRENDER_DIR := $(SHARED_DIR)/fontrender
FONTRENDER_SOURCES := $(addprefix $(FONTRENDER_DIR)/,fr_platform.c fr_ttf.c fr_raster.c font_backend_truetype.c)
FONTRENDER_HEADERS := $(wildcard $(FONTRENDER_DIR)/*.h $(FONTRENDER_DIR)/fontrender/*.h)
NAMETAG_FONT_FILES := $(addprefix vendor/cascadia-code/,CascadiaMono-Regular.ttf CascadiaMono-Bold.ttf CascadiaCode-Regular.ttf CascadiaCode-Bold.ttf)
NAMETAG_SOURCES := $(SHARED_DIR)/bitmap_font.c $(SHARED_DIR)/nametag.c $(PLATFORM_DIR)/graphics.c
NAMETAG_RENDER_FLAGS := $(RP2350_CFLAGS) -O2
NAMETAG_HEADERS := $(SHARED_DIR)/bitmap_font.h $(SHARED_DIR)/nametag.h $(SHARED_DIR)/bytes.h $(PLATFORM_DIR)/graphics.h
NAMETAG_FRAME_C := $(SHARED_DIR)/frame_diff.c
NAMETAG_FRAME_H := $(SHARED_DIR)/frame_diff.h
NAMETAG_ASSET_DIR := $(BUILD_DIR)/nametag/assets-$(NAMETAG_COVERAGE)
NAMETAG_FONT_C := $(NAMETAG_ASSET_DIR)/nametag_fonts.c
NAMETAG_FONT_H := $(NAMETAG_ASSET_DIR)/nametag_fonts.h
NAMETAG_FONT_STAMP := $(NAMETAG_ASSET_DIR)/generated.stamp
NAMETAG_PREVIEW := $(HOST_TOOLS_DIR)/nametag_preview-$(NAMETAG_COVERAGE)
NAMETAG_TEST := $(HOST_TOOLS_DIR)/nametag_test-$(NAMETAG_COVERAGE)
NAMETAG_HOST_FLAGS := -std=c11 -Wall -Wextra -Werror -O2 -iquote $(SHARED_DIR)
NAMETAG_TARGET_DIR := $(BUILD_DIR)/nametag/target-$(NAMETAG_COVERAGE)
NAMETAG_TARGET_OBJS := $(addprefix $(NAMETAG_TARGET_DIR)/,bitmap_font.o nametag.o frame_diff.o graphics.o nametag_fonts.o nametag_target.o)
NAMETAG_M2_DIR := $(BUILD_DIR)/nametag/m2-$(NAMETAG_COVERAGE)
NAMETAG_M2_NAMES := start picobin display graphics diagnostics_logic bootrom_reboot nametag_clock \
	bitmap_font nametag frame_diff nametag_fonts nametag_measure usb_report live_usb_control live_usb_hw wifi_trace nametag_m2
NAMETAG_M2_OBJS := $(addprefix $(NAMETAG_M2_DIR)/,$(addsuffix .o,$(NAMETAG_M2_NAMES)))
NAMETAG_M2_INPUTS := $(TOOLS_DIR)/nametag_m2.c $(SHARED_DIR)/nametag_measure.c $(SHARED_DIR)/nametag_measure.h \
	$(ARCH_DIR)/nametag_clock.c $(ARCH_DIR)/nametag_clock.h $(USB_LIVE_BUILD_INPUTS) $(NAMETAG_SOURCES) \
	$(NAMETAG_HEADERS) $(NAMETAG_FRAME_C) $(NAMETAG_FRAME_H) $(NAMETAG_FONT_C) $(NAMETAG_FONT_H) $(HOST_TOOLS_SRC_DIR)/nametag_m2_host.c \
	$(HOST_TOOLS_SRC_DIR)/usb_live_transport.h
BADGE_NAMETAG_DIR := $(BUILD_DIR)/nametag/app-$(NAMETAG_COVERAGE)
BADGE_NAMETAG_OBJS := $(addprefix $(NAMETAG_M2_DIR)/,picobin.o display.o graphics.o \
	bootrom_reboot.o nametag_clock.o bitmap_font.o nametag.o frame_diff.o nametag_fonts.o diagnostics_logic.o) \
	$(addprefix $(BADGE_NAMETAG_DIR)/,start.o home.o badge-nametag.o)

.PHONY: badge-nametag
badge-nametag: $(UF2_DIR)/badge-nametag.uf2
	@printf 'Continuous nametag, Mono B, natural coverage: %s\n' $<

$(BADGE_NAMETAG_DIR):
	mkdir -p $@

$(BADGE_NAMETAG_DIR)/start.o: $(ARCH_DIR)/start.c | $(BADGE_NAMETAG_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -DBARE_GPIO_WAKE_IRQ -c $< -o $@

$(BADGE_NAMETAG_DIR)/home.o: $(PLATFORM_DIR)/home.c $(PLATFORM_DIR)/home.h | $(BADGE_NAMETAG_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -fstack-usage -c $< -o $@

$(BADGE_NAMETAG_DIR)/badge-nametag.o: $(TOOLS_DIR)/badge-nametag.c $(NAMETAG_HEADERS) \
	$(NAMETAG_FRAME_H) $(NAMETAG_FONT_H) $(PLATFORM_DIR)/display.h \
	$(PLATFORM_DIR)/home.h $(TOOLS_DIR)/diagnostics_logic.h \
	$(ARCH_DIR)/nametag_clock.h $(ARCH_DIR)/bootrom_reboot.h | $(BADGE_NAMETAG_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -I$(PLATFORM_DIR) -I$(NAMETAG_ASSET_DIR) -fstack-usage -c $< -o $@

$(BADGE_NAMETAG_DIR)/badge-nametag.elf: $(BADGE_NAMETAG_OBJS) $(ARCH_DIR)/memmap_flash.ld
	$(RP2350_CC) $(RP2350_CFLAGS) -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(ARCH_DIR)/memmap_flash.ld -Wl,-Map=$(BADGE_NAMETAG_DIR)/badge-nametag.map $(BADGE_NAMETAG_OBJS) -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

$(UF2_DIR)/badge-nametag.uf2: $(BADGE_NAMETAG_DIR)/badge-nametag.elf $(BIN2UF2) | $(UF2_DIR)
	$(RP2350_OBJCOPY) -O binary $< $(BADGE_NAMETAG_DIR)/badge-nametag.bin
	$(BIN2UF2) $(BADGE_NAMETAG_DIR)/badge-nametag.bin $@ $(FLASH_BASE) $(RP2350_FAMILY_ID)

$(HOST_TOOLS_DIR)/home_test: src/host/tests/home_test.c $(PLATFORM_DIR)/home.c $(PLATFORM_DIR)/home.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) $(NAMETAG_HOST_FLAGS) -O1 -g -fsanitize=address,undefined $< -o $@

.PHONY: test-home
test-home: $(HOST_TOOLS_DIR)/home_test
	$(HOST_TOOLS_DIR)/home_test

BOAT_DIR := $(BUILD_DIR)/boat
BOAT_OBJS := $(addprefix $(FIRMWARE_DIR)/,start.o picobin.o display.o graphics.o \
	i2c_bitbang.o lsm6ds3.o diagnostics_logic.o bootrom_reboot.o) \
	$(addprefix $(BOAT_DIR)/,boat.o boat_physics.o boat_input.o frame_diff.o home.o nametag_clock.o)

.PHONY: boat test-boat
boat: $(UF2_DIR)/boat.uf2
	@printf 'Boat prototype (not hardware-qualified): %s\n' $<

$(BOAT_DIR):
	mkdir -p $@

$(BOAT_DIR)/boat.o: $(TOOLS_DIR)/boat.c $(SHARED_DIR)/boat_physics.h $(SHARED_DIR)/boat_input.h $(SHARED_DIR)/frame_diff.h $(SHARED_DIR)/bytes.h \
	$(PLATFORM_DIR)/display.h $(PLATFORM_DIR)/graphics.h $(PLATFORM_DIR)/home.h \
	$(PLATFORM_DIR)/i2c_bitbang.h $(PLATFORM_DIR)/lsm6ds3.h $(TOOLS_DIR)/diagnostics_logic.h \
	$(ARCH_DIR)/nametag_clock.h $(ARCH_DIR)/bootrom_reboot.h | $(BOAT_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -I$(PLATFORM_DIR) -O2 -fstack-usage -c $< -o $@

$(BOAT_DIR)/boat_physics.o: $(SHARED_DIR)/boat_physics.c $(SHARED_DIR)/boat_physics.h | $(BOAT_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -O2 -fstack-usage -c $< -o $@

$(BOAT_DIR)/boat_input.o: $(SHARED_DIR)/boat_input.c $(SHARED_DIR)/boat_input.h \
	$(PLATFORM_DIR)/lsm6ds3.h $(TOOLS_DIR)/diagnostics_logic.h $(SHARED_DIR)/bytes.h | $(BOAT_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -O2 -fstack-usage -c $< -o $@

$(BOAT_DIR)/frame_diff.o: $(NAMETAG_FRAME_C) $(NAMETAG_FRAME_H) $(SHARED_DIR)/bytes.h $(PLATFORM_DIR)/graphics.h | $(BOAT_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -O2 -fstack-usage -c $< -o $@

$(BOAT_DIR)/home.o: $(PLATFORM_DIR)/home.c $(PLATFORM_DIR)/home.h | $(BOAT_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(BOAT_DIR)/nametag_clock.o: $(ARCH_DIR)/nametag_clock.c $(ARCH_DIR)/nametag_clock.h | $(BOAT_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(BOAT_DIR)/boat.elf: $(BOAT_OBJS) $(ARCH_DIR)/memmap_flash.ld
	$(RP2350_CC) $(RP2350_CFLAGS) $(RP2350_LDFLAGS) $(BOAT_OBJS) \
		-Wl,-Map=$(BOAT_DIR)/boat.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

$(UF2_DIR)/boat.uf2: $(BOAT_DIR)/boat.elf $(BIN2UF2) | $(UF2_DIR)
	$(RP2350_OBJCOPY) -O binary $< $(BOAT_DIR)/boat.bin
	$(BIN2UF2) $(BOAT_DIR)/boat.bin $@ $(FLASH_BASE) $(RP2350_FAMILY_ID)

$(HOST_TOOLS_DIR)/boat_test: src/host/tests/boat_test.c $(SHARED_DIR)/boat_physics.c $(SHARED_DIR)/boat_physics.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) $(NAMETAG_HOST_FLAGS) -O1 -g -fsanitize=address,undefined \
		src/host/tests/boat_test.c $(SHARED_DIR)/boat_physics.c -o $@

$(HOST_TOOLS_DIR)/boat_application_test: src/host/tests/boat_application_test.c $(TOOLS_DIR)/boat.c \
	$(SHARED_DIR)/boat_physics.c $(SHARED_DIR)/boat_physics.h $(NAMETAG_FRAME_C) $(NAMETAG_FRAME_H) \
	$(SHARED_DIR)/boat_input.c $(SHARED_DIR)/boat_input.h $(SHARED_DIR)/bytes.h \
	$(PLATFORM_DIR)/graphics.c $(PLATFORM_DIR)/graphics.h $(PLATFORM_DIR)/display.h $(PLATFORM_DIR)/home.h \
	$(PLATFORM_DIR)/lsm6ds3.h $(PLATFORM_DIR)/i2c_bitbang.h $(TOOLS_DIR)/diagnostics_logic.c \
	$(TOOLS_DIR)/diagnostics_logic.h $(ARCH_DIR)/nametag_clock.h $(ARCH_DIR)/bootrom_reboot.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) $(NAMETAG_HOST_FLAGS) -O1 -g -fsanitize=address,undefined -I$(PLATFORM_DIR) \
		src/host/tests/boat_application_test.c $(SHARED_DIR)/boat_physics.c $(SHARED_DIR)/boat_input.c $(NAMETAG_FRAME_C) \
		$(PLATFORM_DIR)/graphics.c $(TOOLS_DIR)/diagnostics_logic.c -o $@

$(HOST_TOOLS_DIR)/boat_input_test: src/host/tests/boat_input_test.c $(SHARED_DIR)/boat_input.c \
	$(SHARED_DIR)/boat_input.h $(SHARED_DIR)/bytes.h $(PLATFORM_DIR)/lsm6ds3.h \
	$(TOOLS_DIR)/diagnostics_logic.c $(TOOLS_DIR)/diagnostics_logic.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) $(NAMETAG_HOST_FLAGS) -O1 -g -fsanitize=address,undefined \
		src/host/tests/boat_input_test.c $(SHARED_DIR)/boat_input.c $(TOOLS_DIR)/diagnostics_logic.c -lm -o $@

test-boat: $(HOST_TOOLS_DIR)/boat_test $(HOST_TOOLS_DIR)/boat_application_test $(HOST_TOOLS_DIR)/boat_input_test $(HOST_TOOLS_DIR)/boat_profile_test
	$(HOST_TOOLS_DIR)/boat_test
	$(HOST_TOOLS_DIR)/boat_application_test
	$(HOST_TOOLS_DIR)/boat_input_test
	$(HOST_TOOLS_DIR)/boat_profile_test
	node --test src/host/tests/boat_profile_report_test.mjs

.PHONY: test-boat-fluid boat-fluid-preview
$(HOST_TOOLS_DIR)/boat_fluid_test: src/host/tests/boat_fluid_test.c $(SHARED_DIR)/boat_fluid.c \
	$(HOST_TOOLS_SRC_DIR)/boat_fluid_topology.h \
	$(SHARED_DIR)/boat_fluid.h $(SHARED_DIR)/boat_physics.h $(SHARED_DIR)/bytes.h \
	$(TOOLS_DIR)/diagnostics_logic.c $(TOOLS_DIR)/diagnostics_logic.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) $(NAMETAG_HOST_FLAGS) -O1 -g -fsanitize=address,undefined \
		$< $(SHARED_DIR)/boat_fluid.c $(TOOLS_DIR)/diagnostics_logic.c -o $@
$(HOST_TOOLS_DIR)/boat_fluid_preview: $(HOST_TOOLS_SRC_DIR)/boat_fluid_preview.c $(SHARED_DIR)/boat_fluid.c \
	$(HOST_TOOLS_SRC_DIR)/boat_fluid_topology.h \
	$(SHARED_DIR)/boat_fluid.h $(SHARED_DIR)/boat_physics.h $(SHARED_DIR)/bytes.h \
	$(TOOLS_DIR)/diagnostics_logic.c $(TOOLS_DIR)/diagnostics_logic.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) $(NAMETAG_HOST_FLAGS) $< $(SHARED_DIR)/boat_fluid.c $(TOOLS_DIR)/diagnostics_logic.c -o $@
test-boat-fluid: $(HOST_TOOLS_DIR)/boat_fluid_test $(HOST_TOOLS_DIR)/boat_breaking_application_test
	$(HOST_TOOLS_DIR)/boat_fluid_test
	$(HOST_TOOLS_DIR)/boat_breaking_application_test
boat-fluid-preview: $(HOST_TOOLS_DIR)/boat_fluid_preview

BOAT_BREAKING_DIR := $(BOAT_DIR)/breaking
BOAT_BREAKING_OBJS := $(filter-out $(BOAT_DIR)/boat.o $(BOAT_DIR)/boat_physics.o,$(BOAT_OBJS)) \
	$(addprefix $(BOAT_BREAKING_DIR)/,boat-breaking.o boat_fluid.o)
.PHONY: boat-breaking
boat-breaking: $(UF2_DIR)/boat-breaking.uf2
	@printf 'Experimental 2D particle water (cadence not hardware-qualified): %s\n' $<
$(BOAT_BREAKING_DIR):
	mkdir -p $@
$(BOAT_BREAKING_DIR)/boat-breaking.o: $(TOOLS_DIR)/boat-breaking.c $(TOOLS_DIR)/boat.c \
	$(SHARED_DIR)/boat_fluid.h $(SHARED_DIR)/boat_input.h $(SHARED_DIR)/boat_physics.h \
	$(SHARED_DIR)/frame_diff.h $(SHARED_DIR)/bytes.h $(PLATFORM_DIR)/graphics.h \
	$(PLATFORM_DIR)/display.h $(PLATFORM_DIR)/home.h $(PLATFORM_DIR)/lsm6ds3.h \
	$(PLATFORM_DIR)/i2c_bitbang.h $(TOOLS_DIR)/diagnostics_logic.h \
	$(ARCH_DIR)/nametag_clock.h $(ARCH_DIR)/bootrom_reboot.h | $(BOAT_BREAKING_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -O2 -fstack-usage -I$(PLATFORM_DIR) -c $< -o $@
$(BOAT_BREAKING_DIR)/boat_fluid.o: $(SHARED_DIR)/boat_fluid.c $(SHARED_DIR)/boat_fluid.h \
	$(SHARED_DIR)/boat_physics.h $(SHARED_DIR)/bytes.h $(TOOLS_DIR)/diagnostics_logic.h \
	$(PLATFORM_DIR)/graphics.h | $(BOAT_BREAKING_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -O2 -fstack-usage -c $< -o $@
$(BOAT_BREAKING_DIR)/boat-breaking.elf: $(BOAT_BREAKING_OBJS) $(ARCH_DIR)/memmap_flash.ld
	$(RP2350_CC) $(RP2350_CFLAGS) $(RP2350_LDFLAGS) $(BOAT_BREAKING_OBJS) \
		-Wl,-Map=$(BOAT_BREAKING_DIR)/boat-breaking.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@
$(UF2_DIR)/boat-breaking.uf2: $(BOAT_BREAKING_DIR)/boat-breaking.elf $(BIN2UF2) | $(UF2_DIR)
	$(RP2350_OBJCOPY) -O binary $< $(BOAT_BREAKING_DIR)/boat-breaking.bin
	$(BIN2UF2) $(BOAT_BREAKING_DIR)/boat-breaking.bin $@ $(FLASH_BASE) $(RP2350_FAMILY_ID)
$(HOST_TOOLS_DIR)/boat_breaking_application_test: src/host/tests/boat_application_test.c \
	$(TOOLS_DIR)/boat-breaking.c $(TOOLS_DIR)/boat.c $(SHARED_DIR)/boat_fluid.c $(SHARED_DIR)/boat_fluid.h \
	$(SHARED_DIR)/boat_physics.h $(SHARED_DIR)/boat_input.c $(SHARED_DIR)/boat_input.h \
	$(NAMETAG_FRAME_C) $(NAMETAG_FRAME_H) $(SHARED_DIR)/bytes.h $(PLATFORM_DIR)/graphics.c \
	$(PLATFORM_DIR)/graphics.h $(TOOLS_DIR)/diagnostics_logic.c $(TOOLS_DIR)/diagnostics_logic.h \
	$(PLATFORM_DIR)/display.h $(PLATFORM_DIR)/home.h $(PLATFORM_DIR)/lsm6ds3.h \
	$(PLATFORM_DIR)/i2c_bitbang.h $(ARCH_DIR)/nametag_clock.h $(ARCH_DIR)/bootrom_reboot.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) $(NAMETAG_HOST_FLAGS) -O1 -g -fsanitize=address,undefined \
		-DBOAT_BREAKING_TEST -I$(PLATFORM_DIR) $< $(SHARED_DIR)/boat_fluid.c \
		$(SHARED_DIR)/boat_input.c $(NAMETAG_FRAME_C) $(PLATFORM_DIR)/graphics.c \
		$(TOOLS_DIR)/diagnostics_logic.c -o $@

BOAT_PROFILE_DIR := $(BOAT_DIR)/profile
BOAT_PROFILE_OBJS := $(filter-out $(BOAT_DIR)/boat.o,$(BOAT_OBJS)) \
	$(addprefix $(BOAT_PROFILE_DIR)/,boat_profile.o boat_measure.o live_usb_control.o live_usb_hw.o nametag_measure.o usb_report.o wifi_trace.o)
BOAT_PROFILE_INPUTS := $(TOOLS_DIR)/boat_profile.c $(TOOLS_DIR)/boat.c \
	$(SHARED_DIR)/boat_measure.c $(SHARED_DIR)/boat_measure.h \
	$(SHARED_DIR)/boat_physics.c $(SHARED_DIR)/boat_physics.h $(SHARED_DIR)/boat_input.c $(SHARED_DIR)/boat_input.h \
	$(NAMETAG_FRAME_C) $(NAMETAG_FRAME_H) $(PLATFORM_DIR)/display.c $(PLATFORM_DIR)/graphics.c \
	$(PLATFORM_DIR)/i2c_bitbang.c $(PLATFORM_DIR)/lsm6ds3.c $(PLATFORM_DIR)/home.c \
	$(TOOLS_DIR)/diagnostics_logic.c $(ARCH_DIR)/nametag_clock.c $(ARCH_DIR)/bootrom_reboot.c \
	$(ARCH_DIR)/start.c $(ARCH_DIR)/picobin.S $(ARCH_DIR)/memmap_flash.ld \
	$(PLATFORM_DIR)/display.h $(PLATFORM_DIR)/graphics.h $(PLATFORM_DIR)/i2c_bitbang.h \
	$(PLATFORM_DIR)/lsm6ds3.h $(PLATFORM_DIR)/home.h $(TOOLS_DIR)/diagnostics_logic.h \
	$(SHARED_DIR)/bytes.h $(SHARED_DIR)/stdint.h $(ARCH_DIR)/nametag_clock.h $(ARCH_DIR)/bootrom_reboot.h \
	src/usb/live_usb_control.c src/usb/live_usb_hw.c src/usb/live_usb.h src/usb/live_usb_hw.h \
	$(SHARED_DIR)/nametag_measure.c $(SHARED_DIR)/nametag_measure.h $(SHARED_DIR)/usb_report.c \
	$(SHARED_DIR)/usb_report.h $(RADIO_DIR)/wifi_trace.c $(RADIO_DIR)/wifi_trace.h \
	$(HOST_TOOLS_SRC_DIR)/boat_profile_host.c $(HOST_TOOLS_SRC_DIR)/usb_live_transport.h

.PHONY: boat-profile
boat-profile: $(UF2_DIR)/boat_profile.uf2 $(HOST_TOOLS_DIR)/boat_profile_host

$(HOST_TOOLS_DIR)/boat_profile_test: src/host/tests/boat_application_test.c $(BOAT_PROFILE_INPUTS) \
	$(BOAT_PROFILE_DIR)/boat_profile_build.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) $(NAMETAG_HOST_FLAGS) -O1 -g -fsanitize=address,undefined \
		-DBOAT_PROFILE -DBOAT_PROFILE_TEST -DLIVE_USB_NAMETAG_M2 -I$(PLATFORM_DIR) -I$(BOAT_PROFILE_DIR) \
		src/host/tests/boat_application_test.c $(SHARED_DIR)/boat_physics.c $(SHARED_DIR)/boat_input.c \
		$(SHARED_DIR)/boat_measure.c $(NAMETAG_FRAME_C) $(PLATFORM_DIR)/graphics.c \
		$(TOOLS_DIR)/diagnostics_logic.c -o $@

$(BOAT_PROFILE_DIR):
	mkdir -p $@
$(BOAT_PROFILE_DIR)/boat_profile_build.h: $(BOAT_PROFILE_INPUTS) Makefile | $(BOAT_PROFILE_DIR)
	@id=$$(shasum -a 256 $(BOAT_PROFILE_INPUTS) Makefile | shasum -a 256 | cut -c1-8); \
	printf '#define USB_REPORT_BUILD_ID 0x%su\n' "$$id" > $@
$(BOAT_PROFILE_DIR)/boat_profile.o: $(BOAT_PROFILE_INPUTS) $(BOAT_PROFILE_DIR)/boat_profile_build.h | $(BOAT_PROFILE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -O2 -fstack-usage -DBOAT_PROFILE -DLIVE_USB_NAMETAG_M2 \
		-I$(PLATFORM_DIR) -I$(BOAT_PROFILE_DIR) -c $(TOOLS_DIR)/boat_profile.c -o $@
$(BOAT_PROFILE_DIR)/boat_measure.o: $(SHARED_DIR)/boat_measure.c $(SHARED_DIR)/boat_measure.h | $(BOAT_PROFILE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -O2 -c $< -o $@
$(BOAT_PROFILE_DIR)/live_usb_control.o: src/usb/live_usb_control.c src/usb/live_usb.h $(SHARED_DIR)/nametag_measure.h | $(BOAT_PROFILE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -DLIVE_USB_NAMETAG_M2 -c $< -o $@
$(BOAT_PROFILE_DIR)/live_usb_hw.o: src/usb/live_usb_hw.c src/usb/live_usb_hw.h src/usb/live_usb.h | $(BOAT_PROFILE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -DLIVE_USB_NAMETAG_M2 -c $< -o $@
$(BOAT_PROFILE_DIR)/nametag_measure.o: $(SHARED_DIR)/nametag_measure.c $(SHARED_DIR)/nametag_measure.h | $(BOAT_PROFILE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@
$(BOAT_PROFILE_DIR)/usb_report.o: $(SHARED_DIR)/usb_report.c $(SHARED_DIR)/usb_report.h | $(BOAT_PROFILE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@
$(BOAT_PROFILE_DIR)/wifi_trace.o: $(RADIO_DIR)/wifi_trace.c $(RADIO_DIR)/wifi_trace.h | $(BOAT_PROFILE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@
$(BOAT_PROFILE_DIR)/boat_profile.elf: $(BOAT_PROFILE_OBJS) $(ARCH_DIR)/memmap_flash.ld
	$(RP2350_CC) $(RP2350_CFLAGS) $(RP2350_LDFLAGS) $(BOAT_PROFILE_OBJS) \
		-Wl,-Map=$(BOAT_PROFILE_DIR)/boat_profile.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@
$(UF2_DIR)/boat_profile.uf2: $(BOAT_PROFILE_DIR)/boat_profile.elf $(BIN2UF2) | $(UF2_DIR)
	$(RP2350_OBJCOPY) -O binary $< $(BOAT_PROFILE_DIR)/boat_profile.bin
	$(BIN2UF2) $(BOAT_PROFILE_DIR)/boat_profile.bin $@ $(FLASH_BASE) $(RP2350_FAMILY_ID)
$(HOST_TOOLS_DIR)/boat_profile_host: $(HOST_TOOLS_SRC_DIR)/boat_profile_host.c \
	$(BOAT_PROFILE_DIR)/boat_profile_build.h $(SHARED_DIR)/boat_measure.h \
	$(HOST_TOOLS_SRC_DIR)/usb_live_transport.h $(SHARED_DIR)/nametag_measure.c $(SHARED_DIR)/usb_report.c \
	$(TOOLS_DIR)/diagnostics_logic.c | $(HOST_TOOLS_DIR)
	@pkg-config --exists libusb-1.0 || { echo 'Boat collector requires the existing libusb-1.0 host dependency'; exit 1; }
	$(HOST_CC) $(NAMETAG_HOST_FLAGS) -DLIVE_USB_NAMETAG_M2 -I$(BOAT_PROFILE_DIR) \
		$$(pkg-config --cflags libusb-1.0) $< $(SHARED_DIR)/nametag_measure.c \
		$(SHARED_DIR)/usb_report.c $(TOOLS_DIR)/diagnostics_logic.c \
		$$(pkg-config --libs libusb-1.0) -o $@

.PHONY: nametag-m2 test-nametag-m2
nametag-m2: $(UF2_DIR)/nametag_m2.uf2 $(HOST_TOOLS_DIR)/nametag_m2_host
	@printf 'M2 diagnostic only, bounded recovery, selected 8-bit Mono B: %s\n' $(UF2_DIR)/nametag_m2.uf2

$(NAMETAG_M2_DIR):
	mkdir -p $@

$(NAMETAG_M2_DIR)/nametag_m2_build.h: $(NAMETAG_M2_INPUTS) Makefile | $(NAMETAG_M2_DIR)
	@test "$(NAMETAG_COVERAGE)" = 8 || { echo 'M2 requires the accepted 8-bit baseline'; exit 1; }
	@id=$$(shasum -a 256 $(NAMETAG_M2_INPUTS) Makefile | shasum -a 256 | cut -c1-8); \
	printf '#define USB_REPORT_BUILD_ID 0x%su\n' "$$id" > $@

$(NAMETAG_M2_DIR)/start.o: $(ARCH_DIR)/start.c | $(NAMETAG_M2_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@
$(NAMETAG_M2_DIR)/picobin.o: $(ARCH_DIR)/picobin.S | $(NAMETAG_M2_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(NAMETAG_M2_DIR)/display.o: $(PLATFORM_DIR)/display.c $(PLATFORM_DIR)/display.h | $(NAMETAG_M2_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@
$(NAMETAG_M2_DIR)/graphics.o: $(PLATFORM_DIR)/graphics.c $(PLATFORM_DIR)/graphics.h | $(NAMETAG_M2_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@
$(NAMETAG_M2_DIR)/diagnostics_logic.o: $(TOOLS_DIR)/diagnostics_logic.c $(TOOLS_DIR)/diagnostics_logic.h | $(NAMETAG_M2_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@
$(NAMETAG_M2_DIR)/bootrom_reboot.o: $(ARCH_DIR)/bootrom_reboot.c $(ARCH_DIR)/bootrom_reboot.h | $(NAMETAG_M2_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@
$(NAMETAG_M2_DIR)/nametag_clock.o: $(ARCH_DIR)/nametag_clock.c $(ARCH_DIR)/nametag_clock.h | $(NAMETAG_M2_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@
$(NAMETAG_M2_DIR)/bitmap_font.o: $(SHARED_DIR)/bitmap_font.c $(NAMETAG_HEADERS) | $(NAMETAG_M2_DIR)
	$(RP2350_CC) $(NAMETAG_RENDER_FLAGS) -fstack-usage -c $< -o $@
$(NAMETAG_M2_DIR)/nametag.o: $(SHARED_DIR)/nametag.c $(NAMETAG_HEADERS) | $(NAMETAG_M2_DIR)
	$(RP2350_CC) $(NAMETAG_RENDER_FLAGS) -fstack-usage -c $< -o $@
$(NAMETAG_M2_DIR)/frame_diff.o: $(NAMETAG_FRAME_C) $(NAMETAG_FRAME_H) $(SHARED_DIR)/bytes.h $(PLATFORM_DIR)/graphics.h | $(NAMETAG_M2_DIR)
	$(RP2350_CC) $(NAMETAG_RENDER_FLAGS) -fstack-usage -c $< -o $@
$(NAMETAG_M2_DIR)/nametag_fonts.o: $(NAMETAG_FONT_C) $(NAMETAG_FONT_H) $(NAMETAG_HEADERS) | $(NAMETAG_M2_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -I$(NAMETAG_ASSET_DIR) -c $< -o $@
$(NAMETAG_M2_DIR)/nametag_measure.o: $(SHARED_DIR)/nametag_measure.c $(SHARED_DIR)/nametag_measure.h $(SHARED_DIR)/usb_report.h $(SHARED_DIR)/bytes.h | $(NAMETAG_M2_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@
$(NAMETAG_M2_DIR)/usb_report.o: $(SHARED_DIR)/usb_report.c $(SHARED_DIR)/usb_report.h $(SHARED_DIR)/bytes.h | $(NAMETAG_M2_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@
$(NAMETAG_M2_DIR)/wifi_trace.o: $(RADIO_DIR)/wifi_trace.c $(RADIO_DIR)/wifi_trace.h | $(NAMETAG_M2_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@
$(NAMETAG_M2_DIR)/live_usb_control.o: src/usb/live_usb_control.c src/usb/live_usb.h $(SHARED_DIR)/nametag_measure.h $(SHARED_DIR)/bytes.h | $(NAMETAG_M2_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -DLIVE_USB_NAMETAG_M2 -c $< -o $@
$(NAMETAG_M2_DIR)/live_usb_hw.o: src/usb/live_usb_hw.c src/usb/live_usb_hw.h src/usb/live_usb.h $(SHARED_DIR)/nametag_measure.h | $(NAMETAG_M2_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -DLIVE_USB_NAMETAG_M2 -c $< -o $@
$(NAMETAG_M2_DIR)/nametag_m2.o: $(TOOLS_DIR)/nametag_m2.c $(NAMETAG_M2_INPUTS) $(NAMETAG_M2_DIR)/nametag_m2_build.h | $(NAMETAG_M2_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -DLIVE_USB_NAMETAG_M2 -I$(PLATFORM_DIR) -I$(NAMETAG_ASSET_DIR) -I$(NAMETAG_M2_DIR) -fstack-usage -c $< -o $@

$(NAMETAG_M2_DIR)/nametag_m2.elf: $(NAMETAG_M2_OBJS) $(ARCH_DIR)/memmap_report.ld
	$(RP2350_CC) $(RP2350_CFLAGS) -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(ARCH_DIR)/memmap_report.ld -Wl,-Map=$(NAMETAG_M2_DIR)/nametag_m2.map $(NAMETAG_M2_OBJS) -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

$(UF2_DIR)/nametag_m2.uf2: $(NAMETAG_M2_DIR)/nametag_m2.elf $(BIN2UF2) | $(UF2_DIR)
	$(RP2350_OBJCOPY) -O binary $< $(NAMETAG_M2_DIR)/nametag_m2.bin
	$(BIN2UF2) $(NAMETAG_M2_DIR)/nametag_m2.bin $@ $(FLASH_BASE) $(RP2350_FAMILY_ID)

$(HOST_TOOLS_DIR)/nametag_m2_host: $(HOST_TOOLS_SRC_DIR)/nametag_m2_host.c $(HOST_TOOLS_SRC_DIR)/usb_live_transport.h $(NAMETAG_M2_DIR)/nametag_m2_build.h $(SHARED_DIR)/nametag_measure.c $(SHARED_DIR)/usb_report.c $(TOOLS_DIR)/diagnostics_logic.c src/usb/live_usb.h | $(HOST_TOOLS_DIR)
	@pkg-config --exists libusb-1.0 || { echo 'M2 collector requires the existing libusb-1.0 host dependency'; exit 1; }
	$(HOST_CC) $(NAMETAG_HOST_FLAGS) -DLIVE_USB_NAMETAG_M2 -I$(NAMETAG_M2_DIR) \
		$$(pkg-config --cflags libusb-1.0) $(HOST_TOOLS_SRC_DIR)/nametag_m2_host.c \
		$(SHARED_DIR)/nametag_measure.c $(SHARED_DIR)/usb_report.c $(TOOLS_DIR)/diagnostics_logic.c \
		$$(pkg-config --libs libusb-1.0) -o $@

$(HOST_TOOLS_DIR)/nametag_m2_test: src/host/tests/nametag_m2_test.c $(SHARED_DIR)/nametag_measure.c $(SHARED_DIR)/nametag_measure.h \
		src/usb/live_usb_control.c src/usb/live_usb.h $(SHARED_DIR)/usb_report.c $(RADIO_DIR)/wifi_trace.c $(TOOLS_DIR)/diagnostics_logic.c | $(HOST_TOOLS_DIR)
	$(HOST_CC) $(NAMETAG_HOST_FLAGS) -O1 -g -fsanitize=address,undefined -DLIVE_USB_NAMETAG_M2 \
		src/host/tests/nametag_m2_test.c $(SHARED_DIR)/nametag_measure.c src/usb/live_usb_control.c \
		$(SHARED_DIR)/usb_report.c $(RADIO_DIR)/wifi_trace.c $(TOOLS_DIR)/diagnostics_logic.c -o $@

test-nametag-m2: $(HOST_TOOLS_DIR)/nametag_m2_test
	$(HOST_TOOLS_DIR)/nametag_m2_test
	node --test src/host/tests/nametag_m2_campaign_test.mjs

all: $(UF2S)
	@printf '%s\n' $(addprefix "UF2: ",$(UF2S))

tilt-maze: $(TILT_MAZE_UF2)
	@printf 'UF2: %s\n' $(TILT_MAZE_UF2)

test-tilt-maze: $(HOST_TOOLS_DIR)/tilt_maze_test
	$(HOST_TOOLS_DIR)/tilt_maze_test

$(FIRMWARE_DIR)/wifi_gspi.o $(FIRMWARE_DIR)/wifi_probe.o $(FIRMWARE_DIR)/wifi_bringup.o $(FIRMWARE_DIR)/wifi_bringup_view.o $(FIRMWARE_DIR)/live_usb_hw.o $(HOST_TOOLS_DIR)/wifi_bringup_test: $(RADIO_DIR)/wifi_trace.h $(SHARED_DIR)/usb_report.h

diagnostics: $(DIAGNOSTICS_UF2)
	@printf 'UF2: %s\n' $(DIAGNOSTICS_UF2)

wifi-bringup: $(WIFI_BRINGUP_UF2)
	@printf 'UF2: %s\n' $(WIFI_BRINGUP_UF2)

test-wifi-bringup: $(HOST_TOOLS_DIR)/wifi_bringup_test
	$(HOST_TOOLS_DIR)/wifi_bringup_test

usb-report-test: $(USB_REPORT_UF2) $(HOST_TOOLS_DIR)/usb_report_host
	@printf 'UF2: %s\n' $(USB_REPORT_UF2)

test-usb-report: usb-report-test $(HOST_TOOLS_DIR)/usb_report_test
	$(HOST_TOOLS_DIR)/usb_report_test
	bash -n src/host/tools/usb_report_run.sh
	bash src/host/tests/usb_report_run_test.sh

usb-live-report: $(USB_LIVE_UF2) $(HOST_TOOLS_DIR)/usb_live_host
	@printf 'UF2: %s\n' $(USB_LIVE_UF2)

test-live-usb: $(HOST_TOOLS_DIR)/live_usb_test
	$(HOST_TOOLS_DIR)/live_usb_test
	bash -n src/host/tools/usb_live_run.sh

test-nametag-control:
	node --test src/host/tests/nametag_control_test.mjs

nametag-assets: $(NAMETAG_FONT_STAMP)

$(HOST_TOOLS_DIR)/nametag_font_compile: $(HOST_TOOLS_SRC_DIR)/nametag_font_compile.c $(HOST_TOOLS_SRC_DIR)/font_platform.c $(HOST_TOOLS_SRC_DIR)/font_platform.h $(FONTRENDER_SOURCES) $(FONTRENDER_HEADERS) $(NAMETAG_HEADERS) $(SHARED_DIR)/bitmap_font.c | $(HOST_TOOLS_DIR)
	$(HOST_CC) $(NAMETAG_HOST_FLAGS) -DFR_RASTER_DISABLE_SIMD=1 -I$(FONTRENDER_DIR) \
		$(HOST_TOOLS_SRC_DIR)/nametag_font_compile.c $(HOST_TOOLS_SRC_DIR)/font_platform.c \
		$(FONTRENDER_SOURCES) $(SHARED_DIR)/bitmap_font.c -lm -o $@

$(HOST_TOOLS_DIR)/nametag_font_compile_sanitized: $(HOST_TOOLS_SRC_DIR)/nametag_font_compile.c $(HOST_TOOLS_SRC_DIR)/font_platform.c $(HOST_TOOLS_SRC_DIR)/font_platform.h $(FONTRENDER_SOURCES) $(FONTRENDER_HEADERS) $(NAMETAG_HEADERS) $(SHARED_DIR)/bitmap_font.c | $(HOST_TOOLS_DIR)
	$(HOST_CC) $(NAMETAG_HOST_FLAGS) -O1 -g -fsanitize=address,undefined -DFR_RASTER_DISABLE_SIMD=1 -I$(FONTRENDER_DIR) \
		$(HOST_TOOLS_SRC_DIR)/nametag_font_compile.c $(HOST_TOOLS_SRC_DIR)/font_platform.c \
		$(FONTRENDER_SOURCES) $(SHARED_DIR)/bitmap_font.c -lm -o $@

$(HOST_TOOLS_DIR)/font_core_test: src/host/tests/font_core_test.c $(HOST_TOOLS_SRC_DIR)/font_platform.c $(HOST_TOOLS_SRC_DIR)/font_platform.h $(FONTRENDER_SOURCES) $(FONTRENDER_HEADERS) | $(HOST_TOOLS_DIR)
	$(HOST_CC) $(NAMETAG_HOST_FLAGS) -O1 -g -fsanitize=address,undefined -DFR_RASTER_DISABLE_SIMD=1 -I$(FONTRENDER_DIR) \
		src/host/tests/font_core_test.c $(HOST_TOOLS_SRC_DIR)/font_platform.c $(FONTRENDER_SOURCES) -lm -o $@

$(NAMETAG_FONT_STAMP): $(HOST_TOOLS_DIR)/nametag_font_compile $(NAMETAG_FONT_FILES) vendor/cascadia-code/LICENSE $(NAMETAG_HEADERS)
	mkdir -p $(NAMETAG_ASSET_DIR)
	$(HOST_TOOLS_DIR)/nametag_font_compile $(NAMETAG_FONT_C) $(NAMETAG_FONT_H) $(NAMETAG_FONT_FILES) $(NAMETAG_COVERAGE) vendor/cascadia-code/LICENSE
	touch $@

$(NAMETAG_FONT_C) $(NAMETAG_FONT_H): $(NAMETAG_FONT_STAMP)
	@test -f $@ || { echo "Missing generated font output; remove $(NAMETAG_FONT_STAMP) and rebuild"; exit 1; }

$(NAMETAG_PREVIEW): $(HOST_TOOLS_SRC_DIR)/nametag_preview.c $(NAMETAG_SOURCES) $(NAMETAG_HEADERS) $(NAMETAG_FONT_C) $(NAMETAG_FONT_H) | $(HOST_TOOLS_DIR)
	$(HOST_CC) $(NAMETAG_HOST_FLAGS) -I$(NAMETAG_ASSET_DIR) \
		$(HOST_TOOLS_SRC_DIR)/nametag_preview.c $(NAMETAG_SOURCES) $(NAMETAG_FONT_C) -o $@

$(NAMETAG_TEST): src/host/tests/nametag_test.c $(NAMETAG_SOURCES) $(NAMETAG_HEADERS) $(NAMETAG_FRAME_C) $(NAMETAG_FRAME_H) $(NAMETAG_FONT_C) $(NAMETAG_FONT_H) | $(HOST_TOOLS_DIR)
	$(HOST_CC) $(NAMETAG_HOST_FLAGS) -O1 -g -fsanitize=address,undefined -I$(NAMETAG_ASSET_DIR) \
		src/host/tests/nametag_test.c $(NAMETAG_SOURCES) $(NAMETAG_FRAME_C) $(NAMETAG_FONT_C) -o $@

test-nametag: $(NAMETAG_TEST) $(HOST_TOOLS_DIR)/font_core_test $(HOST_TOOLS_DIR)/nametag_font_compile_sanitized
	$(NAMETAG_TEST)
	$(HOST_TOOLS_DIR)/font_core_test
	NAMETAG_GENERATOR=$(abspath $(HOST_TOOLS_DIR)/nametag_font_compile_sanitized) node --test src/host/tests/nametag_assets_test.mjs

nametag-stills: $(NAMETAG_PREVIEW)
	mkdir -p $(BUILD_DIR)/nametag/preview-$(NAMETAG_COVERAGE)
	@for variant in A B C; do for family in mono code; do for curve in natural crisp; do \
		for time in 0 5500 6000; do \
			$(NAMETAG_PREVIEW) --still $$variant $$time $$family $$curve \
				$(BUILD_DIR)/nametag/preview-$(NAMETAG_COVERAGE)/$$variant-$$family-$$curve-$$time.ppm || exit; \
		done; done; done; done
	$(NAMETAG_PREVIEW) --info

nametag-preview-binary: $(NAMETAG_PREVIEW)

nametag-preview:
	$(MAKE) NAMETAG_COVERAGE=8 nametag-preview-binary
	$(MAKE) NAMETAG_COVERAGE=4 nametag-preview-binary
	node $(HOST_TOOLS_SRC_DIR)/nametag_preview_server.mjs $(HOST_TOOLS_DIR)/nametag_preview-8 $(HOST_TOOLS_DIR)/nametag_preview-4

test-nametag-preview:
	$(MAKE) NAMETAG_COVERAGE=8 nametag-preview-binary
	$(MAKE) NAMETAG_COVERAGE=4 nametag-preview-binary
	NAMETAG_PREVIEW_8=$(abspath $(HOST_TOOLS_DIR)/nametag_preview-8) NAMETAG_PREVIEW_4=$(abspath $(HOST_TOOLS_DIR)/nametag_preview-4) \
		node --test src/host/tests/nametag_preview_test.mjs

$(NAMETAG_TARGET_DIR):
	mkdir -p $@

$(NAMETAG_TARGET_DIR)/bitmap_font.o: $(SHARED_DIR)/bitmap_font.c $(NAMETAG_HEADERS) | $(NAMETAG_TARGET_DIR)
	$(RP2350_CC) $(NAMETAG_RENDER_FLAGS) -fstack-usage -c $< -o $@

$(NAMETAG_TARGET_DIR)/nametag.o: $(SHARED_DIR)/nametag.c $(NAMETAG_HEADERS) | $(NAMETAG_TARGET_DIR)
	$(RP2350_CC) $(NAMETAG_RENDER_FLAGS) -fstack-usage -c $< -o $@
$(NAMETAG_TARGET_DIR)/frame_diff.o: $(NAMETAG_FRAME_C) $(NAMETAG_FRAME_H) $(SHARED_DIR)/bytes.h $(PLATFORM_DIR)/graphics.h | $(NAMETAG_TARGET_DIR)
	$(RP2350_CC) $(NAMETAG_RENDER_FLAGS) -fstack-usage -c $< -o $@

$(NAMETAG_TARGET_DIR)/graphics.o: $(PLATFORM_DIR)/graphics.c $(PLATFORM_DIR)/graphics.h | $(NAMETAG_TARGET_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(NAMETAG_TARGET_DIR)/nametag_fonts.o: $(NAMETAG_FONT_C) $(NAMETAG_FONT_H) $(NAMETAG_HEADERS) | $(NAMETAG_TARGET_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -I$(NAMETAG_ASSET_DIR) -c $< -o $@

$(NAMETAG_TARGET_DIR)/nametag_target.o: src/host/tests/nametag_target.c $(NAMETAG_FONT_H) $(NAMETAG_HEADERS) $(NAMETAG_FRAME_H) | $(NAMETAG_TARGET_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -I$(NAMETAG_ASSET_DIR) -c $< -o $@

$(NAMETAG_TARGET_DIR)/nametag_target.elf: $(NAMETAG_TARGET_OBJS) $(ARCH_DIR)/memmap_flash.ld
	$(RP2350_CC) $(RP2350_LDFLAGS) -Wl,-e,nametag_target_main -Wl,--undefined=nametag_target_sizes \
		-Wl,-Map=$(NAMETAG_TARGET_DIR)/nametag_target.map $(NAMETAG_TARGET_OBJS) -o $@
	@test -z "$$($(RP2350_NM) -u $@)"

test-nametag-target: $(NAMETAG_TARGET_DIR)/nametag_target.elf
	$(RP2350_SIZE) $<
	@echo "Link-only Cortex-M33/no-libc proof; not a bootable or hardware-tested image."

usb-wifi-firmware: $(UF2_DIR)/usb_wifi_firmware.uf2 $(HOST_TOOLS_DIR)/usb_wifi_firmware_host
	@printf 'UF2: %s (vendor radio firmware/configuration data included)\n' $(UF2_DIR)/usb_wifi_firmware.uf2

test-wifi-firmware: $(HOST_TOOLS_DIR)/wifi_firmware_test $(HOST_TOOLS_DIR)/live_usb_firmware_test
	$(HOST_TOOLS_DIR)/wifi_firmware_test
	$(HOST_TOOLS_DIR)/live_usb_firmware_test

usb-wifi-link: $(UF2_DIR)/usb_wifi_link.uf2 $(HOST_TOOLS_DIR)/usb_wifi_link_host
	@printf 'UF2: %s (paired AP/station experiment; vendor radio data included)\n' $(UF2_DIR)/usb_wifi_link.uf2

usb-wifi-udp: $(UF2_DIR)/usb_wifi_udp.uf2 $(HOST_TOOLS_DIR)/usb_wifi_udp_host
	@printf 'UF2: %s (static-IP UDP echo; vendor radio data included)\n' $(UF2_DIR)/usb_wifi_udp.uf2

test-wifi-udp: $(HOST_TOOLS_DIR)/wifi_udp_test $(HOST_TOOLS_DIR)/wifi_udp_report_test $(HOST_TOOLS_DIR)/wifi_udp_usb_test $(HOST_TOOLS_DIR)/wifi_link_view_test $(HOST_TOOLS_DIR)/bytes_test
	$(HOST_TOOLS_DIR)/wifi_udp_test
	$(HOST_TOOLS_DIR)/wifi_udp_report_test
	$(HOST_TOOLS_DIR)/wifi_udp_usb_test
	$(HOST_TOOLS_DIR)/wifi_link_view_test
	$(HOST_TOOLS_DIR)/bytes_test
	bash -n $(HOST_TOOLS_SRC_DIR)/usb_wifi_pair_run.sh

$(HOST_TOOLS_DIR)/wifi_link_view_test: src/host/tests/wifi_link_view_test.c $(TOOLS_DIR)/wifi_link_view.h $(PLATFORM_DIR)/graphics.c $(PLATFORM_DIR)/graphics.h $(PLATFORM_DIR)/display.h $(SHARED_DIR)/bytes.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
		src/host/tests/wifi_link_view_test.c $(PLATFORM_DIR)/graphics.c -o $@

$(HOST_TOOLS_DIR)/bytes_test: badge-compiler/tests/fixtures/bytes_fixture.c $(SHARED_DIR)/bytes.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined $< -o $@

$(USB_UDP_BUILD_H): $(USB_UDP_INPUTS) $(WIFI_ASSET_FILES) Makefile | $(FIRMWARE_DIR)
	@id=$$(shasum -a 256 $(USB_UDP_INPUTS) $(WIFI_ASSET_FILES) Makefile | shasum -a 256 | cut -c1-8); \
	printf '#ifndef USB_UDP_BUILD_H\n#define USB_UDP_BUILD_H\n#define USB_REPORT_BUILD_ID 0x%su\n#endif\n' "$$id" > $@

$(FIRMWARE_DIR)/usb_wifi_udp.o: $(TOOLS_DIR)/usb_wifi_link.c $(USB_UDP_BUILD_H) $(USB_UDP_INPUTS) | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -DLIVE_USB_LINK -DLIVE_USB_UDP -I$(PLATFORM_DIR) -I$(FIRMWARE_DIR) -c $< -o $@

$(FIRMWARE_DIR)/live_usb_udp_control.o: src/usb/live_usb_control.c src/usb/live_usb.h $(WIFI_LINK_HEADERS) $(WIFI_UDP_HEADERS) $(WIFI_USB_HEADERS) $(SHARED_DIR)/usb_report.h $(SHARED_DIR)/stdint.h $(SHARED_DIR)/bytes.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -DLIVE_USB_LINK -DLIVE_USB_UDP -c $< -o $@

$(FIRMWARE_DIR)/wifi_udp.o: $(RADIO_DIR)/wifi_udp.c $(WIFI_UDP_HEADERS) $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/wifi_udp_report.o: $(RADIO_DIR)/wifi_udp_report.c $(WIFI_UDP_HEADERS) $(SHARED_DIR)/usb_report.h $(TOOLS_DIR)/diagnostics_logic.h $(SHARED_DIR)/stdint.h $(SHARED_DIR)/bytes.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/usb_wifi_udp.elf: $(COMMON_FIRMWARE_OBJS) $(USB_UDP_OBJS) $(ARCH_DIR)/memmap_report.ld
	$(RP2350_CC) $(RP2350_CFLAGS) -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(ARCH_DIR)/memmap_report.ld $(COMMON_FIRMWARE_OBJS) $(USB_UDP_OBJS) \
		-Wl,-Map=$(FIRMWARE_DIR)/usb_wifi_udp.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

$(HOST_TOOLS_DIR)/usb_wifi_udp_host: $(HOST_TOOLS_SRC_DIR)/usb_live_host.c src/usb/live_usb.h $(SHARED_DIR)/usb_report.c $(SHARED_DIR)/usb_report.h $(TOOLS_DIR)/diagnostics_logic.c $(RADIO_DIR)/wifi_trace.c $(WIFI_LINK_SOURCES) $(WIFI_LINK_HEADERS) $(WIFI_UDP_SOURCES) $(WIFI_UDP_HEADERS) $(USB_UDP_BUILD_H) | $(HOST_TOOLS_DIR)
	@pkg-config --exists libusb-1.0 || { echo "UDP collector requires host libusb-1.0"; exit 1; }
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O2 -DLIVE_USB_LINK -DLIVE_USB_UDP -I$(FIRMWARE_DIR) \
		$$(pkg-config --cflags libusb-1.0) $(HOST_TOOLS_SRC_DIR)/usb_live_host.c \
		$(SHARED_DIR)/usb_report.c $(TOOLS_DIR)/diagnostics_logic.c $(RADIO_DIR)/wifi_trace.c \
		$(WIFI_LINK_SOURCES) $(WIFI_UDP_SOURCES) $$(pkg-config --libs libusb-1.0) -o $@

$(HOST_TOOLS_DIR)/wifi_udp_test: src/host/tests/wifi_udp_test.c $(RADIO_DIR)/wifi_udp.c $(WIFI_UDP_HEADERS) | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
		src/host/tests/wifi_udp_test.c $(RADIO_DIR)/wifi_udp.c -o $@

$(HOST_TOOLS_DIR)/wifi_udp_report_test: src/host/tests/wifi_udp_report_test.c $(RADIO_DIR)/wifi_udp_report.c $(WIFI_UDP_HEADERS) $(SHARED_DIR)/usb_report.c $(SHARED_DIR)/usb_report.h $(TOOLS_DIR)/diagnostics_logic.c $(SHARED_DIR)/bytes.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
		src/host/tests/wifi_udp_report_test.c $(RADIO_DIR)/wifi_udp_report.c \
		$(SHARED_DIR)/usb_report.c $(TOOLS_DIR)/diagnostics_logic.c -o $@

$(HOST_TOOLS_DIR)/wifi_udp_usb_test: src/host/tests/wifi_link_usb_test.c src/usb/live_usb_control.c src/usb/live_usb.h $(RADIO_DIR)/wifi_link_report.c $(RADIO_DIR)/wifi_udp_report.c $(WIFI_LINK_HEADERS) $(WIFI_UDP_HEADERS) $(WIFI_USB_HEADERS) $(RADIO_DIR)/wifi_trace.c $(SHARED_DIR)/usb_report.c $(SHARED_DIR)/usb_report.h $(TOOLS_DIR)/diagnostics_logic.c $(SHARED_DIR)/bytes.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -DLIVE_USB_LINK -DLIVE_USB_UDP \
		src/host/tests/wifi_link_usb_test.c src/usb/live_usb_control.c $(RADIO_DIR)/wifi_link_report.c \
		$(RADIO_DIR)/wifi_udp_report.c $(RADIO_DIR)/wifi_trace.c $(SHARED_DIR)/usb_report.c $(TOOLS_DIR)/diagnostics_logic.c -o $@

test-wifi-link: $(HOST_TOOLS_DIR)/wifi_link_test $(HOST_TOOLS_DIR)/wifi_link_usb_test
	$(HOST_TOOLS_DIR)/wifi_link_test
	$(HOST_TOOLS_DIR)/wifi_link_usb_test
	bash -n $(HOST_TOOLS_SRC_DIR)/usb_wifi_pair_run.sh

$(USB_LINK_BUILD_H): $(USB_LINK_INPUTS) $(WIFI_ASSET_FILES) Makefile | $(FIRMWARE_DIR)
	@id=$$(shasum -a 256 $(USB_LINK_INPUTS) $(WIFI_ASSET_FILES) Makefile | shasum -a 256 | cut -c1-8); \
	printf '#ifndef USB_LINK_BUILD_H\n#define USB_LINK_BUILD_H\n#define USB_REPORT_BUILD_ID 0x%su\n#endif\n' "$$id" > $@

$(FIRMWARE_DIR)/usb_wifi_link.o: $(TOOLS_DIR)/usb_wifi_link.c $(USB_LINK_BUILD_H) $(USB_LINK_INPUTS) | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -DLIVE_USB_LINK -I$(PLATFORM_DIR) -I$(FIRMWARE_DIR) -c $< -o $@

$(FIRMWARE_DIR)/live_usb_link_control.o: src/usb/live_usb_control.c src/usb/live_usb.h $(WIFI_LINK_HEADERS) $(WIFI_USB_HEADERS) $(SHARED_DIR)/usb_report.h $(SHARED_DIR)/stdint.h $(SHARED_DIR)/bytes.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -DLIVE_USB_LINK -c $< -o $@

$(FIRMWARE_DIR)/wifi_link.o: $(RADIO_DIR)/wifi_link.c $(WIFI_LINK_HEADERS) $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/wifi_link_report.o: $(RADIO_DIR)/wifi_link_report.c $(WIFI_LINK_HEADERS) $(SHARED_DIR)/usb_report.h $(TOOLS_DIR)/diagnostics_logic.h $(SHARED_DIR)/stdint.h $(SHARED_DIR)/bytes.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/usb_wifi_link.elf: $(COMMON_FIRMWARE_OBJS) $(USB_LINK_OBJS) $(ARCH_DIR)/memmap_report.ld
	$(RP2350_CC) $(RP2350_CFLAGS) -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(ARCH_DIR)/memmap_report.ld $(COMMON_FIRMWARE_OBJS) $(USB_LINK_OBJS) \
		-Wl,-Map=$(FIRMWARE_DIR)/usb_wifi_link.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

$(HOST_TOOLS_DIR)/usb_wifi_link_host: $(HOST_TOOLS_SRC_DIR)/usb_live_host.c src/usb/live_usb.h $(SHARED_DIR)/usb_report.c $(SHARED_DIR)/usb_report.h $(TOOLS_DIR)/diagnostics_logic.c $(RADIO_DIR)/wifi_trace.c $(WIFI_LINK_SOURCES) $(WIFI_LINK_HEADERS) $(USB_LINK_BUILD_H) | $(HOST_TOOLS_DIR)
	@pkg-config --exists libusb-1.0 || { echo "Paired collector requires host libusb-1.0"; exit 1; }
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O2 -DLIVE_USB_LINK -I$(FIRMWARE_DIR) \
		$$(pkg-config --cflags libusb-1.0) $(HOST_TOOLS_SRC_DIR)/usb_live_host.c \
		$(SHARED_DIR)/usb_report.c $(TOOLS_DIR)/diagnostics_logic.c $(RADIO_DIR)/wifi_trace.c $(WIFI_LINK_SOURCES) \
		$$(pkg-config --libs libusb-1.0) -o $@

$(HOST_TOOLS_DIR)/wifi_link_test: src/host/tests/wifi_link_test.c $(RADIO_DIR)/wifi_link.c $(WIFI_LINK_HEADERS) | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
		src/host/tests/wifi_link_test.c $(RADIO_DIR)/wifi_link.c -o $@

$(HOST_TOOLS_DIR)/wifi_link_usb_test: src/host/tests/wifi_link_usb_test.c src/usb/live_usb_control.c src/usb/live_usb.h $(RADIO_DIR)/wifi_link_report.c $(WIFI_LINK_HEADERS) $(WIFI_USB_HEADERS) $(RADIO_DIR)/wifi_trace.c $(SHARED_DIR)/usb_report.c $(SHARED_DIR)/usb_report.h $(TOOLS_DIR)/diagnostics_logic.c $(SHARED_DIR)/bytes.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -DLIVE_USB_LINK \
		src/host/tests/wifi_link_usb_test.c src/usb/live_usb_control.c $(RADIO_DIR)/wifi_link_report.c \
		$(RADIO_DIR)/wifi_trace.c $(SHARED_DIR)/usb_report.c $(TOOLS_DIR)/diagnostics_logic.c -o $@

$(HOST_TOOLS_DIR)/wifi_firmware_test: src/host/tests/wifi_firmware_test.c $(WIFI_FW_SOURCES) $(WIFI_FW_HEADERS) $(SHARED_DIR)/usb_report.c $(SHARED_DIR)/usb_report.h $(TOOLS_DIR)/diagnostics_logic.c $(TOOLS_DIR)/diagnostics_logic.h $(SHARED_DIR)/bytes.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
		src/host/tests/wifi_firmware_test.c $(WIFI_FW_SOURCES) $(SHARED_DIR)/usb_report.c $(TOOLS_DIR)/diagnostics_logic.c -o $@

$(HOST_TOOLS_DIR)/live_usb_firmware_test: src/host/tests/live_usb_test.c src/usb/live_usb_control.c src/usb/live_usb.h $(SHARED_DIR)/usb_report.c $(SHARED_DIR)/usb_report.h $(TOOLS_DIR)/diagnostics_logic.c $(TOOLS_DIR)/diagnostics_logic.h $(WIFI_USB_HOST_SOURCES) $(WIFI_USB_HEADERS) $(WIFI_FW_HEADERS) $(SHARED_DIR)/bytes.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -DLIVE_USB_FIRMWARE \
		src/host/tests/live_usb_test.c src/usb/live_usb_control.c $(SHARED_DIR)/usb_report.c \
		$(TOOLS_DIR)/diagnostics_logic.c $(WIFI_USB_HOST_SOURCES) -o $@

$(HOST_TOOLS_DIR)/wifi_pack_assets: $(HOST_TOOLS_SRC_DIR)/wifi_pack_assets.c $(WIFI_ASSET_INPUTS) | $(HOST_TOOLS_DIR)
	@printf '%s  %s\n' \
		120bb63db1c5cf42cd44cf9a057d6f14601038a9413badf5a3b737f6552bc9ea $(WIFI_ASSET_SOURCE_DIR)/w43439A0_7_95_49_00_combined.h \
		ddc82b00667643f55d7687bba1b5e4028f24717ec8883f05bd7d435f4c9c01ad $(WIFI_ASSET_SOURCE_DIR)/wifi_nvram_43439.h | shasum -a 256 -c -
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O2 -I$(WIFI_ASSET_SOURCE_DIR) $< -o $@

$(WIFI_ASSET_DIR)/wifi_assets_generated.h: $(HOST_TOOLS_DIR)/wifi_pack_assets $(WIFI_ASSET_INPUTS)
	mkdir -p $(WIFI_ASSET_DIR)
	$(HOST_TOOLS_DIR)/wifi_pack_assets $(WIFI_ASSET_DIR)

$(WIFI_ASSET_DIR)/wifi_firmware.bin $(WIFI_ASSET_DIR)/wifi_nvram.bin $(WIFI_ASSET_DIR)/wifi_clm.bin: $(WIFI_ASSET_DIR)/wifi_assets_generated.h
	@test -f $@ || $(HOST_TOOLS_DIR)/wifi_pack_assets $(WIFI_ASSET_DIR)

$(USB_WIFI_BUILD_H): $(USB_WIFI_INPUTS) $(WIFI_ASSET_FILES) Makefile | $(FIRMWARE_DIR)
	@id=$$(shasum -a 256 $(USB_WIFI_INPUTS) $(WIFI_ASSET_FILES) Makefile | shasum -a 256 | cut -c1-8); \
	printf '#ifndef USB_WIFI_BUILD_H\n#define USB_WIFI_BUILD_H\n#define USB_REPORT_BUILD_ID 0x%su\n#endif\n' "$$id" > $@

$(FIRMWARE_DIR)/usb_wifi_firmware.o: $(TOOLS_DIR)/usb_live_report.c $(USB_WIFI_BUILD_H) $(USB_WIFI_INPUTS) | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -DLIVE_USB_FIRMWARE -I$(PLATFORM_DIR) -I$(FIRMWARE_DIR) -c $< -o $@

$(FIRMWARE_DIR)/live_usb_firmware_control.o: src/usb/live_usb_control.c src/usb/live_usb.h $(WIFI_FW_HEADERS) $(WIFI_USB_HEADERS) $(SHARED_DIR)/usb_report.h $(SHARED_DIR)/stdint.h $(SHARED_DIR)/bytes.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -DLIVE_USB_FIRMWARE -c $< -o $@

$(FIRMWARE_DIR)/wifi_firmware.o: $(RADIO_DIR)/wifi_firmware.c $(WIFI_FW_HEADERS) $(WIFI_USB_HEADERS) $(TOOLS_DIR)/diagnostics_logic.h $(SHARED_DIR)/stdint.h $(SHARED_DIR)/bytes.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/wifi_control.o: $(RADIO_DIR)/wifi_control.c $(RADIO_DIR)/wifi_control.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/wifi_assets.o: $(RADIO_DIR)/wifi_assets.c $(RADIO_DIR)/wifi_assets.h $(WIFI_FW_HEADERS) $(WIFI_ASSET_DIR)/wifi_assets_generated.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -I$(WIFI_ASSET_DIR) -c $< -o $@

$(FIRMWARE_DIR)/wifi_assets_blob.o: $(RADIO_DIR)/wifi_assets.S $(WIFI_ASSET_FILES) | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_ASFLAGS) -I$(WIFI_ASSET_DIR) -DWIFI_FIRMWARE_BIN='"$(abspath $(WIFI_ASSET_DIR)/wifi_firmware.bin)"' \
		-DWIFI_NVRAM_BIN='"$(abspath $(WIFI_ASSET_DIR)/wifi_nvram.bin)"' \
		-DWIFI_CLM_BIN='"$(abspath $(WIFI_ASSET_DIR)/wifi_clm.bin)"' -c $< -o $@

$(FIRMWARE_DIR)/usb_wifi_firmware.elf: $(COMMON_FIRMWARE_OBJS) $(USB_WIFI_OBJS) $(ARCH_DIR)/memmap_report.ld
	$(RP2350_CC) $(RP2350_CFLAGS) -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(ARCH_DIR)/memmap_report.ld $(COMMON_FIRMWARE_OBJS) $(USB_WIFI_OBJS) \
		-Wl,-Map=$(FIRMWARE_DIR)/usb_wifi_firmware.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

$(HOST_TOOLS_DIR)/usb_wifi_firmware_host: $(HOST_TOOLS_SRC_DIR)/usb_live_host.c src/usb/live_usb.h $(SHARED_DIR)/usb_report.c $(SHARED_DIR)/usb_report.h $(TOOLS_DIR)/diagnostics_logic.c $(WIFI_USB_HOST_SOURCES) $(WIFI_USB_HEADERS) $(WIFI_FW_SOURCES) $(WIFI_FW_HEADERS) $(USB_WIFI_BUILD_H) | $(HOST_TOOLS_DIR)
	@pkg-config --exists libusb-1.0 || { echo "Host live-report collector requires libusb-1.0"; exit 1; }
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O2 -DLIVE_USB_FIRMWARE -I$(FIRMWARE_DIR) \
		$$(pkg-config --cflags libusb-1.0) $(HOST_TOOLS_SRC_DIR)/usb_live_host.c \
		$(SHARED_DIR)/usb_report.c $(TOOLS_DIR)/diagnostics_logic.c $(WIFI_USB_HOST_SOURCES) $(WIFI_FW_SOURCES) \
		$$(pkg-config --libs libusb-1.0) -o $@

$(USB_LIVE_BUILD_H): $(USB_LIVE_BUILD_INPUTS) Makefile | $(FIRMWARE_DIR)
	@id=$$(shasum -a 256 $(USB_LIVE_BUILD_INPUTS) Makefile | shasum -a 256 | cut -c1-8); \
	printf '#ifndef USB_LIVE_BUILD_H\n#define USB_LIVE_BUILD_H\n#define USB_REPORT_BUILD_ID 0x%su\n#endif\n' "$$id" > $@

$(USB_LIVE_ELF): $(COMMON_FIRMWARE_OBJS) $(USB_LIVE_OBJS) $(ARCH_DIR)/memmap_report.ld
	$(RP2350_CC) $(RP2350_CFLAGS) \
		--target=arm-none-eabi -mcpu=cortex-m33 -mthumb \
		-nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(ARCH_DIR)/memmap_report.ld $(COMMON_FIRMWARE_OBJS) $(USB_LIVE_OBJS) \
		-Wl,-Map=$(FIRMWARE_DIR)/usb_live_report.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

$(FIRMWARE_DIR)/live_usb_control.o: src/usb/live_usb_control.c src/usb/live_usb.h $(SHARED_DIR)/usb_report.h $(WIFI_USB_HEADERS) $(SHARED_DIR)/stdint.h $(SHARED_DIR)/bytes.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/live_usb_hw.o: src/usb/live_usb_hw.c src/usb/live_usb_hw.h src/usb/live_usb.h $(SHARED_DIR)/usb_report.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/wifi_usb_report.o: $(RADIO_DIR)/wifi_usb_report.c $(WIFI_USB_HEADERS) $(SHARED_DIR)/usb_report.h $(TOOLS_DIR)/diagnostics_logic.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/wifi_trace.o: $(RADIO_DIR)/wifi_trace.c $(RADIO_DIR)/wifi_trace.h $(SHARED_DIR)/usb_report.h $(TOOLS_DIR)/diagnostics_logic.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/usb_live_report.o: $(TOOLS_DIR)/usb_live_report.c src/usb/live_usb_hw.h src/usb/live_usb.h $(SHARED_DIR)/usb_report.h $(ARCH_DIR)/bootrom_reboot.h $(PLATFORM_DIR)/display.h $(PLATFORM_DIR)/graphics.h $(PLATFORM_DIR)/wifi_board.h $(WIFI_USB_HEADERS) $(TOOLS_DIR)/diagnostics_logic.h $(USB_LIVE_BUILD_H) $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -I$(PLATFORM_DIR) -I$(FIRMWARE_DIR) -c $< -o $@

$(HOST_TOOLS_DIR)/usb_live_host: $(HOST_TOOLS_SRC_DIR)/usb_live_host.c src/usb/live_usb.h $(SHARED_DIR)/usb_report.c $(SHARED_DIR)/usb_report.h $(TOOLS_DIR)/diagnostics_logic.c $(TOOLS_DIR)/diagnostics_logic.h $(WIFI_USB_HOST_SOURCES) $(WIFI_USB_HEADERS) $(USB_LIVE_BUILD_H) | $(HOST_TOOLS_DIR)
	@pkg-config --exists libusb-1.0 || { echo "Host live-report collector requires libusb-1.0"; exit 1; }
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O2 -I$(FIRMWARE_DIR) \
		$$(pkg-config --cflags libusb-1.0) $(HOST_TOOLS_SRC_DIR)/usb_live_host.c \
		$(SHARED_DIR)/usb_report.c $(TOOLS_DIR)/diagnostics_logic.c $(WIFI_USB_HOST_SOURCES) \
		$$(pkg-config --libs libusb-1.0) -o $@

$(HOST_TOOLS_DIR)/live_usb_test: src/host/tests/live_usb_test.c src/usb/live_usb_control.c src/usb/live_usb.h $(SHARED_DIR)/usb_report.c $(SHARED_DIR)/usb_report.h $(TOOLS_DIR)/diagnostics_logic.c $(TOOLS_DIR)/diagnostics_logic.h $(WIFI_USB_HOST_SOURCES) $(WIFI_USB_HEADERS) $(SHARED_DIR)/bytes.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
		src/host/tests/live_usb_test.c src/usb/live_usb_control.c \
		$(SHARED_DIR)/usb_report.c $(TOOLS_DIR)/diagnostics_logic.c $(WIFI_USB_HOST_SOURCES) -o $@

picotool-usb:
	@test -f "$(PICOTOOL_SRC)/CMakeLists.txt" || { echo "Set PICOTOOL_SRC to an existing picotool checkout"; exit 1; }
	@pkg-config --exists libusb-1.0 || { echo "USB picotool requires host libusb-1.0"; exit 1; }
	cmake -S "$(PICOTOOL_SRC)" -B $(PICOTOOL_USB_BUILD_DIR) -G Ninja \
		-DPICO_SDK_PATH=$(abspath $(PICO_SDK_DIR)) -DPICOTOOL_NO_LIBUSB=OFF
	cmake --build $(PICOTOOL_USB_BUILD_DIR) --parallel 4
	$(PICOTOOL_USB_BUILD_DIR)/picotool help save >/dev/null

$(USB_REPORT_BUILD_H): $(USB_REPORT_BUILD_INPUTS) Makefile | $(FIRMWARE_DIR)
	@id=$$(shasum -a 256 $(USB_REPORT_BUILD_INPUTS) Makefile | shasum -a 256 | cut -c1-8); \
	printf '#ifndef USB_REPORT_BUILD_H\n#define USB_REPORT_BUILD_H\n#define USB_REPORT_BUILD_ID 0x%su\n#endif\n' "$$id" > $@

$(USB_REPORT_ELF): $(COMMON_FIRMWARE_OBJS) $(USB_REPORT_OBJS) $(ARCH_DIR)/memmap_report.ld
	$(RP2350_CC) $(RP2350_CFLAGS) \
		--target=arm-none-eabi -mcpu=cortex-m33 -mthumb \
		-nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(ARCH_DIR)/memmap_report.ld $(COMMON_FIRMWARE_OBJS) $(USB_REPORT_OBJS) \
		-Wl,-Map=$(FIRMWARE_DIR)/usb_report_test.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

.PHONY: compiler-test test-compiler-test
.PHONY: test-display-timing
$(HOST_TOOLS_DIR)/display_timing_test: src/host/tests/display_timing_test.c $(PLATFORM_DIR)/display.c $(PLATFORM_DIR)/display.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined $< -o $@
test-display-timing: $(HOST_TOOLS_DIR)/display_timing_test
	./$(HOST_TOOLS_DIR)/display_timing_test
compiler-test: $(UF2_DIR)/compiler_test.uf2 $(HOST_TOOLS_DIR)/compiler_test_host

$(COMPILER_TEST_BUILD_H): $(COMPILER_TEST_INPUTS) force | $(FIRMWARE_DIR)
	@id=$$({ shasum -a 256 $(COMPILER_TEST_INPUTS); printf 'timing=%s\n' "$(COMPILER_TEST_FLAGS)"; } | shasum -a 256 | cut -c1-8); \
	printf '#ifndef COMPILER_TEST_BUILD_H\n#define COMPILER_TEST_BUILD_H\n#define COMPILER_TEST_BUILD_ID 0x%su\n#endif\n' "$$id" > $@

$(FIRMWARE_DIR)/compiler_%.o: $(COMPILER_TEST_DIR)/%.c $(COMPILER_TEST_DIR)/suite.h $(COMPILER_TEST_INPUTS) $(COMPILER_TEST_BUILD_H) | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) $(COMPILER_TEST_FLAGS) -DLIVE_USB_COMPILER -I$(FIRMWARE_DIR) -c $< -o $@

$(FIRMWARE_DIR)/compiler_usb_control.o: src/usb/live_usb_control.c $(COMPILER_TEST_INPUTS) | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) $(COMPILER_TEST_FLAGS) -DLIVE_USB_COMPILER -c $< -o $@

$(HOST_TOOLS_DIR)/compiler_test_host: src/host/tools/compiler_test_host.c \
	src/host/tools/usb_live_transport.h $(COMPILER_TEST_BUILD_H) $(COMPILER_TEST_DIR)/suite.h \
	$(SHARED_DIR)/usb_report.c $(SHARED_DIR)/usb_report.h $(TOOLS_DIR)/diagnostics_logic.c \
	$(TOOLS_DIR)/diagnostics_logic.h src/usb/live_usb.h $(SHARED_DIR)/bytes.h | $(HOST_TOOLS_DIR)
	@pkg-config --exists libusb-1.0 || { echo "Compiler collector requires host libusb-1.0"; exit 1; }
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O2 $(COMPILER_TEST_FLAGS) -DLIVE_USB_COMPILER -I$(FIRMWARE_DIR) \
		$$(pkg-config --cflags libusb-1.0) src/host/tools/compiler_test_host.c \
		$(SHARED_DIR)/usb_report.c $(TOOLS_DIR)/diagnostics_logic.c $$(pkg-config --libs libusb-1.0) -o $@

$(FIRMWARE_DIR)/compiler_test.elf: $(COMMON_FIRMWARE_OBJS) $(COMPILER_TEST_OBJS) $(COMPILER_TEST_LINKER)
	$(RP2350_CC) $(RP2350_CFLAGS) -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(COMPILER_TEST_LINKER) $(COMMON_FIRMWARE_OBJS) $(COMPILER_TEST_OBJS) \
		-Wl,-Map=$(FIRMWARE_DIR)/compiler_test.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

$(HOST_TOOLS_DIR)/compiler_suite_test: src/host/tests/compiler_suite_test.c $(COMPILER_TEST_SOURCES) \
	$(COMPILER_TEST_DIR)/suite.h $(COMPILER_TEST_DIR)/kernels.h $(PLATFORM_DIR)/graphics.c $(PLATFORM_DIR)/graphics.h \
	$(SHARED_DIR)/usb_report.c $(SHARED_DIR)/usb_report.h $(SHARED_DIR)/bytes.h \
	$(TOOLS_DIR)/diagnostics_logic.c $(TOOLS_DIR)/diagnostics_logic.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined $(COMPILER_TEST_FLAGS) \
		src/host/tests/compiler_suite_test.c $(COMPILER_TEST_SOURCES) $(PLATFORM_DIR)/graphics.c \
		$(SHARED_DIR)/usb_report.c $(TOOLS_DIR)/diagnostics_logic.c -o $@

$(HOST_TOOLS_DIR)/compiler_usb_test: src/host/tests/compiler_usb_test.c $(COMPILER_TEST_SOURCES) \
	$(COMPILER_TEST_DIR)/suite.h $(COMPILER_TEST_DIR)/kernels.h $(PLATFORM_DIR)/graphics.c src/usb/live_usb_control.c src/usb/live_usb.h \
	$(SHARED_DIR)/usb_report.c $(SHARED_DIR)/usb_report.h $(SHARED_DIR)/bytes.h \
	$(TOOLS_DIR)/diagnostics_logic.c $(TOOLS_DIR)/diagnostics_logic.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined $(COMPILER_TEST_FLAGS) -DLIVE_USB_COMPILER \
		src/host/tests/compiler_usb_test.c $(COMPILER_TEST_SOURCES) $(PLATFORM_DIR)/graphics.c \
		src/usb/live_usb_control.c $(SHARED_DIR)/usb_report.c $(TOOLS_DIR)/diagnostics_logic.c -o $@

test-compiler-test: $(HOST_TOOLS_DIR)/compiler_suite_test $(HOST_TOOLS_DIR)/compiler_usb_test
	$(HOST_TOOLS_DIR)/compiler_suite_test
	$(HOST_TOOLS_DIR)/compiler_usb_test
	node --test badge-compiler/tests/compiler-compare.test.mjs

$(addprefix $(HOST_TOOLS_DIR)/,usb_live_host usb_wifi_firmware_host usb_wifi_link_host usb_wifi_udp_host): src/host/tools/usb_live_transport.h

$(FIRMWARE_DIR)/usb_report.o: $(SHARED_DIR)/usb_report.c $(SHARED_DIR)/usb_report.h $(TOOLS_DIR)/diagnostics_logic.h $(SHARED_DIR)/stdint.h $(SHARED_DIR)/bytes.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/bootrom_reboot.o: $(ARCH_DIR)/bootrom_reboot.c $(ARCH_DIR)/bootrom_reboot.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/usb_report_test.o: $(TOOLS_DIR)/usb_report_test.c $(SHARED_DIR)/usb_report.h $(ARCH_DIR)/bootrom_reboot.h $(PLATFORM_DIR)/display.h $(PLATFORM_DIR)/graphics.h $(TOOLS_DIR)/diagnostics_logic.h $(USB_REPORT_BUILD_H) $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -I$(PLATFORM_DIR) -I$(FIRMWARE_DIR) -c $< -o $@

$(HOST_TOOLS_DIR)/usb_report_host: $(HOST_TOOLS_SRC_DIR)/usb_report_host.c $(SHARED_DIR)/usb_report.c $(SHARED_DIR)/usb_report.h $(TOOLS_DIR)/diagnostics_logic.c $(TOOLS_DIR)/diagnostics_logic.h $(USB_REPORT_BUILD_H) | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O2 -I$(FIRMWARE_DIR) \
		$(HOST_TOOLS_SRC_DIR)/usb_report_host.c $(SHARED_DIR)/usb_report.c $(TOOLS_DIR)/diagnostics_logic.c -o $@

$(HOST_TOOLS_DIR)/usb_report_test: src/host/tests/usb_report_test.c $(SHARED_DIR)/usb_report.c $(SHARED_DIR)/usb_report.h $(TOOLS_DIR)/diagnostics_logic.c $(TOOLS_DIR)/diagnostics_logic.h $(SHARED_DIR)/bytes.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
		src/host/tests/usb_report_test.c $(SHARED_DIR)/usb_report.c $(TOOLS_DIR)/diagnostics_logic.c -o $@

$(WIFI_BRINGUP_ELF): $(COMMON_FIRMWARE_OBJS) $(WIFI_BRINGUP_OBJS) $(ARCH_DIR)/memmap_flash.ld
	$(RP2350_CC) $(RP2350_CFLAGS) $(RP2350_LDFLAGS) \
		$(COMMON_FIRMWARE_OBJS) $(WIFI_BRINGUP_OBJS) \
		-Wl,-Map=$(FIRMWARE_DIR)/wifi_bringup.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

$(FIRMWARE_DIR)/wifi_gspi.o: $(RADIO_DIR)/wifi_gspi.c $(RADIO_DIR)/wifi_gspi.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/wifi_probe.o: $(RADIO_DIR)/wifi_probe.c $(RADIO_DIR)/wifi_probe.h $(RADIO_DIR)/wifi_gspi.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/wifi_board.o: $(PLATFORM_DIR)/wifi_board.c $(PLATFORM_DIR)/wifi_board.h $(PLATFORM_DIR)/wifi_pio.h $(RADIO_DIR)/wifi_gspi.h $(RADIO_DIR)/wifi_trace.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/wifi_pio.o: $(PLATFORM_DIR)/wifi_pio.c $(PLATFORM_DIR)/wifi_pio.h $(PLATFORM_DIR)/wifi_pio_fifo.h $(RADIO_DIR)/wifi_gspi.h $(RADIO_DIR)/wifi_trace.h $(SHARED_DIR)/stdint.h $(SHARED_DIR)/bytes.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/wifi_bringup_view.o: $(TOOLS_DIR)/wifi_bringup_view.c $(TOOLS_DIR)/wifi_bringup_view.h $(TOOLS_DIR)/diagnostics_logic.h $(RADIO_DIR)/wifi_probe.h $(RADIO_DIR)/wifi_gspi.h $(PLATFORM_DIR)/graphics.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/wifi_bringup.o: $(TOOLS_DIR)/wifi_bringup.c $(TOOLS_DIR)/wifi_bringup_view.h $(TOOLS_DIR)/diagnostics_logic.h $(PLATFORM_DIR)/display.h $(PLATFORM_DIR)/graphics.h $(PLATFORM_DIR)/wifi_board.h $(RADIO_DIR)/wifi_probe.h $(RADIO_DIR)/wifi_gspi.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -I$(PLATFORM_DIR) -c $< -o $@

$(HOST_TOOLS_DIR)/wifi_bringup_test: src/host/tests/wifi_bringup_test.c $(RADIO_DIR)/wifi_gspi.c $(RADIO_DIR)/wifi_gspi.h $(RADIO_DIR)/wifi_probe.c $(RADIO_DIR)/wifi_probe.h $(TOOLS_DIR)/wifi_bringup_view.c $(TOOLS_DIR)/wifi_bringup_view.h $(TOOLS_DIR)/diagnostics_logic.c $(TOOLS_DIR)/diagnostics_logic.h $(PLATFORM_DIR)/graphics.c $(PLATFORM_DIR)/graphics.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g \
		-fsanitize=address,undefined src/host/tests/wifi_bringup_test.c \
		$(RADIO_DIR)/wifi_gspi.c $(RADIO_DIR)/wifi_probe.c \
		$(TOOLS_DIR)/wifi_bringup_view.c $(TOOLS_DIR)/diagnostics_logic.c \
		$(PLATFORM_DIR)/graphics.c -o $@

test-diagnostics: $(HOST_TOOLS_DIR)/diagnostics_test $(HOST_TOOLS_DIR)/cap1208_test
	$(HOST_TOOLS_DIR)/diagnostics_test
	$(HOST_TOOLS_DIR)/cap1208_test

wifi-handover: | $(UF2_DIR)
	cmake -S $(WIFI_HANDOVER_SRC_DIR) -B $(WIFI_HANDOVER_BUILD_DIR) -G Ninja \
		-DCMAKE_MAKE_PROGRAM=ninja \
		-DPICO_SDK_PATH=$(abspath $(PICO_SDK_DIR)) \
		-DPICO_BOARD_HEADER_DIRS=$(abspath $(BADGER_BOARD_DIR)) \
		-DPICO_BOARD=pimoroni_tufty2350
	cmake --build $(WIFI_HANDOVER_BUILD_DIR)
	cp $(WIFI_HANDOVER_BUILD_DIR)/wifi_handover.uf2 $(UF2_DIR)/wifi_handover.uf2
	@ls -lh $(UF2_DIR)/wifi_handover.uf2

test-wifi-handover: $(HOST_TOOLS_DIR)/handover_ui_test $(HOST_TOOLS_DIR)/handover_coordinator_test $(HOST_TOOLS_DIR)/provision_protocol_test $(HOST_TOOLS_DIR)/ble_provision_test $(HOST_TOOLS_DIR)/udp_protocol_test $(HOST_TOOLS_DIR)/wifi_transport_test
	$(HOST_TOOLS_DIR)/handover_ui_test
	$(HOST_TOOLS_DIR)/handover_coordinator_test
	$(HOST_TOOLS_DIR)/provision_protocol_test
	$(HOST_TOOLS_DIR)/ble_provision_test
	$(HOST_TOOLS_DIR)/ble_provision_test startup-timeout
	$(HOST_TOOLS_DIR)/udp_protocol_test
	$(HOST_TOOLS_DIR)/wifi_transport_test

$(HOST_TOOLS_DIR)/wifi_handover.h: $(WIFI_HANDOVER_SRC_DIR)/wifi_handover.gatt $(PICO_SDK_DIR)/lib/btstack/tool/compile_gatt.py | $(HOST_TOOLS_DIR)
	$(HOST_PYTHON) $(PICO_SDK_DIR)/lib/btstack/tool/compile_gatt.py $< $@

$(HOST_TOOLS_DIR)/ble_provision_test: src/host/tests/ble_provision_test.c $(WIFI_HANDOVER_SRC_DIR)/ble_provision.c $(WIFI_HANDOVER_SRC_DIR)/ble_provision.h $(WIFI_HANDOVER_SRC_DIR)/provision_protocol.c $(WIFI_HANDOVER_SRC_DIR)/provision_protocol.h $(WIFI_HANDOVER_SRC_DIR)/btstack_config.h $(WIFI_HANDOVER_SRC_DIR)/experiment.h $(HOST_TOOLS_DIR)/wifi_handover.h $(PICO_SDK_DIR)/lib/btstack/src/btstack_util.c | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g \
		-fsanitize=address,undefined -DENABLE_BLE \
		-I$(WIFI_HANDOVER_SRC_DIR) -I$(HOST_TOOLS_DIR) \
		-I$(PICO_SDK_DIR)/lib/btstack/src \
		src/host/tests/ble_provision_test.c $(WIFI_HANDOVER_SRC_DIR)/ble_provision.c \
		$(WIFI_HANDOVER_SRC_DIR)/provision_protocol.c \
		$(PICO_SDK_DIR)/lib/btstack/src/btstack_util.c -o $@

$(HOST_TOOLS_DIR)/wifi_transport_test: src/host/tests/wifi_transport_test.c $(WIFI_HANDOVER_SRC_DIR)/wifi_transport.c $(WIFI_HANDOVER_SRC_DIR)/wifi_transport.h $(WIFI_HANDOVER_SRC_DIR)/udp_protocol.c $(WIFI_HANDOVER_SRC_DIR)/udp_protocol.h $(WIFI_HANDOVER_SRC_DIR)/experiment.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g \
		-fsanitize=address,undefined -I$(WIFI_HANDOVER_SRC_DIR) \
		src/host/tests/wifi_transport_test.c $(WIFI_HANDOVER_SRC_DIR)/udp_protocol.c -o $@

$(HOST_TOOLS_DIR)/handover_coordinator_test: src/host/tests/handover_coordinator_test.c $(WIFI_HANDOVER_SRC_DIR)/main.c $(WIFI_HANDOVER_SRC_DIR)/experiment.h $(WIFI_HANDOVER_SRC_DIR)/controls.h $(WIFI_HANDOVER_SRC_DIR)/ui.h $(WIFI_HANDOVER_SRC_DIR)/ble_provision.h $(WIFI_HANDOVER_SRC_DIR)/wifi_transport.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g \
		-fsanitize=address,undefined -I$(WIFI_HANDOVER_SRC_DIR) \
		src/host/tests/handover_coordinator_test.c -o $@

$(HOST_TOOLS_DIR)/handover_ui_test: src/host/tests/handover_ui_test.c $(WIFI_HANDOVER_SRC_DIR)/controls.c $(WIFI_HANDOVER_SRC_DIR)/controls.h $(WIFI_HANDOVER_SRC_DIR)/ui_render.c $(WIFI_HANDOVER_SRC_DIR)/ui.h $(WIFI_HANDOVER_SRC_DIR)/experiment.h $(TOOLS_DIR)/diagnostics_logic.c $(TOOLS_DIR)/diagnostics_logic.h $(PLATFORM_DIR)/graphics.c $(PLATFORM_DIR)/graphics.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g \
		-fsanitize=address,undefined -I$(WIFI_HANDOVER_SRC_DIR) \
		src/host/tests/handover_ui_test.c $(WIFI_HANDOVER_SRC_DIR)/controls.c \
		$(WIFI_HANDOVER_SRC_DIR)/ui_render.c $(TOOLS_DIR)/diagnostics_logic.c \
		$(PLATFORM_DIR)/graphics.c -o $@

$(HOST_TOOLS_DIR)/provision_protocol_test: src/host/tests/provision_protocol_test.c $(WIFI_HANDOVER_SRC_DIR)/provision_protocol.c $(WIFI_HANDOVER_SRC_DIR)/provision_protocol.h $(WIFI_HANDOVER_SRC_DIR)/experiment.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g \
		-fsanitize=address,undefined -I$(WIFI_HANDOVER_SRC_DIR) \
		src/host/tests/provision_protocol_test.c $(WIFI_HANDOVER_SRC_DIR)/provision_protocol.c -o $@

$(HOST_TOOLS_DIR)/udp_protocol_test: src/host/tests/udp_protocol_test.c $(WIFI_HANDOVER_SRC_DIR)/udp_protocol.c $(WIFI_HANDOVER_SRC_DIR)/udp_protocol.h $(WIFI_HANDOVER_SRC_DIR)/experiment.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g \
		-fsanitize=address,undefined -I$(WIFI_HANDOVER_SRC_DIR) \
		src/host/tests/udp_protocol_test.c $(WIFI_HANDOVER_SRC_DIR)/udp_protocol.c -o $@

wifi-demo: $(WIFI_CREDENTIALS_H) | $(UF2_DIR)
	cmake -S $(WIFI_SRC_DIR) -B $(WIFI_BUILD_DIR) -G Ninja \
		-DCMAKE_MAKE_PROGRAM=ninja \
		-DBADGER_BUILD_FIRMWARE_DIR=$(abspath $(FIRMWARE_DIR)) \
		-DPICO_SDK_PATH=$(abspath $(PICO_SDK_DIR)) \
		-DPICO_BOARD_HEADER_DIRS=$(abspath $(BADGER_BOARD_DIR)) \
		-DPICO_BOARD=pimoroni_tufty2350
	cmake --build $(WIFI_BUILD_DIR)
	cp $(WIFI_BUILD_DIR)/wifi_demo.uf2 $(UF2_DIR)/wifi_demo.uf2
	@ls -lh $(UF2_DIR)/wifi_demo.uf2

wifi-mac-flash: $(WIFI_CREDENTIALS_H) | $(UF2_DIR)
	cmake -S $(WIFI_MAC_FLASH_SRC_DIR) -B $(WIFI_MAC_FLASH_BUILD_DIR) -G Ninja \
		-DCMAKE_MAKE_PROGRAM=ninja \
		-DBADGER_BUILD_FIRMWARE_DIR=$(abspath $(FIRMWARE_DIR)) \
		-DPICO_SDK_PATH=$(abspath $(PICO_SDK_DIR)) \
		-DPICO_BOARD_HEADER_DIRS=$(abspath $(BADGER_BOARD_DIR)) \
		-DPICO_BOARD=pimoroni_tufty2350
	cmake --build $(WIFI_MAC_FLASH_BUILD_DIR)
	cp $(WIFI_MAC_FLASH_BUILD_DIR)/wifi_mac_flash.uf2 $(UF2_DIR)/wifi_mac_flash.uf2
	@ls -lh $(UF2_DIR)/wifi_mac_flash.uf2

wireless-lab: | $(UF2_DIR)
	cmake -S $(WIRELESS_LAB_SRC_DIR) -B $(WIRELESS_LAB_BUILD_DIR) -G Ninja \
		-DCMAKE_MAKE_PROGRAM=ninja \
		-DPICO_SDK_PATH=$(abspath $(PICO_SDK_DIR)) \
		-DPICO_BOARD_HEADER_DIRS=$(abspath $(BADGER_BOARD_DIR)) \
		-DPICO_BOARD=pimoroni_tufty2350
	cmake --build $(WIRELESS_LAB_BUILD_DIR)
	cp $(WIRELESS_LAB_BUILD_DIR)/wireless_lab.uf2 $(UF2_DIR)/wireless_lab.uf2
	@ls -lh $(UF2_DIR)/wireless_lab.uf2

ble-tilt-pong: | $(UF2_DIR)
	cmake -S $(BLE_TILT_PONG_SRC_DIR) -B $(BLE_TILT_PONG_BUILD_DIR) -G Ninja \
		-DCMAKE_MAKE_PROGRAM=ninja \
		-DPICO_SDK_PATH=$(abspath $(PICO_SDK_DIR)) \
		-DPICO_BOARD_HEADER_DIRS=$(abspath $(BADGER_BOARD_DIR)) \
		-DPICO_BOARD=pimoroni_tufty2350
	cmake --build $(BLE_TILT_PONG_BUILD_DIR)
	cp $(BLE_TILT_PONG_BUILD_DIR)/ble_tilt_pong.uf2 $(UF2_DIR)/ble_tilt_pong.uf2
	@ls -lh $(UF2_DIR)/ble_tilt_pong.uf2

$(UF2_DIR)/%.uf2: $(FIRMWARE_DIR)/%.bin $(BIN2UF2) | $(UF2_DIR)
	$(BIN2UF2) $< $@ $(FLASH_BASE) $(RP2350_FAMILY_ID)

$(FIRMWARE_DIR)/%.bin: $(FIRMWARE_DIR)/%.elf
	$(RP2350_OBJCOPY) -O binary $< $@

$(HELLO_ELF): $(COMMON_FIRMWARE_OBJS) $(FIRMWARE_DIR)/hello_idle.o $(ARCH_DIR)/memmap_flash.ld
	$(RP2350_CC) $(RP2350_CFLAGS) $(RP2350_LDFLAGS) \
		$(COMMON_FIRMWARE_OBJS) $(FIRMWARE_DIR)/hello_idle.o \
		-Wl,-Map=$(FIRMWARE_DIR)/hello_idle.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

.PHONY: red-square red-square-shared test-red-square test-red-square-target
red-square: $(RED_SQUARE_UF2)
red-square-shared: $(RED_SQUARE_SHARED_UF2)

$(FIRMWARE_DIR)/start_red_square.o: $(ARCH_DIR)/start_red_square.S | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_ASFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/red_square.o: $(TOOLS_DIR)/red_square.c | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/red_square_shared.o: $(TOOLS_DIR)/red_square.c $(PLATFORM_DIR)/display.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -I$(PLATFORM_DIR) -DRED_SQUARE_SHARED_DISPLAY -c $< -o $@

$(RED_SQUARE_ELF): $(FIRMWARE_DIR)/start_red_square.o $(FIRMWARE_DIR)/picobin.o $(FIRMWARE_DIR)/red_square.o $(ARCH_DIR)/memmap_red_square.ld $(ARCH_DIR)/memmap_flash.ld
	$(RP2350_CC) $(RP2350_CFLAGS) -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(ARCH_DIR)/memmap_red_square.ld $(filter %.o,$^) -Wl,-Map=$(FIRMWARE_DIR)/red_square.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)" || { $(RP2350_NM) -u $@; exit 1; }
	$(RP2350_SIZE) $@

$(RED_SQUARE_SHARED_ELF): $(COMMON_FIRMWARE_OBJS) $(FIRMWARE_DIR)/display.o $(FIRMWARE_DIR)/red_square_shared.o $(ARCH_DIR)/memmap_flash.ld
	$(RP2350_CC) $(RP2350_CFLAGS) $(RP2350_LDFLAGS) $(filter %.o,$^) -Wl,-Map=$(FIRMWARE_DIR)/red_square_shared.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)" || { $(RP2350_NM) -u $@; exit 1; }
	$(RP2350_SIZE) $@

$(HOST_TOOLS_DIR)/red_square_test: tests/red_square_test.c $(TOOLS_DIR)/red_square.c | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -DRED_SQUARE_TEST $^ -o $@

test-red-square: $(HOST_TOOLS_DIR)/red_square_test
	./$(HOST_TOOLS_DIR)/red_square_test

test-red-square-target:
	$(MAKE) -C badge-compiler all
	node --test badge-compiler/tests/red-square.test.mjs

$(SCREEN_TEST_ELF): $(COMMON_FIRMWARE_OBJS) $(FIRMWARE_DIR)/display.o $(FIRMWARE_DIR)/screen_test.o $(ARCH_DIR)/memmap_flash.ld
	$(RP2350_CC) $(RP2350_CFLAGS) $(RP2350_LDFLAGS) \
		$(COMMON_FIRMWARE_OBJS) $(FIRMWARE_DIR)/display.o \
		$(FIRMWARE_DIR)/screen_test.o \
		-Wl,-Map=$(FIRMWARE_DIR)/screen_test.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

$(BUTTON_MAPPER_ELF): $(COMMON_FIRMWARE_OBJS) $(FIRMWARE_DIR)/display.o $(FIRMWARE_DIR)/button_mapper.o $(ARCH_DIR)/memmap_flash.ld
	$(RP2350_CC) $(RP2350_CFLAGS) $(RP2350_LDFLAGS) \
		$(COMMON_FIRMWARE_OBJS) $(FIRMWARE_DIR)/display.o \
		$(FIRMWARE_DIR)/button_mapper.o \
		-Wl,-Map=$(FIRMWARE_DIR)/button_mapper.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

$(GPIO_EVENTS_ELF): $(COMMON_FIRMWARE_OBJS) $(FIRMWARE_DIR)/display.o $(FIRMWARE_DIR)/gpio_events.o $(ARCH_DIR)/memmap_flash.ld
	$(RP2350_CC) $(RP2350_CFLAGS) $(RP2350_LDFLAGS) \
		$(COMMON_FIRMWARE_OBJS) $(FIRMWARE_DIR)/display.o \
		$(FIRMWARE_DIR)/gpio_events.o \
		-Wl,-Map=$(FIRMWARE_DIR)/gpio_events.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

$(PERIPHERAL_PROBE_ELF): $(COMMON_FIRMWARE_OBJS) $(FIRMWARE_DIR)/display.o $(FIRMWARE_DIR)/i2c_bitbang.o $(FIRMWARE_DIR)/peripheral_probe.o $(ARCH_DIR)/memmap_flash.ld
	$(RP2350_CC) $(RP2350_CFLAGS) $(RP2350_LDFLAGS) \
		$(COMMON_FIRMWARE_OBJS) $(FIRMWARE_DIR)/display.o \
		$(FIRMWARE_DIR)/i2c_bitbang.o $(FIRMWARE_DIR)/peripheral_probe.o \
		-Wl,-Map=$(FIRMWARE_DIR)/peripheral_probe.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

$(LOWER_MAPPER_ELF): $(COMMON_FIRMWARE_OBJS) $(FIRMWARE_DIR)/display.o $(FIRMWARE_DIR)/lower_mapper.o $(ARCH_DIR)/memmap_flash.ld
	$(RP2350_CC) $(RP2350_CFLAGS) $(RP2350_LDFLAGS) \
		$(COMMON_FIRMWARE_OBJS) $(FIRMWARE_DIR)/display.o \
		$(FIRMWARE_DIR)/lower_mapper.o \
		-Wl,-Map=$(FIRMWARE_DIR)/lower_mapper.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

$(GPIO_MONITOR_ELF): $(COMMON_FIRMWARE_OBJS) $(FIRMWARE_DIR)/display.o $(FIRMWARE_DIR)/gpio_monitor.o $(ARCH_DIR)/memmap_flash.ld
	$(RP2350_CC) $(RP2350_CFLAGS) $(RP2350_LDFLAGS) \
		$(COMMON_FIRMWARE_OBJS) $(FIRMWARE_DIR)/display.o \
		$(FIRMWARE_DIR)/gpio_monitor.o \
		-Wl,-Map=$(FIRMWARE_DIR)/gpio_monitor.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

$(IMU_DEMO_ELF): $(COMMON_FIRMWARE_OBJS) $(FIRMWARE_DIR)/display.o $(FIRMWARE_DIR)/i2c_bitbang.o $(FIRMWARE_DIR)/lsm6ds3.o $(FIRMWARE_DIR)/imu_demo.o $(ARCH_DIR)/memmap_flash.ld
	$(RP2350_CC) $(RP2350_CFLAGS) $(RP2350_LDFLAGS) \
		$(COMMON_FIRMWARE_OBJS) $(FIRMWARE_DIR)/display.o \
		$(FIRMWARE_DIR)/i2c_bitbang.o $(FIRMWARE_DIR)/lsm6ds3.o \
		$(FIRMWARE_DIR)/imu_demo.o \
		-Wl,-Map=$(FIRMWARE_DIR)/imu_demo.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

$(TILT_BREAKOUT_ELF): $(COMMON_FIRMWARE_OBJS) $(FIRMWARE_DIR)/display.o $(FIRMWARE_DIR)/i2c_bitbang.o $(FIRMWARE_DIR)/lsm6ds3.o $(FIRMWARE_DIR)/tilt_breakout.o $(ARCH_DIR)/memmap_flash.ld
	$(RP2350_CC) $(RP2350_CFLAGS) $(RP2350_LDFLAGS) \
		$(COMMON_FIRMWARE_OBJS) $(FIRMWARE_DIR)/display.o \
		$(FIRMWARE_DIR)/i2c_bitbang.o $(FIRMWARE_DIR)/lsm6ds3.o \
		$(FIRMWARE_DIR)/tilt_breakout.o \
		-Wl,-Map=$(FIRMWARE_DIR)/tilt_breakout.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

$(DIAGNOSTICS_ELF): $(COMMON_FIRMWARE_OBJS) $(DIAGNOSTICS_OBJS) $(ARCH_DIR)/memmap_flash.ld
	$(RP2350_CC) $(RP2350_CFLAGS) $(RP2350_LDFLAGS) \
		$(COMMON_FIRMWARE_OBJS) $(DIAGNOSTICS_OBJS) \
		-Wl,-Map=$(FIRMWARE_DIR)/diagnostics.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

$(TILT_MAZE_ELF): $(COMMON_FIRMWARE_OBJS) $(TILT_MAZE_OBJS) $(ARCH_DIR)/memmap_flash.ld
	$(RP2350_CC) $(RP2350_CFLAGS) $(RP2350_LDFLAGS) \
		$(COMMON_FIRMWARE_OBJS) $(TILT_MAZE_OBJS) \
		-Wl,-Map=$(FIRMWARE_DIR)/tilt_maze.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

$(FIRMWARE_DIR)/tilt_maze.o: $(TOOLS_DIR)/tilt_maze.c $(PLATFORM_DIR)/display.h $(PLATFORM_DIR)/graphics.h $(PLATFORM_DIR)/diagnostic_hw.h $(TOOLS_DIR)/diagnostics_logic.h $(PLATFORM_DIR)/i2c_bitbang.h $(PLATFORM_DIR)/lsm6ds3.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) $(TILT_MAZE_FLAGS) -I$(PLATFORM_DIR) -I$(TOOLS_DIR) -c $< -o $@

$(HOST_TOOLS_DIR)/tilt_maze_test: src/host/tests/tilt_maze_test.c $(TOOLS_DIR)/tilt_maze.c $(TOOLS_DIR)/diagnostics_logic.c $(TOOLS_DIR)/diagnostics_logic.h $(PLATFORM_DIR)/graphics.c $(PLATFORM_DIR)/graphics.h $(PLATFORM_DIR)/display.h $(PLATFORM_DIR)/diagnostic_hw.h $(PLATFORM_DIR)/i2c_bitbang.h $(PLATFORM_DIR)/lsm6ds3.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g \
		-fsanitize=address,undefined -I$(PLATFORM_DIR) -I$(TOOLS_DIR) \
		src/host/tests/tilt_maze_test.c $(TOOLS_DIR)/diagnostics_logic.c \
		$(PLATFORM_DIR)/graphics.c -o $@

$(FIRMWARE_DIR)/diagnostics.o: $(TOOLS_DIR)/diagnostics.c $(TOOLS_DIR)/diagnostics_logic.h $(PLATFORM_DIR)/display.h $(PLATFORM_DIR)/graphics.h $(PLATFORM_DIR)/diagnostic_hw.h $(PLATFORM_DIR)/cap1208.h $(PLATFORM_DIR)/lsm6ds3.h $(PLATFORM_DIR)/i2c_bitbang.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -I$(PLATFORM_DIR) -c $< -o $@

$(FIRMWARE_DIR)/diagnostics_logic.o: $(TOOLS_DIR)/diagnostics_logic.c $(TOOLS_DIR)/diagnostics_logic.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/graphics.o: $(PLATFORM_DIR)/graphics.c $(PLATFORM_DIR)/graphics.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/diagnostic_hw.o: $(PLATFORM_DIR)/diagnostic_hw.c $(PLATFORM_DIR)/diagnostic_hw.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/cap1208.o: $(PLATFORM_DIR)/cap1208.c $(PLATFORM_DIR)/cap1208.h $(PLATFORM_DIR)/i2c_bitbang.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(HOST_TOOLS_DIR)/diagnostics_test: src/host/tests/diagnostics_test.c $(TOOLS_DIR)/diagnostics.c $(TOOLS_DIR)/diagnostics_logic.c $(TOOLS_DIR)/diagnostics_logic.h $(PLATFORM_DIR)/graphics.c $(PLATFORM_DIR)/graphics.h $(PLATFORM_DIR)/display.h $(PLATFORM_DIR)/diagnostic_hw.h $(PLATFORM_DIR)/i2c_bitbang.h $(PLATFORM_DIR)/lsm6ds3.h $(PLATFORM_DIR)/cap1208.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g \
		-fsanitize=address,undefined -I$(PLATFORM_DIR) \
		src/host/tests/diagnostics_test.c $(TOOLS_DIR)/diagnostics_logic.c \
		$(PLATFORM_DIR)/graphics.c -o $@

$(HOST_TOOLS_DIR)/cap1208_test: src/host/tests/cap1208_test.c $(PLATFORM_DIR)/cap1208.c $(PLATFORM_DIR)/cap1208.h $(PLATFORM_DIR)/i2c_bitbang.h | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g \
		-fsanitize=address,undefined -I$(PLATFORM_DIR) \
		src/host/tests/cap1208_test.c $(PLATFORM_DIR)/cap1208.c -o $@

$(FIRMWARE_DIR)/start.o: $(ARCH_DIR)/start.c $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/picobin.o: $(ARCH_DIR)/picobin.S | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_ASFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/%.o: $(TOOLS_DIR)/%.c $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(FIRMWARE_DIR)/display.o: $(PLATFORM_DIR)/display.c $(PLATFORM_DIR)/display.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -I$(PLATFORM_DIR) -c $< -o $@

$(FIRMWARE_DIR)/screen_test.o: $(TOOLS_DIR)/screen_test.c $(PLATFORM_DIR)/display.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -I$(PLATFORM_DIR) -c $< -o $@

$(FIRMWARE_DIR)/button_mapper.o: $(TOOLS_DIR)/button_mapper.c $(PLATFORM_DIR)/display.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -I$(PLATFORM_DIR) -c $< -o $@

$(FIRMWARE_DIR)/gpio_events.o: $(TOOLS_DIR)/button_mapper.c $(PLATFORM_DIR)/display.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -DBUTTON_MAPPER_LIVE -I$(PLATFORM_DIR) -c $< -o $@

$(FIRMWARE_DIR)/i2c_bitbang.o: $(PLATFORM_DIR)/i2c_bitbang.c $(PLATFORM_DIR)/i2c_bitbang.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -I$(PLATFORM_DIR) -c $< -o $@

$(FIRMWARE_DIR)/lsm6ds3.o: $(PLATFORM_DIR)/lsm6ds3.c $(PLATFORM_DIR)/lsm6ds3.h $(PLATFORM_DIR)/i2c_bitbang.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -I$(PLATFORM_DIR) -c $< -o $@

$(FIRMWARE_DIR)/imu_demo.o: $(TOOLS_DIR)/imu_demo.c $(PLATFORM_DIR)/display.h $(PLATFORM_DIR)/i2c_bitbang.h $(PLATFORM_DIR)/lsm6ds3.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -I$(PLATFORM_DIR) -c $< -o $@

$(FIRMWARE_DIR)/tilt_breakout.o: $(TOOLS_DIR)/tilt_breakout.c $(PLATFORM_DIR)/display.h $(PLATFORM_DIR)/i2c_bitbang.h $(PLATFORM_DIR)/lsm6ds3.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -I$(PLATFORM_DIR) -c $< -o $@

$(FIRMWARE_DIR)/peripheral_probe.o: $(TOOLS_DIR)/peripheral_probe.c $(PLATFORM_DIR)/display.h $(PLATFORM_DIR)/i2c_bitbang.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -I$(PLATFORM_DIR) -c $< -o $@

$(FIRMWARE_DIR)/lower_mapper.o: $(TOOLS_DIR)/button_mapper.c $(PLATFORM_DIR)/display.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -DBUTTON_MAPPER_LOWER -I$(PLATFORM_DIR) -c $< -o $@

$(FIRMWARE_DIR)/gpio_monitor.o: $(TOOLS_DIR)/gpio_monitor.c $(PLATFORM_DIR)/display.h $(SHARED_DIR)/stdint.h | $(FIRMWARE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -I$(PLATFORM_DIR) -c $< -o $@

$(BIN2UF2): $(HOST_TOOLS_SRC_DIR)/bin2uf2.c | $(HOST_TOOLS_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O2 $< -o $@

$(FIRMWARE_DIR) $(UF2_DIR) $(HOST_TOOLS_DIR):
	mkdir -p $@

$(WIFI_CREDENTIALS_H): force | $(FIRMWARE_DIR)
	@if [ -f $(WIFI_CREDENTIALS_TXT) ]; then \
		awk 'function trim(s){sub(/^[ \t]*/,"",s); sub(/[ \t\r]*$$/,"",s); return s} function esc(s){gsub(/\\/,"\\\\",s); gsub(/"/,"\\\"",s); return s} /^SSID:[ \t]*/{ssid=trim(substr($$0,index($$0,":")+1))} /^Pass:[ \t]*/{pass=trim(substr($$0,index($$0,":")+1))} END{print "#ifndef BADGER_WIFI_CREDENTIALS_H"; print "#define BADGER_WIFI_CREDENTIALS_H"; printf "#define BADGER_WIFI_SSID \"%s\"\n", esc(ssid); printf "#define BADGER_WIFI_PASS \"%s\"\n", esc(pass); print "#endif"}' $(WIFI_CREDENTIALS_TXT); \
	else \
		printf '%s\n' '#ifndef BADGER_WIFI_CREDENTIALS_H' '#define BADGER_WIFI_CREDENTIALS_H' '#define BADGER_WIFI_SSID ""' '#define BADGER_WIFI_PASS ""' '#endif'; \
	fi > $@

$(addprefix $(FIRMWARE_DIR)/,wifi_control.o wifi_link.o wifi_udp.o): $(SHARED_DIR)/bytes.h
$(addprefix $(HOST_TOOLS_DIR)/,usb_report_host usb_live_host usb_wifi_firmware_host usb_wifi_link_host usb_wifi_udp_host wifi_link_test wifi_udp_test): $(SHARED_DIR)/bytes.h

.PHONY: test-wifi-pair-run
test-wifi-pair-run:
	bash -n src/host/tools/usb_wifi_pair_run.sh
	bash src/host/tests/usb_wifi_pair_run_test.sh

clean:
	rm -rf $(BUILD_DIR)
