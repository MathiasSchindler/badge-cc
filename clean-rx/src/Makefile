CC      ?= clang
OBJCOPY ?= llvm-objcopy
SIZE    ?= llvm-size
BUILD   ?= build
include firmware.mk

CFLAGS  := --target=arm-none-eabi -mcpu=cortex-m3 -mthumb -std=c11 \
           -Wall -Wextra -Werror -Oz \
           -ffreestanding -nostdinc -fno-builtin \
           -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables \
           -ffunction-sections -fdata-sections -I.
CFLAGS += $(PROFILE_FLAGS) $(EXTRA_CFLAGS)

LDFLAGS := -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings -T cyw43.ld \
           -Wl,--defsym=clean_rx_profile_id=$(PROFILE_ID)

SRCS    := main.c pmu.c radio_phy.c psm_ucode.c mac.c block_ack.c
OBJS    := $(BUILD)/startup.o $(addprefix $(BUILD)/,$(SRCS:.c=.o))
HEADERS := $(wildcard *.h runtime/*.h runtime/*.inc psm/*.h)
ifneq ($(PROFILE),runtime)
HEADERS += $(wildcard diagnostic/*.h diagnostic/*.inc)
endif

.PHONY: all clean test force

all: $(BUILD)/cyw43_rx_fw.bin $(BUILD)/profile.txt

# Some supported Make versions compare timestamps only to whole seconds.
FLAGS_CHANGED := $(shell printf '%s\n' '$(CC)' '$(CFLAGS)' '$(PROFILE)' | cmp -s - '$(BUILD)/build_flags' || printf changed)
ifneq ($(FLAGS_CHANGED),)
$(OBJS) $(BUILD)/cyw43_rx_fw.elf $(BUILD)/cyw43_rx_fw.bin $(BUILD)/profile.txt: force
endif

$(BUILD)/profile.txt: $(BUILD)/cyw43_rx_fw.bin $(BUILD)/build_flags
	@printf '%s\n' 'profile=$(PROFILE)' 'profile_id=$(PROFILE_ID)' 'flags=$(CFLAGS)' > $@

$(BUILD)/build_flags: force | $(BUILD)
	@printf '%s\n' '$(CC)' '$(CFLAGS)' '$(PROFILE)' > $@.tmp; \
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi

$(OBJS): $(BUILD)/build_flags

.PHONY: unit-test integration-test runtime-test copy-scope-test early-rx-test test-copy-scope test-early-rx test-recovery
test unit-test integration-test runtime-test copy-scope-test early-rx-test test-copy-scope test-early-rx test-recovery:
	$(MAKE) -C ../tests BUILD=$(abspath $(BUILD)) PROFILE=$(PROFILE) EXTRA_CFLAGS='$(EXTRA_CFLAGS)' $@

.PHONY: diagnostic sdio-test
diagnostic sdio-test:
	$(MAKE) BUILD=$(abspath $(BUILD))/profiles/$@ PROFILE=$@ all

$(BUILD):
	mkdir -p $@

$(BUILD)/startup.o: startup.S | $(BUILD)
	$(CC) --target=arm-none-eabi -mcpu=cortex-m3 -mthumb -nostdinc -c $< -o $@

$(BUILD)/%.o: %.c $(HEADERS) firmware.mk | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/cyw43_rx_fw.elf: $(OBJS) cyw43.ld
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -Wl,-Map=$(BUILD)/cyw43_rx_fw.map -o $@
	$(SIZE) $@

$(BUILD)/cyw43_rx_fw.bin: $(BUILD)/cyw43_rx_fw.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD)
