QEMU_ARCH = arm
QEMU_MACHINE = mps2-an505
MICROPY_FLOAT_IMPL = float
SRC_BOARD_O = shared/runtime/gchelper_generic.o
LDSCRIPT = $(BOARD_DIR)/memory.ld
CFLAGS += -mthumb -mcpu=cortex-m33 -mfloat-abi=soft -fno-short-enums
CFLAGS += -DQEMU_SOC_MPS2 -DMICROPY_OPT_COMPUTED_GOTO=1
CFLAGS += -DMICROPY_HW_MCU_NAME='"Cortex-M33"'

.SECONDEXPANSION:
$(BUILD)/profile-vm.i: ../../py/vm.c $(BUILD)/py/vm.o
	$(Q)$(CC) $(CFLAGS) -E -P $< -o $@

$(BUILD)/profile-main.o: $(BOARD_DIR)/main.c $(BUILD)/workload.h $(BUILD)/py/vm.o
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/profile-%.elf: $(BUILD)/profile-main.o $(BUILD)/vm-%.o \
                       $(LDSCRIPT) $$(filter-out $$(BUILD)/py/vm.o,$$(OBJ))
	$(Q)$(LD) $(LDFLAGS) --wrap=main --wrap=uart_init --wrap=uart_tx_strn \
		--wrap=ticks_init -o $@ $(filter-out $(BUILD)/py/vm.o,$(OBJ)) \
		$(BUILD)/profile-main.o $(BUILD)/vm-$*.o $(LIBS)

$(BUILD)/vm-gcc.o: $(BUILD)/py/vm.o
	cp $< $@
