CC := cc
BUILD := ../build/logi-keypad
CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror
FRAMEWORKS := -framework IOKit -framework CoreFoundation -framework CoreGraphics -framework ImageIO
PROBE := $(BUILD)/keypad-probe

.PHONY: all inspect test firmware test-firmware host-firmware test-host-firmware
all: $(PROBE)

$(BUILD):
	mkdir -p $@

$(PROBE): keypad-probe.c keypad-lcd.h keypad-input.h | $(BUILD)
	$(CC) $(CFLAGS) $< $(FRAMEWORKS) -o $@

inspect: $(PROBE)
	$(PROBE) --inspect

$(BUILD)/keypad-probe-test: keypad-probe-test.c keypad-probe.c keypad-lcd.h keypad-input.h | $(BUILD)
	$(CC) $(CFLAGS) $< $(FRAMEWORKS) -o $@

test: $(PROBE) $(BUILD)/keypad-probe-test
	$(BUILD)/keypad-probe-test
	$(PROBE) --help > /dev/null
	@for args in '--watch' '--watch 0' '--watch -1' '--watch 3601' \
		'--watch 99999999999999999999' '--watch 1x' '--watch 1.5' \
		'--serial' '--unknown' '--inspect --watch 1' '--navigation' \
		'--inspect --navigation' '--watch 1 --navigation --navigation' \
		'--other-speed --navigation' '--other-speed --inspect' \
		'--other-speed --watch 1' \
		'--lcd-test' '--lcd-test 0' '--lcd-test 3601' '--lcd-test 1x' \
		'--lcd-test 1 --watch 1' '--lcd-test 1 --inspect'; do \
		$(PROBE) $$args > /dev/null 2>&1; result=$$?; \
		if [ $$result -ne 2 ]; then \
			echo "Expected usage error (2) for: $$args; got $$result"; exit 1; \
		fi; \
	done
	@echo "Unit and CLI tests passed (no hardware required)."

firmware:
	$(MAKE) -C firmware all

test-firmware:
	$(MAKE) -C firmware test

host-firmware:
	$(MAKE) -C host-firmware all

test-host-firmware:
	$(MAKE) -C host-firmware test
