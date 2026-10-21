// Copyright 2026 Morgan Venable
// SPDX-License-Identifier: GPL-2.0-or-later

// Stored settings reset on the first boot of a different firmware build (see build_reset.h).

#include <string.h>
#include "build_reset.h"

uint32_t build_reset_hash(const uint8_t *image, size_t len) {
    uint32_t h = 0x811C9DC5u;
    size_t   i = 0;
    for (; i + 4 <= len; i += 4) {
        uint32_t w;
        memcpy(&w, image + i, 4);
        h = (h ^ w) * 0x01000193u;
    }
    if (i < len) {
        uint32_t w = 0xFFFFFFFFu;
        memcpy(&w, image + i, len - i);
        h = (h ^ w) * 0x01000193u;
    }
    return h;
}

static bool valid(const build_reset_record_t *rec) {
    return rec && rec->magic == BUILD_RESET_MAGIC && rec->check == ~(rec->magic ^ rec->image ^ rec->flags);
}

bool build_reset_matches(const build_reset_record_t *rec, uint32_t image) {
    return valid(rec) && rec->image == image;
}

bool build_reset_due(bool available, const build_reset_record_t *rec, uint32_t image) {
    return available && !build_reset_matches(rec, image);
}

bool build_reset_notice(const build_reset_record_t *rec, uint32_t image) {
    return build_reset_matches(rec, image) && (rec->flags & BUILD_RESET_NOTICE);
}

void build_reset_record_make(build_reset_record_t *rec, uint32_t image, uint32_t flags) {
    rec->magic = BUILD_RESET_MAGIC;
    rec->image = image;
    rec->flags = flags;
    rec->check = ~(rec->magic ^ rec->image ^ rec->flags);
}

static void put32le(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t get32le(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

_Static_assert(sizeof(build_reset_record_t) + 8 == BUILD_RESET_HEAD_BYTES, "the blank mark follows the record");

void build_reset_invalid_page(uint8_t page[BUILD_RESET_PAGE_BYTES], uint8_t mode) {
    memset(page, 0xFF, BUILD_RESET_PAGE_BYTES);
    memset(page, 0x00, BUILD_RESET_HEAD_BYTES);
    if (mode == BUILD_RESET_TO_BLANK) {
        put32le(page + sizeof(build_reset_record_t), BUILD_RESET_BLANK_MAGIC);
        put32le(page + sizeof(build_reset_record_t) + 4, ~BUILD_RESET_BLANK_MAGIC);
    }
}

void build_reset_program_head(uint8_t head[BUILD_RESET_HEAD_BYTES], const uint8_t page[BUILD_RESET_PAGE_BYTES]) {
    for (size_t i = 0; i < BUILD_RESET_HEAD_BYTES; i++) head[i] &= page[i];
}

bool build_reset_blank_requested(const uint8_t head[BUILD_RESET_HEAD_BYTES]) {
    build_reset_record_t rec;
    memcpy(&rec, head, sizeof(rec));
    const uint8_t *mark = head + sizeof(rec);
    return build_reset_cleared(&rec) && get32le(mark) == BUILD_RESET_BLANK_MAGIC && get32le(mark + 4) == (uint32_t)~BUILD_RESET_BLANK_MAGIC;
}

bool build_reset_cleared(const build_reset_record_t *rec) {
    const uint8_t *p = (const uint8_t *)rec;
    for (size_t i = 0; i < sizeof(*rec); i++) {
        if (p[i]) return false;
    }
    return true;
}

void build_reset_program_over(build_reset_record_t *rec, const uint8_t page[BUILD_RESET_PAGE_BYTES]) {
    uint8_t *p = (uint8_t *)rec;
    for (size_t i = 0; i < sizeof(*rec); i++) p[i] &= page[i];
}

#ifndef BUILD_RESET_HOST_TEST
#    include "eeconfig.h"
#    include "dynamic_keymap.h"
#    include "matrix.h"
#    include "identity.h"
#    include "hardware/regs/addressmap.h"
#    include "hardware/sync.h"
#    include "pico/bootrom.h"
extern const uint8_t BOOT2_ROM[256];

_Static_assert(BUILD_RESET_SECTOR + 4096u == IDENTITY_SECTOR_A, "the build reset sector is just below the identity sectors");
#    ifdef SVAL_UPDATE_BASE
_Static_assert(SVAL_UPDATE_BASE + SVAL_UPDATE_SIZE <= BUILD_RESET_SECTOR, "the staging slot ends below the build reset sector");
#    endif

extern const uint8_t __flash_binary_end[]; // RP2040_FLASH_TIMECRIT.ld

#    ifdef SVAL_DEV_KEEP_SETTINGS
#        ifdef SVAL_UPDATE_RELEASE
#            error "release builds cannot have SVAL_DEV_KEEP_SETTINGS (dev builds only)"
#        endif
// Kept in the ELF so check_release_elf.py finds it.
__attribute__((used)) const volatile uint8_t sval_dev_keep_settings = 1;
#    endif

static uint32_t image_hash;
static bool     pending, notice, record_failed, invalidated;
static bool     blank_due, blanked; // D44: this boot sets every key to KC_NO after the reset
// SETTINGS_RESET (D42, build_reset_invalidate): boot2 taken at pre-init, while
// page 0 is this firmware's boot2, and the page programmed over the record.
static uint32_t boot2_copy[64];
static bool     boot2_ready;
static uint8_t  invalid_page[BUILD_RESET_PAGE_BYTES] __attribute__((aligned(4)));

static const build_reset_record_t *record(void) {
    return (const build_reset_record_t *)(XIP_NOCACHE_NOALLOC_BASE + BUILD_RESET_SECTOR);
}

static void read_head(uint8_t head[BUILD_RESET_HEAD_BYTES]) {
    memcpy(head, (const void *)record(), BUILD_RESET_HEAD_BYTES);
}

void build_reset_pre_init(void) {
    memcpy(boot2_copy, BOOT2_ROM, sizeof(boot2_copy)); // for build_reset_invalidate (D42)
    boot2_ready = true;
#    ifdef SVAL_DEV_KEEP_SETTINGS
    // Dev "keep" builds: settings carry over and the record is left alone,
    // except after a SETTINGS_RESET (D42), which asked for the reset.
    if (sval_dev_keep_settings && !build_reset_cleared(record())) return;
#    endif
    // On a smaller die the sector does not exist (its address would wrap onto
    // the firmware), so there is no record to keep.
    if (!identity_available()) return;
    image_hash = build_reset_hash((const uint8_t *)XIP_BASE, (size_t)(__flash_binary_end - (const uint8_t *)XIP_BASE));
    pending    = build_reset_due(true, record(), image_hash);
    notice     = !pending && build_reset_notice(record(), image_hash);
    if (pending) {
        uint8_t head[BUILD_RESET_HEAD_BYTES];
        read_head(head);
        blank_due = build_reset_blank_requested(head); // SETTINGS_RESET mode BLANK (D44)
    }
    // quantum_init() then finds the settings invalid and resets them all, as
    // for a new EECONFIG_MAGIC_NUMBER.
    if (pending) eeconfig_disable();
}

static bool write_record(uint32_t flags) {
    build_reset_record_t rec;
    build_reset_record_make(&rec, image_hash, flags);
    return identity_build_record_write(&rec, sizeof(rec));
}

// D44: every key on every layer KC_NO (0x0000), over the defaults that the
// reset just wrote (VIA's dynamic_keymap_reset(), and sval_init()'s, which ran
// in the module post-init before this). It runs before the record is written,
// so after a power cut here the record still reads cleared with the mark in
// place, and the next boot resets and blanks again.
static void blank_keymap(void) {
    static const uint8_t zero[64] = {0};
    uint32_t             size     = (uint32_t)dynamic_keymap_get_layer_count() * MATRIX_ROWS * MATRIX_COLS * 2;
    for (uint32_t off = 0; off < size; off += sizeof(zero)) {
        uint32_t n = size - off < sizeof(zero) ? size - off : sizeof(zero);
        dynamic_keymap_set_buffer((uint16_t)off, (uint16_t)n, (uint8_t *)zero);
    }
    blanked = true;
}

void build_reset_post_init(void) {
    if (!pending) return;
    pending = false;
    if (blank_due) blank_keymap();
    if (write_record(BUILD_RESET_NOTICE)) {
        notice = true;
    } else {
        record_failed = true; // the next boot resets again: tell the host now
    }
}

bool build_reset_notice_pending(void) {
    return notice;
}

bool build_reset_record_failed(void) {
    return record_failed;
}

bool build_reset_blanked(void) {
    return blanked;
}

void build_reset_notice_clear(void) {
    if (!notice || invalidated) return; // after SETTINGS_RESET the record stays invalid until the reset
    if (write_record(0)) notice = false;
}

// One page program over the record, through the boot ROM from RAM, as
// identity.c and update_flash.c do: the Pico SDK's flash_range_program()
// hard-asserts any address above PICO_FLASH_SIZE_BYTES (2 MB here), and the
// record is at 16 MB - 12 KB. Interrupts are off around it (the caller's
// section); nothing may run from flash meanwhile.
static void __no_inline_not_in_flash_func(record_page_program)(void) {
    rom_connect_internal_flash_fn connect  = (rom_connect_internal_flash_fn)rom_func_lookup_inline(ROM_FUNC_CONNECT_INTERNAL_FLASH);
    rom_flash_exit_xip_fn         exit_xip = (rom_flash_exit_xip_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_EXIT_XIP);
    rom_flash_range_program_fn    program  = (rom_flash_range_program_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_RANGE_PROGRAM);
    rom_flash_flush_cache_fn      flush    = (rom_flash_flush_cache_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_FLUSH_CACHE);
    __compiler_memory_barrier();
    connect();
    exit_xip();
    program(BUILD_RESET_SECTOR, invalid_page, BUILD_RESET_PAGE_BYTES); // no erase: NOR programming only clears bits
    flush();
    ((void (*)(void))((intptr_t)boot2_copy + 1))(); // back to XIP via boot2
}

bool build_reset_invalidate(uint8_t mode) {
    if (mode > BUILD_RESET_MODE_MAX || !boot2_ready || !identity_available()) return false;
    build_reset_invalid_page(invalid_page, mode);
    invalidated = true;
    uint32_t irq = save_and_disable_interrupts();
    record_page_program();
    restore_interrupts(irq);
    uint8_t head[BUILD_RESET_HEAD_BYTES];
    read_head(head);
    build_reset_record_t rec;
    memcpy(&rec, head, sizeof(rec));
    if (build_reset_matches(&rec, rec.image)) return false;
    return build_reset_blank_requested(head) == (mode == BUILD_RESET_TO_BLANK);
}
#endif
