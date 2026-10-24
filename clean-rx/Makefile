BUILD ?= ../build/clean-rx/check

.PHONY: check check-tools
check:
	node tools/check.cjs $(abspath $(BUILD))

check-tools:
	node --test tools/*.test.cjs
