# Run from the repository root: make -f clean-rx/vendor-burst/Makefile reference
include Makefile
.DEFAULT_GOAL := reference
REF_DIR := clean-rx/vendor-burst
REF_BUILD := build/clean-rx/vendor-burst
REF_CONTROLLER := $(REF_BUILD)/controller/cyw43_rx_fw.bin
REF_FLAGS := -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1
REF_CFLAGS := $(RP2350_CFLAGS) -DLIVE_USB_FIRMWARE -DLIVE_USB_CLEAN \
	-DWIFI_TRACE_SIZE=5888u -I$(REF_BUILD) -I$(WIFI_ASSET_DIR) $(REF_FLAGS)
REF_COMMON := $(REF_DIR)/main.c $(ARCH_DIR)/start.c $(ARCH_DIR)/bootrom_reboot.c \
	src/usb/live_usb_control.c src/usb/live_usb_hw.c $(SHARED_DIR)/usb_report.c \
	$(TOOLS_DIR)/diagnostics_logic.c $(PLATFORM_DIR)/wifi_board.c \
	$(PLATFORM_DIR)/display.c $(PLATFORM_DIR)/graphics.c \
	$(PLATFORM_DIR)/wifi_pio.c $(RADIO_DIR)/wifi_gspi.c $(RADIO_DIR)/wifi_probe.c \
	$(RADIO_DIR)/wifi_usb_report.c $(RADIO_DIR)/wifi_trace.c
REF_VENDOR := $(REF_COMMON) $(REF_DIR)/vendor.c $(WIFI_FW_SOURCES) $(RADIO_DIR)/wifi_assets.c
REF_OBSERVER := $(REF_COMMON) $(REF_DIR)/observer.c clean-rx/badge/loader.c
REF_VOBJS := $(addprefix $(REF_BUILD)/vendor/,$(notdir $(REF_VENDOR:.c=.o))) \
	$(REF_BUILD)/vendor/assets.o $(REF_BUILD)/picobin.o
REF_OOBJS := $(addprefix $(REF_BUILD)/observer/,$(notdir $(REF_OBSERVER:.c=.o))) \
	$(REF_BUILD)/observer/assets.o $(REF_BUILD)/picobin.o
REF_HEADERS := $(wildcard $(REF_DIR)/*.h clean-rx/src/*.h clean-rx/badge/loader.h \
	$(SHARED_DIR)/*.h src/usb/*.h $(RADIO_DIR)/*.h $(PLATFORM_DIR)/*.h $(ARCH_DIR)/*.h)
VPATH := $(sort $(dir $(REF_VENDOR) $(REF_OBSERVER)))

.PHONY: reference reference-early reference-phy reference-window reference-rearm reference-recover reference-recover-test reference-test reference-controller reference-force
reference: $(REF_BUILD)/vendor.uf2 $(REF_BUILD)/observer.uf2 $(REF_BUILD)/collector
reference-early:
	$(MAKE) -f $(REF_DIR)/Makefile REF_FLAGS='-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1' reference reference-test
reference-phy:
	$(MAKE) -f $(REF_DIR)/Makefile REF_FLAGS='-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_PHY_SNAPSHOT=1' reference reference-test
reference-window:
	$(MAKE) -f $(REF_DIR)/Makefile REF_FLAGS='-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1' reference reference-test
reference-rearm:
	$(MAKE) -f $(REF_DIR)/Makefile REF_FLAGS='-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1 -DCLEAN_RX_HT_REARM_WAIT=1' reference reference-test
reference-recover:
	$(MAKE) -f $(REF_DIR)/Makefile REF_FLAGS='-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1 -DCLEAN_RX_HT_REARM_WAIT=1 -DCLEAN_RX_HT_REARM_RECOVER=1' reference reference-test
reference-recover-test:
	$(MAKE) -f $(REF_DIR)/Makefile REF_FLAGS='-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1 -DCLEAN_RX_HT_REARM_WAIT=1 -DCLEAN_RX_HT_REARM_RECOVER=1 -DCLEAN_RX_HT_REARM_INJECT=1' reference reference-test
reference-controller:
	$(MAKE) -C clean-rx/src CC=clang BUILD=../../$(REF_BUILD)/controller EXTRA_CFLAGS='$(REF_FLAGS)' all test

$(REF_BUILD)/build_id.h: reference-force reference-controller $(WIFI_ASSET_FILES)
	@mkdir -p $(REF_BUILD)
	@id=$$(shasum -a 256 $(REF_VENDOR) $(REF_OBSERVER) $(REF_HEADERS) \
		$(REF_DIR)/collector.c $(REF_DIR)/Makefile $(REF_CONTROLLER) $(WIFI_ASSET_FILES) \
		| shasum -a 256 | cut -c1-8); \
	printf '#define USB_REPORT_BUILD_ID 0x%su\n' "$$id" > $@.tmp; \
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi

$(REF_BUILD)/vendor/%.o: %.c $(REF_BUILD)/build_id.h $(REF_HEADERS)
	@mkdir -p $(dir $@)
	$(RP2350_CC) $(REF_CFLAGS) -c $< -o $@
$(REF_BUILD)/observer/%.o: %.c $(REF_BUILD)/build_id.h $(REF_HEADERS)
	@mkdir -p $(dir $@)
	$(RP2350_CC) $(REF_CFLAGS) -c $< -o $@
$(REF_BUILD)/vendor/main.o $(REF_BUILD)/observer/main.o: $(REF_DIR)/main.c $(REF_BUILD)/build_id.h $(REF_HEADERS)
	@mkdir -p $(dir $@)
	$(RP2350_CC) $(REF_CFLAGS) -c $< -o $@
$(REF_BUILD)/vendor/assets.o: $(RADIO_DIR)/wifi_assets.S $(REF_BUILD)/build_id.h
	$(RP2350_CC) $(REF_CFLAGS) -DWIFI_FIRMWARE_BIN='"$(abspath $(WIFI_ASSET_DIR)/wifi_firmware.bin)"' \
		-DWIFI_NVRAM_BIN='"$(abspath $(WIFI_ASSET_DIR)/wifi_nvram.bin)"' \
		-DWIFI_CLM_BIN='"$(abspath $(WIFI_ASSET_DIR)/wifi_clm.bin)"' -c $< -o $@
$(REF_BUILD)/observer/assets.o: clean-rx/badge/assets.S $(REF_BUILD)/build_id.h
	$(RP2350_CC) $(REF_CFLAGS) -DCLEAN_CONTROLLER_PATH='"$(abspath $(REF_CONTROLLER))"' -c $< -o $@
$(REF_BUILD)/picobin.o: $(ARCH_DIR)/picobin.S
	@mkdir -p $(dir $@)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@
$(REF_BUILD)/vendor.elf: $(REF_VOBJS) $(ARCH_DIR)/memmap_report.ld
	$(RP2350_CC) $(REF_CFLAGS) -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(ARCH_DIR)/memmap_report.ld $(REF_VOBJS) -Wl,-Map=$(REF_BUILD)/vendor.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@
$(REF_BUILD)/observer.elf: $(REF_OOBJS) $(ARCH_DIR)/memmap_report.ld
	$(RP2350_CC) $(REF_CFLAGS) -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(ARCH_DIR)/memmap_report.ld $(REF_OOBJS) -Wl,-Map=$(REF_BUILD)/observer.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@
$(REF_BUILD)/%.bin: $(REF_BUILD)/%.elf
	$(RP2350_OBJCOPY) -O binary $< $@
$(REF_BUILD)/%.uf2: $(REF_BUILD)/%.bin $(BIN2UF2)
	$(BIN2UF2) $< $@ $(FLASH_BASE) $(RP2350_FAMILY_ID)
$(REF_BUILD)/collector: $(REF_DIR)/collector.c $(REF_BUILD)/build_id.h $(REF_HEADERS)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O2 $(REF_FLAGS) -DWIFI_TRACE_SIZE=5888u \
		-I$(REF_BUILD) $$(pkg-config --cflags libusb-1.0) $< $(SHARED_DIR)/usb_report.c \
		$(TOOLS_DIR)/diagnostics_logic.c $(RADIO_DIR)/wifi_trace.c \
		$$(pkg-config --libs libusb-1.0) -o $@
reference-test: $(REF_BUILD)/protocol_test
	$(REF_BUILD)/protocol_test
$(REF_BUILD)/protocol_test: $(REF_DIR)/protocol_test.c $(REF_HEADERS) $(REF_BUILD)/build_id.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g $(REF_FLAGS) -fsanitize=address,undefined \
		$< $(TOOLS_DIR)/diagnostics_logic.c $(PLATFORM_DIR)/graphics.c -o $@
