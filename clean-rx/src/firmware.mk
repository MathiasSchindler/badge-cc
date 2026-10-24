PROFILE ?= runtime
ifeq ($(PROFILE),runtime)
PROFILE_ID := 0
PROFILE_FLAGS := -DCLEAN_RX_DIAGNOSTIC=0
else ifeq ($(PROFILE),diagnostic)
PROFILE_ID := 1
PROFILE_FLAGS := -DCLEAN_RX_DIAGNOSTIC=1
else ifeq ($(PROFILE),sdio-test)
PROFILE_ID := 2
PROFILE_FLAGS := -DCLEAN_RX_DIAGNOSTIC=1 -DCLEAN_SDIO_SELF_TEST=1
else
$(error Unknown PROFILE '$(PROFILE)'; use runtime, diagnostic or sdio-test)
endif
