# Invoke from the repository root: make -f clean-rx/peer/Makefile ht-peer
include Makefile
.DEFAULT_GOAL := ht-peer

HT_PEER_DIR := clean-rx/peer
HT_PEER_BUILD := build/clean-rx/peer
HT_PEER_SHARED := $(filter-out $(FIRMWARE_DIR)/usb_wifi_firmware.o,$(USB_WIFI_OBJS)) \
	$(FIRMWARE_DIR)/wifi_link.o $(HT_PEER_BUILD)/clock.o
HT_PEER_INPUTS := $(HT_PEER_DIR)/main.c $(HT_PEER_DIR)/collector.c \
	$(HT_PEER_DIR)/protocol.h $(HT_PEER_DIR)/frame.h $(HT_PEER_DIR)/lifecycle.h \
	$(HT_PEER_DIR)/lifecycle_usb.h $(HT_PEER_DIR)/Makefile \
	$(USB_WIFI_INPUTS) $(WIFI_ASSET_FILES) $(RADIO_DIR)/wifi_link.c $(RADIO_DIR)/wifi_link.h \
	$(ARCH_DIR)/nametag_clock.c $(ARCH_DIR)/nametag_clock.h

.PHONY: ht-peer ht-peer-test
ht-peer: $(HT_PEER_BUILD)/vendor_ht.uf2 $(HT_PEER_BUILD)/collector

$(HT_PEER_BUILD):
	mkdir -p $@

$(HT_PEER_BUILD)/build_id.h: $(HT_PEER_INPUTS) | $(HT_PEER_BUILD)
	@id=$$(shasum -a 256 $(HT_PEER_INPUTS) | shasum -a 256 | cut -c1-8); \
	printf '#define USB_REPORT_BUILD_ID 0x%su\n' "$$id" > $@

$(HT_PEER_BUILD)/main.o: $(HT_PEER_DIR)/main.c $(HT_PEER_BUILD)/build_id.h
	$(RP2350_CC) $(RP2350_CFLAGS) -DLIVE_USB_FIRMWARE -I$(HT_PEER_BUILD) -c $< -o $@

$(HT_PEER_BUILD)/clock.o: $(ARCH_DIR)/nametag_clock.c $(ARCH_DIR)/nametag_clock.h | $(HT_PEER_BUILD)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(HT_PEER_BUILD)/vendor_ht.elf: $(HT_PEER_BUILD)/main.o $(COMMON_FIRMWARE_OBJS) $(HT_PEER_SHARED) $(ARCH_DIR)/memmap_report.ld
	$(RP2350_CC) $(RP2350_CFLAGS) -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(ARCH_DIR)/memmap_report.ld $(HT_PEER_BUILD)/main.o $(COMMON_FIRMWARE_OBJS) \
		$(HT_PEER_SHARED) -Wl,-Map=$(HT_PEER_BUILD)/vendor_ht.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

$(HT_PEER_BUILD)/vendor_ht.bin: $(HT_PEER_BUILD)/vendor_ht.elf
	$(RP2350_OBJCOPY) -O binary $< $@

$(HT_PEER_BUILD)/vendor_ht.uf2: $(HT_PEER_BUILD)/vendor_ht.bin $(BIN2UF2)
	$(BIN2UF2) $< $@ $(FLASH_BASE) $(RP2350_FAMILY_ID)

$(HT_PEER_BUILD)/collector: $(HT_PEER_DIR)/collector.c $(HT_PEER_BUILD)/build_id.h
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O2 -I$(HT_PEER_BUILD) \
		$$(pkg-config --cflags libusb-1.0) $< $(SHARED_DIR)/usb_report.c \
		$(TOOLS_DIR)/diagnostics_logic.c $(RADIO_DIR)/wifi_trace.c \
		$$(pkg-config --libs libusb-1.0) -o $@

ht-peer-test: $(HT_PEER_BUILD)/protocol_test
	$(HT_PEER_BUILD)/protocol_test

$(HT_PEER_BUILD)/protocol_test: $(HT_PEER_DIR)/protocol_test.c $(HT_PEER_DIR)/protocol.h $(HT_PEER_DIR)/frame.h $(HT_PEER_DIR)/lifecycle_usb.h src/usb/live_usb_control.c | $(HT_PEER_BUILD)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -DLIVE_USB_FIRMWARE \
		$< src/usb/live_usb_control.c $(SHARED_DIR)/usb_report.c \
		$(TOOLS_DIR)/diagnostics_logic.c $(WIFI_USB_HOST_SOURCES) -o $@
