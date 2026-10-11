#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "ble_tilt_pong.h"
#include "btstack.h"
#include "i2c_bitbang.h"
#include "lsm6ds3.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "pico/stdlib.h"

#define LCD_WIDTH 320
#define LCD_HEIGHT 240
#define LCD_BACKLIGHT 26u
#define LCD_CS 27u
#define LCD_DC 28u
#define LCD_WR 30u
#define LCD_RD 31u
#define LCD_DATA0 32u
#define BOARD_POWER_EN 41u

#define IMU_SDA 18u
#define IMU_SCL 19u
#define FRAME_US 16667u

#define PONG_SERVICE_UUID16 0xff28u
#define RADIO_SERVICE_MS 4u
#define RECONNECT_DELAY_MS 600u
#define CONNECT_TIMEOUT_MS 10000u

#define MSG_PADDLE 1u
#define MSG_SNAPSHOT 2u
#define PADDLE_MESSAGE_SIZE 7u
#define SNAPSHOT_MESSAGE_SIZE 18u

#define PLAY_LEFT 8
#define PLAY_RIGHT 312
#define PLAY_TOP 24
#define PLAY_BOTTOM 238
#define TOP_PADDLE_Y 30
#define BOTTOM_PADDLE_Y 226
#define PADDLE_WIDTH 56
#define PADDLE_HEIGHT 6
#define VERTICAL_MIRROR_AXIS \
    (TOP_PADDLE_Y + BOTTOM_PADDLE_Y + PADDLE_HEIGHT - 1)
#define BALL_RADIUS 4
#define FIXED_SHIFT 8

/* Gameplay tuning, in Q8 pixels/frame unless marked otherwise. */
#define PADDLE_DEAD_ZONE_RAW 800
#define PADDLE_FULL_TILT_RAW 12000
#define PADDLE_MIN_SPEED 64
#define PADDLE_MAX_SPEED 2304
#define BALL_START_X_SPEED 360
#define BALL_START_Y_SPEED 620
#define BALL_MIN_X_SPEED 150
#define BALL_MAX_X_SPEED 1100
#define BALL_MAX_Y_SPEED 1250
#define BALL_HIT_ACCEL 20
#define BALL_OFFSET_GAIN 19
#define BALL_PADDLE_GAIN 4
#define POINT_PAUSE_MS 1000u
#define MATCH_PAUSE_MS 3000u
#define WIN_SCORE 9u

#define FLAG_READY 0x01u
#define FLAG_SERVE_PAUSE 0x02u
#define FLAG_GAME_OVER 0x04u
#define FLAG_WINNER_CENTRAL 0x08u
#define FLAG_LOW_LATENCY 0x10u

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

typedef enum {
    LINK_RADIO_STARTING,
    LINK_DISCOVERING,
    LINK_CONNECTING,
    LINK_CONFIGURING,
    LINK_READY,
    LINK_RECONNECTING,
    LINK_GATT_ERROR,
    LINK_RADIO_ERROR
} link_state_t;

typedef enum {
    GATT_CONFIG_IDLE,
    GATT_CONFIG_WAITING_FOR_CENTRAL,
    GATT_CONFIG_SERVICE,
    GATT_CONFIG_CHARACTERISTIC,
    GATT_CONFIG_CCC
} gatt_config_stage_t;

typedef struct {
    link_state_t state;
    bool central;
    bool interval_ok;
    uint16_t interval;
    uint16_t latency;
} link_view_t;

typedef struct {
    uint16_t sequence;
    int16_t paddle_x;
    int16_t paddle_velocity;
} paddle_update_t;

typedef struct {
    uint16_t sequence;
    int32_t ball_x;
    int32_t ball_y;
    int16_t ball_vx;
    int16_t ball_vy;
    int16_t central_paddle_x;
    int16_t peripheral_paddle_x;
    uint8_t central_score;
    uint8_t peripheral_score;
    uint8_t flags;
} snapshot_t;

typedef struct {
    link_view_t link;
    paddle_update_t local_input;
    uint32_t local_input_generation;
    paddle_update_t remote_input;
    uint32_t remote_input_generation;
    snapshot_t outgoing_snapshot;
    uint32_t outgoing_snapshot_generation;
    snapshot_t incoming_snapshot;
    uint32_t incoming_snapshot_generation;
} shared_state_t;

typedef struct {
    int32_t ball_x;
    int32_t ball_y;
    int32_t bottom_paddle_x;
    int32_t top_paddle_x;
    uint8_t own_score;
    uint8_t peer_score;
    uint8_t flags;
    bool central;
    bool interval_ok;
} display_state_t;

static const uint8_t font_letters[26][7] = {
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14},{7,2,2,2,18,18,12},
    {17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
};

static const uint8_t font_digits[10][7] = {
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14},{14,17,17,15,1,1,14},
};

static mutex_t shared_mutex;
static shared_state_t shared;

static bd_addr_t local_addr;
static bd_addr_t peer_addr;
static bd_addr_type_t peer_addr_type;
static hci_con_handle_t connection_handle = HCI_CON_HANDLE_INVALID;
static gatt_client_service_t peer_service;
static gatt_client_characteristic_t peer_characteristic;
static gatt_client_notification_t notification_listener;
static btstack_packet_callback_registration_t hci_event_registration;
static btstack_context_callback_registration_t client_send_registration;
static btstack_timer_source_t service_timer;
static bool local_is_central;
static volatile gatt_config_stage_t gatt_config_stage;
static bool notifications_enabled;
static bool listener_registered;
static bool client_send_requested;
static bool notify_send_requested;
static bool have_rx_input_sequence;
static bool have_rx_snapshot_sequence;
static uint16_t last_rx_input_sequence;
static uint16_t last_rx_snapshot_sequence;
static uint32_t last_snapshot_tx_generation;
static uint32_t last_input_tx_generation;
static uint32_t reconnect_deadline;
static uint32_t connect_deadline;
static uint8_t stored_value[SNAPSHOT_MESSAGE_SIZE];
static uint16_t stored_length;

static const uuid128_t pong_service_uuid = {
    0x00u,0x00u,0xffu,0x28u,0x00u,0x00u,0x10u,0x00u,
    0x80u,0x00u,0x00u,0x80u,0x5fu,0x9bu,0x34u,0xfbu
};
static const uuid128_t pong_characteristic_uuid = {
    0x00u,0x00u,0xffu,0x29u,0x00u,0x00u,0x10u,0x00u,
    0x80u,0x00u,0x00u,0x80u,0x5fu,0x9bu,0x34u,0xfbu
};
static uint8_t advertising_data[] = {
    0x02u, BLUETOOTH_DATA_TYPE_FLAGS, 0x06u,
    0x09u, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,
    'U','2','6',' ','P','O','N','G',
    0x03u, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS,
    0x28u,0xffu
};

void mp_pico_panic(const char *fmt, ...) {
    (void)fmt;
    while (true) tight_loop_contents();
}

static int32_t clamp_i32(int32_t value, int32_t low, int32_t high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static int32_t minimum(int32_t a, int32_t b) {
    return a < b ? a : b;
}

static int32_t maximum(int32_t a, int32_t b) {
    return a > b ? a : b;
}

static bool sequence_newer(uint16_t sequence, uint16_t previous) {
    return (int16_t)(sequence - previous) > 0;
}

static void shared_set_link(link_state_t state, bool central,
                            uint16_t interval, uint16_t latency) {
    mutex_enter_blocking(&shared_mutex);
    shared.link.state = state;
    shared.link.central = central;
    shared.link.interval = interval;
    shared.link.latency = latency;
    shared.link.interval_ok = interval == 6u && latency == 0u;
    mutex_exit(&shared_mutex);
}

static link_view_t shared_get_link(void) {
    link_view_t value;
    mutex_enter_blocking(&shared_mutex);
    value = shared.link;
    mutex_exit(&shared_mutex);
    return value;
}

static void shared_publish_local_input(const paddle_update_t *input) {
    mutex_enter_blocking(&shared_mutex);
    shared.local_input = *input;
    ++shared.local_input_generation;
    mutex_exit(&shared_mutex);
}

static uint32_t shared_get_local_input(paddle_update_t *input) {
    uint32_t generation;
    mutex_enter_blocking(&shared_mutex);
    *input = shared.local_input;
    generation = shared.local_input_generation;
    mutex_exit(&shared_mutex);
    return generation;
}

static void shared_publish_remote_input(const paddle_update_t *input) {
    mutex_enter_blocking(&shared_mutex);
    shared.remote_input = *input;
    ++shared.remote_input_generation;
    mutex_exit(&shared_mutex);
}

static uint32_t shared_get_remote_input(paddle_update_t *input) {
    uint32_t generation;
    mutex_enter_blocking(&shared_mutex);
    *input = shared.remote_input;
    generation = shared.remote_input_generation;
    mutex_exit(&shared_mutex);
    return generation;
}

static void shared_publish_outgoing_snapshot(const snapshot_t *snapshot) {
    mutex_enter_blocking(&shared_mutex);
    shared.outgoing_snapshot = *snapshot;
    ++shared.outgoing_snapshot_generation;
    mutex_exit(&shared_mutex);
}

static uint32_t shared_get_outgoing_snapshot(snapshot_t *snapshot) {
    uint32_t generation;
    mutex_enter_blocking(&shared_mutex);
    *snapshot = shared.outgoing_snapshot;
    generation = shared.outgoing_snapshot_generation;
    mutex_exit(&shared_mutex);
    return generation;
}

static void shared_publish_incoming_snapshot(const snapshot_t *snapshot) {
    mutex_enter_blocking(&shared_mutex);
    shared.incoming_snapshot = *snapshot;
    ++shared.incoming_snapshot_generation;
    mutex_exit(&shared_mutex);
}

static uint32_t shared_get_incoming_snapshot(snapshot_t *snapshot) {
    uint32_t generation;
    mutex_enter_blocking(&shared_mutex);
    *snapshot = shared.incoming_snapshot;
    generation = shared.incoming_snapshot_generation;
    mutex_exit(&shared_mutex);
    return generation;
}

static void raw_gpio_put(uint32_t gpio, uint32_t value) {
    if (gpio < 32u) {
        *(value ? SIO_GPIO_OUT_SET : SIO_GPIO_OUT_CLR) = 1u << gpio;
    } else {
        *(value ? SIO_GPIO_HI_OUT_SET : SIO_GPIO_HI_OUT_CLR) =
            1u << (gpio - 32u);
    }
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

static void lcd_set_window(uint32_t x, uint32_t y,
                           uint32_t width, uint32_t height) {
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

static void lcd_begin_region(uint32_t x, uint32_t y,
                             uint32_t width, uint32_t height) {
    lcd_set_window(x, y, width, height);
    raw_gpio_put(LCD_DC, 0u);
    raw_gpio_put(LCD_CS, 0u);
    lcd_write_byte(ST7789_RAMWR);
    raw_gpio_put(LCD_DC, 1u);
}

static void lcd_end_region(void) {
    raw_gpio_put(LCD_CS, 1u);
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
    for (uint32_t pin = LCD_DATA0; pin < LCD_DATA0 + 8u; ++pin) {
        raw_gpio_output(pin, 0u);
    }
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

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
    return (uint16_t)(((uint16_t)(red & 0xf8u) << 8) |
                      ((uint16_t)(green & 0xfcu) << 3) |
                      ((uint16_t)blue >> 3));
}

static uint8_t glyph_row(char character, uint32_t row) {
    if (character >= 'A' && character <= 'Z') {
        return font_letters[(uint32_t)(character - 'A')][row];
    }
    if (character >= '0' && character <= '9') {
        return font_digits[(uint32_t)(character - '0')][row];
    }
    if (character == '-') return row == 3u ? 14u : 0u;
    if (character == ':') return (row == 2u || row == 5u) ? 4u : 0u;
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
    return (glyph_row(text[index], local_y / scale) &
            (uint8_t)(0x10u >> cell_x)) != 0u;
}

static uint16_t status_pixel(uint32_t x, uint32_t y,
                             const char *line1, const char *line2) {
    uint16_t background = rgb565(5u, 16u, 31u);
    uint16_t cyan = rgb565(60u, 220u, 220u);
    uint16_t white = rgb565(245u, 248u, 250u);
    if (x < 5u || x >= 315u || y < 5u || y >= 235u) return cyan;
    if (text_pixel("BLE TILT PONG", x, y, 44u, 54u, 3u)) return white;
    if (text_pixel(line1, x, y, 34u, 123u, 2u)) return cyan;
    if (text_pixel(line2, x, y, 34u, 160u, 2u)) return white;
    return background;
}

static void draw_status(const char *line1, const char *line2) {
    lcd_begin_region(0u, 0u, LCD_WIDTH, LCD_HEIGHT);
    for (uint32_t x = 0; x < LCD_WIDTH; ++x) {
        for (uint32_t y = 0; y < LCD_HEIGHT; ++y) {
            lcd_write_u16(status_pixel(x, y, line1, line2));
        }
    }
    lcd_end_region();
}

static const char *game_status_text(const display_state_t *state) {
    if ((state->flags & FLAG_GAME_OVER) != 0u) {
        bool central_won = (state->flags & FLAG_WINNER_CENTRAL) != 0u;
        return central_won == state->central ? "YOU WIN" : "PEER WINS";
    }
    if ((state->flags & FLAG_SERVE_PAUSE) != 0u) return "SERVE";
    return "";
}

static uint16_t game_pixel(uint32_t x, uint32_t y,
                           const display_state_t *state) {
    char score_text[] = "YOU 0  PEER 0";
    const uint16_t background = rgb565(4u, 18u, 31u);
    const uint16_t court = rgb565(72u, 106u, 126u);
    const uint16_t white = rgb565(245u, 248u, 250u);
    const uint16_t own = rgb565(40u, 225u, 184u);
    const uint16_t peer = rgb565(247u, 167u, 53u);
    const uint16_t ball = rgb565(242u, 75u, 91u);
    int32_t dx = (int32_t)x - state->ball_x;
    int32_t dy = (int32_t)y - state->ball_y;

    score_text[4] = (char)('0' + state->own_score);
    score_text[12] = (char)('0' + state->peer_score);
    if (text_pixel(score_text, x, y, 73u, 5u, 2u)) return white;
    if (text_pixel(state->interval_ok ? "7MS" : "TUNE",
                   x, y, 280u, 8u, 1u)) {
        return state->interval_ok ? own : peer;
    }
    if (x == PLAY_LEFT || x == PLAY_RIGHT - 1) return court;
    if (y == PLAY_TOP && x >= PLAY_LEFT && x < PLAY_RIGHT) return court;
    if ((int32_t)x >= state->bottom_paddle_x &&
        (int32_t)x < state->bottom_paddle_x + PADDLE_WIDTH &&
        y >= BOTTOM_PADDLE_Y && y < BOTTOM_PADDLE_Y + PADDLE_HEIGHT) {
        return own;
    }
    if ((int32_t)x >= state->top_paddle_x &&
        (int32_t)x < state->top_paddle_x + PADDLE_WIDTH &&
        y >= TOP_PADDLE_Y && y < TOP_PADDLE_Y + PADDLE_HEIGHT) {
        return peer;
    }
    if (dx * dx + dy * dy <= BALL_RADIUS * BALL_RADIUS) return ball;
    if (y >= 129u && y <= 132u && x >= PLAY_LEFT && x < PLAY_RIGHT &&
        ((x - PLAY_LEFT) % 18u) < 9u) return court;
    const char *status = game_status_text(state);
    uint32_t status_x = (LCD_WIDTH - (uint32_t)strlen(status) * 12u) / 2u;
    if (text_pixel(status, x, y, status_x, 110u, 2u)) return white;
    return background;
}

static void draw_game_region(int32_t x, int32_t y, int32_t width,
                             int32_t height, const display_state_t *state) {
    int32_t left = clamp_i32(x, 0, LCD_WIDTH);
    int32_t top = clamp_i32(y, 0, LCD_HEIGHT);
    int32_t right = clamp_i32(x + width, 0, LCD_WIDTH);
    int32_t bottom = clamp_i32(y + height, 0, LCD_HEIGHT);
    if (right <= left || bottom <= top) return;
    lcd_begin_region((uint32_t)left, (uint32_t)top,
                     (uint32_t)(right - left), (uint32_t)(bottom - top));
    for (int32_t px = left; px < right; ++px) {
        for (int32_t py = top; py < bottom; ++py) {
            lcd_write_u16(game_pixel((uint32_t)px, (uint32_t)py, state));
        }
    }
    lcd_end_region();
}

static void draw_game_full(const display_state_t *state) {
    draw_game_region(0, 0, LCD_WIDTH, LCD_HEIGHT, state);
}

static void draw_moving_object(int32_t old_x, int32_t old_y,
                               int32_t new_x, int32_t new_y,
                               int32_t half_width, int32_t half_height,
                               const display_state_t *state) {
    int32_t left = minimum(old_x, new_x) - half_width - 1;
    int32_t top = minimum(old_y, new_y) - half_height - 1;
    int32_t right = maximum(old_x, new_x) + half_width + 2;
    int32_t bottom = maximum(old_y, new_y) + half_height + 2;
    draw_game_region(left, top, right - left, bottom - top, state);
}

static void render_game_update(const display_state_t *previous,
                               const display_state_t *current) {
    draw_moving_object(previous->ball_x, previous->ball_y,
                       current->ball_x, current->ball_y,
                       BALL_RADIUS, BALL_RADIUS, current);
    draw_moving_object(previous->bottom_paddle_x + PADDLE_WIDTH / 2,
                       BOTTOM_PADDLE_Y + PADDLE_HEIGHT / 2,
                       current->bottom_paddle_x + PADDLE_WIDTH / 2,
                       BOTTOM_PADDLE_Y + PADDLE_HEIGHT / 2,
                       PADDLE_WIDTH / 2, PADDLE_HEIGHT / 2, current);
    draw_moving_object(previous->top_paddle_x + PADDLE_WIDTH / 2,
                       TOP_PADDLE_Y + PADDLE_HEIGHT / 2,
                       current->top_paddle_x + PADDLE_WIDTH / 2,
                       TOP_PADDLE_Y + PADDLE_HEIGHT / 2,
                       PADDLE_WIDTH / 2, PADDLE_HEIGHT / 2, current);
    if (previous->own_score != current->own_score ||
        previous->peer_score != current->peer_score ||
        previous->interval_ok != current->interval_ok) {
        draw_game_region(55, 2, 250, 18, current);
    }
    if ((previous->flags & (FLAG_SERVE_PAUSE | FLAG_GAME_OVER)) !=
        (current->flags & (FLAG_SERVE_PAUSE | FLAG_GAME_OVER))) {
        draw_game_region(65, 105, 190, 24, current);
    }
}

static int32_t update_local_paddle(int32_t filtered_x,
                                   int32_t *position,
                                   int16_t *velocity) {
    int32_t previous = *position;
    int32_t magnitude = filtered_x < 0 ? -filtered_x : filtered_x;
    int32_t speed = 0;
    if (magnitude > PADDLE_DEAD_ZONE_RAW) {
        int32_t range = PADDLE_FULL_TILT_RAW - PADDLE_DEAD_ZONE_RAW;
        int32_t excess = magnitude - PADDLE_DEAD_ZONE_RAW;
        if (excess > range) excess = range;
        speed = PADDLE_MIN_SPEED +
            (int32_t)((int64_t)excess * excess *
                      (PADDLE_MAX_SPEED - PADDLE_MIN_SPEED) /
                      ((int64_t)range * range));
        if (filtered_x < 0) speed = -speed;
    }
    *position += speed;
    *position = clamp_i32(*position, PLAY_LEFT << FIXED_SHIFT,
                          (PLAY_RIGHT - PADDLE_WIDTH) << FIXED_SHIFT);
    *velocity = (int16_t)(*position - previous);
    return *position >> FIXED_SHIFT;
}

typedef struct {
    int32_t ball_x;
    int32_t ball_y;
    int16_t ball_vx;
    int16_t ball_vy;
    int32_t central_paddle_x;
    int16_t central_paddle_velocity;
    int32_t peripheral_paddle_x;
    int16_t peripheral_paddle_velocity;
    uint8_t central_score;
    uint8_t peripheral_score;
    bool paused;
    bool game_over;
    bool winner_central;
    int8_t next_serve_direction;
    uint32_t pause_deadline;
    uint16_t sequence;
} authoritative_game_t;

static void begin_serve(authoritative_game_t *game, int8_t direction,
                        uint32_t now, uint32_t delay_ms) {
    game->ball_x = (LCD_WIDTH / 2) << FIXED_SHIFT;
    game->ball_y = ((PLAY_TOP + PLAY_BOTTOM) / 2) << FIXED_SHIFT;
    game->ball_vx = 0;
    game->ball_vy = 0;
    game->next_serve_direction = direction;
    game->paused = true;
    game->pause_deadline = now + delay_ms;
}

static void reset_match(authoritative_game_t *game, int32_t local_paddle_x,
                        uint32_t now) {
    memset(game, 0, sizeof(*game));
    game->central_paddle_x = local_paddle_x;
    game->peripheral_paddle_x =
        (LCD_WIDTH - PADDLE_WIDTH) / 2;
    begin_serve(game, -1, now, POINT_PAUSE_MS);
}

static int16_t accelerated_vertical(int16_t velocity) {
    int32_t magnitude = velocity < 0 ? -velocity : velocity;
    magnitude += BALL_HIT_ACCEL;
    if (magnitude > BALL_MAX_Y_SPEED) magnitude = BALL_MAX_Y_SPEED;
    return (int16_t)magnitude;
}

static void paddle_bounce(authoritative_game_t *game, int32_t paddle_x,
                          int16_t paddle_velocity, bool bottom) {
    int32_t x = game->ball_x >> FIXED_SHIFT;
    int32_t offset = x - (paddle_x + PADDLE_WIDTH / 2);
    int32_t vx = offset * BALL_OFFSET_GAIN +
                 paddle_velocity / BALL_PADDLE_GAIN;
    vx = clamp_i32(vx, -BALL_MAX_X_SPEED, BALL_MAX_X_SPEED);
    if (vx >= 0 && vx < BALL_MIN_X_SPEED) vx = BALL_MIN_X_SPEED;
    if (vx < 0 && vx > -BALL_MIN_X_SPEED) vx = -BALL_MIN_X_SPEED;
    game->ball_vx = (int16_t)vx;
    int16_t vertical = accelerated_vertical(game->ball_vy);
    game->ball_vy = bottom ? (int16_t)-vertical : vertical;
}

static void score_point(authoritative_game_t *game, bool central_scored,
                        uint32_t now) {
    if (central_scored) ++game->central_score;
    else ++game->peripheral_score;
    if (game->central_score >= WIN_SCORE ||
        game->peripheral_score >= WIN_SCORE) {
        game->game_over = true;
        game->winner_central = game->central_score >= WIN_SCORE;
        begin_serve(game, central_scored ? -1 : 1, now, MATCH_PAUSE_MS);
        return;
    }
    begin_serve(game, central_scored ? -1 : 1, now, POINT_PAUSE_MS);
}

static void update_authoritative_ball(authoritative_game_t *game,
                                      uint32_t now) {
    if (game->paused) {
        if ((int32_t)(now - game->pause_deadline) < 0) return;
        if (game->game_over) {
            game->central_score = 0u;
            game->peripheral_score = 0u;
            game->game_over = false;
            game->winner_central = false;
            begin_serve(game, -game->next_serve_direction,
                        now, POINT_PAUSE_MS);
            return;
        }
        game->paused = false;
        game->ball_vx = game->next_serve_direction < 0 ?
                        -BALL_START_X_SPEED : BALL_START_X_SPEED;
        game->ball_vy = game->next_serve_direction < 0 ?
                        -BALL_START_Y_SPEED : BALL_START_Y_SPEED;
    }

    int32_t previous_y = game->ball_y >> FIXED_SHIFT;
    game->ball_x += game->ball_vx;
    game->ball_y += game->ball_vy;
    int32_t x = game->ball_x >> FIXED_SHIFT;
    int32_t y = game->ball_y >> FIXED_SHIFT;

    if (x - BALL_RADIUS < PLAY_LEFT) {
        game->ball_x = (PLAY_LEFT + BALL_RADIUS) << FIXED_SHIFT;
        if (game->ball_vx < 0) game->ball_vx = (int16_t)-game->ball_vx;
        x = game->ball_x >> FIXED_SHIFT;
    } else if (x + BALL_RADIUS >= PLAY_RIGHT) {
        game->ball_x = (PLAY_RIGHT - BALL_RADIUS - 1) << FIXED_SHIFT;
        if (game->ball_vx > 0) game->ball_vx = (int16_t)-game->ball_vx;
        x = game->ball_x >> FIXED_SHIFT;
    }

    int32_t remote_shared_x =
        LCD_WIDTH - game->peripheral_paddle_x - PADDLE_WIDTH;
    int16_t remote_shared_velocity =
        (int16_t)-game->peripheral_paddle_velocity;
    if (game->ball_vy > 0 &&
        previous_y + BALL_RADIUS < BOTTOM_PADDLE_Y &&
        y + BALL_RADIUS >= BOTTOM_PADDLE_Y &&
        x >= game->central_paddle_x - BALL_RADIUS &&
        x <= game->central_paddle_x + PADDLE_WIDTH + BALL_RADIUS) {
        game->ball_y = (BOTTOM_PADDLE_Y - BALL_RADIUS - 1) << FIXED_SHIFT;
        paddle_bounce(game, game->central_paddle_x,
                      game->central_paddle_velocity, true);
    } else if (game->ball_vy < 0 &&
               previous_y - BALL_RADIUS > TOP_PADDLE_Y + PADDLE_HEIGHT &&
               y - BALL_RADIUS <= TOP_PADDLE_Y + PADDLE_HEIGHT &&
               x >= remote_shared_x - BALL_RADIUS &&
               x <= remote_shared_x + PADDLE_WIDTH + BALL_RADIUS) {
        game->ball_y =
            (TOP_PADDLE_Y + PADDLE_HEIGHT + BALL_RADIUS + 1) << FIXED_SHIFT;
        paddle_bounce(game, remote_shared_x, remote_shared_velocity, false);
    }

    y = game->ball_y >> FIXED_SHIFT;
    if (y + BALL_RADIUS < PLAY_TOP) {
        score_point(game, true, now);
    } else if (y - BALL_RADIUS >= PLAY_BOTTOM) {
        score_point(game, false, now);
    }
}

static snapshot_t make_snapshot(authoritative_game_t *game,
                                bool interval_ok) {
    snapshot_t snapshot;
    snapshot.sequence = ++game->sequence;
    snapshot.ball_x = game->ball_x;
    snapshot.ball_y = game->ball_y;
    snapshot.ball_vx = game->ball_vx;
    snapshot.ball_vy = game->ball_vy;
    snapshot.central_paddle_x = (int16_t)game->central_paddle_x;
    snapshot.peripheral_paddle_x = (int16_t)game->peripheral_paddle_x;
    snapshot.central_score = game->central_score;
    snapshot.peripheral_score = game->peripheral_score;
    snapshot.flags = FLAG_READY;
    if (game->paused) snapshot.flags |= FLAG_SERVE_PAUSE;
    if (game->game_over) snapshot.flags |= FLAG_GAME_OVER;
    if (game->winner_central) snapshot.flags |= FLAG_WINNER_CENTRAL;
    if (interval_ok) snapshot.flags |= FLAG_LOW_LATENCY;
    return snapshot;
}

static display_state_t central_display_state(
    const authoritative_game_t *game, bool interval_ok) {
    display_state_t state;
    state.ball_x = game->ball_x >> FIXED_SHIFT;
    state.ball_y = game->ball_y >> FIXED_SHIFT;
    state.bottom_paddle_x = game->central_paddle_x;
    state.top_paddle_x =
        LCD_WIDTH - game->peripheral_paddle_x - PADDLE_WIDTH;
    state.own_score = game->central_score;
    state.peer_score = game->peripheral_score;
    state.flags = FLAG_READY |
        (game->paused ? FLAG_SERVE_PAUSE : 0u) |
        (game->game_over ? FLAG_GAME_OVER : 0u) |
        (game->winner_central ? FLAG_WINNER_CENTRAL : 0u);
    state.central = true;
    state.interval_ok = interval_ok;
    return state;
}

static display_state_t peripheral_display_state(const snapshot_t *snapshot,
                                                int32_t local_paddle_x,
                                                bool interval_ok) {
    display_state_t state;
    state.ball_x = LCD_WIDTH - 1 - (snapshot->ball_x >> FIXED_SHIFT);
    state.ball_y =
        VERTICAL_MIRROR_AXIS - (snapshot->ball_y >> FIXED_SHIFT);
    state.bottom_paddle_x = local_paddle_x;
    state.top_paddle_x =
        LCD_WIDTH - snapshot->central_paddle_x - PADDLE_WIDTH;
    state.own_score = snapshot->peripheral_score;
    state.peer_score = snapshot->central_score;
    state.flags = snapshot->flags;
    state.central = false;
    state.interval_ok = interval_ok;
    return state;
}

static void show_link_state(link_state_t state) {
    switch (state) {
        case LINK_RADIO_STARTING:
            draw_status("RADIO STARTING", "PLEASE WAIT");
            break;
        case LINK_DISCOVERING:
            draw_status("SEARCHING FOR PEER", "AUTO CONNECT");
            break;
        case LINK_CONNECTING:
            draw_status("PEER FOUND", "CONNECTING");
            break;
        case LINK_CONFIGURING:
            switch (gatt_config_stage) {
                case GATT_CONFIG_WAITING_FOR_CENTRAL:
                    draw_status("CONFIGURING GATT", "PERIPHERAL WAIT");
                    break;
                case GATT_CONFIG_SERVICE:
                    draw_status("CONFIGURING GATT", "CENTRAL SERVICE");
                    break;
                case GATT_CONFIG_CHARACTERISTIC:
                    draw_status("CONFIGURING GATT", "CENTRAL CHAR");
                    break;
                case GATT_CONFIG_CCC:
                    draw_status("CONFIGURING GATT", "CENTRAL NOTIFY");
                    break;
                case GATT_CONFIG_IDLE:
                    draw_status("CONFIGURING GATT", "STARTING");
                    break;
            }
            break;
        case LINK_RECONNECTING:
            draw_status("CONNECTION LOST", "RECONNECTING");
            break;
        case LINK_GATT_ERROR:
            draw_status("GATT ERROR", "RECONNECTING");
            break;
        case LINK_RADIO_ERROR:
            draw_status("RADIO ERROR", "REBOOT BADGE");
            break;
        case LINK_READY:
            break;
    }
}

static void game_core_main(void) {
    uint8_t identity = 0u;
    lsm6ds3_sample_t imu_sample;
    int32_t filtered_x = 0;
    bool filter_initialized = false;
    int32_t paddle_position =
        ((LCD_WIDTH - PADDLE_WIDTH) / 2) << FIXED_SHIFT;
    int16_t paddle_velocity = 0;
    uint16_t input_sequence = 0u;
    uint32_t remote_generation = 0u;
    uint32_t snapshot_generation = 0u;
    authoritative_game_t game;
    display_state_t displayed = {0};
    link_state_t displayed_link = (link_state_t)255;
    gatt_config_stage_t displayed_gatt_stage = GATT_CONFIG_IDLE;
    bool game_visible = false;
    bool was_ready = false;
    bool imu_ok = true;

    lcd_init();
    show_link_state(LINK_RADIO_STARTING);
    if (badger_i2c_init_pins(IMU_SDA, IMU_SCL) != BADGER_I2C_OK ||
        lsm6ds3_init(&identity) != LSM6DS3_OK ||
        !lsm6ds3_read(&imu_sample)) {
        imu_ok = false;
        draw_status("IMU FAILURE", "CHECK GPIO 18 19");
    }

    while (true) {
        uint32_t frame_started = time_us_32();
        if (!imu_ok) {
            sleep_ms(1000u);
            continue;
        }
        if (!lsm6ds3_read(&imu_sample)) {
            imu_ok = false;
            draw_status("IMU READ ERROR", "REBOOT BADGE");
            continue;
        }
        if (!filter_initialized) {
            filtered_x = imu_sample.accel_x;
            filter_initialized = true;
        } else {
            filtered_x += ((int32_t)imu_sample.accel_x - filtered_x) / 3;
        }
        int32_t local_paddle_x =
            update_local_paddle(filtered_x, &paddle_position, &paddle_velocity);
        paddle_update_t local_input = {
            .sequence = ++input_sequence,
            .paddle_x = (int16_t)local_paddle_x,
            .paddle_velocity = paddle_velocity,
        };
        shared_publish_local_input(&local_input);

        link_view_t link = shared_get_link();
        if (link.state != LINK_READY) {
            was_ready = false;
            game_visible = false;
            if (link.state != displayed_link ||
                (link.state == LINK_CONFIGURING &&
                 gatt_config_stage != displayed_gatt_stage)) {
                show_link_state(link.state);
                displayed_link = link.state;
                displayed_gatt_stage = gatt_config_stage;
            }
        } else if (link.central) {
            if (!was_ready) {
                reset_match(&game, local_paddle_x, to_ms_since_boot(get_absolute_time()));
                was_ready = true;
                game_visible = false;
            }
            paddle_update_t remote;
            uint32_t next_remote_generation = shared_get_remote_input(&remote);
            if (next_remote_generation != remote_generation) {
                remote_generation = next_remote_generation;
                game.peripheral_paddle_x =
                    clamp_i32(remote.paddle_x, PLAY_LEFT,
                              PLAY_RIGHT - PADDLE_WIDTH);
                game.peripheral_paddle_velocity = remote.paddle_velocity;
            }
            game.central_paddle_velocity = paddle_velocity;
            game.central_paddle_x = local_paddle_x;
            update_authoritative_ball(
                &game, to_ms_since_boot(get_absolute_time()));
            snapshot_t snapshot = make_snapshot(&game, link.interval_ok);
            shared_publish_outgoing_snapshot(&snapshot);
            display_state_t current =
                central_display_state(&game, link.interval_ok);
            if (!game_visible) {
                draw_game_full(&current);
                game_visible = true;
            } else {
                render_game_update(&displayed, &current);
            }
            displayed = current;
            displayed_link = LINK_READY;
        } else {
            snapshot_t snapshot;
            uint32_t next_snapshot_generation =
                shared_get_incoming_snapshot(&snapshot);
            was_ready = true;
            if (next_snapshot_generation == 0u) {
                if (displayed_link != LINK_READY) {
                    draw_status("CONNECTED", "SYNCING GAME");
                    displayed_link = LINK_READY;
                }
            } else {
                snapshot_generation = next_snapshot_generation;
                display_state_t current =
                    peripheral_display_state(&snapshot, local_paddle_x,
                                             link.interval_ok);
                if (!game_visible) {
                    draw_game_full(&current);
                    game_visible = true;
                } else {
                    render_game_update(&displayed, &current);
                }
                displayed = current;
                displayed_link = LINK_READY;
            }
            (void)snapshot_generation;
        }

        int32_t remaining =
            (int32_t)(FRAME_US - (time_us_32() - frame_started));
        if (remaining > 0) sleep_us((uint32_t)remaining);
    }
}

static void encode_paddle(uint8_t *buffer, const paddle_update_t *input) {
    buffer[0] = MSG_PADDLE;
    little_endian_store_16(buffer, 1, input->sequence);
    little_endian_store_16(buffer, 3, (uint16_t)input->paddle_x);
    little_endian_store_16(buffer, 5, (uint16_t)input->paddle_velocity);
}

static void encode_snapshot(uint8_t *buffer, const snapshot_t *snapshot) {
    buffer[0] = MSG_SNAPSHOT;
    little_endian_store_16(buffer, 1, snapshot->sequence);
    little_endian_store_16(buffer, 3, (uint16_t)(snapshot->ball_x >> 4));
    little_endian_store_16(buffer, 5, (uint16_t)(snapshot->ball_y >> 4));
    little_endian_store_16(buffer, 7, (uint16_t)snapshot->ball_vx);
    little_endian_store_16(buffer, 9, (uint16_t)snapshot->ball_vy);
    little_endian_store_16(buffer, 11, (uint16_t)snapshot->central_paddle_x);
    little_endian_store_16(buffer, 13, (uint16_t)snapshot->peripheral_paddle_x);
    buffer[15] = snapshot->central_score;
    buffer[16] = snapshot->peripheral_score;
    buffer[17] = snapshot->flags;
}

static bool decode_paddle(const uint8_t *buffer, uint16_t length,
                          paddle_update_t *input) {
    if (length != PADDLE_MESSAGE_SIZE || buffer[0] != MSG_PADDLE) return false;
    input->sequence = little_endian_read_16(buffer, 1);
    input->paddle_x = (int16_t)little_endian_read_16(buffer, 3);
    input->paddle_velocity = (int16_t)little_endian_read_16(buffer, 5);
    if (input->paddle_x < PLAY_LEFT ||
        input->paddle_x > PLAY_RIGHT - PADDLE_WIDTH) return false;
    return true;
}

static bool decode_snapshot(const uint8_t *buffer, uint16_t length,
                            snapshot_t *snapshot) {
    if (length != SNAPSHOT_MESSAGE_SIZE || buffer[0] != MSG_SNAPSHOT) {
        return false;
    }
    snapshot->sequence = little_endian_read_16(buffer, 1);
    snapshot->ball_x = (int32_t)(int16_t)little_endian_read_16(buffer, 3) << 4;
    snapshot->ball_y = (int32_t)(int16_t)little_endian_read_16(buffer, 5) << 4;
    snapshot->ball_vx = (int16_t)little_endian_read_16(buffer, 7);
    snapshot->ball_vy = (int16_t)little_endian_read_16(buffer, 9);
    snapshot->central_paddle_x =
        (int16_t)little_endian_read_16(buffer, 11);
    snapshot->peripheral_paddle_x =
        (int16_t)little_endian_read_16(buffer, 13);
    snapshot->central_score = buffer[15];
    snapshot->peripheral_score = buffer[16];
    snapshot->flags = buffer[17];
    if (snapshot->ball_x < (PLAY_LEFT - BALL_RADIUS) << FIXED_SHIFT ||
        snapshot->ball_x > (PLAY_RIGHT + BALL_RADIUS) << FIXED_SHIFT ||
        snapshot->ball_y < (PLAY_TOP - BALL_RADIUS) << FIXED_SHIFT ||
        snapshot->ball_y > (PLAY_BOTTOM + BALL_RADIUS) << FIXED_SHIFT ||
        snapshot->central_paddle_x < PLAY_LEFT ||
        snapshot->central_paddle_x > PLAY_RIGHT - PADDLE_WIDTH ||
        snapshot->peripheral_paddle_x < PLAY_LEFT ||
        snapshot->peripheral_paddle_x > PLAY_RIGHT - PADDLE_WIDTH ||
        snapshot->central_score > WIN_SCORE ||
        snapshot->peripheral_score > WIN_SCORE) {
        return false;
    }
    return true;
}

static void handle_received_value(const uint8_t *buffer, uint16_t length) {
    if (local_is_central) {
        paddle_update_t input;
        if (!decode_paddle(buffer, length, &input)) return;
        if (have_rx_input_sequence &&
            !sequence_newer(input.sequence, last_rx_input_sequence)) return;
        have_rx_input_sequence = true;
        last_rx_input_sequence = input.sequence;
        shared_publish_remote_input(&input);
    } else {
        snapshot_t snapshot;
        if (!decode_snapshot(buffer, length, &snapshot)) return;
        if (have_rx_snapshot_sequence &&
            !sequence_newer(snapshot.sequence, last_rx_snapshot_sequence)) {
            return;
        }
        have_rx_snapshot_sequence = true;
        last_rx_snapshot_sequence = snapshot.sequence;
        shared_publish_incoming_snapshot(&snapshot);
    }
}

static bool local_address_sorts_first(const bd_addr_t remote) {
    for (uint32_t i = 0; i < sizeof(bd_addr_t); ++i) {
        if (local_addr[i] < remote[i]) return true;
        if (local_addr[i] > remote[i]) return false;
    }
    return false;
}

static void configure_advertising(void) {
    bd_addr_t null_addr = {0};
    gap_advertisements_set_params(0x0030u, 0x0060u, 0u, 0u, null_addr,
                                  0x07u, 0x00u);
    gap_advertisements_set_data(sizeof(advertising_data), advertising_data);
    gap_advertisements_enable(1);
}

static void reset_transport_state(void) {
    notifications_enabled = false;
    client_send_requested = false;
    notify_send_requested = false;
    have_rx_input_sequence = false;
    have_rx_snapshot_sequence = false;
    gatt_config_stage = GATT_CONFIG_IDLE;
    last_snapshot_tx_generation = 0u;
    last_input_tx_generation = 0u;
    if (listener_registered) {
        gatt_client_stop_listening_for_characteristic_value_updates(
            &notification_listener);
        listener_registered = false;
    }
    mutex_enter_blocking(&shared_mutex);
    shared.remote_input_generation = 0u;
    shared.outgoing_snapshot_generation = 0u;
    shared.incoming_snapshot_generation = 0u;
    mutex_exit(&shared_mutex);
}

static void start_discovery(void) {
    reset_transport_state();
    connection_handle = HCI_CON_HANDLE_INVALID;
    local_is_central = false;
    shared_set_link(LINK_DISCOVERING, false, 0u, 0u);
    configure_advertising();
    gap_set_scan_parameters(1u, 0x0030u, 0x0030u);
    gap_start_scan();
}

static void schedule_reconnect(link_state_t state) {
    gap_stop_scan();
    gap_advertisements_enable(0);
    reset_transport_state();
    shared_set_link(state, local_is_central, 0u, 0u);
    reconnect_deadline = btstack_run_loop_get_time_ms() + RECONNECT_DELAY_MS;
}

static int att_write_callback(hci_con_handle_t con_handle, uint16_t att_handle,
                              uint16_t transaction_mode, uint16_t offset,
                              uint8_t *buffer, uint16_t buffer_size) {
    (void)transaction_mode;
    if (offset != 0u) return ATT_ERROR_INVALID_OFFSET;
    if (att_handle ==
        ATT_CHARACTERISTIC_0000FF29_0000_1000_8000_00805F9B34FB_01_CLIENT_CONFIGURATION_HANDLE) {
        if (buffer_size < 2u) return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
        notifications_enabled =
            little_endian_read_16(buffer, 0) ==
            GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION;
        connection_handle = con_handle;
        if (notifications_enabled && !local_is_central) {
            link_view_t link = shared_get_link();
            shared_set_link(LINK_READY, false, link.interval, link.latency);
        }
        return 0;
    }
    if (att_handle ==
        ATT_CHARACTERISTIC_0000FF29_0000_1000_8000_00805F9B34FB_01_VALUE_HANDLE) {
        if (buffer_size > sizeof(stored_value)) {
            return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
        }
        memcpy(stored_value, buffer, buffer_size);
        stored_length = buffer_size;
        if (!local_is_central) handle_received_value(buffer, buffer_size);
    }
    return 0;
}

static uint16_t att_read_callback(hci_con_handle_t con_handle,
                                  uint16_t att_handle, uint16_t offset,
                                  uint8_t *buffer, uint16_t buffer_size) {
    (void)con_handle;
    if (att_handle !=
        ATT_CHARACTERISTIC_0000FF29_0000_1000_8000_00805F9B34FB_01_VALUE_HANDLE) {
        return 0u;
    }
    return att_read_callback_handle_blob(stored_value, stored_length, offset,
                                         buffer, buffer_size);
}

static void fail_gatt(void) {
    shared_set_link(LINK_GATT_ERROR, local_is_central, 0u, 0u);
    reconnect_deadline = btstack_run_loop_get_time_ms() + RECONNECT_DELAY_MS;
}

static void gatt_client_handler(uint8_t packet_type, uint16_t channel,
                                uint8_t *packet, uint16_t size) {
    (void)packet_type;
    (void)channel;
    (void)size;
    uint8_t event = hci_event_packet_get_type(packet);
    if (event == GATT_EVENT_NOTIFICATION) {
        handle_received_value(gatt_event_notification_get_value(packet),
                              gatt_event_notification_get_value_length(packet));
        return;
    }

    link_view_t link = shared_get_link();
    if (link.state == LINK_CONFIGURING) {
        if (event == GATT_EVENT_SERVICE_QUERY_RESULT) {
            gatt_event_service_query_result_get_service(packet, &peer_service);
            return;
        }
        if (event == GATT_EVENT_QUERY_COMPLETE &&
            gatt_config_stage == GATT_CONFIG_SERVICE) {
            if (gatt_event_query_complete_get_att_status(packet) !=
                    ATT_ERROR_SUCCESS ||
                peer_service.start_group_handle == 0u) {
                fail_gatt();
                return;
            }
            gatt_config_stage = GATT_CONFIG_CHARACTERISTIC;
            uint8_t status =
                gatt_client_discover_characteristics_for_service_by_uuid128(
                    gatt_client_handler, connection_handle, &peer_service,
                    pong_characteristic_uuid);
            if (status != ERROR_CODE_SUCCESS) fail_gatt();
            return;
        }
        if (event == GATT_EVENT_CHARACTERISTIC_QUERY_RESULT) {
            gatt_event_characteristic_query_result_get_characteristic(
                packet, &peer_characteristic);
            return;
        }
        if (event == GATT_EVENT_QUERY_COMPLETE &&
            gatt_config_stage == GATT_CONFIG_CHARACTERISTIC) {
            if (gatt_event_query_complete_get_att_status(packet) !=
                    ATT_ERROR_SUCCESS ||
                peer_characteristic.value_handle == 0u) {
                fail_gatt();
                return;
            }
            gatt_config_stage = GATT_CONFIG_CCC;
            listener_registered = true;
            gatt_client_listen_for_characteristic_value_updates(
                &notification_listener, gatt_client_handler,
                connection_handle, &peer_characteristic);
            uint8_t status =
                gatt_client_write_client_characteristic_configuration(
                    gatt_client_handler, connection_handle,
                    &peer_characteristic,
                    GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION);
            if (status != ERROR_CODE_SUCCESS) fail_gatt();
            return;
        }
        if (event == GATT_EVENT_QUERY_COMPLETE &&
            gatt_config_stage == GATT_CONFIG_CCC) {
            if (gatt_event_query_complete_get_att_status(packet) ==
                ATT_ERROR_SUCCESS) {
                gatt_config_stage = GATT_CONFIG_IDLE;
                shared_set_link(LINK_READY, true,
                                link.interval, link.latency);
                gap_update_connection_parameters(connection_handle, 6u, 6u,
                                                 0u, 200u);
            } else {
                fail_gatt();
            }
        }
    }
}

static void handle_advertisement(uint8_t *packet) {
    link_view_t link = shared_get_link();
    if (link.state != LINK_DISCOVERING) return;
    uint8_t length = gap_event_advertising_report_get_data_length(packet);
    const uint8_t *data = gap_event_advertising_report_get_data(packet);
    if (!ad_data_contains_uuid16(length, data, PONG_SERVICE_UUID16)) return;
    bd_addr_t address;
    gap_event_advertising_report_get_address(packet, address);
    if (bd_addr_cmp(address, local_addr) == 0) return;
    memcpy(peer_addr, address, sizeof(peer_addr));
    peer_addr_type = gap_event_advertising_report_get_address_type(packet);
    connect_deadline = btstack_run_loop_get_time_ms() + CONNECT_TIMEOUT_MS;
    gap_stop_scan();
    if (local_address_sorts_first(address)) {
        local_is_central = true;
        gap_advertisements_enable(0);
        shared_set_link(LINK_CONNECTING, true, 0u, 0u);
        if (gap_connect(peer_addr, peer_addr_type) != ERROR_CODE_SUCCESS) {
            schedule_reconnect(LINK_RECONNECTING);
        }
    } else {
        local_is_central = false;
        shared_set_link(LINK_CONNECTING, false, 0u, 0u);
    }
}

static void update_connection_view(uint16_t interval, uint16_t latency) {
    link_view_t link = shared_get_link();
    shared_set_link(link.state, link.central, interval, latency);
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
        start_discovery();
        return;
    }
    if (event == GAP_EVENT_ADVERTISING_REPORT) {
        handle_advertisement(packet);
        return;
    }
    if (event == HCI_EVENT_META_GAP &&
        hci_event_gap_meta_get_subevent_code(packet) ==
        GAP_SUBEVENT_LE_CONNECTION_COMPLETE) {
        if (gap_subevent_le_connection_complete_get_status(packet) !=
            ERROR_CODE_SUCCESS) {
            schedule_reconnect(LINK_RECONNECTING);
            return;
        }
        connection_handle =
            gap_subevent_le_connection_complete_get_connection_handle(packet);
        local_is_central =
            gap_subevent_le_connection_complete_get_role(packet) ==
            HCI_ROLE_MASTER;
        uint16_t interval =
            gap_subevent_le_connection_complete_get_conn_interval(packet);
        uint16_t latency =
            gap_subevent_le_connection_complete_get_conn_latency(packet);
        gap_stop_scan();
        gap_advertisements_enable(0);
        reset_transport_state();
        shared_set_link(LINK_CONFIGURING, local_is_central,
                        interval, latency);
        connect_deadline = btstack_run_loop_get_time_ms() + CONNECT_TIMEOUT_MS;
        if (local_is_central) {
            memset(&peer_service, 0, sizeof(peer_service));
            memset(&peer_characteristic, 0, sizeof(peer_characteristic));
            gatt_config_stage = GATT_CONFIG_SERVICE;
            uint8_t status = gatt_client_discover_primary_services_by_uuid128(
                gatt_client_handler, connection_handle, pong_service_uuid);
            if (status != ERROR_CODE_SUCCESS) fail_gatt();
        } else {
            gatt_config_stage = GATT_CONFIG_WAITING_FOR_CENTRAL;
        }
        return;
    }
    if (event == HCI_EVENT_LE_META &&
        hci_event_le_meta_get_subevent_code(packet) ==
        HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE &&
        hci_subevent_le_connection_update_complete_get_connection_handle(
            packet) == connection_handle &&
        hci_subevent_le_connection_update_complete_get_status(packet) ==
            ERROR_CODE_SUCCESS) {
        update_connection_view(
            hci_subevent_le_connection_update_complete_get_conn_interval(packet),
            hci_subevent_le_connection_update_complete_get_conn_latency(packet));
        return;
    }
    if (event == HCI_EVENT_DISCONNECTION_COMPLETE) {
        connection_handle = HCI_CON_HANDLE_INVALID;
        schedule_reconnect(LINK_RECONNECTING);
    }
}

static void client_can_send(void *context) {
    (void)context;
    client_send_requested = false;
    link_view_t link = shared_get_link();
    if (link.state != LINK_READY || !local_is_central ||
        connection_handle == HCI_CON_HANDLE_INVALID) return;
    snapshot_t snapshot;
    uint32_t generation = shared_get_outgoing_snapshot(&snapshot);
    if (generation == 0u || generation == last_snapshot_tx_generation) return;
    uint8_t buffer[SNAPSHOT_MESSAGE_SIZE];
    encode_snapshot(buffer, &snapshot);
    uint8_t status =
        gatt_client_write_value_of_characteristic_without_response(
            connection_handle, peer_characteristic.value_handle,
            sizeof(buffer), buffer);
    if (status == ERROR_CODE_SUCCESS) {
        last_snapshot_tx_generation = generation;
    }
}

static void request_client_send(void) {
    if (client_send_requested) return;
    client_send_requested = true;
    client_send_registration.callback = client_can_send;
    client_send_registration.context = NULL;
    uint8_t status = gatt_client_request_to_write_without_response(
        &client_send_registration, connection_handle);
    if (status != ERROR_CODE_SUCCESS) client_send_requested = false;
}

static void att_packet_handler(uint8_t packet_type, uint16_t channel,
                               uint8_t *packet, uint16_t size) {
    (void)channel;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET ||
        hci_event_packet_get_type(packet) != ATT_EVENT_CAN_SEND_NOW) return;
    notify_send_requested = false;
    link_view_t link = shared_get_link();
    if (link.state != LINK_READY || local_is_central ||
        !notifications_enabled ||
        connection_handle == HCI_CON_HANDLE_INVALID) return;
    paddle_update_t input;
    uint32_t generation = shared_get_local_input(&input);
    if (generation == 0u || generation == last_input_tx_generation) return;
    uint8_t buffer[PADDLE_MESSAGE_SIZE];
    encode_paddle(buffer, &input);
    uint8_t status = att_server_notify(
        connection_handle,
        ATT_CHARACTERISTIC_0000FF29_0000_1000_8000_00805F9B34FB_01_VALUE_HANDLE,
        buffer, sizeof(buffer));
    if (status == ERROR_CODE_SUCCESS) {
        last_input_tx_generation = generation;
    }
}

static void service_timer_handler(btstack_timer_source_t *timer) {
    uint32_t now = btstack_run_loop_get_time_ms();
    link_view_t link = shared_get_link();
    if ((link.state == LINK_RECONNECTING ||
         link.state == LINK_GATT_ERROR) &&
        (int32_t)(now - reconnect_deadline) >= 0) {
        if (connection_handle != HCI_CON_HANDLE_INVALID) {
            gap_disconnect(connection_handle);
            reconnect_deadline = now + RECONNECT_DELAY_MS;
        } else {
            start_discovery();
        }
    } else if ((link.state == LINK_CONNECTING ||
                link.state == LINK_CONFIGURING) &&
               (int32_t)(now - connect_deadline) >= 0) {
        if (link.state == LINK_CONNECTING && local_is_central) {
            gap_connect_cancel();
        }
        schedule_reconnect(LINK_RECONNECTING);
    } else if (link.state == LINK_READY) {
        if (local_is_central) {
            snapshot_t snapshot;
            uint32_t generation =
                shared_get_outgoing_snapshot(&snapshot);
            if (generation != 0u &&
                generation != last_snapshot_tx_generation) {
                request_client_send();
            }
        } else if (notifications_enabled && !notify_send_requested) {
            paddle_update_t input;
            uint32_t generation = shared_get_local_input(&input);
            if (generation != 0u &&
                generation != last_input_tx_generation) {
                notify_send_requested = true;
                att_server_request_can_send_now_event(connection_handle);
            }
        }
    }
    btstack_run_loop_set_timer(timer, RADIO_SERVICE_MS);
    btstack_run_loop_add_timer(timer);
}

int main(void) {
    mutex_init(&shared_mutex);
    memset(&shared, 0, sizeof(shared));
    shared.link.state = LINK_RADIO_STARTING;

    if (cyw43_arch_init() != 0) {
        shared_set_link(LINK_RADIO_ERROR, false, 0u, 0u);
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

    service_timer.process = service_timer_handler;
    btstack_run_loop_set_timer(&service_timer, RADIO_SERVICE_MS);
    btstack_run_loop_add_timer(&service_timer);
    multicore_launch_core1(game_core_main);
    hci_power_control(HCI_POWER_ON);
    btstack_run_loop_execute();
    return 0;
}
