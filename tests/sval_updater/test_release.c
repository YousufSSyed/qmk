// Copyright 2026 Morgan Venable
// SPDX-License-Identifier: GPL-2.0-or-later

// Host test for a RELEASE updater build (M3): built by util/updater_test/run.sh
// with SVAL_UPDATE_RELEASE, against the real update_build_info.c and
// update_image.c.
//
//   test_release [FILE.svup:ok|FILE.svup:refused ...]
//
// Checks the build-info record of a release build (RELEASE set, no test
// hooks, keytest or host bootloader, the retired key fields clear), that a
// release build refuses DIAGNOSTIC images and version-1 manifests, then runs
// each .svup through the checks a release build makes before it erases
// anything (manifest fields, hash, image structure), expecting it to pass
// ("ok") or fail ("refused"). The .svup files come from outside (run.sh's
// optional SVAL_RELEASE_SVUPS, or a CI or local release run).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "update_manifest.h"
#include "update_image.h"
#include "update_build_info.h"
#include "sha512.h"

#ifndef SVAL_UPDATE_RELEASE
#    error "test_release.c is built as a release updater build"
#endif

static int checks, failures;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        checks++;                                                                     \
        if (!(cond)) {                                                                \
            failures++;                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                             \
    } while (0)

static uint8_t *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(n > 0 ? (size_t)n : 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

// The checks a release build runs before it erases anything: ARM's manifest
// check (again at BEGIN), VERIFYING_IMAGE's hash and structure. A .svup is
// the manifest, then the image.
static update_status_t release_check(const uint8_t *buf, size_t len) {
    if (len < UPDATE_MANIFEST_BYTES) return UPDATE_INVALID;
    sval_update_manifest_t m;
    memcpy(&m, buf, sizeof(m));
    const uint8_t  *image = buf + UPDATE_MANIFEST_BYTES;
    size_t          ilen  = len - UPDATE_MANIFEST_BYTES;
    update_device_t dev   = {
          .hand           = m.hand,
          .pointing_id    = m.pointing_id,
          .security_epoch = SVAL_UPDATE_SECURITY_EPOCH,
          .storage_format = SVAL_UPDATE_STORAGE_FORMAT,
          .release_build  = true,
          .max_image      = SVAL_UPDATE_MAX_IMAGE,
    };
    update_status_t st = update_manifest_check(&m, &dev);
    if (st != UPDATE_OK) return st;
    if (m.image_len != ilen) return UPDATE_BAD_HASH;
    uint8_t h[64];
    crypto_sha512(h, image, ilen);
    if (memcmp(h, m.sha512, 64) != 0) return UPDATE_BAD_HASH;
    return ilen >= UPDATE_HEAD_BYTES ? update_image_head_check(image, m.image_len) : UPDATE_BAD_IMAGE;
}

int main(int argc, char **argv) {
    // ---- the build-info record --------------------------------------------------------------
    CHECK(memcmp(sval_update_build_info.magic, "SVBI", 4) == 0);
    CHECK(sval_update_build_info.info_ver == UPDATE_BUILD_INFO_VERSION);
    CHECK(sval_update_build_info.flags & UPDATE_BUILD_RELEASE);
    CHECK(!(sval_update_build_info.flags & (UPDATE_BUILD_TEST_HOOKS | UPDATE_BUILD_KEYTEST | UPDATE_BUILD_HOST_BOOTLOADER)));
    CHECK(!(sval_update_build_info.flags & (0x02 | 0x20))); // retired key flags
    CHECK(sval_update_build_info.reserved0 == 0);

    // ---- what a release build refuses --------------------------------------------------------
    sval_update_manifest_t m;
    memset(&m, 0, sizeof(m));
    m.magic          = UPDATE_MANIFEST_MAGIC;
    m.manifest_ver   = UPDATE_MANIFEST_VERSION;
    m.hand           = UPDATE_HAND_RIGHT;
    m.pointing_id    = UPDATE_POINTING_PMW3389;
    m.keymap_id      = UPDATE_KEYMAP_SVAL;
    m.storage_format = SVAL_UPDATE_STORAGE_FORMAT;
    m.updater_proto  = UPDATE_PROTOCOL_VERSION;
    m.image_len      = 0x400;
    update_device_t dev = {.hand = UPDATE_HAND_RIGHT, .pointing_id = UPDATE_POINTING_PMW3389, .storage_format = SVAL_UPDATE_STORAGE_FORMAT, .release_build = true, .max_image = SVAL_UPDATE_MAX_IMAGE};
    m.flags = UPDATE_FLAG_RELEASE;
    CHECK(update_manifest_check(&m, &dev) == UPDATE_OK);
    m.flags = 0;
    CHECK(update_manifest_check(&m, &dev) == UPDATE_OK); // RELEASE is a label; anyone's build is taken
    m.flags = UPDATE_FLAG_RELEASE | UPDATE_FLAG_DIAGNOSTIC;
    CHECK(update_manifest_check(&m, &dev) == UPDATE_BAD_IMAGE); // a release build takes no DIAGNOSTIC image
    m.flags = UPDATE_FLAG_DIAGNOSTIC;
    CHECK(update_manifest_check(&m, &dev) == UPDATE_BAD_IMAGE);
    m.flags        = UPDATE_FLAG_RELEASE;
    m.manifest_ver = 1;
    CHECK(update_manifest_check(&m, &dev) == UPDATE_UNSUPPORTED); // version 1 (signed) manifests
    m.manifest_ver = UPDATE_MANIFEST_VERSION;
    for (unsigned k = 1; k < 256; k++) {
        m.reserved0 = (uint8_t)k; // version 1's key_id
        CHECK(update_manifest_check(&m, &dev) == UPDATE_INVALID);
    }

    // ---- .svup files from outside ----------------------------------------------------------
    int files = 0;
    for (int i = 1; i < argc; i++) {
        char  path[4096];
        char *colon = strrchr(argv[i], ':');
        if (!colon || (strcmp(colon + 1, "ok") && strcmp(colon + 1, "refused"))) {
            fprintf(stderr, "%s: expected FILE.svup:ok or FILE.svup:refused\n", argv[i]);
            return 2;
        }
        snprintf(path, sizeof(path), "%.*s", (int)(colon - argv[i]), argv[i]);
        size_t   len;
        uint8_t *buf = read_file(path, &len);
        CHECK(buf != NULL);
        if (!buf) continue;
        update_status_t st   = release_check(buf, len);
        int             want = strcmp(colon + 1, "ok") == 0;
        printf("  %s: %s (status %d), expected %s\n", path, st == UPDATE_OK ? "accepted" : "refused", st, colon + 1);
        CHECK((st == UPDATE_OK) == want);
        files++;
        free(buf);
    }

    printf("release build: %d checks, %d failures, %d .svup files\n", checks, failures, files);
    return failures ? 1 : 0;
}
