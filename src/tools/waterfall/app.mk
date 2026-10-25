ROOT := ../../..
BUILD ?= $(ROOT)/build/waterfall/activity
CC := clang
FLAGS := --target=arm-none-eabi -mcpu=cortex-m33 -mthumb -std=c11 -Wall -Wextra -Werror \
	-Oz -ffreestanding -nostdinc -fno-builtin -fno-stack-protector \
	-fno-unwind-tables -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections \
	-I$(ROOT)/src/shared -I$(BUILD) -DWIFI_TRACE_SIZE=256u
SOURCES := app.c activity.c $(ROOT)/clean-rx/badge/loader.c \
	$(ROOT)/src/arch/rp2350/start.c $(ROOT)/src/arch/rp2350/picobin.S \
	$(ROOT)/src/arch/rp2350/bootrom_reboot.c $(ROOT)/src/arch/rp2350/nametag_clock.c \
	$(ROOT)/src/usb/live_usb_control.c $(ROOT)/src/usb/live_usb_hw.c \
	$(ROOT)/src/shared/usb_report.c $(ROOT)/src/tools/diagnostics_logic.c \
	$(ROOT)/src/platform/badger/wifi_board.c $(ROOT)/src/platform/badger/wifi_pio.c \
	$(ROOT)/src/platform/badger/display.c $(ROOT)/src/platform/badger/graphics.c \
	$(ROOT)/src/platform/badger/home.c \
	$(ROOT)/src/radio/wifi_gspi.c $(ROOT)/src/radio/wifi_probe.c \
	$(ROOT)/src/radio/wifi_trace.c
HEADERS := $(wildcard *.h $(ROOT)/clean-rx/src/*.h $(ROOT)/clean-rx/badge/loader.h \
	$(addsuffix /*.h,$(addprefix $(ROOT)/src/,shared usb radio platform/badger arch/rp2350)))
OBJECTS := $(addprefix $(BUILD)/,$(notdir $(SOURCES:.c=.o))) $(BUILD)/assets.o
OBJECTS := $(OBJECTS:.S=.o)
VPATH := $(sort $(dir $(SOURCES)))
.DEFAULT_GOAL := all
.PHONY: all test force controller
all: $(BUILD)/waterfall.uf2 $(BUILD)/activity_collect
$(BUILD):
	mkdir -p $@
CONTROLLER_FLAGS := --target=arm-none-eabi -mcpu=cortex-m3 -mthumb -std=c11 \
	-Wall -Wextra -Werror -Oz -ffreestanding -nostdinc -fno-builtin \
	-fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables \
	-ffunction-sections -fdata-sections -I$(ROOT)/clean-rx/src
CONTROLLER_OBJECTS := $(BUILD)/controller/activity_controller.o \
	$(addprefix $(BUILD)/controller/,pmu.o radio_phy.o psm_ucode.o mac.o block_ack.o startup.o)
CONTROLLER_INPUTS := $(wildcard $(ROOT)/clean-rx/src/*.h $(ROOT)/clean-rx/src/runtime/* \
	$(ROOT)/clean-rx/src/psm/*.h)
$(BUILD)/controller:
	mkdir -p $@
$(BUILD)/controller/activity_controller.o: activity_controller.c $(ROOT)/clean-rx/src/main.c $(CONTROLLER_INPUTS) | $(BUILD)/controller
	$(CC) $(CONTROLLER_FLAGS) -c $< -o $@
$(BUILD)/controller/%.o: $(ROOT)/clean-rx/src/%.c $(CONTROLLER_INPUTS) | $(BUILD)/controller
	$(CC) $(CONTROLLER_FLAGS) -c $< -o $@
$(BUILD)/controller/startup.o: $(ROOT)/clean-rx/src/startup.S | $(BUILD)/controller
	$(CC) $(CONTROLLER_FLAGS) -c $< -o $@
$(BUILD)/controller/cyw43_rx_fw.elf: $(CONTROLLER_OBJECTS) $(ROOT)/clean-rx/src/cyw43.ld
	$(CC) $(CONTROLLER_FLAGS) -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(ROOT)/clean-rx/src/cyw43.ld $(CONTROLLER_OBJECTS) -o $@
	@test -z "$$(llvm-nm -u $@)"
$(BUILD)/controller/cyw43_rx_fw.bin: $(BUILD)/controller/cyw43_rx_fw.elf
	llvm-objcopy -O binary $< $@
controller: $(BUILD)/controller/cyw43_rx_fw.bin
$(BUILD)/activity_build.h: force $(BUILD)/controller/cyw43_rx_fw.bin | $(BUILD)
	@id=$$({ shasum -a 256 $(SOURCES) $(HEADERS) app.mk $(BUILD)/controller/cyw43_rx_fw.bin; \
		printf '%s\n' '$(FLAGS)'; } | shasum -a 256 | cut -c1-8); \
	printf '#define ACTIVITY_BUILD_ID 0x%su\n' "$$id" > $@.tmp; \
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi
$(BUILD)/%.o: %.c $(HEADERS) $(BUILD)/activity_build.h
	$(CC) $(FLAGS) -c $< -o $@
$(BUILD)/%.o: %.S | $(BUILD)
	$(CC) $(FLAGS) -c $< -o $@
$(BUILD)/assets.o: $(ROOT)/clean-rx/badge/assets.S $(BUILD)/activity_build.h
	$(CC) $(FLAGS) -DCLEAN_CONTROLLER_PATH='"$(abspath $(BUILD))/controller/cyw43_rx_fw.bin"' -c $< -o $@
$(BUILD)/waterfall.elf: $(OBJECTS) $(ROOT)/src/arch/rp2350/memmap_report.ld
	$(CC) $(FLAGS) -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(ROOT)/src/arch/rp2350/memmap_report.ld $(OBJECTS) -Wl,-Map=$(BUILD)/waterfall.map -o $@
	@test -z "$$(llvm-nm -u $@)"
	llvm-size $@
$(BUILD)/waterfall.bin: $(BUILD)/waterfall.elf
	llvm-objcopy -O binary $< $@
$(ROOT)/build/host-tools/bin2uf2:
	$(MAKE) -C $(ROOT) build/host-tools/bin2uf2
$(BUILD)/waterfall.uf2: $(BUILD)/waterfall.bin $(ROOT)/build/host-tools/bin2uf2
	$(ROOT)/build/host-tools/bin2uf2 $< $@ 0x10000000 0xe48bff59
$(BUILD)/activity_test: activity_test.c activity.c activity.h $(ROOT)/src/platform/badger/graphics.c | $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
		activity_test.c activity.c $(ROOT)/src/platform/badger/graphics.c \
		$(ROOT)/src/tools/diagnostics_logic.c -o $@
test: $(BUILD)/activity_test
	$(BUILD)/activity_test $(BUILD)/layout.ppm
$(BUILD)/activity_collect: activity_collect.c $(BUILD)/activity_build.h $(ROOT)/src/radio/wifi_trace.c
	cc -std=c11 -Wall -Wextra -Werror -O2 -I$(BUILD) $$(pkg-config --cflags libusb-1.0) \
		$< $(ROOT)/src/shared/usb_report.c $(ROOT)/src/tools/diagnostics_logic.c $(ROOT)/src/radio/wifi_trace.c \
		$$(pkg-config --libs libusb-1.0) -o $@
