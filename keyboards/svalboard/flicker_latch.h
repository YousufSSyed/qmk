// Copyright 2026 Morgan Venable
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Flicker latch (Sval 0x3D, docs/protocol.md "Flicker latch").
//
// Every half watches its raw switch rows, before debounce, on every scan:
// a switch that went down since the last read sets its pressed-since bit, one
// that went up sets its released-since bit, and each change counts (a
// saturating 8-bit count per switch). A host reads them and the read clears
// them, so a bounce that lasts a single scan, which debounce hides from the
// keymap and from VIA's matrix state, still shows in a key tester. Nothing
// here changes debounce or what gets typed.
//
// The half with USB answers for both halves: the other half's block comes over
// the split link (KEYBOARD_SYNC_B, first byte SVAL_FLICKER_RPC_OP).

#include <stdbool.h>
#include <stdint.h>
#include "matrix.h"

#define SVAL_FLICKER_RPC_OP 0xB7
#define SVAL_FLICKER_RPC_REPLY 32

enum sval_flicker_op {
    SVAL_FLICKER_READ   = 0, // raw rows and the latches, both halves; clears the latches
    SVAL_FLICKER_COUNTS = 1, // one half's change counts (arg: 0 left, 1 right); clears them
};

// matrix_scan_custom: this half's raw rows for this scan, before debounce.
void sval_flicker_frame(const matrix_row_t *raw, uint8_t rows);
// Sval 0x3D: data after [0xDF][0x3D], answered in place.
void sval_flicker_command(uint8_t *data, uint8_t length);
// KEYBOARD_SYNC_B on the half without USB: true if the request was ours.
bool sval_flicker_split_rpc(uint8_t in_len, const uint8_t *in, uint8_t out_len, uint8_t *out);
