// Host test for the typing pause (keyboards/svalboard/typing_pause.c, Sval 0x3C):
// the filter at the host driver, the lease that only the owning client's
// packets extend, the end of the pause (held keys released inside it and
// swallowed, a drain, an empty report), and the end on USB suspend. QMK, the
// clock, USB and the matrix are mocks (stub/quantum.h).
// Built and run by util/updater_test/run.sh.
#include <stdio.h>
#include "quantum.h"
#include "../../keyboards/svalboard/typing_pause.c"

static int checks, failures;
#define CHECK(c)                                                  \
    do {                                                          \
        checks++;                                                 \
        if (!(c)) {                                               \
            failures++;                                           \
            printf("FAIL %s:%d %s\n", __func__, __LINE__, #c);    \
        }                                                         \
    } while (0)

/* ---- mocks ---- */
usb_driver_t    USB_DRIVER;
keymap_config_t keymap_config;
static uint32_t now;
static matrix_row_t phys[MATRIX_ROWS];   // switches down
static matrix_row_t keymap[MATRIX_ROWS]; // what the keymap thinks is down
static host_driver_t *driver;
// What reached the computer.
static int usb_keyboard, usb_keyboard_empty, usb_nkro, usb_mouse, usb_extra;
static int keymap_presses, keymap_releases;

static void usb_send_keyboard(report_keyboard_t *r) {
    usb_keyboard++;
    bool empty = !r->mods;
    for (int i = 0; i < 6; i++) empty = empty && !r->keys[i];
    if (empty) usb_keyboard_empty++;
}
static void usb_send_nkro(report_nkro_t *r) {
    (void)r;
    usb_nkro++;
}
static void usb_send_mouse(report_mouse_t *r) {
    (void)r;
    usb_mouse++;
}
static void usb_send_extra(report_extra_t *r) {
    (void)r;
    usb_extra++;
}
static host_driver_t usb = {NULL, usb_send_keyboard, usb_send_nkro, usb_send_mouse, usb_send_extra};

host_driver_t *host_get_driver(void) { return driver; }
void           host_set_driver(host_driver_t *d) { driver = d; }
void           host_keyboard_send(report_keyboard_t *r) { if (driver) driver->send_keyboard(r); }
void           host_nkro_send(report_nkro_t *r) { if (driver) driver->send_nkro(r); }
bool           host_can_send_nkro(void) { return true; }
static void    send_key_report(uint8_t key) {
    report_keyboard_t r = {0};
    r.keys[0]           = key;
    host_keyboard_send(&r);
}
static void send_media(uint16_t usage) {
    report_extra_t r = {3, usage};
    if (driver) driver->send_extra(&r);
}
static void send_mouse(void) {
    report_mouse_t r = {0};
    r.x              = 3;
    if (driver) driver->send_mouse(&r);
}
void clear_keyboard(void) {
    memset(keymap, 0, sizeof(keymap));
    send_key_report(0);
}
void         clear_oneshot_mods(void) {}
void         clear_oneshot_layer_state(int s) { (void)s; }
uint32_t     timer_read32(void) { return now; }
uint32_t     timer_elapsed32(uint32_t t) { return now - t; }
bool         is_keyboard_master(void) { return true; }
static uint32_t expired_id; // a client ID whose lease (CLIENT_WRAPPER_TTL_SECS) has run out
bool         client_wrapper_valid_id(uint32_t id) { return id != 0 && id != 0xFFFFFFFF && id != expired_id; }
matrix_row_t matrix_get_row(uint8_t row) { return phys[row]; }

// QMK's action_exec: pre_process_record_kb, then the keymap types the key.
void action_exec(keyevent_t e) {
    keyrecord_t rec = {e};
    if (!sval_typing_pause_pre_process(&rec)) return;
    matrix_row_t bit = (matrix_row_t)1 << e.key.col;
    if (e.pressed) {
        keymap[e.key.row] |= bit;
        keymap_presses++;
        send_key_report(4 + e.key.row * MATRIX_COLS + e.key.col);
    } else {
        keymap[e.key.row] &= ~bit;
        keymap_releases++;
        send_key_report(0);
    }
}

/* ---- helpers ---- */
static void reset(void) {
    // A fresh module per test: the statics in typing_pause.c.
    state = SVAL_TYPING_PAUSE_STATE_OFF;
    last_end = 0; owner = owner_seen = drain_started = current_client = 0;
    saved_driver = NULL; synthesizing = false;
    memset(swallow, 0, sizeof(swallow));
    blocked_keyboard = blocked_extra = passed_mouse = 0;
    released_keys = empty_sent = 0;
    now = 1000;
    expired_id = 0;
    memset(phys, 0, sizeof(phys));
    memset(keymap, 0, sizeof(keymap));
    driver = &usb;
    USB_DRIVER.state = USB_ACTIVE;
    keymap_config.nkro = false;
    usb_keyboard = usb_keyboard_empty = usb_nkro = usb_mouse = usb_extra = 0;
    keymap_presses = keymap_releases = 0;
}
static uint8_t reply[59];
// One wrapped Sval 0x3C from a client: the wrapper hook runs first, as in via_command_kb.
static uint8_t cmd(uint32_t client, uint8_t op) {
    sval_typing_pause_client_packet(client);
    memset(reply, 0, sizeof(reply));
    reply[0] = op;
    sval_typing_pause_command(reply, sizeof(reply));
    return reply[1];
}
static void poll(uint32_t client) { sval_typing_pause_client_packet(client); } // e.g. a VIA matrix read
static void advance(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i += 10) {
        now += 10;
        sval_typing_pause_task();
    }
}
static void press(uint8_t row, uint8_t col) {
    phys[row] |= (matrix_row_t)1 << col;
    action_exec(MAKE_KEYEVENT(row, col, true));
}
static void release(uint8_t row, uint8_t col) {
    phys[row] &= ~((matrix_row_t)1 << col);
    action_exec(MAKE_KEYEVENT(row, col, false));
}

#define A 0x11111111u
#define B 0x22222222u

/* ---- tests ---- */
static void test_on_blocks_keyboard_and_media_passes_mouse(void) {
    reset();
    CHECK(cmd(A, SVAL_TYPING_PAUSE_ON) == SVAL_TYPING_PAUSE_OK);
    CHECK(reply[2] == SVAL_TYPING_PAUSE_STATE_PAUSED);
    CHECK(reply[4] == 1); // A owns it
    CHECK(usb_keyboard_empty >= 1); // turning on sends an empty report
    int before = usb_keyboard;
    press(0, 1);
    release(0, 1);
    press(7, 2); // the other half's rows: same path on the USB half
    release(7, 2);
    CHECK(usb_keyboard == before);  // nothing typed reached the computer
    CHECK(keymap_presses == 2);     // the keymap still ran
    keymap_config.nkro = true;
    report_nkro_t n = {0};
    host_nkro_send(&n);
    CHECK(usb_nkro == 0);
    send_media(0xE9); // volume up
    CHECK(usb_extra == 0);
    send_mouse();
    CHECK(usb_mouse == 1);
    cmd(A, SVAL_TYPING_PAUSE_STATUS);
    CHECK(reply[7] + (reply[8] << 8) == 5); // 4 keyboard + 1 NKRO blocked
    CHECK(reply[9] == 1);
    CHECK(reply[11] == 1);
}

static void test_owner_polls_extend_lease(void) {
    reset();
    cmd(A, SVAL_TYPING_PAUSE_ON);
    for (int i = 0; i < 100; i++) { // 5 s of 50 ms polls
        advance(50);
        poll(A);
    }
    CHECK(sval_typing_pause_active());
    CHECK(state == SVAL_TYPING_PAUSE_STATE_PAUSED);
}

static void test_other_client_does_not_extend_lease(void) {
    reset();
    cmd(A, SVAL_TYPING_PAUSE_ON);
    for (int i = 0; i < 28; i++) { // 1.4 s: only B (another app) talks
        advance(50);
        poll(B);
    }
    CHECK(state == SVAL_TYPING_PAUSE_STATE_PAUSED);
    advance(150); // 1.55 s since A's last packet
    poll(B);
    CHECK(state == SVAL_TYPING_PAUSE_STATE_RELEASING);
    CHECK(last_end == SVAL_TYPING_PAUSE_END_LEASE);
    advance(SVAL_TYPING_PAUSE_DRAIN_MS + 20);
    CHECK(state == SVAL_TYPING_PAUSE_STATE_OFF);
    CHECK(driver == &usb);
}

static void test_unwrapped_or_bad_client_refused(void) {
    reset();
    CHECK(cmd(0, SVAL_TYPING_PAUSE_ON) == SVAL_TYPING_PAUSE_REFUSED);
    CHECK(!sval_typing_pause_active());
    CHECK(driver == &usb);
}

static void test_lease_ends_with_no_traffic(void) {
    reset();
    cmd(A, SVAL_TYPING_PAUSE_ON);
    advance(1490);
    CHECK(state == SVAL_TYPING_PAUSE_STATE_PAUSED);
    advance(20);
    CHECK(state == SVAL_TYPING_PAUSE_STATE_RELEASING);
}

static void test_off_releases_held_keys_inside_pause(void) {
    reset();
    cmd(A, SVAL_TYPING_PAUSE_ON);
    press(0, 0); // held while the tester closes (Esc held to exit)
    press(8, 3); // and one on the other half
    int typed = usb_keyboard;
    int empties = usb_keyboard_empty;
    CHECK(cmd(A, SVAL_TYPING_PAUSE_OFF) == SVAL_TYPING_PAUSE_OK);
    CHECK(reply[2] == SVAL_TYPING_PAUSE_STATE_RELEASING);
    CHECK(keymap[0] == 0 && keymap[8] == 0); // released in the keymap
    CHECK(usb_keyboard == typed);            // ... with nothing reaching the computer
    press(2, 2);                             // a new press while ending: swallowed
    CHECK(keymap[2] == 0);
    advance(SVAL_TYPING_PAUSE_DRAIN_MS - 20);
    CHECK(usb_keyboard == typed);
    advance(40);
    CHECK(state == SVAL_TYPING_PAUSE_STATE_OFF);
    CHECK(driver == &usb);
    CHECK(usb_keyboard_empty > empties); // an empty report on release
    int after = usb_keyboard;
    release(0, 0); // the real releases never reach the keymap
    release(8, 3);
    release(2, 2);
    CHECK(usb_keyboard == after);
    CHECK(keymap_releases == 2); // only the two synthetic ones
    press(0, 0); // typing works again
    CHECK(usb_keyboard == after + 1);
    cmd(A, SVAL_TYPING_PAUSE_STATUS);
    CHECK(reply[13] == 2); // keys released at the end
    CHECK(reply[15] == 0); // nothing swallowed any more
}

static void test_other_client_can_turn_off(void) {
    reset();
    cmd(A, SVAL_TYPING_PAUSE_ON);
    cmd(B, SVAL_TYPING_PAUSE_OFF);
    CHECK(state == SVAL_TYPING_PAUSE_STATE_RELEASING);
}

static void test_renew_during_release_keeps_pause(void) {
    reset();
    cmd(A, SVAL_TYPING_PAUSE_ON);
    cmd(A, SVAL_TYPING_PAUSE_OFF);
    advance(100);
    cmd(A, SVAL_TYPING_PAUSE_ON);
    CHECK(state == SVAL_TYPING_PAUSE_STATE_PAUSED);
    CHECK(driver != &usb);
}

static void test_new_owner_after_client_renewal(void) {
    reset();
    cmd(A, SVAL_TYPING_PAUSE_ON);
    cmd(B, SVAL_TYPING_PAUSE_ON); // the same app with a renewed client ID
    for (int i = 0; i < 40; i++) {
        advance(50);
        poll(B);
    }
    CHECK(state == SVAL_TYPING_PAUSE_STATE_PAUSED);
    cmd(A, SVAL_TYPING_PAUSE_STATUS);
    CHECK(reply[4] == 0); // A no longer owns it
}

static void test_usb_suspend_ends_pause(void) {
    reset();
    cmd(A, SVAL_TYPING_PAUSE_ON);
    USB_DRIVER.state = USB_SUSPENDED;
    advance(10);
    CHECK(last_end == SVAL_TYPING_PAUSE_END_USB);
    poll(A);
    advance(SVAL_TYPING_PAUSE_DRAIN_MS + 20);
    CHECK(state == SVAL_TYPING_PAUSE_STATE_OFF);
    CHECK(driver == &usb);
    CHECK(cmd(A, SVAL_TYPING_PAUSE_ON) == SVAL_TYPING_PAUSE_REFUSED); // not while suspended
}

static void test_bad_op_and_short(void) {
    reset();
    CHECK(cmd(A, 9) == SVAL_TYPING_PAUSE_BAD_OP);
    uint8_t s[4] = {SVAL_TYPING_PAUSE_ON};
    sval_typing_pause_command(s, sizeof(s));
    CHECK(s[1] == SVAL_TYPING_PAUSE_TOO_SHORT);
    CHECK(!sval_typing_pause_active());
}

/* ---- the board never stays mute ---- */

static uint16_t lease_left(void) {
    return reply[5] | (reply[6] << 8);
}

// The lease and the drain across the 32-bit millisecond timer's wrap (49.7 days
// after boot): owner_seen just below UINT32_MAX, "now" past zero.
static void test_lease_ends_across_timer_wrap(void) {
    reset();
    now = UINT32_MAX - 700;
    cmd(A, SVAL_TYPING_PAUSE_ON);
    for (int i = 0; i < 20; i++) { // 1 s of owner polls, across the wrap
        advance(50);
        poll(A);
    }
    CHECK(now < 1000); // wrapped
    CHECK(state == SVAL_TYPING_PAUSE_STATE_PAUSED);
    uint32_t last = now;
    advance(1490);
    CHECK(state == SVAL_TYPING_PAUSE_STATE_PAUSED);
    advance(20);
    CHECK(state == SVAL_TYPING_PAUSE_STATE_RELEASING);
    CHECK(last_end == SVAL_TYPING_PAUSE_END_LEASE);
    CHECK(now - last <= SVAL_TYPING_PAUSE_LEASE_MS + 10);
    advance(SVAL_TYPING_PAUSE_DRAIN_MS + 20);
    CHECK(state == SVAL_TYPING_PAUSE_STATE_OFF);
    CHECK(driver == &usb);
}

// The owner's last packet lands just below UINT32_MAX and the lease runs out
// after the wrap.
static void test_owner_seen_near_uint32_max(void) {
    reset();
    now = UINT32_MAX - 30;
    cmd(A, SVAL_TYPING_PAUSE_ON);
    CHECK(owner_seen == UINT32_MAX - 30);
    cmd(B, SVAL_TYPING_PAUSE_STATUS);
    CHECK(lease_left() == SVAL_TYPING_PAUSE_LEASE_MS);
    advance(100); // past zero
    cmd(B, SVAL_TYPING_PAUSE_STATUS);
    CHECK(reply[2] == SVAL_TYPING_PAUSE_STATE_PAUSED);
    CHECK(lease_left() == SVAL_TYPING_PAUSE_LEASE_MS - 100);
    advance(1390);
    CHECK(state == SVAL_TYPING_PAUSE_STATE_PAUSED);
    advance(20);
    CHECK(state == SVAL_TYPING_PAUSE_STATE_RELEASING);
    advance(SVAL_TYPING_PAUSE_DRAIN_MS + 20);
    CHECK(state == SVAL_TYPING_PAUSE_STATE_OFF);
    CHECK(driver == &usb);
}

// The drain starts just below UINT32_MAX and ends after the wrap.
static void test_drain_across_timer_wrap(void) {
    reset();
    now = UINT32_MAX - 200;
    cmd(A, SVAL_TYPING_PAUSE_ON);
    cmd(A, SVAL_TYPING_PAUSE_OFF);
    CHECK(drain_started == UINT32_MAX - 200);
    advance(SVAL_TYPING_PAUSE_DRAIN_MS - 20);
    CHECK(state == SVAL_TYPING_PAUSE_STATE_RELEASING);
    advance(30);
    CHECK(state == SVAL_TYPING_PAUSE_STATE_OFF);
    CHECK(driver == &usb);
}

// Every starting point of the clock near the wrap, 1 ms apart: the pause always
// ends between 1.5 s and 1.5 s + one task pass after the owner's last packet,
// and typing is back by the drain after that.
static void test_every_phase_near_wrap_ends(void) {
    for (uint32_t back = 0; back <= 2100; back += 7) {
        reset();
        now = UINT32_MAX - back;
        cmd(A, SVAL_TYPING_PAUSE_ON);
        uint32_t t = 0;
        while (state == SVAL_TYPING_PAUSE_STATE_PAUSED && t < 10000) {
            now += 1;
            t += 1;
            sval_typing_pause_task();
        }
        CHECK(t == SVAL_TYPING_PAUSE_LEASE_MS);
        while (state != SVAL_TYPING_PAUSE_STATE_OFF && t < 10000) {
            now += 1;
            t += 1;
            sval_typing_pause_task();
        }
        CHECK(t == SVAL_TYPING_PAUSE_LEASE_MS + SVAL_TYPING_PAUSE_DRAIN_MS);
        CHECK(driver == &usb);
    }
}

// An owner whose client ID expired keeps sending with it (the wrapper rejects
// those packets, but via_command_kb hands the ID to the pause first): they must
// not extend the lease.
static void test_expired_owner_id_does_not_extend(void) {
    reset();
    cmd(A, SVAL_TYPING_PAUSE_ON);
    advance(500);
    poll(A);
    uint32_t last = now;
    expired_id = A;
    for (int i = 0; i < 40; i++) { // 2 s of stale packets
        advance(50);
        poll(A);
        cmd(A, SVAL_TYPING_PAUSE_ON); // refused: no valid client ID
        if (state != SVAL_TYPING_PAUSE_STATE_PAUSED) break;
    }
    CHECK(state != SVAL_TYPING_PAUSE_STATE_PAUSED);
    CHECK(last_end == SVAL_TYPING_PAUSE_END_LEASE);
    CHECK(now - last <= SVAL_TYPING_PAUSE_LEASE_MS + 50);
    advance(SVAL_TYPING_PAUSE_DRAIN_MS + 20);
    CHECK(state == SVAL_TYPING_PAUSE_STATE_OFF);
    CHECK(cmd(A, SVAL_TYPING_PAUSE_ON) == SVAL_TYPING_PAUSE_REFUSED);
    CHECK(!sval_typing_pause_active());
}

// A renewed client ID that never sends "on" again: its packets are not the
// owner's, so they don't extend the pause.
static void test_renewed_id_without_on_does_not_extend(void) {
    reset();
    cmd(A, SVAL_TYPING_PAUSE_ON);
    uint32_t last = now;
    for (int i = 0; i < 40; i++) {
        advance(50);
        poll(B);
        if (state != SVAL_TYPING_PAUSE_STATE_PAUSED) break;
    }
    CHECK(state == SVAL_TYPING_PAUSE_STATE_RELEASING);
    CHECK(now - last <= SVAL_TYPING_PAUSE_LEASE_MS + 50);
}

// The main loop stalls for 5 s (the task does not run): the first pass after
// it ends the pause, and the drain follows.
static void test_stalled_loop_ends_on_first_pass(void) {
    reset();
    cmd(A, SVAL_TYPING_PAUSE_ON);
    now += 5000;
    sval_typing_pause_task();
    CHECK(state == SVAL_TYPING_PAUSE_STATE_RELEASING);
    advance(SVAL_TYPING_PAUSE_DRAIN_MS + 10);
    CHECK(state == SVAL_TYPING_PAUSE_STATE_OFF);
    CHECK(driver == &usb);
}

// USB resets and comes back between two task passes: the lease still ends it.
static void test_usb_blip_between_passes_lease_still_ends(void) {
    reset();
    cmd(A, SVAL_TYPING_PAUSE_ON);
    USB_DRIVER.state = USB_READY; // reset, then configured again before the task runs
    USB_DRIVER.state = USB_ACTIVE;
    advance(1510);
    CHECK(state == SVAL_TYPING_PAUSE_STATE_RELEASING);
    advance(SVAL_TYPING_PAUSE_DRAIN_MS + 20);
    CHECK(state == SVAL_TYPING_PAUSE_STATE_OFF);
}

int main(void) {
    test_lease_ends_across_timer_wrap();
    test_owner_seen_near_uint32_max();
    test_drain_across_timer_wrap();
    test_every_phase_near_wrap_ends();
    test_expired_owner_id_does_not_extend();
    test_renewed_id_without_on_does_not_extend();
    test_stalled_loop_ends_on_first_pass();
    test_usb_blip_between_passes_lease_still_ends();
    test_on_blocks_keyboard_and_media_passes_mouse();
    test_owner_polls_extend_lease();
    test_other_client_does_not_extend_lease();
    test_unwrapped_or_bad_client_refused();
    test_lease_ends_with_no_traffic();
    test_off_releases_held_keys_inside_pause();
    test_other_client_can_turn_off();
    test_renew_during_release_keeps_pause();
    test_new_owner_after_client_renewal();
    test_usb_suspend_ends_pause();
    test_bad_op_and_short();
    printf("typing pause: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
