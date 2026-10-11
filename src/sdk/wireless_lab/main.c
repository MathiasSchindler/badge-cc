#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "btstack.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "wireless_lab.h"

#define LCD_WIDTH 320u
#define LCD_HEIGHT 240u
#define LCD_BACKLIGHT 26u
#define LCD_CS 27u
#define LCD_DC 28u
#define LCD_WR 30u
#define LCD_RD 31u
#define LCD_DATA0 32u
#define BOARD_POWER_EN 41u

#define BUTTON_LEFT 7u
#define BUTTON_RIGHT 9u
#define BUTTON_ENTER 10u
#define BUTTON_UP 11u
#define BUTTON_DOWN 6u

#define LAB_SERVICE_UUID16 0xff26u
#define CONTROL_PERIOD_MS 25u
#define DISPLAY_PERIOD_MS 250u
#define BEACON_PERIOD_MS 1000u
#define BENCHMARK_PACKET_COUNT 100u
#define BENCHMARK_TIMEOUT_MS 1000u
#define BENCHMARK_GAP_MS 50u

#define MESSAGE_MANUAL 1u
#define MESSAGE_PING 2u
#define MESSAGE_ACK 3u

#define BADGER_IO_BANK0_BASE 0x40028000u
#define BADGER_PADS_BANK0_BASE 0x40038000u
#define BADGER_SIO_BASE 0xd0000000u
#define GPIO_FUNC_SIO 5u
#define GPIO_CTRL(gpio) ((volatile uint32_t *)(BADGER_IO_BANK0_BASE + 0x04u + ((gpio) * 8u)))
#define GPIO_PAD(gpio) ((volatile uint32_t *)(BADGER_PADS_BANK0_BASE + 0x04u + ((gpio) * 4u)))
#define GPIO_PAD_8MA_FAST 0x21u
#define SIO_GPIO_OUT_SET ((volatile uint32_t *)(BADGER_SIO_BASE + 0x18u))
#define SIO_GPIO_HI_OUT_SET ((volatile uint32_t *)(BADGER_SIO_BASE + 0x1cu))
#define SIO_GPIO_OUT_CLR ((volatile uint32_t *)(BADGER_SIO_BASE + 0x20u))
#define SIO_GPIO_HI_OUT_CLR ((volatile uint32_t *)(BADGER_SIO_BASE + 0x24u))
#define SIO_GPIO_OE_SET ((volatile uint32_t *)(BADGER_SIO_BASE + 0x38u))
#define SIO_GPIO_HI_OE_SET ((volatile uint32_t *)(BADGER_SIO_BASE + 0x3cu))

#define ST7789_SWRESET 0x01u
#define ST7789_SLPOUT 0x11u
#define ST7789_INVON 0x21u
#define ST7789_DISPON 0x29u
#define ST7789_CASET 0x2au
#define ST7789_RASET 0x2bu
#define ST7789_RAMWR 0x2cu
#define ST7789_MADCTL 0x36u
#define ST7789_COLMOD 0x3au
#define ST7789_PORCTRL 0xb2u
#define ST7789_RAMCTRL 0xb0u
#define ST7789_GCTRL 0xb7u
#define ST7789_VCOMS 0xbbu
#define ST7789_LCMCTRL 0xc0u
#define ST7789_VDVVRHEN 0xc2u
#define ST7789_VRHS 0xc3u
#define ST7789_VDVS 0xc4u
#define ST7789_FRCTRL2 0xc6u
#define ST7789_PWCTRL1 0xd0u
#define ST7789_GMCTRP1 0xe0u
#define ST7789_GMCTRN1 0xe1u

typedef enum {
    MODE_SCANNER,
    MODE_BEACON,
    MODE_PEER,
    MODE_METRICS,
    MODE_COUNT
} lab_mode_t;

typedef enum {
    PEER_IDLE,
    PEER_DISCOVERING,
    PEER_CONNECTING,
    PEER_LINKED,
    PEER_DISCOVER_SERVICE,
    PEER_DISCOVER_CHARACTERISTIC,
    PEER_ENABLE_NOTIFY,
    PEER_READY,
    PEER_ERROR
} peer_state_t;

static const uint8_t font_letters[26][7] = {
    {0x0e,0x11,0x11,0x1f,0x11,0x11,0x11},{0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e},
    {0x0e,0x11,0x10,0x10,0x10,0x11,0x0e},{0x1e,0x11,0x11,0x11,0x11,0x11,0x1e},
    {0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f},{0x1f,0x10,0x10,0x1e,0x10,0x10,0x10},
    {0x0e,0x11,0x10,0x17,0x11,0x11,0x0f},{0x11,0x11,0x11,0x1f,0x11,0x11,0x11},
    {0x0e,0x04,0x04,0x04,0x04,0x04,0x0e},{0x07,0x02,0x02,0x02,0x12,0x12,0x0c},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1f},
    {0x11,0x1b,0x15,0x15,0x11,0x11,0x11},{0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    {0x0e,0x11,0x11,0x11,0x11,0x11,0x0e},{0x1e,0x11,0x11,0x1e,0x10,0x10,0x10},
    {0x0e,0x11,0x11,0x11,0x15,0x12,0x0d},{0x1e,0x11,0x11,0x1e,0x14,0x12,0x11},
    {0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e},{0x1f,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0e},{0x11,0x11,0x11,0x11,0x11,0x0a,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0a},{0x11,0x11,0x0a,0x04,0x0a,0x11,0x11},
    {0x11,0x11,0x0a,0x04,0x04,0x04,0x04},{0x1f,0x01,0x02,0x04,0x08,0x10,0x1f},
};

static const uint8_t font_digits[10][7] = {
    {0x0e,0x11,0x13,0x15,0x19,0x11,0x0e},{0x04,0x0c,0x04,0x04,0x04,0x04,0x0e},
    {0x0e,0x11,0x01,0x02,0x04,0x08,0x1f},{0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e},
    {0x02,0x06,0x0a,0x12,0x1f,0x02,0x02},{0x1f,0x10,0x10,0x1e,0x01,0x01,0x1e},
    {0x0e,0x10,0x10,0x1e,0x11,0x11,0x0e},{0x1f,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e},{0x0e,0x11,0x11,0x0f,0x01,0x01,0x0e},
};

static lab_mode_t mode = MODE_SCANNER;
static peer_state_t peer_state = PEER_IDLE;
static bool stack_ready;
static bool local_is_central;
static bool notifications_enabled;
static bool listener_registered;
static volatile bool ui_dirty = true;
static bd_addr_t local_addr;
static bd_addr_t peer_addr;
static bd_addr_type_t peer_addr_type;
static hci_con_handle_t connection_handle = HCI_CON_HANDLE_INVALID;
static gatt_client_service_t peer_service;
static gatt_client_characteristic_t peer_characteristic;
static gatt_client_notification_t notification_listener;
static btstack_packet_callback_registration_t hci_event_registration;
static btstack_timer_source_t ui_timer;
static btstack_timer_source_t beacon_timer;
static uint32_t advertisement_count;
static uint32_t badge_advertisement_count;
static uint32_t beacon_counter;
static uint32_t tx_counter;
static uint32_t rx_counter;
static bool client_write_pending;
static bool notify_pending;
static bool benchmark_running;
static bool benchmark_waiting;
static bool benchmark_transmitted;
static bool benchmark_aborted;
static bool benchmark_send_error;
static bool benchmark_send_blocked;
static bool ack_pending;
static uint32_t benchmark_sent;
static uint32_t benchmark_acked;
static uint32_t benchmark_lost;
static uint32_t benchmark_sequence;
static uint32_t benchmark_sent_at_ms;
static uint32_t benchmark_next_send_ms;
static uint32_t benchmark_rtt_last_ms;
static uint32_t benchmark_rtt_min_ms;
static uint32_t benchmark_rtt_max_ms;
static uint32_t benchmark_rtt_total_ms;
static uint32_t benchmark_send_blocked_at_ms;
static uint32_t ack_sequence;
static uint16_t connection_interval_units;
static uint16_t connection_latency;
static uint8_t connection_profile_status;
static int8_t strongest_rssi = -127;
static bd_addr_t strongest_addr;
static uint8_t message_value[8];
static uint16_t message_length;
static uint8_t notify_value[8];
static uint16_t notify_length;
static uint8_t stored_value[8];
static uint16_t stored_length;
static uint32_t peer_deadline_ms;
static uint8_t peer_error_stage;
static lab_mode_t displayed_mode = MODE_COUNT;
static const uuid128_t lab_service_uuid = {
    0x00u, 0x00u, 0xffu, 0x26u, 0x00u, 0x00u, 0x10u, 0x00u,
    0x80u, 0x00u, 0x00u, 0x80u, 0x5fu, 0x9bu, 0x34u, 0xfbu
};
static const uuid128_t lab_message_uuid = {
    0x00u, 0x00u, 0xffu, 0x27u, 0x00u, 0x00u, 0x10u, 0x00u,
    0x80u, 0x00u, 0x00u, 0x80u, 0x5fu, 0x9bu, 0x34u, 0xfbu
};
static uint8_t advertising_data[] = {
    0x02u, BLUETOOTH_DATA_TYPE_FLAGS, 0x06u,
    0x08u, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME, 'U','2','6',' ','L','A','B',
    0x03u, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS, 0x26u, 0xffu,
    0x09u, BLUETOOTH_DATA_TYPE_MANUFACTURER_SPECIFIC_DATA,
    0xffu, 0xffu, 0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
};

void mp_pico_panic(const char *fmt, ...) {
    (void)fmt;
    while (true) tight_loop_contents();
}

static void raw_gpio_put(uint32_t gpio, uint32_t value) {
    if (gpio < 32u) *(value ? SIO_GPIO_OUT_SET : SIO_GPIO_OUT_CLR) = 1u << gpio;
    else *(value ? SIO_GPIO_HI_OUT_SET : SIO_GPIO_HI_OUT_CLR) = 1u << (gpio - 32u);
}

static void raw_gpio_output(uint32_t gpio, uint32_t initial_value) {
    raw_gpio_put(gpio, initial_value);
    *GPIO_PAD(gpio) = GPIO_PAD_8MA_FAST;
    *GPIO_CTRL(gpio) = GPIO_FUNC_SIO;
    if (gpio < 32u) *SIO_GPIO_OE_SET = 1u << gpio;
    else *SIO_GPIO_HI_OE_SET = 1u << (gpio - 32u);
}

static void delay_cycles(uint32_t cycles) {
    while (cycles-- != 0u) __asm__ volatile ("nop");
}

static void lcd_write_byte(uint8_t value) {
    *SIO_GPIO_HI_OUT_CLR = 0xffu;
    *SIO_GPIO_HI_OUT_SET = value;
    delay_cycles(4u);
    *SIO_GPIO_OUT_CLR = 1u << LCD_WR;
    delay_cycles(8u);
    *SIO_GPIO_OUT_SET = 1u << LCD_WR;
    delay_cycles(4u);
}

static void lcd_write_u16(uint16_t value) {
    lcd_write_byte((uint8_t)(value >> 8));
    lcd_write_byte((uint8_t)value);
}

static void lcd_command(uint8_t command, const uint8_t *data, uint32_t length) {
    raw_gpio_put(LCD_DC, 0u);
    raw_gpio_put(LCD_CS, 0u);
    lcd_write_byte(command);
    raw_gpio_put(LCD_DC, 1u);
    for (uint32_t i = 0; i < length; ++i) lcd_write_byte(data[i]);
    raw_gpio_put(LCD_CS, 1u);
}

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
    return (uint16_t)(((uint16_t)(red & 0xf8u) << 8) |
                      ((uint16_t)(green & 0xfcu) << 3) | ((uint16_t)blue >> 3));
}

static uint8_t glyph_row(char ch, uint32_t row) {
    if (ch >= 'A' && ch <= 'Z') return font_letters[(uint32_t)(ch - 'A')][row];
    if (ch >= '0' && ch <= '9') return font_digits[(uint32_t)(ch - '0')][row];
    if (ch == '-') return row == 3u ? 0x0eu : 0u;
    if (ch == ':') return (row == 2u || row == 5u) ? 0x04u : 0u;
    return 0u;
}

static bool text_pixel(const char *text, uint32_t x, uint32_t y,
                       uint32_t origin_x, uint32_t origin_y, uint32_t scale) {
    if (x < origin_x || y < origin_y) return false;
    uint32_t local_x = x - origin_x;
    uint32_t local_y = y - origin_y;
    if (local_y >= 7u * scale) return false;
    uint32_t index = local_x / (6u * scale);
    uint32_t cell_x = (local_x / scale) % 6u;
    if (cell_x >= 5u || index >= strlen(text)) return false;
    return (glyph_row(text[index], local_y / scale) & (uint8_t)(0x10u >> cell_x)) != 0u;
}

static void lcd_set_window(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    uint32_t x_end = x + width - 1u;
    uint32_t y_end = y + height - 1u;
    const uint8_t caset[] = {
        (uint8_t)(y >> 8), (uint8_t)y,
        (uint8_t)(y_end >> 8), (uint8_t)y_end
    };
    const uint8_t raset[] = {
        (uint8_t)(x >> 8), (uint8_t)x,
        (uint8_t)(x_end >> 8), (uint8_t)x_end
    };
    lcd_command(ST7789_CASET, caset, sizeof(caset));
    lcd_command(ST7789_RASET, raset, sizeof(raset));
}

static uint16_t ui_pixel(uint32_t x, uint32_t y, const char *title,
                         const char *line1, const char *line2, const char *line3,
                         const char *footer) {
    const uint16_t ink = rgb565(22u, 27u, 34u);
    const uint16_t paper = rgb565(252u, 246u, 226u);
    const uint16_t teal = rgb565(15u, 91u, 96u);
    const uint16_t yellow = rgb565(244u, 185u, 48u);
    const uint16_t white = rgb565(255u, 255u, 245u);
    uint16_t color = paper;
    if (y < 55u) color = teal;
    if (y >= 202u) color = ink;
    if (x < 5u || x >= 315u || y < 5u || y >= 235u) color = ink;
    if (text_pixel(title, x, y, 24u, 19u, 2u)) return white;
    if (text_pixel(line1, x, y, 24u, 78u, 2u)) return teal;
    if (text_pixel(line2, x, y, 24u, 111u, 2u)) return ink;
    if (text_pixel(line3, x, y, 24u, 144u, 2u)) return ink;
    if (text_pixel(footer, x, y, 24u, 214u, 1u)) return yellow;
    return color;
}

static void lcd_draw_region(uint32_t x_start, uint32_t y_start,
                            uint32_t width, uint32_t height,
                            const char *title, const char *line1,
                            const char *line2, const char *line3,
                            const char *footer) {
    lcd_set_window(x_start, y_start, width, height);
    raw_gpio_put(LCD_DC, 0u);
    raw_gpio_put(LCD_CS, 0u);
    lcd_write_byte(ST7789_RAMWR);
    raw_gpio_put(LCD_DC, 1u);
    for (uint32_t x = x_start; x < x_start + width; ++x) {
        for (uint32_t y = y_start; y < y_start + height; ++y) {
            lcd_write_u16(ui_pixel(x, y, title, line1, line2, line3, footer));
        }
    }
    raw_gpio_put(LCD_CS, 1u);
}

static void lcd_show(const char *title, const char *line1, const char *line2,
                     const char *line3, const char *footer) {
    lcd_draw_region(0u, 0u, LCD_WIDTH, LCD_HEIGHT,
                    title, line1, line2, line3, footer);
}

static void lcd_update_values(const char *title, const char *line1,
                              const char *line2, const char *line3,
                              const char *footer) {
    if (displayed_mode != mode) {
        lcd_show(title, line1, line2, line3, footer);
        displayed_mode = mode;
        return;
    }
    lcd_draw_region(20u, 74u, 282u, 22u, title, line1, line2, line3, footer);
    lcd_draw_region(20u, 107u, 282u, 22u, title, line1, line2, line3, footer);
    lcd_draw_region(20u, 140u, 282u, 22u, title, line1, line2, line3, footer);
}

static void lcd_init(void) {
    static const uint8_t colmod[] = {0x05u};
    static const uint8_t porctrl[] = {0x0cu,0x0cu,0x00u,0x33u,0x33u};
    static const uint8_t lcmctrl[] = {0x2cu};
    static const uint8_t one[] = {0x01u};
    static const uint8_t vrhs[] = {0x0fu};
    static const uint8_t vdvs[] = {0x20u};
    static const uint8_t pwctrl1[] = {0xa4u,0xa1u};
    static const uint8_t frctrl2[] = {0x0fu};
    static const uint8_t ramctrl[] = {0x00u,0xc0u};
    static const uint8_t gctrl[] = {0x35u};
    static const uint8_t vcoms[] = {0x1bu};
    static const uint8_t madctl[] = {0x90u};

    raw_gpio_output(BOARD_POWER_EN, 1u);
    raw_gpio_output(LCD_BACKLIGHT, 0u);
    raw_gpio_output(LCD_CS, 1u);
    raw_gpio_output(LCD_DC, 1u);
    raw_gpio_output(LCD_WR, 1u);
    raw_gpio_output(LCD_RD, 1u);
    for (uint32_t pin = LCD_DATA0; pin < LCD_DATA0 + 8u; ++pin) raw_gpio_output(pin, 0u);
    lcd_command(ST7789_SWRESET, NULL, 0u);
    sleep_ms(150u);
    lcd_command(ST7789_COLMOD, colmod, sizeof(colmod));
    lcd_command(ST7789_PORCTRL, porctrl, sizeof(porctrl));
    lcd_command(ST7789_LCMCTRL, lcmctrl, sizeof(lcmctrl));
    lcd_command(ST7789_VDVVRHEN, one, sizeof(one));
    lcd_command(ST7789_VRHS, vrhs, sizeof(vrhs));
    lcd_command(ST7789_VDVS, vdvs, sizeof(vdvs));
    lcd_command(ST7789_PWCTRL1, pwctrl1, sizeof(pwctrl1));
    lcd_command(ST7789_FRCTRL2, frctrl2, sizeof(frctrl2));
    lcd_command(ST7789_RAMCTRL, ramctrl, sizeof(ramctrl));
    lcd_command(ST7789_GCTRL, gctrl, sizeof(gctrl));
    lcd_command(ST7789_VCOMS, vcoms, sizeof(vcoms));
    lcd_command(ST7789_INVON, NULL, 0u);
    lcd_command(ST7789_SLPOUT, NULL, 0u);
    sleep_ms(100u);
    lcd_set_window(0u, 0u, LCD_WIDTH, LCD_HEIGHT);
    lcd_command(ST7789_MADCTL, madctl, sizeof(madctl));
    lcd_command(ST7789_DISPON, NULL, 0u);
    raw_gpio_put(LCD_BACKLIGHT, 1u);
}

static void format_addr_suffix(char *buffer, size_t size, const bd_addr_t address) {
    snprintf(buffer, size, "%02X%02X%02X", address[3], address[4], address[5]);
}

static void render_ui(void) {
    char line1[24];
    char line2[24];
    char line3[24];
    char local[8];
    char remote[8];
    format_addr_suffix(local, sizeof(local), local_addr);
    format_addr_suffix(remote, sizeof(remote), strongest_addr);

    if (!stack_ready) {
        lcd_show("WIRELESS LAB", "RADIO STARTING", "PLEASE WAIT", "", "LEFT RIGHT CHANGE MODE");
        return;
    }

    switch (mode) {
        case MODE_SCANNER:
            snprintf(line1, sizeof(line1), "REPORTS %lu", (unsigned long)advertisement_count);
            snprintf(line2, sizeof(line2), "BADGES %lu", (unsigned long)badge_advertisement_count);
            if (strongest_rssi == -127) snprintf(line3, sizeof(line3), "WAITING");
            else snprintf(line3, sizeof(line3), "%s RSSI %d", remote, strongest_rssi);
            lcd_update_values("BLE SCANNER", line1, line2, line3, "LEFT RIGHT MODE");
            break;
        case MODE_BEACON:
            snprintf(line1, sizeof(line1), "ID %s", local);
            snprintf(line2, sizeof(line2), "COUNTER %lu", (unsigned long)beacon_counter);
            snprintf(line3, sizeof(line3), "SERVICE FF26");
            lcd_update_values("BLE BEACON", line1, line2, line3, "ENTER INCREMENT");
            break;
        case MODE_PEER:
            snprintf(line1, sizeof(line1), "ID %s", local);
            if (peer_state == PEER_READY) {
                snprintf(line2, sizeof(line2), "%s %s",
                         local_is_central ? "CENTRAL" : "PERIPHERAL",
                         notifications_enabled || local_is_central ? "READY" : "LINKED");
            } else if (peer_state == PEER_CONNECTING) {
                snprintf(line2, sizeof(line2), "CONNECTING");
            } else if (peer_state == PEER_LINKED) {
                snprintf(line2, sizeof(line2), "PERIPHERAL LINKED");
            } else if (peer_state == PEER_ERROR) {
                const char *stage = peer_error_stage == 1u ? "SERVICE" :
                                    peer_error_stage == 2u ? "CHAR" : "NOTIFY";
                snprintf(line2, sizeof(line2), "ERROR %s", stage);
            } else {
                snprintf(line2, sizeof(line2), "DISCOVERING");
            }
            snprintf(line3, sizeof(line3), "TX %lu RX %lu",
                     (unsigned long)tx_counter, (unsigned long)rx_counter);
            lcd_update_values("BLE PEER", line1, line2, line3, "ENTER SEND LEFT RIGHT MODE");
            break;
        case MODE_METRICS:
            if (peer_state != PEER_READY) {
                snprintf(line1, sizeof(line1), "ID %s", local);
                if (peer_state == PEER_ERROR) {
                    const char *stage = peer_error_stage == 1u ? "SERVICE" :
                                        peer_error_stage == 2u ? "CHAR" : "NOTIFY";
                    snprintf(line2, sizeof(line2), "ERROR %s", stage);
                } else if (peer_state == PEER_CONNECTING) {
                    snprintf(line2, sizeof(line2), "CONNECTING");
                } else {
                    snprintf(line2, sizeof(line2), "DISCOVERING");
                }
                snprintf(line3, sizeof(line3), "WAIT FOR PEER");
            } else {
                if (benchmark_running) {
                    snprintf(line1, sizeof(line1), "RUN %lu OF %u",
                             (unsigned long)benchmark_sent, BENCHMARK_PACKET_COUNT);
                } else if (benchmark_send_error) {
                    snprintf(line1, sizeof(line1), "LOCAL SEND ERROR");
                } else if (benchmark_aborted) {
                    snprintf(line1, sizeof(line1), "STOP %lu ACK %lu",
                             (unsigned long)benchmark_sent,
                             (unsigned long)benchmark_acked);
                } else if (benchmark_sent != 0u) {
                    snprintf(line1, sizeof(line1), "DONE %lu ACK %lu",
                             (unsigned long)benchmark_sent,
                             (unsigned long)benchmark_acked);
                } else {
                    snprintf(line1, sizeof(line1), "%s READY",
                             local_is_central ? "CENTRAL" : "PERIPHERAL");
                }
                if (benchmark_sent == 0u && !benchmark_running) {
                    uint32_t interval_hundredths =
                        (uint32_t)connection_interval_units * 125u;
                    snprintf(line2, sizeof(line2), "CI %lu.%02lu MS LAT %u",
                             (unsigned long)(interval_hundredths / 100u),
                             (unsigned long)(interval_hundredths % 100u),
                             connection_latency);
                    const char *profile = connection_profile_status == 1u ? "REQUEST ULTRA" :
                                          connection_profile_status == 2u ? "REQUEST BASE" :
                                          connection_profile_status == 3u ? "REQUEST ACCEPTED" :
                                          connection_profile_status == 4u ? "PROFILE APPLIED" :
                                          connection_profile_status == 5u ? "PROFILE REJECTED" :
                                          connection_profile_status == 6u ? "LOCAL REQUEST ERR" :
                                          "ENTER TO START";
                    snprintf(line3, sizeof(line3), "%s", profile);
                } else {
                    snprintf(line2, sizeof(line2), "LOSS %lu PCT %lu",
                             (unsigned long)benchmark_lost,
                             (unsigned long)(benchmark_acked + benchmark_lost == 0u ? 0u :
                                             benchmark_lost * 100u /
                                             (benchmark_acked + benchmark_lost)));
                    snprintf(line3, sizeof(line3), "R%lu N%lu A%lu X%lu",
                             (unsigned long)benchmark_rtt_last_ms,
                             (unsigned long)(benchmark_acked == 0u ? 0u : benchmark_rtt_min_ms),
                             (unsigned long)(benchmark_acked == 0u ? 0u :
                                             benchmark_rtt_total_ms / benchmark_acked),
                             (unsigned long)benchmark_rtt_max_ms);
                }
            }
            lcd_update_values("BLE METRICS", line1, line2, line3, "UP ULTRA DOWN BASE ENTER");
            break;
        default:
            break;
    }
}

static void display_core_main(void) {
    while (true) {
        if (ui_dirty) {
            ui_dirty = false;
            __asm__ volatile ("dmb" ::: "memory");
            render_ui();
        }
        sleep_ms(DISPLAY_PERIOD_MS);
    }
}

static void build_message(uint8_t type, uint32_t sequence) {
    message_value[0] = type;
    message_value[1] = local_addr[4];
    message_value[2] = local_addr[5];
    little_endian_store_32(message_value, 3, sequence);
    message_length = 7u;
}

static void configure_advertising(bool connectable) {
    bd_addr_t null_addr = {0};
    advertising_data[21] = (uint8_t)mode;
    advertising_data[22] = (uint8_t)beacon_counter;
    advertising_data[23] = (uint8_t)(beacon_counter >> 8);
    advertising_data[24] = local_addr[4];
    advertising_data[25] = local_addr[5];
    gap_advertisements_set_params(0x00a0u, 0x00f0u, connectable ? 0u : 3u,
                                  0u, null_addr, 0x07u, 0x00u);
    gap_advertisements_set_data(sizeof(advertising_data), advertising_data);
    gap_advertisements_enable(1);
}

static void reset_benchmark(void);

static void stop_radio_activity(void) {
    gap_stop_scan();
    gap_advertisements_enable(0);
    if (connection_handle != HCI_CON_HANDLE_INVALID) gap_disconnect(connection_handle);
    if (listener_registered) {
        gatt_client_stop_listening_for_characteristic_value_updates(&notification_listener);
        listener_registered = false;
    }
    connection_handle = HCI_CON_HANDLE_INVALID;
    notifications_enabled = false;
    client_write_pending = false;
    notify_pending = false;
    benchmark_running = false;
    benchmark_waiting = false;
    benchmark_transmitted = false;
    benchmark_send_blocked = false;
    ack_pending = false;
}

static void apply_mode(void) {
    if (!stack_ready) return;
    stop_radio_activity();
    ui_dirty = true;

    if (mode == MODE_SCANNER) {
        advertisement_count = 0u;
        badge_advertisement_count = 0u;
        strongest_rssi = -127;
        memset(strongest_addr, 0, sizeof(strongest_addr));
        gap_set_scan_parameters(1u, 0x00a0u, 0x0030u);
        gap_start_scan();
    } else if (mode == MODE_BEACON) {
        configure_advertising(false);
    } else {
        if (mode == MODE_METRICS) reset_benchmark();
        peer_state = PEER_DISCOVERING;
        peer_error_stage = 0u;
        local_is_central = false;
        configure_advertising(true);
        gap_set_scan_parameters(1u, 0x0030u, 0x0030u);
        gap_start_scan();
    }
}

static bool local_address_sorts_first(const bd_addr_t remote) {
    for (uint32_t i = 0; i < sizeof(bd_addr_t); ++i) {
        if (local_addr[i] < remote[i]) return true;
        if (local_addr[i] > remote[i]) return false;
    }
    return false;
}

static bool send_packet(uint8_t type, uint32_t sequence);
static void handle_received_packet(const uint8_t *buffer, uint16_t length);

static int att_write_callback(hci_con_handle_t con_handle, uint16_t att_handle,
                              uint16_t transaction_mode, uint16_t offset,
                              uint8_t *buffer, uint16_t buffer_size) {
    (void)transaction_mode;
    if (offset != 0u) return ATT_ERROR_INVALID_OFFSET;
    if (att_handle == ATT_CHARACTERISTIC_0000FF27_0000_1000_8000_00805F9B34FB_01_CLIENT_CONFIGURATION_HANDLE) {
        if (buffer_size < 2u) return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
        notifications_enabled =
            little_endian_read_16(buffer, 0) == GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION;
        connection_handle = con_handle;
        peer_state = notifications_enabled ? PEER_READY : PEER_LINKED;
        ui_dirty = true;
        return 0;
    }
    if (att_handle == ATT_CHARACTERISTIC_0000FF27_0000_1000_8000_00805F9B34FB_01_VALUE_HANDLE) {
        if (buffer_size > sizeof(stored_value)) return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
        memcpy(stored_value, buffer, buffer_size);
        stored_length = buffer_size;
        handle_received_packet(buffer, buffer_size);
    }
    return 0;
}

static uint16_t att_read_callback(hci_con_handle_t con_handle, uint16_t att_handle,
                                  uint16_t offset, uint8_t *buffer, uint16_t buffer_size) {
    (void)con_handle;
    if (att_handle != ATT_CHARACTERISTIC_0000FF27_0000_1000_8000_00805F9B34FB_01_VALUE_HANDLE) return 0u;
    return att_read_callback_handle_blob(stored_value, stored_length, offset, buffer, buffer_size);
}

static void gatt_client_handler(uint8_t packet_type, uint16_t channel,
                                uint8_t *packet, uint16_t size) {
    (void)packet_type;
    (void)channel;
    (void)size;
    uint8_t event = hci_event_packet_get_type(packet);

    if (event == GATT_EVENT_NOTIFICATION) {
        const uint8_t *value = gatt_event_notification_get_value(packet);
        uint16_t length = gatt_event_notification_get_value_length(packet);
        handle_received_packet(value, length);
        return;
    }

    if (peer_state == PEER_READY && event == GATT_EVENT_QUERY_COMPLETE) {
        client_write_pending = false;
        return;
    }

    if (peer_state == PEER_DISCOVER_SERVICE) {
        if (event == GATT_EVENT_SERVICE_QUERY_RESULT) {
            gatt_event_service_query_result_get_service(packet, &peer_service);
        } else if (event == GATT_EVENT_QUERY_COMPLETE) {
            if (gatt_event_query_complete_get_att_status(packet) != ATT_ERROR_SUCCESS ||
                peer_service.start_group_handle == 0u) {
                peer_state = PEER_ERROR;
                peer_error_stage = 1u;
                ui_dirty = true;
                return;
            }
            peer_state = PEER_DISCOVER_CHARACTERISTIC;
            uint8_t status = gatt_client_discover_characteristics_for_service_by_uuid128(
                gatt_client_handler, connection_handle, &peer_service, lab_message_uuid);
            if (status != ERROR_CODE_SUCCESS) {
                peer_state = PEER_ERROR;
                peer_error_stage = 2u;
                ui_dirty = true;
            }
        }
    } else if (peer_state == PEER_DISCOVER_CHARACTERISTIC) {
        if (event == GATT_EVENT_CHARACTERISTIC_QUERY_RESULT) {
            gatt_event_characteristic_query_result_get_characteristic(packet, &peer_characteristic);
        } else if (event == GATT_EVENT_QUERY_COMPLETE) {
            if (gatt_event_query_complete_get_att_status(packet) != ATT_ERROR_SUCCESS ||
                peer_characteristic.value_handle == 0u) {
                peer_state = PEER_ERROR;
                peer_error_stage = 2u;
                ui_dirty = true;
                return;
            }
            listener_registered = true;
            gatt_client_listen_for_characteristic_value_updates(
                &notification_listener, gatt_client_handler, connection_handle, &peer_characteristic);
            peer_state = PEER_ENABLE_NOTIFY;
            uint8_t status = gatt_client_write_client_characteristic_configuration(
                gatt_client_handler, connection_handle, &peer_characteristic,
                GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION);
            if (status != ERROR_CODE_SUCCESS) {
                peer_state = PEER_ERROR;
                peer_error_stage = 3u;
                ui_dirty = true;
            }
        }
    } else if (peer_state == PEER_ENABLE_NOTIFY && event == GATT_EVENT_QUERY_COMPLETE) {
        if (gatt_event_query_complete_get_att_status(packet) == ATT_ERROR_SUCCESS) {
            peer_state = PEER_READY;
        } else {
            peer_state = PEER_ERROR;
            peer_error_stage = 3u;
        }
        ui_dirty = true;
    }
}

static void handle_advertisement(uint8_t *packet) {
    uint8_t length = gap_event_advertising_report_get_data_length(packet);
    const uint8_t *data = gap_event_advertising_report_get_data(packet);
    bd_addr_t address;
    gap_event_advertising_report_get_address(packet, address);
    int8_t rssi = gap_event_advertising_report_get_rssi(packet);
    bool is_badge = ad_data_contains_uuid16(length, data, LAB_SERVICE_UUID16);

    ++advertisement_count;
    if (is_badge) ++badge_advertisement_count;
    if (rssi > strongest_rssi) {
        strongest_rssi = rssi;
        memcpy(strongest_addr, address, sizeof(bd_addr_t));
    }
    ui_dirty = true;

    if ((mode != MODE_PEER && mode != MODE_METRICS) ||
        peer_state != PEER_DISCOVERING || !is_badge) return;
    if (bd_addr_cmp(address, local_addr) == 0) return;
    memcpy(peer_addr, address, sizeof(bd_addr_t));
    peer_addr_type = gap_event_advertising_report_get_address_type(packet);

    if (local_address_sorts_first(address)) {
        local_is_central = true;
        peer_state = PEER_CONNECTING;
        peer_deadline_ms = btstack_run_loop_get_time_ms() + 10000u;
        gap_stop_scan();
        gap_advertisements_enable(0);
        if (gap_connect(peer_addr, peer_addr_type) != ERROR_CODE_SUCCESS) apply_mode();
    } else {
        local_is_central = false;
        peer_state = PEER_CONNECTING;
        peer_deadline_ms = btstack_run_loop_get_time_ms() + 10000u;
        gap_stop_scan();
    }
    ui_dirty = true;
}

static void packet_handler(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size) {
    (void)channel;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;

    uint8_t event = hci_event_packet_get_type(packet);
    if (event == BTSTACK_EVENT_STATE &&
        btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
        gap_local_bd_addr(local_addr);
        stack_ready = true;
        apply_mode();
        return;
    }
    if (event == GAP_EVENT_ADVERTISING_REPORT) {
        handle_advertisement(packet);
        return;
    }
    if (event == HCI_EVENT_META_GAP &&
        hci_event_gap_meta_get_subevent_code(packet) == GAP_SUBEVENT_LE_CONNECTION_COMPLETE) {
        uint8_t status = gap_subevent_le_connection_complete_get_status(packet);
        if (status != ERROR_CODE_SUCCESS) {
            apply_mode();
            return;
        }
        if (event == HCI_EVENT_LE_META &&
            hci_event_le_meta_get_subevent_code(packet) ==
                HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE &&
            hci_subevent_le_connection_update_complete_get_connection_handle(packet) ==
                connection_handle) {
            if (hci_subevent_le_connection_update_complete_get_status(packet) ==
                ERROR_CODE_SUCCESS) {
                connection_interval_units =
                    hci_subevent_le_connection_update_complete_get_conn_interval(packet);
                connection_latency =
                    hci_subevent_le_connection_update_complete_get_conn_latency(packet);
                connection_profile_status = 4u;
            } else {
                connection_profile_status = 5u;
            }
            ui_dirty = true;
            return;
        }
        if (event == L2CAP_EVENT_CONNECTION_PARAMETER_UPDATE_RESPONSE &&
            l2cap_event_connection_parameter_update_response_get_handle(packet) ==
                connection_handle) {
            connection_profile_status =
                l2cap_event_connection_parameter_update_response_get_result(packet) ==
                    ERROR_CODE_SUCCESS ? 3u : 5u;
            ui_dirty = true;
            return;
        }
        connection_handle = gap_subevent_le_connection_complete_get_connection_handle(packet);
        connection_interval_units = gap_subevent_le_connection_complete_get_conn_interval(packet);
        connection_latency = gap_subevent_le_connection_complete_get_conn_latency(packet);
        connection_profile_status = 0u;
        local_is_central = gap_subevent_le_connection_complete_get_role(packet) == HCI_ROLE_MASTER;
        gap_stop_scan();
        gap_advertisements_enable(0);
        if (local_is_central) {
            memset(&peer_service, 0, sizeof(peer_service));
            memset(&peer_characteristic, 0, sizeof(peer_characteristic));
            peer_state = PEER_DISCOVER_SERVICE;
            uint8_t query_status = gatt_client_discover_primary_services_by_uuid128(
                gatt_client_handler, connection_handle, lab_service_uuid);
            if (query_status != ERROR_CODE_SUCCESS) {
                peer_state = PEER_ERROR;
                peer_error_stage = 1u;
            }
        } else {
            peer_state = PEER_LINKED;
        }
        ui_dirty = true;
        return;
    }
    if (event == HCI_EVENT_DISCONNECTION_COMPLETE) {
        connection_handle = HCI_CON_HANDLE_INVALID;
        notifications_enabled = false;
        if (listener_registered) {
            gatt_client_stop_listening_for_characteristic_value_updates(&notification_listener);
            listener_registered = false;
        }
        if (mode == MODE_PEER || mode == MODE_METRICS) apply_mode();
        return;
    }
}

static void att_packet_handler(uint8_t packet_type, uint16_t channel,
                               uint8_t *packet, uint16_t size) {
    (void)channel;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET ||
        hci_event_packet_get_type(packet) != ATT_EVENT_CAN_SEND_NOW) return;
    if (!notifications_enabled || connection_handle == HCI_CON_HANDLE_INVALID) {
        notify_pending = false;
        return;
    }
    uint8_t status = att_server_notify(
        connection_handle,
        ATT_CHARACTERISTIC_0000FF27_0000_1000_8000_00805F9B34FB_01_VALUE_HANDLE,
        notify_value, notify_length);
    if (status == ERROR_CODE_SUCCESS) {
        if (notify_length >= 7u && notify_value[0] == MESSAGE_PING &&
            benchmark_waiting &&
            little_endian_read_32(notify_value, 3) == benchmark_sequence) {
            benchmark_sent_at_ms = btstack_run_loop_get_time_ms();
            benchmark_transmitted = true;
        }
        notify_pending = false;
    } else {
        att_server_request_can_send_now_event(connection_handle);
    }
}

static bool send_packet(uint8_t type, uint32_t sequence) {
    if (peer_state != PEER_READY || connection_handle == HCI_CON_HANDLE_INVALID) return false;
    if (local_is_central && client_write_pending) return false;
    if (!local_is_central && (!notifications_enabled || notify_pending)) return false;

    build_message(type, sequence);
    if (local_is_central) {
        uint8_t status = gatt_client_write_value_of_characteristic(
            gatt_client_handler, connection_handle, peer_characteristic.value_handle,
            message_length, message_value);
        if (status != ERROR_CODE_SUCCESS) return false;
        client_write_pending = true;
    } else {
        memcpy(notify_value, message_value, message_length);
        notify_length = message_length;
        notify_pending = true;
        att_server_request_can_send_now_event(connection_handle);
    }
    return true;
}

static void send_message(void) {
    uint32_t next_counter = tx_counter + 1u;
    if (!send_packet(MESSAGE_MANUAL, next_counter)) return;
    tx_counter = next_counter;
    ui_dirty = true;
}

static void handle_received_packet(const uint8_t *buffer, uint16_t length) {
    if (length < 7u || buffer[0] < MESSAGE_MANUAL || buffer[0] > MESSAGE_ACK) return;
    uint32_t sequence = little_endian_read_32(buffer, 3);
    rx_counter = sequence;
    ui_dirty = true;

    if (buffer[0] == MESSAGE_PING) {
        if (!send_packet(MESSAGE_ACK, sequence)) {
            ack_pending = true;
            ack_sequence = sequence;
        }
        return;
    }
    if (buffer[0] != MESSAGE_ACK || !benchmark_running ||
        !benchmark_waiting || sequence != benchmark_sequence) return;

    uint32_t rtt = btstack_run_loop_get_time_ms() - benchmark_sent_at_ms;
    benchmark_rtt_last_ms = rtt;
    benchmark_rtt_total_ms += rtt;
    if (benchmark_acked == 0u || rtt < benchmark_rtt_min_ms) benchmark_rtt_min_ms = rtt;
    if (rtt > benchmark_rtt_max_ms) benchmark_rtt_max_ms = rtt;
    ++benchmark_acked;
    benchmark_waiting = false;
    benchmark_transmitted = false;
    benchmark_send_blocked = false;
    benchmark_next_send_ms = btstack_run_loop_get_time_ms() + BENCHMARK_GAP_MS;
    if (benchmark_sent >= BENCHMARK_PACKET_COUNT) benchmark_running = false;
}

static void reset_benchmark(void) {
    benchmark_running = false;
    benchmark_waiting = false;
    benchmark_transmitted = false;
    benchmark_aborted = false;
    benchmark_send_error = false;
    benchmark_send_blocked = false;
    benchmark_sent = 0u;
    benchmark_acked = 0u;
    benchmark_lost = 0u;
    benchmark_sequence = 0u;
    benchmark_rtt_last_ms = 0u;
    benchmark_rtt_min_ms = 0u;
    benchmark_rtt_max_ms = 0u;
    benchmark_rtt_total_ms = 0u;
    benchmark_send_blocked_at_ms = 0u;
    benchmark_next_send_ms = btstack_run_loop_get_time_ms();
    ui_dirty = true;
}

static void start_benchmark(void) {
    reset_benchmark();
    benchmark_running = true;
}

static void service_pending_ack(void) {
    if (!ack_pending || peer_state != PEER_READY) return;
    if (send_packet(MESSAGE_ACK, ack_sequence)) ack_pending = false;
}

static void service_benchmark(void) {
    if (mode != MODE_METRICS || peer_state != PEER_READY || !benchmark_running) return;
    uint32_t now = btstack_run_loop_get_time_ms();

    if (benchmark_waiting && !benchmark_transmitted &&
        (int32_t)(now - (benchmark_sent_at_ms + BENCHMARK_TIMEOUT_MS)) >= 0) {
        benchmark_running = false;
        benchmark_send_error = true;
        ui_dirty = true;
    }

    if (benchmark_waiting && benchmark_transmitted &&
        (int32_t)(now - (benchmark_sent_at_ms + BENCHMARK_TIMEOUT_MS)) >= 0) {
        ++benchmark_lost;
        benchmark_waiting = false;
        benchmark_next_send_ms = now + BENCHMARK_GAP_MS;
        if (benchmark_sent >= BENCHMARK_PACKET_COUNT) benchmark_running = false;
        ui_dirty = true;
    }

    if (!benchmark_running || benchmark_waiting ||
        benchmark_sent >= BENCHMARK_PACKET_COUNT ||
        (int32_t)(now - benchmark_next_send_ms) < 0) return;

    uint32_t sequence = benchmark_sent + 1u;
    if (!send_packet(MESSAGE_PING, sequence)) {
        if (!benchmark_send_blocked) {
            benchmark_send_blocked = true;
            benchmark_send_blocked_at_ms = now;
        } else if ((int32_t)(now - (benchmark_send_blocked_at_ms +
                                    BENCHMARK_TIMEOUT_MS)) >= 0) {
            benchmark_running = false;
            benchmark_send_error = true;
            ui_dirty = true;
        }
        return;
    }
    benchmark_send_blocked = false;
    benchmark_sequence = sequence;
    benchmark_sent_at_ms = now;
    benchmark_waiting = true;
    benchmark_transmitted = local_is_central;
    ++benchmark_sent;
    ui_dirty = true;
}

static void request_connection_profile(bool low_latency) {
    if (mode != MODE_METRICS || connection_handle == HCI_CON_HANDLE_INVALID) return;
    uint16_t interval = low_latency ? 6u : 24u;
    uint16_t latency = low_latency ? 0u : 4u;
    connection_profile_status = low_latency ? 1u : 2u;
    ui_dirty = true;
    int status;
    if (local_is_central) {
        status = gap_update_connection_parameters(
            connection_handle, interval, interval, latency, 200u);
    } else {
        status = gap_request_connection_parameter_update(
            connection_handle, interval, interval, latency, 200u);
    }
    if (status != ERROR_CODE_SUCCESS) {
        connection_profile_status = 6u;
        ui_dirty = true;
    }
}

static void beacon_timer_handler(btstack_timer_source_t *timer) {
    if (stack_ready && mode == MODE_BEACON) {
        ++beacon_counter;
        gap_advertisements_enable(0);
        configure_advertising(false);
        ui_dirty = true;
    }
    btstack_run_loop_set_timer(timer, BEACON_PERIOD_MS);
    btstack_run_loop_add_timer(timer);
}

static void ui_timer_handler(btstack_timer_source_t *timer) {
    static bool previous_left = true;
    static bool previous_right = true;
    static bool previous_enter = true;
    static bool previous_up = true;
    static bool previous_down = true;
    bool left = gpio_get(BUTTON_LEFT);
    bool right = gpio_get(BUTTON_RIGHT);
    bool enter = gpio_get(BUTTON_ENTER);
    bool up = gpio_get(BUTTON_UP);
    bool down = gpio_get(BUTTON_DOWN);

    if (previous_left && !left) {
        mode = (lab_mode_t)((mode + MODE_COUNT - 1) % MODE_COUNT);
        apply_mode();
    }
    if (previous_right && !right) {
        mode = (lab_mode_t)((mode + 1) % MODE_COUNT);
        apply_mode();
    }
    if (previous_enter && !enter) {
        if (mode == MODE_BEACON) {
            ++beacon_counter;
            gap_advertisements_enable(0);
            configure_advertising(false);
            ui_dirty = true;
        } else if (mode == MODE_PEER) {
            send_message();
        } else if (mode == MODE_METRICS && peer_state == PEER_READY) {
            if (benchmark_running) {
                if (benchmark_waiting) ++benchmark_lost;
                benchmark_running = false;
                benchmark_waiting = false;
                benchmark_aborted = true;
                ui_dirty = true;
            } else {
                start_benchmark();
            }
        }
    }
    if (previous_up && !up) request_connection_profile(true);
    if (previous_down && !down) request_connection_profile(false);
    previous_left = left;
    previous_right = right;
    previous_enter = enter;
    previous_up = up;
    previous_down = down;

    if ((mode == MODE_PEER || mode == MODE_METRICS) &&
        peer_state == PEER_CONNECTING &&
        (int32_t)(btstack_run_loop_get_time_ms() - peer_deadline_ms) >= 0) {
        if (local_is_central) gap_connect_cancel();
        apply_mode();
    }

    service_pending_ack();
    service_benchmark();

    btstack_run_loop_set_timer(timer, CONTROL_PERIOD_MS);
    btstack_run_loop_add_timer(timer);
}

int main(void) {
    lcd_init();
    lcd_show("WIRELESS LAB", "RADIO STARTING", "PLEASE WAIT", "", "LEFT RIGHT CHANGE MODE");

    const uint8_t buttons[] = {
        BUTTON_LEFT, BUTTON_RIGHT, BUTTON_ENTER, BUTTON_UP, BUTTON_DOWN
    };
    for (uint32_t i = 0; i < sizeof(buttons); ++i) {
        gpio_init(buttons[i]);
        gpio_set_dir(buttons[i], GPIO_IN);
        gpio_pull_up(buttons[i]);
    }

    if (cyw43_arch_init() != 0) {
        lcd_show("WIRELESS LAB", "RADIO ERROR", "INIT FAILED", "", "REBOOT BADGE");
        while (true) tight_loop_contents();
    }

    l2cap_init();
    gatt_client_init();
    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(0u);
    att_server_init(profile_data, att_read_callback, att_write_callback);
    att_server_register_packet_handler(att_packet_handler);
    hci_event_registration.callback = packet_handler;
    hci_add_event_handler(&hci_event_registration);

    ui_timer.process = ui_timer_handler;
    btstack_run_loop_set_timer(&ui_timer, CONTROL_PERIOD_MS);
    btstack_run_loop_add_timer(&ui_timer);
    beacon_timer.process = beacon_timer_handler;
    btstack_run_loop_set_timer(&beacon_timer, BEACON_PERIOD_MS);
    btstack_run_loop_add_timer(&beacon_timer);

    multicore_launch_core1(display_core_main);
    hci_power_control(HCI_POWER_ON);
    btstack_run_loop_execute();
    return 0;
}
