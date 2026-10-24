ROOT := ../..
BUILD ?= $(ROOT)/build/clean-rx/badge
CONTROLLER_BIN ?= $(ROOT)/build/clean-rx/cyw43_rx_fw.bin
PROFILE ?= diagnostic
include ../src/firmware.mk
CC := clang
CFLAGS := --target=arm-none-eabi -mcpu=cortex-m33 -mthumb -std=c11 \
	-Wall -Wextra -Werror -Oz -ffreestanding -fno-builtin -fno-stack-protector \
	-fno-unwind-tables -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections -fstack-usage \
	-I$(ROOT)/src/shared -I$(BUILD) -DWIFI_TRACE_SIZE=2092u -DLIVE_USB_CLEAN
HOST_FLAGS := $(PROFILE_FLAGS) $(SELECTION_FLAGS) $(EXTRA_CFLAGS)
DEV := ../tools/badge
CFLAGS += $(HOST_FLAGS)
SOURCES := main.c loader.c wpa2.c ../host/wpa2_crypto.c ../src/mac.c ../src/block_ack.c ../peer/network.c \
	$(ROOT)/src/arch/rp2350/start.c $(ROOT)/src/arch/rp2350/bootrom_reboot.c \
	$(ROOT)/src/arch/rp2350/nametag_clock.c \
	$(ROOT)/src/usb/live_usb_control.c $(ROOT)/src/usb/live_usb_hw.c \
	$(ROOT)/src/shared/usb_report.c $(ROOT)/src/tools/diagnostics_logic.c \
	$(ROOT)/src/platform/badger/wifi_board.c $(ROOT)/src/platform/badger/wifi_pio.c \
	$(ROOT)/src/platform/badger/display.c $(ROOT)/src/platform/badger/graphics.c \
	$(ROOT)/src/radio/wifi_gspi.c $(ROOT)/src/radio/wifi_probe.c \
	$(ROOT)/src/radio/wifi_usb_report.c $(ROOT)/src/radio/wifi_trace.c
OBJECTS := $(addprefix $(BUILD)/,$(notdir $(SOURCES:.c=.o))) $(BUILD)/picobin.o $(BUILD)/assets.o
VPATH := $(sort $(dir $(SOURCES)))
HEADERS := $(wildcard *.h ../src/*.h ../src/psm/*.h ../src/diagnostic/*.h ../host/*.h ../tests/fixtures/*.h $(ROOT)/src/tools/diagnostics_logic.h \
	../peer/frame.h ../peer/protocol.h ../peer/lifecycle.h ../peer/network.h ../peer/network_json.h \
	$(addsuffix /*.h,$(addprefix $(ROOT)/src/,shared usb radio platform/badger arch/rp2350)))
NATIVE_TESTS := $(addprefix $(BUILD)/,loader_test loader_strict_test wpa2_test secret_test ht20_test station_lifecycle_test)
LOADER_TEST_SOURCES := loader_test.c loader.c wpa2.c ../host/wpa2_crypto.c ../src/mac.c ../src/block_ack.c \
	$(ROOT)/src/radio/wifi_gspi.c $(ROOT)/src/radio/wifi_probe.c \
	$(ROOT)/src/tools/diagnostics_logic.c

.PHONY: all firmware tools controller force test
all: $(BUILD)/clean_rx.uf2 $(BUILD)/collector
firmware: $(BUILD)/clean_rx.uf2
tools: $(BUILD)/collector

ifeq ($(abspath $(CONTROLLER_BIN)),$(abspath $(ROOT)/build/clean-rx/cyw43_rx_fw.bin))
controller:
	$(MAKE) -C ../src CC=clang BUILD=../../build/clean-rx PROFILE=$(PROFILE) SELECTION=$(SELECTION) EXTRA_CFLAGS='$(EXTRA_CFLAGS)'
else
controller:
	@test -f "$(CONTROLLER_BIN)" || { echo "Build the supplied CONTROLLER_BIN first" >&2; exit 1; }
endif

$(BUILD):
	mkdir -p $@

$(BUILD)/build_id.h: force controller | $(BUILD)
	@id=$$({ shasum -a 256 $(SOURCES) $(HEADERS) assets.S Makefile $(CONTROLLER_BIN); \
		printf '%s\n' '$(HOST_FLAGS)'; } | shasum -a 256 | cut -c1-8); \
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

$(BUILD)/collector: $(DEV)/collector.c frames.h station.h ../host/wpa2_crypto.c ../src/mac.c ../src/block_ack.c ../src/tx_diag.h $(BUILD)/build_id.h
	cc -std=c11 -Wall -Wextra -Werror -O2 $(HOST_FLAGS) -I$(BUILD) -DWIFI_TRACE_SIZE=2092u $$(pkg-config --cflags libusb-1.0) \
		$< ../peer/network.c ../host/wpa2_crypto.c ../src/mac.c ../src/block_ack.c $(ROOT)/src/shared/usb_report.c $(ROOT)/src/tools/diagnostics_logic.c \
		$(ROOT)/src/radio/wifi_trace.c $$(pkg-config --libs libusb-1.0) -o $@

test: $(NATIVE_TESTS) $(BUILD)/collector
	$(BUILD)/loader_test
	$(BUILD)/loader_strict_test
	$(BUILD)/wpa2_test
	$(BUILD)/secret_test
	$(BUILD)/ht20_test
	$(BUILD)/station_lifecycle_test
	node $(DEV)/network_verify.cjs --self-test
	node ../tools/ampdu_verify.cjs --self-test $(BUILD)/collector
	node $(DEV)/lifecycle_verify.cjs --self-test
	node $(DEV)/qualification_verify.cjs --self-test
	node $(DEV)/wpa2_verify.cjs --self-test $(BUILD)/collector
	node $(DEV)/wpa2_rekey_verify.cjs --self-test
	bash -n $(DEV)/lifecycle_probe.sh
	bash -n $(DEV)/qualification_probe.sh
	bash -n $(ROOT)/src/host/tools/usb_wifi_clean_wpa2_run.sh
	bash -n $(DEV)/mac_trials.sh
	bash -n $(DEV)/mac_soak.sh
	bash -n $(DEV)/ht_probe.sh
	bash -n $(DEV)/wpa2_trial.sh
	bash -n $(DEV)/wpa2_matrix.sh

$(NATIVE_TESTS): $(HEADERS) $(ROOT)/src/tools/diagnostics_logic.c $(BUILD)/build_id.h | $(BUILD)

$(BUILD)/wpa2_test: wpa2_test.c wpa2.c wpa2.h station.h ../host/wpa2_crypto.c ../host/wpa2_crypto.h ../src/mac.c ../src/block_ack.c
	cc -std=c11 -Wall -Wextra -Werror -O1 -g $(HOST_FLAGS) -fsanitize=address,undefined wpa2_test.c wpa2.c ../host/wpa2_crypto.c ../src/mac.c ../src/block_ack.c \
		$(ROOT)/src/tools/diagnostics_logic.c -o $@

$(BUILD)/secret_test: secret_test.c $(ROOT)/src/usb/live_usb_control.c $(ROOT)/src/usb/live_usb.h \
		$(ROOT)/src/radio/wifi_trace.c $(ROOT)/src/shared/usb_report.c | $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O1 -g $(HOST_FLAGS) -fsanitize=address,undefined -DLIVE_USB_CLEAN \
		secret_test.c $(ROOT)/src/usb/live_usb_control.c $(ROOT)/src/radio/wifi_trace.c \
		$(ROOT)/src/shared/usb_report.c $(ROOT)/src/tools/diagnostics_logic.c -o $@

$(BUILD)/loader_strict_test: $(LOADER_TEST_SOURCES) | $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O2 $(HOST_FLAGS) -DCLEAN_RX_STRICT_TEST $(LOADER_TEST_SOURCES) -o $@

$(BUILD)/loader_test: $(LOADER_TEST_SOURCES) | $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O2 $(HOST_FLAGS) $(LOADER_TEST_SOURCES) -o $@

$(BUILD)/ht20_test: ht20_test.c ../src/mac.c ../src/block_ack.c $(ROOT)/src/tools/diagnostics_logic.c | $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O1 -g $(HOST_FLAGS) -fsanitize=address,undefined \
		ht20_test.c ../src/mac.c ../src/block_ack.c $(ROOT)/src/tools/diagnostics_logic.c -o $@

$(BUILD)/station_lifecycle_test: station_lifecycle_test.c station_lifecycle.h ../peer/lifecycle.h | $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O1 -g $(HOST_FLAGS) -fsanitize=address,undefined \
		$< $(ROOT)/src/tools/diagnostics_logic.c -o $@
