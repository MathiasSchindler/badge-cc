ROOT := ../..
BUILD := $(ROOT)/build/cyw43
ARM_CC ?= clang
NM ?= llvm-nm
FLAGS := --target=arm-none-eabi -mthumb -std=c11 -Wall -Wextra -Werror -Oz \
	-ffreestanding -fno-builtin -fno-stack-protector -fno-unwind-tables \
	-fno-asynchronous-unwind-tables -nostdinc -I$(ROOT)/src/shared -I$(ROOT)/src/include
SOURCES := pmu-startup/resources.c pmu-startup/source/pll.c pmu-startup/source/access.c \
	pmu-startup/source/resource.c phy-access/access.c rx-clock/wait.c \
	tx-inhibit/guard.c phy-tables/table.c phy-tables/board.c phy-tables/agc.c \
	phy-tables/cold_start.c phy-tables/radio_startup.c phy-tables/rx_config.c \
	phy-tables/rx_startup.c rx-validation/validate.c rx-preparation/prepare.c \
	rx-dma/dma.c rx-dma/frame.c rx-dma/recovery.c \
	psm-rx/source/source_psm.c psm-rx/source/source_psm_images.c
HEADERS := $(wildcard $(addsuffix *.h,$(sort $(dir $(SOURCES)))))
M3_OBJECTS := $(addprefix $(BUILD)/m3/,$(SOURCES:.c=.o)) \
	$(BUILD)/m3/rx-dma/cyw.o $(BUILD)/m3/hash.o
M33_OBJECTS := $(addprefix $(BUILD)/m33/,$(SOURCES:.c=.o)) $(BUILD)/m33/hash.o

.PHONY: all verify
all: $(BUILD)/components-m3.o $(BUILD)/components-m33.o

$(BUILD)/m3/%.o: %.c $(HEADERS) $(ROOT)/src/include/r0_hash.h
	@mkdir -p $(dir $@)
	$(ARM_CC) $(FLAGS) -mcpu=cortex-m3 -c $< -o $@

$(BUILD)/m33/%.o: %.c $(HEADERS) $(ROOT)/src/include/r0_hash.h
	@mkdir -p $(dir $@)
	$(ARM_CC) $(FLAGS) -mcpu=cortex-m33 -c $< -o $@

$(BUILD)/m3/hash.o: $(ROOT)/src/shared/hash.c $(ROOT)/src/include/r0_hash.h
	@mkdir -p $(dir $@)
	$(ARM_CC) $(FLAGS) -mcpu=cortex-m3 -c $< -o $@

$(BUILD)/m33/hash.o: $(ROOT)/src/shared/hash.c $(ROOT)/src/include/r0_hash.h
	@mkdir -p $(dir $@)
	$(ARM_CC) $(FLAGS) -mcpu=cortex-m33 -c $< -o $@

$(BUILD)/components-m3.o: $(M3_OBJECTS)
	$(ARM_CC) $(FLAGS) -mcpu=cortex-m3 -nostdlib -fuse-ld=lld -Wl,-r $^ -o $@
	@test -z "$$($(NM) -u $@)"

$(BUILD)/components-m33.o: $(M33_OBJECTS)
	$(ARM_CC) $(FLAGS) -mcpu=cortex-m33 -nostdlib -fuse-ld=lld -Wl,-r $^ -o $@
	@test -z "$$($(NM) -u $@)"

verify: all
	@test -z "$$($(NM) -u $(BUILD)/components-m3.o)"
	@test -z "$$($(NM) -u $(BUILD)/components-m33.o)"
