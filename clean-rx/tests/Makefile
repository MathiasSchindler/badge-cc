HOST_CC ?= cc
BUILD ?= ../../build/clean-rx/tests
SRC := ../src
ROOT := ../..
include $(SRC)/firmware.mk
NATIVE_FLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g \
	-fsanitize=address,undefined -I$(SRC) -Isupport
NATIVE_FLAGS += $(PROFILE_FLAGS) $(EXTRA_CFLAGS)
UNIT_HEADERS := $(wildcard $(SRC)/*.h $(SRC)/diagnostic/*.h support/*.h)
INTEGRATION_HEADERS := $(UNIT_HEADERS) $(wildcard fixtures/*.h ../badge/*.h ../peer/*.h ../vendor-burst/*.h \
	$(ROOT)/src/shared/*.h $(ROOT)/src/tools/*.h $(ROOT)/src/radio/*.h)
DIAGNOSTICS := $(ROOT)/src/tools/diagnostics_logic.c
UNIT_TESTS := runtime_test runtime_types_first_test firmware_command_test psm_ucode_test block_ack_test rx_recovery_test

.DEFAULT_GOAL := test
.PHONY: test unit-test integration-test runtime-test copy-scope-test early-rx-test force
test: unit-test integration-test
unit-test: $(addprefix $(BUILD)/,$(UNIT_TESTS))
	$(BUILD)/runtime_test
	$(BUILD)/runtime_types_first_test
	$(BUILD)/firmware_command_test
	$(BUILD)/psm_ucode_test
	$(BUILD)/block_ack_test
	$(BUILD)/rx_recovery_test
integration-test: $(BUILD)/mac_test
	$(BUILD)/mac_test
runtime-test: $(BUILD)/runtime_test $(BUILD)/runtime_types_first_test
	$(BUILD)/runtime_test
	$(BUILD)/runtime_types_first_test
copy-scope-test: $(BUILD)/copy_scope_test
	$(BUILD)/copy_scope_test
early-rx-test: $(BUILD)/early_rx_test
	$(BUILD)/early_rx_test

$(BUILD):
	mkdir -p $@

$(BUILD)/test_flags: force | $(BUILD)
	@printf '%s\n' '$(HOST_CC)' '$(NATIVE_FLAGS)' '$(PROFILE)' > $@.tmp; \
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi

FLAGS_CHANGED := $(shell printf '%s\n' '$(HOST_CC)' '$(NATIVE_FLAGS)' '$(PROFILE)' | cmp -s - '$(BUILD)/test_flags' || printf changed)
ifneq ($(FLAGS_CHANGED),)
$(addprefix $(BUILD)/,$(UNIT_TESTS) mac_test copy_scope_test early_rx_test): force
endif

$(addprefix $(BUILD)/,$(UNIT_TESTS) mac_test copy_scope_test early_rx_test): $(BUILD)/test_flags Makefile profiles.mk $(SRC)/firmware.mk

$(BUILD)/runtime_test: unit/runtime_test.c $(UNIT_HEADERS) | $(BUILD)
	$(HOST_CC) $(NATIVE_FLAGS) $< -o $@
$(BUILD)/runtime_types_first_test: unit/runtime_test.c $(UNIT_HEADERS) | $(BUILD)
	$(HOST_CC) $(NATIVE_FLAGS) -DCLEAN_TYPES_FIRST $< -o $@
$(BUILD)/firmware_command_test: unit/firmware_command_test.c $(SRC)/mac.c $(UNIT_HEADERS) | $(BUILD)
	$(HOST_CC) $(NATIVE_FLAGS) $< $(SRC)/mac.c -o $@
$(BUILD)/psm_ucode_test: unit/psm_ucode_test.c $(SRC)/psm_ucode.c $(UNIT_HEADERS) | $(BUILD)
	$(HOST_CC) $(NATIVE_FLAGS) $< $(SRC)/psm_ucode.c -o $@
$(BUILD)/block_ack_test: unit/block_ack_test.c $(SRC)/block_ack.c $(SRC)/mac.c $(UNIT_HEADERS) | $(BUILD)
	$(HOST_CC) $(NATIVE_FLAGS) $< $(SRC)/block_ack.c $(SRC)/mac.c -o $@
$(BUILD)/rx_recovery_test: unit/rx_recovery_test.c $(SRC)/psm_ucode.c $(UNIT_HEADERS) | $(BUILD)
	$(HOST_CC) $(NATIVE_FLAGS) $< $(SRC)/psm_ucode.c -o $@

$(BUILD)/mac_test: integration/mac_test.c $(SRC)/mac.c $(DIAGNOSTICS) $(INTEGRATION_HEADERS) | $(BUILD)
	$(HOST_CC) $(NATIVE_FLAGS) $< $(SRC)/mac.c $(DIAGNOSTICS) -o $@
$(BUILD)/copy_scope_test: integration/copy_scope_test.c $(SRC)/psm_ucode.c $(DIAGNOSTICS) $(INTEGRATION_HEADERS) | $(BUILD)
	$(HOST_CC) $(NATIVE_FLAGS) $< $(SRC)/psm_ucode.c $(DIAGNOSTICS) -o $@
$(BUILD)/early_rx_test: integration/early_rx_test.c $(SRC)/mac.c $(SRC)/block_ack.c $(DIAGNOSTICS) $(INTEGRATION_HEADERS) | $(BUILD)
	$(HOST_CC) $(NATIVE_FLAGS) $< $(SRC)/mac.c $(SRC)/block_ack.c $(DIAGNOSTICS) -o $@

include profiles.mk
