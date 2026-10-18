ROOT := ../..
BUILD ?= $(ROOT)/build/clean-rx/badge
CONTROLLER_BIN ?= $(ROOT)/build/clean-rx/cyw43_rx_fw.bin
CC := clang
CFLAGS := --target=arm-none-eabi -mcpu=cortex-m33 -mthumb -std=c11 \
	-Wall -Wextra -Werror -Oz -ffreestanding -fno-builtin -fno-stack-protector \
	-fno-unwind-tables -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections -fstack-usage \
	-I$(ROOT)/src/shared -I$(BUILD) -DWIFI_TRACE_SIZE=2048u -DLIVE_USB_CLEAN
CFLAGS += $(EXTRA_CFLAGS)
SOURCES := main.c loader.c wpa2.c ../src/wpa2_crypto.c ../src/mac.c \
	$(ROOT)/src/arch/rp2350/start.c $(ROOT)/src/arch/rp2350/bootrom_reboot.c \
	$(ROOT)/src/usb/live_usb_control.c $(ROOT)/src/usb/live_usb_hw.c \
	$(ROOT)/src/shared/usb_report.c $(ROOT)/src/tools/diagnostics_logic.c \
	$(ROOT)/src/platform/badger/wifi_board.c $(ROOT)/src/platform/badger/wifi_pio.c \
	$(ROOT)/src/platform/badger/display.c $(ROOT)/src/platform/badger/graphics.c \
	$(ROOT)/src/radio/wifi_gspi.c $(ROOT)/src/radio/wifi_probe.c \
	$(ROOT)/src/radio/wifi_usb_report.c $(ROOT)/src/radio/wifi_trace.c
OBJECTS := $(addprefix $(BUILD)/,$(notdir $(SOURCES:.c=.o))) $(BUILD)/picobin.o $(BUILD)/assets.o
VPATH := $(sort $(dir $(SOURCES)))
HEADERS := $(wildcard *.h ../src/*.h $(ROOT)/src/tools/diagnostics_logic.h \
	$(addsuffix /*.h,$(addprefix $(ROOT)/src/,shared usb radio platform/badger arch/rp2350)))
NATIVE_TESTS := $(addprefix $(BUILD)/,loader_test loader_strict_test wpa2_test secret_test ht20_test)
LOADER_TEST_SOURCES := loader_test.c loader.c wpa2.c ../src/wpa2_crypto.c ../src/mac.c \
	$(ROOT)/src/radio/wifi_gspi.c $(ROOT)/src/radio/wifi_probe.c \
	$(ROOT)/src/tools/diagnostics_logic.c

.PHONY: all controller force test
all: $(BUILD)/clean_rx.uf2 $(BUILD)/collector

ifeq ($(abspath $(CONTROLLER_BIN)),$(abspath $(ROOT)/build/clean-rx/cyw43_rx_fw.bin))
controller:
	$(MAKE) -C ../src CC=clang BUILD=../../build/clean-rx EXTRA_CFLAGS='$(EXTRA_CFLAGS)'
else
controller:
	@test -f "$(CONTROLLER_BIN)" || { echo "Build the supplied CONTROLLER_BIN first" >&2; exit 1; }
endif

$(BUILD):
	mkdir -p $@

$(BUILD)/build_id.h: force controller | $(BUILD)
	@id=$$({ shasum -a 256 $(SOURCES) $(HEADERS) collector.c assets.S Makefile $(CONTROLLER_BIN); \
		printf '%s\n' '$(EXTRA_CFLAGS)'; } | shasum -a 256 | cut -c1-8); \
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

$(BUILD)/collector: collector.c frames.h station.h ../src/wpa2_crypto.c ../src/mac.c ../src/tx_diag.h $(BUILD)/build_id.h
	cc -std=c11 -Wall -Wextra -Werror -O2 $(EXTRA_CFLAGS) -I$(BUILD) -DWIFI_TRACE_SIZE=2048u $$(pkg-config --cflags libusb-1.0) \
		$< ../src/wpa2_crypto.c ../src/mac.c $(ROOT)/src/shared/usb_report.c $(ROOT)/src/tools/diagnostics_logic.c \
		$(ROOT)/src/radio/wifi_trace.c $$(pkg-config --libs libusb-1.0) -o $@

test: $(NATIVE_TESTS)
	$(BUILD)/loader_test
	$(BUILD)/loader_strict_test
	$(BUILD)/wpa2_test
	$(BUILD)/secret_test
	$(BUILD)/ht20_test
	bash -n $(ROOT)/src/host/tools/usb_wifi_clean_wpa2_run.sh
	bash -n mac_trials.sh
	bash -n mac_soak.sh
	bash -n ht_probe.sh

$(NATIVE_TESTS): $(HEADERS) $(ROOT)/src/tools/diagnostics_logic.c $(BUILD)/build_id.h | $(BUILD)

$(BUILD)/wpa2_test: wpa2_test.c wpa2.c wpa2.h station.h ../src/wpa2_crypto.c ../src/wpa2_crypto.h ../src/mac.c
	cc -std=c11 -Wall -Wextra -Werror -O1 -g $(EXTRA_CFLAGS) -fsanitize=address,undefined wpa2_test.c wpa2.c ../src/wpa2_crypto.c ../src/mac.c \
		$(ROOT)/src/tools/diagnostics_logic.c -o $@

$(BUILD)/secret_test: secret_test.c $(ROOT)/src/usb/live_usb_control.c $(ROOT)/src/usb/live_usb.h \
		$(ROOT)/src/radio/wifi_trace.c $(ROOT)/src/shared/usb_report.c | $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O1 -g $(EXTRA_CFLAGS) -fsanitize=address,undefined -DLIVE_USB_CLEAN \
		secret_test.c $(ROOT)/src/usb/live_usb_control.c $(ROOT)/src/radio/wifi_trace.c \
		$(ROOT)/src/shared/usb_report.c $(ROOT)/src/tools/diagnostics_logic.c -o $@

$(BUILD)/loader_strict_test: $(LOADER_TEST_SOURCES) | $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O2 $(EXTRA_CFLAGS) -DCLEAN_RX_STRICT_TEST $(LOADER_TEST_SOURCES) -o $@

$(BUILD)/loader_test: $(LOADER_TEST_SOURCES) | $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O2 $(EXTRA_CFLAGS) $(LOADER_TEST_SOURCES) -o $@

$(BUILD)/ht20_test: ht20_test.c ../src/mac.c $(ROOT)/src/tools/diagnostics_logic.c | $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O1 -g $(EXTRA_CFLAGS) -fsanitize=address,undefined \
		ht20_test.c ../src/mac.c $(ROOT)/src/tools/diagnostics_logic.c -o $@
