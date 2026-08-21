// Copyright (c) 2004-2026 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#include "mepa_driver.h"
#include "mepa_utils.h" // For MEPA_RC

#include "as2xxxx_utils.h"

#include "mdio.h"
#include "microchip/ethernet/phy/api/types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ipc.h"

// ------------------------------------------------------------------------------------------------
// Logging: this file logs as the IPC module, see as2xxxx_utils.h
// ------------------------------------------------------------------------------------------------

#define LOG_W(fmt, ...)        AS2XXXX_LOG_W(IPC, fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...)        AS2XXXX_LOG_E(IPC, fmt, ##__VA_ARGS__)
#define RC_LOG(expr, msg, ...) AS2XXXX_RC_LOG(IPC, expr, msg, ##__VA_ARGS__)

// ------------------------------------------------------------------------------------------------
// Macros
// ------------------------------------------------------------------------------------------------

/** @brief Converts milli- to micro-seconds */
#define MS_TO_US(val) ((val) * 1000)
/** @brief Max data length for IPC message */
#define IPC_DATA_MAX_RX_LEN (IPC_DATA_REG_CNT * sizeof(uint16_t))

/** @brief How long to wait for IPC command processing in microseconds. */
#define IPC_CMD_PROCESS_DELAY_US MS_TO_US(20)
/** @brief How often to poll for IPC command completion in microseconds. */
#define IPC_CMD_WAIT_POLL_RATE_US MS_TO_US(10)
/** @brief How many time retry the IPC command completion poll before giving up */
#define IPC_CMD_WAIT_POLL_RETRY_CNT 200u

/** @brief Integer division with rounding up. */
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

// ------------------------------------------------------------------------------------------------
// Local function definitions
// ------------------------------------------------------------------------------------------------

// Helper function for the "do operation" procedure.
static mepa_rc send_dbgcmd(mepa_device_t *const         dev,
                           ipc_state_t *const           state,
                           const ipc_operation_t *const op);
static mepa_rc send_wbuf(mepa_device_t *const         dev,
                         ipc_state_t *const           state,
                         const ipc_operation_t *const op);
static mepa_rc do_poll(mepa_device_t *const dev, ipc_state_t *const state);
static mepa_rc get_rbuf(mepa_device_t *const   dev,
                        ipc_state_t *const     state,
                        ipc_operation_t *const op);
// Other helpers
static mepa_rc     send_noop(mepa_device_t *const dev,
                             ipc_state_t *const   state,
                             uint16_t *const      ret_status);
static mepa_rc     send_cmd(mepa_device_t *const dev,
                            ipc_state_t *const   state,
                            uint16_t             cmd,
                            uint16_t *const      ret_status);
static mepa_rc     wait_cmd(mepa_device_t *const dev, mepa_bool_t parity_status);
static mepa_bool_t is_settled(uint16_t ipc_status_reg_value, mepa_bool_t parity_status);
static const char *status2string(uint16_t ipc_status_reg_value);

// ------------------------------------------------------------------------------------------------
// Public functions
// ------------------------------------------------------------------------------------------------

mepa_rc ipc_do_operation(mepa_device_t *const   dev,
                         ipc_state_t *const     state,
                         ipc_operation_t *const op)
{
    mepa_rc rc = MEPA_RC_OK;

    NULL_CHECK(op);

    RC_LOG(send_dbgcmd(dev, state, op), "Failed to send dbg cmd");
    RC_LOG(send_wbuf(dev, state, op), "Failed to send wbuf");
    RC_LOG(do_poll(dev, state), "Failed to do poll");

    if (op->out_data.buf != NULL) {
        RC_LOG(get_rbuf(dev, state, op), "Failed to get rbuf");
    }

    return MEPA_RC_OK;
}

mepa_rc ipc_send_msg(mepa_device_t *const dev, ipc_state_t *const state, ipc_msg_t *const msg)
{
    size_t   i, ret_size, out_reg_cnt;
    uint16_t cmd, ret_status, word;

    if (msg == NULL || msg->io_data.len > IPC_DATA_REG_CNT) {
        return MEPA_RC_ERR_PARM;
    }

    // Fill up data registers with message data
    for (i = 0; i < msg->io_data.len; i++) {
        MEPA_RC(mdio_mmd_vendor_wr(dev, AS2XXXX_REG_ADDR_IPC_DATAn(i), msg->io_data.buf[i]));
    }

    // Build and send command. Size in bytes is expected
    cmd = FIELD_PREP16(AS2XXXX_REG_MASK_IPC_CMD_SIZE, (msg->io_data.len * sizeof(uint16_t))) |
          FIELD_PREP16(AS2XXXX_REG_MASK_IPC_CMD_OPCODE, msg->opcode);

    MEPA_RC(send_cmd(dev, state, cmd, &ret_status));
    if ((ret_status & AS2XXXX_REG_MASK_IPC_STS_STATUS) == AS2XXXX_IPC_STS_STATUS_ERROR) {
        LOG_E("IPC opcode 0x%02x rejected, status register 0x%04x (%s)", (unsigned)msg->opcode,
              ret_status, status2string(ret_status));
        return MEPA_RC_ERROR;
    }

    // Get response if requested
    if (msg->get_response) {

        ret_size = FIELD_GET16(AS2XXXX_REG_MASK_IPC_STS_SIZE, ret_status);
        if (ret_size > IPC_DATA_MAX_RX_LEN) {
            LOG_E("Returned response size is more than max len. ret_size = %u, max_size = %u",
                  (uint32_t)ret_size, (uint32_t)IPC_DATA_MAX_RX_LEN);
            return MEPA_RC_ERROR;
        }

        out_reg_cnt = DIV_ROUND_UP(ret_size, sizeof(uint16_t));
        for (i = 0; i < out_reg_cnt; i++) {
            MEPA_RC(mdio_mmd_vendor_rd(dev, AS2XXXX_REG_ADDR_IPC_DATAn(i), &word));
            msg->io_data.buf[i] = word;
        }

        msg->io_data.len = out_reg_cnt;
    }

    return MEPA_RC_OK;
}

mepa_rc ipc_sync_parity(mepa_device_t *const dev, ipc_state_t *const state)
{
    uint16_t status = 0;

    // Send NOP with no parity - ignore return code
    (void)send_noop(dev, state, NULL);

    // Reset packet parity
    state->parity_status = false;

    // Send second NOP with no parity - ignore return code, might be errors due to parity not sync.
    // The status is asked for and then discarded: passing a pointer is what makes send_cmd wait for
    // the command to complete, which is the point of this second NOP.
    (void)send_noop(dev, state, &status);

    return MEPA_RC_OK;
}

mepa_rc ipc_send_noop_cmd(mepa_device_t *const dev, ipc_state_t *const state)
{
    uint16_t status;
    // Add status just to force wait for the command completion, but ignore the value
    return send_noop(dev, state, &status);
}

// ------------------------------------------------------------------------------------------------
// Local functions
// ------------------------------------------------------------------------------------------------

static mepa_rc send_dbgcmd(mepa_device_t *const         dev,
                           ipc_state_t *const           state,
                           const ipc_operation_t *const op)
{
    ipc_msg_t msg = {0};
    // Undocumented third word of the DBGCMD header. Always zero; no meaning is available for it.
    const uint16_t dbgcmd_reserved_word = 0;

    msg.opcode = AS2XXXX_IPC_CMD_DBGCMD;
    msg.io_data.buf[msg.io_data.len++] =
        FIELD_PREP16(U16_HIGH_BYTE, op->cmd) | FIELD_PREP16(U16_LOW_BYTE, op->subcmd);
    msg.io_data.buf[msg.io_data.len++] =
        op->in_data.len * sizeof(uint16_t); // Size in bytes is expected
    msg.io_data.buf[msg.io_data.len++] = dbgcmd_reserved_word;

    return ipc_send_msg(dev, state, &msg);
}

static mepa_rc send_wbuf(mepa_device_t *const         dev,
                         ipc_state_t *const           state,
                         const ipc_operation_t *const op)
{
    ipc_msg_t msg = {0};
    msg.opcode = AS2XXXX_IPC_CMD_WR_BUF;
    msg.io_data = op->in_data;
    return ipc_send_msg(dev, state, &msg);
}

static mepa_rc do_poll(mepa_device_t *const dev, ipc_state_t *const state)
{
    ipc_msg_t      msg = {0};
    mepa_bool_t    poll_done = false;
    size_t         retries = IPC_CMD_WAIT_POLL_RETRY_CNT;
    const uint16_t poll_complete = 1;

    msg.get_response = true;
    msg.opcode = AS2XXXX_IPC_CMD_POLL;

    while (retries--) {

        // POLL carries no payload. Both fields have to be cleared every pass because ipc_send_msg
        // leaves the reply behind in them.
        msg.io_data.len = 0;
        msg.io_data.buf[0] = 0;

        MEPA_RC(ipc_send_msg(dev, state, &msg));
        poll_done = (msg.io_data.buf[0] == poll_complete);

        if (poll_done) {
            return MEPA_RC_OK;
        }

        sleep_us(IPC_CMD_WAIT_POLL_RATE_US);
    }

    LOG_E("POLL failed: buf[0]=0x%04x, expected 0x%04x", msg.io_data.buf[0], poll_complete);
    return MEPA_RC_ERROR;
}

static mepa_rc get_rbuf(mepa_device_t *const   dev,
                        ipc_state_t *const     state,
                        ipc_operation_t *const op)
{
    mepa_rc   rc = MEPA_RC_OK;
    ipc_msg_t msg = {0};
    size_t    words_read = 0;
    size_t    words_in_msg;
    size_t    i;

    // Validate expected size
    if (op->out_data.expected_words == 0) {
        return MEPA_RC_ERR_PARM;
    }

    // Check buffer size is sufficient
    if (op->out_data.expected_words > op->out_data.buf_words_max) {
        LOG_E("Buffer size is insufficient");
        return MEPA_RC_ERR_PARM;
    }

    msg.opcode = AS2XXXX_IPC_CMD_RD_BUF;
    msg.get_response = true;

    // Keep reading until we get all expected words
    while (words_read < op->out_data.expected_words) {

        msg.io_data.len = 0;

        // Send RD_BUF command to get next chunk
        rc = ipc_send_msg(dev, state, &msg);

        if (rc != MEPA_RC_OK) {
            // A failed read means the firmware has no more to give, which is normal when the
            // exact reply size was not known up front. The caller compares actual_read_words
            // against what it expected.
            LOG_W("RBUF returned error, assuming end of data (read %u words)",
                  (uint32_t)words_read);
            break;
        }

        // Calculate how many words we got in this message
        words_in_msg = msg.io_data.len;

        // If PHY returns 0 words, it has no more data
        if (words_in_msg == 0) {
            break;
        }

        // Check we don't overflow
        if (words_read + words_in_msg > op->out_data.expected_words) {
            words_in_msg = op->out_data.expected_words - words_read;
        }

        // Copy received words to output buffer
        for (i = 0; i < words_in_msg; i++) {
            op->out_data.buf[words_read + i] = msg.io_data.buf[i];
        }

        words_read += words_in_msg;
    }

    // Store how many words we actually read
    op->out_data.actual_read_words = words_read;

    // Return success even if we didn't read expected amount
    // Caller checks actual_read_words vs expected_words
    return MEPA_RC_OK;
}

static mepa_rc send_noop(mepa_device_t *const dev,
                         ipc_state_t *const   state,
                         uint16_t *const      ret_status)
{
    uint16_t cmd = FIELD_PREP16(AS2XXXX_REG_MASK_IPC_CMD_SIZE, 0) |
                   FIELD_PREP16(AS2XXXX_REG_MASK_IPC_CMD_OPCODE, AS2XXXX_IPC_CMD_NOOP);
    return send_cmd(dev, state, cmd, ret_status);
}

static mepa_rc send_cmd(mepa_device_t *const dev,
                        ipc_state_t *const   state,
                        uint16_t             cmd,
                        uint16_t *const      ret_status)
{
    mepa_bool_t curr_parity;
    uint16_t    status;

    // The IPC uses a single parity bit to synch.
    // Each CMD must have this bit alternated to keep the correct cmd order.
    curr_parity = state->parity_status;
    if (state->parity_status) {
        cmd |= AS2XXXX_IPC_PARITY_BIT;
    }

    // Update parity for the next packet
    state->parity_status = !state->parity_status;
    MEPA_RC(mdio_mmd_vendor_wr(dev, AS2XXXX_REG_ADDR_IPC_CMD, cmd));

    // Wait for packet to be processed
    sleep_us(IPC_CMD_PROCESS_DELAY_US);

    // Caller not interested in the status, ignore waiting
    if (!ret_status) {
        return MEPA_RC_OK;
    }

    MEPA_RC(wait_cmd(dev, curr_parity));
    MEPA_RC(mdio_mmd_vendor_rd(dev, AS2XXXX_REG_ADDR_IPC_STATUS, &status));

    *ret_status = status;

    status &= AS2XXXX_REG_MASK_IPC_STS_STATUS;
    if (!(status == AS2XXXX_IPC_STS_STATUS_SUCCESS || status == AS2XXXX_IPC_STS_STATUS_READY)) {
        LOG_E("Bad status. Expected 0x%04x (%s) or 0x%04x (%s), received 0x%04x (%s)",
              AS2XXXX_IPC_STS_STATUS_READY, status2string(AS2XXXX_IPC_STS_STATUS_READY),
              AS2XXXX_IPC_STS_STATUS_SUCCESS, status2string(AS2XXXX_IPC_STS_STATUS_SUCCESS), status,
              status2string(status));
        return MEPA_RC_ERROR;
    }

    return MEPA_RC_OK;
}

static mepa_rc wait_cmd(mepa_device_t *const dev, mepa_bool_t parity_status)
{

    uint16_t status = 0;
    size_t   retries = IPC_CMD_WAIT_POLL_RETRY_CNT;

    while (retries--) {
        MEPA_RC(mdio_mmd_vendor_rd(dev, AS2XXXX_REG_ADDR_IPC_STATUS, &status));

        if (is_settled(status, parity_status)) {
            return MEPA_RC_OK;
        }

        sleep_us(IPC_CMD_WAIT_POLL_RATE_US);
    }

    LOG_E("IPC cmd timed-out. Status = 0x%04x (%s), parity = %u", status, status2string(status),
          parity_status);
    return MEPA_RC_ERROR;
}

static mepa_bool_t is_settled(uint16_t ipc_status_reg_value, mepa_bool_t parity_status)
{
    uint16_t status = ipc_status_reg_value & AS2XXXX_REG_MASK_IPC_STS_STATUS;

    if (status == AS2XXXX_IPC_STS_STATUS_ERROR) {
        return true;
    }

    // Ensure parity is in sync
    if (FIELD_GET16(AS2XXXX_REG_MASK_IPC_STS_PARITY, ipc_status_reg_value) != parity_status) {
        return false;
    }

    return (status != AS2XXXX_IPC_STS_STATUS_RCVD && status != AS2XXXX_IPC_STS_STATUS_PROCESS &&
            status != AS2XXXX_IPC_STS_STATUS_BUSY);
}

static const char *status2string(uint16_t ipc_status_reg_value)
{
    uint16_t status = ipc_status_reg_value & AS2XXXX_REG_MASK_IPC_STS_STATUS;

    switch (status) {
    case AS2XXXX_IPC_STS_STATUS_RCVD:    return "RCVD";
    case AS2XXXX_IPC_STS_STATUS_PROCESS: return "PROCESS";
    case AS2XXXX_IPC_STS_STATUS_SUCCESS: return "SUCCESS";
    case AS2XXXX_IPC_STS_STATUS_ERROR:   return "ERROR";
    case AS2XXXX_IPC_STS_STATUS_BUSY:    return "BUSY";
    case AS2XXXX_IPC_STS_STATUS_READY:   return "READY";
    default:                             return "UNKNOWN";
    }
}