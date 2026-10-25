ROOT := ../../..
BUILD ?= $(ROOT)/build/waterfall/probe
CC := clang
PICO ?= 0
ifneq ($(PICO),0)
ifneq ($(PICO),1)
$(error PICO must be 0 or 1)
endif
endif
COMMON := -std=c11 -Wall -Wextra -Werror -Oz -ffreestanding -nostdinc \
	-fno-builtin -fno-stack-protector -fno-unwind-tables \
	-fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections
ARM := --target=arm-none-eabi -mthumb
HOST_FLAGS := $(COMMON) $(ARM) -mcpu=cortex-m33 -I$(ROOT)/src/shared -I$(BUILD) \
	-DWIFI_TRACE_SIZE=2092u -DWATERFALL_PICO=$(PICO)
CONTROLLER_FLAGS := $(COMMON) $(ARM) -mcpu=cortex-m3 -I$(ROOT)/clean-rx/src
SOURCES := main.c $(ROOT)/clean-rx/badge/loader.c \
	$(ROOT)/src/arch/rp2350/start.c $(ROOT)/src/arch/rp2350/bootrom_reboot.c \
	$(ROOT)/src/arch/rp2350/nametag_clock.c \
	$(ROOT)/src/usb/live_usb_control.c $(ROOT)/src/usb/live_usb_hw.c \
	$(ROOT)/src/shared/usb_report.c $(ROOT)/src/tools/diagnostics_logic.c \
	$(ROOT)/src/platform/badger/wifi_board.c $(ROOT)/src/platform/badger/wifi_pio.c \
	$(ROOT)/src/platform/badger/display.c $(ROOT)/src/platform/badger/graphics.c \
	$(ROOT)/src/radio/wifi_gspi.c $(ROOT)/src/radio/wifi_probe.c \
	$(ROOT)/src/radio/wifi_usb_report.c $(ROOT)/src/radio/wifi_trace.c
ifeq ($(PICO),1)
SOURCES += $(ROOT)/pico2w/radio/clock.c
endif
OBJECTS := $(addprefix $(BUILD)/,$(notdir $(SOURCES:.c=.o))) $(BUILD)/picobin.o $(BUILD)/assets.o
VPATH := $(sort $(dir $(SOURCES)))
HEADERS := $(wildcard *.h $(ROOT)/clean-rx/src/*.h $(ROOT)/clean-rx/badge/loader.h \
	$(addsuffix /*.h,$(addprefix $(ROOT)/src/,shared usb radio platform/badger arch/rp2350)))

.PHONY: all test force
all: $(BUILD)/waterfall.uf2 $(BUILD)/collect

$(BUILD):
	mkdir -p $@

$(BUILD)/controller.o: controller.c probe.h $(ROOT)/clean-rx/src/radio_phy.c $(HEADERS) | $(BUILD)
	$(CC) $(CONTROLLER_FLAGS) -c $< -o $@

$(BUILD)/pmu.o: $(ROOT)/clean-rx/src/pmu.c $(HEADERS) | $(BUILD)
	$(CC) $(CONTROLLER_FLAGS) -c $< -o $@

$(BUILD)/controller_start.o: $(ROOT)/clean-rx/src/startup.S | $(BUILD)
	$(CC) $(CONTROLLER_FLAGS) -c $< -o $@

$(BUILD)/controller.elf: $(BUILD)/controller.o $(BUILD)/pmu.o $(BUILD)/controller_start.o $(ROOT)/clean-rx/src/cyw43.ld
	$(CC) $(CONTROLLER_FLAGS) -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(ROOT)/clean-rx/src/cyw43.ld $(filter %.o,$^) -o $@
	@test -z "$$(llvm-nm -u $@)"
	llvm-size $@

$(BUILD)/controller.bin: $(BUILD)/controller.elf
	llvm-objcopy -O binary $< $@

$(BUILD)/build_id.h: force $(BUILD)/controller.bin | $(BUILD)
	@id=$$({ shasum -a 256 $(SOURCES) $(HEADERS) Makefile $(ROOT)/clean-rx/badge/assets.S $(BUILD)/controller.bin; \
		printf '%s\n' '$(HOST_FLAGS)'; } | shasum -a 256 | cut -c1-8); \
	printf '#define USB_REPORT_BUILD_ID 0x%su\n' "$$id" > $@.tmp; \
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi

$(BUILD)/%.o: %.c $(HEADERS) $(BUILD)/build_id.h
	$(CC) $(HOST_FLAGS) -c $< -o $@

$(BUILD)/picobin.o: $(ROOT)/src/arch/rp2350/picobin.S | $(BUILD)
	$(CC) $(HOST_FLAGS) -c $< -o $@

$(BUILD)/assets.o: $(ROOT)/clean-rx/badge/assets.S $(BUILD)/controller.bin
	$(CC) $(ARM) -mcpu=cortex-m33 -DCLEAN_CONTROLLER_PATH='"$(abspath $(BUILD)/controller.bin)"' -c $< -o $@

$(BUILD)/waterfall.elf: $(OBJECTS) $(ROOT)/src/arch/rp2350/memmap_report.ld
	$(CC) $(HOST_FLAGS) -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(ROOT)/src/arch/rp2350/memmap_report.ld $(OBJECTS) -Wl,-Map=$(BUILD)/waterfall.map -o $@
	@test -z "$$(llvm-nm -u $@)"
	llvm-size $@

$(BUILD)/waterfall.bin: $(BUILD)/waterfall.elf
	llvm-objcopy -O binary $< $@

$(ROOT)/build/host-tools/bin2uf2:
	$(MAKE) -C $(ROOT) build/host-tools/bin2uf2

$(BUILD)/waterfall.uf2: $(BUILD)/waterfall.bin $(ROOT)/build/host-tools/bin2uf2
	$(ROOT)/build/host-tools/bin2uf2 $< $@ 0x10000000 0xe48bff59

$(BUILD)/collect: collect.c probe.h $(BUILD)/build_id.h
	cc -std=c11 -Wall -Wextra -Werror -O2 -DWIFI_TRACE_SIZE=2092u -I$(BUILD) $$(pkg-config --cflags libusb-1.0) \
		$< $(ROOT)/src/shared/usb_report.c $(ROOT)/src/tools/diagnostics_logic.c \
		$(ROOT)/src/radio/wifi_trace.c \
		$$(pkg-config --libs libusb-1.0) -o $@

$(BUILD)/probe_test: probe_test.c probe.h | $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined $< -o $@

$(BUILD)/collect_test: collect_test.c collect.c probe.h $(BUILD)/build_id.h
	cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
		-DWIFI_TRACE_SIZE=2092u -I$(BUILD) $$(pkg-config --cflags libusb-1.0) \
		$< $(ROOT)/src/shared/usb_report.c $(ROOT)/src/tools/diagnostics_logic.c \
		$(ROOT)/src/radio/wifi_trace.c $$(pkg-config --libs libusb-1.0) -o $@

test: $(BUILD)/probe_test $(BUILD)/collect_test
	$(BUILD)/probe_test
	$(BUILD)/collect_test > $(BUILD)/collect_test.log
