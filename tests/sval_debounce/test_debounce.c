// Host tests for keyboards/svalboard/debounce_sval.c. Run by util/updater_test/run.sh.
//
// The firmware calls debounce() on every matrix_scan() pass; these tests call
// it once per simulated millisecond.
//
// Optical debounce off: every optical key on the same scan. On: QMK's
// sym_defer_g over the optical keys, checked against a copy of that
// algorithm. The thumb Double Down is debounced per key in both modes. Mode
// changes mid-bounce, and the split sync byte.
#include "debounce_sval_host.h"
#include "debounce_sval.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SVAL_DD_DEBOUNCE_MS
#    define SVAL_DD_DEBOUNCE_MS 5
#endif
#ifndef SVAL_OPTICAL_DEBOUNCE_MS
#    define SVAL_OPTICAL_DEBOUNCE_MS 5
#endif
#define DD_MS SVAL_DD_DEBOUNCE_MS
#define OPT_MS SVAL_OPTICAL_DEBOUNCE_MS
#define DD ((matrix_row_t)1 << DOUBLEDOWN_COL)

static uint16_t     now_ms;
static bool         mode_on; // the saved mode this half uses
static matrix_row_t raw[MATRIX_ROWS_PER_HAND], cooked[MATRIX_ROWS_PER_HAND];
static int          failures;

uint16_t host_now_ms(void) {
    return now_ms;
}

bool sval_optical_debounce_on(void) {
    return mode_on;
}

#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
            failures++;                                 \
        }                                               \
    } while (0)

static void reset_mode(uint16_t t, bool on) {
    now_ms  = t;
    mode_on = on;
    memset(raw, 0, sizeof(raw));
    memset(cooked, 0, sizeof(cooked));
    debounce_init();
}

static void reset(uint16_t t) {
    reset_mode(t, false);
}

static bool scan(void) {
    return debounce(raw, cooked, true);
}

static bool dd_cooked(void) {
    return (cooked[0] & DD) != 0;
}

// Scan once per ms until the debounced Double Down equals want: the ms taken, or -1.
static int run_until_dd(bool want, int max_ms) {
    for (int i = 0; i <= max_ms; i++) {
        scan();
        if (dd_cooked() == want) return i;
        now_ms++;
    }
    return -1;
}

// Scan once per ms until cooked row r has bit set to want: the ms taken, or -1.
static int run_until_bit(int r, matrix_row_t bit, bool want, int max_ms) {
    for (int i = 0; i <= max_ms; i++) {
        scan();
        if (((cooked[r] & bit) != 0) == want) return i;
        now_ms++;
    }
    return -1;
}

// ---------------------------------------------------------------- mode off ---

// Every optical key, finger rows' COL6 (Double South) included, changes on
// the same scan its raw bit flips, press and release, and so does a chord.
static void test_optical_same_scan(void) {
    reset(100);
    for (int r = 0; r < MATRIX_ROWS_PER_HAND; r++) {
        for (int c = 0; c < 6; c++) {
            if (r == 0 && c == DOUBLEDOWN_COL) continue;
            matrix_row_t bit = (matrix_row_t)1 << c;
            raw[r] |= bit;
            CHECK(scan(), "r%d c%d press reported", r, c);
            CHECK(cooked[r] & bit, "r%d c%d press on the same scan", r, c);
            now_ms++;
            raw[r] &= (matrix_row_t)~bit;
            CHECK(scan(), "r%d c%d release reported", r, c);
            CHECK(!(cooked[r] & bit), "r%d c%d release on the same scan", r, c);
            now_ms++;
        }
    }
    raw[1] = 0x3F;
    raw[4] = 0x15;
    CHECK(scan() && cooked[1] == 0x3F && cooked[4] == 0x15, "chord on the same scan");
    raw[1] = raw[4] = 0;
    CHECK(scan() && cooked[1] == 0 && cooked[4] == 0, "chord release on the same scan");
    CHECK(!scan(), "an unchanged scan reports nothing");
    // An optical key flipping every scan follows raw exactly: nothing is filtered.
    for (int i = 0; i < 10; i++) {
        raw[2] ^= 0x01;
        scan();
        CHECK(cooked[2] == raw[2], "optical flip %d followed", i);
        now_ms++;
    }
}

// ----------------------------------------------------------- Double Down ---

// A clean Double Down press and release each report after DD_MS.
static void test_dd_clean(bool on) {
    reset_mode(1000, on);
    raw[0] = DD;
    int t  = run_until_dd(true, 50);
    CHECK(t == DD_MS, "mode %d: clean press after %d ms (want %d)", on, t, DD_MS);
    raw[0] = 0;
    t      = run_until_dd(false, 50);
    CHECK(t == DD_MS, "mode %d: clean release after %d ms (want %d)", on, t, DD_MS);
}

// Feed one raw Double Down level per ms; count debounced changes.
static int feed_dd(const char *pattern, int *last_change_at) {
    int  changes = 0;
    bool last    = dd_cooked();
    for (int i = 0; pattern[i]; i++) {
        raw[0] = (raw[0] & (matrix_row_t)~DD) | (pattern[i] == '1' ? DD : 0);
        scan();
        if (dd_cooked() != last) {
            changes++;
            if (last_change_at) *last_change_at = i;
            last = dd_cooked();
        }
        now_ms++;
    }
    return changes;
}

// A bouncing press settles DD_MS after the last bounce, as one press; a
// bouncing release, as one release; glitches shorter than DD_MS never report.
static void test_dd_bounce(bool on) {
    reset_mode(2000, on);
    int at = -1;
    //                      bounces for 6 ms, then held
    int n = feed_dd("101101"
                    "1111111111",
                    &at);
    CHECK(n == 1, "mode %d: bouncing press: %d changes (want 1)", on, n);
    CHECK(dd_cooked(), "mode %d: bouncing press settles down", on);
    CHECK(at == 5 + DD_MS, "mode %d: bouncing press settled at %d ms (want %d)", on, at, 5 + DD_MS);

    n = feed_dd("010010"
                "0000000000",
                &at);
    CHECK(n == 1, "mode %d: bouncing release: %d changes (want 1)", on, n);
    CHECK(!dd_cooked(), "mode %d: bouncing release settles up", on);

    char glitch[DD_MS + 12];
    memset(glitch, '0', sizeof(glitch) - 1);
    glitch[sizeof(glitch) - 1] = 0;
    memset(glitch + 2, '1', DD_MS - 1); // held one ms short of DD_MS
    n = feed_dd(glitch, NULL);
    CHECK(n == 0, "mode %d: a %d ms glitch reported %d changes", on, DD_MS - 1, n);
}

// Mode off: while Double Down bounces, optical keys on every row, the thumb
// row included, still change on the same scan.
static void test_dd_bounce_does_not_delay_optical(void) {
    reset(3000);
    const char *bounce = "1010110101101010";
    for (int i = 0; bounce[i]; i++) {
        raw[0]           = (raw[0] & (matrix_row_t)~DD) | (bounce[i] == '1' ? DD : 0);
        int          r   = i % MATRIX_ROWS_PER_HAND;
        matrix_row_t bit = (matrix_row_t)1 << (i % DOUBLEDOWN_COL); // never the DD column
        raw[r] ^= bit;
        scan();
        CHECK((cooked[r] & bit) == (raw[r] & bit), "ms %d: optical r%d bit 0x%02x followed raw during bounce", i, r, bit);
        CHECK(!dd_cooked(), "ms %d: bouncing Double Down not yet reported", i);
        now_ms++;
    }
    // Then held down: it reports DD_MS later.
    raw[0] |= DD;
    int t = run_until_dd(true, 50);
    CHECK(t == DD_MS, "Double Down settled %d ms after the last bounce (want %d)", t, DD_MS);
}

// Mode on: a Double Down bounce never restarts the optical wait, and optical
// keys changing every ms never delay Double Down.
static void test_dd_and_optical_independent_when_on(void) {
    reset_mode(3500, true);
    raw[2]             = 0x01; // an optical press
    const char *bounce = "10101101";
    for (int i = 0; bounce[i]; i++) {
        raw[0] = (raw[0] & (matrix_row_t)~DD) | (bounce[i] == '1' ? DD : 0);
        scan();
        if (i == OPT_MS) CHECK(cooked[2] == 0x01, "optical press after %d ms despite the Double Down bounce", OPT_MS);
        now_ms++;
    }
    CHECK(cooked[2] == 0x01, "optical press reported during the Double Down bounce");
    CHECK(!dd_cooked(), "the bouncing Double Down not yet reported");

    reset_mode(3600, true);
    raw[0] = DD;
    for (int i = 0; i <= DD_MS; i++) {
        raw[3] ^= 0x02; // an optical key flickering every ms: its wait never ends
        scan();
        if (i < DD_MS) CHECK(!dd_cooked(), "ms %d: Double Down early", i);
        now_ms++;
    }
    CHECK(dd_cooked(), "Double Down after %d ms while optical keys keep changing", DD_MS);
    CHECK((cooked[3] & 0x02) == 0, "the flickering optical key never reported");
}

// The wait is right across the 16-bit ms timer wrap.
static void test_timer_wrap(bool on) {
    reset_mode((uint16_t)(65536 - 2), on);
    raw[0] = DD;
    int t  = run_until_dd(true, 50);
    CHECK(t == DD_MS, "mode %d: press across the timer wrap after %d ms (want %d)", on, t, DD_MS);
    if (on) {
        reset_mode((uint16_t)(65536 - 2), true);
        raw[1] = 0x04;
        t      = run_until_bit(1, 0x04, true, 50);
        CHECK(t == OPT_MS, "optical press across the timer wrap after %d ms (want %d)", t, OPT_MS);
    }
}

// --------------------------------------------------------------- mode on ---

// A clean optical press and release each report OPT_MS after the change.
static void test_on_clean(void) {
    reset_mode(5000, true);
    for (int r = 0; r < MATRIX_ROWS_PER_HAND; r++) {
        for (int c = 0; c < 6; c++) {
            if (r == 0 && c == DOUBLEDOWN_COL) continue;
            matrix_row_t bit = (matrix_row_t)1 << c;
            raw[r] |= bit;
            int t = run_until_bit(r, bit, true, 50);
            CHECK(t == OPT_MS, "r%d c%d press after %d ms (want %d)", r, c, t, OPT_MS);
            raw[r] &= (matrix_row_t)~bit;
            t = run_until_bit(r, bit, false, 50);
            CHECK(t == OPT_MS, "r%d c%d release after %d ms (want %d)", r, c, t, OPT_MS);
        }
    }
}

// The wait is for the whole half: a second key changing restarts it for both,
// and a bounce shorter than OPT_MS that returns to the debounced state never reports.
static void test_on_whole_half(void) {
    reset_mode(6000, true);
    raw[1] = 0x01;
    scan();
    now_ms += 3;
    scan();
    raw[4] = 0x08; // a key on another row, 3 ms later
    scan();
    CHECK(cooked[1] == 0 && cooked[4] == 0, "nothing yet");
    int t = run_until_bit(1, 0x01, true, 50);
    CHECK(t == OPT_MS, "first key waited %d ms after the second key's change (want %d)", t, OPT_MS);
    CHECK(cooked[4] == 0x08, "both keys reported together");

    reset_mode(6100, true);
    for (int i = 0; i < OPT_MS + 10; i++) {
        raw[2] = (i >= 2 && i < 2 + OPT_MS - 1) ? 0x10 : 0; // high one ms short of OPT_MS
        CHECK(!scan() && cooked[2] == 0, "ms %d: a short bounce reported", i);
        now_ms++;
    }
}

// sym_defer_g (quantum/debounce/sym_defer_g.c, DEBOUNCE 5), restricted to the
// optical bits: the release firmware's algorithm, to compare against.
typedef struct {
    bool         debouncing;
    uint16_t     since;
    matrix_row_t last_raw[MATRIX_ROWS_PER_HAND];
    matrix_row_t cooked[MATRIX_ROWS_PER_HAND];
} ref_t;

static void ref_step(ref_t *s, const matrix_row_t *r, uint16_t now) {
    bool changed = memcmp(s->last_raw, r, sizeof(s->last_raw)) != 0;
    if (changed) {
        memcpy(s->last_raw, r, sizeof(s->last_raw));
        s->debouncing = true;
        s->since      = now;
    } else if (s->debouncing && (uint16_t)(now - s->since) >= OPT_MS) {
        memcpy(s->cooked, r, sizeof(s->cooked));
        s->debouncing = false;
    }
}

// Mode on matches sym_defer_g on the optical keys, scan for scan, over random
// typing, chords, single-scan flickers, pauses and the timer wrap.
static void test_on_matches_sym_defer_g(void) {
    srand(12345);
    reset_mode((uint16_t)(65536 - 3000), true);
    ref_t ref;
    memset(&ref, 0, sizeof(ref));
    int mismatches = 0, changes = 0;
    for (int i = 0; i < 200000; i++) {
        int roll = rand() % 100;
        if (roll < 8) { // a key changes
            int r = rand() % MATRIX_ROWS_PER_HAND, c = rand() % 6;
            if (r == 0 && c == DOUBLEDOWN_COL) c = 0;
            raw[r] ^= (matrix_row_t)1 << c;
        } else if (roll < 9) { // a chord
            for (int k = 0; k < 3; k++)
                raw[1 + rand() % 4] ^= (matrix_row_t)1 << (rand() % 6);
        }
        matrix_row_t opt[MATRIX_ROWS_PER_HAND];
        for (int r = 0; r < MATRIX_ROWS_PER_HAND; r++)
            opt[r] = raw[r] & (r == 0 ? (matrix_row_t)~DD : (matrix_row_t)~0);
        matrix_row_t before[MATRIX_ROWS_PER_HAND];
        memcpy(before, cooked, sizeof(before));
        bool reported = scan();
        ref_step(&ref, opt, now_ms);
        if (reported != (memcmp(before, cooked, sizeof(before)) != 0)) mismatches++;
        if (reported) changes++;
        for (int r = 0; r < MATRIX_ROWS_PER_HAND; r++) {
            if (cooked[r] != ref.cooked[r]) {
                if (mismatches < 5) printf("  ms %d row %d: 0x%02x, sym_defer_g 0x%02x\n", i, r, cooked[r], ref.cooked[r]);
                mismatches++;
            }
        }
        now_ms++;
        if (rand() % 500 == 0) { // a pause in typing
            for (int k = 0; k < 20; k++) {
                scan();
                ref_step(&ref, opt, now_ms);
                now_ms++;
            }
        }
    }
    CHECK(mismatches == 0, "mode on differs from sym_defer_g on %d scans", mismatches);
    CHECK(changes > 1000, "only %d reported changes: the comparison saw too little", changes);
}

// ----------------------------------------------------------- mode changes ---

// On to off while an optical change waits: it is reported on the next scan.
// Off to on while a key bounces: the wait starts, as a change, and the key
// reports OPT_MS after its last change. Double Down keeps its own timing
// across both.
static void test_mode_change_mid_bounce(void) {
    reset_mode(7000, true);
    raw[2] = 0x04;
    scan();
    now_ms++;
    scan();
    CHECK(cooked[2] == 0, "on: still waiting");
    now_ms++;
    mode_on = false;
    CHECK(scan() && cooked[2] == 0x04, "switched off: the waiting press is reported on the next scan");
    now_ms++;
    raw[2] = 0;
    CHECK(scan() && cooked[2] == 0, "off: the release is on the same scan");

    reset_mode(7100, false);
    const char *bounce = "1010";
    for (int i = 0; bounce[i]; i++) {
        raw[3] = bounce[i] == '1' ? 0x02 : 0;
        scan();
        CHECK(cooked[3] == raw[3], "off: bounce %d followed", i);
        now_ms++;
    }
    mode_on = true; // switched on with the key down and still bouncing
    raw[3]  = 0x02;
    scan();
    CHECK(cooked[3] == 0, "switched on mid-bounce: the change waits");
    now_ms++;
    raw[3] = 0; // bounces back up
    scan();
    now_ms++;
    raw[3] = 0x02; // and down to stay
    int t  = run_until_bit(3, 0x02, true, 50);
    CHECK(t == OPT_MS, "on: settled %d ms after the last bounce (want %d)", t, OPT_MS);

    reset_mode(7200, false);
    raw[0] = DD;
    for (int i = 0; i < DD_MS; i++) {
        mode_on = (i % 2) == 0; // the mode flips on every scan of the Double Down wait
        scan();
        CHECK(!dd_cooked(), "ms %d: Double Down early across mode changes", i);
        now_ms++;
    }
    scan();
    CHECK(dd_cooked(), "Double Down after %d ms across mode changes", DD_MS);
}

// ------------------------------------------------------------- split sync ---

// The half with USB sends its mode in every presence ping; the other half uses
// it. A ping from older firmware, too short to carry the byte, or a byte
// without the valid bit, leaves the other half's own mode. Each half's
// debounce uses the synced mode on its next scan.
static void test_split_sync(void) {
    CHECK(sval_debounce_sync_byte(true) == (SVAL_DEBOUNCE_SYNC_VALID | SVAL_DEBOUNCE_SYNC_ON), "on byte");
    CHECK(sval_debounce_sync_byte(false) == SVAL_DEBOUNCE_SYNC_VALID, "off byte");
    for (int master = 0; master < 2; master++) {
        for (int own = 0; own < 2; own++) {
            uint8_t b = sval_debounce_sync_byte(master);
            CHECK(sval_debounce_sync_mode(true, b, own) == (bool)master, "master %d, other half %d: follows the master", master, own);
            CHECK(sval_debounce_sync_mode(false, b, own) == (bool)own, "master %d, other half %d: a short ping keeps its own", master, own);
            CHECK(sval_debounce_sync_mode(true, 0x00, own) == (bool)own, "other half %d: a zero byte keeps its own", own);
            CHECK(sval_debounce_sync_mode(true, SVAL_DEBOUNCE_SYNC_ON, own) == (bool)own, "other half %d: no valid bit keeps its own", own);
        }
    }

    // The other half saved "on" (the default) and the half with USB is set
    // off: after the first ping it types with no optical debounce.
    reset_mode(8000, true);
    raw[1] = 0x01;
    scan();
    CHECK(cooked[1] == 0, "before the ping: its own mode, on");
    mode_on = sval_debounce_sync_mode(true, sval_debounce_sync_byte(false), mode_on);
    now_ms++;
    CHECK(scan() && cooked[1] == 0x01, "after the ping: off, the press on the next scan");
    // The half with USB is switched back on: the other half follows.
    mode_on = sval_debounce_sync_mode(true, sval_debounce_sync_byte(true), mode_on);
    now_ms++;
    raw[1] = 0;
    scan();
    CHECK(cooked[1] == 0x01, "after the next ping: on, the release waits");
    int t = run_until_bit(1, 0x01, false, 50);
    CHECK(t == OPT_MS, "release %d ms later (want %d)", t, OPT_MS);
}

int main(void) {
    test_optical_same_scan();
    for (int on = 0; on < 2; on++) {
        test_dd_clean(on);
        test_dd_bounce(on);
        test_timer_wrap(on);
    }
    test_dd_bounce_does_not_delay_optical();
    test_dd_and_optical_independent_when_on();
    test_on_clean();
    test_on_whole_half();
    test_on_matches_sym_defer_g();
    test_mode_change_mid_bounce();
    test_split_sync();
    if (failures) {
        printf("sval_debounce: %d failures\n", failures);
        return 1;
    }
    printf("sval_debounce: all tests passed (optical %d ms when on, Double Down %d ms in both modes)\n", OPT_MS, DD_MS);
    return 0;
}
