// Mock QMK surface for tests/sval_flicker_latch/test_flicker_latch.c.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define MATRIX_ROWS 10
#define MATRIX_COLS 6
#define KEYBOARD_SYNC_B 1
typedef uint8_t matrix_row_t;
bool is_keyboard_master(void);
bool is_keyboard_left(void);
bool is_transport_connected(void);
bool transaction_rpc_exec(int id, uint8_t in_len, const void *in, uint8_t out_len, void *out);
