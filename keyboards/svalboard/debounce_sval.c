// Copyright 2026 Morgan Venable
// SPDX-License-Identifier: GPL-2.0-or-later

// Svalboard debounce (DEBOUNCE_TYPE = custom, rules.mk).
//
// Every key but one is an optical interrupter, read once per frame while its
// row is lit. The exception is the thumb Double Down key, a mechanical tact
// switch: local row 0 (the thumb row) in column DOUBLEDOWN_COL. The finger
// rows use the same column for their optical Double South keys, which count
// as optical keys.
//
// Optical keys have a saved mode (sval_optical_debounce_on(), Scan Lab's
// "Debounce (optical keys)"):
//   on   (the default) QMK's sym_defer_g, reproduced on the optical keys: once
//        no optical key on this half has changed for SVAL_OPTICAL_DEBOUNCE_MS,
//        their raw state is reported, all at once. Any optical change restarts
//        the wait. Release firmware types this way.
//   off  no debounce: a raw change reaches the debounced matrix on the scan
//        that sees it.
//
// Double Down is debounced in both modes, per key and deferred: a change is
// reported once its raw level has held for SVAL_DD_DEBOUNCE_MS, and any raw
// change restarts the wait. Its bounce never delays an optical key, and an
// optical change never delays it.
//
// Each half debounces its own rows (matrix_scan() passes matrix + thisHand),
// so the one local position covers both thumbs: left [0,5] and right [5,5].
// The half without USB takes the mode from the other half's presence ping
// (svalboard.c, sval_debounce_sync_mode()).

#include "debounce_sval.h"
#ifdef SVAL_DEBOUNCE_HOST_TEST
#    include "debounce_sval_host.h" // tests/sval_debounce
#else
#    include "debounce.h"
#    include "timer.h"
#endif

#ifndef SVAL_DD_DEBOUNCE_MS
#    define SVAL_DD_DEBOUNCE_MS 5
#endif
#if SVAL_DD_DEBOUNCE_MS < 1 || SVAL_DD_DEBOUNCE_MS > 250
#    error "SVAL_DD_DEBOUNCE_MS must be 1 to 250"
#endif
// The optical keys' "on" mode: QMK's DEBOUNCE default, which release firmware uses.
#ifndef SVAL_OPTICAL_DEBOUNCE_MS
#    define SVAL_OPTICAL_DEBOUNCE_MS 5
#endif
#if SVAL_OPTICAL_DEBOUNCE_MS < 1 || SVAL_OPTICAL_DEBOUNCE_MS > 250
#    error "SVAL_OPTICAL_DEBOUNCE_MS must be 1 to 250"
#endif

#define SVAL_DD_ROW 0 // the thumb row, local to each half
#define SVAL_DD_BIT ((matrix_row_t)1 << DOUBLEDOWN_COL)

// The optical keys of row r: every bit but Double Down's.
static inline matrix_row_t optical_mask(uint8_t r) {
    return (r == SVAL_DD_ROW) ? (matrix_row_t)~SVAL_DD_BIT : (matrix_row_t)~0;
}

static bool         dd_waiting;  // raw Double Down differs from debounced and is being timed
static uint16_t     dd_since;    // when raw last moved away from debounced, in ms
static int8_t       opt_mode;    // the mode the last pass used: 1 on, 0 off, -1 none yet
static bool         opt_waiting; // on: an optical key changed; waiting for quiet
static uint16_t     opt_since;   // on: when the optical keys last changed, in ms
static matrix_row_t opt_last[MATRIX_ROWS_PER_HAND]; // on: optical raw at the last pass

void debounce_init(void) {
    dd_waiting  = false;
    dd_since    = 0;
    opt_mode    = -1;
    opt_waiting = false;
    opt_since   = 0;
    for (uint8_t r = 0; r < MATRIX_ROWS_PER_HAND; r++) opt_last[r] = 0;
}

uint8_t sval_debounce_sync_byte(bool optical_on) {
    return SVAL_DEBOUNCE_SYNC_VALID | (optical_on ? SVAL_DEBOUNCE_SYNC_ON : 0);
}

bool sval_debounce_sync_mode(bool present, uint8_t sync_byte, bool current_on) {
    if (!present || !(sync_byte & SVAL_DEBOUNCE_SYNC_VALID)) return current_on;
    return (sync_byte & SVAL_DEBOUNCE_SYNC_ON) != 0;
}

// Copy the optical bits of raw into cooked; true when that changed cooked.
static bool optical_copy(matrix_row_t raw[], matrix_row_t cooked[]) {
    bool changed = false;
    for (uint8_t r = 0; r < MATRIX_ROWS_PER_HAND; r++) {
        matrix_row_t m    = optical_mask(r);
        matrix_row_t next = (cooked[r] & (matrix_row_t)~m) | (raw[r] & m);
        if (next != cooked[r]) {
            cooked[r] = next;
            changed   = true;
        }
    }
    return changed;
}

// Optical keys, mode on: sym_defer_g over the optical bits.
static bool optical_deferred(matrix_row_t raw[], matrix_row_t cooked[], uint16_t now_ms) {
    if (opt_mode != 1) {
        // Just switched on (or the first pass): start from the raw state now.
        // A key that differs from the debounced state (it changed on this
        // pass) starts the wait, as a change does.
        opt_waiting = false;
        for (uint8_t r = 0; r < MATRIX_ROWS_PER_HAND; r++) {
            opt_last[r] = raw[r] & optical_mask(r);
            if (opt_last[r] != (cooked[r] & optical_mask(r))) opt_waiting = true;
        }
        opt_since = now_ms;
        return false;
    }
    bool moved = false;
    for (uint8_t r = 0; r < MATRIX_ROWS_PER_HAND; r++) {
        matrix_row_t now = raw[r] & optical_mask(r);
        if (now != opt_last[r]) {
            opt_last[r] = now;
            moved       = true;
        }
    }
    if (moved) {
        opt_waiting = true;
        opt_since   = now_ms;
        return false;
    }
    if (opt_waiting && (uint16_t)(now_ms - opt_since) >= SVAL_OPTICAL_DEBOUNCE_MS) {
        opt_waiting = false;
        return optical_copy(raw, cooked);
    }
    return false;
}

// Double Down: deferred, per key, in both modes.
static bool double_down(matrix_row_t raw[], matrix_row_t cooked[], uint16_t now_ms) {
    bool raw_dd    = (raw[SVAL_DD_ROW] & SVAL_DD_BIT) != 0;
    bool cooked_dd = (cooked[SVAL_DD_ROW] & SVAL_DD_BIT) != 0;
    if (raw_dd == cooked_dd) {
        dd_waiting = false; // bounced back: the next change starts a new wait
        return false;
    }
    if (!dd_waiting) {
        dd_waiting = true;
        dd_since   = now_ms;
    }
    if ((uint16_t)(now_ms - dd_since) >= SVAL_DD_DEBOUNCE_MS) {
        cooked[SVAL_DD_ROW] ^= SVAL_DD_BIT;
        dd_waiting = false;
        return true;
    }
    return false;
}

// One pass at time now_ms. Split out of debounce() for the host tests.
bool sval_debounce_step(matrix_row_t raw[], matrix_row_t cooked[], uint16_t now_ms) {
    bool on      = sval_optical_debounce_on();
    bool changed = false;
    if (on) {
        changed = optical_deferred(raw, cooked, now_ms);
    } else {
        opt_waiting = false;
        changed     = optical_copy(raw, cooked);
    }
    opt_mode = on ? 1 : 0;
    if (double_down(raw, cooked, now_ms)) changed = true;
    return changed;
}

// matrix_scan() calls this on every pass, changed or not, so both waits also
// end on passes without a new frame.
bool debounce(matrix_row_t raw[], matrix_row_t cooked[], bool changed) {
    (void)changed;
#ifdef SVAL_DEBOUNCE_HOST_TEST
    return sval_debounce_step(raw, cooked, host_now_ms());
#else
    return sval_debounce_step(raw, cooked, timer_read());
#endif
}
