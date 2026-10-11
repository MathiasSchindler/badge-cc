#ifndef UNIVERSE26_USB_REPORT_H
#define UNIVERSE26_USB_REPORT_H

#include <stdint.h>

#define USB_REPORT_REQUEST_ADDRESS 0x2007f000u
#define USB_REPORT_RESPONSE_ADDRESS 0x2007f100u
#define USB_REPORT_WIRE_SIZE 64u
#define USB_REPORT_NONCE_SIZE 16u
#define USB_REPORT_TEST_ID 1u
#define USB_REPORT_OK 1u
#define USB_REPORT_REQUEST_INVALID 2u
#define USB_REPORT_ROM_ERROR 3u

typedef enum {
    USB_RECORD_OK,
    USB_RECORD_LENGTH,
    USB_RECORD_MAGIC,
    USB_RECORD_VERSION,
    USB_RECORD_CRC,
    USB_RECORD_BUILD,
    USB_RECORD_TEST,
    USB_RECORD_NONCE,
    USB_RECORD_RESERVED,
    USB_RECORD_STATUS,
    USB_RECORD_PAYLOAD
} usb_record_result_t;

/* Header only: the encoder must finish its payload and CRC before publication. */
void usb_report_header(uint8_t wire[USB_REPORT_WIRE_SIZE], uint32_t magic, uint32_t build_id,
                        const uint8_t nonce[USB_REPORT_NONCE_SIZE], uint32_t test_id);
void usb_report_request(uint8_t wire[USB_REPORT_WIRE_SIZE], uint32_t build_id,
                         const uint8_t nonce[USB_REPORT_NONCE_SIZE]);
void usb_report_request_for_test(uint8_t wire[USB_REPORT_WIRE_SIZE], uint32_t build_id,
                                  const uint8_t nonce[USB_REPORT_NONCE_SIZE], uint32_t test_id);
usb_record_result_t usb_report_check_request(const uint8_t *wire, uint32_t length,
                                              uint32_t build_id);
usb_record_result_t usb_report_check_request_for_test(const uint8_t *wire, uint32_t length,
                                                       uint32_t build_id, uint32_t test_id);
void usb_report_response(uint8_t wire[USB_REPORT_WIRE_SIZE], uint32_t build_id,
                          const uint8_t nonce[USB_REPORT_NONCE_SIZE],
                          uint32_t status, uint32_t detail);
usb_record_result_t usb_report_check_response(const uint8_t *wire, uint32_t length,
                                               const uint8_t *request, uint32_t request_length,
                                               uint32_t build_id);
uint32_t usb_report_u32(const uint8_t *wire);
const char *usb_report_result_name(usb_record_result_t result);

#endif
