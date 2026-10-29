ROOT := ../..
BUILD ?= $(ROOT)/build/logi-keypad/firmware
CC := clang
FLAGS := -std=c11 -Oz -Wall -Wextra -Werror --target=arm-none-eabi -mthumb \
	-mcpu=cortex-m33 -ffreestanding -nostdinc -fno-builtin -fno-stack-protector \
	-fno-unwind-tables -fno-asynchronous-unwind-tables -ffunction-sections \
	-fdata-sections -DLIVE_USB_KEYPAD_TRACE -I$(ROOT)/src/shared -I$(BUILD)
SHARED := $(ROOT)/src/shared/usb_report.c $(ROOT)/src/tools/diagnostics_logic.c \
	$(ROOT)/src/radio/wifi_usb_report.c $(ROOT)/src/radio/wifi_trace.c
USB := $(ROOT)/src/usb/live_usb_control.c
SOURCES := main.c trace.c $(USB) $(SHARED) $(ROOT)/src/usb/live_usb_hw.c \
	$(ROOT)/src/arch/rp2350/start.c $(ROOT)/src/arch/rp2350/bootrom_reboot.c \
	$(ROOT)/src/arch/rp2350/nametag_clock.c $(ROOT)/src/platform/badger/display.c \
	$(ROOT)/src/platform/badger/graphics.c $(ROOT)/src/platform/badger/diagnostic_hw.c
HEADERS := $(wildcard *.h ../keypad-input.h $(addsuffix /*.h,$(addprefix $(ROOT)/src/,shared usb radio arch/rp2350 platform/badger tools)))
OBJECTS := $(addprefix $(BUILD)/,$(notdir $(SOURCES:.c=.o))) $(BUILD)/picobin.o
VPATH := $(sort $(dir $(SOURCES)))
HOST_FLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
	-DLIVE_USB_KEYPAD_TRACE -I$(BUILD)

.PHONY: all test force
all: $(BUILD)/keypad-trace.uf2 $(BUILD)/keypad-replay

$(BUILD):
	mkdir -p $@

$(BUILD)/build_id.h: force $(SOURCES) $(HEADERS) ../keypad-replay.c Makefile | $(BUILD)
	@id=$$({ shasum -a 256 $(SOURCES) $(HEADERS) ../keypad-replay.c Makefile \
		$(ROOT)/src/arch/rp2350/picobin.S $(ROOT)/src/arch/rp2350/memmap_report.ld; \
		printf '%s\n' '$(FLAGS)'; } | shasum -a 256 | cut -c1-8); \
	printf '#define USB_REPORT_BUILD_ID 0x%su\n' "$$id" > $@.tmp; \
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi

$(BUILD)/%.o: %.c $(HEADERS) $(BUILD)/build_id.h
	$(CC) $(FLAGS) -c $< -o $@

$(BUILD)/picobin.o: $(ROOT)/src/arch/rp2350/picobin.S | $(BUILD)
	$(CC) $(FLAGS) -c $< -o $@

$(BUILD)/keypad-trace.elf: $(OBJECTS) $(ROOT)/src/arch/rp2350/memmap_report.ld
	$(CC) $(FLAGS) -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(ROOT)/src/arch/rp2350/memmap_report.ld $(OBJECTS) \
		-Wl,-Map=$(BUILD)/keypad-trace.map -o $@
	@test -z "$$(llvm-nm -u $@)"
	llvm-size $@

$(BUILD)/keypad-trace.bin: $(BUILD)/keypad-trace.elf
	llvm-objcopy -O binary $< $@

$(ROOT)/build/host-tools/bin2uf2:
	$(MAKE) -C $(ROOT) build/host-tools/bin2uf2

$(BUILD)/keypad-trace.uf2: $(BUILD)/keypad-trace.bin $(ROOT)/build/host-tools/bin2uf2
	$(ROOT)/build/host-tools/bin2uf2 $< $@ 0x10000000 0xe48bff59

$(BUILD)/keypad-replay: ../keypad-replay.c trace.c $(HEADERS) $(BUILD)/build_id.h
	cc -std=c11 -O2 -Wall -Wextra -Werror -I$(BUILD) $< trace.c \
		$(ROOT)/src/tools/diagnostics_logic.c -framework IOKit -framework CoreFoundation -o $@

$(BUILD)/trace-test: trace-test.c trace.c $(USB) $(SHARED) $(HEADERS) | $(BUILD)
	cc $(HOST_FLAGS) trace-test.c trace.c $(USB) $(SHARED) -o $@

$(BUILD)/replay-test: ../keypad-replay-test.c ../keypad-replay.c trace.c \
	$(USB) $(SHARED) $(HEADERS) $(BUILD)/build_id.h
	cc $(HOST_FLAGS) $< trace.c $(USB) $(SHARED) \
		-framework IOKit -framework CoreFoundation -o $@

test: $(BUILD)/trace-test $(BUILD)/replay-test
	$(BUILD)/trace-test
	$(BUILD)/replay-test
