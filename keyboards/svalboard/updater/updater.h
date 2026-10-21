// Copyright 2026 Morgan Venable
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// In-firmware updater: the session state machine (plan M1, docs/updater-plan.md).
// The host talks to it over VIA custom values on UPDATE_CHANNEL; every request
// is answered at once and all slow work (manifest check, erase, image verify,
// commit) runs one slice per updater_task() pass. The request and response
// layouts are documented at the top of updater.c.

#include <stdbool.h>
#include <stdint.h>
#include "update_manifest.h"

// The held approval of a pair update (D41) lasts this long after the relay to
// the other half reaches DONE. The host sends this half's ARM within 60 s.
#ifndef SVAL_UPDATE_PAIR_HOLD_MS
#    define SVAL_UPDATE_PAIR_HOLD_MS 120000
#endif
// SETTINGS_RESET (D42): this half resets this long after its reply.
#ifndef SVAL_UPDATE_SETTINGS_RESET_MS
#    define SVAL_UPDATE_SETTINGS_RESET_MS 100
#endif

#ifndef SVAL_FW_VERSION
#    define SVAL_FW_VERSION 0 // numeric release version: rules.mk sets it from updater/fw_version.txt (D17, D32)
#endif

// From housekeeping, every pass. Runs the timeouts, one slice of slow work and
// the LEDs.
void updater_task(void);

// VIA custom-value command on UPDATE_CHANNEL: data = [command][channel][op][value_data],
// answered in place over value_data.
void updater_via_command(uint8_t *data, uint8_t length);

// Any state but IDLE: the keyboard keeps full clock and no idle dimming, and
// skips its identity task.
bool           updater_active(void);
update_state_t updater_state(void);
