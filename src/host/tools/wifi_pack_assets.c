#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Host-only vendor data exception; never compile this file into the target. */
#ifndef WIFI_FIRMWARE_DATA_HEADER
#define WIFI_FIRMWARE_DATA_HEADER "../../../vendor/pico-sdk/lib/cyw43-driver/firmware/w43439A0_7_95_49_00_combined.h"
#endif
#ifndef WIFI_NVRAM_DATA_HEADER
#define WIFI_NVRAM_DATA_HEADER "../../../vendor/pico-sdk/lib/cyw43-driver/firmware/wifi_nvram_43439.h"
#endif
#define CYW43_RESOURCE_ATTRIBUTE
#include WIFI_FIRMWARE_DATA_HEADER
#include WIFI_NVRAM_DATA_HEADER
#undef CYW43_RESOURCE_ATTRIBUTE

/* Source-file SHA256 pins for the build to enforce before compiling this tool. */
#define FIRMWARE_SOURCE_SHA256 "120bb63db1c5cf42cd44cf9a057d6f14601038a9413badf5a3b737f6552bc9ea"
#define NVRAM_SOURCE_SHA256 "ddc82b00667643f55d7687bba1b5e4028f24717ec8883f05bd7d435f4c9c01ad"
#define FIRMWARE_ORIGINAL_SIZE 224190u
#define FIRMWARE_SIZE 224192u
#define CLM_OFFSET 224256u
#define CLM_SIZE 984u
#define NVRAM_ORIGINAL_SIZE 743u
#define NVRAM_SIZE 744u

typedef struct {
    const char *name;
    const unsigned char *data;
    size_t original_size;
    size_t size;
    char *path;
    char *temporary;
    int staged;
} output_t;

static uint32_t crc32(const unsigned char *data, size_t length, size_t padded) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0u; i < padded; ++i) {
        crc ^= i < length ? data[i] : 0u;
        for (unsigned int bit = 0u; bit < 8u; ++bit) {
            crc = (crc >> 1u) ^ ((crc & 1u) ? 0xedb88320u : 0u);
        }
    }
    return ~crc;
}

static int validate_inputs(void) {
    const unsigned char *firmware = w43439A0_7_95_49_00_combined;
    static const unsigned char vector[] = {0, 0, 0, 0, 0x65, 0x14, 0, 0};
    static const char firmware_tail[] = "DVID 01-b0b4520f";
    static const char nvram_head[] = "NVRAMRev=$Rev$";
    static const char nvram_tail[] = "btc_mode=1\0\0\0";

    if (CYW43_WIFI_FW_LEN != FIRMWARE_ORIGINAL_SIZE ||
        CYW43_CLM_LEN != CLM_SIZE ||
        sizeof(w43439A0_7_95_49_00_combined) != CLM_OFFSET + CLM_SIZE ||
        sizeof(wifi_nvram_4343) != NVRAM_ORIGINAL_SIZE) {
        fprintf(stderr, "wifi_pack_assets: vendor header/array lengths do not match the pinned WiFi-only assets\n");
        return 0;
    }
    if (memcmp(firmware, vector, sizeof(vector)) != 0 ||
        memcmp(firmware + FIRMWARE_ORIGINAL_SIZE - (sizeof(firmware_tail) - 1u),
               firmware_tail, sizeof(firmware_tail) - 1u) != 0 ||
        memcmp(firmware + CLM_OFFSET, "BLOB\x3c\0\0", 8u) != 0 ||
        memcmp(firmware + CLM_OFFSET + 60u, "CLM DATA", 8u) != 0 ||
        memcmp(wifi_nvram_4343, nvram_head, sizeof(nvram_head)) != 0 ||
        memcmp(wifi_nvram_4343 + NVRAM_ORIGINAL_SIZE - sizeof(nvram_tail),
               nvram_tail, sizeof(nvram_tail)) != 0) {
        fprintf(stderr, "wifi_pack_assets: firmware, CLM or NVRAM sentinel mismatch\n");
        return 0;
    }
    for (size_t i = FIRMWARE_ORIGINAL_SIZE; i < CLM_OFFSET; ++i) {
        if (firmware[i] != 0u) {
            fprintf(stderr, "wifi_pack_assets: nonzero firmware-to-CLM padding at byte %zu\n", i);
            return 0;
        }
    }
    if (crc32(firmware, FIRMWARE_ORIGINAL_SIZE, FIRMWARE_ORIGINAL_SIZE) != 0xfe8b5e03u ||
        crc32(firmware + CLM_OFFSET, CLM_SIZE, CLM_SIZE) != 0x29aa78fdu ||
        crc32(wifi_nvram_4343, sizeof(wifi_nvram_4343), sizeof(wifi_nvram_4343)) != 0x357a3338u) {
        fprintf(stderr, "wifi_pack_assets: vendor payload CRC32 mismatch; review the source SHA256 pins\n");
        return 0;
    }
    return 1;
}

static void io_error(const char *operation, const char *path) {
    fprintf(stderr, "wifi_pack_assets: cannot %s %s: %s\n", operation, path,
            errno != 0 ? strerror(errno) : "I/O error");
}

static char *output_path(const char *directory, const char *name, const char *suffix) {
    size_t directory_size = strlen(directory);
    size_t extra = strlen(name) + strlen(suffix) + 2u;
    if (directory_size > SIZE_MAX - extra) {
        fprintf(stderr, "wifi_pack_assets: output path too long\n");
        return NULL;
    }
    char *path = malloc(directory_size + extra);
    if (path == NULL) {
        fprintf(stderr, "wifi_pack_assets: out of memory constructing output paths\n");
        return NULL;
    }
    snprintf(path, directory_size + extra, "%s/%s%s", directory, name, suffix);
    return path;
}

static int stage_output(output_t *output) {
    static const unsigned char padding[3] = {0, 0, 0};
    errno = 0;
    FILE *file = fopen(output->temporary, "wbx");
    if (file == NULL) {
        io_error("create temporary output (remove stale .tmp only when no packer is running)",
                 output->temporary);
        return 0;
    }
    output->staged = 1;
    errno = 0;
    int ok = fwrite(output->data, 1u, output->original_size, file) == output->original_size;
    size_t padding_size = output->size - output->original_size;
    if (ok && padding_size != 0u) {
        ok = fwrite(padding, 1u, padding_size, file) == padding_size;
    }
    if (!ok) io_error("write", output->temporary);
    errno = 0;
    if (fclose(file) != 0) {
        io_error("close", output->temporary);
        ok = 0;
    }
    return ok;
}

int main(int argc, char **argv) {
    char header[2048];
    int result = EXIT_FAILURE;
    if (argc != 2 || argv[1][0] == '\0') {
        fprintf(stderr, "usage: wifi_pack_assets EXISTING_OUTPUT_DIRECTORY\n");
        return EXIT_FAILURE;
    }
    if (!validate_inputs()) return EXIT_FAILURE;

    const unsigned char *firmware = w43439A0_7_95_49_00_combined;
    int header_size = snprintf(header, sizeof(header),
        "/* Generated by wifi_pack_assets; do not edit. See docs/wifi-assets.md.\n"
        " * CRC32 covers each complete output file, including zero padding. */\n"
        "#ifndef UNIVERSE26_WIFI_ASSETS_GENERATED_H\n"
        "#define UNIVERSE26_WIFI_ASSETS_GENERATED_H\n"
        "#define WIFI_FIRMWARE_SOURCE_SHA256 \"%s\"\n"
        "#define WIFI_NVRAM_SOURCE_SHA256 \"%s\"\n"
        "#define WIFI_COMBINED_SOURCE_SIZE %zu\n"
        "#define WIFI_CLM_SOURCE_OFFSET %u\n"
        "#define WIFI_FIRMWARE_ORIGINAL_SIZE %u\n"
        "#define WIFI_FIRMWARE_SIZE %u\n"
        "#define WIFI_FIRMWARE_CRC32 0x%08" PRIx32 "\n"
        "#define WIFI_NVRAM_ORIGINAL_SIZE %zu\n"
        "#define WIFI_NVRAM_SIZE %u\n"
        "#define WIFI_NVRAM_CRC32 0x%08" PRIx32 "\n"
        "#define WIFI_CLM_SIZE %u\n"
        "#define WIFI_CLM_CRC32 0x%08" PRIx32 "\n"
        "#endif\n",
        FIRMWARE_SOURCE_SHA256, NVRAM_SOURCE_SHA256,
        sizeof(w43439A0_7_95_49_00_combined), CLM_OFFSET,
        FIRMWARE_ORIGINAL_SIZE, FIRMWARE_SIZE,
        crc32(firmware, FIRMWARE_ORIGINAL_SIZE, FIRMWARE_SIZE),
        sizeof(wifi_nvram_4343), NVRAM_SIZE,
        crc32(wifi_nvram_4343, sizeof(wifi_nvram_4343), NVRAM_SIZE),
        CLM_SIZE, crc32(firmware + CLM_OFFSET, CLM_SIZE, CLM_SIZE));
    if (header_size < 0 || (size_t)header_size >= sizeof(header)) {
        fprintf(stderr, "wifi_pack_assets: generated header exceeds buffer or formatting failed\n");
        return EXIT_FAILURE;
    }

    /* Stage the header first, keeping its exclusive .tmp until the final rename. */
    output_t outputs[] = {
        {"wifi_assets_generated.h", (const unsigned char *)header,
         (size_t)header_size, (size_t)header_size, NULL, NULL, 0},
        {"wifi_firmware.bin", firmware, FIRMWARE_ORIGINAL_SIZE, FIRMWARE_SIZE, NULL, NULL, 0},
        {"wifi_nvram.bin", wifi_nvram_4343, sizeof(wifi_nvram_4343), NVRAM_SIZE, NULL, NULL, 0},
        {"wifi_clm.bin", firmware + CLM_OFFSET, CLM_SIZE, CLM_SIZE, NULL, NULL, 0}
    };
    const size_t count = sizeof(outputs) / sizeof(outputs[0]);
    for (size_t i = 0u; i < count; ++i) {
        outputs[i].path = output_path(argv[1], outputs[i].name, "");
        outputs[i].temporary = output_path(argv[1], outputs[i].name, ".tmp");
        if (outputs[i].path == NULL || outputs[i].temporary == NULL) goto cleanup;
    }
    for (size_t i = 0u; i < count; ++i) {
        if (!stage_output(&outputs[i])) goto cleanup;
    }

    /* An interrupted/failed publication must not leave an old valid manifest. */
    errno = 0;
    if (remove(outputs[0].path) != 0 && errno != ENOENT) {
        io_error("invalidate previous generated header", outputs[0].path);
        goto cleanup;
    }
    for (size_t i = 1u; i <= count; ++i) {
        output_t *output = &outputs[i % count];
        errno = 0;
        if (rename(output->temporary, output->path) != 0) {
            io_error("publish", output->path);
            fprintf(stderr, "wifi_pack_assets: incomplete output set; generated header absent, rerun before building\n");
            goto cleanup;
        }
        output->staged = 0;
    }
    result = EXIT_SUCCESS;

cleanup:
    for (size_t i = 0u; i < count; ++i) {
        if (outputs[i].staged && remove(outputs[i].temporary) != 0) {
            io_error("remove temporary output", outputs[i].temporary);
            result = EXIT_FAILURE;
        }
        free(outputs[i].path);
        free(outputs[i].temporary);
    }
    return result;
}
