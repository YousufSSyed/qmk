// Copyright 2026 Morgan Venable
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Structure checks on a manifest and on a staged image's first page and vector
// table (R7). Pure functions over bytes: the state machine runs them at
// VERIFYING_MANIFEST, VERIFYING_IMAGE and again at commit step 0, and the host
// tests run them unchanged. kb/tools/make_update.py implements the same rules.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "update_manifest.h"

#define UPDATE_BOOT2_BYTES 256
#define UPDATE_BOOT2_CRC_OFFSET 252 // the ROM checks bytes 0-251 against the LE word here
#define UPDATE_HEAD_BYTES 0x108     // boot2 page, then initial SP and reset vector
#define UPDATE_SP_MIN 0x20000000u
#define UPDATE_SP_MAX 0x20042000u   // top of SRAM5; inclusive (a full descending stack starts here)
#define UPDATE_XIP_BASE 0x10000000u
#define UPDATE_VECTORS_OFFSET 0x100u
#define UPDATE_FLASH_DIE_MAX 0x1000000u // 16 MiB: no image is longer

// What the running build is, for the checks that compare an image with it.
typedef struct {
    uint8_t  hand;           // UPDATE_HAND_*: reported (INFO), not checked against the manifest (D33)
    uint8_t  pointing_id;    // UPDATE_POINTING_*: reported (INFO), not checked (D33)
    uint16_t security_epoch; // SVAL_UPDATE_SECURITY_EPOCH
    uint8_t  storage_format; // SVAL_UPDATE_STORAGE_FORMAT
    bool     release_build;  // SVAL_UPDATE_RELEASE: refuse DIAGNOSTIC images
    uint32_t max_image;      // SVAL_UPDATE_MAX_IMAGE
} update_device_t;

// This firmware's own description (firmware builds only).
void update_device_self(update_device_t *dev);

// CRC-32/MPEG-2: poly 0x04C11DB7, init 0xFFFFFFFF, no reflection, no xorout.
// The _update form continues a CRC over more bytes, starting from
// UPDATE_CRC32_INIT.
#define UPDATE_CRC32_INIT 0xFFFFFFFFu
uint32_t update_crc32_mpeg2(const uint8_t *p, size_t n);
uint32_t update_crc32_mpeg2_update(uint32_t crc, const uint8_t *p, size_t n);

// Whether page 0 carries a boot2 the ROM would run.
bool update_boot2_valid(const uint8_t page0[UPDATE_BOOT2_BYTES]);

// Manifest fields against this build: integrity and structure only (magic,
// version, flags, floors, length), never its hand or pointing device (D33).
// UPDATE_OK or the refusal's status; the order of checks is fixed, so a manifest with several
// faults always reports the same one.
update_status_t update_manifest_check(const sval_update_manifest_t *m, const update_device_t *dev);

// The image's first UPDATE_HEAD_BYTES against its manifest length: boot2 CRC,
// initial SP, reset vector. UPDATE_OK or UPDATE_BAD_IMAGE.
update_status_t update_image_head_check(const uint8_t head[UPDATE_HEAD_BYTES], uint32_t image_len);

// The image's own build-info records (update_build_info.h, D45), read while
// its bytes are hashed, so a release build checks what the image is, not only
// what its manifest says. A record is "SVBI" then info_ver 3, the same match
// as make_update.py, which packs only images with exactly one; its flags are
// the byte after. The scan carries the last bytes across calls, so a record
// split between two slices is still found.
typedef struct {
    uint8_t win[6];  // the last bytes seen: magic, info_ver, flags
    uint8_t seen;    // bytes in win (at most 6)
    uint8_t records; // records found (stops at 255)
    uint8_t flags;   // their flags, ORed
} update_build_scan_t;
void update_build_scan_init(update_build_scan_t *sc);
void update_build_scan(update_build_scan_t *sc, const uint8_t *p, uint32_t n);
// Whether this build takes the image the scan saw: a release build refuses
// one whose record says no-chord (UPDATE_BUILD_NO_CHORD) or kept settings
// (UPDATE_BUILD_DEV_KEEP), whatever its manifest's flags say (UPDATE_BAD_IMAGE).
// An image with no record (Vial, vLaunch2, a build without the updater) cannot
// be a no-chord build and is taken.
update_status_t update_image_build_check(const update_build_scan_t *sc, const update_device_t *dev);
