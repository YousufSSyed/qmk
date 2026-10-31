// Copyright 2026 Morgan Venable
// SPDX-License-Identifier: GPL-2.0-or-later

// Key scan timing presets and the auto mouse layer timeout (sval_timing.h).
#include "sval_timing.h"

const uint16_t sval_turbo_us[SVAL_TURBO_CHOICES]             = {90, 60, 45, 30, 25, 20, 15};
const int16_t  mh_timer_choices[SVAL_AUTOMOUSE_CHOICES]      = {200, 300, 400, 500, 800, -1}; // -1: no timeout

uint8_t sval_timing_preset(uint16_t pre, uint16_t post) {
    if (pre != post) return SVAL_TIMING_CUSTOM;
    for (uint8_t i = 0; i < SVAL_TURBO_CHOICES; i++)
        if (sval_turbo_us[i] == pre) return i;
    return SVAL_TIMING_CUSTOM;
}

bool sval_timing_set_preset(uint8_t idx, uint16_t *pre, uint16_t *post) {
    if (idx >= SVAL_TURBO_CHOICES) return false;
    *pre  = sval_turbo_us[idx];
    *post = sval_turbo_us[idx];
    return true;
}

void sval_timing_step(uint16_t *pre, uint16_t *post) {
    uint16_t slower = *pre > *post ? *pre : *post;
    uint8_t  next   = 0; // past the fastest preset: back to the first
    for (uint8_t i = 0; i < SVAL_TURBO_CHOICES; i++) {
        if (sval_turbo_us[i] < slower) {
            next = i;
            break;
        }
    }
    sval_timing_set_preset(next, pre, post);
}

uint8_t sval_timing_ping_turbo(uint16_t pre, uint16_t post) {
    uint8_t p = sval_timing_preset(pre, post);
    return p == SVAL_TIMING_CUSTOM ? 0 : p;
}

uint16_t sval_timing_written_us(uint16_t us) {
    return us ? us : sval_turbo_us[0];
}

void sval_timing_effective(uint16_t pre, uint16_t post, uint8_t ping_turbo, uint16_t *eff_pre, uint16_t *eff_post) {
    uint8_t t = ping_turbo < SVAL_TURBO_CHOICES ? ping_turbo : 0;
    *eff_pre  = pre ? pre : sval_turbo_us[t];
    *eff_post = post ? post : sval_turbo_us[t];
}

static bool automouse_units_valid(uint8_t units10) {
    return units10 >= SVAL_AUTOMOUSE_MIN_MS / SVAL_AUTOMOUSE_STEP_MS && units10 <= SVAL_AUTOMOUSE_MAX_MS / SVAL_AUTOMOUSE_STEP_MS;
}

bool sval_timing_migrate(uint16_t *pre, uint16_t *post, uint8_t *legacy) {
    bool changed = false;
    if (*pre == 0 || *post == 0) {
        uint8_t t = *legacy < SVAL_TURBO_CHOICES ? *legacy : 0;
        if (*pre == 0) *pre = sval_turbo_us[t];
        if (*post == 0) *post = sval_turbo_us[t];
        changed = true;
    }
    // Below the shortest timeout the byte still holds a turbo index (0-6):
    // it is not an auto mouse timeout, so it goes back to "none set".
    if (*legacy != 0 && !automouse_units_valid(*legacy)) {
        *legacy = 0;
        changed = true;
    }
    return changed;
}

// ---- auto mouse layer timeout ----------------------------------------------

static uint8_t nearest_choice(uint16_t ms) {
    uint8_t  best      = 0;
    uint16_t best_diff = UINT16_MAX;
    for (uint8_t i = 0; i < SVAL_AUTOMOUSE_NEVER_INDEX; i++) {
        uint16_t c    = (uint16_t)mh_timer_choices[i];
        uint16_t diff = c > ms ? c - ms : ms - c;
        if (diff < best_diff) {
            best      = i;
            best_diff = diff;
        }
    }
    return best;
}

uint16_t sval_automouse_ms(uint8_t mh_index, uint8_t units10) {
    if (automouse_units_valid(units10)) return (uint16_t)units10 * SVAL_AUTOMOUSE_STEP_MS;
    if (mh_index < SVAL_AUTOMOUSE_NEVER_INDEX) return (uint16_t)mh_timer_choices[mh_index];
    return (uint16_t)mh_timer_choices[3]; // the default, 500 ms
}

int32_t sval_automouse_timeout(uint8_t mh_index, uint8_t units10) {
    if (mh_index == SVAL_AUTOMOUSE_NEVER_INDEX) return -1;
    return sval_automouse_ms(mh_index, units10);
}

void sval_automouse_set_ms(uint16_t ms, uint8_t *mh_index, uint8_t *units10) {
    if (ms < SVAL_AUTOMOUSE_MIN_MS) ms = SVAL_AUTOMOUSE_MIN_MS;
    if (ms > SVAL_AUTOMOUSE_MAX_MS) ms = SVAL_AUTOMOUSE_MAX_MS;
    *units10 = (uint8_t)((ms + SVAL_AUTOMOUSE_STEP_MS / 2) / SVAL_AUTOMOUSE_STEP_MS);
    if (*mh_index != SVAL_AUTOMOUSE_NEVER_INDEX) *mh_index = nearest_choice((uint16_t)*units10 * SVAL_AUTOMOUSE_STEP_MS);
}

void sval_automouse_set_never(bool never, uint8_t *mh_index, uint8_t *units10) {
    uint16_t ms = sval_automouse_ms(*mh_index, *units10);
    *units10    = (uint8_t)(ms / SVAL_AUTOMOUSE_STEP_MS);
    *mh_index   = never ? SVAL_AUTOMOUSE_NEVER_INDEX : nearest_choice(ms);
}

bool sval_automouse_set_choice(uint8_t idx, uint8_t *mh_index, uint8_t *units10) {
    if (idx >= SVAL_AUTOMOUSE_CHOICES) return false;
    *mh_index = idx;
    if (idx != SVAL_AUTOMOUSE_NEVER_INDEX) *units10 = (uint8_t)(mh_timer_choices[idx] / SVAL_AUTOMOUSE_STEP_MS);
    return true;
}

void sval_automouse_step(uint8_t *mh_index, uint8_t *units10) {
    uint8_t next = *mh_index + 1 < SVAL_AUTOMOUSE_CHOICES ? *mh_index + 1 : 0;
    sval_automouse_set_choice(next, mh_index, units10);
}
