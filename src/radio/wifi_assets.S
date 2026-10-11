#ifndef WIFI_ASSETS_GENERATED_HEADER
#define WIFI_ASSETS_GENERATED_HEADER "wifi_assets_generated.h"
#endif
#include WIFI_ASSETS_GENERATED_HEADER

#ifndef WIFI_FIRMWARE_BIN
#error "Define WIFI_FIRMWARE_BIN as a quoted path to wifi_firmware.bin"
#endif
#ifndef WIFI_NVRAM_BIN
#error "Define WIFI_NVRAM_BIN as a quoted path to wifi_nvram.bin"
#endif
#ifndef WIFI_CLM_BIN
#error "Define WIFI_CLM_BIN as a quoted path to wifi_clm.bin"
#endif

.section .rodata.wifi_firmware, "a", %progbits
.balign 4
.global wifi_firmware_start
.global wifi_firmware_end
.type wifi_firmware_start, %object
wifi_firmware_start:
.incbin WIFI_FIRMWARE_BIN
wifi_firmware_end:
.size wifi_firmware_start, wifi_firmware_end - wifi_firmware_start
.if (wifi_firmware_end - wifi_firmware_start) != WIFI_FIRMWARE_SIZE
.error "wifi_firmware.bin length does not match generated header"
.endif

.section .rodata.wifi_nvram, "a", %progbits
.balign 4
.global wifi_nvram_start
.global wifi_nvram_end
.type wifi_nvram_start, %object
wifi_nvram_start:
.incbin WIFI_NVRAM_BIN
wifi_nvram_end:
.size wifi_nvram_start, wifi_nvram_end - wifi_nvram_start
.if (wifi_nvram_end - wifi_nvram_start) != WIFI_NVRAM_SIZE
.error "wifi_nvram.bin length does not match generated header"
.endif

.section .rodata.wifi_clm, "a", %progbits
.balign 4
.global wifi_clm_start
.global wifi_clm_end
.type wifi_clm_start, %object
wifi_clm_start:
.incbin WIFI_CLM_BIN
wifi_clm_end:
.size wifi_clm_start, wifi_clm_end - wifi_clm_start
.if (wifi_clm_end - wifi_clm_start) != WIFI_CLM_SIZE
.error "wifi_clm.bin length does not match generated header"
.endif

.section .note.GNU-stack, "", %progbits
