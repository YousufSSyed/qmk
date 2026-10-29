// Copyright 2026 Morgan Venable
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Typing pause (Sval 0x3C, docs/protocol.md "Typing pause").
//
// While a host app's key tester is open, the board sends no keyboard output
// to the computer: no keyboard or NKRO reports, and no consumer or system
// reports (media, volume, sleep). The keymap keeps running (layers work), the
// mouse report still goes out (buttons, motion, scroll), and VIA's matrix
// state still reports every switch.
//
// The filter sits at the host driver: send_keyboard, send_nkro and send_extra
// are wrapped, send_mouse and the rest pass through. Every keyboard,
// consumer and system report QMK sends goes through those three, whatever
// produced it (keymap, macros, tap dances, combos, leader, auto shift), so
// nothing can slip past; the keymap itself is not touched.
//
// The pause belongs to the client (Sval client ID) that turned it on. Only
// that client's packets keep it alive; when it is quiet for
// SVAL_TYPING_PAUSE_LEASE_MS, the pause ends by itself. It also ends when USB
// leaves the active state (suspend, reset, unplug). The board never stays mute.
//
// Ending the pause: keys held at that moment are released inside the pause
// and their real release is swallowed later, so they never fire. Output stays
// blocked for SVAL_TYPING_PAUSE_DRAIN_MS so a pending tap, tap dance or combo
// resolves into the void; then an empty report goes to the host and the
// driver is restored.
//
// Only the half with USB pauses. The other half's keys arrive in its matrix
// over the split link and are reported by this half, so both halves pause.

#include <stdbool.h>
#include <stdint.h>
#include "action.h"

#ifndef SVAL_TYPING_PAUSE_LEASE_MS
#    define SVAL_TYPING_PAUSE_LEASE_MS SVAL_HOST_ACTIVE_MS
#endif
#ifndef SVAL_TYPING_PAUSE_DRAIN_MS
#    define SVAL_TYPING_PAUSE_DRAIN_MS 500
#endif

enum sval_typing_pause_op {
    SVAL_TYPING_PAUSE_OFF    = 0,
    SVAL_TYPING_PAUSE_ON     = 1, // also renews the lease; the sender becomes the owner
    SVAL_TYPING_PAUSE_STATUS = 2,
};

enum sval_typing_pause_status {
    SVAL_TYPING_PAUSE_OK       = 0,
    SVAL_TYPING_PAUSE_BAD_OP   = 1,
    SVAL_TYPING_PAUSE_REFUSED  = 2, // not the half with USB, USB not active, no host driver, or no client ID
    SVAL_TYPING_PAUSE_TOO_SHORT = 3,
};

enum sval_typing_pause_state {
    SVAL_TYPING_PAUSE_STATE_OFF       = 0,
    SVAL_TYPING_PAUSE_STATE_PAUSED    = 1,
    SVAL_TYPING_PAUSE_STATE_RELEASING = 2, // ending: output still blocked for the drain time
};

enum sval_typing_pause_end {
    SVAL_TYPING_PAUSE_END_NONE    = 0,
    SVAL_TYPING_PAUSE_END_COMMAND = 1,
    SVAL_TYPING_PAUSE_END_LEASE   = 2,
    SVAL_TYPING_PAUSE_END_USB     = 3,
};

// Sval 0x3C: data after [0xDF][0x3C], length bytes, answered in place.
void sval_typing_pause_command(uint8_t *data, uint8_t length);
// Every wrapped packet's client ID, before it is dispatched.
void sval_typing_pause_client_packet(uint32_t client_id);
// housekeeping_task_kb (half with USB).
void sval_typing_pause_task(void);
// pre_process_record_kb: false swallows the event (a key held when a pause ended).
bool sval_typing_pause_pre_process(keyrecord_t *record);
bool sval_typing_pause_active(void);
