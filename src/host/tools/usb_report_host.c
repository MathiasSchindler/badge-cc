#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../shared/usb_report.h"
#include "usb_report_build.h"

static int read_record(const char *path, uint8_t record[USB_REPORT_WIRE_SIZE]) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        perror(path);
        return 0;
    }
    size_t length = fread(record, 1u, USB_REPORT_WIRE_SIZE, file);
    int extra = fgetc(file);
    int valid = length == USB_REPORT_WIRE_SIZE && extra == EOF && !ferror(file);
    if (fclose(file) != 0) valid = 0;
    if (!valid) fprintf(stderr, "%s: expected exactly %u bytes\n", path, USB_REPORT_WIRE_SIZE);
    return valid;
}

static int write_record(const char *path, const uint8_t record[USB_REPORT_WIRE_SIZE]) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        perror(path);
        return 0;
    }
    int valid = fwrite(record, 1u, USB_REPORT_WIRE_SIZE, file) == USB_REPORT_WIRE_SIZE;
    if (fclose(file) != 0) valid = 0;
    if (!valid) fprintf(stderr, "%s: cannot write complete record\n", path);
    return valid;
}

static int prepare(const char *request_path, const char *clear_path) {
    uint8_t nonce[USB_REPORT_NONCE_SIZE];
    uint8_t request[USB_REPORT_WIRE_SIZE];
    uint8_t clear[USB_REPORT_WIRE_SIZE] = {0};
    FILE *random = fopen("/dev/urandom", "rb");
    if (random == NULL) {
        perror("/dev/urandom");
        return 1;
    }
    int valid = fread(nonce, 1u, sizeof(nonce), random) == sizeof(nonce);
    if (fclose(random) != 0) valid = 0;
    if (!valid) {
        fputs("Cannot generate run nonce\n", stderr);
        return 1;
    }
    usb_report_request(request, USB_REPORT_BUILD_ID, nonce);
    if (usb_report_check_request(request, sizeof(request), USB_REPORT_BUILD_ID) != USB_RECORD_OK) {
        fputs("Generated invalid request\n", stderr);
        return 1;
    }
    return write_record(request_path, request) && write_record(clear_path, clear) ? 0 : 1;
}

static int verify(const char *request_path, const char *response_path) {
    uint8_t request[USB_REPORT_WIRE_SIZE], response[USB_REPORT_WIRE_SIZE];
    if (!read_record(request_path, request) || !read_record(response_path, response)) return 1;
    usb_record_result_t result = usb_report_check_response(response, sizeof(response),
                                                           request, sizeof(request), USB_REPORT_BUILD_ID);
    if (result != USB_RECORD_OK) {
        fprintf(stderr, "USB report rejected: %s; status=0x%08x detail=0x%08x\n",
                usb_report_result_name(result), (unsigned)usb_report_u32(response + 32u),
                (unsigned)usb_report_u32(response + 36u));
        return 1;
    }
    printf("{\"verified\":true,\"test\":\"known-ram-report\",\"version\":1,"
           "\"build_id\":\"%08x\",\"nonce\":\"", (unsigned)USB_REPORT_BUILD_ID);
    for (uint32_t i = 16u; i < 32u; ++i) printf("%02x", (unsigned)response[i]);
    printf("\",\"address\":\"%08x\",\"length\":%u,\"value\":\"%08x\","
           "\"crc32\":\"%08x\"}\n", USB_REPORT_RESPONSE_ADDRESS, USB_REPORT_WIRE_SIZE,
           (unsigned)usb_report_u32(response + 36u), (unsigned)usb_report_u32(response + 60u));
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 4 && strcmp(argv[1], "prepare") == 0) return prepare(argv[2], argv[3]);
    if (argc == 4 && strcmp(argv[1], "verify") == 0) return verify(argv[2], argv[3]);
    fputs("Usage: usb_report_host prepare REQUEST.bin CLEAR.bin\n"
          "       usb_report_host verify REQUEST.bin RESPONSE.bin\n", stderr);
    return 2;
}
