CC      ?= clang
OBJCOPY ?= llvm-objcopy
SIZE    ?= llvm-size
BUILD   ?= build

CFLAGS  := --target=arm-none-eabi -mcpu=cortex-m3 -mthumb -std=c11 \
           -Wall -Wextra -Werror -Oz \
           -ffreestanding -nostdinc -fno-builtin \
           -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables \
           -ffunction-sections -fdata-sections -I.
CFLAGS += $(EXTRA_CFLAGS)

LDFLAGS := -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings -T cyw43.ld

SRCS    := main.c pmu.c radio_phy.c psm_ucode.c mac.c block_ack.c
OBJS    := $(BUILD)/startup.o $(addprefix $(BUILD)/,$(SRCS:.c=.o))

.PHONY: all clean test force

all: $(BUILD)/cyw43_rx_fw.bin

$(BUILD)/build_flags: force | $(BUILD)
	@printf '%s\n' '$(CC)' '$(CFLAGS)' > $@.tmp; \
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi

$(OBJS): $(BUILD)/build_flags

.PHONY: unit-test integration-test runtime-test copy-scope-test early-rx-test test-copy-scope test-early-rx test-recovery
test unit-test integration-test runtime-test copy-scope-test early-rx-test test-copy-scope test-early-rx test-recovery:
	$(MAKE) -C ../tests BUILD=$(abspath $(BUILD)) EXTRA_CFLAGS='$(EXTRA_CFLAGS)' $@

$(BUILD):
	mkdir -p $@

$(BUILD)/startup.o: startup.S | $(BUILD)
	$(CC) --target=arm-none-eabi -mcpu=cortex-m3 -mthumb -nostdinc -c $< -o $@

$(BUILD)/%.o: %.c cyw43_hw.h types.h bytes.h sdio_ring.h psm_ack.h ht_observer.h rx_recovery.h copy_scope.h observer_dma.h early_rx.h unicast.h unicast_contract.h mac.h block_ack.h tx_diag.h phy_rates.h ht20.h ht_contract.h ht_command.h sdio_test.h frame_limits.h rx_contract.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/cyw43_rx_fw.elf: $(OBJS) cyw43.ld
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -Wl,-Map=$(BUILD)/cyw43_rx_fw.map -o $@
	$(SIZE) $@

$(BUILD)/cyw43_rx_fw.bin: $(BUILD)/cyw43_rx_fw.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD)
