// Copyright (c) 2004-2026 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

// SerDes receive eye diagram: scan the grid, print it as ASCII.
//
// AS2xxxx Firmware rules:
//  - Ask for groups in ascending order, once each. Word 0 of a reply is only the request echoed
//    back, so a misread request still returns a full and plausible group - for the same phase
//    every time.
//  - The families put the group number in opposite bytes of the request, see eye_req_order_t.
//  - Read every word of a group, or the rest stays in the firmware's buffer and breaks every
//    later IPC operation.
//  - Ask for the geometry after the scan; asking first changes what the scan returns.
//

#include <stdint.h>
#include <string.h>

#include "eye.h"
#include "ipc.h"
#include "mepa_driver.h"
#include "mepa_utils.h"
#include "microchip/ethernet/phy/api/types.h"

// ------------------------------------------------------------------------------------------------
// Configuration
// ------------------------------------------------------------------------------------------------

/** @brief Resolution, chosen at compile time because every buffer is sized from it.
 *  Options:
 *      - Low  - (default) answers the usual question - is the eye open. Needs about 6.7kB of stack
 *               memory and half the time of the full resolution.
 *      - Full - costs about 33.4kB of stack and, together with EYE_RAW_HEX_DUMP, produces a
 *               capture that an external plotting tool can reshape. */
#define EYE_FULL_RESOLUTION 0

/** @brief Dump every group verbatim so a capture can be replotted elsewhere.
 *  Log the console output and cut the block between the EYE_RAW markers - see the README. */
#define EYE_RAW_HEX_DUMP 0

#if EYE_RAW_HEX_DUMP && !EYE_FULL_RESOLUTION
// The dump format carries no geometry, so it is only reshapable at the full 124x254 size.
#error "EYE_RAW_HEX_DUMP requires EYE_FULL_RESOLUTION"
#endif

// ------------------------------------------------------------------------------------------------
// Hardware constants
// ------------------------------------------------------------------------------------------------

/** @brief SerDes carrying the host lane, 0 on both families. */
#define EYE_SDS_ID 0
/** @brief Phase groups in one scan. */
#define EYE_PHASE_GROUPS 31
/** @brief Phases in one group, each a full column of voltages. */
#define EYE_PHASES_PER_GROUP 4
/** @brief Voltage steps in one column. */
#define EYE_VOLTAGE_STEPS 254
/** @brief Words in one group, all of which a fetch must read. */
#define EYE_GROUP_WORDS (EYE_PHASES_PER_GROUP * EYE_VOLTAGE_STEPS) // = 1016
/** @brief Word of a group holding the echoed request instead of a count. */
#define EYE_GROUP_TAG_WORD 0
/** @brief Comparisons made at each point; the firmware and its readback call this nsamp. */
#define EYE_COMPARES_PER_POINT 1024
/** @brief Fields the geometry readback returns: groups, phases per group, voltages, compares. */
#define EYE_CFG_GET_FIELDS 4
/** @brief 16-bit halves each field arrives in, low half first. */
#define EYE_CFG_HALVES_PER_FIELD 2
/** @brief Words the readback returns. */
#define EYE_CFG_GET_WORDS (EYE_CFG_GET_FIELDS * EYE_CFG_HALVES_PER_FIELD)

// ------------------------------------------------------------------------------------------------
// Algorithm constants (Do not change unless you know what you're doing)
// ------------------------------------------------------------------------------------------------

/** @brief Counts are divided by this before being stored, so that one fits in a byte. */
#define EYE_COUNT_SCALE 16

// ------------------------------------------------------------------------------------------------
// Derived values
// ------------------------------------------------------------------------------------------------

#if EYE_FULL_RESOLUTION
/** @brief Step between the phase groups fetched. */
#define EYE_GROUP_STEP 1
/** @brief Step between the voltages kept. */
#define EYE_VOLTAGE_STEP 1
/** @brief Step between the rows printed, keeping the picture near 32 lines. */
#define EYE_PRINT_ROW_STEP 8
/** @brief Resolution name, for the heading. */
#define EYE_RES_NAME "full"
#else
/** @brief Step between the phase groups fetched. */
#define EYE_GROUP_STEP     2
/** @brief Step between the voltages kept. */
#define EYE_VOLTAGE_STEP   4
/** @brief Step between the rows printed, keeping the picture near 32 lines. */
#define EYE_PRINT_ROW_STEP 2
/** @brief Resolution name, for the heading. */
#define EYE_RES_NAME       "low"
#endif

/** @brief Groups actually fetched, rounded up so a last partial step still gets one. */
#define EYE_GROUPS_FETCHED ((EYE_PHASE_GROUPS + EYE_GROUP_STEP - 1) / EYE_GROUP_STEP)
/** @brief Picture width, one column per fetched phase. */
#define EYE_WIDTH (EYE_GROUPS_FETCHED * EYE_PHASES_PER_GROUP)
/** @brief Picture height, rounded up so a last partial step still gets a row. */
#define EYE_HEIGHT ((EYE_VOLTAGE_STEPS + EYE_VOLTAGE_STEP - 1) / EYE_VOLTAGE_STEP)
/** @brief Points in the picture, and so the size of the grid in bytes. */
#define EYE_POINTS (EYE_WIDTH * EYE_HEIGHT)
/** @brief Largest count once divided by EYE_COUNT_SCALE. */
#define EYE_COUNT_MAX (EYE_COMPARES_PER_POINT / EYE_COUNT_SCALE)
/** @brief Grid value for a point that could not be measured, above every real count. */
#define EYE_COUNT_UNMEASURED UINT8_MAX

#if (EYE_COMPARES_PER_POINT / EYE_COUNT_SCALE) >= UINT8_MAX
// A scaled count has to stay below the sentinel, or a measured point reads as unmeasured.
#error "EYE_COUNT_SCALE is too small for a scaled count to fit beside EYE_COUNT_UNMEASURED"
#endif

// ------------------------------------------------------------------------------------------------
// Typedefs
// ------------------------------------------------------------------------------------------------

/** @brief Struct for passing data internally. */
typedef struct {
    uint16_t       *group_buffer; // One group as fetched, EYE_GROUP_WORDS words
    uint8_t        *grid;         // Stored counts, one per point, [y * EYE_WIDTH + x]
    lmu_ss_t       *ss;
    eye_req_order_t req_order;
    size_t          point_count; // Points measured, so the population the percentiles use
    size_t          tag_errors;  // Groups that answered for the wrong phase
    size_t          count_seen[EYE_COUNT_MAX + 1]; // How many points hold each stored count
} eye_data_internal_t;

// ------------------------------------------------------------------------------------------------
// Local function definitions
// ------------------------------------------------------------------------------------------------

static uint16_t eye_request_word(const eye_data_internal_t *const eye_data, size_t group_idx);
static mepa_rc  fetch_group(mepa_device_t *const       dev,
                            ipc_state_t *const         state,
                            size_t                     group_idx,
                            eye_data_internal_t *const eye_data);
static void     store_group(size_t fetch_idx, eye_data_internal_t *const eye_data);
static void     verify_geometry(mepa_device_t *const             dev,
                                ipc_state_t *const               state,
                                const eye_data_internal_t *const eye_data);
static uint8_t  count_at_percentile(const eye_data_internal_t *const eye_data, size_t pct);
static uint8_t  pick_threshold(const eye_data_internal_t *const eye_data);
static void     print_eye(const eye_data_internal_t *const eye_data);
#if EYE_RAW_HEX_DUMP
static void dump_group_hex(const eye_data_internal_t *const eye_data);
#endif

// ------------------------------------------------------------------------------------------------
// Public functions
// ------------------------------------------------------------------------------------------------

mepa_rc eye_print(mepa_device_t *const dev, ipc_state_t *const state, eye_data_t *const eye_data)
{
    eye_data_internal_t eye_data_int = {0};
    size_t              group_idx;
    size_t              fetch_idx;
    uint16_t            expected_tag;
    uint16_t            group_buffer[EYE_GROUP_WORDS];
    uint8_t             grid[EYE_POINTS];
    lmu_ss_t *const     ss = eye_data->ss;

    eye_data_int.group_buffer = group_buffer;
    eye_data_int.grid = grid;
    eye_data_int.ss = eye_data->ss;
    eye_data_int.req_order = eye_data->req_order;

    // A group that fails to fetch is then drawn closed.
    memset(grid, EYE_COUNT_UNMEASURED, sizeof(grid));

#if EYE_RAW_HEX_DUMP
    pr("EYE_RAW_BEGIN xres %u yres %u groups %u\n", (uint32_t)EYE_WIDTH,
       (uint32_t)EYE_VOLTAGE_STEPS, (uint32_t)EYE_GROUPS_FETCHED);
#endif

    // Report before the first fetch.
    if (eye_data->progress_func != NULL) {
        eye_data->progress_func(0, EYE_GROUPS_FETCHED);
    }

    // Ascending, one fetch per group, which the firmware requires - see the file header.
    for (fetch_idx = 0; fetch_idx < EYE_GROUPS_FETCHED; fetch_idx++) {

        group_idx = fetch_idx * EYE_GROUP_STEP;

        if (fetch_group(dev, state, group_idx, &eye_data_int) != MEPA_RC_OK) {
            pr(" Failed to fetch eye group %zu, drawing its columns closed\n", group_idx);

#if EYE_RAW_HEX_DUMP
            // Dumped anyway so the token count stays exact and the file can still be reshaped.
            // All-ones reads as closed there just as it does here.
            memset(group_buffer, 0xFF, sizeof(group_buffer));
            dump_group_hex(&eye_data_int);
#endif
            continue;
        }

        // The reply only echoes the request, see the file header.
        expected_tag = eye_request_word(&eye_data_int, group_idx);
        if (eye_data_int.group_buffer[EYE_GROUP_TAG_WORD] != expected_tag) {
            pr("  Group %zu returned tag 0x%04x, expected 0x%04x\n", group_idx,
               (uint32_t)eye_data_int.group_buffer[EYE_GROUP_TAG_WORD], (uint32_t)expected_tag);
            eye_data_int.tag_errors++;
        }

#if EYE_RAW_HEX_DUMP
        dump_group_hex(&eye_data_int);
#endif

        store_group(fetch_idx, &eye_data_int);

        if (eye_data->progress_func != NULL) {
            eye_data->progress_func(fetch_idx + 1, EYE_GROUPS_FETCHED);
        }
    }

#if EYE_RAW_HEX_DUMP
    pr("EYE_RAW_END\n");
#endif

    verify_geometry(dev, state, &eye_data_int);

    pr("\n");

    // A diagram drawn from no measurements would be closed throughout, so it would read as a shut
    // eye. Draw nothing and leave the caller to report the failure.
    if (eye_data_int.point_count == 0u) {
        pr("  Nothing was measured, so there is no diagram to draw\n");
        return MEPA_RC_ERROR;
    }

    if (eye_data_int.tag_errors != 0u) {
        pr("  Warning: %zu of %u groups answered for the wrong phase, so those columns of the "
           "picture are misplaced\n",
           eye_data_int.tag_errors, (uint32_t)EYE_GROUPS_FETCHED);
    }
    pr("  Eye diagram (%s resolution, %ux%u)\n", EYE_RES_NAME, (uint32_t)EYE_WIDTH,
       (uint32_t)EYE_HEIGHT);
    print_eye(&eye_data_int);

    return MEPA_RC_OK;
}

// ------------------------------------------------------------------------------------------------
// Local functions
// ------------------------------------------------------------------------------------------------

static uint16_t eye_request_word(const eye_data_internal_t *const eye_data, size_t group_idx)
{
    uint16_t word;

    // The families order the two request bytes differently, see eye_req_order_t.
    if (eye_data->req_order == EYE_REQ_GRP_LOW_BYTE) {
        word = FIELD_PREP16(U16_HIGH_BYTE, EYE_SDS_ID) | FIELD_PREP16(U16_LOW_BYTE, group_idx);
    } else {
        word = FIELD_PREP16(U16_HIGH_BYTE, group_idx) | FIELD_PREP16(U16_LOW_BYTE, EYE_SDS_ID);
    }

    return word;
}

static mepa_rc fetch_group(mepa_device_t *const       dev,
                           ipc_state_t *const         state,
                           size_t                     group_idx,
                           eye_data_internal_t *const eye_data)
{
    ipc_operation_t op = {0};
    // Firmware needs more than one IPC transaction to produce a group.
    const size_t   max_attempts = 16;
    const uint32_t retry_delay_ms = 50;
    size_t         attempt;
    size_t         words_read = 0;

    if (group_idx >= EYE_PHASE_GROUPS) {
        return MEPA_RC_ERR_PARM;
    }

    op.cmd = AS2XXXX_IPC_DBGCMD_SUBCMD_SDS;
    op.subcmd = AS2XXXX_IPC_DBGCMD_SDS_SUBCMD_EYE_SCAN;
    op.in_data.buf[0] = eye_request_word(eye_data, group_idx);
    op.in_data.len = 1;

    for (attempt = 0; attempt < max_attempts; attempt++) {

        // A short reply leaves the mailbox out of step, and without a resync here every later
        // operation on this device fails, not just the eye scan.
        (void)ipc_sync_parity(dev, state);

        // Continue where the last attempt stopped.
        op.out_data.buf = eye_data->group_buffer + words_read;
        op.out_data.buf_words_max = EYE_GROUP_WORDS - words_read;
        op.out_data.expected_words = EYE_GROUP_WORDS - words_read;

        if (ipc_do_operation(dev, state, &op) == MEPA_RC_OK) {
            words_read += op.out_data.actual_read_words;

            if (words_read >= EYE_GROUP_WORDS) {
                return MEPA_RC_OK;
            }
        }

        MEPA_MSLEEP(retry_delay_ms);
    }

    return MEPA_RC_ERROR;
}

static void store_group(size_t fetch_idx, eye_data_internal_t *const eye_data)
{
    size_t   phase, x, y, y_out, word, point;
    uint16_t error_count;
    uint8_t  scale_cnt;

    for (phase = 0; phase < EYE_PHASES_PER_GROUP; phase++) {

        x = (fetch_idx * EYE_PHASES_PER_GROUP) + phase;
        y_out = 0;

        for (y = 0; y < EYE_VOLTAGE_STEPS; y += EYE_VOLTAGE_STEP, y_out++) {

            word = (phase * EYE_VOLTAGE_STEPS) + y;
            error_count = eye_data->group_buffer[word];

            // The echoed request, not a count. It has to be skipped by position.
            if (word == EYE_GROUP_TAG_WORD) {
                continue;
            }

            // A count cannot exceed the comparisons made, so anything larger is not a count.
            if (error_count > EYE_COMPARES_PER_POINT) {
                continue;
            }

            // Range checked just above, so this cannot scale past the end of count_seen[].
            scale_cnt = (uint8_t)(error_count / EYE_COUNT_SCALE);
            point = (y_out * EYE_WIDTH) + x;

            eye_data->grid[point] = scale_cnt;
            eye_data->count_seen[scale_cnt]++;
            eye_data->point_count++;
        }
    }
}

static void verify_geometry(mepa_device_t *const             dev,
                            ipc_state_t *const               state,
                            const eye_data_internal_t *const eye_data)
{
    static const char *const field_name[EYE_CFG_GET_FIELDS] = {"groups", "phases/group", "voltages",
                                                               "compares/point"};
    const uint32_t           expected[EYE_CFG_GET_FIELDS] = {EYE_PHASE_GROUPS, EYE_PHASES_PER_GROUP,
                                                             EYE_VOLTAGE_STEPS, EYE_COMPARES_PER_POINT};
    ipc_operation_t          cfg = {0};
    uint16_t                 cfg_buf[EYE_CFG_GET_WORDS] = {0};
    lmu_ss_t *const          ss = eye_data->ss;
    size_t                   i;
    size_t                   half;
    uint32_t                 reported;

    // Only AS22xxx implements this, so a failure means nothing was checked rather than that
    // something is wrong. Asked after the scan, never before - see the file header.
    cfg.cmd = AS2XXXX_IPC_DBGCMD_SUBCMD_SDS;
    cfg.subcmd = AS2XXXX_IPC_DBGCMD_SDS_SUBCMD_EYE_SCAN_CFG_GET;
    cfg.in_data.buf[0] = EYE_SDS_ID;
    cfg.in_data.len = 1;
    cfg.out_data.buf = cfg_buf;
    cfg.out_data.buf_words_max = EYE_CFG_GET_WORDS;
    cfg.out_data.expected_words = EYE_CFG_GET_WORDS;

    (void)ipc_sync_parity(dev, state);

    if (ipc_do_operation(dev, state, &cfg) != MEPA_RC_OK) {
        return;
    }
    if (cfg.out_data.actual_read_words < EYE_CFG_GET_WORDS) {
        return;
    }

    for (i = 0; i < EYE_CFG_GET_FIELDS; i++) {
        half = i * EYE_CFG_HALVES_PER_FIELD;
        // Each field arrives as two 16-bit halves, low half first.
        reported = FIELD_PREP32(U32_LOW_WORD, cfg_buf[half]) |
                   FIELD_PREP32(U32_HIGH_WORD, cfg_buf[half + 1u]);

        if (reported != expected[i]) {
            pr("  Firmware reports %s %u, driver assumes %u\n", field_name[i], (uint32_t)reported,
               (uint32_t)expected[i]);
        }
    }
}

static uint8_t count_at_percentile(const eye_data_internal_t *const eye_data, size_t pct)
{
    // The stored count below which the given share of the measured points falls.
    const size_t percent = 100;
    const size_t target = (eye_data->point_count * pct) / percent;
    size_t       seen = 0;
    size_t       i;

    for (i = 0; i <= EYE_COUNT_MAX; i++) {
        seen += eye_data->count_seen[i];
        if (seen >= target) {
            break;
        }
    }

    return (uint8_t)i;
}

static uint8_t pick_threshold(const eye_data_internal_t *const eye_data)
{
    const size_t open_pct = 5;
    const size_t closed_pct = 95;
    uint8_t      open_count;
    uint8_t      closed_count;

    if (eye_data->point_count == 0u) {
        return 0;
    }

    open_count = count_at_percentile(eye_data, open_pct);
    closed_count = count_at_percentile(eye_data, closed_pct);

    // The closed count came out no higher than the open one, so nearly every point holds the same
    // count and nothing separates open from closed. Only a point with zero errors is drawn open
    // then, otherwise a uniformly bad lane would come out as a full eye.
    if (closed_count <= open_count) {
        return 0;
    }

    // Halfway rather than just above the open count: points inside the eye do not all hold the
    // same count, they vary around it, and AS22xxx varies the most. A threshold close to the open
    // count leaves some of those points above it, drawing them closed and speckling the eye.
    return (uint8_t)(open_count + ((closed_count - open_count) / 2u));
}

static void print_eye(const eye_data_internal_t *const eye_data)
{
    lmu_ss_t *const ss = eye_data->ss;
    char            row_str[EYE_WIDTH + 1];
    const uint8_t   threshold = pick_threshold(eye_data);
    const size_t    top_row = EYE_HEIGHT - 1u;
    const size_t    rows = (top_row / EYE_PRINT_ROW_STEP) + 1u;
    size_t          row, x, y, point;

    for (row = 0; row < rows; row++) {
        // Highest voltage first. Counted upward and subtracted, because an unsigned index counting
        // down past 0 would wrap and read far outside the grid.
        y = top_row - (row * EYE_PRINT_ROW_STEP);
        for (x = 0; x < EYE_WIDTH; x++) {
            point = (y * EYE_WIDTH) + x;
            row_str[x] = (eye_data->grid[point] > threshold) ? '1' : '0';
        }
        row_str[EYE_WIDTH] = '\0';
        pr("%-6zu%s\n", y, row_str);
    }
}

#if EYE_RAW_HEX_DUMP
static void dump_group_hex(const eye_data_internal_t *const eye_data)
{
    lmu_ss_t *const ss = eye_data->ss;
    const size_t    tokens_per_line = 16;
    size_t          k;

    for (k = 0; k < EYE_GROUP_WORDS; k++) {
        pr(" 0x%04x", eye_data->group_buffer[k]);
        if ((k % tokens_per_line) == (tokens_per_line - 1u)) {
            pr("\n");
        }
    }
    pr("\n");
}
#endif
