ROOT := ../..
BUILD ?= $(ROOT)/build/clean-rx/messenger
ROLE ?= 1
BASELINE ?= 0
CONTROLLER := $(abspath $(BUILD)/controller/cyw43_rx_fw.bin)
FLAGS := -DCLEAN_RX_HT_DATA=1 -DCLEAN_RX_HT_RX=1
APP_FLAGS := -DWIFI_TRACE_SIZE=512u -DMSG_BASELINE=$(BASELINE)
CC := clang
CFLAGS := --target=arm-none-eabi -mcpu=cortex-m33 -mthumb -std=c11 \
	-Wall -Wextra -Werror -Oz -ffreestanding -fno-builtin -fno-stack-protector \
	-fno-unwind-tables -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections \
	-I$(ROOT)/src/shared -I$(BUILD) -DMSG_ROLE=$(ROLE) $(FLAGS) $(APP_FLAGS) -DLIVE_USB_CLEAN -DLIVE_USB_FIRMWARE
SOURCES := main.c link.c ../badge/loader.c ../src/mac.c ../src/block_ack.c \
	$(ROOT)/src/arch/rp2350/start.c $(ROOT)/src/arch/rp2350/bootrom_reboot.c \
	$(ROOT)/src/arch/rp2350/nametag_clock.c \
	$(ROOT)/src/usb/live_usb_control.c $(ROOT)/src/usb/live_usb_hw.c \
	$(ROOT)/src/shared/usb_report.c $(ROOT)/src/tools/diagnostics_logic.c \
	$(ROOT)/src/platform/badger/wifi_board.c $(ROOT)/src/platform/badger/wifi_pio.c \
	$(ROOT)/src/platform/badger/display.c $(ROOT)/src/platform/badger/graphics.c \
	$(ROOT)/src/radio/wifi_gspi.c $(ROOT)/src/radio/wifi_probe.c \
	$(ROOT)/src/radio/wifi_usb_report.c $(ROOT)/src/radio/wifi_trace.c
HEADERS := $(wildcard *.h ../src/*.h ../src/psm/*.h ../badge/loader.h $(ROOT)/src/shared/*.h \
	$(ROOT)/src/usb/*.h $(ROOT)/src/radio/*.h $(ROOT)/src/platform/badger/*.h \
	$(ROOT)/src/arch/rp2350/*.h $(ROOT)/src/tools/diagnostics_logic.h)
OBJECTS := $(addprefix $(BUILD)/role$(ROLE)/,$(notdir $(SOURCES:.c=.o))) \
	$(BUILD)/role$(ROLE)/picobin.o $(BUILD)/role$(ROLE)/assets.o
VPATH := $(sort $(dir $(SOURCES)))

.PHONY: all pair controller force test
all: $(BUILD)/role$(ROLE)/messenger.uf2 $(BUILD)/collector
pair:
	$(MAKE) ROLE=1 all
	$(MAKE) ROLE=2 all
controller:
	$(MAKE) -C ../src CC=clang BUILD=$(abspath $(BUILD)/controller) PROFILE=runtime EXTRA_CFLAGS='$(FLAGS)'
$(BUILD) $(BUILD)/role$(ROLE):
	mkdir -p $@
$(BUILD)/build_id.h: force controller | $(BUILD)
	@id=$$({ shasum -a 256 $(SOURCES) $(HEADERS) collector.c Makefile $(CONTROLLER); \
		printf '%s\n' '$(APP_FLAGS)'; } | shasum -a 256 | cut -c1-8); \
	printf '#define USB_REPORT_BUILD_ID 0x%su\n' "$$id" > $@.tmp; \
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi
$(BUILD)/role$(ROLE)/%.o: %.c $(HEADERS) $(BUILD)/build_id.h | $(BUILD)/role$(ROLE)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/role$(ROLE)/picobin.o: $(ROOT)/src/arch/rp2350/picobin.S | $(BUILD)/role$(ROLE)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/role$(ROLE)/assets.o: ../badge/assets.S $(BUILD)/build_id.h | $(BUILD)/role$(ROLE)
	$(CC) $(CFLAGS) -DCLEAN_CONTROLLER_PATH='"$(CONTROLLER)"' -c $< -o $@
$(BUILD)/role$(ROLE)/messenger.elf: $(OBJECTS) $(ROOT)/src/arch/rp2350/memmap_report.ld
	$(CC) $(CFLAGS) -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(ROOT)/src/arch/rp2350/memmap_report.ld $(OBJECTS) -Wl,-Map=$(@:.elf=.map) -o $@
	@test -z "$$(llvm-nm -u $@)"
	llvm-size $@
$(BUILD)/role$(ROLE)/messenger.bin: $(BUILD)/role$(ROLE)/messenger.elf
	llvm-objcopy -O binary $< $@
$(ROOT)/build/host-tools/bin2uf2:
	$(MAKE) -C $(ROOT) build/host-tools/bin2uf2
$(BUILD)/role$(ROLE)/messenger.uf2: $(BUILD)/role$(ROLE)/messenger.bin $(ROOT)/build/host-tools/bin2uf2
	$(ROOT)/build/host-tools/bin2uf2 $< $@ 0x10000000 0xe48bff59
$(BUILD)/collector: collector.c link.c $(HEADERS) $(BUILD)/build_id.h
	cc -std=c11 -Wall -Wextra -Werror -O2 $(FLAGS) $(APP_FLAGS) -I$(BUILD) $$(pkg-config --cflags libusb-1.0) \
		collector.c link.c ../src/mac.c ../src/block_ack.c $(ROOT)/src/shared/usb_report.c \
		$(ROOT)/src/tools/diagnostics_logic.c $(ROOT)/src/radio/wifi_trace.c \
		$$(pkg-config --libs libusb-1.0) -o $@
test: $(BUILD)/link_test
	$(BUILD)/link_test
$(BUILD)/link_test: link_test.c link.c $(HEADERS) | $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O1 -g $(FLAGS) $(APP_FLAGS) -fsanitize=address,undefined \
		link_test.c link.c ../src/mac.c ../src/block_ack.c \
		$(ROOT)/src/tools/diagnostics_logic.c -o $@
