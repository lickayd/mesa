// Copyright (c) 2004-2026 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#ifndef AS2XXXX_EYE_H_
#define AS2XXXX_EYE_H_

#include <lm_utils.h>

#include "microchip/ethernet/phy/api/types.h"
#include "ipc.h" // For the ipc_state_t passed through to the IPC layer

// ------------------------------------------------------------------------------------------------
// Typedefs
// ------------------------------------------------------------------------------------------------

/** @brief Which byte of a scan request carries the group index. */
typedef enum {
    EYE_REQ_GRP_HIGH_BYTE = 0, /**< AS21xxx: group in the high byte, SerDes in the low */
    EYE_REQ_GRP_LOW_BYTE = 1   /**< AS22xxx: group in the low byte, SerDes in the high */
} eye_req_order_t;

/** @brief EYE data structure. */
typedef struct {
    lmu_ss_t       *ss;        /**< String stream the eye diagram is printed to */
    eye_req_order_t req_order; /**< Request byte order for the chip family */
    /** Called after every fetched group, NULL to disable. The string stream is only
     *  flushed once the caller is done with it, so this is the only way to report
     *  progress while the scan runs. */
    void (*progress_func)(size_t done_groups, size_t total_groups);
} eye_data_t;

// ------------------------------------------------------------------------------------------------
// APIs
// ------------------------------------------------------------------------------------------------

/**
 * @brief Print the eye diagram.
 * @param[in] dev The device.
 * @param[in,out] state The device's IPC state, forwarded to the IPC layer.
 * @param[in] eye_data Eye data structure with initialized buffers and print function.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 * @note This is a long-running operation. It is advised to call this function under lock.
 */
mepa_rc eye_print(mepa_device_t *const dev, ipc_state_t *const state, eye_data_t *const eye_data);

#endif /* EYE_H_ */
