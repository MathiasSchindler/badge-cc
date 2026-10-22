ROOT := ../..
SHELL := /bin/bash
.SHELLFLAGS := -eu -o pipefail -c
BUILD := ../build/radio
NODE ?= 1
CONTROLLER := $(abspath $(BUILD)/controller/cyw43_rx_fw.bin)
CC := clang
APP_FLAGS := -DWIFI_TRACE_SIZE=512u -DLIVE_USB_FIRMWARE
CFLAGS := --target=arm-none-eabi -mcpu=cortex-m33 -mthumb -std=c11 \
	-Wall -Wextra -Werror -Oz -ffreestanding -nostdinc -fno-builtin \
	-fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables \
	-ffunction-sections -fdata-sections -I$(ROOT)/src/shared -I$(BUILD) \
	$(APP_FLAGS) -DPR_NODE=$(NODE)
SOURCES := main.c protocol.c $(ROOT)/clean-rx/badge/loader.c \
	$(ROOT)/clean-rx/src/mac.c $(ROOT)/clean-rx/src/block_ack.c \
	$(ROOT)/src/arch/rp2350/start.c $(ROOT)/src/arch/rp2350/bootrom_reboot.c \
	$(ROOT)/src/arch/rp2350/nametag_clock.c \
	$(ROOT)/src/usb/live_usb_control.c $(ROOT)/src/usb/live_usb_hw.c \
	$(ROOT)/src/shared/usb_report.c $(ROOT)/src/tools/diagnostics_logic.c \
	$(ROOT)/src/platform/badger/wifi_pio.c $(ROOT)/src/radio/wifi_gspi.c \
	$(ROOT)/src/radio/wifi_probe.c $(ROOT)/src/radio/wifi_usb_report.c \
	$(ROOT)/src/radio/wifi_trace.c
ifeq ($(NODE),1)
SOURCES += pico_board.c clock.c
else
SOURCES += $(ROOT)/src/platform/badger/wifi_board.c \
	$(ROOT)/src/platform/badger/display.c $(ROOT)/src/platform/badger/graphics.c
endif
HEADERS := $(wildcard *.h $(ROOT)/clean-rx/src/*.h $(ROOT)/clean-rx/badge/*.h \
	$(ROOT)/src/shared/*.h $(ROOT)/src/usb/*.h $(ROOT)/src/radio/*.h \
	$(ROOT)/src/platform/badger/*.h $(ROOT)/src/arch/rp2350/*.h \
	$(ROOT)/src/tools/diagnostics_logic.h)
OUT := $(BUILD)/node$(NODE)
OBJECTS := $(addprefix $(OUT)/,$(notdir $(SOURCES:.c=.o))) $(OUT)/picobin.o $(OUT)/assets.o
VPATH := $(sort $(dir $(SOURCES)))

.PHONY: all firmware controller force test
all:
	$(MAKE) NODE=1 firmware
	$(MAKE) NODE=2 firmware
	$(MAKE) NODE=3 firmware
	$(MAKE) $(BUILD)/collector
firmware: $(OUT)/radio.uf2
controller:
	$(MAKE) -C $(ROOT)/clean-rx/src CC=clang BUILD=$(abspath $(BUILD)/controller)
$(BUILD) $(OUT):
	mkdir -p $@
$(BUILD)/build_id.h: force controller | $(BUILD)
	@id=$$({ shasum -a 256 main.c protocol.c pico_board.c clock.c $(HEADERS) Makefile $(CONTROLLER) \
		$(filter-out main.c protocol.c pico_board.c clock.c \
		$(ROOT)/src/platform/badger/wifi_board.c $(ROOT)/src/platform/badger/display.c \
		$(ROOT)/src/platform/badger/graphics.c,$(SOURCES)) \
		$(ROOT)/src/platform/badger/wifi_board.c $(ROOT)/src/platform/badger/display.c \
		$(ROOT)/src/platform/badger/graphics.c; printf '%s\n' '$(APP_FLAGS)'; \
		} | shasum -a 256 | cut -c1-8); \
	printf '#define USB_REPORT_BUILD_ID 0x%su\n' "$$id" > $@.tmp; \
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi
$(OUT)/%.o: %.c $(HEADERS) $(BUILD)/build_id.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/picobin.o: $(ROOT)/src/arch/rp2350/picobin.S | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/assets.o: $(ROOT)/clean-rx/badge/assets.S $(BUILD)/build_id.h | $(OUT)
	$(CC) $(CFLAGS) -DCLEAN_CONTROLLER_PATH='"$(CONTROLLER)"' -c $< -o $@
$(OUT)/radio.elf: $(OBJECTS) $(ROOT)/src/arch/rp2350/memmap_report.ld
	$(CC) $(CFLAGS) -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(ROOT)/src/arch/rp2350/memmap_report.ld $(OBJECTS) -Wl,-Map=$(@:.elf=.map) -o $@
	@test -z "$$(llvm-nm -u $@)"
	llvm-size $@
$(OUT)/radio.bin: $(OUT)/radio.elf
	llvm-objcopy -O binary $< $@
$(ROOT)/build/host-tools/bin2uf2:
	$(MAKE) -C $(ROOT) build/host-tools/bin2uf2
$(OUT)/radio.uf2: $(OUT)/radio.bin $(ROOT)/build/host-tools/bin2uf2
	$(ROOT)/build/host-tools/bin2uf2 $< $@ 0x10000000 0xe48bff59
$(BUILD)/collector: collector.c protocol.c $(HEADERS) $(BUILD)/build_id.h
	cc -std=c11 -Wall -Wextra -Werror -O2 $(APP_FLAGS) -I$(BUILD) \
		$$(pkg-config --cflags libusb-1.0) collector.c protocol.c \
		$(ROOT)/src/shared/usb_report.c $(ROOT)/src/tools/diagnostics_logic.c \
		$(ROOT)/src/radio/wifi_trace.c $$(pkg-config --libs libusb-1.0) -o $@
test: $(BUILD)/protocol_test
	$(BUILD)/protocol_test
$(BUILD)/protocol_test: protocol_test.c protocol.c $(HEADERS) | $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
		protocol_test.c protocol.c $(ROOT)/src/tools/diagnostics_logic.c -o $@
