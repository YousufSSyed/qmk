// Host tests for keyboards/svalboard/sval_timing.c. Run by util/updater_test/run.sh.
//
// Scan timing: the boot migration from every turbo index, explicit waits kept,
// the old id_turbo_scan read and write, the SV_TURBO_SCAN key, a 0 written by
// an older host, and the split sync between new and older halves.
// Auto mouse layer timeout: 100-2000 ms, no timeout, the old choice kept in
// step for older firmware, and the old byte it reuses.
#include "sval_timing.h"
#include <stdio.h>
#include <stdlib.h>

static int failures, checks;

#define CHECK(cond, ...)                                \
    do {                                                \
        checks++;                                       \
        if (!(cond)) {                                  \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
            failures++;                                 \
        }                                               \
    } while (0)

static const uint16_t table[SVAL_TURBO_CHOICES] = {90, 60, 45, 30, 25, 20, 15};

// What firmware from before this ran for a stored turbo index and waits
// (matrix.c sval_scan_timing before feat/timing-us-only).
static void old_firmware_timing(uint8_t turbo, uint16_t pre, uint16_t post, uint16_t *eff_pre, uint16_t *eff_post) {
    uint8_t t = turbo >= SVAL_TURBO_CHOICES ? 0 : turbo;
    *eff_pre  = pre ? pre : table[t];
    *eff_post = post ? post : table[t];
}

static void test_table(void) {
    for (int i = 0; i < SVAL_TURBO_CHOICES; i++)
        CHECK(sval_turbo_us[i] == table[i], "table[%d] = %u", i, sval_turbo_us[i]);
}

// Every board keeps exactly the timing it ran: each turbo index, with both,
// one or neither wait set, and the byte that held the index is freed.
static void test_migration(void) {
    const uint16_t waits[] = {0, 1, 5, 45, 90, 200, 1000};
    for (int turbo = 0; turbo < 10; turbo++) {
        for (unsigned a = 0; a < sizeof(waits) / sizeof(waits[0]); a++) {
            for (unsigned b = 0; b < sizeof(waits) / sizeof(waits[0]); b++) {
                uint16_t pre = waits[a], post = waits[b], want_pre, want_post;
                uint8_t  legacy = (uint8_t)turbo;
                old_firmware_timing((uint8_t)turbo, pre, post, &want_pre, &want_post);
                bool changed = sval_timing_migrate(&pre, &post, &legacy);
                CHECK(pre == want_pre && post == want_post, "turbo %d, %u/%u: migrated to %u/%u, ran %u/%u", turbo, waits[a], waits[b], pre, post, want_pre, want_post);
                CHECK(pre != 0 && post != 0, "a wait is still 0");
                CHECK(legacy == 0, "turbo %d: the old byte reads %u", turbo, legacy);
                bool want_change = waits[a] == 0 || waits[b] == 0 || turbo != 0;
                CHECK(changed == want_change, "turbo %d, %u/%u: changed %d", turbo, waits[a], waits[b], changed);
                // Once is enough: a second boot changes nothing.
                CHECK(!sval_timing_migrate(&pre, &post, &legacy), "a second migration changed something");
            }
        }
    }
    // The cases on the boards: turbo 6 with 0/0 runs 15/15; turbo 0 with 0/0, 90/90;
    // revision B 45/5 stays 45/5; an explicit pre with post 0 keeps the mix.
    uint16_t pre = 0, post = 0;
    uint8_t  legacy = 6;
    sval_timing_migrate(&pre, &post, &legacy);
    CHECK(pre == 15 && post == 15, "turbo 6, 0/0 -> %u/%u", pre, post);
    pre = 0, post = 0, legacy = 0;
    CHECK(sval_timing_migrate(&pre, &post, &legacy) && pre == 90 && post == 90, "turbo 0, 0/0 -> %u/%u", pre, post);
    pre = 45, post = 5, legacy = 0;
    CHECK(!sval_timing_migrate(&pre, &post, &legacy) && pre == 45 && post == 5, "revision B 45/5 -> %u/%u", pre, post);
    pre = 200, post = 0, legacy = 6;
    sval_timing_migrate(&pre, &post, &legacy);
    CHECK(pre == 200 && post == 15, "turbo 6, 200/0 -> %u/%u", pre, post);
    // An auto mouse timeout already in the byte (after the migration) stays.
    pre = 90, post = 90, legacy = 120;
    CHECK(!sval_timing_migrate(&pre, &post, &legacy) && legacy == 120, "a timeout in the byte was changed to %u", legacy);
    // A board taken back to older firmware that stored a 0 wait there: the
    // byte is no turbo index, so the wait takes the first preset.
    pre = 0, post = 30, legacy = 120;
    sval_timing_migrate(&pre, &post, &legacy);
    CHECK(pre == 90 && post == 30 && legacy == 120, "0/30 with a timeout in the byte -> %u/%u, byte %u", pre, post, legacy);
}

static void test_turbo_value(void) {
    for (uint8_t i = 0; i < SVAL_TURBO_CHOICES; i++) {
        uint16_t pre = 200, post = 120;
        CHECK(sval_timing_set_preset(i, &pre, &post) && pre == table[i] && post == table[i], "write %u -> %u/%u", i, pre, post);
        CHECK(sval_timing_preset(pre, post) == i, "read after write %u: %u", i, sval_timing_preset(pre, post));
    }
    uint16_t pre = 200, post = 120;
    CHECK(!sval_timing_set_preset(7, &pre, &post) && pre == 200 && post == 120, "write 7 changed the waits");
    CHECK(!sval_timing_set_preset(0xFF, &pre, &post) && pre == 200 && post == 120, "write 0xFF changed the waits");
    CHECK(sval_timing_preset(200, 120) == SVAL_TIMING_CUSTOM, "200/120 reads a preset");
    CHECK(sval_timing_preset(45, 5) == SVAL_TIMING_CUSTOM, "45/5 reads a preset");
    CHECK(sval_timing_preset(45, 45) == 2, "45/45 reads %u", sval_timing_preset(45, 45));
    CHECK(sval_timing_preset(15, 90) == SVAL_TIMING_CUSTOM, "15/90 reads a preset");
    CHECK(sval_timing_preset(0, 0) == SVAL_TIMING_CUSTOM, "0/0 reads a preset");
    // A write of 0 (older hosts' "back to the turbo table") stores the first preset.
    CHECK(sval_timing_written_us(0) == 90, "0 -> %u", sval_timing_written_us(0));
    CHECK(sval_timing_written_us(1) == 1 && sval_timing_written_us(200) == 200 && sval_timing_written_us(65535) == 65535, "a value was changed");
}

static void test_step(void) {
    uint16_t pre = 90, post = 90;
    for (int i = 1; i < SVAL_TURBO_CHOICES; i++) {
        sval_timing_step(&pre, &post);
        CHECK(pre == table[i] && post == table[i], "step %d -> %u/%u", i, pre, post);
    }
    sval_timing_step(&pre, &post);
    CHECK(pre == 90 && post == 90, "after the fastest -> %u/%u", pre, post);
    pre = 45, post = 5; // revision B: the slower wait, 45, steps to 30
    sval_timing_step(&pre, &post);
    CHECK(pre == 30 && post == 30, "45/5 -> %u/%u", pre, post);
    pre = 200, post = 120;
    sval_timing_step(&pre, &post);
    CHECK(pre == 90 && post == 90, "200/120 -> %u/%u", pre, post);
    pre = 8, post = 8;
    sval_timing_step(&pre, &post);
    CHECK(pre == 90 && post == 90, "8/8 -> %u/%u", pre, post);
    pre = 50, post = 20;
    sval_timing_step(&pre, &post);
    CHECK(pre == 45 && post == 45, "50/20 -> %u/%u", pre, post);
}

// The presence ping carries the turbo byte, pre and post; the half without
// USB uses them in RAM. New and older firmware on either side.
static void test_split_sync(void) {
    const uint16_t cases[][2] = {{90, 90}, {15, 15}, {45, 5}, {200, 120}, {1, 1000}, {45, 45}};
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint16_t pre = cases[i][0], post = cases[i][1];
        uint8_t  ping_turbo = sval_timing_ping_turbo(pre, post);
        CHECK(ping_turbo < SVAL_TURBO_CHOICES, "%u/%u: ping turbo %u is no preset", pre, post, ping_turbo);
        uint16_t p, q;
        // New half with USB, new half without.
        sval_timing_effective(pre, post, ping_turbo, &p, &q);
        CHECK(p == pre && q == post, "new -> new: %u/%u ran %u/%u", pre, post, p, q);
        // New half with USB, older half without: it runs the waits too.
        old_firmware_timing(ping_turbo, pre, post, &p, &q);
        CHECK(p == pre && q == post, "new -> older: %u/%u ran %u/%u", pre, post, p, q);
    }
    CHECK(sval_timing_ping_turbo(15, 15) == 6, "15/15 pings %u", sval_timing_ping_turbo(15, 15));
    CHECK(sval_timing_ping_turbo(200, 120) == 0, "custom pings %u", sval_timing_ping_turbo(200, 120));
    // Older half with USB, new half without: 0 waits take the ping's turbo index, as before.
    for (uint8_t t = 0; t < 10; t++) {
        uint16_t p, q, wp, wq;
        sval_timing_effective(0, 0, t, &p, &q);
        old_firmware_timing(t, 0, 0, &wp, &wq);
        CHECK(p == wp && q == wq, "older -> new, turbo %u, 0/0: %u/%u, older ran %u/%u", t, p, q, wp, wq);
        sval_timing_effective(200, 0, t, &p, &q);
        old_firmware_timing(t, 200, 0, &wp, &wq);
        CHECK(p == wp && q == wq, "older -> new, turbo %u, 200/0: %u/%u", t, p, q);
    }
}

static void test_automouse(void) {
    // Defaults and every old choice, read as before.
    for (uint8_t i = 0; i < 5; i++) {
        uint8_t mh = 0, u = 0;
        CHECK(sval_automouse_set_choice(i, &mh, &u), "choice %u refused", i);
        CHECK(sval_automouse_timeout(mh, u) == mh_timer_choices[i], "choice %u: %ld ms", i, (long)sval_automouse_timeout(mh, u));
        CHECK(sval_automouse_ms(mh, u) == (uint16_t)mh_timer_choices[i], "choice %u: VIA 49 reads %u", i, sval_automouse_ms(mh, u));
        // The settings of a board from before this: the byte is 0 (or a turbo index, cleared at boot).
        CHECK(sval_automouse_timeout(i, 0) == mh_timer_choices[i], "old settings, choice %u: %ld", i, (long)sval_automouse_timeout(i, 0));
    }
    uint8_t mh = 3, u = 50;
    CHECK(sval_automouse_timeout(mh, u) == 500, "default: %ld", (long)sval_automouse_timeout(mh, u));
    CHECK(sval_automouse_set_choice(5, &mh, &u) && sval_automouse_timeout(mh, u) == -1, "choice 5 is not no timeout");
    CHECK(!sval_automouse_set_choice(6, &mh, &u) && mh == 5, "choice 6 accepted");
    CHECK(sval_automouse_timeout(5, 0) == -1, "old settings, Infinite: %ld", (long)sval_automouse_timeout(5, 0));

    // VIA 49: 100-2000 ms in 10 ms steps, clamped and rounded.
    struct {
        uint16_t in, out;
        uint8_t  choice;
    } ms_cases[] = {
        {0, 100, 0}, {50, 100, 0}, {99, 100, 0}, {100, 100, 0}, {104, 100, 0}, {105, 110, 0}, {250, 250, 0}, {251, 250, 0}, {640, 640, 3}, {1000, 1000, 4}, {1999, 2000, 4}, {2000, 2000, 4}, {2001, 2000, 4}, {2550, 2000, 4}, {65535, 2000, 4}, {500, 500, 3}, {360, 360, 2},
    };
    for (unsigned i = 0; i < sizeof(ms_cases) / sizeof(ms_cases[0]); i++) {
        mh = 3, u = 50;
        sval_automouse_set_ms(ms_cases[i].in, &mh, &u);
        CHECK(sval_automouse_ms(mh, u) == ms_cases[i].out, "write %u reads %u", ms_cases[i].in, sval_automouse_ms(mh, u));
        CHECK(sval_automouse_timeout(mh, u) == ms_cases[i].out, "write %u runs %ld", ms_cases[i].in, (long)sval_automouse_timeout(mh, u));
        // Older firmware on these settings reads the nearest old choice.
        CHECK(mh == ms_cases[i].choice, "write %u: old choice %u, want %u", ms_cases[i].in, mh, ms_cases[i].choice);
    }
    // Every value 0-2100 reads back clamped and on a 10 ms step.
    for (uint32_t ms = 0; ms <= 2100; ms++) {
        mh = 0, u = 0;
        sval_automouse_set_ms((uint16_t)ms, &mh, &u);
        uint16_t got = sval_automouse_ms(mh, u);
        CHECK(got >= 100 && got <= 2000 && got % 10 == 0, "write %lu reads %u", (unsigned long)ms, got);
        CHECK(mh < 5, "write %lu: old choice %u", (unsigned long)ms, mh);
    }

    // VIA 50, no timeout: on and off, and the ms value kept across it.
    mh = 3, u = 50;
    sval_automouse_set_ms(1500, &mh, &u);
    sval_automouse_set_never(true, &mh, &u);
    CHECK(sval_automouse_timeout(mh, u) == -1 && mh == 5, "no timeout on: %ld", (long)sval_automouse_timeout(mh, u));
    CHECK(sval_automouse_ms(mh, u) == 1500, "no timeout on: VIA 49 reads %u", sval_automouse_ms(mh, u));
    sval_automouse_set_ms(1200, &mh, &u); // a write of 49 while no timeout is on keeps it on
    CHECK(sval_automouse_timeout(mh, u) == -1 && sval_automouse_ms(mh, u) == 1200, "ms write turned no timeout off");
    sval_automouse_set_never(false, &mh, &u);
    CHECK(sval_automouse_timeout(mh, u) == 1200 && mh == 4, "no timeout off: %ld ms, choice %u", (long)sval_automouse_timeout(mh, u), mh);
    // Infinite from old settings (byte 0), then off: back to the default 500.
    mh = 5, u = 0;
    CHECK(sval_automouse_ms(mh, u) == 500, "Infinite, old settings: VIA 49 reads %u", sval_automouse_ms(mh, u));
    sval_automouse_set_never(false, &mh, &u);
    CHECK(sval_automouse_timeout(mh, u) == 500, "off after old Infinite: %ld", (long)sval_automouse_timeout(mh, u));
    // A garbage old choice reads the default.
    CHECK(sval_automouse_timeout(9, 0) == 500, "choice 9: %ld", (long)sval_automouse_timeout(9, 0));

    // SV_MH_CHANGE_TIMEOUTS steps through the old choices and wraps, from any value.
    mh = 0, u = 0;
    sval_automouse_set_ms(1500, &mh, &u);
    sval_automouse_step(&mh, &u); // 1500 keeps choice 4 (800): next is no timeout
    CHECK(mh == 5 && sval_automouse_timeout(mh, u) == -1, "step from 1500: choice %u", mh);
    sval_automouse_step(&mh, &u);
    CHECK(mh == 0 && sval_automouse_timeout(mh, u) == 200, "step wraps to %ld", (long)sval_automouse_timeout(mh, u));
    for (int i = 1; i < 5; i++) {
        sval_automouse_step(&mh, &u);
        CHECK(sval_automouse_timeout(mh, u) == mh_timer_choices[i], "step %d: %ld", i, (long)sval_automouse_timeout(mh, u));
    }

    // The byte it reuses after the boot migration: a turbo index there is cleared,
    // so it never reads as a 10-60 ms timeout.
    for (uint8_t t = 1; t < SVAL_TURBO_CHOICES; t++) {
        uint16_t pre = 90, post = 90;
        uint8_t  byte = t;
        sval_timing_migrate(&pre, &post, &byte);
        CHECK(sval_automouse_timeout(3, byte) == 500, "turbo %u left in the byte: %ld ms", t, (long)sval_automouse_timeout(3, byte));
    }
}

int main(void) {
    test_table();
    test_migration();
    test_turbo_value();
    test_step();
    test_split_sync();
    test_automouse();
    if (failures) {
        printf("sval_timing: %d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("sval_timing: %d checks passed\n", checks);
    return 0;
}
