ROOT := ../..
BUILD ?= $(ROOT)/build/clean-rx/badge
CONTROLLER_BIN ?= $(ROOT)/build/clean-rx/cyw43_rx_fw.bin
CC := clang
CFLAGS := --target=arm-none-eabi -mcpu=cortex-m33 -mthumb -std=c11 \
	-Wall -Wextra -Werror -Oz -ffreestanding -fno-builtin -fno-stack-protector \
	-fno-unwind-tables -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections \
	-I$(ROOT)/src/shared -I$(BUILD) -DWIFI_TRACE_SIZE=2048u
SOURCES := main.c loader.c \
	$(ROOT)/src/arch/rp2350/start.c $(ROOT)/src/arch/rp2350/bootrom_reboot.c \
	$(ROOT)/src/usb/live_usb_control.c $(ROOT)/src/usb/live_usb_hw.c \
	$(ROOT)/src/shared/usb_report.c $(ROOT)/src/tools/diagnostics_logic.c \
	$(ROOT)/src/platform/badger/wifi_board.c $(ROOT)/src/platform/badger/wifi_pio.c \
	$(ROOT)/src/platform/badger/display.c $(ROOT)/src/platform/badger/graphics.c \
	$(ROOT)/src/radio/wifi_gspi.c $(ROOT)/src/radio/wifi_probe.c \
	$(ROOT)/src/radio/wifi_usb_report.c $(ROOT)/src/radio/wifi_trace.c
OBJECTS := $(addprefix $(BUILD)/,$(notdir $(SOURCES:.c=.o))) $(BUILD)/picobin.o $(BUILD)/assets.o
VPATH := $(sort $(dir $(SOURCES)))
HEADERS := ../tx_diag.h ../frame_limits.h ../src/sdio_ring.h $(wildcard *.h $(addsuffix /*.h,$(addprefix $(ROOT)/src/,shared usb radio platform/badger arch/rp2350)))

.PHONY: all controller force test
all: $(BUILD)/clean_rx.uf2 $(BUILD)/collector

controller:
	$(MAKE) -C ../src CC=clang BUILD=../../build/clean-rx

$(BUILD):
	mkdir -p $@

$(BUILD)/build_id.h: force controller | $(BUILD)
	@id=$$(shasum -a 256 $(SOURCES) $(HEADERS) collector.c assets.S Makefile $(CONTROLLER_BIN) | shasum -a 256 | cut -c1-8); \
	printf '#define USB_REPORT_BUILD_ID 0x%su\n' "$$id" > $@.tmp; \
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi

$(BUILD)/%.o: %.c $(BUILD)/build_id.h $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/picobin.o: $(ROOT)/src/arch/rp2350/picobin.S | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/assets.o: assets.S $(BUILD)/build_id.h
	$(CC) --target=arm-none-eabi -mcpu=cortex-m33 -mthumb \
		-DCLEAN_CONTROLLER_PATH='"$(abspath $(CONTROLLER_BIN))"' -c $< -o $@

$(BUILD)/clean_rx.elf: $(OBJECTS) $(ROOT)/src/arch/rp2350/memmap_report.ld
	$(CC) $(CFLAGS) -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(ROOT)/src/arch/rp2350/memmap_report.ld $(OBJECTS) -Wl,-Map=$(BUILD)/clean_rx.map -o $@
	@test -z "$$(llvm-nm -u $@)"
	llvm-size $@

$(BUILD)/clean_rx.bin: $(BUILD)/clean_rx.elf
	llvm-objcopy -O binary $< $@

$(ROOT)/build/host-tools/bin2uf2:
	$(MAKE) -C $(ROOT) build/host-tools/bin2uf2

$(BUILD)/clean_rx.uf2: $(BUILD)/clean_rx.bin $(ROOT)/build/host-tools/bin2uf2
	$(ROOT)/build/host-tools/bin2uf2 $< $@ 0x10000000 0xe48bff59

$(BUILD)/collector: collector.c frames.h ../tx_diag.h $(BUILD)/build_id.h
	cc -std=c11 -Wall -Wextra -Werror -O2 -I$(BUILD) -DWIFI_TRACE_SIZE=2048u $$(pkg-config --cflags libusb-1.0) \
		$< $(ROOT)/src/shared/usb_report.c $(ROOT)/src/tools/diagnostics_logic.c \
		$(ROOT)/src/radio/wifi_trace.c $$(pkg-config --libs libusb-1.0) -o $@

test: $(BUILD)/loader_test $(BUILD)/loader_strict_test
	$(BUILD)/loader_test
	$(BUILD)/loader_strict_test

$(BUILD)/loader_strict_test: loader_test.c loader.c loader.h frames.h ../tx_diag.h ../frame_limits.h ../src/sdio_ring.h | $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O2 -DCLEAN_RX_STRICT_TEST loader_test.c loader.c \
		$(ROOT)/src/radio/wifi_gspi.c $(ROOT)/src/radio/wifi_probe.c \
		$(ROOT)/src/tools/diagnostics_logic.c -o $@

$(BUILD)/loader_test: loader_test.c loader.c loader.h frames.h ../tx_diag.h ../frame_limits.h ../src/sdio_ring.h | $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O2 loader_test.c loader.c \
		$(ROOT)/src/radio/wifi_gspi.c $(ROOT)/src/radio/wifi_probe.c \
		$(ROOT)/src/tools/diagnostics_logic.c -o $@
