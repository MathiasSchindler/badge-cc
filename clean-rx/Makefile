BUILD ?= ../build/clean-rx/check

.PHONY: check check-tools check-docs
check:
	node tools/check.cjs $(abspath $(BUILD))

check-tools:
	node --test tools/*.test.cjs

check-docs:
	node tools/documentation.cjs
