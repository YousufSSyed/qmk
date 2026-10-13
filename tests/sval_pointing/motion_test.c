// Host test for pointing_motion.h: the detected-code to fragment mapping (Sval 0x18) and the
// POINTING_MOTION (Sval 0x2D) packing, clamping and saturation. sval_config.h is generated
// from the real sval.json by test_detect.py.
#include <stdio.h>
#include <string.h>
#include "sval_config.h"
#include "pointing_motion.h"

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                 \
        }                                                               \
    } while (0)

static int16_t le16(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }

static void test_mapping(void) {
    CHECK(SVAL_FRAGMENT_IDX_LEFT_POINTING == 10);
    CHECK(SVAL_FRAGMENT_IDX_RIGHT_POINTING == 11);
    CHECK(sval_pointing_fragment_for_detected(0) == 0xFF);
    CHECK(sval_pointing_fragment_for_detected(1) == 5);
    CHECK(sval_pointing_fragment_for_detected(2) == 6);
    CHECK(sval_pointing_fragment_for_detected(3) == 4);
    CHECK(sval_pointing_fragment_for_detected(4) == 9);
    CHECK(sval_pointing_fragment_for_detected(5) == 7);
    CHECK(sval_pointing_fragment_for_detected(6) == 8);
    CHECK(sval_pointing_fragment_for_detected(7) == 0xFF);
    CHECK(sval_pointing_fragment_for_detected(0xFF) == 0xFF);
}

static void test_pack_plain(void) {
    sval_motion_acc_t acc = {0};
    uint8_t           out[SVAL_MOTION_HAND_LEN];
    sval_motion_accumulate(&acc, 3, -4, 1, -2, 0x01);
    sval_motion_accumulate(&acc, -1, -6, 0, 5, 0x04);
    sval_motion_pack_hand(out, &acc, SVAL_MOTION_VALID | SVAL_MOTION_SCROLLING);
    CHECK(out[0] == (SVAL_MOTION_VALID | SVAL_MOTION_SCROLLING));
    CHECK(le16(&out[1]) == 2);
    CHECK(le16(&out[3]) == -10);
    CHECK((int8_t)out[5] == 1);
    CHECK((int8_t)out[6] == 3);
    CHECK(out[7] == 0x05);
    CHECK(out[8] == 0 && out[9] == 0);
}

static void test_pack_clamps(void) {
    sval_motion_acc_t acc = {0};
    uint8_t           out[SVAL_MOTION_HAND_LEN];
    for (int i = 0; i < 400; i++) sval_motion_accumulate(&acc, 127, -127, 0, 0, 0);
    CHECK(!acc.saturated); // the int32 accumulators hold it
    sval_motion_pack_hand(out, &acc, SVAL_MOTION_VALID);
    CHECK(le16(&out[1]) == 32767);
    CHECK(le16(&out[3]) == -32768);
    CHECK(out[0] == (SVAL_MOTION_VALID | SVAL_MOTION_SATURATED));

    memset(&acc, 0, sizeof(acc));
    sval_motion_accumulate(&acc, 0, 0, 100, -100, 0);
    sval_motion_accumulate(&acc, 0, 0, 100, -100, 0);
    sval_motion_pack_hand(out, &acc, SVAL_MOTION_VALID);
    CHECK((int8_t)out[5] == 127);
    CHECK((int8_t)out[6] == -128);
    CHECK(out[0] & SVAL_MOTION_SATURATED);

    // int16 h/v accumulators saturate rather than wrap
    memset(&acc, 0, sizeof(acc));
    for (int i = 0; i < 300; i++) sval_motion_accumulate(&acc, 0, 0, 127, -128, 0);
    CHECK(acc.h == 32767 && acc.v == -32768 && acc.saturated);

    // int32 accumulators saturate rather than wrap
    memset(&acc, 0, sizeof(acc));
    acc.x = INT32_MAX - 1;
    acc.y = INT32_MIN + 1;
    sval_motion_accumulate(&acc, 5, -5, 0, 0, 0);
    CHECK(acc.x == INT32_MAX && acc.y == INT32_MIN && acc.saturated);
    sval_motion_pack_hand(out, &acc, 0);
    CHECK(out[0] == SVAL_MOTION_SATURATED);

    // bit 7 is always zero
    memset(&acc, 0, sizeof(acc));
    sval_motion_pack_hand(out, &acc, 0xFF);
    CHECK((out[0] & 0x80) == 0);
}

static void test_elapsed(void) {
    CHECK(sval_motion_elapsed(true, 1234) == 0);
    CHECK(sval_motion_elapsed(false, 33) == 33);
    CHECK(sval_motion_elapsed(false, 255) == 255);
    CHECK(sval_motion_elapsed(false, 100000) == 255);
}

static void test_layout(void) {
    CHECK(SVAL_MOTION_LEFT_OFF + SVAL_MOTION_HAND_LEN == SVAL_MOTION_RIGHT_OFF);
    CHECK(SVAL_MOTION_RIGHT_OFF + SVAL_MOTION_HAND_LEN == SVAL_MOTION_AXIS_OFF);
    CHECK(SVAL_MOTION_AXIS_OFF + 1 == SVAL_MOTION_REPORT_LEN);
    CHECK(2 + SVAL_MOTION_REPORT_LEN <= 26); // fits a client-wrapped packet
}

int main(void) {
    test_mapping();
    test_pack_plain();
    test_pack_clamps();
    test_elapsed();
    test_layout();
    if (failures) return 1;
    printf("OK\n");
    return 0;
}
