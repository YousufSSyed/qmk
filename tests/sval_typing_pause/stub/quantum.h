// Mock QMK surface for tests/sval_typing_pause/test_typing_pause.c.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define MATRIX_ROWS 12
#define MATRIX_COLS 6
#define SVAL_HOST_ACTIVE_MS 1500
#define SVAL_EITHER_HAND // side.c owns pre_process_record_kb
#define NKRO_ENABLE
#define ONESHOT_PRESSED 1

typedef uint8_t matrix_row_t;

typedef struct {
    uint8_t col, row;
} keypos_t;
enum { TICK_EVENT = 0, KEY_EVENT = 1 };
typedef struct {
    keypos_t key;
    uint16_t time;
    uint8_t  type;
    bool     pressed;
} keyevent_t;
typedef struct {
    keyevent_t event;
} keyrecord_t;
#define IS_KEYEVENT(e) ((e).type == KEY_EVENT)
#define MAKE_KEYEVENT(r, c, p) ((keyevent_t){.key = {.col = (c), .row = (r)}, .pressed = (p), .time = 1, .type = KEY_EVENT})

typedef struct {
    uint8_t report_id, mods, reserved, keys[6];
} report_keyboard_t;
typedef struct {
    uint8_t report_id, mods, bits[30];
} report_nkro_t;
typedef struct {
    uint8_t report_id, buttons;
    int16_t x, y, v, h;
} report_mouse_t;
typedef struct {
    uint8_t  report_id;
    uint16_t usage;
} report_extra_t;
typedef struct {
    uint8_t (*keyboard_leds)(void);
    void (*send_keyboard)(report_keyboard_t *);
    void (*send_nkro)(report_nkro_t *);
    void (*send_mouse)(report_mouse_t *);
    void (*send_extra)(report_extra_t *);
} host_driver_t;
typedef struct {
    bool nkro;
} keymap_config_t;

enum { USB_STOP = 1, USB_READY = 2, USB_SELECTED = 3, USB_ACTIVE = 4, USB_SUSPENDED = 5 };
typedef struct {
    int state;
} usb_driver_t;

extern usb_driver_t    USB_DRIVER;
extern keymap_config_t keymap_config;

host_driver_t *host_get_driver(void);
void           host_set_driver(host_driver_t *d);
void           host_keyboard_send(report_keyboard_t *r);
void           host_nkro_send(report_nkro_t *r);
bool           host_can_send_nkro(void);
void           clear_keyboard(void);
void           clear_oneshot_mods(void);
void           clear_oneshot_layer_state(int state);
uint32_t       timer_read32(void);
uint32_t       timer_elapsed32(uint32_t t);
bool           is_keyboard_master(void);
bool           client_wrapper_valid_id(uint32_t id);
matrix_row_t   matrix_get_row(uint8_t row);
void           action_exec(keyevent_t e);
