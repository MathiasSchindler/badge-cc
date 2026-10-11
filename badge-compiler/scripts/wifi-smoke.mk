# Invoke with the root Makefile first; reuse its exact Wi-Fi source/asset set.
override RP2350_CC := bash badge-compiler/scripts/owned-cc.sh
override RP2350_OBJCOPY := badge-compiler/build/elf2bin
override RP2350_NM := badge-compiler/build/badge-elf-info
override RP2350_SIZE := badge-compiler/build/badge-elf-info
override RP2350_CFLAGS += -DBADGER_FAST_BYTES -DBADGER_WIFI_SMOKE_40M

WIFI_SMOKE_DIR := badge-compiler/build/wifi-smoke
WIFI_SMOKE_LINKER := badge-compiler/scripts/memmap_wifi_smoke.ld
WIFI_SMOKE_FAST_OBJS := $(addprefix $(WIFI_SMOKE_DIR)/,bytes.o wifi_pio_fifo.o display_pixels.o)
USB_UDP_INPUTS += $(WIFI_SMOKE_LINKER) badge-compiler/scripts/wifi-smoke.mk \
	$(ARCH_DIR)/bytes.S $(ARCH_DIR)/wifi_pio_fifo.S $(ARCH_DIR)/display_pixels.S \
	$(PLATFORM_DIR)/wifi_pio_fifo.h

.PHONY: owned-wifi-smoke
owned-wifi-smoke: $(WIFI_SMOKE_DIR)/usb_wifi_udp.uf2 $(HOST_TOOLS_DIR)/usb_wifi_udp_host

$(WIFI_SMOKE_DIR)/usb_wifi_udp.elf: $(COMMON_FIRMWARE_OBJS) $(USB_UDP_OBJS) $(WIFI_SMOKE_FAST_OBJS) $(WIFI_SMOKE_LINKER) | $(WIFI_SMOKE_DIR)
	$(RP2350_CC) $(RP2350_CFLAGS) -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(WIFI_SMOKE_LINKER) $(COMMON_FIRMWARE_OBJS) $(USB_UDP_OBJS) $(WIFI_SMOKE_FAST_OBJS) \
		-Wl,-Map=$(WIFI_SMOKE_DIR)/usb_wifi_udp.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@

$(WIFI_SMOKE_DIR)/usb_wifi_udp.bin: $(WIFI_SMOKE_DIR)/usb_wifi_udp.elf
	$(RP2350_OBJCOPY) -O binary $< $@

$(WIFI_SMOKE_FAST_OBJS): $(WIFI_SMOKE_DIR)/%.o: $(ARCH_DIR)/%.S | $(WIFI_SMOKE_DIR)
	$(RP2350_CC) $(RP2350_ASFLAGS) -c $< -o $@

$(WIFI_SMOKE_DIR)/usb_wifi_udp.uf2: $(WIFI_SMOKE_DIR)/usb_wifi_udp.bin $(BIN2UF2)
	$(BIN2UF2) $< $@ $(FLASH_BASE) $(RP2350_FAMILY_ID)

$(WIFI_SMOKE_DIR):
	mkdir -p $@
