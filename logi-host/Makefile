ROOT := ..
BUILD ?= $(ROOT)/build/logi-host
CC := clang
FLAGS := -std=c11 -Oz -Wall -Wextra -Werror --target=arm-none-eabi -mthumb \
	-mcpu=cortex-m33 -ffreestanding -nostdinc -fno-builtin -fno-stack-protector \
	-fno-unwind-tables -fno-asynchronous-unwind-tables -ffunction-sections \
	-fdata-sections -DLIVE_USB_LOGI_HOST -I$(ROOT)/src/shared -I$(BUILD)
SHARED := $(ROOT)/src/shared/usb_report.c $(ROOT)/src/tools/diagnostics_logic.c \
	$(ROOT)/src/radio/wifi_usb_report.c $(ROOT)/src/radio/wifi_trace.c
USB := $(ROOT)/src/usb/live_usb_control.c
SOURCES := main.c board.c fs_wire.c fs_host.c fs_pio.c $(USB) $(SHARED) $(ROOT)/src/usb/live_usb_hw.c \
	$(ROOT)/src/arch/rp2350/start.c $(ROOT)/src/arch/rp2350/bootrom_reboot.c \
	$(ROOT)/src/arch/rp2350/nametag_clock.c
HEADERS := $(wildcard *.h $(ROOT)/src/shared/*.h $(ROOT)/src/usb/*.h \
	$(ROOT)/src/radio/*.h $(ROOT)/src/arch/rp2350/*.h) $(ROOT)/src/tools/diagnostics_logic.h
HEADERS += $(ROOT)/src/host/tools/usb_macos.h
OBJECTS := $(addprefix $(BUILD)/,$(notdir $(SOURCES:.c=.o))) $(BUILD)/picobin.o
VPATH := $(sort $(dir $(SOURCES)))

.PHONY: all test force
all: $(BUILD)/logi-host.uf2 $(BUILD)/logi-host-monitor

$(BUILD):
	mkdir -p $@

$(BUILD)/build_id.h: force $(SOURCES) $(HEADERS) Makefile memmap.ld | $(BUILD)
	@id=$$({ shasum -a 256 $(SOURCES) $(HEADERS) Makefile memmap.ld \
		$(ROOT)/src/arch/rp2350/picobin.S; printf '%s\n' '$(FLAGS)'; } | shasum -a 256 | cut -c1-8); \
	printf '#define USB_REPORT_BUILD_ID 0x%su\n' "$$id" > $@.tmp; \
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi

$(BUILD)/%.o: %.c $(HEADERS) $(BUILD)/build_id.h
	$(CC) $(FLAGS) -c $< -o $@

$(BUILD)/fs_wire.o $(BUILD)/fs_pio.o: FLAGS += -O3 -flto

$(BUILD)/picobin.o: $(ROOT)/src/arch/rp2350/picobin.S | $(BUILD)
	$(CC) $(FLAGS) -c $< -o $@

$(BUILD)/logi-host.elf: $(OBJECTS) memmap.ld
	$(CC) $(FLAGS) -O3 -flto -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-Tmemmap.ld $(OBJECTS) -Wl,-Map=$(BUILD)/logi-host.map -o $@
	@test -z "$$(llvm-nm -u $@)"
	llvm-size $@

$(BUILD)/logi-host.bin: $(BUILD)/logi-host.elf
	llvm-objcopy -O binary $< $@

$(ROOT)/build/host-tools/bin2uf2:
	$(MAKE) -C $(ROOT) build/host-tools/bin2uf2

$(BUILD)/logi-host.uf2: $(BUILD)/logi-host.bin $(ROOT)/build/host-tools/bin2uf2
	$(ROOT)/build/host-tools/bin2uf2 $< $@ 0x10000000 0xe48bff59

$(BUILD)/logi-host-monitor: monitor.c board.c fs_host.c fs_wire.c $(HEADERS) $(BUILD)/build_id.h
	cc -std=c11 -O2 -Wall -Wextra -Werror -I$(BUILD) -DLIVE_USB_LOGI_HOST \
		$< board.c fs_host.c fs_wire.c $(ROOT)/src/tools/diagnostics_logic.c \
		-framework IOKit -framework CoreFoundation -o $@

$(BUILD)/board-test: board-test.c board.c $(USB) $(SHARED) $(HEADERS) | $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
		-DLIVE_USB_LOGI_HOST $< board.c $(USB) $(SHARED) -o $@

$(BUILD)/monitor-test: monitor-test.c monitor.c board.c fs_host.c fs_wire.c $(HEADERS) $(BUILD)/build_id.h
	cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
		-I$(BUILD) -DLIVE_USB_LOGI_HOST $< board.c fs_host.c fs_wire.c $(ROOT)/src/tools/diagnostics_logic.c \
		-framework IOKit -framework CoreFoundation -o $@

$(BUILD)/fs-test: fs-test.c fs_wire.c fs_host.c $(HEADERS) | $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
		$< fs_wire.c fs_host.c $(ROOT)/src/tools/diagnostics_logic.c -o $@

$(BUILD)/fs-pio-test: fs-pio-test.c fs_pio.c fs_wire.c $(HEADERS) | $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
		-DLOGI_FS_TEST $< fs_pio.c fs_wire.c -o $@

test: $(BUILD)/board-test $(BUILD)/monitor-test $(BUILD)/fs-test $(BUILD)/fs-pio-test $(BUILD)/logi-host-monitor
	$(BUILD)/board-test
	$(BUILD)/monitor-test
	$(BUILD)/fs-test
	$(BUILD)/fs-pio-test
	$(BUILD)/logi-host-monitor --help
