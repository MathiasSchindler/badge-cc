ROOT := ../..
BUILD ?= $(ROOT)/build/logi-keypad/host-firmware
CC := clang
FLAGS := -std=c11 -Oz -Wall -Wextra -Werror --target=arm-none-eabi -mthumb \
	-mcpu=cortex-m33 -ffreestanding -nostdinc -fno-builtin -fno-stack-protector \
	-fno-unwind-tables -fno-asynchronous-unwind-tables -ffunction-sections \
	-fdata-sections -I$(ROOT)/src/shared -I$(BUILD)
LOGIC := usb_host.c keypad_host.c ../firmware/trace.c $(ROOT)/src/tools/diagnostics_logic.c
SOURCES := main.c $(LOGIC) $(ROOT)/src/usb/live_usb_hw.c \
	$(ROOT)/src/arch/rp2350/start.c $(ROOT)/src/arch/rp2350/bootrom_reboot.c \
	$(ROOT)/src/arch/rp2350/nametag_clock.c $(ROOT)/src/platform/badger/display.c \
	$(ROOT)/src/platform/badger/graphics.c $(ROOT)/src/platform/badger/diagnostic_hw.c
HEADERS := $(wildcard *.h ../keypad-input.h ../keypad-descriptors.h ../firmware/trace.h \
	$(addsuffix /*.h,$(addprefix $(ROOT)/src/,shared usb arch/rp2350 platform/badger tools)))
OBJECTS := $(addprefix $(BUILD)/,$(notdir $(SOURCES:.c=.o))) $(BUILD)/picobin.o
VPATH := $(sort $(dir $(SOURCES)))

.PHONY: all test force
all: $(BUILD)/keypad-host.uf2

$(BUILD):
	mkdir -p $@

$(BUILD)/build_id.h: force $(SOURCES) $(HEADERS) Makefile | $(BUILD)
	@id=$$({ shasum -a 256 $(SOURCES) $(HEADERS) Makefile \
		$(ROOT)/src/arch/rp2350/picobin.S $(ROOT)/src/arch/rp2350/memmap_report.ld; \
		printf '%s\n' '$(FLAGS)'; } | shasum -a 256 | cut -c1-8); \
	printf '#define USB_REPORT_BUILD_ID 0x%su\n' "$$id" > $@.tmp; \
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi

$(BUILD)/%.o: %.c $(HEADERS) $(BUILD)/build_id.h
	$(CC) $(FLAGS) -c $< -o $@

$(BUILD)/picobin.o: $(ROOT)/src/arch/rp2350/picobin.S | $(BUILD)
	$(CC) $(FLAGS) -c $< -o $@

$(BUILD)/keypad-host.elf: $(OBJECTS) $(ROOT)/src/arch/rp2350/memmap_report.ld
	$(CC) $(FLAGS) -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(ROOT)/src/arch/rp2350/memmap_report.ld $(OBJECTS) \
		-Wl,-Map=$(BUILD)/keypad-host.map -o $@
	@test -z "$$(llvm-nm -u $@)"
	llvm-size $@

$(BUILD)/keypad-host.bin: $(BUILD)/keypad-host.elf
	llvm-objcopy -O binary $< $@

$(ROOT)/build/host-tools/bin2uf2:
	$(MAKE) -C $(ROOT) build/host-tools/bin2uf2

$(BUILD)/keypad-host.uf2: $(BUILD)/keypad-host.bin $(ROOT)/build/host-tools/bin2uf2
	$(ROOT)/build/host-tools/bin2uf2 $< $@ 0x10000000 0xe48bff59

$(BUILD)/host-test: host-test.c $(LOGIC) $(HEADERS) | $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
		host-test.c $(LOGIC) -o $@

$(BUILD)/app-test: app-test.c main.c $(LOGIC) $(HEADERS) $(BUILD)/build_id.h
	cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I$(BUILD) \
		app-test.c $(LOGIC) $(ROOT)/src/platform/badger/graphics.c -o $@

test: $(BUILD)/host-test $(BUILD)/app-test
	$(BUILD)/host-test
	$(BUILD)/app-test
