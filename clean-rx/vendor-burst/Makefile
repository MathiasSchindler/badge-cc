# Run from the repository root: make -f clean-rx/vendor-burst/Makefile reference
include Makefile
.DEFAULT_GOAL := reference
REF_DIR := clean-rx/vendor-burst
REF_BUILD := build/clean-rx/vendor-burst
REF_CONTROLLER := $(REF_BUILD)/controller/cyw43_rx_fw.bin
REF_FLAGS := -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1
REF_STIMULUS ?= 0
REF_HANDOFF ?= 0
REF_GUARD ?= 0
REF_MONITOR ?= 0
REF_NEXT_FIELD ?= 0x83u
REF_ORDERED_FLUSH ?= 0
REF_PREFIX_RECOVERY ?= 0
REF_REARM_INJECT ?= 0
REF_VARIABLE ?= 0
REF_NEXT_STREAM ?= 0
REF_VARIABLE_STREAM64 ?= 0
REF_SHM_ONLY ?= 0
REF_SHORT_STREAM ?= 0
REF_DELIMITER_GUARD ?= 0
REF_FLAGS += -DREF_CLEAN_STIMULUS=$(REF_STIMULUS)
REF_CFLAGS := $(RP2350_CFLAGS) -DLIVE_USB_FIRMWARE -DLIVE_USB_CLEAN \
	-DWIFI_TRACE_SIZE=5888u -I$(REF_BUILD) -I$(WIFI_ASSET_DIR) $(REF_FLAGS)
REF_COMMON := $(REF_DIR)/main.c $(ARCH_DIR)/start.c $(ARCH_DIR)/bootrom_reboot.c \
	src/usb/live_usb_control.c src/usb/live_usb_hw.c $(SHARED_DIR)/usb_report.c \
	$(TOOLS_DIR)/diagnostics_logic.c $(PLATFORM_DIR)/wifi_board.c \
	$(PLATFORM_DIR)/display.c $(PLATFORM_DIR)/graphics.c \
	$(PLATFORM_DIR)/wifi_pio.c $(RADIO_DIR)/wifi_gspi.c $(RADIO_DIR)/wifi_probe.c \
	$(RADIO_DIR)/wifi_usb_report.c $(RADIO_DIR)/wifi_trace.c
REF_VENDOR := $(REF_COMMON) $(REF_DIR)/vendor.c $(WIFI_FW_SOURCES) $(RADIO_DIR)/wifi_assets.c
ifeq ($(REF_STIMULUS),1)
REF_VENDOR := $(REF_COMMON) $(REF_DIR)/stimulus.c clean-rx/badge/loader.c clean-rx/src/mac.c clean-rx/src/block_ack.c
endif
REF_OBSERVER := $(REF_COMMON) $(REF_DIR)/observer.c clean-rx/badge/loader.c clean-rx/src/mac.c clean-rx/src/block_ack.c
ifeq ($(REF_MONITOR),1)
REF_OBSERVER := $(REF_COMMON) $(REF_DIR)/monitor.c $(WIFI_FW_SOURCES) $(RADIO_DIR)/wifi_assets.c
endif
REF_VOBJS := $(addprefix $(REF_BUILD)/vendor/,$(notdir $(REF_VENDOR:.c=.o))) \
	$(REF_BUILD)/vendor/assets.o $(REF_BUILD)/picobin.o
REF_OOBJS := $(addprefix $(REF_BUILD)/observer/,$(notdir $(REF_OBSERVER:.c=.o))) \
	$(REF_BUILD)/observer/assets.o $(REF_BUILD)/picobin.o
DIAGNOSTIC_FLAGS := -DCLEAN_RX_DIAGNOSTIC=1
REF_HEADERS := $(wildcard $(REF_DIR)/*.h clean-rx/src/*.h clean-rx/src/diagnostic/*.h clean-rx/tests/fixtures/*.h clean-rx/badge/loader.h \
	$(SHARED_DIR)/*.h src/usb/*.h $(RADIO_DIR)/*.h $(PLATFORM_DIR)/*.h $(ARCH_DIR)/*.h)
VPATH := $(sort $(dir $(REF_VENDOR) $(REF_OBSERVER)))

.PHONY: reference reference-early reference-phy reference-window reference-rearm reference-recover reference-recover-test reference-reset reference-reset-test reference-stimulus reference-stimulus-fifo reference-stimulus-reset reference-stimulus-reset-test reference-park reference-stimulus-park-test reference-test reference-controller reference-force
.PHONY: reference-stimulus-fifo-test reference-stimulus-fifo-stop-test
.PHONY: reference-handoff reference-handoff-copy reference-handoff-wait reference-handoff-guard reference-handoff-end6 reference-handoff-quiet
.PHONY: reference-handoff-monitor
.PHONY: reference-handoff-defer
.PHONY: reference-handoff-dma reference-handoff-defer-dma
.PHONY: reference-handoff-stream-hold reference-handoff-stream-resume reference-handoff-stream-late
.PHONY: reference-handoff-stream-rearm-control reference-handoff-stream-rearm
.PHONY: reference-handoff-stall-control reference-handoff-stall-rearm
.PHONY: reference-handoff-destination-control reference-handoff-destination-shift
reference: $(REF_BUILD)/vendor.uf2 $(REF_BUILD)/observer.uf2 $(REF_BUILD)/collector
reference-early:
	$(MAKE) -f $(REF_DIR)/Makefile REF_FLAGS='-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1' reference reference-test
reference-phy:
	$(MAKE) -f $(REF_DIR)/Makefile REF_FLAGS='-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_PHY_SNAPSHOT=1' reference reference-test
reference-window:
	$(MAKE) -f $(REF_DIR)/Makefile REF_FLAGS='-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1' reference reference-test
reference-rearm:
	$(MAKE) -f $(REF_DIR)/Makefile REF_FLAGS='-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1 -DCLEAN_RX_HT_REARM_WAIT=1' reference reference-test
reference-recover:
	$(MAKE) -f $(REF_DIR)/Makefile REF_FLAGS='-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1 -DCLEAN_RX_HT_REARM_WAIT=1 -DCLEAN_RX_HT_REARM_RECOVER=1' reference reference-test
reference-recover-test:
	$(MAKE) -f $(REF_DIR)/Makefile REF_FLAGS='-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1 -DCLEAN_RX_HT_REARM_WAIT=1 -DCLEAN_RX_HT_REARM_RECOVER=1 -DCLEAN_RX_HT_REARM_INJECT=1' reference reference-test
reference-reset:
	$(MAKE) -f $(REF_DIR)/Makefile REF_FLAGS='-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1 -DCLEAN_RX_HT_REARM_WAIT=1 -DCLEAN_RX_HT_REARM_RECOVER=1 -DCLEAN_RX_HT_REARM_RESET=1' reference reference-test
reference-reset-test:
	$(MAKE) -f $(REF_DIR)/Makefile REF_FLAGS='-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1 -DCLEAN_RX_HT_REARM_WAIT=1 -DCLEAN_RX_HT_REARM_RECOVER=1 -DCLEAN_RX_HT_REARM_RESET=1 -DCLEAN_RX_HT_REARM_INJECT=1' reference reference-test
reference-stimulus:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1 -DCLEAN_RX_HT_REARM_WAIT=1' reference reference-test
reference-handoff:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1' reference reference-test
reference-handoff-copy:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_NEXT_FIELD=$(REF_NEXT_FIELD) -DCLEAN_RX_HT_VARIABLE=$(REF_VARIABLE) -DCLEAN_RX_HT_NEXT_STREAM=$(REF_NEXT_STREAM) -DCLEAN_RX_HT_VARIABLE_STREAM64=$(REF_VARIABLE_STREAM64) -DCLEAN_RX_HT_SHM_ONLY=$(REF_SHM_ONLY) -DCLEAN_RX_HT_SHORT_STREAM=$(REF_SHORT_STREAM) -DCLEAN_RX_HT_DELIMITER_GUARD=$(REF_DELIMITER_GUARD)' reference reference-test
.PHONY: reference-handoff-variable
reference-handoff-variable:
	$(MAKE) -f $(REF_DIR)/Makefile REF_NEXT_FIELD=0xc3u REF_VARIABLE=1 reference-handoff-copy
.PHONY: reference-handoff-shm-only
reference-handoff-shm-only:
	$(MAKE) -f $(REF_DIR)/Makefile REF_SHM_ONLY=1 reference-handoff-variable
.PHONY: reference-handoff-short-stream
reference-handoff-short-stream:
	$(MAKE) -f $(REF_DIR)/Makefile REF_SHORT_STREAM=1 reference-handoff-variable
.PHONY: reference-handoff-safe-variable
reference-handoff-safe-variable:
	$(MAKE) -f $(REF_DIR)/Makefile REF_DELIMITER_GUARD=1 reference-handoff-short-stream
.PHONY: reference-handoff-next-c3 reference-handoff-defer-next-c3
reference-handoff-next-c3:
	$(MAKE) -f $(REF_DIR)/Makefile REF_NEXT_FIELD=0xc3u reference-handoff-copy
reference-handoff-defer-next-c3:
	$(MAKE) -f $(REF_DIR)/Makefile REF_NEXT_FIELD=0xc3u reference-handoff-defer
reference-handoff-monitor:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_MONITOR=1 REF_FLAGS='-DREF_VENDOR_MONITOR=1 -DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1' reference reference-test
.PHONY: reference-stimulus-monitor reference-stimulus-monitor-poll reference-stimulus-monitor-poll-fast
reference-stimulus-monitor:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_MONITOR=1 REF_FLAGS='-DREF_VENDOR_MONITOR=1 -DREF_CLEAN_STIMULUS=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1' reference reference-test
reference-stimulus-monitor-poll:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_MONITOR=1 REF_FLAGS='-DREF_MONITOR_POLL=1 -DREF_VENDOR_MONITOR=1 -DREF_CLEAN_STIMULUS=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1' reference reference-test
reference-stimulus-monitor-poll-fast:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_MONITOR=1 REF_FLAGS='-DREF_MONITOR_POLL=2 -DREF_VENDOR_MONITOR=1 -DREF_CLEAN_STIMULUS=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1' reference reference-test
reference-handoff-defer:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DEFER_STREAM=1 -DCLEAN_RX_HT_NEXT_FIELD=$(REF_NEXT_FIELD)' reference reference-test
reference-handoff-stream-hold:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DEFER_STREAM=1 -DCLEAN_RX_HT_COPY_STREAM_PHASE=1' reference reference-test
reference-handoff-stream-resume:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DEFER_STREAM=1 -DCLEAN_RX_HT_COPY_STREAM_PHASE=2' reference reference-test
reference-handoff-stream-late:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DEFER_STREAM=1 -DCLEAN_RX_HT_COPY_STREAM_PHASE=3' reference reference-test
reference-handoff-stream-rearm-control:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DEFER_STREAM=1 -DCLEAN_RX_HT_COPY_STREAM_PHASE=2 -DCLEAN_RX_HT_COPY_STREAM_REARM=1' reference reference-test
reference-handoff-stream-rearm:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DEFER_STREAM=1 -DCLEAN_RX_HT_COPY_STREAM_PHASE=2 -DCLEAN_RX_HT_COPY_STREAM_REARM=2' reference reference-test
reference-handoff-stall-control:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DEFER_STREAM=1 -DCLEAN_RX_HT_COPY_STREAM_PHASE=2 -DCLEAN_RX_HT_COPY_STREAM_REARM=3' reference reference-test
reference-handoff-stall-rearm:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DEFER_STREAM=1 -DCLEAN_RX_HT_COPY_STREAM_PHASE=2 -DCLEAN_RX_HT_COPY_STREAM_REARM=4' reference reference-test
reference-handoff-destination-control:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DEFER_STREAM=1 -DCLEAN_RX_HT_COPY_STREAM_PHASE=2 -DCLEAN_RX_HT_COPY_STREAM_REARM=5' reference reference-test
reference-handoff-destination-shift:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DEFER_STREAM=1 -DCLEAN_RX_HT_COPY_STREAM_PHASE=2 -DCLEAN_RX_HT_COPY_STREAM_REARM=6' reference reference-test
.PHONY: reference-handoff-remaining-control reference-handoff-remaining
reference-handoff-remaining-control:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DEFER_STREAM=1 -DCLEAN_RX_HT_COPY_STREAM_PHASE=2 -DCLEAN_RX_HT_COPY_STREAM_REARM=7' reference reference-test
reference-handoff-remaining:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DEFER_STREAM=1 -DCLEAN_RX_HT_COPY_STREAM_PHASE=2 -DCLEAN_RX_HT_COPY_STREAM_REARM=8' reference reference-test
.PHONY: reference-handoff-scope-on reference-handoff-scope-off
reference-handoff-scope-on:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_SCOPE=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DEFER_STREAM=1 -DCLEAN_RX_HT_COPY_STREAM_PHASE=2 -DCLEAN_RX_HT_COPY_STREAM_REARM=6 -DCLEAN_RX_HT_COPY_SCOPE=1' reference reference-test
reference-handoff-scope-off:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_SCOPE=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DEFER_STREAM=1 -DCLEAN_RX_HT_COPY_STREAM_PHASE=2 -DCLEAN_RX_HT_COPY_STREAM_REARM=6 -DCLEAN_RX_HT_COPY_SCOPE=2' reference reference-test
.PHONY: reference-handoff-scope-hold reference-handoff-scope-resume
reference-handoff-scope-hold:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_SCOPE=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DEFER_STREAM=1 -DCLEAN_RX_HT_COPY_STREAM_PHASE=2 -DCLEAN_RX_HT_COPY_STREAM_REARM=6 -DCLEAN_RX_HT_COPY_SCOPE=3' reference reference-test
reference-handoff-scope-resume:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_SCOPE=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DEFER_STREAM=1 -DCLEAN_RX_HT_COPY_STREAM_PHASE=2 -DCLEAN_RX_HT_COPY_STREAM_REARM=6 -DCLEAN_RX_HT_COPY_SCOPE=4' reference reference-test
.PHONY: reference-handoff-scope-replay reference-handoff-scope-no-replay
.PHONY: reference-handoff-scope-no-bit6 reference-handoff-scope-no-bit7 reference-handoff-scope-no-bit13 reference-handoff-scope-no-bit7-shift
reference-handoff-scope-no-bit6:
	$(MAKE) -f $(REF_DIR)/Makefile REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DEFER_STREAM=1 -DCLEAN_RX_HT_COPY_STREAM_PHASE=2 -DCLEAN_RX_HT_COPY_STREAM_REARM=5 -DCLEAN_RX_HT_COPY_SCOPE=3 -DCLEAN_RX_HT_COPY_SCOPE_REPLAY_FIELD=0x82u' REF_STIMULUS=1 REF_HANDOFF=1 REF_SCOPE=1 reference reference-test
reference-handoff-scope-no-bit7:
	$(MAKE) -f $(REF_DIR)/Makefile REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DEFER_STREAM=1 -DCLEAN_RX_HT_COPY_STREAM_PHASE=2 -DCLEAN_RX_HT_COPY_STREAM_REARM=5 -DCLEAN_RX_HT_COPY_SCOPE=3 -DCLEAN_RX_HT_COPY_SCOPE_REPLAY_FIELD=0x81u' REF_STIMULUS=1 REF_HANDOFF=1 REF_SCOPE=1 reference reference-test
reference-handoff-scope-no-bit13:
	$(MAKE) -f $(REF_DIR)/Makefile REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DEFER_STREAM=1 -DCLEAN_RX_HT_COPY_STREAM_PHASE=2 -DCLEAN_RX_HT_COPY_STREAM_REARM=5 -DCLEAN_RX_HT_COPY_SCOPE=3 -DCLEAN_RX_HT_COPY_SCOPE_REPLAY_FIELD=0x03u' REF_STIMULUS=1 REF_HANDOFF=1 REF_SCOPE=1 reference reference-test
reference-handoff-scope-no-bit7-shift:
	$(MAKE) -f $(REF_DIR)/Makefile REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DEFER_STREAM=1 -DCLEAN_RX_HT_COPY_STREAM_PHASE=2 -DCLEAN_RX_HT_COPY_STREAM_REARM=6 -DCLEAN_RX_HT_COPY_SCOPE=3 -DCLEAN_RX_HT_COPY_SCOPE_REPLAY_FIELD=0x81u' REF_STIMULUS=1 REF_HANDOFF=1 REF_SCOPE=1 reference reference-test
reference-handoff-scope-replay:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_SCOPE=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DEFER_STREAM=1 -DCLEAN_RX_HT_COPY_STREAM_PHASE=2 -DCLEAN_RX_HT_COPY_STREAM_REARM=5 -DCLEAN_RX_HT_COPY_SCOPE=3' reference reference-test
reference-handoff-scope-no-replay:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_SCOPE=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DEFER_STREAM=1 -DCLEAN_RX_HT_COPY_STREAM_PHASE=2 -DCLEAN_RX_HT_COPY_STREAM_REARM=5 -DCLEAN_RX_HT_COPY_SCOPE=3 -DCLEAN_RX_HT_COPY_SCOPE_NO_REPLAY=1' reference reference-test
reference-handoff-dma:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DMA_PROBE=1' reference reference-test
reference-handoff-defer-dma:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_DEFER_STREAM=1 -DCLEAN_RX_HT_COPY_DMA_PROBE=1' reference reference-test
reference-handoff-guard:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_GUARD=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_HANDOFF_GUARD=1' reference reference-test
reference-handoff-end6:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_END_PLUS6=1' reference reference-test
reference-handoff-quiet:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_QUIET=1' reference reference-test
reference-handoff-wait:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_HANDOFF=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DREF_CLEAN_HANDOFF=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_ADVANCE=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_COPY_PROBE=1 -DCLEAN_RX_HT_COPY_WAIT=1' reference reference-test
reference-stimulus-fifo:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1 -DCLEAN_RX_HT_REARM_WAIT=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1' reference reference-test
.PHONY: reference-early-fifo reference-early-fifo-stop-test reference-early-fifo-reset-test
reference-early-fifo:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_EARLY=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1 -DCLEAN_RX_HT_REARM_WAIT=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_EARLY_FIFO=1' reference reference-test
.PHONY: reference-early-fifo-recovery reference-early-fifo-ordered reference-early-fifo-prefix reference-early-fifo-ordered-retry reference-early-fifo-prefix-retry
reference-early-fifo-recovery:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_EARLY=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1 -DCLEAN_RX_HT_REARM_WAIT=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_EARLY_FIFO=1 -DCLEAN_RX_HT_REARM_RECOVER=1 -DCLEAN_RX_HT_REARM_INJECT=$(REF_REARM_INJECT) -DCLEAN_RX_HT_ORDERED_FLUSH=$(REF_ORDERED_FLUSH) -DCLEAN_RX_HT_PREFIX_RECOVERY=$(REF_PREFIX_RECOVERY)' reference reference-test
reference-early-fifo-ordered:
	$(MAKE) -f $(REF_DIR)/Makefile REF_ORDERED_FLUSH=1 reference-early-fifo-recovery
reference-early-fifo-prefix:
	$(MAKE) -f $(REF_DIR)/Makefile REF_ORDERED_FLUSH=1 REF_PREFIX_RECOVERY=1 reference-early-fifo-recovery
reference-early-fifo-ordered-retry:
	$(MAKE) -f $(REF_DIR)/Makefile REF_ORDERED_FLUSH=2 reference-early-fifo-recovery
reference-early-fifo-prefix-retry:
	$(MAKE) -f $(REF_DIR)/Makefile REF_ORDERED_FLUSH=2 REF_PREFIX_RECOVERY=1 reference-early-fifo-recovery
.PHONY: reference-early-fifo-ordered-retry-test
reference-early-fifo-ordered-retry-test:
	$(MAKE) -f $(REF_DIR)/Makefile REF_ORDERED_FLUSH=2 REF_REARM_INJECT=1 reference-early-fifo-recovery
reference-early-fifo-stop-test:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_EARLY=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1 -DCLEAN_RX_HT_REARM_WAIT=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_EARLY_FIFO=1 -DREF_OBSERVER_PAUSE=1 -DREF_OBSERVER_STOP_FULL=1' reference reference-test
reference-early-fifo-reset-test:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_EARLY=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1 -DCLEAN_RX_HT_REARM_WAIT=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DCLEAN_RX_HT_EARLY_FIFO=1 -DCLEAN_RX_HT_REARM_RECOVER=1 -DCLEAN_RX_HT_REARM_RESET=1 -DCLEAN_RX_HT_REARM_INJECT=1' reference reference-test
reference-stimulus-fifo-test:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1 -DCLEAN_RX_HT_REARM_WAIT=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DREF_OBSERVER_PAUSE=1' reference reference-test
reference-stimulus-fifo-stop-test:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1 -DCLEAN_RX_HT_REARM_WAIT=1 -DCLEAN_RX_HT_OBSERVER_FIFO=1 -DREF_OBSERVER_PAUSE=1 -DREF_OBSERVER_STOP_FULL=1' reference reference-test
reference-stimulus-reset:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1 -DCLEAN_RX_HT_REARM_WAIT=1 -DCLEAN_RX_HT_REARM_RECOVER=1 -DCLEAN_RX_HT_REARM_RESET=1' reference reference-test
reference-stimulus-reset-test:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1 -DCLEAN_RX_HT_REARM_WAIT=1 -DCLEAN_RX_HT_REARM_RECOVER=1 -DCLEAN_RX_HT_REARM_RESET=1 -DCLEAN_RX_HT_REARM_INJECT=1' reference reference-test
reference-park:
	$(MAKE) -f $(REF_DIR)/Makefile REF_FLAGS='-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1 -DCLEAN_RX_HT_REARM_WAIT=1 -DCLEAN_RX_HT_REARM_RECOVER=1 -DCLEAN_RX_HT_REARM_RESET=1 -DCLEAN_RX_HT_REARM_PARK=1' reference reference-test
reference-stimulus-park-test:
	$(MAKE) -f $(REF_DIR)/Makefile REF_STIMULUS=1 REF_FLAGS='-DREF_CLEAN_STIMULUS=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1 -DCLEAN_RX_HT_REARM_WAIT=1 -DCLEAN_RX_HT_REARM_RECOVER=1 -DCLEAN_RX_HT_REARM_RESET=1 -DCLEAN_RX_HT_REARM_PARK=1 -DCLEAN_RX_HT_REARM_INJECT=1' reference reference-test
reference-controller:
	$(MAKE) -C clean-rx/src CC=clang PROFILE=diagnostic BUILD=../../$(REF_BUILD)/controller EXTRA_CFLAGS='$(REF_FLAGS)' all test
ifeq ($(REF_SCOPE),1)
	$(MAKE) -C clean-rx/src CC=clang PROFILE=diagnostic BUILD=../../$(REF_BUILD)/controller EXTRA_CFLAGS='$(REF_FLAGS)' copy-scope-test
endif
ifeq ($(REF_EARLY),1)
	$(MAKE) -C clean-rx/src CC=clang PROFILE=diagnostic BUILD=../../$(REF_BUILD)/controller EXTRA_CFLAGS='$(REF_FLAGS)' early-rx-test
endif
ifeq ($(REF_STIMULUS),1)
	$(MAKE) -C clean-rx/src CC=clang PROFILE=diagnostic BUILD=../../$(REF_BUILD)/stimulus-controller EXTRA_CFLAGS='-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_DATA=1 -DCLEAN_RX_HT_STIMULUS=$(if $(filter 1,$(REF_HANDOFF)),0,1) -DCLEAN_RX_HT_HANDOFF_STIMULUS=$(REF_HANDOFF) -DCLEAN_RX_HT_HANDOFF_GUARD=$(REF_GUARD) -DCLEAN_RX_HT_VARIABLE=$(REF_VARIABLE)' all test
endif

$(REF_BUILD)/build_id.h: reference-force reference-controller $(WIFI_ASSET_FILES)
	@mkdir -p $(REF_BUILD)
	@id=$$({ printf '%s\n' '$(REF_FLAGS)' '$(REF_STIMULUS)' '$(REF_HANDOFF)' '$(REF_GUARD)' '$(REF_MONITOR)'; \
		shasum -a 256 $(REF_VENDOR) $(REF_OBSERVER) $(REF_HEADERS) \
		$(REF_DIR)/collector.c $(REF_DIR)/Makefile $(REF_CONTROLLER) $(if $(filter 1,$(REF_STIMULUS)),$(REF_BUILD)/stimulus-controller/cyw43_rx_fw.bin) $(WIFI_ASSET_FILES) \
		; } | shasum -a 256 | cut -c1-8); \
	printf '#define USB_REPORT_BUILD_ID 0x%su\n' "$$id" > $@.tmp; \
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi

$(REF_BUILD)/vendor/%.o: %.c $(REF_BUILD)/build_id.h $(REF_HEADERS)
	@mkdir -p $(dir $@)
	$(RP2350_CC) $(REF_CFLAGS) $(DIAGNOSTIC_FLAGS) -c $< -o $@
$(REF_BUILD)/observer/%.o: %.c $(REF_BUILD)/build_id.h $(REF_HEADERS)
	@mkdir -p $(dir $@)
	$(RP2350_CC) $(REF_CFLAGS) $(DIAGNOSTIC_FLAGS) -c $< -o $@
$(REF_BUILD)/vendor/main.o $(REF_BUILD)/observer/main.o: $(REF_DIR)/main.c $(REF_BUILD)/build_id.h $(REF_HEADERS)
	@mkdir -p $(dir $@)
	$(RP2350_CC) $(REF_CFLAGS) $(DIAGNOSTIC_FLAGS) -c $< -o $@
$(REF_BUILD)/vendor/assets.o: $(RADIO_DIR)/wifi_assets.S $(REF_BUILD)/build_id.h
ifeq ($(REF_STIMULUS),1)
	$(RP2350_CC) $(REF_CFLAGS) $(DIAGNOSTIC_FLAGS) -DCLEAN_CONTROLLER_PATH='"$(abspath $(REF_BUILD)/stimulus-controller/cyw43_rx_fw.bin)"' -c clean-rx/badge/assets.S -o $@
else
	$(RP2350_CC) $(REF_CFLAGS) $(DIAGNOSTIC_FLAGS) -DWIFI_FIRMWARE_BIN='"$(abspath $(WIFI_ASSET_DIR)/wifi_firmware.bin)"' \
		-DWIFI_NVRAM_BIN='"$(abspath $(WIFI_ASSET_DIR)/wifi_nvram.bin)"' \
		-DWIFI_CLM_BIN='"$(abspath $(WIFI_ASSET_DIR)/wifi_clm.bin)"' -c $< -o $@
endif
$(REF_BUILD)/observer/assets.o: clean-rx/badge/assets.S $(REF_BUILD)/build_id.h
ifeq ($(REF_MONITOR),1)
	$(RP2350_CC) $(REF_CFLAGS) $(DIAGNOSTIC_FLAGS) -DWIFI_FIRMWARE_BIN='"$(abspath $(WIFI_ASSET_DIR)/wifi_firmware.bin)"' \
		-DWIFI_NVRAM_BIN='"$(abspath $(WIFI_ASSET_DIR)/wifi_nvram.bin)"' \
		-DWIFI_CLM_BIN='"$(abspath $(WIFI_ASSET_DIR)/wifi_clm.bin)"' -c $(RADIO_DIR)/wifi_assets.S -o $@
else
	$(RP2350_CC) $(REF_CFLAGS) $(DIAGNOSTIC_FLAGS) -DCLEAN_CONTROLLER_PATH='"$(abspath $(REF_CONTROLLER))"' -c $< -o $@
endif
$(REF_BUILD)/picobin.o: $(ARCH_DIR)/picobin.S
	@mkdir -p $(dir $@)
	$(RP2350_CC) $(RP2350_CFLAGS) -c $< -o $@
$(REF_BUILD)/vendor.elf: $(REF_VOBJS) $(ARCH_DIR)/memmap_report.ld
	$(RP2350_CC) $(REF_CFLAGS) $(DIAGNOSTIC_FLAGS) -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(ARCH_DIR)/memmap_report.ld $(REF_VOBJS) -Wl,-Map=$(REF_BUILD)/vendor.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@
$(REF_BUILD)/observer.elf: $(REF_OOBJS) $(ARCH_DIR)/memmap_report.ld
	$(RP2350_CC) $(REF_CFLAGS) $(DIAGNOSTIC_FLAGS) -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,--fatal-warnings \
		-T$(ARCH_DIR)/memmap_report.ld $(REF_OOBJS) -Wl,-Map=$(REF_BUILD)/observer.map -o $@
	@test -z "$$($(RP2350_NM) -u $@)"
	$(RP2350_SIZE) $@
$(REF_BUILD)/%.bin: $(REF_BUILD)/%.elf
	$(RP2350_OBJCOPY) -O binary $< $@
$(REF_BUILD)/%.uf2: $(REF_BUILD)/%.bin $(BIN2UF2)
	$(BIN2UF2) $< $@ $(FLASH_BASE) $(RP2350_FAMILY_ID)
$(REF_BUILD)/collector: $(REF_DIR)/collector.c $(REF_BUILD)/build_id.h $(REF_HEADERS)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O2 $(REF_FLAGS) $(DIAGNOSTIC_FLAGS) -DWIFI_TRACE_SIZE=5888u \
		-I$(REF_BUILD) $$(pkg-config --cflags libusb-1.0) $< $(SHARED_DIR)/usb_report.c \
		$(TOOLS_DIR)/diagnostics_logic.c $(RADIO_DIR)/wifi_trace.c clean-rx/src/mac.c clean-rx/src/block_ack.c \
		$(RADIO_DIR)/wifi_control.c \
		$$(pkg-config --libs libusb-1.0) -o $@
ifeq ($(REF_MONITOR)$(REF_HANDOFF),10)
reference-test: $(REF_BUILD)/monitor_test
endif
reference-test: $(REF_BUILD)/protocol_test
	$(REF_BUILD)/protocol_test
ifeq ($(REF_MONITOR)$(REF_HANDOFF),10)
	$(REF_BUILD)/monitor_test
endif
$(REF_BUILD)/monitor_test: $(REF_DIR)/monitor_test.c $(REF_DIR)/monitor.c $(REF_HEADERS) $(REF_BUILD)/build_id.h
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g $(REF_FLAGS) $(DIAGNOSTIC_FLAGS) -fsanitize=address,undefined \
		$< $(TOOLS_DIR)/diagnostics_logic.c $(RADIO_DIR)/wifi_control.c $(SHARED_DIR)/usb_report.c -o $@
$(REF_BUILD)/protocol_test: $(REF_DIR)/protocol_test.c $(REF_HEADERS) $(REF_BUILD)/build_id.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g $(REF_FLAGS) $(DIAGNOSTIC_FLAGS) $(if $(filter 1,$(REF_STIMULUS)),-DCLEAN_RX_HT_DATA=1 -DCLEAN_RX_HT_STIMULUS=1 -DCLEAN_RX_HT_HANDOFF_STIMULUS=1) -fsanitize=address,undefined \
		$< $(TOOLS_DIR)/diagnostics_logic.c $(PLATFORM_DIR)/graphics.c clean-rx/src/mac.c clean-rx/src/block_ack.c $(RADIO_DIR)/wifi_control.c -o $@
