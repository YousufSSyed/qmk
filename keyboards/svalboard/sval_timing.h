// Copyright 2026 Morgan Venable
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Key scan timing and the auto mouse layer timeout: the rules, kept apart from
// svalboard.c so the host tests (tests/sval_timing) can run them.
//
// Scan timing. The two saved waits, before and after each row read, in
// microseconds, are the timing the board runs. There is no turbo setting any
// more: the old turbo table is a list of presets that fill in both waits.
//  - Migration: a board from before this stored 0 in a wait to mean "use the
//    turbo table". At boot, a 0 wait is filled in from the table with the
//    saved turbo index, once, and saved, so the board keeps the timing it ran.
//  - VIA id_turbo_scan (9), kept for older hosts: a write sets both waits from
//    the table; a read returns the preset both waits match, or
//    SVAL_TIMING_CUSTOM.
//  - The SV_TURBO_SCAN key steps both waits to the next faster preset.
//
// Auto mouse layer timeout. The saved byte that held the turbo index is free
// after the migration; it now holds the timeout in 10 ms units
// (SVAL_AUTOMOUSE_*_MS). mh_timer_index, the old choice, stays as it was and
// is kept at the nearest old choice, so older firmware on the same settings
// still reads a valid one.

#include <stdbool.h>
#include <stdint.h>

#define SVAL_TURBO_CHOICES 7
#define SVAL_TIMING_CUSTOM 0xFF // id_turbo_scan read: the waits match no preset

extern const uint16_t sval_turbo_us[SVAL_TURBO_CHOICES]; // 90, 60, 45, 30, 25, 20, 15

// The preset both waits match, or SVAL_TIMING_CUSTOM.
uint8_t sval_timing_preset(uint16_t pre, uint16_t post);
// id_turbo_scan write: both waits from preset idx. False (nothing changed) when idx is out of range.
bool sval_timing_set_preset(uint8_t idx, uint16_t *pre, uint16_t *post);
// SV_TURBO_SCAN: both waits to the first preset faster than the slower wait
// now; from the fastest preset (or faster), back to the first.
void sval_timing_step(uint16_t *pre, uint16_t *post);
// A wait written over VIA (13, 14). 0, which older hosts write to mean "the
// turbo table", stores the first preset, which is what 0 ran on a board on
// turbo 0. Every other value is stored as it is.
uint16_t sval_timing_written_us(uint16_t us);
// The waits a frame uses, outside a Scan Lab sweep. A 0 wait comes only from
// the presence ping of an older half with USB; it takes the turbo index that
// ping carries, as before.
void sval_timing_effective(uint16_t pre, uint16_t post, uint8_t ping_turbo, uint16_t *eff_pre, uint16_t *eff_post);
// The turbo byte of the presence ping: the preset the waits match, or 0 for
// none (an older half without USB reads it only for a wait of 0, never sent now).
uint8_t sval_timing_ping_turbo(uint16_t pre, uint16_t post);
// Boot migration over the stored values. legacy is the byte that held the
// turbo index (saved_values.automouse_10ms). A 0 wait is filled in from
// sval_turbo_us[legacy] (0 when legacy is no preset). A legacy byte that is
// not an auto mouse timeout (below SVAL_AUTOMOUSE_MIN_MS / 10) is cleared.
// True when anything changed, so the caller saves.
bool sval_timing_migrate(uint16_t *pre, uint16_t *post, uint8_t *legacy);

// ---- auto mouse layer timeout --------------------------------------------
#define SVAL_AUTOMOUSE_MIN_MS 100
#define SVAL_AUTOMOUSE_MAX_MS 2000
#define SVAL_AUTOMOUSE_STEP_MS 10
#define SVAL_AUTOMOUSE_NEVER_INDEX 5 // mh_timer_index: no timeout ("Infinite")
#define SVAL_AUTOMOUSE_CHOICES 6
extern const int16_t mh_timer_choices[SVAL_AUTOMOUSE_CHOICES]; // 200, 300, 400, 500, 800, -1 (never)

// The timeout in ms, or -1 for none.
int32_t sval_automouse_timeout(uint8_t mh_index, uint8_t units10);
// The timeout the board keeps when "no timeout" is off (VIA 49 read): always 100-2000 ms.
uint16_t sval_automouse_ms(uint8_t mh_index, uint8_t units10);
// VIA 49 write: clamped to 100-2000 ms and rounded to 10 ms. Keeps "no timeout" as it is.
void sval_automouse_set_ms(uint16_t ms, uint8_t *mh_index, uint8_t *units10);
// VIA 50 write: no timeout on or off. Off goes back to the ms value.
void sval_automouse_set_never(bool never, uint8_t *mh_index, uint8_t *units10);
// VIA 5 write (the old choice, 0-5) and the SV_MH_CHANGE_TIMEOUTS key: the choice
// as before; the ms value follows it. False when idx is out of range.
bool sval_automouse_set_choice(uint8_t idx, uint8_t *mh_index, uint8_t *units10);
void sval_automouse_step(uint8_t *mh_index, uint8_t *units10);
