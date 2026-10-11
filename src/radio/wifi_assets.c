#include "wifi_assets.h"

#ifndef WIFI_ASSETS_GENERATED_HEADER
#define WIFI_ASSETS_GENERATED_HEADER "wifi_assets_generated.h"
#endif
#include WIFI_ASSETS_GENERATED_HEADER

extern const uint8_t wifi_firmware_start[];
extern const uint8_t wifi_firmware_end[];
extern const uint8_t wifi_nvram_start[];
extern const uint8_t wifi_nvram_end[];
extern const uint8_t wifi_clm_start[];
extern const uint8_t wifi_clm_end[];

_Static_assert(WIFI_FIRMWARE_SIZE % 4u == 0u, "firmware must be word padded");
_Static_assert(WIFI_NVRAM_SIZE % 4u == 0u, "NVRAM must be word padded");
_Static_assert(WIFI_CLM_SIZE % 4u == 0u, "CLM must be word aligned");

static const wifi_fw_assets_t assets = {
    .firmware = wifi_firmware_start,
    .firmware_size = WIFI_FIRMWARE_SIZE,
    .firmware_crc = WIFI_FIRMWARE_CRC32,
    .nvram = wifi_nvram_start,
    .nvram_size = WIFI_NVRAM_SIZE,
    .nvram_crc = WIFI_NVRAM_CRC32,
    .clm = wifi_clm_start,
    .clm_size = WIFI_CLM_SIZE,
    .clm_crc = WIFI_CLM_CRC32
};

const wifi_fw_assets_t *wifi_firmware_assets(void) {
    return &assets;
}
