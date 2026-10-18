CC      ?= clang
OBJCOPY ?= llvm-objcopy
SIZE    ?= llvm-size
BUILD   ?= build

CFLAGS  := --target=arm-none-eabi -mcpu=cortex-m3 -mthumb -std=c11 \
           -Wall -Wextra -Werror -Oz \
           -ffreestanding -nostdinc -fno-builtin \
           -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables \
           -ffunction-sections -fdata-sections -I. -I../../src/shared
CFLAGS += $(EXTRA_CFLAGS)

LDFLAGS := -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings -T cyw43.ld

SRCS    := main.c pmu.c radio_phy.c psm_ucode.c mac.c block_ack.c
OBJS    := $(BUILD)/startup.o $(addprefix $(BUILD)/,$(SRCS:.c=.o))

.PHONY: all clean test force

all: $(BUILD)/cyw43_rx_fw.bin

$(BUILD)/build_flags: force | $(BUILD)
	@printf '%s\n' '$(CC)' '$(CFLAGS)' > $@.tmp; \
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi

$(OBJS) $(BUILD)/psm_ucode_test $(BUILD)/mac_test: $(BUILD)/build_flags

test: $(BUILD)/psm_ucode_test $(BUILD)/mac_test $(BUILD)/block_ack_test
	$(BUILD)/psm_ucode_test
	$(BUILD)/mac_test
	$(BUILD)/block_ack_test

$(BUILD)/block_ack_test: block_ack_test.c block_ack.c block_ack.h mac.c mac.h frame_limits.h $(BUILD)/build_flags | $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O1 -g $(EXTRA_CFLAGS) -fsanitize=address,undefined \
		block_ack_test.c block_ack.c mac.c -o $@

$(BUILD)/mac_test: mac_test.c mac.c $(wildcard *.h ../badge/*.h) ../../src/shared/stdint.h ../../src/shared/bytes.h \
		../../src/tools/diagnostics_logic.c ../../src/tools/diagnostics_logic.h | $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O1 -g $(EXTRA_CFLAGS) -fsanitize=address,undefined \
		mac_test.c mac.c ../../src/tools/diagnostics_logic.c -o $@

$(BUILD)/psm_ucode_test: Makefile psm_ucode_test.c psm_ucode.c psm_ack.h unicast.h phy_rates.h frame_limits.h ../../src/shared/stdint.h \
		../../src/tools/diagnostics_logic.c ../../src/tools/diagnostics_logic.h | $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O1 -g $(EXTRA_CFLAGS) -fsanitize=address,undefined psm_ucode_test.c psm_ucode.c \
		../../src/tools/diagnostics_logic.c -o $@

$(BUILD):
	mkdir -p $@

$(BUILD)/startup.o: startup.S | $(BUILD)
	$(CC) --target=arm-none-eabi -mcpu=cortex-m3 -mthumb -nostdinc -c $< -o $@

$(BUILD)/%.o: %.c cyw43_hw.h types.h sdio_ring.h psm_ack.h unicast.h mac.h block_ack.h ../../src/shared/stdint.h tx_diag.h phy_rates.h ht20.h sdio_test.h frame_limits.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/cyw43_rx_fw.elf: $(OBJS) cyw43.ld
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -Wl,-Map=$(BUILD)/cyw43_rx_fw.map -o $@
	$(SIZE) $@

$(BUILD)/cyw43_rx_fw.bin: $(BUILD)/cyw43_rx_fw.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD)
