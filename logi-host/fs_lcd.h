#ifndef LOGI_HOST_FS_LCD_H
#define LOGI_HOST_FS_LCD_H

#include "fs.h"
#include "../logi-keypad/keypad-lcd-wire.h"

typedef struct { const uint8_t *bytes; uint32_t length; } fs_lcd_image_t;
extern const fs_lcd_image_t fs_lcd_images[9];

enum { FS_LCD_DISCOVER = 1, FS_LCD_KEEPALIVE, FS_LCD_UPLOAD, FS_LCD_HOLD, FS_LCD_DONE, FS_LCD_FAILED };
typedef struct {
    uint8_t report[LCD_PACKET_SIZE], command[20];
    fs_packet_t reply;
    uint32_t offset, count, last_keepalive, reply_started, pause_started, pause_us, hold_started;
    uint8_t stage, waiting, feature, software, part, images, fragments, retries;
} fs_lcd_t;

void fs_lcd_begin(fs_lcd_t *lcd);
int32_t fs_lcd_step(fs_lcd_t *lcd, fs_keypad_t *keypad);
int32_t fs_lcd_input(fs_lcd_t *lcd, const fs_packet_t *packet);
void fs_lcd_status(const fs_lcd_t *lcd, uint8_t bytes[64]);

#endif
