OBSERVER_FLAGS = $(CLEAN_PROFILE_reference-early)
COPY_SCOPE_FLAGS = $(CLEAN_PROFILE_copy-scope)
EARLY_RX_FLAGS = $(CLEAN_PROFILE_early-rx)
RECOVERY_FLAGS = $(CLEAN_PROFILE_early-prefix)

.PHONY: test-copy-scope test-early-rx test-recovery
test-copy-scope:
	$(MAKE) BUILD=$(abspath $(BUILD))/profiles/copy-scope PROFILE=diagnostic SELECTION=copy-scope EXTRA_CFLAGS= test copy-scope-test
test-early-rx:
	$(MAKE) BUILD=$(abspath $(BUILD))/profiles/early-rx PROFILE=diagnostic SELECTION=early-rx EXTRA_CFLAGS= test early-rx-test
test-recovery:
	$(MAKE) BUILD=$(abspath $(BUILD))/profiles/recovery PROFILE=diagnostic SELECTION=early-prefix EXTRA_CFLAGS= test early-rx-test
