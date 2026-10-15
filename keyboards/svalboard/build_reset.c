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

void build_reset_invalid_page(uint8_t page[BUILD_RESET_PAGE_BYTES]) {
    memset(page, 0xFF, BUILD_RESET_PAGE_BYTES);
    memset(page, 0x00, sizeof(build_reset_record_t));
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
#    include "identity.h"
#    include "hardware/regs/addressmap.h"
#    include "hardware/flash.h"
#    include "hardware/sync.h"

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

static const build_reset_record_t *record(void) {
    return (const build_reset_record_t *)(XIP_NOCACHE_NOALLOC_BASE + BUILD_RESET_SECTOR);
}

void build_reset_pre_init(void) {
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
    // quantum_init() then finds the settings invalid and resets them all, as
    // for a new EECONFIG_MAGIC_NUMBER.
    if (pending) eeconfig_disable();
}

static bool write_record(uint32_t flags) {
    build_reset_record_t rec;
    build_reset_record_make(&rec, image_hash, flags);
    return identity_build_record_write(&rec, sizeof(rec));
}

void build_reset_post_init(void) {
    if (!pending) return;
    pending = false;
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

void build_reset_notice_clear(void) {
    if (!notice || invalidated) return; // after SETTINGS_RESET the record stays invalid until the reset
    if (write_record(0)) notice = false;
}

bool build_reset_invalidate(void) {
    if (!identity_available()) return false;
    static uint8_t page[BUILD_RESET_PAGE_BYTES] __attribute__((aligned(4)));
    build_reset_invalid_page(page);
    invalidated = true;
    // flash_range_program() runs from RAM, leaves XIP and restores it through
    // boot2, and flushes the XIP cache; nothing may run from flash meanwhile.
    uint32_t irq = save_and_disable_interrupts();
    flash_range_program(BUILD_RESET_SECTOR, page, sizeof(page));
    restore_interrupts(irq);
    build_reset_record_t rec;
    memcpy(&rec, (const void *)record(), sizeof(rec));
    return !build_reset_matches(&rec, rec.image);
}
#endif
