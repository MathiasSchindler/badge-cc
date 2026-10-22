#ifndef CLICKER_HID_H
#define CLICKER_HID_H
#include "controller.h"
#include "bluetooth.h"
void clicker_hid_run(badger_canvas_t *canvas, clicker_input_t *input,
                      uint8_t (*buttons_read)(void), clicker_bt_t *bt,
                      const uint8_t address[6], uint16_t acl_size, uint8_t acl_packets);
#endif
