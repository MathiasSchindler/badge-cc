CC      ?= clang
OBJCOPY ?= llvm-objcopy
SIZE    ?= llvm-size
BUILD   ?= build

CFLAGS  := --target=arm-none-eabi -mcpu=cortex-m3 -mthumb -std=c11 \
           -Wall -Wextra -Werror -Oz \
           -ffreestanding -nostdinc -fno-builtin \
           -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables \
           -ffunction-sections -fdata-sections -I.

LDFLAGS := -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings -T cyw43.ld

SRCS    := main.c pmu.c radio_phy.c psm_ucode.c
OBJS    := $(BUILD)/startup.o $(addprefix $(BUILD)/,$(SRCS:.c=.o))

.PHONY: all clean

all: $(BUILD)/cyw43_rx_fw.bin

$(BUILD):
	mkdir -p $@

$(BUILD)/startup.o: startup.S | $(BUILD)
	$(CC) --target=arm-none-eabi -mcpu=cortex-m3 -mthumb -nostdinc -c $< -o $@

$(BUILD)/%.o: %.c cyw43_hw.h types.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/cyw43_rx_fw.elf: $(OBJS) cyw43.ld
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -Wl,-Map=$(BUILD)/cyw43_rx_fw.map -o $@
	$(SIZE) $@

$(BUILD)/cyw43_rx_fw.bin: $(BUILD)/cyw43_rx_fw.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD)
