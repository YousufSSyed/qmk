// Copyright 2026 Morgan Venable
// SPDX-License-Identifier: GPL-2.0-or-later

// Svalboard debounce (debounce_sval.c): the optical-key mode and its split sync.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// The optical keys' debounce mode, a saved setting (svalboard.c: VIA value
// id_optical_debounce, stored as global_saved_values.optical_debounce_off so
// that a board that never set it reads "on"). The thumb Double Down is
// debounced in both modes.
bool sval_optical_debounce_on(void);

// Split sync. The half with USB owns the setting and sends this byte in its
// presence ping (KEYBOARD_SYNC_A); the other half uses the mode it carries.
#define SVAL_DEBOUNCE_SYNC_VALID 0x80 // the byte carries a mode
#define SVAL_DEBOUNCE_SYNC_ON    0x01 // optical debounce on
uint8_t sval_debounce_sync_byte(bool optical_on);
// The mode the other half uses: the master's when its ping carries one
// (present: the ping was long enough to hold the byte), else its own.
bool sval_debounce_sync_mode(bool present, uint8_t sync_byte, bool current_on);
