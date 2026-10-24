ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
USB_TOOLS_BUILD := $(ROOT)/build/clean-rx/tools

.PHONY: all
all: $(USB_TOOLS_BUILD)/usb-ready

$(USB_TOOLS_BUILD)/usb-ready: $(ROOT)/clean-rx/tools/usb_ready.c $(ROOT)/src/host/tools/usb_live_transport.h $(ROOT)/src/usb/live_usb.h
	mkdir -p $(USB_TOOLS_BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O2 $$(pkg-config --cflags libusb-1.0) $< \
		$$(pkg-config --libs libusb-1.0) -o $@
