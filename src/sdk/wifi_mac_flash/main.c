#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cyw43.h"
#include "lwip/dns.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/tcp.h"
#include "pico/cyw43_arch.h"
#include "pico/cyw43_driver.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "wifi_credentials.h"

extern volatile uint32_t badger_cyw43_timeout_stage;
extern volatile uint32_t badger_cyw43_driver_stage;
extern volatile int32_t badger_cyw43_packet_ret;
extern volatile uint32_t badger_cyw43_event_type;
extern volatile uint32_t badger_cyw43_event_status;
extern volatile uint32_t badger_cyw43_event_reason;

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

#define LCD_WIDTH 320u
#define LCD_HEIGHT 240u

#define LCD_BACKLIGHT 26u
#define LCD_CS 27u
#define LCD_DC 28u
#define LCD_WR 30u
#define LCD_RD 31u
#define LCD_DATA0 32u
#define LCD_DATA_MASK 0xffu
#define BOARD_POWER_EN 41u
#define BUTTON_LEFT 7u
#define BUTTON_RIGHT 9u
#define BUTTON_UP 11u
#define BUTTON_DOWN 6u

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

static const uint8_t font_letters[26][7] = {
    {0x0eu, 0x11u, 0x11u, 0x1fu, 0x11u, 0x11u, 0x11u}, {0x1eu, 0x11u, 0x11u, 0x1eu, 0x11u, 0x11u, 0x1eu},
    {0x0eu, 0x11u, 0x10u, 0x10u, 0x10u, 0x11u, 0x0eu}, {0x1eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x1eu},
    {0x1fu, 0x10u, 0x10u, 0x1eu, 0x10u, 0x10u, 0x1fu}, {0x1fu, 0x10u, 0x10u, 0x1eu, 0x10u, 0x10u, 0x10u},
    {0x0eu, 0x11u, 0x10u, 0x17u, 0x11u, 0x11u, 0x0fu}, {0x11u, 0x11u, 0x11u, 0x1fu, 0x11u, 0x11u, 0x11u},
    {0x0eu, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x0eu}, {0x07u, 0x02u, 0x02u, 0x02u, 0x12u, 0x12u, 0x0cu},
    {0x11u, 0x12u, 0x14u, 0x18u, 0x14u, 0x12u, 0x11u}, {0x10u, 0x10u, 0x10u, 0x10u, 0x10u, 0x10u, 0x1fu},
    {0x11u, 0x1bu, 0x15u, 0x15u, 0x11u, 0x11u, 0x11u}, {0x11u, 0x19u, 0x15u, 0x13u, 0x11u, 0x11u, 0x11u},
    {0x0eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0eu}, {0x1eu, 0x11u, 0x11u, 0x1eu, 0x10u, 0x10u, 0x10u},
    {0x0eu, 0x11u, 0x11u, 0x11u, 0x15u, 0x12u, 0x0du}, {0x1eu, 0x11u, 0x11u, 0x1eu, 0x14u, 0x12u, 0x11u},
    {0x0fu, 0x10u, 0x10u, 0x0eu, 0x01u, 0x01u, 0x1eu}, {0x1fu, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u},
    {0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0eu}, {0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0au, 0x04u},
    {0x11u, 0x11u, 0x11u, 0x15u, 0x15u, 0x15u, 0x0au}, {0x11u, 0x11u, 0x0au, 0x04u, 0x0au, 0x11u, 0x11u},
    {0x11u, 0x11u, 0x0au, 0x04u, 0x04u, 0x04u, 0x04u}, {0x1fu, 0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x1fu},
};

static const uint8_t font_digits[10][7] = {
    {0x0eu, 0x11u, 0x13u, 0x15u, 0x19u, 0x11u, 0x0eu}, {0x04u, 0x0cu, 0x04u, 0x04u, 0x04u, 0x04u, 0x0eu},
    {0x0eu, 0x11u, 0x01u, 0x02u, 0x04u, 0x08u, 0x1fu}, {0x1eu, 0x01u, 0x01u, 0x0eu, 0x01u, 0x01u, 0x1eu},
    {0x02u, 0x06u, 0x0au, 0x12u, 0x1fu, 0x02u, 0x02u}, {0x1fu, 0x10u, 0x10u, 0x1eu, 0x01u, 0x01u, 0x1eu},
    {0x0eu, 0x10u, 0x10u, 0x1eu, 0x11u, 0x11u, 0x0eu}, {0x1fu, 0x01u, 0x02u, 0x04u, 0x08u, 0x08u, 0x08u},
    {0x0eu, 0x11u, 0x11u, 0x0eu, 0x11u, 0x11u, 0x0eu}, {0x0eu, 0x11u, 0x11u, 0x0fu, 0x01u, 0x01u, 0x0eu},
};

void mp_pico_panic(const char *fmt, ...) {
    (void)fmt;
    while (1) tight_loop_contents();
}

static void delay_cycles(uint32_t cycles) {
    while (cycles-- != 0u) __asm__ volatile ("nop");
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

static void lcd_write_byte(uint8_t value) {
    *SIO_GPIO_HI_OUT_CLR = LCD_DATA_MASK;
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

static void lcd_command(uint8_t command, const uint8_t *data, uint32_t len) {
    raw_gpio_put(LCD_DC, 0u);
    raw_gpio_put(LCD_CS, 0u);
    lcd_write_byte(command);
    if (data != NULL) {
        raw_gpio_put(LCD_DC, 1u);
        for (uint32_t i = 0; i < len; ++i) lcd_write_byte(data[i]);
    }
    raw_gpio_put(LCD_CS, 1u);
}

static void lcd_command0(uint8_t command) {
    lcd_command(command, NULL, 0u);
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((uint16_t)(r & 0xf8u) << 8) | ((uint16_t)(g & 0xfcu) << 3) | ((uint16_t)b >> 3));
}

static uint8_t glyph_row(char ch, uint32_t row) {
    if (ch >= 'A' && ch <= 'Z') return font_letters[(uint32_t)(ch - 'A')][row];
    if (ch >= '0' && ch <= '9') return font_digits[(uint32_t)(ch - '0')][row];
    if (ch == '.') return row == 6u ? 0x04u : 0u;
    if (ch == '-') return row == 3u ? 0x0eu : 0u;
    if (ch == ':') return (row == 2u || row == 5u) ? 0x04u : 0u;
    return 0u;
}

static int text_pixel(const char *text, uint32_t x, uint32_t y, uint32_t origin_x, uint32_t origin_y, uint32_t scale) {
    if (x < origin_x || y < origin_y) return 0;
    uint32_t local_x = x - origin_x;
    uint32_t local_y = y - origin_y;
    if (local_y >= 7u * scale) return 0;
    uint32_t index = local_x / (6u * scale);
    uint32_t cell_x = (local_x / scale) % 6u;
    if (cell_x >= 5u || index >= strlen(text)) return 0;
    return (glyph_row(text[index], local_y / scale) & (uint8_t)(0x10u >> cell_x)) != 0u;
}

static uint16_t pixel_color(uint32_t x, uint32_t y, const char *line1, const char *line2, const char *line3, uint16_t accent) {
    uint16_t ink = rgb565(25u, 30u, 38u);
    uint16_t panel = rgb565(255u, 248u, 228u);
    uint16_t teal = rgb565(22u, 91u, 96u);
    uint16_t white = rgb565(255u, 255u, 245u);
    uint16_t color = panel;

    if (y < 56u) color = teal;
    if (x < 6u || x >= LCD_WIDTH - 6u || y < 6u || y >= LCD_HEIGHT - 6u) color = ink;
    if (x >= 18u && x < 302u && (y == 70u || y == 210u)) color = ink;
    if ((x == 18u || x == 301u) && y >= 70u && y < 211u) color = ink;

    if (text_pixel("MAC FLASH", x, y, 86u, 20u, 2u)) return white;
    if (text_pixel(line1, x, y, 42u, 92u, 2u)) return accent;
    if (text_pixel(line2, x, y, 42u, 124u, 2u)) return ink;
    if (text_pixel(line3, x, y, 42u, 156u, 2u)) return ink;
    return color;
}

static void lcd_set_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    uint32_t x1 = x + w - 1u;
    uint32_t y1 = y + h - 1u;
    uint8_t caset[] = {(uint8_t)(y >> 8), (uint8_t)y, (uint8_t)(y1 >> 8), (uint8_t)y1};
    uint8_t raset[] = {(uint8_t)(x >> 8), (uint8_t)x, (uint8_t)(x1 >> 8), (uint8_t)x1};
    lcd_command(ST7789_CASET, caset, sizeof(caset));
    lcd_command(ST7789_RASET, raset, sizeof(raset));
}

static void lcd_show(const char *line1, const char *line2, const char *line3, uint16_t accent) {
    lcd_set_window(0u, 0u, LCD_WIDTH, LCD_HEIGHT);
    raw_gpio_put(LCD_DC, 0u);
    raw_gpio_put(LCD_CS, 0u);
    lcd_write_byte(ST7789_RAMWR);
    raw_gpio_put(LCD_DC, 1u);
    for (uint32_t x = 0; x < LCD_WIDTH; ++x) {
        for (uint32_t y = 0; y < LCD_HEIGHT; ++y) lcd_write_u16(pixel_color(x, y, line1, line2, line3, accent));
    }
    raw_gpio_put(LCD_CS, 1u);
}

static void lcd_fill(uint16_t color) {
    lcd_set_window(0u, 0u, LCD_WIDTH, LCD_HEIGHT);
    raw_gpio_put(LCD_DC, 0u);
    raw_gpio_put(LCD_CS, 0u);
    lcd_write_byte(ST7789_RAMWR);
    raw_gpio_put(LCD_DC, 1u);
    for (uint32_t pixel = 0u; pixel < LCD_WIDTH * LCD_HEIGHT; ++pixel) {
        lcd_write_u16(color);
    }
    raw_gpio_put(LCD_CS, 1u);
}

static void lcd_init_pins(void) {
    raw_gpio_output(BOARD_POWER_EN, 1u);
    raw_gpio_output(LCD_BACKLIGHT, 0u);
    raw_gpio_output(LCD_CS, 1u);
    raw_gpio_output(LCD_DC, 1u);
    raw_gpio_output(LCD_WR, 1u);
    raw_gpio_output(LCD_RD, 1u);
    for (uint32_t pin = LCD_DATA0; pin < LCD_DATA0 + 8u; ++pin) raw_gpio_output(pin, 0u);
}

static void lcd_init_panel(void) {
    static const uint8_t colmod[] = {0x05u};
    static const uint8_t porctrl[] = {0x0cu, 0x0cu, 0x00u, 0x33u, 0x33u};
    static const uint8_t lcmctrl[] = {0x2cu};
    static const uint8_t vdvvrhen[] = {0x01u};
    static const uint8_t vrhs[] = {0x0fu};
    static const uint8_t vdvs[] = {0x20u};
    static const uint8_t pwctrl1[] = {0xa4u, 0xa1u};
    static const uint8_t frctrl2[] = {0x0fu};
    static const uint8_t ramctrl[] = {0x00u, 0xc0u};
    static const uint8_t gctrl[] = {0x35u};
    static const uint8_t vcoms[] = {0x1bu};
    static const uint8_t gamma_pos[] = {0xf0u, 0x00u, 0x06u, 0x04u, 0x05u, 0x05u, 0x31u, 0x44u, 0x48u, 0x36u, 0x12u, 0x12u, 0x2bu, 0x34u};
    static const uint8_t gamma_neg[] = {0xf0u, 0x0bu, 0x0fu, 0x0fu, 0x0du, 0x26u, 0x31u, 0x43u, 0x47u, 0x38u, 0x14u, 0x14u, 0x2cu, 0x32u};
    static const uint8_t madctl[] = {0x90u};

    lcd_command0(ST7789_SWRESET);
    sleep_ms(150u);
    lcd_command(ST7789_COLMOD, colmod, sizeof(colmod));
    lcd_command(ST7789_PORCTRL, porctrl, sizeof(porctrl));
    lcd_command(ST7789_LCMCTRL, lcmctrl, sizeof(lcmctrl));
    lcd_command(ST7789_VDVVRHEN, vdvvrhen, sizeof(vdvvrhen));
    lcd_command(ST7789_VRHS, vrhs, sizeof(vrhs));
    lcd_command(ST7789_VDVS, vdvs, sizeof(vdvs));
    lcd_command(ST7789_PWCTRL1, pwctrl1, sizeof(pwctrl1));
    lcd_command(ST7789_FRCTRL2, frctrl2, sizeof(frctrl2));
    lcd_command(ST7789_RAMCTRL, ramctrl, sizeof(ramctrl));
    lcd_command(ST7789_GCTRL, gctrl, sizeof(gctrl));
    lcd_command(ST7789_VCOMS, vcoms, sizeof(vcoms));
    lcd_command(ST7789_GMCTRP1, gamma_pos, sizeof(gamma_pos));
    lcd_command(ST7789_GMCTRN1, gamma_neg, sizeof(gamma_neg));
    lcd_command0(ST7789_INVON);
    lcd_command0(ST7789_SLPOUT);
    sleep_ms(100u);
    lcd_set_window(0u, 0u, LCD_WIDTH, LCD_HEIGHT);
    lcd_command(ST7789_MADCTL, madctl, sizeof(madctl));
    lcd_command0(ST7789_DISPON);
    raw_gpio_put(LCD_BACKLIGHT, 1u);
}

static const char *link_status_text(int status) {
    switch (status) {
        case CYW43_LINK_UP: return "LINK UP";
        case CYW43_LINK_NOIP: return "WAIT IP";
        case CYW43_LINK_JOIN: return "JOINING";
        case CYW43_LINK_DOWN: return "LINK DOWN";
        case CYW43_LINK_FAIL: return "LINK FAILED";
        case CYW43_LINK_NONET: return "SSID NOT FOUND";
        case CYW43_LINK_BADAUTH: return "BAD PASSWORD";
        default: return "UNKNOWN STATUS";
    }
}

static const char *bus_timeout_text(uint32_t stage) {
    switch (stage) {
        case 1u: return "RX DMA OUT";
        case 2u: return "RX DMA IN";
        case 3u: return "TX DMA";
        case 4u: return "TX PIO STALL";
        default: return "UNKNOWN STAGE";
    }
}

static const char *driver_stage_text(uint32_t stage) {
    switch (stage) {
        case 1u: return "POLL ENTRY";
        case 2u: return "HOST WAKE CHECK";
        case 3u: return "PACKET LOOP";
        case 4u: return "PACKET BUS POLL";
        case 5u: return "ASYNC EVENT";
        case 6u: return "LWIP ETHERNET";
        case 7u: return "DISASSOC IOCTL";
        case 8u: return "WPA AUTH IOCTL";
        case 9u: return "REJOIN IOCTL";
        case 10u: return "BUS SLEEP";
        case 11u: return "PACKET HOST WAKE";
        case 12u: return "WAKE PACKET BUS";
        case 13u: return "READ SPI INTERRUPT";
        case 14u: return "CLEAR SDIO INTERRUPT";
        case 15u: return "CLEAR SPI INTERRUPT";
        case 16u: return "READ PACKET STATUS";
        case 17u: return "READ PACKET FRAME";
        case 18u: return "PARSE PACKET FRAME";
        case 20u: return "EVENT STATE UPDATE";
        case 21u: return "EVENT TRACE PRINT";
        case 22u: return "SCAN CALLBACK";
        case 23u: return "DISASSOC LINK DOWN";
        case 24u: return "SCHEDULE REJOIN";
        case 25u: return "AP LINK UP";
        case 26u: return "EVENT LINK DOWN";
        case 27u: return "STA LINK UP DHCP";
        case 28u: return "EVENT CALLBACK EXIT";
        case 30u: return "LWIP DHCP OUTPUT";
        case 31u: return "ETH THREAD LOCK";
        case 32u: return "ETH ENSURE UP";
        case 33u: return "ETH SDPCM SEND";
        case 34u: return "ETH SEND RETURN";
        case 35u: return "ETH THREAD UNLOCK";
        case 40u: return "LWIP LINK ENTRY";
        case 41u: return "LWIP LINK FLAGS";
        case 42u: return "DHCP LINK CHANGE";
        case 43u: return "DHCP LINK RETURN";
        case 44u: return "LWIP LINK REPORTS";
        case 45u: return "LWIP REPORTS DONE";
        case 50u: return "DHCP STATE CHECK";
        case 51u: return "DHCP START DISCOVER";
        case 52u: return "DHCP DISCOVER";
        case 53u: return "DHCP CREATE MESSAGE";
        case 54u: return "DHCP OPTIONS";
        case 55u: return "DHCP UDP SEND";
        case 56u: return "DHCP UDP RETURN";
        case 60u: return "PBUF ALLOC ENTRY";
        case 61u: return "PBUF HEAP REQUEST";
        case 62u: return "PBUF HEAP RETURN";
        case 63u: return "PBUF INITIALIZE";
        case 64u: return "PBUF ALIGN CHECK";
        case 70u: return "HEAP ALLOC ENTRY";
        case 71u: return "HEAP ALLOC LOCK";
        case 72u: return "HEAP SCAN START";
        case 73u: return "HEAP SCAN BLOCK";
        case 74u: return "HEAP BLOCK FOUND";
        case 75u: return "HEAP UPDATE FREE";
        case 76u: return "HEAP RETURN BLOCK";
        case 77u: return "HEAP NO MEMORY";
        default: return "UNKNOWN DRIVER";
    }
}

static const char *packet_ret_text(int32_t ret) {
    switch (ret) {
        case -4: return "FLOW CONTROL";
        case 0: return "CONTROL PACKET";
        case 1: return "ASYNC PACKET";
        case 2: return "DATA PACKET";
        default: return "PACKET RET OTHER";
    }
}

static volatile int progress_status = CYW43_LINK_DOWN;
#define PROGRESS_PENDING 0
#define PROGRESS_FAILED 1
#define PROGRESS_CONNECTED 2
#define PROGRESS_NO_IP 3
#define PROGRESS_DNS_FAILED 4
#define PROGRESS_TCP_FAILED 5
#define PROGRESS_INTERNET_OK 6
#define PHASE_JOIN 0
#define PHASE_DNS 1
#define PHASE_TCP 2
static volatile int progress_result = PROGRESS_PENDING;
static volatile int progress_phase = PHASE_JOIN;
static volatile uint32_t progress_heartbeat;
static volatile int display_release_requested;
static volatile int display_released;
static char progress_detail[24];
static volatile int network_test_done;
static volatile err_t network_test_error;
static ip_addr_t resolved_address;

static void publish_progress_result(int result) {
    __asm__ volatile ("dmb" ::: "memory");
    progress_result = result;
}

static void progress_core_main(void) {
    uint16_t green = rgb565(79u, 174u, 124u);
    uint16_t amber = rgb565(246u, 186u, 74u);
    uint16_t red = rgb565(198u, 70u, 64u);
    uint64_t started_us = time_us_64();
    uint64_t heartbeat_changed_us = started_us;
    uint32_t heartbeat = progress_heartbeat;
    uint32_t displayed_seconds = 1000u;
    int stalled_displayed = 0;

    while (1) {
        if (display_release_requested) {
            __asm__ volatile ("dmb" ::: "memory");
            display_released = 1;
            while (1) tight_loop_contents();
        }

        uint32_t timeout_stage = badger_cyw43_timeout_stage;
        if (timeout_stage != 0u) {
            lcd_show("BUS TIMEOUT", bus_timeout_text(timeout_stage), "PIO DIV 4", red);
            while (1) tight_loop_contents();
        }

        int result = progress_result;
        __asm__ volatile ("dmb" ::: "memory");
        if (result != PROGRESS_PENDING) {
            if (result == PROGRESS_CONNECTED) {
                lcd_show("CONNECTED", progress_detail, "LOCAL WIFI", green);
            } else if (result == PROGRESS_NO_IP) {
                lcd_show("NO IP ADDRESS", "LINK REPORTED UP", "CHECK LWIP", red);
            } else if (result == PROGRESS_FAILED) {
                lcd_show("JOIN FAILED", link_status_text(progress_status), "RADIO RESPONDED", red);
            } else if (result == PROGRESS_DNS_FAILED) {
                lcd_show("DNS FAILED", "EXAMPLE COM", "NO NAME LOOKUP", red);
            } else if (result == PROGRESS_TCP_FAILED) {
                lcd_show("TCP FAILED", "EXAMPLE COM 80", "NO INTERNET", red);
            } else if (result == PROGRESS_INTERNET_OK) {
                lcd_show("INTERNET OK", "DNS AND TCP", progress_detail, green);
            }
            while (1) tight_loop_contents();
        }

        uint64_t now_us = time_us_64();
        uint32_t current_heartbeat = progress_heartbeat;
        if (current_heartbeat != heartbeat) {
            heartbeat = current_heartbeat;
            heartbeat_changed_us = now_us;
            if (stalled_displayed) {
                stalled_displayed = 0;
                displayed_seconds = 1000u;
            }
        }

        if (now_us - heartbeat_changed_us >= 2000000u) {
            if (!stalled_displayed) {
                uint32_t driver_stage = badger_cyw43_driver_stage;
                char detail[32];
                if (driver_stage >= 20u) {
                    snprintf(detail, sizeof(detail), "E%u S%u R%u",
                             (unsigned)badger_cyw43_event_type,
                             (unsigned)badger_cyw43_event_status,
                             (unsigned)badger_cyw43_event_reason);
                } else if (driver_stage >= 4u) {
                    snprintf(detail, sizeof(detail), "%s", packet_ret_text(badger_cyw43_packet_ret));
                } else {
                    snprintf(detail, sizeof(detail), "NO HEARTBEAT");
                }
                lcd_show("RADIO STALLED", driver_stage_text(driver_stage), detail, red);
                stalled_displayed = 1;
            }
            busy_wait_us_32(10000u);
            continue;
        }

        uint32_t elapsed_seconds = (uint32_t)((now_us - started_us) / 1000000u) % 1000u;
        if (elapsed_seconds != displayed_seconds) {
            char time_line[] = "TIME 000 SEC";
            time_line[5] = (char)('0' + ((elapsed_seconds / 100u) % 10u));
            time_line[6] = (char)('0' + ((elapsed_seconds / 10u) % 10u));
            time_line[7] = (char)('0' + (elapsed_seconds % 10u));
            if (progress_phase == PHASE_DNS) {
                lcd_show("TESTING DNS", "EXAMPLE COM", time_line, amber);
            } else if (progress_phase == PHASE_TCP) {
                lcd_show("TESTING TCP", "EXAMPLE COM 80", time_line, amber);
            } else {
                lcd_show(link_status_text(progress_status), "BACKGROUND MODE", time_line, amber);
            }
            displayed_seconds = elapsed_seconds;
        }
        busy_wait_us_32(10000u);
    }
}

static void dns_complete(const char *hostname, const ip_addr_t *address, void *arg) {
    (void)hostname;
    (void)arg;
    if (address == NULL) {
        network_test_error = ERR_VAL;
    } else {
        ip_addr_copy(resolved_address, *address);
        network_test_error = ERR_OK;
    }
    __asm__ volatile ("dmb" ::: "memory");
    network_test_done = 1;
}

static err_t tcp_connected(void *arg, struct tcp_pcb *pcb, err_t error) {
    (void)arg;
    network_test_error = error;
    if (error == ERR_OK && tcp_close(pcb) != ERR_OK) {
        tcp_abort(pcb);
    }
    __asm__ volatile ("dmb" ::: "memory");
    network_test_done = 1;
    return ERR_OK;
}

static void tcp_failed(void *arg, err_t error) {
    (void)arg;
    network_test_error = error;
    __asm__ volatile ("dmb" ::: "memory");
    network_test_done = 1;
}

static int wait_for_network_test(uint32_t timeout_ms) {
    uint64_t deadline = time_us_64() + (uint64_t)timeout_ms * 1000u;
    while (!network_test_done && time_us_64() < deadline) {
        progress_heartbeat++;
        busy_wait_us_32(50000u);
    }
    __asm__ volatile ("dmb" ::: "memory");
    return network_test_done && network_test_error == ERR_OK;
}

static uint32_t read_buttons(void) {
    uint32_t buttons = 0u;
    if (!gpio_get(BUTTON_LEFT)) buttons |= 1u << 0u;
    if (!gpio_get(BUTTON_RIGHT)) buttons |= 1u << 1u;
    if (!gpio_get(BUTTON_UP)) buttons |= 1u << 2u;
    if (!gpio_get(BUTTON_DOWN)) buttons |= 1u << 3u;
    return buttons;
}

static void buttons_init(void) {
    static const uint8_t pins[] = {
        BUTTON_LEFT, BUTTON_RIGHT, BUTTON_UP, BUTTON_DOWN,
    };
    for (uint32_t index = 0u; index < sizeof(pins); ++index) {
        gpio_init(pins[index]);
        gpio_set_dir(pins[index], GPIO_IN);
        gpio_pull_up(pins[index]);
    }
}

static uint32_t pressed_buttons(uint32_t *previous) {
    uint32_t current = read_buttons();
    uint32_t pressed = current & ~*previous;
    *previous = current;
    return pressed;
}

static void show_ready(const char *ip_line, const char *mac_line,
                       uint32_t flashes_per_second) {
    char controls[24];
    snprintf(controls, sizeof(controls), "UP START %u FPS",
             (unsigned)flashes_per_second);
    lcd_show(ip_line, mac_line, controls, rgb565(79u, 174u, 124u));
}

static uint8_t mac_group(const uint8_t mac[6], uint32_t group) {
    uint8_t value = 0u;
    uint32_t first_bit = group * 3u;
    for (uint32_t offset = 0u; offset < 3u; ++offset) {
        uint32_t bit = first_bit + offset;
        value = (uint8_t)((value << 1u) |
                          ((mac[bit / 8u] >> (7u - bit % 8u)) & 1u));
    }
    return value;
}

static int show_flash(uint16_t color, uint32_t *flashes_per_second,
                      uint32_t *previous_buttons) {
    uint64_t started = time_us_64();
    lcd_fill(color);

    while (1) {
        uint32_t pressed = pressed_buttons(previous_buttons);
        if ((pressed & (1u << 3u)) != 0u) return 0;
        if ((pressed & (1u << 0u)) != 0u && *flashes_per_second > 2u) {
            --*flashes_per_second;
        }
        if ((pressed & (1u << 1u)) != 0u && *flashes_per_second < 12u) {
            ++*flashes_per_second;
        }

        uint64_t duration = 1000000u / *flashes_per_second;
        if (time_us_64() - started >= duration) return 1;
        busy_wait_us_32(2000u);
    }
}

static int show_mac_sequence(const uint8_t mac[6],
                             uint32_t *flashes_per_second,
                             uint32_t *previous_buttons) {
    uint16_t colors[] = {
        rgb565(255u, 0u, 0u),
        rgb565(0u, 80u, 255u),
        rgb565(0u, 220u, 80u),
    };
    uint16_t white = rgb565(255u, 255u, 255u);

    if (!show_flash(white, flashes_per_second, previous_buttons) ||
        !show_flash(colors[0], flashes_per_second, previous_buttons) ||
        !show_flash(white, flashes_per_second, previous_buttons)) {
        return 0;
    }

    for (uint32_t group = 0u; group < 16u; ++group) {
        uint8_t value = mac_group(mac, group);
        if (!show_flash(colors[value / 3u], flashes_per_second,
                        previous_buttons) ||
            !show_flash(colors[value % 3u], flashes_per_second,
                        previous_buttons)) {
            return 0;
        }
    }

    return show_flash(white, flashes_per_second, previous_buttons) &&
           show_flash(colors[2], flashes_per_second, previous_buttons) &&
           show_flash(white, flashes_per_second, previous_buttons);
}

int main(void) {
    stdio_init_all();
    lcd_init_pins();
    lcd_init_panel();

    uint16_t amber = rgb565(246u, 186u, 74u);
    uint16_t red = rgb565(198u, 70u, 64u);

    if (BADGER_WIFI_SSID[0] == '\0') {
        lcd_show("NO SSID", "CHECK SSID-DATA", "SSID TXT", red);
        while (1) tight_loop_contents();
    }

    cyw43_set_pio_clock_divisor(4u, 0u);
    lcd_show("INIT CYW43", "USING SDK", "PIO DIV 4", amber);
    int init_result = cyw43_arch_init();
    uint32_t timeout_stage = badger_cyw43_timeout_stage;
    if (init_result != 0 || timeout_stage != 0u) {
        lcd_show(timeout_stage == 0u ? "CYW43 INIT FAIL" : "BUS TIMEOUT",
                 timeout_stage == 0u ? "CHECK RADIO" : bus_timeout_text(timeout_stage),
                 timeout_stage == 0u ? "PINS OR POWER" : "DURING INIT", red);
        while (1) tight_loop_contents();
    }

    cyw43_arch_enable_sta_mode();
    timeout_stage = badger_cyw43_timeout_stage;
    if (timeout_stage != 0u || (cyw43_state.itf_state & (1u << CYW43_ITF_STA)) == 0u) {
        lcd_show(timeout_stage == 0u ? "STA ENABLE FAIL" : "BUS TIMEOUT",
                 timeout_stage == 0u ? "WIFI ON FAILED" : bus_timeout_text(timeout_stage),
                 timeout_stage == 0u ? "PIO DIV 4" : "DURING WIFI ON", red);
        while (1) sleep_ms(1000);
    }
    lcd_show("START JOIN", "SSID LOADED", "RADIO COMMANDS", amber);

    const char *password = BADGER_WIFI_PASS[0] == '\0' ? NULL : BADGER_WIFI_PASS;
    uint32_t auth = password == NULL ? CYW43_AUTH_OPEN : CYW43_AUTH_WPA2_MIXED_PSK;
    int result = cyw43_arch_wifi_connect_async(BADGER_WIFI_SSID, password, auth);
    if (result != 0) {
        char line[24];
        snprintf(line, sizeof(line), "ERROR %d", result);
        lcd_show("START FAILED", line, "RADIO COMMAND", red);
        while (1) sleep_ms(1000);
    }

    progress_status = CYW43_LINK_DOWN;
    progress_result = PROGRESS_PENDING;
    progress_heartbeat = 0u;
    multicore_launch_core1(progress_core_main);

    int status = CYW43_LINK_DOWN;
    while (1) {
        status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        progress_status = status;
        progress_heartbeat++;
        if (status == CYW43_LINK_UP || status < 0) break;
        busy_wait_us_32(50000u);
    }

    if (status != CYW43_LINK_UP) {
        publish_progress_result(PROGRESS_FAILED);
        while (1) sleep_ms(1000);
    }

    ip4_addr_t addr;
    int have_addr;
    const ip4_addr_t *current_addr = netif_default == NULL ? NULL : netif_ip4_addr(netif_default);
    have_addr = current_addr != NULL && ip4_addr_get_u32(current_addr) != 0u;
    if (have_addr) addr = *current_addr;
    if (!have_addr) {
        publish_progress_result(PROGRESS_NO_IP);
        while (1) sleep_ms(1000);
    }

    display_release_requested = 1;
    __asm__ volatile ("dmb" ::: "memory");
    while (!display_released) tight_loop_contents();
    multicore_reset_core1();

    uint8_t mac[6];
    if (cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, mac) != 0) {
        lcd_show("MAC READ FAIL", "CYW43 RESPONDED", "RESET BADGER", red);
        while (1) sleep_ms(1000);
    }

    char ip_line[24];
    char mac_line[24];
    snprintf(ip_line, sizeof(ip_line), "IP %s", ip4addr_ntoa(&addr));
    snprintf(mac_line, sizeof(mac_line), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    buttons_init();
    uint32_t flashes_per_second = 6u;
    uint32_t previous_buttons = read_buttons();
    show_ready(ip_line, mac_line, flashes_per_second);

    while (1) {
        uint32_t pressed = pressed_buttons(&previous_buttons);
        if ((pressed & (1u << 0u)) != 0u && flashes_per_second > 2u) {
            --flashes_per_second;
            show_ready(ip_line, mac_line, flashes_per_second);
        }
        if ((pressed & (1u << 1u)) != 0u && flashes_per_second < 12u) {
            ++flashes_per_second;
            show_ready(ip_line, mac_line, flashes_per_second);
        }
        if ((pressed & (1u << 2u)) != 0u) {
            show_mac_sequence(mac, &flashes_per_second, &previous_buttons);
            show_ready(ip_line, mac_line, flashes_per_second);
        }
        busy_wait_us_32(5000u);
    }
}