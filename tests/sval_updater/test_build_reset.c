// Copyright 2026 Morgan Venable
// SPDX-License-Identifier: GPL-2.0-or-later

// Host test for the build reset record (keyboards/svalboard/build_reset.c): over
// any sequence of flashes, a different image resets stored settings exactly
// once, the identical image never does, firmware that never writes the record
// (Vial, vLaunch2 and older) counts as a different build, and a record that
// cannot be written resets again at the next boot without ever resetting twice
// in one session.

#include <stdio.h>
#include <string.h>
#include "build_reset.h"

static int checks, failures;
#define CHECK(c)                                                         \
    do {                                                                 \
        checks++;                                                        \
        if (!(c)) {                                                      \
            failures++;                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
        }                                                                \
    } while (0)

static build_reset_record_t sector; // the build reset sector
static bool                 available = true, writable = true;
static int                  resets;
static bool                 notice, record_failed;

static void erase(void) {
    memset(&sector, 0xFF, sizeof(sector));
}

static bool write_record(uint32_t image, uint32_t flags) {
    if (!writable) return false;
    build_reset_record_make(&sector, image, flags);
    return true;
}

// One boot of an image, as build_reset.c: pre-init decides, quantum_init
// resets, post-init records. Returns the resets during this boot.
static int boot(uint32_t image) {
    int  before   = resets;
    bool pending  = build_reset_due(available, &sector, image);
    notice        = !pending && build_reset_notice(&sector, image);
    record_failed = false;
    if (pending) {
        resets++;
        if (write_record(image, BUILD_RESET_NOTICE)) {
            notice = true;
        } else {
            record_failed = true;
        }
    }
    return resets - before;
}

static void host_clears(uint32_t image) {
    if (notice && write_record(image, 0)) notice = false;
}

// Flash an image, then boot it n times; returns the resets this caused.
static int flash(uint32_t image, int n) {
    int before = resets;
    while (n--)
        boot(image);
    return resets - before;
}

// What a Vial or older Svalboard QMK build does to the sector: nothing, or (an
// erased chip) leaves it erased.
static void other_firmware_runs(void) {}

int main(void) {
    const uint8_t a[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    uint8_t       b[sizeof(a)];
    memcpy(b, a, sizeof(a));
    b[8] ^= 1;
    const uint32_t A = build_reset_hash(a, sizeof(a)), B = build_reset_hash(b, sizeof(b));
    CHECK(A != B);                                  // a change in the trailing partial word counts
    CHECK(A == build_reset_hash(a, sizeof(a)));     // the hash is stable
    CHECK(A != build_reset_hash(a, sizeof(a) - 1)); // and covers the length

    CHECK(!build_reset_matches(NULL, A));
    erase();
    CHECK(!build_reset_matches(&sector, 0xFFFFFFFFu)); // an erased sector holds no record

    // From Vial or vLaunch2 (no record yet): the first build resets once.
    CHECK(flash(A, 3) == 1);
    CHECK(notice); // the host is told...
    host_clears(A);
    CHECK(!notice);
    CHECK(flash(A, 2) == 0 && !notice); // ...once, and the same image again keeps settings
    CHECK(flash(B, 2) == 1);            // a different build: once
    CHECK(flash(B, 1) == 0 && notice);  // the notice waits for the host across boots
    host_clears(B);
    CHECK(flash(A, 1) == 1); // back to the earlier build: it is different from B
    other_firmware_runs();
    CHECK(flash(A, 1) == 0); // (vLaunch2 in between leaves A's record: see D39)

    // Garbage in the sector never counts as a record.
    build_reset_record_make(&sector, A, 0);
    sector.check ^= 1;
    CHECK(flash(A, 1) == 1);
    build_reset_record_make(&sector, A, 0);
    sector.magic = 0x52445653u; // a dev branch record ("SVDR") from before D39
    sector.check = ~(sector.magic ^ sector.image ^ sector.flags);
    CHECK(flash(A, 1) == 1);

    // A record that cannot be written: each boot resets once (never twice in a
    // session), the host is told the change could not be saved, and a boot
    // that can write it recovers for good.
    erase();
    writable = false;
    CHECK(boot(A) == 1 && record_failed && !notice);
    CHECK(boot(A) == 1 && record_failed);
    writable = true;
    CHECK(boot(A) == 1 && !record_failed && notice);
    CHECK(boot(A) == 0 && !record_failed);

    // SETTINGS_RESET (D42): one page programmed over the record, no erase.
    // Whatever the record held (valid with or without the notice, erased,
    // garbage), it reads all zeros afterwards, which is never a valid record:
    // the next boot of the same image resets once and records it again.
    uint8_t page[BUILD_RESET_PAGE_BYTES];
    build_reset_invalid_page(page, BUILD_RESET_TO_DEFAULTS);
    for (size_t i = 0; i < sizeof(page); i++)
        CHECK(page[i] == (i < BUILD_RESET_HEAD_BYTES ? 0x00 : 0xFF)); // the blank mark's bytes too
    for (int k = 0; k < 4; k++) {
        if (k == 0) build_reset_record_make(&sector, A, 0);
        if (k == 1) build_reset_record_make(&sector, A, BUILD_RESET_NOTICE);
        if (k == 2) erase();
        if (k == 3) memset(&sector, 0x5A, sizeof(sector));
        build_reset_program_over(&sector, page);
        CHECK(build_reset_cleared(&sector));
        CHECK(!build_reset_matches(&sector, A) && !build_reset_matches(&sector, 0));
        CHECK(flash(A, 3) == 1 && notice);
        CHECK(!build_reset_cleared(&sector));
        host_clears(A);
    }
    erase();
    CHECK(!build_reset_cleared(&sector)); // erased flash is not a SETTINGS_RESET
    build_reset_record_make(&sector, 0, 0);
    CHECK(!build_reset_cleared(&sector));

    // P4-29, SETTINGS_RESET mode BLANK (D44): the record cleared as for
    // defaults, and the blank mark after it, over an erased mark area.
    {
        uint8_t blank[BUILD_RESET_PAGE_BYTES], head[BUILD_RESET_HEAD_BYTES];
        build_reset_invalid_page(blank, BUILD_RESET_TO_BLANK);
        for (size_t i = 0; i < sizeof(build_reset_record_t); i++)
            CHECK(blank[i] == 0);
        for (size_t i = BUILD_RESET_HEAD_BYTES; i < sizeof(blank); i++)
            CHECK(blank[i] == 0xFF);
        CHECK(blank[16] == 'B' && blank[17] == 'L' && blank[18] == 'N' && blank[19] == 'K');
        // over every record a sector can hold (valid, with the notice, erased,
        // garbage), the mark area being erased as the record write leaves it
        for (int k = 0; k < 4; k++) {
            memset(head, 0xFF, sizeof(head));
            build_reset_record_t rec;
            if (k == 0) build_reset_record_make(&rec, A, 0);
            if (k == 1) build_reset_record_make(&rec, A, BUILD_RESET_NOTICE);
            if (k == 2) memset(&rec, 0xFF, sizeof(rec));
            if (k == 3) memset(&rec, 0x5A, sizeof(rec));
            memcpy(head, &rec, sizeof(rec));
            CHECK(!build_reset_blank_requested(head));
            build_reset_program_head(head, blank);
            CHECK(build_reset_blank_requested(head));
            memcpy(&rec, head, sizeof(rec));
            CHECK(build_reset_cleared(&rec) && !build_reset_matches(&rec, A)); // the next boot resets, then blanks
        }
        // defaults over a blank mark (a blank reset whose boot never wrote the
        // record): the mark is gone, so the reset is to defaults
        build_reset_program_head(head, page);
        CHECK(!build_reset_blank_requested(head));
        // a blank mark programmed over a zeroed mark area reads wrong: never blank
        // (build_reset_invalidate() then reports the failure)
        build_reset_program_head(head, blank);
        CHECK(!build_reset_blank_requested(head));
        // a mark over a valid record (no SETTINGS_RESET) never counts
        memset(head, 0xFF, sizeof(head));
        build_reset_record_t rec;
        build_reset_record_make(&rec, A, 0);
        memcpy(head, &rec, sizeof(rec));
        memcpy(head + sizeof(rec), blank + sizeof(rec), 8);
        CHECK(!build_reset_blank_requested(head));
        // half a mark is no mark
        memset(head, 0, sizeof(head));
        memcpy(head + sizeof(rec), blank + sizeof(rec), 4);
        CHECK(!build_reset_blank_requested(head));
        memcpy(head + sizeof(rec), blank + sizeof(rec), 8);
        CHECK(build_reset_blank_requested(head));
    }

    // No sector (a die smaller than 16 MB): nothing to compare, settings kept.
    erase();
    available = false;
    CHECK(flash(A, 2) == 0 && flash(B, 2) == 0 && !notice);
    available = true;

    printf("build_reset: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
