#ifndef PICO2W_REPORT_H
#define PICO2W_REPORT_H

#include "types.h"

#define USB_REPORT_SIZE 64u
#define USB_NONCE_SIZE 16u
#define USB_REPORT_TEST_ID 1u
#define USB_REQUEST_MAGIC 0x51573250u
#define USB_RESPONSE_MAGIC 0x52573250u
#define USB_REPORT_COMMIT 0xc001cafeu

typedef enum {
    USB_RECORD_OK,
    USB_RECORD_LENGTH,
    USB_RECORD_MAGIC,
    USB_RECORD_VERSION,
    USB_RECORD_CRC,
    USB_RECORD_BUILD,
    USB_RECORD_TEST,
    USB_RECORD_NONCE,
    USB_RECORD_RESERVED
} usb_record_result_t;

uint32_t usb_load_u32(const uint8_t *data);
void usb_store_u32(uint8_t *data, uint32_t value);
uint32_t usb_crc32(const uint8_t *data, uint32_t length);
void usb_report_request(uint8_t wire[USB_REPORT_SIZE], uint32_t build_id,
                        const uint8_t nonce[USB_NONCE_SIZE]);
usb_record_result_t usb_report_check_request(const uint8_t *wire, uint32_t length,
                                             uint32_t build_id);
void usb_report_response(uint8_t wire[USB_REPORT_SIZE], uint32_t build_id,
                         const uint8_t nonce[USB_NONCE_SIZE],
                         const uint32_t chip_id[2], uint32_t sequence);

#endif
