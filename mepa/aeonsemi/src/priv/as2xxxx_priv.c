// Copyright (c) 2004-2026 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

/**
 * @brief Private implementation layer for AS2XXXX PHY driver.
 * Contains public API functions (as2xxxx_priv_*) that perform parameter validation, locking,
 * and NULL checks before delegating to static helper functions. Static functions assume
 * pre-validated inputs to avoid double-locking and redundant checks. The IPC communication layer
 * is isolated in separate files (ipc.c/h) for separation of concerns.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "as2xxxx_priv.h"
#include "as2xxxx_bitfields.h"
#include "as2xxxx_registers.h"
#include "as2xxxx_utils.h"

#include "as2xxxx_fw_img.h"

#include "ipc.h"
#include "eye.h"
#include "mdio.h"

#include "mepa_driver.h"
#include "mepa_utils.h"
#include "microchip/ethernet/common.h"
#include "microchip/ethernet/phy/api/types.h"

// ------------------------------------------------------------------------------------------------
// Logging: this file logs as the PRIV module, see as2xxxx_utils.h
// ------------------------------------------------------------------------------------------------

#define LOG_D(fmt, ...)        AS2XXXX_LOG_D(PRIV, fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...)        AS2XXXX_LOG_W(PRIV, fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...)        AS2XXXX_LOG_E(PRIV, fmt, ##__VA_ARGS__)
#define RC_LOG(expr, msg, ...) AS2XXXX_RC_LOG(PRIV, expr, msg, ##__VA_ARGS__)

// ------------------------------------------------------------------------------------------------
// Macros
// ------------------------------------------------------------------------------------------------

/** @brief Helper macro to indicate that the parameter is not of interest */
#define DONT_CARE ((void *)NULL)

// Error messages shared by several call sites.
#define ERR_MSG_ACCESS_SIDE_LINE "Failed to set line side access to registers"
#define ERR_MSG_ACCESS_SIDE_HOST "Failed to set host side access to registers"
#define ERR_MSG_HOST_LOOPBACK    "Applying host loopback to the PHY failed"

// ------------------------------------------------------------------------------------------------
// Typedefs
// ------------------------------------------------------------------------------------------------

/** @brief Helper struct to set LED config using IPC */
typedef struct {
    struct {
        as2xxxx_led_behavior_t led0;
        as2xxxx_led_behavior_t led1;
        as2xxxx_led_behavior_t led2;
        as2xxxx_led_behavior_t led3;
        as2xxxx_led_behavior_t led4;
    } behavior;

    as2xxxx_led_polarity_t   polarity;
    as2xxxx_led_blink_rate_t blink_rate;
} ipc_led_config_t;

/** @brief Firmware image plus the caller's progress reporting, assembled by
 *  boot_mdio() once the image has been selected. */
typedef struct {
    const uint8_t        *img;
    size_t                img_size;
    as2xxxx_fw_progress_t progress_track;
} fw_load_data_t;

/** @brief Side */
typedef enum {
    AS2XXXX_SIDE_LINE, // BASE-T
    AS2XXXX_SIDE_HOST, // XFI/BASE-R
} as2xxxx_side_t;

// ------------------------------------------------------------------------------------------------
// Local instances
// ------------------------------------------------------------------------------------------------

/** @brief Map of supported SerDes speeds for the host side port config */
static const struct {
    mepa_port_speed_t      mesa_speed;
    as2xxxx_serdes_speed_t serdes_speed;
} s_serdes_speed_map[] = {
    {MESA_SPEED_1G,    AS2XXXX_SERDES_SPEED_1G   },
    {MESA_SPEED_2500M, AS2XXXX_SERDES_SPEED_2500M},
    {MESA_SPEED_5G,    AS2XXXX_SERDES_SPEED_5G   },
    {MESA_SPEED_10G,   AS2XXXX_SERDES_SPEED_10G  }
};

/** @brief Map of supported MDI speeds for the line side port config */
static const struct {
    mepa_port_speed_t   mesa_speed;
    as2xxxx_mdi_speed_t mdi_speed;
} s_line_speed_map[] = {
    {MESA_SPEED_100M,  AS2XXXX_MDI_SPEED_T100M },
    {MESA_SPEED_1G,    AS2XXXX_MDI_SPEED_T1G   },
    {MESA_SPEED_2500M, AS2XXXX_MDI_SPEED_T2500M},
    {MESA_SPEED_5G,    AS2XXXX_MDI_SPEED_T5G   },
    {MESA_SPEED_10G,   AS2XXXX_MDI_SPEED_T10G  },
};

static as2xxxx_serdes_speed_t serdes_speed_from_mesa(mesa_port_speed_t speed)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(s_serdes_speed_map); i++) {
        if (s_serdes_speed_map[i].mesa_speed == speed) {
            return s_serdes_speed_map[i].serdes_speed;
        }
    }

    return AS2XXXX_SERDES_SPEED_INVALID;
}

static mesa_port_speed_t mesa_speed_from_serdes(as2xxxx_serdes_speed_t speed)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(s_serdes_speed_map); i++) {
        if (s_serdes_speed_map[i].serdes_speed == speed) {
            return s_serdes_speed_map[i].mesa_speed;
        }
    }

    return MESA_SPEED_UNDEFINED;
}

static as2xxxx_mdi_speed_t mdi_speed_from_mesa(mesa_port_speed_t speed)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(s_line_speed_map); i++) {
        if (s_line_speed_map[i].mesa_speed == speed) {
            return s_line_speed_map[i].mdi_speed;
        }
    }

    return AS2XXXX_MDI_SPEED_INVALID;
}

/** @brief Map of IRQ event to a corresponding IRQ status word mask */
static const struct {
    as2xxxx_irq_event_t event;
    uint16_t            status_word_mask;
} s_event_mask_map[] = {
    {AS2XXXX_IRQ_EVENT_WAKE_ON_LAN,        AS2XXXX_REG_MASK_IRQ_STATUS_WAKE_ON_LAN},
    {AS2XXXX_IRQ_EVENT_LINK_STATUS_CHANGE, AS2XXXX_REG_MASK_IRQ_STATUS_LINK_CHANGE},
};

/** @brief Firmware image per chip family.
 *
 * The images are not interchangeable, each family boots only its own. A family
 * missing from this table cannot be booted over MDIO. */
static const struct {
    as2xxxx_family_t family;
    const uint8_t   *img;
    size_t           img_size;
} s_fw_images[] = {
    {AS2XXXX_FAMILY_AS21XXX, AEONSEMI_FW_IMG_AS21XXX, sizeof(AEONSEMI_FW_IMG_AS21XXX)},
    {AS2XXXX_FAMILY_AS22XXX, AEONSEMI_FW_IMG_AS22XXX, sizeof(AEONSEMI_FW_IMG_AS22XXX)},
};

/** @brief Default LED configuration */
static const ipc_led_config_t s_default_led_config = {
    .behavior.led0 = AS2XXXX_LED_ON_NG_BLINK_ACT,
    .behavior.led1 = AS2XXXX_LED_ON_FE_GE_BLINK_ACT,
    .behavior.led2 = AS2XXXX_LED_LINK_EST,
    .behavior.led3 = AS2XXXX_LED_TX_RX_ACT,
    .behavior.led4 = AS2XXXX_LED_LINK_EST_BLINK_ACT,
    .blink_rate = AS2XXXX_LED_BLINK_RATE_3_9063HZ,
    .polarity = AS2XXXX_LED_0_4_POLARITY_NO_SWAP,
};

// ------------------------------------------------------------------------------------------------
// Local function definitions
//
// These assume pre-validated inputs and, unless stated otherwise, that the device
// lock is already held by the calling as2xxxx_priv_*() entry point.
//
// Out-parameter contract, for these and for the public API: the values pointed to
// are only meaningful when the function returns MEPA_RC_OK. On failure they must
// be treated as indeterminate - some paths fail before writing anything, others
// after having written a value the PHY turned out to have reported wrongly. Where
// a function normalises an out-param to a defined "unknown" on failure, that is
// defensive tidiness, not something callers may rely on. Every caller checks the
// return code first.
// ------------------------------------------------------------------------------------------------

// Settings
static mepa_rc set_led_config(as2xxxx_priv_data_t *const pd, const ipc_led_config_t *const cfg);
static mepa_rc set_mmd_reg_access_side(as2xxxx_priv_data_t *const pd, as2xxxx_side_t side);

// Speed, link, aneg
static mepa_rc set_line_speed_cfg(as2xxxx_priv_data_t *const pd,
                                  as2xxxx_mdi_speed_t        speed,
                                  mepa_bool_t                aneg_enable);
static mepa_rc set_line_flow_control(as2xxxx_priv_data_t *const pd, mepa_bool_t enable);
static mepa_rc set_aneg_enable(as2xxxx_priv_data_t *const pd, mepa_bool_t enable);
static mepa_rc set_phy_enable(as2xxxx_priv_data_t *const pd, as2xxxx_phy_enable_t enable);
static mepa_rc set_host_inband(as2xxxx_priv_data_t *const pd, as2xxxx_host_inband_t inband);
static mepa_rc set_host_inband_and_speed(as2xxxx_priv_data_t *const pd,
                                         as2xxxx_host_inband_t      inband,
                                         as2xxxx_serdes_speed_t     speed);
static mepa_rc get_host_inband(as2xxxx_priv_data_t *const pd, as2xxxx_host_inband_t *const inband);
static mepa_rc get_host_cfg(as2xxxx_priv_data_t *const     pd,
                            as2xxxx_host_inband_t *const   inband,
                            as2xxxx_serdes_speed_t *const  speed,
                            as2xxxx_serdes_opmode_t *const opmode);
static mepa_rc set_host_cfg(as2xxxx_priv_data_t *const pd,
                            as2xxxx_host_inband_t      inband,
                            as2xxxx_serdes_speed_t     speed,
                            as2xxxx_serdes_opmode_t    opmode);
static mepa_rc set_dp_mode(as2xxxx_priv_data_t *const pd, as2xxxx_dp_mode_t dp_mode);
static mepa_rc reset_sds(as2xxxx_priv_data_t *const pd);
static mepa_rc enable_dpc_ra(as2xxxx_priv_data_t *const pd);
static mepa_rc set_dpc_fsm(as2xxxx_priv_data_t *const pd, as2xxxx_cfg_dpc_fsm_t enable);
static mepa_rc restart_dpc_fsm(as2xxxx_priv_data_t *const pd);
static mepa_rc get_dp_mode(as2xxxx_priv_data_t *const pd, as2xxxx_dp_mode_t *const dp_mode);
static mepa_rc get_link_status(as2xxxx_priv_data_t *const pd,
                               as2xxxx_side_t             side,
                               mepa_bool_t *const         is_link_up);

// IRQs
static mepa_rc get_irq_event(as2xxxx_priv_data_t *const pd,
                             as2xxxx_irq_event_t        event,
                             mepa_bool_t *const         is_active);
static mepa_rc clear_irq_event(as2xxxx_priv_data_t *const pd, as2xxxx_irq_event_t event);

// Boot and firmware
static mepa_rc     boot_mdio(as2xxxx_priv_data_t *const         pd,
                             const as2xxxx_fw_progress_t *const progress);
static mepa_rc     warm_boot(as2xxxx_priv_data_t *const pd);
static mepa_bool_t is_fw_running(as2xxxx_priv_data_t *const pd);
static mepa_rc     read_phy_id_checked(as2xxxx_priv_data_t *const pd, uint32_t *const phy_id);
static mepa_rc     reboot_cpu_warm(as2xxxx_priv_data_t *const pd);
static mepa_rc     load_and_boot_firmware(as2xxxx_priv_data_t *const  pd,
                                          const fw_load_data_t *const fw_data);
static mepa_rc     load_fw_img(as2xxxx_priv_data_t *const pd, const fw_load_data_t *const fw_data);
static mepa_rc     fw_image_get(as2xxxx_family_t family, fw_load_data_t *const fw_data);

// Counters
static mepa_rc read_counter_block(as2xxxx_priv_data_t *const pd,
                                  uint16_t                   cmd,
                                  uint16_t                   subcmd,
                                  uint16_t *const            buf,
                                  size_t                     words);
static mepa_rc clear_counter_block(as2xxxx_priv_data_t *const pd, uint16_t cmd, uint16_t subcmd);
static mepa_rc get_counters_as21xxx(as2xxxx_priv_data_t *const        pd,
                                    as2xxxx_counters_as21xxx_t *const counters,
                                    mepa_bool_t                       clear);
static mepa_rc get_counters_as22xxx(as2xxxx_priv_data_t *const        pd,
                                    as2xxxx_counters_as22xxx_t *const counters,
                                    mepa_bool_t                       clear);

// Misc
static inline uint32_t   extract_counter_u32(const uint16_t *buf, size_t counter_idx);
static inline uint64_t   extract_counter_u64(const uint16_t *buf, size_t counter_idx);
static void              extract_pkt_block(const uint16_t            *buf,
                                           size_t                     first_counter_idx,
                                           as2xxxx_pkt_block_t *const blk);
static mesa_port_speed_t decode_line_speed(uint16_t regval);

// ------------------------------------------------------------------------------------------------
// Public functions
// ------------------------------------------------------------------------------------------------

mepa_rc as2xxxx_priv_read_phyid(as2xxxx_priv_data_t *const pd, uint32_t *const devid)
{
    mepa_rc              rc = MEPA_RC_OK;
    mepa_device_t *const dev = pd->dev;
    uint16_t             devid_lsb = 0, devid_msb = 0;

    NULL_CHECK(devid);

    *devid = 0;

    RC_LOG(mdio_mmd_rd(dev, AS2XXXX_DEV_PMA_PMD, AS2XXXX_REG_ADDR_DEV_ID_MSB, &devid_msb),
           "Failed to read devid msb");
    RC_LOG(mdio_mmd_rd(dev, AS2XXXX_DEV_PMA_PMD, AS2XXXX_REG_ADDR_DEV_ID_LSB, &devid_lsb),
           "Failed to read devid lsb");

    *devid = FIELD_PREP32(U32_HIGH_WORD, devid_msb) | FIELD_PREP32(U32_LOW_WORD, devid_lsb);
    return rc;
}

as2xxxx_family_t as2xxxx_priv_family_from_phy_id(uint32_t phy_id)
{
    uint8_t model;

    if ((phy_id & AS2XXXX_PHY_ID_MASK) != AS2XXXX_PHY_ID_VENDOR) {
        return AS2XXXX_FAMILY_UNKNOWN;
    }

    model = as2xxxx_priv_model_from_phy_id(phy_id);

    if (model <= AS2XXXX_DEV_ID_MODEL_AS21XXX_MAX) {
        return AS2XXXX_FAMILY_AS21XXX;
    }

    if (model == AS2XXXX_DEV_ID_MODEL_AS22XXX) {
        return AS2XXXX_FAMILY_AS22XXX;
    }

    return AS2XXXX_FAMILY_UNKNOWN;
}

uint8_t as2xxxx_priv_model_from_phy_id(uint32_t phy_id)
{
    return (uint8_t)FIELD_GET32(AS2XXXX_REG_MASK_DEV_ID_MODEL, phy_id);
}

uint8_t as2xxxx_priv_revision_from_phy_id(uint32_t phy_id)
{
    return (uint8_t)FIELD_GET32(AS2XXXX_REG_MASK_DEV_ID_REVISION, phy_id);
}

const char *as2xxxx_priv_family_to_string(as2xxxx_family_t family)
{
    switch (family) {
    case AS2XXXX_FAMILY_AS21XXX: return "AS21xxx (SEI1)";
    case AS2XXXX_FAMILY_AS22XXX: return "AS22xxx (SEI2)";
    case AS2XXXX_FAMILY_UNKNOWN:
    default:                     return "Unknown";
    }
}

const char *as2xxxx_priv_host_inband_to_string(as2xxxx_host_inband_t inband)
{
    switch (inband) {
    case AS2XXXX_HOST_INBAND_DIS:     return "DISABLED";
    case AS2XXXX_HOST_INBAND_ENA:     return "ENABLED";
    case AS2XXXX_HOST_INBAND_UNKNOWN:
    default:                          return "UNKNOWN";
    }
}

uint32_t as2xxxx_priv_dev_id_get(const as2xxxx_priv_data_t *const pd) { return pd->dev_id; }

as2xxxx_family_t as2xxxx_priv_family_get(const as2xxxx_priv_data_t *const pd) { return pd->family; }

const char *as2xxxx_priv_fw_version_get(as2xxxx_priv_data_t *const pd)
{
    // Marked cached only on success, so a failed read is retried next time.
    if (!pd->is_fw_ver_cached &&
        as2xxxx_priv_get_fw_version(pd, pd->fw_version, sizeof(pd->fw_version)) == MEPA_RC_OK) {
        pd->is_fw_ver_cached = true;
    }

    return pd->fw_version;
}

mepa_rc as2xxxx_priv_probe(as2xxxx_priv_data_t *const         pd,
                           mepa_device_t *const               dev,
                           const as2xxxx_fw_progress_t *const progress)
{
    mepa_rc          rc = MEPA_RC_OK;
    uint32_t         phy_id = 0;
    as2xxxx_family_t family;

    // The handle carries the device from here on, so every other entry point in
    // this layer needs only the handle.
    pd->dev = dev;

    RC_LOG(read_phy_id_checked(pd, &phy_id), "Failed to get the PHY id");

    family = as2xxxx_priv_family_from_phy_id(phy_id);

    if (family == AS2XXXX_FAMILY_UNKNOWN) {
        LOG_E("Unsupported device with phy_id = 0x%08x", phy_id);
        return MEPA_RC_ERROR;
    }

    // Publish the family before booting: the boot paths dispatch on it, both to
    // pick the firmware image and to decide whether the datapath FSM needs
    // restarting.
    pd->family = family;

    // Only warm boot and MDIO boot are supported; booting from flash is not.
    if (is_fw_running(pd)) {
        RC_LOG(warm_boot(pd), "Failed to warm boot");
    } else {
        RC_LOG(boot_mdio(pd, progress), "Failed to boot from MDIO");

        // Re-read the ID: on AS21xxx it is now the model specific one, on
        // AS22xxx it is unchanged. Either way the family must not have moved.
        RC_LOG(read_phy_id_checked(pd, &phy_id), "Failed to get the PHY id after MDIO boot");

        if (as2xxxx_priv_family_from_phy_id(phy_id) != family) {
            LOG_E("PHY ID 0x%08x reports a different family after MDIO boot", phy_id);
            return MEPA_RC_ERROR;
        }
    }

    RC_LOG(as2xxxx_priv_get_fw_version(pd, pd->fw_version, sizeof(pd->fw_version)),
           "Failed to get firmware version");

    pd->is_fw_ver_cached = true;
    pd->dev_id = phy_id;

    LOG_D("%s with id = 0x%08x, FW ver = %s booted successfully",
          as2xxxx_priv_family_to_string(family), pd->dev_id, pd->fw_version);

    return MEPA_RC_OK;
}

static mepa_rc read_phy_id_checked(as2xxxx_priv_data_t *const pd, uint32_t *const phy_id)
{
    MEPA_RC(as2xxxx_priv_read_phyid(pd, phy_id));

    // A zero ID means nothing answered on the bus at all.
    if (*phy_id == 0) {
        LOG_E("PHY id reads as zero, no device on the bus");
        return MEPA_RC_ERROR;
    }

    return MEPA_RC_OK;
}

static mepa_rc boot_mdio(as2xxxx_priv_data_t *const pd, const as2xxxx_fw_progress_t *const progress)
{
    mepa_device_t *const dev = pd->dev;
    mepa_rc              rc = MEPA_RC_OK;
    fw_load_data_t       fw_data = {0};

    RC_LOG(fw_image_get(pd->family, &fw_data), "Failed to select a firmware image");

    if (progress != NULL) {
        fw_data.progress_track = *progress;
    }

    RC_LOG(load_and_boot_firmware(pd, &fw_data), "Failed to load firmware");
    RC_LOG(ipc_sync_parity(dev, &pd->ipc), "Failed to sync parity after MDIO boot");
    RC_LOG(ipc_send_noop_cmd(dev, &pd->ipc), "Failed to do noop after MDIO boot");
    RC_LOG(set_led_config(pd, &s_default_led_config), "Failed to set default LED config");
    RC_LOG(enable_dpc_ra(pd), "Failed to arm the datapath rate adaptation");
    RC_LOG(restart_dpc_fsm(pd), "Failed to restart the datapath FSM after MDIO boot");
    return rc;
}

static mepa_rc warm_boot(as2xxxx_priv_data_t *const pd)
{
    mepa_device_t *const dev = pd->dev;
    mepa_rc              rc = MEPA_RC_OK;
    size_t               attempt;
    const uint32_t       settle_ms = 100;
    const size_t         ready_poll_attempts = 40;
    const uint32_t       ready_poll_ms = 50;

    // The three calls below are deliberately unchecked. Whatever state the
    // firmware is in before the reboot is not trustworthy.
    // The reboot is issued regardless, and everything after the wait is checked
    // because by then the firmware is expected to be responsive.
    (void)ipc_sync_parity(dev, &pd->ipc);
    (void)ipc_send_noop_cmd(dev, &pd->ipc);
    (void)reboot_cpu_warm(pd);

    // We don't unlock here and we sleep while holding the lock because anyway we can't use
    // the PHY while it is rebooting.
    MEPA_MSLEEP(settle_ms);

    // is_fw_running re-syncs parity on every attempt, which is what the restarted firmware needs
    // before it will answer anything.
    for (attempt = 0; attempt < ready_poll_attempts; attempt++) {
        if (is_fw_running(pd)) {
            break;
        }
        MEPA_MSLEEP(ready_poll_ms);
    }

    if (attempt == ready_poll_attempts) {
        LOG_E("The firmware did not answer within %u ms of the warm boot",
              settle_ms + (unsigned)(ready_poll_attempts * ready_poll_ms));
        return MEPA_RC_ERROR;
    }

    RC_LOG(ipc_sync_parity(dev, &pd->ipc), "Failed to sync parity after warm boot");
    RC_LOG(ipc_send_noop_cmd(dev, &pd->ipc), "Failed to do noop after warm boot");
    RC_LOG(set_led_config(pd, &s_default_led_config), "Failed to set default LED config");
    RC_LOG(enable_dpc_ra(pd), "Failed to arm the datapath rate adaptation");
    RC_LOG(restart_dpc_fsm(pd), "Failed to restart the datapath FSM after warm boot");
    return rc;
}

static mepa_bool_t is_fw_running(as2xxxx_priv_data_t *const pd)
{
    mepa_device_t *const dev = pd->dev;
    mepa_bool_t          is_running;
    ipc_msg_t            msg = {0};

    if (dev == NULL) {
        LOG_E("dev ptr is NULL");
        return false;
    }

    // Any command would do, the version query is just the cheapest one that the
    // firmware must always answer.
    msg.opcode = AS2XXXX_IPC_CMD_INFO;
    msg.get_response = true;
    msg.io_data.buf[msg.io_data.len++] = AS2XXXX_IPC_INFO_SUBCMD_VERSION;

    // The parity state is unknown here: this may be a cold boot, a warm restart
    // or a previous driver session, so re-sync before trusting any reply.
    (void)ipc_sync_parity(dev, &pd->ipc);
    is_running = (ipc_send_msg(dev, &pd->ipc, &msg) == MEPA_RC_OK);

    return is_running;
}

mepa_rc as2xxxx_priv_get_fw_version(as2xxxx_priv_data_t *const pd,
                                    char *const                fw_version,
                                    const size_t               fw_ver_size)
{
    mepa_device_t *const dev = pd->dev;
    mepa_rc              rc = MEPA_RC_OK;
    ipc_msg_t            msg = {0};
    size_t               size_bytes;

    NULL_CHECK(fw_version);

    if (fw_ver_size == 0) {
        LOG_E("FW version buffer size must not be 0");
        return MEPA_RC_ERR_PARM;
    }

    msg.opcode = AS2XXXX_IPC_CMD_INFO;
    msg.get_response = true; // We want the FW ver to be read as response
    msg.io_data.buf[msg.io_data.len++] = AS2XXXX_IPC_INFO_SUBCMD_VERSION;

    RC_LOG(ipc_send_msg(dev, &pd->ipc, &msg), "Failed to send IPC message");

    size_bytes = msg.io_data.len * sizeof(uint16_t); // io_data.len indicates size in uint16_t words
    if (size_bytes >= fw_ver_size) {
        size_bytes = fw_ver_size - 1;
    }

    memcpy(fw_version, msg.io_data.buf, size_bytes);
    fw_version[size_bytes] = '\0';
    return rc;
}

mepa_rc as2xxxx_priv_reset(as2xxxx_priv_data_t *const pd)
{
    mepa_device_t *const dev = pd->dev;
    UNUSED(dev);

    // No-op: the device is always reset during cold or warm boot, so no reset is needed here.
    // Note: do not use the reset bit in MII_BMCR - it leaves the chip in a state the firmware
    // does not recover from, see the README. If a mid-operation reset is ever required, use
    // reboot_cpu_warm(pd)

    return MEPA_RC_OK;
}

mepa_rc as2xxxx_priv_phy_enable(as2xxxx_priv_data_t *const pd)
{
    mepa_rc rc = MEPA_RC_OK;

    rc = set_phy_enable(pd, AS2XXXX_PHY_ENABLE_ENABLE);

    if (rc != MEPA_RC_OK) {
        LOG_E("Failed to enable phy");
    }

    return rc;
}

mepa_rc as2xxxx_priv_phy_disable(as2xxxx_priv_data_t *const pd)
{
    mepa_rc rc = MEPA_RC_OK;

    rc = set_phy_enable(pd, AS2XXXX_PHY_ENABLE_DISABLE);

    if (rc != MEPA_RC_OK) {
        LOG_E("Failed to disable phy");
    }

    return rc;
}

mepa_rc as2xxxx_priv_set_host_cfg(as2xxxx_priv_data_t *const  pd,
                                  const as2xxxx_host_inband_t inband,
                                  const mesa_port_speed_t     speed)
{
    mepa_rc                rc = MEPA_RC_OK;
    as2xxxx_serdes_speed_t target_speed;

    target_speed = serdes_speed_from_mesa(speed);

    if (target_speed == AS2XXXX_SERDES_SPEED_INVALID) {
        LOG_E("Given speed is not supported");
        return MEPA_RC_ERR_PARM;
    }

    rc = set_host_inband_and_speed(pd, inband, target_speed);
    if (rc != MEPA_RC_OK) {
        LOG_E("Applying the host configuration to the PHY failed");
    }

    return rc;
}

mepa_rc as2xxxx_priv_set_line_config(as2xxxx_priv_data_t *const pd,
                                     const mesa_port_speed_t    speed,
                                     const mepa_bool_t          is_full_duplex,
                                     const mepa_bool_t          aneg_enable,
                                     const mepa_bool_t          flow_control)
{
    mepa_rc             rc = MEPA_RC_OK;
    as2xxxx_mdi_speed_t target_speed;

    if (!is_full_duplex) {
        LOG_E("Half duplex is not supported, the PHY runs full duplex only");
        return MEPA_RC_ERR_PARM;
    }

    if (speed == MESA_SPEED_10M) {
        LOG_E("10M speed is not supported by the PHY");
        return MEPA_RC_ERR_PARM;
    }

    // 1000BASE-T and every multi-gig BASE-T rate above it can only be established by
    // auto-negotiation, so forcing them is not a thing the copper side can do - 100BASE-TX is the
    // only rate here that can. Refused rather than attempted, because attempting it disables
    // auto-negotiation for a speed that can then never link, and nothing brings the line back
    // without reconfiguring the port twice. The same rates are perfectly fine as a top
    // auto-negotiation speed, which is why the restriction only applies with aneg off.
    if (!aneg_enable && speed > MESA_SPEED_100M) {
        LOG_E("Only 100M can be forced on this PHY, every faster BASE-T rate needs "
              "auto-negotiation");
        return MEPA_RC_ERR_PARM;
    }

    target_speed = mdi_speed_from_mesa(speed);

    if (target_speed == AS2XXXX_MDI_SPEED_INVALID) {
        LOG_E("Given speed is not supported");
        return MEPA_RC_ERR_PARM;
    }

    // The pause advertisement goes in first, because the speed configuration ends by restarting
    // auto-negotiation and the base page has to already carry the pause bits by then. Written the
    // other way round it only reaches the link partner at the next flap of the line.
    //
    // On AS22xxx it does not survive even that: the firmware repopulates the base page during the
    // restart and always advertises both pause bits, whatever was written here. The line therefore
    // advertises pause on that family regardless of this setting, and the caller's choice is
    // honoured by what as2xxxx_priv_get_flow_control_line() reports rather than by the wire. Kept
    // because AS21xxx does take the write.
    RC_LOG(set_line_flow_control(pd, flow_control),
           "Failed to apply the flow control advertisement");

    RC_LOG(set_line_speed_cfg(pd, target_speed, aneg_enable),
           "Failed to apply the line speed configuration");
    return rc;
}

mepa_rc as2xxxx_priv_get_speed_line(as2xxxx_priv_data_t *const pd,
                                    mesa_port_speed_t *const   speed,
                                    mepa_bool_t *const         is_full_duplex)
{
    mepa_device_t *const dev = pd->dev;
    mepa_rc              rc = MEPA_RC_OK;
    uint16_t             regval;
    size_t               attempt;
    mepa_bool_t          is_link_up = false;

    // Transient bus issues may yield an undefined speed value
    const size_t speed_read_retries = 3;

    NULL_CHECK(speed);
    NULL_CHECK(is_full_duplex);

    RC_LOG(set_mmd_reg_access_side(pd, AS2XXXX_SIDE_LINE), ERR_MSG_ACCESS_SIDE_LINE);

    // The MII_BMCR status register is located in MMD device 7 (same as AS2XXXX_DEV_AN).
    RC_LOG(mdio_mmd_rd(dev, AS2XXXX_DEV_AN, AS2XXXX_REG_ADDR_MII_CTRL, &regval),
           "Failed to read control register");

    *is_full_duplex = FIELD_GET16(AS2XXXX_REG_MASK_CTRL_FULL_DUPLEX, regval);

    RC_LOG(mdio_mmd_vendor_rd(dev, AS2XXXX_REG_ADDR_SPEED_STATUS, &regval),
           "Failed to read speed status register");

    *speed = decode_line_speed(regval);

    if (*speed == MESA_SPEED_UNDEFINED) {
        // Only worth rereading while the line is up, where an undecodable value means the register
        // was caught mid-change. On a down line it is the expected answer and no number of rereads
        // will alter it, so the retries would otherwise run on every poll of every idle port. The
        // link read costs one transaction and replaces three.
        RC_LOG(get_link_status(pd, AS2XXXX_SIDE_LINE, &is_link_up),
               "Failed to read the line link status");
    }

    for (attempt = 0; is_link_up && *speed == MESA_SPEED_UNDEFINED && attempt < speed_read_retries;
         attempt++) {

        RC_LOG(mdio_mmd_vendor_rd(dev, AS2XXXX_REG_ADDR_SPEED_STATUS, &regval),
               "Failed to read speed status register (retry)");
        *speed = decode_line_speed(regval);
    }

    // An undecodable speed is left as MESA_SPEED_UNDEFINED and reported as success. It means the
    // line has not settled on a speed, which is an ordinary state - the register was read fine, and
    // a failure to read it has already been reported by the RC_LOGs above. Callers that need a
    // speed check for MESA_SPEED_UNDEFINED; callers that only care whether the bus worked do not
    // have to treat an idle line as a fault. Deliberately not logged: it would print on every poll
    // of a port that is simply down.
    return rc;
}

mepa_rc as2xxxx_priv_get_aneg_done_flag_line(as2xxxx_priv_data_t *const pd,
                                             mepa_bool_t *const         is_aneg_done)
{
    mepa_device_t *const dev = pd->dev;
    mepa_rc              rc = MEPA_RC_OK;
    uint16_t             regval;

    NULL_CHECK(is_aneg_done);

    RC_LOG(set_mmd_reg_access_side(pd, AS2XXXX_SIDE_LINE), ERR_MSG_ACCESS_SIDE_LINE);

    // The MII_BMSR status register is located in MMD device 7 (same as AS2XXXX_DEV_AN).
    RC_LOG(mdio_mmd_rd(dev, AS2XXXX_DEV_AN, AS2XXXX_REG_ADDR_MII_STATUS, &regval),
           "Failed to read status register");

    *is_aneg_done = FIELD_GET16(AS2XXXX_REG_MASK_STATUS_ANEG_DONE, regval);
    return rc;
}

mepa_rc as2xxxx_priv_get_flow_control_line(as2xxxx_priv_data_t *const pd,
                                           mepa_bool_t *const         generate_pause,
                                           mepa_bool_t *const         obey_pause)
{
    mepa_device_t *const dev = pd->dev;
    mepa_rc              rc = MEPA_RC_OK;
    uint16_t             own_adv = 0;
    uint16_t             lp_adv = 0;
    mepa_bool_t          own_sym;
    mepa_bool_t          lp_sym;
    mepa_bool_t          own_asym;
    mepa_bool_t          lp_asym;

    NULL_CHECK(generate_pause);
    NULL_CHECK(obey_pause);

    *generate_pause = false;
    *obey_pause = false;

    RC_LOG(set_mmd_reg_access_side(pd, AS2XXXX_SIDE_LINE), ERR_MSG_ACCESS_SIDE_LINE);

    RC_LOG(mdio_mmd_rd(dev, AS2XXXX_DEV_AN, AS2XXXX_REG_ADDR_MII_ANEG_CTRL, &own_adv),
           "Failed to read the own pause advertisement");
    RC_LOG(mdio_mmd_rd(dev, AS2XXXX_DEV_AN, AS2XXXX_REG_ADDR_MII_ANEG_LP, &lp_adv),
           "Failed to read the link partner pause advertisement");

    own_sym = FIELD_GET16(AS2XXXX_REG_MASK_ANEG_PAUSE, own_adv);
    lp_sym = FIELD_GET16(AS2XXXX_REG_MASK_ANEG_PAUSE, lp_adv);
    own_asym = FIELD_GET16(AS2XXXX_REG_MASK_ANEG_PAUSE_ASM, own_adv);
    lp_asym = FIELD_GET16(AS2XXXX_REG_MASK_ANEG_PAUSE_ASM, lp_adv);

    // IEEE 802.3 Annex 28B.3. Symmetric on both ends resolves to pause in both directions.
    // Otherwise the only outcomes are one-directional, and only when both ends asked for
    // asymmetric: the end that advertised symmetric pause is the one that gets to receive it.
    if (own_sym && lp_sym) {
        *generate_pause = true;
        *obey_pause = true;
    } else if (own_asym && lp_asym) {
        *generate_pause = !own_sym && lp_sym;
        *obey_pause = own_sym && !lp_sym;
    }

    return rc;
}

mepa_rc as2xxxx_priv_get_link_status_host(as2xxxx_priv_data_t *const pd,
                                          mepa_bool_t *const         is_link_up)
{
    mepa_rc rc = MEPA_RC_OK;
    NULL_CHECK(is_link_up);
    rc = get_link_status(pd, AS2XXXX_SIDE_HOST, is_link_up);
    return rc;
}

mepa_rc as2xxxx_priv_get_host_sds_state(as2xxxx_priv_data_t *const pd,
                                        mepa_bool_t *const         is_link_up,
                                        mesa_port_speed_t *const   speed)
{
    mepa_device_t *const   dev = pd->dev;
    mepa_rc                rc = MEPA_RC_OK;
    ipc_operation_t        op = {0};
    uint16_t               out_data[1] = {0};
    as2xxxx_serdes_speed_t sds_speed;

    NULL_CHECK(is_link_up);
    NULL_CHECK(speed);

    // AS21xxx does not have this subcommand at all, so there is nothing to ask it.
    if (pd->family != AS2XXXX_FAMILY_AS22XXX) {
        return MEPA_RC_NOT_IMPLEMENTED;
    }

    // Carries no input payload, unlike most of the SDS subcommands.
    op.cmd = AS2XXXX_IPC_DBGCMD_SUBCMD_SDS;
    op.subcmd = AS2XXXX_IPC_DBGCMD_SDS_SUBCMD_AS22XXX_LINK_STAT;
    op.out_data.buf = out_data;
    op.out_data.buf_words_max = ARRAY_SIZE(out_data);
    op.out_data.expected_words = ARRAY_SIZE(out_data);

    RC_LOG(ipc_do_operation(dev, &pd->ipc, &op), "Failed to get the host SerDes state");

    if (op.out_data.actual_read_words != ARRAY_SIZE(out_data)) {
        LOG_E("Short read of the host SerDes state");
        return MEPA_RC_ERROR;
    }

    *is_link_up =
        (FIELD_GET16(AS2XXXX_REG_MASK_SDS_LINK_UP, out_data[0]) == AS2XXXX_SDS_LINK_UP_VAL);

    // A speed code outside the four the SerDes can run maps to MESA_SPEED_UNDEFINED, which is what
    // the firmware reports while the lane has not settled on one.
    sds_speed = (as2xxxx_serdes_speed_t)FIELD_GET16(AS2XXXX_REG_MASK_SDS_LINK_SPEED, out_data[0]);
    *speed = mesa_speed_from_serdes(sds_speed);

    return rc;
}

mepa_rc as2xxxx_priv_get_link_status_line(as2xxxx_priv_data_t *const pd,
                                          mepa_bool_t *const         is_link_up)
{
    mepa_rc rc = MEPA_RC_OK;
    NULL_CHECK(is_link_up);
    rc = get_link_status(pd, AS2XXXX_SIDE_LINE, is_link_up);
    return rc;
}

mepa_rc as2xxxx_priv_set_loopback_mode_host(as2xxxx_priv_data_t *const pd, mepa_bool_t set)
{
    mepa_device_t *const  dev = pd->dev;
    mepa_rc               rc = MEPA_RC_OK;
    as2xxxx_host_inband_t inband = AS2XXXX_HOST_INBAND_UNKNOWN;
    mepa_bool_t           is_line_up = false;

    // NOTE: the register addresses and bit positions below are not documented and no meaning is
    // available for them, so they are written exactly as the sequence requires. See the README.

    // The first register is the one thing that differs between the families - AS21xxx uses 0x808a
    // and AS22xxx 0x90a - and the bits written into it do not, so the address is picked below and
    // everything else is shared. The sequence must settle between the writes. One second per step
    // is optimal.
    const uint16_t    regaddr2 = 0x8829;
    const uint16_t    mask1 = (BIT16(0) | BIT16(1) | BIT16(10) | BIT16(11) | BIT16(12));
    const uint16_t    val1 = (BIT16(1) | BIT16(10) | BIT16(11));
    const uint16_t    mask2 = BIT16(7);
    const uint16_t    val2 = mask2;
    const uint16_t    mask3 = BIT16(1);
    const uint16_t    val3 = mask3;
    const mepa_bool_t is_as22xxx = (pd->family == AS2XXXX_FAMILY_AS22XXX);
    const uint16_t    regaddr1 = is_as22xxx ? 0x90a : 0x808a;
    const uint16_t    settle_ms = 1000;

    LOG_D("Host loopback %s requested, currently %s", set ? "on" : "off",
          pd->is_host_loopback_ena ? "on" : "off");

    // Neither a disable nor a status query exists for this loopback, so the only request to
    // disable it that can be honoured is one where it was never enabled. Asking whether it was
    // keeps "leave the loopback off" - which is what every caller that does not want loopback at
    // all asks for - from failing over a limitation it never reaches.
    if (!set) {
        if (!pd->is_host_loopback_ena) {
            return MEPA_RC_OK;
        }

        LOG_E("Host loopback cannot be disabled once enabled, the PHY needs a hard reset to "
              "clear it");
        return MEPA_RC_NOT_IMPLEMENTED;
    }

    // No "already enabled" shortcut on the cached flag, tempting as it is - the sequence sleeps
    // three seconds holding the device lock. The hardware can lose the loopback without telling
    // this layer, and with no way to disable it a skipped re-apply would strand the port. The
    // re-apply is idempotent.

    if (pd->family == AS2XXXX_FAMILY_UNKNOWN) {
        LOG_E("Cannot set the host loopback without a known family");
        return MEPA_RC_ERROR;
    }

    // Two preconditions. The lane has to be in XFI, which is in-band disabled: with rate
    // adaptation running the firmware owns the host PCS and the loopback does not work. This one
    // is a configuration error the caller has to fix, so it is refused rather than warned about.
    RC_LOG(get_host_cfg(pd, &inband, DONT_CARE, DONT_CARE),
           "Failed to read the host config before setting the host loopback");
    if (inband != AS2XXXX_HOST_INBAND_DIS) {
        LOG_E("Host loopback needs the host lane in XFI, so in-band disabled - it is enabled, "
              "and this loopback is supported in XFI only");
        return MEPA_RC_ERROR;
    }

    // The other is that the line link has to be up. Unlike the lane mode this is transient, and
    // the loopback starts working by itself once the line comes up, so it is only worth saying
    // out loud - a caller that configures the loopback before plugging the cable in is not wrong.
    RC_LOG(get_link_status(pd, AS2XXXX_SIDE_LINE, &is_line_up),
           "Failed to read the line link status before setting the host loopback");
    if (!is_line_up) {
        LOG_W("Host loopback is being set with the line link down - it will not loop until the "
              "line comes up, which this mode requires");
    }

    RC_LOG(set_mmd_reg_access_side(pd, AS2XXXX_SIDE_HOST), ERR_MSG_ACCESS_SIDE_HOST);
    // Enable loopback and configure sampling parameters
    RC_LOG(mdio_mmd_modify(dev, AS2XXXX_DEV_PMA_PMD, regaddr1, mask1, val1), ERR_MSG_HOST_LOOPBACK);
    MEPA_MSLEEP(settle_ms);
    // Enable sample bypass function
    RC_LOG(mdio_mmd_modify(dev, AS2XXXX_DEV_PCS, regaddr2, mask2, val2), ERR_MSG_HOST_LOOPBACK);
    MEPA_MSLEEP(settle_ms);
    // Route RX data to TX for loopback
    RC_LOG(mdio_mmd_modify(dev, AS2XXXX_DEV_PCS, regaddr2, mask3, val3), ERR_MSG_HOST_LOOPBACK);
    MEPA_MSLEEP(settle_ms);

    // The control is handed back to the line side here.
    RC_LOG(set_mmd_reg_access_side(pd, AS2XXXX_SIDE_LINE), ERR_MSG_ACCESS_SIDE_LINE);

    pd->is_host_loopback_ena = true;

    return rc;
}

mepa_rc as2xxxx_priv_set_loopback_mode_line(as2xxxx_priv_data_t *const pd, mepa_bool_t set)
{
    mepa_rc                 rc = MEPA_RC_OK;
    as2xxxx_host_inband_t   inband = AS2XXXX_HOST_INBAND_UNKNOWN;
    as2xxxx_serdes_speed_t  sds_speed = AS2XXXX_SERDES_SPEED_1G;
    as2xxxx_dp_mode_t       readback = AS2XXXX_DP_MODE_DEFAULT;
    size_t                  restart;
    as2xxxx_serdes_opmode_t op_mode =
        (set ? AS2XXXX_SERDES_OPMODE_LOOPBACK : AS2XXXX_SERDES_OPMODE_NORMAL);

    // Both are what the line loopback sequence requires.
    const size_t   sds_restarts = 2;
    const uint32_t settle_ms = 300;

    LOG_D("Line loopback %s, family %s", set ? "on" : "off",
          as2xxxx_priv_family_to_string(pd->family));

    if (pd->family != AS2XXXX_FAMILY_AS22XXX) {
        // AS21xxx carries the operating mode as a third byte of the host config, so
        // the in-band setting and speed sharing that word have to be preserved.
        RC_LOG(get_host_cfg(pd, &inband, &sds_speed, DONT_CARE),
               "Failed to get the current host SerDes config");
        RC_LOG(set_host_cfg(pd, inband, sds_speed, op_mode),
               "Failed to set the host SerDes config");
        return rc;
    }

    // AS22xxx has no way out of this loopback, so the only request to disable it that can be
    // honoured is one where it was never enabled. An enable is always re-applied rather than
    // skipped on the cached flag, for the reason given in as2xxxx_priv_set_loopback_mode_host().
    if (!set && !pd->is_line_loopback_ena) {
        return MEPA_RC_OK;
    }

    if (!set) {
        LOG_E("Line loopback cannot be disabled once enabled, the PHY needs a hard reset to "
              "clear it");
        return MEPA_RC_NOT_IMPLEMENTED;
    }

    RC_LOG(set_host_cfg(pd, AS2XXXX_HOST_INBAND_DIS, AS2XXXX_SERDES_SPEED_10G,
                        AS2XXXX_SERDES_OPMODE_NORMAL),
           "Failed to pin the host lane for the line loopback");

    RC_LOG(set_dp_mode(pd, AS2XXXX_DP_MODE_RMT_LOOPBACK),
           "Failed to set the datapath loopback mode");

    for (restart = 0; restart < sds_restarts; restart++) {
        MEPA_MSLEEP(settle_ms);
        RC_LOG(reset_sds(pd), "Failed to restart the host SerDes");
    }

    pd->is_line_loopback_ena = true;

    // The firmware is asked what mode it ended up in, because a write that the datapath quietly
    // refuses looks exactly like one it took.
    RC_LOG(get_dp_mode(pd, &readback), "Failed to read the datapath mode back");
    if (readback != AS2XXXX_DP_MODE_RMT_LOOPBACK) {
        LOG_E("Datapath loopback mode was requested but the firmware reports mode %u",
              (uint32_t)readback);
        return MEPA_RC_ERROR;
    }

    return rc;
}

mepa_rc as2xxxx_priv_is_loopback_mode_host(as2xxxx_priv_data_t *const pd,
                                           mepa_bool_t *const         is_loopback)
{
    NULL_CHECK(is_loopback);
    *is_loopback = pd->is_host_loopback_ena;

    return MEPA_RC_OK;
}

mepa_rc as2xxxx_priv_is_loopback_mode_line(as2xxxx_priv_data_t *const pd,
                                           mepa_bool_t *const         is_loopback)
{
    mepa_rc                 rc = MEPA_RC_OK;
    as2xxxx_serdes_opmode_t opmode = AS2XXXX_SERDES_OPMODE_NORMAL;
    as2xxxx_dp_mode_t       dp_mode = AS2XXXX_DP_MODE_DEFAULT;

    NULL_CHECK(is_loopback);

    if (pd->family == AS2XXXX_FAMILY_AS22XXX) {
        RC_LOG(get_dp_mode(pd, &dp_mode), "Failed to get the datapath mode");
        *is_loopback = (dp_mode == AS2XXXX_DP_MODE_RMT_LOOPBACK);
    } else {
        RC_LOG(get_host_cfg(pd, DONT_CARE, DONT_CARE, &opmode),
               "Failed to get the current host SerDes config");
        *is_loopback = (opmode == AS2XXXX_SERDES_OPMODE_LOOPBACK);
    }
    return rc;
}

mepa_rc as2xxxx_priv_set_tx_fir(as2xxxx_priv_data_t *const         pd,
                                const as2xxxx_tx_fir_conf_t *const cfg)
{
    mepa_device_t *const dev = pd->dev;
    mepa_rc              rc = MEPA_RC_OK;
    ipc_operation_t      op = {0};
    const uint8_t        sds_id = 0;

    NULL_CHECK(cfg);

    op.cmd = AS2XXXX_IPC_DBGCMD_SUBCMD_SDS;

    switch (pd->family) {
    case AS2XXXX_FAMILY_AS21XXX:
        // Four bytes: SerDes id, pre, main, post.
        op.subcmd = AS2XXXX_IPC_DBGCMD_SDS_SUBCMD_AS21XXX_TXFIR_SET;
        op.in_data.buf[op.in_data.len++] =
            FIELD_PREP16(U16_HIGH_BYTE, cfg->pre) | FIELD_PREP16(U16_LOW_BYTE, sds_id);
        op.in_data.buf[op.in_data.len++] =
            FIELD_PREP16(U16_HIGH_BYTE, cfg->post) | FIELD_PREP16(U16_LOW_BYTE, cfg->main);
        break;

    case AS2XXXX_FAMILY_AS22XXX:
        // Three bytes: pre, main, post, with no SerDes id.
        op.subcmd = AS2XXXX_IPC_DBGCMD_SDS_SUBCMD_AS22XXX_TXFIR_SET;
        op.in_data.buf[op.in_data.len++] =
            FIELD_PREP16(U16_HIGH_BYTE, cfg->main) | FIELD_PREP16(U16_LOW_BYTE, cfg->pre);
        op.in_data.buf[op.in_data.len++] = FIELD_PREP16(U16_LOW_BYTE, cfg->post);
        break;

    case AS2XXXX_FAMILY_UNKNOWN:
    default:                     {
        LOG_E("Cannot set the Tx FIR without a known family");
        return MEPA_RC_ERROR;
    }
    }

    rc = ipc_do_operation(dev, &pd->ipc, &op);

    if (rc != MEPA_RC_OK) {
        LOG_E("Failed to set tx fir");
    }

    return rc;
}

mepa_rc as2xxxx_priv_get_host_inband(as2xxxx_priv_data_t *const   pd,
                                     as2xxxx_host_inband_t *const inband)
{
    mepa_rc rc = MEPA_RC_OK;

    NULL_CHECK(inband);

    rc = get_host_inband(pd, inband);

    return rc;
}

mepa_rc as2xxxx_priv_get_host_lane_rate(as2xxxx_priv_data_t *const pd,
                                        mesa_port_speed_t *const   speed)
{
    mepa_rc                rc = MEPA_RC_OK;
    as2xxxx_serdes_speed_t sds_speed = AS2XXXX_SERDES_SPEED_INVALID;

    NULL_CHECK(speed);

    RC_LOG(get_host_cfg(pd, DONT_CARE, &sds_speed, DONT_CARE),
           "Failed to get the configured host lane rate");

    *speed = mesa_speed_from_serdes(sds_speed);
    return rc;
}

mepa_rc as2xxxx_priv_set_temp_monitor_conf(as2xxxx_priv_data_t *const               pd,
                                           const as2xxxx_temp_monitor_conf_t *const conf)
{
    mepa_device_t *const dev = pd->dev;
    mepa_rc              rc = MEPA_RC_OK;
    ipc_msg_t            msg = {0};

    NULL_CHECK(conf);

    msg.opcode = AS2XXXX_IPC_CMD_CFG_PARAM;
    msg.get_response = false;
    msg.io_data.buf[msg.io_data.len++] = AS2XXXX_IPC_CFG_PARAM_SUBCMD_DIRECT;
    msg.io_data.buf[msg.io_data.len++] = AS2XXXX_IPC_CFG_PARAM_DIRECT_SUBCMD_TEMP_MONITOR;

    if (conf->enable) {
        msg.io_data.buf[msg.io_data.len++] = AS2XXXX_CFG_TEMP_MONITOR_SUBCMD_SET_CFG;
        msg.io_data.buf[msg.io_data.len++] = conf->config;
        RC_LOG(ipc_send_msg(dev, &pd->ipc, &msg), "Failed to set the temperature monitor config");
        msg.io_data.len = 2; // Reset length. The first 2 entries remain the same
    }

    msg.io_data.buf[msg.io_data.len++] =
        conf->enable ? AS2XXXX_CFG_TEMP_MONITOR_SUBCMD_START : AS2XXXX_CFG_TEMP_MONITOR_SUBCMD_STOP;

    RC_LOG(ipc_send_msg(dev, &pd->ipc, &msg), "Failed to set the temperature monitor config");
    return rc;
}

mepa_rc as2xxxx_priv_get_temp_monitor_last_sample(as2xxxx_priv_data_t *const pd,
                                                  int16_t *const             temp_celsius,
                                                  uint16_t *const            temp_celsius_frac)
{
    mepa_device_t *const dev = pd->dev;
    mepa_rc              rc = MEPA_RC_OK;
    ipc_msg_t            msg = {0};
    int32_t              raw_q16_16;
    int32_t              temp_hundredths;

    NULL_CHECK(temp_celsius);

    msg.opcode = AS2XXXX_IPC_CMD_CFG_PARAM;
    msg.get_response = true;
    msg.io_data.buf[msg.io_data.len++] = AS2XXXX_IPC_CFG_PARAM_SUBCMD_DIRECT;
    msg.io_data.buf[msg.io_data.len++] = AS2XXXX_IPC_CFG_PARAM_DIRECT_SUBCMD_TEMP_MONITOR;
    msg.io_data.buf[msg.io_data.len++] = AS2XXXX_CFG_TEMP_MONITOR_SUBCMD_GET_SAMPLE;

    rc = ipc_send_msg(dev, &pd->ipc, &msg);

    if (rc != MEPA_RC_OK) {
        LOG_E("Failed to get the temperature sample");
    } else if (msg.io_data.len < AS2XXXX_TEMP_SAMPLE_WORD_COUNT) {
        LOG_E("Temperature sample returned %u words, expected at least %u",
              (unsigned)msg.io_data.len, (unsigned)AS2XXXX_TEMP_SAMPLE_WORD_COUNT);
        rc = MEPA_RC_ERROR;
    } else {
        raw_q16_16 =
            (int32_t)(FIELD_PREP32(U32_HIGH_WORD, msg.io_data.buf[AS2XXXX_TEMP_SAMPLE_WORD_MSB]) |
                      FIELD_PREP32(U32_LOW_WORD, msg.io_data.buf[AS2XXXX_TEMP_SAMPLE_WORD_LSB]));

        // Convert the fixed-point sample to signed hundredths of a degree. The
        // multiply is widened because a Q16.16 sample scaled by 100 no longer fits
        // in 32 bits, and the scaling is a division rather than a shift so that it
        // stays defined for negative samples.
        temp_hundredths = (int32_t)(((int64_t)raw_q16_16 * AS2XXXX_TEMP_FRAC_PER_DEGREE) /
                                    AS2XXXX_TEMP_Q16_SCALE);

        if (temp_celsius != NULL) {
            // Truncates towards zero and keeps the sign.
            *temp_celsius = (int16_t)(temp_hundredths / AS2XXXX_TEMP_FRAC_PER_DEGREE);
        }

        if (temp_celsius_frac != NULL) {
            // The remainder carries the sign of the sample, which the caller does
            // not want in the fraction.
            *temp_celsius_frac = (uint16_t)MEPA_ABS(temp_hundredths % AS2XXXX_TEMP_FRAC_PER_DEGREE);
        }
    }

    return rc;
}

mepa_rc as2xxxx_priv_set_irq_event_conf(as2xxxx_priv_data_t *const      pd,
                                        const as2xxxx_irq_conf_t *const conf)
{
    mepa_device_t *const dev = pd->dev;
    mepa_rc              rc = MEPA_RC_OK;
    ipc_msg_t            msg = {0};

    NULL_CHECK(conf);

    // The firmware expects exactly this field order: event, enable, signal_type, pulses.
    msg.opcode = AS2XXXX_IPC_CMD_CFG_IRQ;
    msg.io_data.buf[msg.io_data.len++] = AS2XXXX_IPC_CFG_IRQ_SUBCMD_CONFIG;
    msg.io_data.buf[msg.io_data.len++] = conf->event;
    msg.io_data.buf[msg.io_data.len++] = (conf->enable ? 1 : 0);
    msg.io_data.buf[msg.io_data.len++] = conf->signal_type;
    msg.io_data.buf[msg.io_data.len++] = conf->number_of_pulses;

    rc = ipc_send_msg(dev, &pd->ipc, &msg);

    if (rc != MEPA_RC_OK) {
        LOG_E("Failed to configure irq events");
    }

    return rc;
}

mepa_rc as2xxxx_priv_get_irq_event_state(as2xxxx_priv_data_t *const pd,
                                         as2xxxx_irq_event_t        event,
                                         mepa_bool_t *const         is_active)
{
    mepa_rc rc = MEPA_RC_OK;

    NULL_CHECK(is_active);

    RC_LOG(get_irq_event(pd, event, is_active), "Failed to get irq event state");

    // Clear the event if it is active
    if (*is_active) {
        RC_LOG(clear_irq_event(pd, event), "Failed to clear irq event");
    }
    return rc;
}

mepa_rc as2xxxx_priv_get_counters(as2xxxx_priv_data_t *const pd,
                                  as2xxxx_counters_t *const  counters,
                                  mepa_bool_t                clear)
{
    mepa_rc rc = MEPA_RC_OK;

    NULL_CHECK(counters);

    // The two families report completely different counter sets over different
    // opcodes, so the caller has to be told which union member is valid.
    memset(counters, 0, sizeof(*counters));
    counters->family = pd->family;

    switch (pd->family) {
    case AS2XXXX_FAMILY_AS21XXX:
        rc = get_counters_as21xxx(pd, &counters->u.as21xxx, clear);

        if (rc != MEPA_RC_OK) {
            LOG_E("Failed to get AS21xxx counters");
        }
        break;

    case AS2XXXX_FAMILY_AS22XXX:
        rc = get_counters_as22xxx(pd, &counters->u.as22xxx, clear);

        if (rc != MEPA_RC_OK) {
            LOG_E("Failed to get AS22xxx counters");
        }
        break;

    case AS2XXXX_FAMILY_UNKNOWN:
    default:
        LOG_E("Cannot read counters without a known chip family");
        rc = MEPA_RC_ERROR;
        break;
    }

    return rc;
}

mepa_rc as2xxxx_priv_print_eye(as2xxxx_priv_data_t *const pd,
                               lmu_ss_t *const            ss,
                               void (*progress_func)(size_t done_groups, size_t total_groups))
{
    mepa_device_t *const dev = pd->dev;
    mepa_rc              rc = MEPA_RC_OK;
    eye_data_t           eye_data = {0};

    NULL_CHECK(ss);

    eye_data.ss = ss;
    eye_data.progress_func = progress_func;

    // The families order the scan request differently, and the reply gives no hint that
    // the order was wrong: the scan runs and returns a full group either way, just always
    // for the same phase. See eye_req_order_t.
    if (as2xxxx_priv_family_get(pd) == AS2XXXX_FAMILY_AS22XXX) {
        eye_data.req_order = EYE_REQ_GRP_LOW_BYTE;
    } else {
        eye_data.req_order = EYE_REQ_GRP_HIGH_BYTE;
    }

    rc = eye_print(dev, &pd->ipc, &eye_data);

    if (rc != MEPA_RC_OK) {
        LOG_E("Failed to print the eye diagram");
    }

    return rc;
}

mepa_rc as2xxxx_priv_reg_rd(as2xxxx_priv_data_t *const pd,
                            const uint16_t             reg_addr,
                            uint16_t *const            value)
{
    mepa_device_t *const dev = pd->dev;
    mepa_rc              rc = MEPA_RC_OK;

    NULL_CHECK(value);

    rc = mdio_rd(dev, reg_addr, value);

    return rc;
}

mepa_rc as2xxxx_priv_reg_rd_mmd(as2xxxx_priv_data_t *const pd,
                                const uint16_t             mmd,
                                const uint16_t             reg_addr,
                                uint16_t *const            value)
{
    mepa_device_t *const dev = pd->dev;
    mepa_rc              rc = MEPA_RC_OK;

    NULL_CHECK(value);

    rc = mdio_mmd_rd(dev, mmd, reg_addr, value);

    return rc;
}

mepa_rc as2xxxx_priv_reg_wr(as2xxxx_priv_data_t *const pd,
                            const uint16_t             reg_addr,
                            uint16_t const             value)
{
    mepa_device_t *const dev = pd->dev;
    mepa_rc              rc = MEPA_RC_OK;

    rc = mdio_wr(dev, reg_addr, value);

    return rc;
}

mepa_rc as2xxxx_priv_reg_wr_mmd(as2xxxx_priv_data_t *const pd,
                                const uint16_t             mmd,
                                const uint16_t             reg_addr,
                                uint16_t const             value)
{
    mepa_device_t *const dev = pd->dev;
    mepa_rc              rc = MEPA_RC_OK;

    rc = mdio_mmd_wr(dev, mmd, reg_addr, value);

    return rc;
}

// ------------------------------------------------------------------------------------------------
// Local functions
// ------------------------------------------------------------------------------------------------

static mepa_rc set_led_config(as2xxxx_priv_data_t *const pd, const ipc_led_config_t *const cfg)
{
    mepa_device_t *const dev = pd->dev;
    ipc_msg_t            msg = {0};

    msg.opcode = AS2XXXX_IPC_CMD_CFG_LED;
    msg.io_data.buf[msg.io_data.len++] = cfg->behavior.led0;
    msg.io_data.buf[msg.io_data.len++] = cfg->behavior.led1;
    msg.io_data.buf[msg.io_data.len++] = cfg->behavior.led2;
    msg.io_data.buf[msg.io_data.len++] = cfg->behavior.led3;
    msg.io_data.buf[msg.io_data.len++] = cfg->behavior.led4;
    msg.io_data.buf[msg.io_data.len++] = cfg->polarity; // supported only from FW 1.9.x
    msg.io_data.buf[msg.io_data.len++] = cfg->blink_rate;

    return ipc_send_msg(dev, &pd->ipc, &msg);
}

static mepa_rc set_mmd_reg_access_side(as2xxxx_priv_data_t *const pd, as2xxxx_side_t side)
{
    mepa_device_t *const dev = pd->dev;
    uint16_t             value =
        (side == AS2XXXX_SIDE_LINE) ? AS2XXXX_REG_ACCESS_LINE_VAL : AS2XXXX_REG_ACCESS_HOST_VAL;

    return mdio_mmd_vendor_modify(dev, AS2XXXX_REG_ADDR_CHIP_CTRL, AS2XXXX_REG_MASK_REG_ACCESS,
                                  value);
}

static mepa_rc set_line_speed_cfg(as2xxxx_priv_data_t *const pd,
                                  as2xxxx_mdi_speed_t        speed,
                                  mepa_bool_t                aneg_enable)
{
    mepa_device_t *const dev = pd->dev;
    ipc_msg_t            msg = {0};

    // Common for all ops below
    msg.opcode = AS2XXXX_IPC_CMD_CFG_PARAM;
    msg.get_response = false; // Don't care about the IPC msg response here
    msg.io_data.buf[msg.io_data.len++] = AS2XXXX_IPC_CFG_PARAM_SUBCMD_DIRECT;
    msg.io_data.buf[msg.io_data.len++] = AS2XXXX_IPC_CFG_PARAM_DIRECT_SUBCMD_CU_AN;

    // Set top speed
    msg.io_data.buf[msg.io_data.len++] = AS2XXXX_CFG_CU_AN_SUBCMD_TOP_SPEED;
    msg.io_data.buf[msg.io_data.len++] = speed;

    MEPA_RC(ipc_send_msg(dev, &pd->ipc, &msg));

    // Enable or disable auto-negotiation. When enabling, this comes before the restart rather
    // than after it: a restart issued while auto-negotiation is disabled is dropped, and enabling
    // afterwards does not begin a new one, so the line stays down until something reconfigures it a
    // second time. Reachable whenever a forced speed has been applied first, since that leaves
    // auto-negotiation off.
    MEPA_RC(set_aneg_enable(pd, aneg_enable));

    if (!aneg_enable) {
        // A forced speed is complete here: the top speed is written, auto-negotiation is off, and
        // no restart is sent - there is nothing to negotiate, and restarting would only begin an
        // attempt the line then has to abandon.
        return MEPA_RC_OK;
    }

    // Restart auto-negotiation. Kept last, which is also the order both vendor call sites use.
    msg.io_data.len = 2; // Move data len to 2nd idx as first 2 remain unchanged
    msg.io_data.buf[msg.io_data.len++] = AS2XXXX_CFG_CU_AN_SUBCMD_RESTART;

    return ipc_send_msg(dev, &pd->ipc, &msg);
}

static mepa_rc set_line_flow_control(as2xxxx_priv_data_t *const pd, mepa_bool_t enable)
{
    mepa_device_t *const dev = pd->dev;
    mepa_rc              rc = MEPA_RC_OK;

    RC_LOG(set_mmd_reg_access_side(pd, AS2XXXX_SIDE_LINE), ERR_MSG_ACCESS_SIDE_LINE);

    RC_LOG(mdio_mmd_modify(dev, AS2XXXX_DEV_AN, AS2XXXX_REG_ADDR_MII_ANEG_CTRL,
                           AS2XXXX_FLOW_CTRL_MASK,
                           enable ? AS2XXXX_FLOW_CTRL_ENABLE_VAL : AS2XXXX_FLOW_CTRL_DISABLE_VAL),
           "Applying the flow control advertisement to the PHY failed");
    return rc;
}

static mepa_rc set_aneg_enable(as2xxxx_priv_data_t *const pd, mepa_bool_t enable)
{
    mepa_device_t *const dev = pd->dev;
    ipc_operation_t      op = {0};

    op.cmd = AS2XXXX_IPC_DBGCMD_SUBCMD_CU_AN;
    op.subcmd = AS2XXXX_IPC_DBGCMD_CU_AN_SUBCMD_ENABLE;
    op.in_data.buf[op.in_data.len++] = (enable ? 1 : 0);

    return ipc_do_operation(dev, &pd->ipc, &op);
}

static mepa_rc set_phy_enable(as2xxxx_priv_data_t *const pd, as2xxxx_phy_enable_t enable)
{
    mepa_device_t *const dev = pd->dev;
    ipc_msg_t            msg = {0};

    // Suspend and resume go through this IPC on both families
    msg.opcode = AS2XXXX_IPC_CMD_CPU;
    msg.io_data.buf[msg.io_data.len++] = AS2XXXX_IPC_CPU_SUBCMD_PHY_ENABLE;
    msg.io_data.buf[msg.io_data.len++] = enable;

    return ipc_send_msg(dev, &pd->ipc, &msg);
}

static mepa_rc set_host_inband(as2xxxx_priv_data_t *const pd, as2xxxx_host_inband_t inband)
{
    mepa_device_t *const dev = pd->dev;
    ipc_operation_t      op = {0};

    // AS21xxx only. That family keeps the in-band setting in a command of its own, and expects the
    // speed field of the word to be zero, which is what leaving it unset gives. On AS22xxx this
    // same opcode is the combined host configuration, so going through here would clear the speed.
    op.cmd = AS2XXXX_IPC_DBGCMD_SUBCMD_DPC;
    op.subcmd = AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_RA_SET_CFG;
    op.in_data.buf[op.in_data.len++] = FIELD_PREP16(AS2XXXX_REG_MASK_HOST_CFG_INBAND, inband);

    return ipc_do_operation(dev, &pd->ipc, &op);
}

static mepa_rc set_host_inband_and_speed(as2xxxx_priv_data_t *const pd,
                                         as2xxxx_host_inband_t      inband,
                                         as2xxxx_serdes_speed_t     speed)
{
    mepa_rc                 rc = MEPA_RC_OK;
    as2xxxx_host_inband_t   current_inband = AS2XXXX_HOST_INBAND_UNKNOWN;
    as2xxxx_serdes_opmode_t opmode = AS2XXXX_SERDES_OPMODE_NORMAL;
    size_t                  attempt;
    mepa_bool_t             is_sds_up = false;
    mesa_port_speed_t       sds_speed = MESA_SPEED_UNDEFINED;
    const size_t            link_poll_attempts = 60;
    const uint32_t          link_poll_ms = 50;

    RC_LOG(get_host_cfg(pd, &current_inband, DONT_CARE, &opmode),
           "Failed to read the host config before changing it");

    if (pd->family == AS2XXXX_FAMILY_AS22XXX) {
        RC_LOG(set_host_cfg(pd, inband, speed, opmode), "Failed to write the host config");
    } else {
        RC_LOG(set_host_inband(pd, inband), "Failed to write the host in-band setting");
        RC_LOG(set_host_cfg(pd, current_inband, speed, opmode), "Failed to write the host config");
    }

    if (pd->family == AS2XXXX_FAMILY_AS22XXX) {
        LOG_D("Host configuration written, restarting the datapath FSM to apply it");

        RC_LOG(restart_dpc_fsm(pd),
               "Failed to restart the datapath FSM after the host config change");

        for (attempt = 0; attempt < link_poll_attempts; attempt++) {
            if (as2xxxx_priv_get_host_sds_state(pd, &is_sds_up, &sds_speed) == MEPA_RC_OK &&
                is_sds_up) {
                break;
            }
            MEPA_MSLEEP(link_poll_ms);
        }

        if (attempt == link_poll_attempts) {
            // Warned rather than failed: the lane may still come up a moment later, and returning
            // an error would leave the caller with a host configuration it thinks was not applied.
            LOG_W("The host SerDes link did not come up within %u ms of the host reconfiguration, "
                  "traffic may be lost until it does",
                  (uint32_t)(link_poll_attempts * link_poll_ms));
        } else {
            LOG_D("Host SerDes link up %u ms after the host reconfiguration",
                  (uint32_t)(attempt * link_poll_ms));
        }
    }

    return rc;
}

static mepa_rc get_host_inband(as2xxxx_priv_data_t *const pd, as2xxxx_host_inband_t *const inband)
{
    mepa_device_t *const dev = pd->dev;
    mepa_rc              rc = MEPA_RC_OK;
    ipc_operation_t      op = {0};
    uint16_t             out_data[1] = {0};

    NULL_CHECK(inband);

    switch (pd->family) {
    case AS2XXXX_FAMILY_AS21XXX:
        op.subcmd = AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS21XXX_RA_GET_CFG;
        break;

    case AS2XXXX_FAMILY_AS22XXX:
        op.subcmd = AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS22XXX_RA_GET_CFG;
        break;

    case AS2XXXX_FAMILY_UNKNOWN:
    default:
        LOG_E("Cannot read the host in-band setting without a known family");
        return MEPA_RC_ERROR;
    }

    op.cmd = AS2XXXX_IPC_DBGCMD_SUBCMD_DPC;
    op.out_data.buf = out_data;
    op.out_data.buf_words_max = ARRAY_SIZE(out_data);
    op.out_data.expected_words = ARRAY_SIZE(out_data);

    RC_LOG(ipc_do_operation(dev, &pd->ipc, &op), "Failed to get the host in-band config");

    if (op.out_data.actual_read_words != ARRAY_SIZE(out_data)) {
        *inband = AS2XXXX_HOST_INBAND_UNKNOWN;
        LOG_E("Short read of the host in-band config");
        return MEPA_RC_ERROR;
    }

    // Only the in-band field is taken. On AS22xxx this word also carries the host lane rate, which
    // is read through get_host_cfg instead - that one shares this opcode on AS22xxx and has its own
    // on AS21xxx, so it is the single place the whole word is handled.
    *inband = (as2xxxx_host_inband_t)FIELD_GET16(AS2XXXX_REG_MASK_HOST_CFG_INBAND, out_data[0]);

    if (*inband != AS2XXXX_HOST_INBAND_DIS && *inband != AS2XXXX_HOST_INBAND_ENA) {
        // Normalised rather than left as the raw field so a caller that ignores the return code
        // sees "unknown" instead of a bogus value.
        *inband = AS2XXXX_HOST_INBAND_UNKNOWN;
        LOG_E("PHY reported an unknown host in-band setting");
        return MEPA_RC_ERROR;
    }

    return rc;
}

static mepa_rc get_host_cfg(as2xxxx_priv_data_t *const     pd,
                            as2xxxx_host_inband_t *const   inband,
                            as2xxxx_serdes_speed_t *const  speed,
                            as2xxxx_serdes_opmode_t *const opmode)
{
    mepa_device_t *const dev = pd->dev;
    mepa_rc              rc = MEPA_RC_OK;
    ipc_operation_t      op = {0};
    uint16_t             out_data[2] = {0};
    size_t               words;

    switch (pd->family) {
    case AS2XXXX_FAMILY_AS21XXX:
        op.subcmd = AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS21XXX_HOST_CFG_GET;
        words = AS2XXXX_HOST_CFG_WORDS_AS21XXX; // pcs_sel + sds_spd, then op_mode
        break;

    case AS2XXXX_FAMILY_AS22XXX:
        op.subcmd = AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS22XXX_HOST_CFG_GET;
        words = AS2XXXX_HOST_CFG_WORDS_AS22XXX; // no op_mode on this family
        break;

    case AS2XXXX_FAMILY_UNKNOWN:
    default:                     {
        LOG_E("Cannot read the host config without a known family");
        return MEPA_RC_ERROR;
    }
    }

    op.cmd = AS2XXXX_IPC_DBGCMD_SUBCMD_DPC;
    op.out_data.buf = out_data;
    op.out_data.buf_words_max = ARRAY_SIZE(out_data);
    op.out_data.expected_words = words;

    RC_LOG(ipc_do_operation(dev, &pd->ipc, &op), "Failed to get the host SerDes config");

    if (op.out_data.actual_read_words != words) {
        LOG_E("Short read of the host SerDes config");
        return MEPA_RC_ERROR;
    }

    if (inband != NULL) {
        *inband = (as2xxxx_host_inband_t)FIELD_GET16(AS2XXXX_REG_MASK_HOST_CFG_INBAND, out_data[0]);
    }
    if (speed != NULL) {
        *speed = (as2xxxx_serdes_speed_t)FIELD_GET16(AS2XXXX_REG_MASK_HOST_CFG_SPEED, out_data[0]);
    }
    if (opmode != NULL) {
        // Only AS21xxx reports an operating mode; the other family has none, so
        // report the normal (non-loopback) mode there.
        *opmode = (pd->family == AS2XXXX_FAMILY_AS21XXX)
                      ? (as2xxxx_serdes_opmode_t)FIELD_GET16(U16_LOW_BYTE, out_data[1])
                      : AS2XXXX_SERDES_OPMODE_NORMAL;
    }

    return rc;
}

static mepa_rc set_host_cfg(as2xxxx_priv_data_t *const pd,
                            as2xxxx_host_inband_t      inband,
                            as2xxxx_serdes_speed_t     speed,
                            as2xxxx_serdes_opmode_t    opmode)
{
    mepa_device_t *const dev = pd->dev;
    ipc_operation_t      op = {0};

    op.cmd = AS2XXXX_IPC_DBGCMD_SUBCMD_DPC;
    op.in_data.buf[op.in_data.len++] = FIELD_PREP16(AS2XXXX_REG_MASK_HOST_CFG_INBAND, inband) |
                                       FIELD_PREP16(AS2XXXX_REG_MASK_HOST_CFG_SPEED, speed);

    switch (pd->family) {
    case AS2XXXX_FAMILY_AS21XXX:
        op.subcmd = AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS21XXX_HOST_CFG_SET;
        op.in_data.buf[op.in_data.len++] = FIELD_PREP16(U16_LOW_BYTE, opmode);
        break;

    case AS2XXXX_FAMILY_AS22XXX:
        // This family has no operating mode byte, so opmode is not sent.
        op.subcmd = AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS22XXX_HOST_CFG_SET;
        break;

    case AS2XXXX_FAMILY_UNKNOWN:
    default:                     {
        LOG_E("Cannot set the host config without a known family");
        return MEPA_RC_ERROR;
    }
    }

    return ipc_do_operation(dev, &pd->ipc, &op);
}

static mepa_rc enable_dpc_ra(as2xxxx_priv_data_t *const pd)
{
    mepa_device_t *const dev = pd->dev;
    mepa_rc              rc = MEPA_RC_ERROR;
    ipc_msg_t            msg = {0};
    size_t               attempt;
    const size_t         max_attempts = 5;
    const size_t         retry_ms = 10;

    for (attempt = 0; attempt < max_attempts; attempt++) {
        msg.io_data.len = 0;
        msg.opcode = AS2XXXX_IPC_CMD_CFG_PARAM;
        msg.get_response = false;
        msg.io_data.buf[msg.io_data.len++] = AS2XXXX_IPC_CFG_PARAM_SUBCMD_DIRECT;
        msg.io_data.buf[msg.io_data.len++] = AS2XXXX_IPC_CFG_PARAM_DIRECT_SUBCMD_DPC_RA;

        rc = ipc_send_msg(dev, &pd->ipc, &msg);
        if (rc == MEPA_RC_OK) {
            break;
        }

        MEPA_MSLEEP(retry_ms);
    }

    if (rc != MEPA_RC_OK) {
        LOG_E("Failed to enable the datapath rate adaptation");
    }

    return rc;
}

static mepa_rc set_dpc_fsm(as2xxxx_priv_data_t *const pd, as2xxxx_cfg_dpc_fsm_t enable)
{
    mepa_device_t *const dev = pd->dev;
    ipc_msg_t            msg = {0};

    msg.opcode = AS2XXXX_IPC_CMD_CFG_PARAM;
    msg.get_response = false;
    msg.io_data.buf[msg.io_data.len++] = AS2XXXX_IPC_CFG_PARAM_SUBCMD_DIRECT;
    msg.io_data.buf[msg.io_data.len++] = AS2XXXX_IPC_CFG_PARAM_DIRECT_SUBCMD_DPC_FSM_START;
    msg.io_data.buf[msg.io_data.len++] = enable;

    return ipc_send_msg(dev, &pd->ipc, &msg);
}

static mepa_rc restart_dpc_fsm(as2xxxx_priv_data_t *const pd)
{
    mepa_rc      rc = MEPA_RC_OK;
    const size_t restart_delay_ms = 20;

    // AS22xxx only: the firmware needs the FSM bounced after a boot before the datapath settles.
    // AS21xxx has no equivalent and does not need it, so this is a deliberate no-op that reports
    // success there - callers can invoke it unconditionally on any family.
    if (pd->family != AS2XXXX_FAMILY_AS22XXX) {
        return MEPA_RC_OK;
    }

    MEPA_RC(set_dpc_fsm(pd, AS2XXXX_CFG_DPC_FSM_DISABLE));
    MEPA_MSLEEP(restart_delay_ms);
    MEPA_RC(set_dpc_fsm(pd, AS2XXXX_CFG_DPC_FSM_ENABLE));

    return rc;
}

static mepa_rc set_dp_mode(as2xxxx_priv_data_t *const pd, as2xxxx_dp_mode_t dp_mode)
{
    mepa_device_t *const dev = pd->dev;
    ipc_operation_t      op = {0};

    op.cmd = AS2XXXX_IPC_DBGCMD_SUBCMD_DPC;
    op.subcmd = AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS22XXX_DP_MODE_SET;
    op.in_data.buf[op.in_data.len++] = FIELD_PREP16(U16_LOW_BYTE, dp_mode);

    return ipc_do_operation(dev, &pd->ipc, &op);
}

static mepa_rc reset_sds(as2xxxx_priv_data_t *const pd)
{
    mepa_device_t *const dev = pd->dev;
    ipc_operation_t      op = {0};

    // There is one host SerDes and it is numbered 0, so the id is not exposed to callers.
    const uint16_t sds_id = 0;

    switch (pd->family) {
    case AS2XXXX_FAMILY_AS21XXX: op.subcmd = AS2XXXX_IPC_DBGCMD_SDS_SUBCMD_AS21XXX_RESET; break;
    case AS2XXXX_FAMILY_AS22XXX: op.subcmd = AS2XXXX_IPC_DBGCMD_SDS_SUBCMD_AS22XXX_RESET; break;

    case AS2XXXX_FAMILY_UNKNOWN:
    default:                     {
        LOG_E("Cannot reset the SerDes without a known family");
        return MEPA_RC_ERROR;
    }
    }

    op.cmd = AS2XXXX_IPC_DBGCMD_SUBCMD_SDS;
    op.in_data.buf[op.in_data.len++] = FIELD_PREP16(U16_LOW_BYTE, sds_id);

    return ipc_do_operation(dev, &pd->ipc, &op);
}

static mepa_rc get_dp_mode(as2xxxx_priv_data_t *const pd, as2xxxx_dp_mode_t *const dp_mode)
{
    mepa_device_t *const dev = pd->dev;
    mepa_rc              rc = MEPA_RC_OK;
    ipc_operation_t      op = {0};
    uint16_t             out_data[1] = {0};

    op.cmd = AS2XXXX_IPC_DBGCMD_SUBCMD_DPC;
    op.subcmd = AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS22XXX_DP_MODE_GET;
    op.out_data.buf = out_data;
    op.out_data.buf_words_max = ARRAY_SIZE(out_data);
    op.out_data.expected_words = ARRAY_SIZE(out_data);

    RC_LOG(ipc_do_operation(dev, &pd->ipc, &op), "Failed to get the datapath mode");

    if (op.out_data.actual_read_words != ARRAY_SIZE(out_data)) {
        LOG_E("Short read of the datapath mode");
        return MEPA_RC_ERROR;
    }

    *dp_mode = (as2xxxx_dp_mode_t)FIELD_GET16(U16_LOW_BYTE, out_data[0]);
    return rc;
}

static mepa_rc get_link_status(as2xxxx_priv_data_t *const pd,
                               as2xxxx_side_t             side,
                               mepa_bool_t *const         is_link_up)
{
    mepa_device_t *const dev = pd->dev;
    mepa_rc              rc = MEPA_RC_OK;
    uint16_t             regval;

    RC_LOG(set_mmd_reg_access_side(pd, side), "Failed to set register access side");

    // The MII_BMSR status register is located in MMD device 7 (same as AS2XXXX_DEV_AN).
    RC_LOG(mdio_mmd_rd(dev, AS2XXXX_DEV_AN, AS2XXXX_REG_ADDR_MII_STATUS, &regval),
           "Failed to read status register");

    *is_link_up = FIELD_GET16(AS2XXXX_REG_MASK_STATUS_LINK_STATUS, regval);
    return rc;
}

static mepa_rc get_irq_event(as2xxxx_priv_data_t *const pd,
                             as2xxxx_irq_event_t        event,
                             mepa_bool_t *const         is_active)
{
    mepa_device_t *const dev = pd->dev;
    mepa_rc              rc = MEPA_RC_OK;
    ipc_msg_t            msg = {0};
    uint16_t             irq_status_word, irq_status_word_mask = UINT16_MAX;
    size_t               i;
    const size_t         irq_query_response_words = 1;

    // Check if requested event is in the map
    for (i = 0; i < ARRAY_SIZE(s_event_mask_map); i++) {
        if (event == s_event_mask_map[i].event) {
            irq_status_word_mask = s_event_mask_map[i].status_word_mask;
            break;
        }
    }

    // Make sure we've found the mask
    if (irq_status_word_mask == UINT16_MAX) {
        LOG_E("Given event is invalid");
        return MEPA_RC_ERR_PARM;
    }

    msg.opcode = AS2XXXX_IPC_CMD_CFG_IRQ;
    msg.io_data.buf[msg.io_data.len++] = AS2XXXX_IPC_CFG_IRQ_SUBCMD_QUERY;
    msg.get_response = true;

    RC_LOG(ipc_send_msg(dev, &pd->ipc, &msg), "Failed to query irq events");

    // ipc_send_msg overwrites io_data.len with the response word count, so this
    // catches a missing or short reply without inspecting the payload.
    if (msg.io_data.len != irq_query_response_words) {
        LOG_E("IRQ query returned %u words, expected %u", (unsigned)msg.io_data.len,
              (unsigned)irq_query_response_words);
        return MEPA_RC_ERROR;
    }

    irq_status_word = msg.io_data.buf[0];
    *is_active = FIELD_GET16(irq_status_word_mask, irq_status_word);

    return rc;
}

static mepa_rc clear_irq_event(as2xxxx_priv_data_t *const pd, as2xxxx_irq_event_t event)
{
    mepa_device_t *const dev = pd->dev;
    ipc_msg_t            msg = {0};

    msg.opcode = AS2XXXX_IPC_CMD_CFG_IRQ;
    msg.io_data.buf[msg.io_data.len++] = AS2XXXX_IPC_CFG_IRQ_SUBCMD_CLEAR;
    msg.io_data.buf[msg.io_data.len++] = event;

    return ipc_send_msg(dev, &pd->ipc, &msg);
}

static mepa_rc reboot_cpu_warm(as2xxxx_priv_data_t *const pd)
{
    mepa_device_t *const dev = pd->dev;
    ipc_msg_t            msg = {0};

    msg.opcode = AS2XXXX_IPC_CMD_CPU;
    msg.io_data.buf[msg.io_data.len++] = AS2XXXX_IPC_CPU_SUBCMD_SYS_REBOOT;

    return ipc_send_msg(dev, &pd->ipc, &msg);
}

static mepa_rc load_and_boot_firmware(as2xxxx_priv_data_t *const  pd,
                                      const fw_load_data_t *const fw_data)
{
    mepa_device_t *const dev = pd->dev;
    uint16_t             val;
    const uint16_t       indirect_status_max = 1;

    // 1. Write CPU_CTRL register to hold CPU in reset state,
    MEPA_RC(mdio_mmd_vendor_modify(dev, AS2XXXX_REG_ADDR_CPU_CTRL, AS2XXXX_REG_MASK_CPU_CTRL,
                                   AS2XXXX_CPU_CTRL_FW_LOAD_VAL));

    // 2. Write MDIO boot address
    MEPA_RC(mdio_mmd_vendor_wr(dev, AS2XXXX_REG_ADDR_FW_START, AS2XXXX_MDIO_BOOT_ADDR_VAL));

    // 3. Set MDIO indirect access command.
    MEPA_RC(mdio_mmd_vendor_modify(dev, AS2XXXX_REG_ADDR_MDIO_INDIRECT_ADDRCMD,
                                   AS2XXXX_REG_MASK_MDIO_INDIRECT_ADDRCMD,
                                   AS2XXXX_MDIO_INDIRECT_ADDRCMD_VAL));

    // 4. Write FW image data to register INDIRECT_LOAD.
    MEPA_RC(mdio_mmd_vendor_rd(dev, AS2XXXX_REG_ADDR_MDIO_INDIRECT_STATUS, &val));

    if (val > indirect_status_max) {
        return MEPA_RC_ERROR;
    }

    MEPA_RC(load_fw_img(pd, fw_data));

    // 5. Write boot address to CPU_BOOT_ADDR register.
    MEPA_RC(mdio_mmd_vendor_wr(dev, AS2XXXX_REG_ADDR_CPU_RESET_ADDR_LO_BASEADDR,
                               FIELD_GET32(U32_LOW_WORD, AS2XXXX_CPU_BOOT_ADDR_VAL)));
    MEPA_RC(mdio_mmd_vendor_wr(dev, AS2XXXX_REG_ADDR_CPU_RESET_ADDR_HI_BASEADDR,
                               FIELD_GET32(U32_HIGH_WORD, AS2XXXX_CPU_BOOT_ADDR_VAL)));

    // 6. Release the CPU (register CPU_CTRL).
    MEPA_RC(mdio_mmd_vendor_modify(dev, AS2XXXX_REG_ADDR_CPU_CTRL, AS2XXXX_REG_MASK_CPU_CTRL,
                                   AS2XXXX_CPU_CTRL_FW_START_VAL));

    return MEPA_RC_OK;
}

static mepa_rc fw_image_get(as2xxxx_family_t family, fw_load_data_t *const fw_data)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(s_fw_images); i++) {
        if (s_fw_images[i].family != family) {
            continue;
        }

        fw_data->img = s_fw_images[i].img;
        fw_data->img_size = s_fw_images[i].img_size;

        LOG_D("Using the %s firmware image (%u bytes)", as2xxxx_priv_family_to_string(family),
              (uint32_t)s_fw_images[i].img_size);

        return MEPA_RC_OK;
    }

    LOG_E("No firmware image built in for %s", as2xxxx_priv_family_to_string(family));
    return MEPA_RC_ERROR;
}

static mepa_rc load_fw_img(as2xxxx_priv_data_t *const pd, const fw_load_data_t *const fw_data)
{
    mepa_device_t *const dev = pd->dev;
    size_t               offset = 0;
    size_t               remaining = 0;
    size_t               chunk = 0;
    size_t               i = 0;
    uint8_t              byte = 0;
    uint8_t              low = 0;
    uint16_t             word = 0;
    mepa_bool_t          has_low = false;
    const size_t         im_max_chunk_size = (4 * 1024);

    // Progress report initialization (only if progress tracking is enabled)
    size_t      next_report = 0;
    size_t      done = 0;
    mepa_bool_t track_progress = (fw_data->progress_track.progress_func != NULL);

    if (track_progress) {
        // Initial display
        next_report = (fw_data->progress_track.step_size_bytes != 0u)
                          ? fw_data->progress_track.step_size_bytes
                          : (size_t)-1;

        fw_data->progress_track.progress_func(0, fw_data->img_size);
    }

    while (offset < fw_data->img_size) {
        remaining = fw_data->img_size - offset;
        chunk = (remaining > im_max_chunk_size) ? im_max_chunk_size : remaining;

        for (i = 0; i < chunk; i++) {
            byte = fw_data->img[offset + i];

            if (!has_low) {
                // first byte is the low byte (little-endian)
                low = byte;
                has_low = true;
            } else {
                // second byte is the high byte
                word = FIELD_PREP16(U16_HIGH_BYTE, byte) | FIELD_PREP16(U16_LOW_BYTE, low);
                MEPA_RC(mdio_mmd_vendor_wr(dev, AS2XXXX_REG_ADDR_MDIO_INDIRECT_LOAD, word));
                has_low = false;
            }

            // Progress updates
            if (track_progress) {
                done++;
                if (fw_data->progress_track.step_size_bytes != 0u && done >= next_report) {
                    fw_data->progress_track.progress_func(done, fw_data->img_size);
                    next_report += fw_data->progress_track.step_size_bytes;
                }
            }
        }

        offset += chunk;
    }

    // If overall length is odd, pad high byte with 0x00 and write final word
    if (has_low) {
        word = FIELD_PREP16(U16_LOW_BYTE, low); // high byte is 0
        MEPA_RC(mdio_mmd_vendor_wr(dev, AS2XXXX_REG_ADDR_MDIO_INDIRECT_LOAD, word));
    }

    // Final progress report
    if (track_progress) {
        fw_data->progress_track.progress_func(fw_data->img_size, fw_data->img_size);
    }

    return MEPA_RC_OK;
}

static inline uint32_t extract_counter_u32(const uint16_t *buf, size_t counter_idx)
{
    const size_t word_idx = counter_idx * AS2XXXX_COUNTER_SIZE_WORDS;
    return FIELD_PREP32(U32_HIGH_WORD, buf[word_idx + 1]) |
           FIELD_PREP32(U32_LOW_WORD, buf[word_idx]);
}

static inline uint64_t extract_counter_u64(const uint16_t *buf, size_t counter_idx)
{
    const uint32_t lsb = extract_counter_u32(buf, counter_idx);
    const uint32_t msb = extract_counter_u32(buf, counter_idx + AS2XXXX_COUNTER_SLOTS_U32);
    return ((uint64_t)msb << AS2XXXX_COUNTER_SIZE_BITS) | lsb;
}

static void extract_pkt_block(const uint16_t            *buf,
                              size_t                     first_counter_idx,
                              as2xxxx_pkt_block_t *const blk)
{
    blk->frm_cnt = extract_counter_u64(buf, first_counter_idx + AS2XXXX_PKT_BLOCK_SLOT_FRM_CNT);
    blk->crc_err = extract_counter_u64(buf, first_counter_idx + AS2XXXX_PKT_BLOCK_SLOT_CRC_ERR);
    blk->err_sym = extract_counter_u64(buf, first_counter_idx + AS2XXXX_PKT_BLOCK_SLOT_ERR_SYM);
    blk->runt = extract_counter_u32(buf, first_counter_idx + AS2XXXX_PKT_BLOCK_SLOT_RUNT);
    blk->oversize = extract_counter_u32(buf, first_counter_idx + AS2XXXX_PKT_BLOCK_SLOT_OVERSIZE);
}

static mepa_rc read_counter_block(as2xxxx_priv_data_t *const pd,
                                  uint16_t                   cmd,
                                  uint16_t                   subcmd,
                                  uint16_t *const            buf,
                                  size_t                     words)
{
    mepa_device_t *const dev = pd->dev;
    ipc_operation_t      op = {0};

    memset(buf, 0, words * sizeof(*buf));

    op.cmd = cmd;
    op.subcmd = subcmd;
    op.out_data.buf = buf;
    op.out_data.buf_words_max = words;
    op.out_data.expected_words = words;

    MEPA_RC(ipc_do_operation(dev, &pd->ipc, &op));

    if (op.out_data.actual_read_words != words) {
        LOG_E("Counter read 0x%02x/0x%02x returned %u of %u words", cmd, subcmd,
              (uint32_t)op.out_data.actual_read_words, (uint32_t)words);
        return MEPA_RC_ERROR;
    }

    return MEPA_RC_OK;
}

static mepa_rc clear_counter_block(as2xxxx_priv_data_t *const pd, uint16_t cmd, uint16_t subcmd)
{
    mepa_device_t *const dev = pd->dev;
    ipc_operation_t      op = {0};

    op.cmd = cmd;
    op.subcmd = subcmd;

    return ipc_do_operation(dev, &pd->ipc, &op);
}

static mepa_rc get_counters_as21xxx(as2xxxx_priv_data_t *const        pd,
                                    as2xxxx_counters_as21xxx_t *const counters,
                                    mepa_bool_t                       clear)
{
    size_t   i;
    uint16_t buffer_ngphy[AS2XXXX_COUNTER_NGPHY_BUFFER_SIZE_WORDS];
    uint16_t buffer_cu_an[AS2XXXX_COUNTER_CU_AN_BUFFER_SIZE_WORDS];
    uint16_t buffer_dpc[AS2XXXX_COUNTER_DPC_BUFFER_SIZE_WORDS];

    const struct {
        uint16_t  cmd;
        uint16_t  get_subcmd;
        uint16_t  clear_subcmd;
        uint16_t *buffer;
        size_t    words;
    } queries[] = {
        {AS2XXXX_IPC_DBGCMD_SUBCMD_NGPHY, AS2XXXX_IPC_DBGCMD_NGPHY_SUBCMD_AS21XXX_GET_CNT,
         AS2XXXX_IPC_DBGCMD_NGPHY_SUBCMD_AS21XXX_CLEAR_CNT, buffer_ngphy,
         AS2XXXX_COUNTER_NGPHY_BUFFER_SIZE_WORDS},
        {AS2XXXX_IPC_DBGCMD_SUBCMD_CU_AN, AS2XXXX_IPC_DBGCMD_CU_AN_SUBCMD_GET_CNT,
         AS2XXXX_IPC_DBGCMD_CU_AN_SUBCMD_CLEAR_CNT,         buffer_cu_an,
         AS2XXXX_COUNTER_CU_AN_BUFFER_SIZE_WORDS},
        {AS2XXXX_IPC_DBGCMD_SUBCMD_DPC,   AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS21XXX_GET_CNT,
         AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS21XXX_CLEAR_CNT,   buffer_dpc,
         AS2XXXX_COUNTER_DPC_BUFFER_SIZE_WORDS  },
    };

    for (i = 0; i < ARRAY_SIZE(queries); i++) {
        MEPA_RC(read_counter_block(pd, queries[i].cmd, queries[i].get_subcmd, queries[i].buffer,
                                   queries[i].words));

        if (clear) {
            MEPA_RC(clear_counter_block(pd, queries[i].cmd, queries[i].clear_subcmd));
        }
    }

    counters->ngphy.err_blk = extract_counter_u32(buffer_ngphy, 0);
    counters->ngphy.ber = extract_counter_u32(buffer_ngphy, 1);
    counters->ngphy.hi_ber = extract_counter_u32(buffer_ngphy, 2);
    counters->ngphy.cfr = extract_counter_u32(buffer_ngphy, 3);
    counters->ngphy.cfr_err = extract_counter_u32(buffer_ngphy, 4);
    counters->cu_an.cu_an = extract_counter_u32(buffer_cu_an, 0);
    counters->dpc.ldpc_err = extract_counter_u32(buffer_dpc, 0);
    counters->dpc.eth_crc_err = extract_counter_u32(buffer_dpc, 1);
    counters->dpc.sds_crc_err = extract_counter_u32(buffer_dpc, 2);

    return MEPA_RC_OK;
}

static mepa_rc get_counters_as22xxx(as2xxxx_priv_data_t *const        pd,
                                    as2xxxx_counters_as22xxx_t *const counters,
                                    mepa_bool_t                       clear)
{
    size_t   i;
    uint16_t buffer_pkt[AS2XXXX_COUNTER_AS22XXX_PKT_DUMP_BUFFER_SIZE_WORDS];
    uint16_t buffer_single[AS2XXXX_COUNTER_AS22XXX_SINGLE_BUFFER_SIZE_WORDS];

    // Where the last two counters of the dump sit, after the four blocks.
    const size_t trailing_slot =
        AS2XXXX_COUNTER_AS22XXX_PKT_BLOCKS * AS2XXXX_COUNTER_AS22XXX_PKT_BLOCK_SIZE;

    // Blocks are laid out back to back in the dump, in datapath order.
    as2xxxx_pkt_block_t *const blocks[AS2XXXX_COUNTER_AS22XXX_PKT_BLOCKS] = {
        &counters->from_sds,
        &counters->to_eth,
        &counters->from_eth,
        &counters->to_sds,
    };

    // Single-counter queries, each its own IPC round trip. The widths are not
    // uniform: the CFR counter is 16-bit, the rest are 32-bit.
    const struct {
        uint16_t  cmd;
        uint16_t  subcmd;
        size_t    words;
        uint32_t *dst;
    } singles[] = {
        {AS2XXXX_IPC_DBGCMD_SUBCMD_NGPHY, AS2XXXX_IPC_DBGCMD_NGPHY_SUBCMD_AS22XXX_LDPC_ERR,
         AS2XXXX_COUNTER_SIZE_WORDS,   &counters->ldpc_err         },
        {AS2XXXX_IPC_DBGCMD_SUBCMD_NGPHY, AS2XXXX_IPC_DBGCMD_NGPHY_SUBCMD_AS22XXX_CFR_CNT,
         AS2XXXX_COUNTER16_SIZE_WORDS, &counters->cfr_cnt          },
        {AS2XXXX_IPC_DBGCMD_SUBCMD_CU_AN, AS2XXXX_IPC_DBGCMD_CU_AN_SUBCMD_GET_CNT,
         AS2XXXX_COUNTER_SIZE_WORDS,   &counters->cu_an            },
        {AS2XXXX_IPC_DBGCMD_SUBCMD_SDS,   AS2XXXX_IPC_DBGCMD_SDS_SUBCMD_AS22XXX_LINK_DOWN_CNT,
         AS2XXXX_COUNTER_SIZE_WORDS,   &counters->sds_link_down_cnt},
    };

    // The packet-check block: one dump for all four measurement points
    MEPA_RC(read_counter_block(pd, AS2XXXX_IPC_DBGCMD_SUBCMD_DPC,
                               AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS22XXX_PKT_DUMP, buffer_pkt,
                               AS2XXXX_COUNTER_AS22XXX_PKT_DUMP_BUFFER_SIZE_WORDS));

    for (i = 0; i < AS2XXXX_COUNTER_AS22XXX_PKT_BLOCKS; i++) {
        extract_pkt_block(buffer_pkt, i * AS2XXXX_COUNTER_AS22XXX_PKT_BLOCK_SIZE, blocks[i]);
    }

    counters->sds_rx_ber = extract_counter_u32(buffer_pkt, trailing_slot);
    counters->sds_rx_err_blk =
        extract_counter_u32(buffer_pkt, trailing_slot + AS2XXXX_COUNTER_SLOTS_U32);

    // The individual counters. A missing one must not sink the whole read, so
    // these are best effort: on failure the field simply stays zero.
    for (i = 0; i < ARRAY_SIZE(singles); i++) {
        if (read_counter_block(pd, singles[i].cmd, singles[i].subcmd, buffer_single,
                               singles[i].words) != MEPA_RC_OK) {
            LOG_W("Failed to read counter 0x%02x/0x%02x, reporting 0", singles[i].cmd,
                  singles[i].subcmd);
            continue;
        }

        *singles[i].dst = (singles[i].words == AS2XXXX_COUNTER16_SIZE_WORDS)
                              ? buffer_single[0]
                              : extract_counter_u32(buffer_single, 0);
    }

    if (clear) {
        // Note there is no clear for the LDPC error counter.
        MEPA_RC(clear_counter_block(pd, AS2XXXX_IPC_DBGCMD_SUBCMD_DPC,
                                    AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS22XXX_PKT_CLR));
        MEPA_RC(clear_counter_block(pd, AS2XXXX_IPC_DBGCMD_SUBCMD_NGPHY,
                                    AS2XXXX_IPC_DBGCMD_NGPHY_SUBCMD_AS22XXX_CFR_CNT_CLR));
        MEPA_RC(clear_counter_block(pd, AS2XXXX_IPC_DBGCMD_SUBCMD_CU_AN,
                                    AS2XXXX_IPC_DBGCMD_CU_AN_SUBCMD_CLEAR_CNT));
        MEPA_RC(clear_counter_block(pd, AS2XXXX_IPC_DBGCMD_SUBCMD_SDS,
                                    AS2XXXX_IPC_DBGCMD_SDS_SUBCMD_AS22XXX_LINK_DOWN_CNT_CLR));
    }

    return MEPA_RC_OK;
}

static mesa_port_speed_t decode_line_speed(uint16_t regval)
{
    switch (regval & AS2XXXX_REG_MASK_LINE_SPEED_STATUS) {
    case AS2XXXX_LINE_SPEED_10G_VAL:   return MESA_SPEED_10G;
    case AS2XXXX_LINE_SPEED_5G_VAL:    return MESA_SPEED_5G;
    case AS2XXXX_LINE_SPEED_2500M_VAL: return MESA_SPEED_2500M;
    case AS2XXXX_LINE_SPEED_1G_VAL:    return MESA_SPEED_1G;
    case AS2XXXX_LINE_SPEED_100M_VAL:  return MESA_SPEED_100M;
    default:                           return MESA_SPEED_UNDEFINED;
    }
}
