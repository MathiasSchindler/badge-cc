# Invoke from the repository root: make -f clean-rx/peer/Makefile ht-peer
include Makefile
.DEFAULT_GOAL := ht-peer

HT_PEER_DIR := clean-rx/peer
HT_PEER_BUILD := build/clean-rx/peer
HT_PEER_PICO ?= 0
HT_PEER_FLAGS := -DCLEAN_PEER_PICO=$(HT_PEER_PICO)
HT_PEER_SHARED := $(filter-out $(FIRMWARE_DIR)/usb_wifi_firmware.o \
	$(FIRMWARE_DIR)/live_usb_firmware_control.o $(FIRMWARE_DIR)/live_usb_hw.o,$(USB_WIFI_OBJS)) \
	$(FIRMWARE_DIR)/wifi_link.o $(HT_PEER_BUILD)/clock.o $(HT_PEER_BUILD)/network.o \
	$(HT_PEER_BUILD)/live_usb_control.o $(HT_PEER_BUILD)/live_usb_hw.o $(HT_PEER_BUILD)/wpa2_crypto.o \
	$(HT_PEER_BUILD)/authenticator.o
HT_PEER_INPUTS := $(HT_PEER_DIR)/main.c $(HT_PEER_DIR)/collector.c \
	$(HT_PEER_DIR)/protocol.h $(HT_PEER_DIR)/frame.h $(HT_PEER_DIR)/lifecycle.h \
	$(HT_PEER_DIR)/lifecycle_usb.h $(HT_PEER_DIR)/Makefile \
	$(HT_PEER_DIR)/network.c $(HT_PEER_DIR)/network.h $(HT_PEER_DIR)/network_json.h \
	$(HT_PEER_DIR)/qualification.h \
	$(HT_PEER_DIR)/protected_ap.h $(HT_PEER_DIR)/authenticator.c $(HT_PEER_DIR)/authenticator.h \
	$(HT_PEER_DIR)/cck_monitor.h clean-rx/src/mac_soak.h clean-rx/src/phy_rates.h \
	clean-rx/src/wpa2_crypto.c clean-rx/src/wpa2_crypto.h \
	$(USB_WIFI_INPUTS) $(WIFI_ASSET_FILES) $(RADIO_DIR)/wifi_link.c $(RADIO_DIR)/wifi_link.h \
	$(ARCH_DIR)/nametag_clock.c $(ARCH_DIR)/nametag_clock.h
ifeq ($(HT_PEER_PICO),1)
HT_PEER_SHARED += $(HT_PEER_BUILD)/pico_sys.o
HT_PEER_INPUTS += pico2w/radio/clock.c
else ifneq ($(HT_PEER_PICO),0)
$(error HT_PEER_PICO must be 0 or 1)
endif

.PHONY: ht-peer ht-peer-test ht-peer-force
ht-peer: $(HT_PEER_BUILD)/vendor_ht.uf2 $(HT_PEER_BUILD)/collector

$(HT_PEER_BUILD):
	mkdir -p $@

$(HT_PEER_BUILD)/build_id.h: $(HT_PEER_INPUTS) ht-peer-force | $(HT_PEER_BUILD)
	@id=$$({ shasum -a 256 $(HT_PEER_INPUTS); printf '%s\n' '$(HT_PEER_FLAGS)'; } | shasum -a 256 | cut -c1-8); \
	printf '#define USB_REPORT_BUILD_ID 0x%su\n' "$$id" > $@.tmp; \
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi

$(HT_PEER_BUILD)/main.o: $(HT_PEER_DIR)/main.c $(HT_PEER_BUILD)/build_id.h
	$(RP2350_CC) $(RP2350_CFLAGS) $(HT_PEER_FLAGS) -fstack-usage -DLIVE_USB_FIRMWARE -DLIVE_USB_CLEAN -I$(HT_PEER_BUILD) -c $< -o $@

$(HT_PEER_BUILD)/live_usb_control.o: src/usb/live_usb_control.c src/usb/live_usb.h | $(HT_PEER_BUILD)
	$(RP2350_CC) $(RP2350_CFLAGS) -DLIVE_USB_FIRMWARE -DLIVE_USB_CLEAN -c $< -o $@

$(HT_PEER_BUILD)/live_usb_hw.o: src/usb/live_usb_hw.c src/usb/live_usb.h | $(HT_PEER_BUILD)
	$(RP2350_CC) $(RP2350_CFLAGS) -DLIVE_USB_FIRMWARE -DLIVE_USB_CLEAN -c $< -o $@

$(HT_PEER_BUILD)/wpa2_crypto.o: clean-rx/src/wpa2_crypto.c clean-rx/src/wpa2_crypto.h | $(HT_PEER_BUILD)
	$(RP2350_CC) $(RP2350_CFLAGS) -fstack-usage -c $< -o $@

$(HT_PEER_BUILD)/authenticator.o: $(HT_PEER_DIR)/authenticator.c $(HT_PEER_DIR)/authenticator.h | $(HT_PEER_BUILD)
	$(RP2350_CC) $(RP2350_CFLAGS) -fstack-usage -c $< -o $@

$(HT_PEER_BUILD)/pico_sys.o: pico2w/radio/clock.c | $(HT_PEER_BUILD)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(HT_PEER_BUILD)/clock.o: $(ARCH_DIR)/nametag_clock.c $(ARCH_DIR)/nametag_clock.h | $(HT_PEER_BUILD)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@

$(HT_PEER_BUILD)/network.o: $(HT_PEER_DIR)/network.c $(HT_PEER_DIR)/network.h | $(HT_PEER_BUILD)
	$(RP2350_CC) $(RP2350_CFLAGS) -fstack-usage -c $< -o $@

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
		$$(pkg-config --cflags libusb-1.0) $< $(HT_PEER_DIR)/network.c $(SHARED_DIR)/usb_report.c \
		clean-rx/src/wpa2_crypto.c $(TOOLS_DIR)/diagnostics_logic.c $(RADIO_DIR)/wifi_trace.c \
		$$(pkg-config --libs libusb-1.0) -o $@

ht-peer-test: $(HT_PEER_BUILD)/protocol_test $(HT_PEER_BUILD)/network_test $(HT_PEER_BUILD)/qualification_test $(HT_PEER_BUILD)/authenticator_test $(HT_PEER_BUILD)/cck_monitor_test
	$(HT_PEER_BUILD)/protocol_test
	$(HT_PEER_BUILD)/network_test
	$(HT_PEER_BUILD)/qualification_test
	$(HT_PEER_BUILD)/authenticator_test
	$(HT_PEER_BUILD)/cck_monitor_test

$(HT_PEER_BUILD)/cck_monitor_test: $(HT_PEER_DIR)/cck_monitor_test.c $(HT_PEER_DIR)/cck_monitor.h $(HT_PEER_DIR)/protocol.h $(wildcard clean-rx/src/*.h) clean-rx/src/mac.c clean-rx/src/block_ack.c $(SHARED_DIR)/usb_report.c $(TOOLS_DIR)/diagnostics_logic.c | $(HT_PEER_BUILD)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
		$< clean-rx/src/mac.c clean-rx/src/block_ack.c $(SHARED_DIR)/usb_report.c \
		$(TOOLS_DIR)/diagnostics_logic.c -o $@

$(HT_PEER_BUILD)/authenticator_test: $(HT_PEER_DIR)/authenticator_test.c $(HT_PEER_DIR)/authenticator.c $(HT_PEER_DIR)/authenticator.h $(wildcard clean-rx/badge/*.h clean-rx/src/*.h) clean-rx/badge/wpa2.c clean-rx/src/wpa2_crypto.c clean-rx/src/mac.c clean-rx/src/block_ack.c | $(HT_PEER_BUILD)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
		$< $(HT_PEER_DIR)/authenticator.c clean-rx/badge/wpa2.c clean-rx/src/wpa2_crypto.c \
		clean-rx/src/mac.c clean-rx/src/block_ack.c $(TOOLS_DIR)/diagnostics_logic.c -o $@

$(HT_PEER_BUILD)/qualification_test: $(HT_PEER_DIR)/qualification_test.c $(HT_PEER_DIR)/qualification.h $(HT_PEER_DIR)/protected_ap.h clean-rx/src/wpa2_crypto.c $(RADIO_DIR)/wifi_link.c $(RADIO_DIR)/wifi_link.h | $(HT_PEER_BUILD)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
		$< $(RADIO_DIR)/wifi_link.c clean-rx/src/wpa2_crypto.c -o $@

$(HT_PEER_BUILD)/network_test: $(HT_PEER_DIR)/network_test.c $(HT_PEER_DIR)/network.c $(HT_PEER_DIR)/network.h $(wildcard clean-rx/badge/*.h clean-rx/src/*.h) | $(HT_PEER_BUILD)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
		$< $(HT_PEER_DIR)/network.c $(TOOLS_DIR)/diagnostics_logic.c -o $@

$(HT_PEER_BUILD)/protocol_test: $(HT_PEER_DIR)/protocol_test.c $(HT_PEER_DIR)/protocol.h $(HT_PEER_DIR)/frame.h $(HT_PEER_DIR)/lifecycle_usb.h $(HT_PEER_DIR)/protected_ap.h clean-rx/src/wpa2_crypto.c $(HT_PEER_DIR)/Makefile src/usb/live_usb_control.c | $(HT_PEER_BUILD)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -DLIVE_USB_FIRMWARE -DLIVE_USB_CLEAN \
		$< clean-rx/src/wpa2_crypto.c src/usb/live_usb_control.c $(SHARED_DIR)/usb_report.c \
		$(TOOLS_DIR)/diagnostics_logic.c $(WIFI_USB_HOST_SOURCES) -o $@
