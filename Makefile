BUILD_DIR := build
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
.PHONY: usb-wifi-firmware test-wifi-firmware
.PHONY: usb-wifi-link test-wifi-link
.PHONY: usb-wifi-udp test-wifi-udp
.PHONY: tilt-maze test-tilt-maze

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
