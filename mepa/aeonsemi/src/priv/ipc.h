// Copyright (c) 2004-2026 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#ifndef AS2XXXX_IPC_H_
#define AS2XXXX_IPC_H_

#include <stdint.h>
#include <stdbool.h>

#include "microchip/ethernet/phy/api/types.h"
#include "as2xxxx_registers.h"

/** @brief Number of IPC data registers */
#define IPC_DATA_REG_CNT (AS2XXXX_REG_ADDR_IPC_DATA_LAST - AS2XXXX_REG_ADDR_IPC_DATA0 + 1)

// ------------------------------------------------------------------------------------------------
// Typedefs
// ------------------------------------------------------------------------------------------------

/** @brief Per-device IPC state.
 *
 * Passed in by the caller rather than looked up from the device, so that this
 * layer needs no knowledge of where the caller keeps it. Zero is the correct
 * initial value for every member. */
typedef struct {
    mepa_bool_t parity_status;
} ipc_state_t;

/** @brief Data struct for IPC procedures representing 8 IPC data registers on the PHY */
typedef struct {
    uint16_t buf[IPC_DATA_REG_CNT];
    uint8_t  len;
} ipc_data_reg_t;

/** @brief IPC message */
typedef struct {
    as2xxxx_ipc_cmd_t opcode;
    mepa_bool_t       get_response;
    ipc_data_reg_t    io_data; // The same for in and out data
} ipc_msg_t;

/** @brief IPC operation (a.k.a. dbg command)
 *
 * cmd entry should be filled with \ref as2xxxx_ipc_dbgcmd_subcmd_t entries with
 * AS2XXXX_IPC_DBGCMD_SUBCMD_ prefixes.
 *
 * subcmd entry should be filled with corresponding subcommands for the cmd entry,
 * such as AS2XXXX_IPC_DBGCMD_<NAME>_SUBCMD_* values.
 *
 * out_data can be much more than what \ref ipc_data_reg_t can delver. Populate with sufficient
 * buffer and expected size of data to be read from the PHY.
 */
typedef struct {
    uint8_t        cmd;
    uint8_t        subcmd;
    ipc_data_reg_t in_data;

    struct ipc_op_data {
        // Populate buf, buf_words_max and expected_words
        uint16_t *buf;
        size_t    buf_words_max;
        size_t    expected_words;
        // Read this after operation to get the actual read words
        size_t actual_read_words;
    } out_data;

} ipc_operation_t;

// ------------------------------------------------------------------------------------------------
// APIs
// ------------------------------------------------------------------------------------------------

/**
 * @brief Do the IPC operation, also known as DBG command.
 * @note Top level command for ipc operation is always \ref AS2XXXX_IPC_CMD_DBGCMD and it is set
 *       internally by the \ref ipc_do_operation. A user must fill up the \ref ipc_operation_t.
 * @param[in] dev The device.
 * @param[in,out] state The device's IPC state.
 * @param[in] op The operation to be done. If out_data.buf is set, it is filled with the data read
 *               back from the PHY and out_data.actual_read_words says how much arrived.
 * @return MEPA_RC_OK on success, error code otherwise
 */
mepa_rc ipc_do_operation(mepa_device_t *const   dev,
                         ipc_state_t *const     state,
                         ipc_operation_t *const op);

/**
 * @brief Send IPC message.
 * @note Sending messages was a default way of doing IPC stuff before introducing \ref
 * ipc_do_operation.
 * @param[in] dev The device.
 * @param[in,out] state The device's IPC state.
 * @param[in] msg The message to be sent. If \ref get_response from \ref ipc_msg_t is set,
 *                the data entry from \ref ipc_msg_t will be filled with data read from the PHY.
 * @return MEPA_RC_OK on success, error code otherwise
 */
mepa_rc ipc_send_msg(mepa_device_t *const dev, ipc_state_t *const state, ipc_msg_t *const msg);

/**
 * @brief Sync parity of the IPC operations.
 * @note IPC operations use a single parity bit that must alternate from operation to operation.
 *       This is a helper function to sync parity on the boot so that following operation have
 *       a correct parity and are successful.
 * @param[in] dev The device.
 * @param[in,out] state The device's IPC state, reset to the unsynced value.
 * @return MEPA_RC_OK on success, error code otherwise
 */
mepa_rc ipc_sync_parity(mepa_device_t *const dev, ipc_state_t *const state);

/**
 * @brief Send no op command
 * @note This is required after the boot to make sure the IPC is up and running.
 * @param[in] dev The device.
 * @param[in,out] state The device's IPC state.
 * @return MEPA_RC_OK on success, error code otherwise
 */
mepa_rc ipc_send_noop_cmd(mepa_device_t *const dev, ipc_state_t *const state);

#endif /* AS2XXXX_IPC_H_ */
