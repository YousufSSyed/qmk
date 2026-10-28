/*
Copyright 2026 Morgan Venable @_claussen

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include <stdint.h>
#include <stdbool.h>

/*
 * Main board identity, read at run time so one image runs on every main board.
 *
 * GP3 tells the boards apart with no strap of its own:
 *   SvalMCUMini V1.0  R1 30k from GP3 to GND (and nothing else on the net).
 *   V3.1 / V3.2       GP3 is the output of an op-amp follower at AMP_REF (2.6 V).
 *   V3.0 / V2B        GP3 reaches only a header: it floats.
 *
 * The read never relies on the internal pull-up against the Mini's 30k (that
 * lands near 1 V, inside the input's undefined band). Two robust reads decide:
 *   1. Pull-down: high only when something drives the pin (V3.1 / V3.2).
 *   2. Otherwise drive the pin high for 5 us, release it with no pull and read
 *      20 us later: the Mini's 30k has pulled it to 0 V (time constant ~0.3 us);
 *      a floating pin is still high.
 * Eight samples must agree, or the board is "not identified" and keeps the
 * regular (V3.x) scan timing, which also works on the Mini.
 *
 * On a Mini, the boot also checks each cluster's emitters (cluster check):
 * a row's sensors light within 0.3 us on a SvalMCUMini cluster and only after
 * ~150 us on an original 330 ohm cluster, which the Mini cannot scan.
 */

// Wire values (Sval 0x38 MAIN_BOARD, keyboards/svalboard/docs/protocol.md).
enum sval_main_board {
    SVAL_MAIN_BOARD_NOT_IDENTIFIED = 0,
    SVAL_MAIN_BOARD_MINI_V1_0      = 1, // SvalMCUMini V1.0
    SVAL_MAIN_BOARD_V3_1_V3_2      = 2, // GP3 driven high: V3.1 or V3.2
    SVAL_MAIN_BOARD_V3_0_V2B       = 3, // GP3 floats: V3.0 or V2B
};

// Per-position cluster class, 2 bits each (row 0 thumb, rows 1-4 index..pinky).
enum sval_cluster_class {
    SVAL_CLUSTER_NOT_CHECKED = 0, // not a Mini, or the reads disagreed
    SVAL_CLUSTER_FAST        = 1, // lit within 60 us: a cluster made for the Mini
    SVAL_CLUSTER_ORIGINAL    = 2, // dark at 60 us, lit by 400 us: original 330 ohm cluster
    SVAL_CLUSTER_DARK        = 3, // dark at both: no cluster, or every lit-at-rest key held
};

#define SVAL_BOARD_REPORTED     0x01 // the block holds a report
#define SVAL_BOARD_LEFT         0x02 // the half is the left half (as in Sval 0x2C)
#define SVAL_BOARD_CHECK_RAN    0x04 // the cluster check ran (Mini only)
#define SVAL_BOARD_UNSUPPORTED  0x08 // a cluster this main board can't scan was found
#define SVAL_BOARD_SIDE_UNKNOWN 0x20 // either-hand builds: no side yet (as in Sval 0x2C)

typedef struct __attribute__((__packed__)) {
    uint8_t  flags;      // SVAL_BOARD_*
    uint8_t  main_board; // enum sval_main_board
    uint8_t  gp3;        // bit0 pull-down read, bit1 pull-up read, bit2 release read (last sample); bits 4-7 samples agreeing (0-8)
    uint16_t clusters;   // 2 bits per row, row 0 in bits 0-1 (enum sval_cluster_class)
} sval_board_identity_t;

#define SVAL_BOARD_RPC_MAGIC 0x42 // 'B': the presence reply carries a board block

uint8_t sval_main_board(void);                          // cached; first call reads GP3
void    sval_cluster_check(void);                       // matrix_init_custom: Mini only, before the first scan
uint8_t sval_muted_rows(void);                          // bit per row (row 0 = bit 0): original cluster, keys muted
void    sval_local_board_identity(sval_board_identity_t *id);
void    sval_other_board_set(const sval_board_identity_t *id); // master: from the presence reply, NULL = none
void    sval_main_board_kb(uint8_t *out, uint8_t length); // Sval 0x38 reply body
