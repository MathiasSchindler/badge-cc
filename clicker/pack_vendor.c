#include <stdint.h>
#include <stdio.h>

#define CYW43_RESOURCE_ATTRIBUTE
#include "wb43439A0_7_95_49_00_combined.h"
#include "wifi_nvram_43439.h"
#include "cyw43_btfw_43439.h"

static int save(const char *name, const unsigned char *data, uint32_t size,
                 uint32_t padding) {
    FILE *file = fopen(name, "wb");
    if (file == NULL) {
        perror(name);
        return 0;
    }
    int ok = fwrite(data, 1u, size, file) == size;
    while (padding-- != 0u) {
        if (fputc(0, file) == EOF) ok = 0;
    }
    if (fclose(file) != 0) ok = 0;
    if (!ok) fprintf(stderr, "failed writing vendor asset: %s\n", name);
    return ok;
}

int main(void) {
    uint32_t clm_offset = (CYW43_WIFI_FW_LEN + 511u) & ~511u;
    if (CYW43_WIFI_FW_LEN < 16u || CYW43_WIFI_FW_LEN >= 0x40000u ||
        CYW43_CLM_LEN == 0u ||
        sizeof(wb43439A0_7_95_49_00_combined) != clm_offset + CYW43_CLM_LEN ||
        sizeof(wifi_nvram_4343) > 4096u ||
        cyw43_btfw_43439_len != sizeof(cyw43_btfw_43439)) {
        fputs("vendor asset lengths do not match the reviewed container format\n", stderr);
        return 1;
    }
    for (uint32_t i = CYW43_WIFI_FW_LEN; i < clm_offset; ++i) {
        if (wb43439A0_7_95_49_00_combined[i] != 0u) {
            fputs("nonzero firmware-to-CLM padding\n", stderr);
            return 1;
        }
    }
    if (!save("wifi-bt.bin", wb43439A0_7_95_49_00_combined, CYW43_WIFI_FW_LEN,
               (0u - CYW43_WIFI_FW_LEN) & 3u) ||
        !save("nvram.bin", wifi_nvram_4343, sizeof(wifi_nvram_4343),
               (0u - (uint32_t)sizeof(wifi_nvram_4343)) & 3u) ||
        !save("clm.bin", wb43439A0_7_95_49_00_combined + clm_offset, CYW43_CLM_LEN, 0u) ||
        !save("bluetooth.bin", cyw43_btfw_43439, sizeof(cyw43_btfw_43439), 0u)) return 1;
    return 0;
}
