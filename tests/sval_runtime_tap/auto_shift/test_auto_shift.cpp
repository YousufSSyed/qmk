// SPDX-License-Identifier: GPL-2.0-or-later
#include "../test_runtime_tap.cpp"
extern "C" {
#include "process_auto_shift.h"
}
using testing::AnyNumber;

TEST_F(RuntimeTap, AutoShiftReleaseUsesRuntimeDelayWithZeroCompileTimeDefault) {
    TestDriver driver;
    auto       key = KeymapKey(0, 0, 0, KC_A);
    set_keymap({key});
    autoshift_enable();
    set_autoshift_timeout(AUTO_SHIFT_TIMEOUT);
    for (uint16_t delay : {0, 37, 300}) {
        code_delay = delay;
        expect_timed_tap(driver, KC_A, delay, [&] { tap_key(key, 30); });
        idle_for(300);
    }
}

TEST_F(RuntimeTap, AutoShiftEnableDisableAndTimeoutFollowRuntimeSettings) {
    TestDriver driver;
    auto       key = KeymapKey(0, 0, 0, KC_A);
    set_keymap({key});
    set_autoshift_timeout(150);

    // Disabled: holding past timeout sends unshifted KC_A immediately on press.
    autoshift_disable();
    {
        InSequence order;
        EXPECT_REPORT(driver, (KC_A));
        EXPECT_EMPTY_REPORT(driver);
        tap_key(key, 200);
        testing::Mock::VerifyAndClearExpectations(&driver);
    }

    // Enabled: holding past timeout sends shifted KC_A.
    autoshift_enable();
    {
        InSequence order;
        EXPECT_CALL(driver, send_keyboard_mock(KeyboardReport(KC_LSFT))).Times(AnyNumber());
        EXPECT_REPORT(driver, (KC_LSFT, KC_A));
        EXPECT_CALL(driver, send_keyboard_mock(KeyboardReport(KC_LSFT))).Times(AnyNumber());
        EXPECT_EMPTY_REPORT(driver);
        tap_key(key, 160);
        testing::Mock::VerifyAndClearExpectations(&driver);
    }

    // Raising runtime timeout makes a 160 ms hold produce unshifted KC_A.
    set_autoshift_timeout(220);
    {
        InSequence order;
        EXPECT_REPORT(driver, (KC_A));
        EXPECT_EMPTY_REPORT(driver);
        tap_key(key, 160);
        testing::Mock::VerifyAndClearExpectations(&driver);
    }
}

TEST_F(RuntimeTap, AutoShiftCategoryExclusionBitsControlAlphaNumericAndSpecialKeys) {
    TestDriver driver;
    auto       alpha   = KeymapKey(0, 0, 0, KC_A);
    auto       numeric = KeymapKey(0, 1, 0, KC_1);
    auto       special = KeymapKey(0, 2, 0, KC_MINS);
    set_keymap({alpha, numeric, special});
    autoshift_enable();
    set_autoshift_timeout(150);

    // Bit 4 (no-alpha), bit 3 (no-numeric), bit 2 (no-special)
    const struct {
        uint8_t    bit;
        KeymapKey *target;
        uint8_t    code;
    } cases[] = {
        {1 << 4, &alpha, KC_A},
        {1 << 3, &numeric, KC_1},
        {1 << 2, &special, KC_MINS},
    };

    for (const auto &c : cases) {
        auto_shift_bits = 0;
        {
            InSequence order;
            EXPECT_CALL(driver, send_keyboard_mock(KeyboardReport(KC_LSFT))).Times(AnyNumber());
            EXPECT_REPORT(driver, (KC_LSFT, c.code));
            EXPECT_CALL(driver, send_keyboard_mock(KeyboardReport(KC_LSFT))).Times(AnyNumber());
            EXPECT_EMPTY_REPORT(driver);
            tap_key(*c.target, 160);
            testing::Mock::VerifyAndClearExpectations(&driver);
        }

        auto_shift_bits = c.bit;
        {
            InSequence order;
            EXPECT_REPORT(driver, (c.code));
            EXPECT_EMPTY_REPORT(driver);
            tap_key(*c.target, 160);
            testing::Mock::VerifyAndClearExpectations(&driver);
        }
    }
}

TEST_F(RuntimeTap, AutoShiftModifiersAndRepeatBitsFollowRuntimeSettings) {
    TestDriver driver;
    auto       ctrl = KeymapKey(0, 0, 0, KC_LCTL);
    auto       key  = KeymapKey(0, 1, 0, KC_A);
    set_keymap({ctrl, key});
    autoshift_enable();
    set_autoshift_timeout(150);

    // Modifiers bit (bit 1) off: Ctrl+long-press A does not add Shift.
    auto_shift_bits = 0;
    {
        InSequence order;
        EXPECT_REPORT(driver, (KC_LCTL));
        EXPECT_REPORT(driver, (KC_LCTL, KC_A));
        EXPECT_REPORT(driver, (KC_LCTL));
        EXPECT_EMPTY_REPORT(driver);
        ctrl.press();
        run_one_scan_loop();
        tap_key(key, 160);
        ctrl.release();
        run_one_scan_loop();
        testing::Mock::VerifyAndClearExpectations(&driver);
    }

    // Modifiers bit (bit 1) on: Ctrl+long-press A adds Shift on timeout.
    auto_shift_bits = 1 << 1;
    {
        InSequence order;
        EXPECT_REPORT(driver, (KC_LCTL));
        EXPECT_CALL(driver, send_keyboard_mock(KeyboardReport(KC_LCTL, KC_LSFT))).Times(AnyNumber());
        EXPECT_REPORT(driver, (KC_LCTL, KC_LSFT, KC_A));
        EXPECT_CALL(driver, send_keyboard_mock(KeyboardReport(KC_LCTL, KC_LSFT))).Times(AnyNumber());
        EXPECT_REPORT(driver, (KC_LCTL));
        EXPECT_EMPTY_REPORT(driver);
        ctrl.press();
        run_one_scan_loop();
        tap_key(key, 160);
        ctrl.release();
        run_one_scan_loop();
        testing::Mock::VerifyAndClearExpectations(&driver);
    }

    // Repeat bit (bit 5) on, no-auto-repeat (bit 6) off: holding past timeout keeps key pressed until release.
    auto_shift_bits = 1 << 5;
    {
        InSequence order;
        EXPECT_CALL(driver, send_keyboard_mock(KeyboardReport(KC_LSFT))).Times(AnyNumber());
        EXPECT_REPORT(driver, (KC_LSFT, KC_A));
        key.press();
        idle_for(160);
        testing::Mock::VerifyAndClearExpectations(&driver);

        EXPECT_CALL(driver, send_keyboard_mock(KeyboardReport(KC_LSFT))).Times(AnyNumber());
        EXPECT_EMPTY_REPORT(driver);
        key.release();
        run_one_scan_loop();
        testing::Mock::VerifyAndClearExpectations(&driver);
    }

    // Repeat bit (bit 5) + no-auto-repeat bit (bit 6): holding past timeout releases immediately.
    idle_for(TAPPING_TERM + 1);
    auto_shift_bits = (1 << 5) | (1 << 6);
    {
        InSequence order;
        EXPECT_CALL(driver, send_keyboard_mock(KeyboardReport(KC_LSFT))).Times(AnyNumber());
        EXPECT_REPORT(driver, (KC_LSFT, KC_A));
        EXPECT_CALL(driver, send_keyboard_mock(KeyboardReport(KC_LSFT))).Times(AnyNumber());
        EXPECT_EMPTY_REPORT(driver);
        key.press();
        idle_for(160);
        testing::Mock::VerifyAndClearExpectations(&driver);

        EXPECT_NO_REPORT(driver);
        key.release();
        run_one_scan_loop();
        testing::Mock::VerifyAndClearExpectations(&driver);
    }
}
