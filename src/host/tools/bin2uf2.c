#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UF2_MAGIC_START0 0x0a324655u
#define UF2_MAGIC_START1 0x9e5d5157u
#define UF2_MAGIC_END 0x0ab16f30u
#define UF2_FLAG_FAMILY_ID_PRESENT 0x00002000u
#define UF2_BLOCK_SIZE 512u
#define UF2_PAYLOAD_SIZE 256u
#define MAX_INPUT_SIZE (16u * 1024u * 1024u)

static void put_u32_le(unsigned char *output, uint32_t value) {
    output[0] = (unsigned char)value;
    output[1] = (unsigned char)(value >> 8);
    output[2] = (unsigned char)(value >> 16);
    output[3] = (unsigned char)(value >> 24);
}

static int parse_u32(const char *text, uint32_t *value) {
    char *end = NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed > UINT32_MAX) {
        return 0;
    }

    *value = (uint32_t)parsed;
    return 1;
}

static unsigned char *read_input(const char *path, uint32_t *size) {
    FILE *input = fopen(path, "rb");
    long length;
    unsigned char *data;

    if (input == NULL) {
        fprintf(stderr, "bin2uf2: cannot open %s: %s\n",
                path, strerror(errno));
        return NULL;
    }
    if (fseek(input, 0, SEEK_END) != 0 ||
        (length = ftell(input)) <= 0 ||
        (unsigned long)length > MAX_INPUT_SIZE ||
        fseek(input, 0, SEEK_SET) != 0) {
        fprintf(stderr, "bin2uf2: unsupported input size for %s\n", path);
        fclose(input);
        return NULL;
    }

    data = malloc((size_t)length);
    if (data == NULL) {
        fprintf(stderr, "bin2uf2: out of memory reading %s\n", path);
        fclose(input);
        return NULL;
    }
    if (fread(data, 1, (size_t)length, input) != (size_t)length) {
        fprintf(stderr, "bin2uf2: failed to read %s\n", path);
        free(data);
        fclose(input);
        return NULL;
    }
    if (fclose(input) != 0) {
        fprintf(stderr, "bin2uf2: failed to close %s: %s\n",
                path, strerror(errno));
        free(data);
        return NULL;
    }

    *size = (uint32_t)length;
    return data;
}

static int write_block(FILE *output, const unsigned char *payload,
                       uint32_t address, uint32_t block_number,
                       uint32_t block_count, uint32_t family_id) {
    unsigned char block[UF2_BLOCK_SIZE];

    memset(block, 0, sizeof(block));
    put_u32_le(block + 0, UF2_MAGIC_START0);
    put_u32_le(block + 4, UF2_MAGIC_START1);
    put_u32_le(block + 8, UF2_FLAG_FAMILY_ID_PRESENT);
    put_u32_le(block + 12, address);
    put_u32_le(block + 16, UF2_PAYLOAD_SIZE);
    put_u32_le(block + 20, block_number);
    put_u32_le(block + 24, block_count);
    put_u32_le(block + 28, family_id);
    memcpy(block + 32, payload, UF2_PAYLOAD_SIZE);
    put_u32_le(block + UF2_BLOCK_SIZE - 4, UF2_MAGIC_END);

    return fwrite(block, 1, sizeof(block), output) == sizeof(block);
}

int main(int argc, char **argv) {
    unsigned char *input_data;
    uint32_t input_size;
    uint32_t flash_base;
    uint32_t family_id;
    uint32_t block_count;
    FILE *output;

    if (argc != 5) {
        fprintf(stderr,
                "usage: %s input.bin output.uf2 flash-base family-id\n",
                argv[0]);
        return 2;
    }
    if (!parse_u32(argv[3], &flash_base) ||
        (flash_base & (UF2_PAYLOAD_SIZE - 1u)) != 0u) {
        fprintf(stderr, "bin2uf2: invalid or unaligned flash base: %s\n",
                argv[3]);
        return 2;
    }
    if (!parse_u32(argv[4], &family_id)) {
        fprintf(stderr, "bin2uf2: invalid family ID: %s\n", argv[4]);
        return 2;
    }

    input_data = read_input(argv[1], &input_size);
    if (input_data == NULL) {
        return 1;
    }

    output = fopen(argv[2], "wb");
    if (output == NULL) {
        fprintf(stderr, "bin2uf2: cannot open %s: %s\n",
                argv[2], strerror(errno));
        free(input_data);
        return 1;
    }

    block_count =
        (input_size + UF2_PAYLOAD_SIZE - 1u) / UF2_PAYLOAD_SIZE;
    for (uint32_t block_number = 0; block_number < block_count;
         ++block_number) {
        unsigned char payload[UF2_PAYLOAD_SIZE];
        uint32_t offset = block_number * UF2_PAYLOAD_SIZE;
        uint32_t remaining = input_size - offset;
        uint32_t copy_size =
            remaining < UF2_PAYLOAD_SIZE ? remaining : UF2_PAYLOAD_SIZE;

        memset(payload, 0xff, sizeof(payload));
        memcpy(payload, input_data + offset, copy_size);
        if (!write_block(output, payload, flash_base + offset,
                         block_number, block_count, family_id)) {
            fprintf(stderr, "bin2uf2: failed to write %s\n", argv[2]);
            fclose(output);
            remove(argv[2]);
            free(input_data);
            return 1;
        }
    }

    if (fclose(output) != 0) {
        fprintf(stderr, "bin2uf2: failed to close %s: %s\n",
                argv[2], strerror(errno));
        remove(argv[2]);
        free(input_data);
        return 1;
    }

    printf("%s: %u bytes, %u UF2 blocks, family 0x%08x\n",
           argv[2], input_size, block_count, family_id);
    free(input_data);
    return 0;
}
