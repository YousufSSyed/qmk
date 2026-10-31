"""Host tests for the QMK settings store (modules/svalboard/core/sval_qmk_settings.c).

Run: python3 -m unittest discover -s tests/sval_storage -v

The production file is compiled with QMK and the EEPROM mocked. It covers the
leader timeout: the reset stored 0, because LEADER_TIMEOUT is the live variable
(post_config.h), and a timeout of 0 ends every sequence 1 ms after it starts.
"""
import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
CORE = ROOT / 'modules/svalboard/core'


def defaults():
    text = (CORE / 'post_config.h').read_text()
    return ''.join(f'#define {name} {value}\n' for name, value in re.findall(r'#\s*define (SVAL_DEFAULT_LEADER_\w+) (\d+)', text))


def settings_source():
    # Drop the quoted includes (QMK and Sval headers); the prelude mocks them.
    return re.sub(r'^#\s*include "[^"]+"\n', '', (CORE / 'sval_qmk_settings.c').read_text(), flags=re.M)


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define LEADER_ENABLE
#define MOUSEKEY_ENABLE
#define AUTO_SHIFT_ENABLE
#define TAPPING_TERM 200
#define MOUSEKEY_DELAY 10
#define MOUSEKEY_INTERVAL 20
#define MOUSEKEY_MOVE_DELTA 8
#define MOUSEKEY_MAX_SPEED 10
#define MOUSEKEY_TIME_TO_MAX 30
#define MOUSEKEY_WHEEL_DELAY 10
#define MOUSEKEY_WHEEL_INTERVAL 80
#define MOUSEKEY_WHEEL_MAX_SPEED 8
#define MOUSEKEY_WHEEL_TIME_TO_MAX 40
#define SVAL_QMK_SETTINGS_OFFSET 100
uint16_t sval_leader_timeout = 0; /* what sval_reset() leaves it at on a first boot */
#define LEADER_TIMEOUT (sval_leader_timeout)
typedef struct { int dummy; } keyrecord_t;
typedef union {
    uint32_t raw;
    struct {
        bool swap_control_capslock : 1, capslock_to_control : 1, swap_lalt_lgui : 1, swap_ralt_rgui : 1, no_gui : 1, swap_grave_esc : 1, swap_backslash_backspace : 1, nkro : 1, swap_lctl_lgui : 1, swap_rctl_rgui : 1, oneshot_enable : 1;
    };
} keymap_config_t;
keymap_config_t keymap_config;
static uint8_t eeprom[256];
static unsigned eeprom_writes;
void eeconfig_read_kb_datablock(void *d, uint32_t o, uint32_t n) { memcpy(d, eeprom + o, n); }
void eeconfig_update_kb_datablock(const void *d, uint32_t o, uint32_t n) { memcpy(eeprom + o, d, n); eeprom_writes++; }
void eeconfig_update_keymap(keymap_config_t *k) { (void)k; }
void clear_keyboard(void) {}
bool get_chordal_hold_default(keyrecord_t *a, keyrecord_t *b) { (void)a; (void)b; return true; }
bool is_flow_tap_key(uint16_t k) { (void)k; return true; }
uint8_t mk_delay, mk_interval, mk_max_speed, mk_time_to_max, mk_wheel_max_speed, mk_wheel_time_to_max;
static uint16_t autoshift_timeout;
static bool autoshift_on;
void set_autoshift_timeout(uint16_t t) { autoshift_timeout = t; }
void autoshift_enable(void) { autoshift_on = true; }
void autoshift_disable(void) { autoshift_on = false; }
bool get_autoshift_state(void) { return autoshift_on; }
'''

TESTS = r'''
#define STORED (*(sval_qmk_settings_t *)(eeprom + SVAL_QMK_SETTINGS_OFFSET))
static uint16_t get16(uint16_t qsid) { uint8_t b[4] = {0}; assert(sval_qmk_settings_get(qsid, b, sizeof b) == 0); return b[0] | b[1] << 8; }
static uint8_t get8(uint16_t qsid) { uint8_t b[4] = {0}; assert(sval_qmk_settings_get(qsid, b, sizeof b) == 0); return b[0]; }
static void set16(uint16_t qsid, uint16_t v) { uint8_t b[2] = {v & 0xFF, v >> 8}; assert(sval_qmk_settings_set(qsid, b, 2) == 0); }

static void boot_with(sval_qmk_settings_t stored) {
    STORED = stored;
    eeprom_writes = 0;
    sval_leader_timeout = 0;
    sval_qmk_settings_init();
}

int main(void) {
    /* A settings reset writes the default leader timing, not the live variable (0 here). */
    sval_qmk_settings_reset();
    assert(STORED.leader_timeout == SVAL_DEFAULT_LEADER_TIMEOUT && SVAL_DEFAULT_LEADER_TIMEOUT == 300);
    assert(get16(28) == 300 && sval_leader_timeout == 300);
    assert(get8(29) == SVAL_DEFAULT_LEADER_PER_KEY_TIMING && sval_get_leader_per_key_timing());
    assert(get16(2) == 50 && get16(4) == 175 && get16(7) == 200 && get16(10) == 20);

    /* A second reset from a live 0 still writes 300 (a board at 0 used to stay at 0). */
    sval_leader_timeout = 0;
    sval_qmk_settings_reset();
    assert(STORED.leader_timeout == 300);

    /* Boot migration: a stored 0 (the old reset) becomes the default, once, and is saved. */
    sval_qmk_settings_t old = STORED;
    old.leader_timeout = 0;
    old.leader_options = 0;
    old.unused2 = 0x5A; /* the bytes around it are kept */
    boot_with(old);
    assert(eeprom_writes == 1);
    assert(STORED.leader_timeout == 300 && (STORED.leader_options & 1) && STORED.unused2 == 0x5A);
    assert(sval_leader_timeout == 300);
    old = STORED;
    boot_with(old);
    assert(eeprom_writes == 0 && STORED.leader_timeout == 300); /* nothing more on the next boot */

    /* A value the user chose is kept, per-key off included. */
    old.leader_timeout = 1000;
    old.leader_options = 0;
    boot_with(old);
    assert(eeprom_writes == 0 && STORED.leader_timeout == 1000 && !(STORED.leader_options & 1) && sval_leader_timeout == 1000);

    /* The other settings where 0 cannot work get their defaults; 0s that mean something stay. */
    old.combo_term = 0;
    old.tapping_term = 0;
    old.auto_shift_timeout = 0;
    old.mousekey_interval = 0;
    old.osk_timeout = 0;      /* one-shot: no timeout */
    old.flow_tap_term = 0;    /* Flow Tap off */
    old.tap_code_delay = 0;
    old.tapping_toggle = 0;   /* read as 1 */
    old.quick_tap_term = 0;   /* quick tap off */
    boot_with(old);
    assert(eeprom_writes == 1);
    assert(STORED.combo_term == 50 && STORED.tapping_term == 200 && STORED.auto_shift_timeout == 175 && STORED.mousekey_interval == 20);
    assert(STORED.osk_timeout == 0 && STORED.flow_tap_term == 0 && STORED.tap_code_delay == 0 && STORED.tapping_toggle == 0 && STORED.quick_tap_term == 0);
    assert(STORED.leader_timeout == 1000);
    assert(autoshift_timeout == 175 && mk_interval == 20);

    /* Writes: a 0 from an older Keybard is stored as the default; other values as sent. */
    set16(28, 0);
    assert(get16(28) == 300 && sval_leader_timeout == 300);
    set16(28, 750);
    assert(get16(28) == 750 && sval_leader_timeout == 750 && STORED.leader_timeout == 750);
    set16(2, 0);
    assert(get16(2) == 50);
    set16(7, 0);
    assert(get16(7) == 200);
    set16(6, 0);
    assert(get16(6) == 0); /* one-shot timeout 0 is allowed */
    printf("qmk settings: ok\n");
    return 0;
}
'''


class QmkSettings(unittest.TestCase):
    def test_leader_and_zero_defaults(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            (path / 'test.c').write_text(PRELUDE + defaults() + settings_source() + TESTS)
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-unused-function', '-fsanitize=undefined', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)

    def test_reset_does_not_read_the_live_leader_variable(self):
        source = (CORE / 'sval_qmk_settings.c').read_text()
        reset = source[source.index('void sval_qmk_settings_reset(void)'):]
        reset = reset[:reset.index('\n}\n')]
        self.assertNotIn('LEADER_TIMEOUT;', reset.replace('SVAL_DEFAULT_LEADER_TIMEOUT;', ''))


if __name__ == '__main__':
    unittest.main()
