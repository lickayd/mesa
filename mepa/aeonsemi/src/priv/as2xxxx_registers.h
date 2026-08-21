// Copyright (c) 2004-2026 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#ifndef AS2XXXX_REGISTERS_H_
#define AS2XXXX_REGISTERS_H_

#include "as2xxxx_bitfields.h"
#include <stdint.h>

#define U16(val) UINT16_C(val)

/** @brief Clause-45 MMD devices */
typedef enum {
    AS2XXXX_DEV_PMA_PMD = 1,
    AS2XXXX_DEV_PCS = 3,
    AS2XXXX_DEV_AN = 7,
    AS2XXXX_DEV_VENDOR = 30,
} as2xxxx_dev_t;

// ------------------------------------------------------------------------------------------------
// General registers
// ------------------------------------------------------------------------------------------------

// Addresses
#define AS2XXXX_REG_ADDR_DEV_ID_MSB    U16(0x0002)
#define AS2XXXX_REG_ADDR_DEV_ID_LSB    U16(0x0003)
#define AS2XXXX_REG_ADDR_MII_CTRL      U16(0xFFE0)
#define AS2XXXX_REG_ADDR_MII_STATUS    U16(0xFFE1)
#define AS2XXXX_REG_ADDR_MII_ANEG_CTRL U16(0xFFE4)
#define AS2XXXX_REG_ADDR_MII_ANEG_LP   U16(0xFFE5)

// Masks
#define AS2XXXX_REG_MASK_DEV_ID_MODEL       GENMASK32(7, 4)
#define AS2XXXX_REG_MASK_DEV_ID_REVISION    GENMASK32(3, 0)
#define AS2XXXX_REG_MASK_CTRL_PHY_RESET     BIT16(15)
#define AS2XXXX_REG_MASK_CTRL_FULL_DUPLEX   BIT16(8)
#define AS2XXXX_REG_MASK_CTRL_ANEG_RESTART  BIT16(9)
#define AS2XXXX_REG_MASK_STATUS_LINK_STATUS BIT16(2)
#define AS2XXXX_REG_MASK_STATUS_ANEG_DONE   BIT16(5)

// Pause capability, in the same two bit positions of the base page we advertise and of the base
// page the link partner advertised back. PAUSE is symmetric pause, ASM_DIR asymmetric - IEEE 802.3
// Annex 28B resolves the two ends' pairs into a direction, which is what get_flow_control_line()
// reproduces.
#define AS2XXXX_REG_MASK_ANEG_PAUSE     BIT16(10)
#define AS2XXXX_REG_MASK_ANEG_PAUSE_ASM BIT16(11)

// Values
#define AS2XXXX_DEV_ID_MODEL_AS21XXX_MAX 0x9u
#define AS2XXXX_DEV_ID_MODEL_AS22XXX     0xAu

/** @brief Both pause bits of the base page, the field flow control is applied to. */
#define AS2XXXX_FLOW_CTRL_MASK (AS2XXXX_REG_MASK_ANEG_PAUSE | AS2XXXX_REG_MASK_ANEG_PAUSE_ASM)

// MEPA carries flow control as one boolean, which is symmetric pause, so that is the bit set here.
// ASM_DIR is deliberately left clear: advertising it as well would let auto-negotiation settle on a
// one-directional outcome the caller never asked for.
#define AS2XXXX_FLOW_CTRL_ENABLE_VAL  AS2XXXX_REG_MASK_ANEG_PAUSE
#define AS2XXXX_FLOW_CTRL_DISABLE_VAL U16(0x0)

// ------------------------------------------------------------------------------------------------
// Vendor specific registers
// ------------------------------------------------------------------------------------------------

// Addresses
#define AS2XXXX_REG_ADDR_CHIP_CTRL                                                                 \
    U16(0x0002) // Same address as AS2XXXX_REG_ADDR_DEV_ID_MSB but MMD 30
#define AS2XXXX_REG_ADDR_SPEED_STATUS U16(0x4002)

// Masks
#define AS2XXXX_REG_MASK_REG_ACCESS        BIT16(15)
#define AS2XXXX_REG_MASK_LINE_SPEED_STATUS GENMASK16(7, 0)

// Values
#define AS2XXXX_REG_ACCESS_LINE_VAL  FIELD_PREP16(AS2XXXX_REG_MASK_REG_ACCESS, 0)
#define AS2XXXX_REG_ACCESS_HOST_VAL  FIELD_PREP16(AS2XXXX_REG_MASK_REG_ACCESS, 1)
#define AS2XXXX_LINE_SPEED_10G_VAL   FIELD_PREP16(AS2XXXX_REG_MASK_LINE_SPEED_STATUS, 0x3)
#define AS2XXXX_LINE_SPEED_5G_VAL    FIELD_PREP16(AS2XXXX_REG_MASK_LINE_SPEED_STATUS, 0x5)
#define AS2XXXX_LINE_SPEED_2500M_VAL FIELD_PREP16(AS2XXXX_REG_MASK_LINE_SPEED_STATUS, 0x9)
#define AS2XXXX_LINE_SPEED_1G_VAL    FIELD_PREP16(AS2XXXX_REG_MASK_LINE_SPEED_STATUS, 0x10)
#define AS2XXXX_LINE_SPEED_100M_VAL  FIELD_PREP16(AS2XXXX_REG_MASK_LINE_SPEED_STATUS, 0x20)

// ------------------------------------------------------------------------------------------------
// Firmware boot & CPU related registers and masks
// ------------------------------------------------------------------------------------------------

// Addresses
#define AS2XXXX_REG_ADDR_CPU_CTRL                   U16(0x000E)
#define AS2XXXX_REG_ADDR_FW_START                   U16(0x0100)
#define AS2XXXX_REG_ADDR_MDIO_INDIRECT_ADDRCMD      U16(0x0101)
#define AS2XXXX_REG_ADDR_MDIO_INDIRECT_LOAD         U16(0x0102)
#define AS2XXXX_REG_ADDR_MDIO_INDIRECT_STATUS       U16(0x0103)
#define AS2XXXX_REG_ADDR_CPU_RESET_ADDR_LO_BASEADDR U16(0x0003)
#define AS2XXXX_REG_ADDR_CPU_RESET_ADDR_HI_BASEADDR U16(0x0004)

// Masks
#define AS2XXXX_REG_MASK_CPU_CTRL              GENMASK16(4, 0)
#define AS2XXXX_REG_MASK_MDIO_INDIRECT_ADDRCMD GENMASK16(15, 2)

// Values
#define AS2XXXX_CPU_CTRL_FW_LOAD_VAL      (BIT16(4) | BIT16(2) | BIT16(1) | BIT16(0))
#define AS2XXXX_CPU_CTRL_FW_START_VAL     (BIT16(0))
#define AS2XXXX_MDIO_INDIRECT_ADDRCMD_VAL (BIT16(15) | BIT16(14))
#define AS2XXXX_MDIO_BOOT_ADDR_VAL        U16(0x1000)
#define AS2XXXX_CPU_BOOT_ADDR_VAL         UINT32_C(0x2000)

// ------------------------------------------------------------------------------------------------
// IPC status & command registers, masks and values
// ------------------------------------------------------------------------------------------------

// Addresses
#define AS2XXXX_REG_ADDR_IPC_CMD       U16(0x5801)
#define AS2XXXX_REG_ADDR_IPC_STATUS    U16(0x5802)
#define AS2XXXX_REG_ADDR_IPC_DATA0     U16(0x5808)
#define AS2XXXX_REG_ADDR_IPC_DATA1     U16(0x5809)
#define AS2XXXX_REG_ADDR_IPC_DATA2     U16(0x580a)
#define AS2XXXX_REG_ADDR_IPC_DATA3     U16(0x580b)
#define AS2XXXX_REG_ADDR_IPC_DATA4     U16(0x580c)
#define AS2XXXX_REG_ADDR_IPC_DATA5     U16(0x580d)
#define AS2XXXX_REG_ADDR_IPC_DATA6     U16(0x580e)
#define AS2XXXX_REG_ADDR_IPC_DATA7     U16(0x580f)
#define AS2XXXX_REG_ADDR_IPC_DATA_LAST AS2XXXX_REG_ADDR_IPC_DATA7
#define AS2XXXX_REG_ADDR_IPC_DATAn(_n) (AS2XXXX_REG_ADDR_IPC_DATA0 + (_n))

// Masks
#define AS2XXXX_REG_MASK_IPC_STS_STATUS GENMASK16(3, 0)
#define AS2XXXX_REG_MASK_IPC_STS_OPCODE GENMASK16(9, 4)
#define AS2XXXX_REG_MASK_IPC_STS_SIZE   GENMASK16(14, 10)
#define AS2XXXX_REG_MASK_IPC_STS_PARITY BIT16(15)
#define AS2XXXX_REG_MASK_IPC_CMD_SIZE   GENMASK16(10, 6)
#define AS2XXXX_REG_MASK_IPC_CMD_OPCODE GENMASK16(5, 0)

// Values
#define AS2XXXX_IPC_PARITY_BIT         BIT16(15)
#define AS2XXXX_IPC_STS_STATUS_RCVD    FIELD_PREP16(AS2XXXX_REG_MASK_IPC_STS_STATUS, 0x1)
#define AS2XXXX_IPC_STS_STATUS_PROCESS FIELD_PREP16(AS2XXXX_REG_MASK_IPC_STS_STATUS, 0x2)
#define AS2XXXX_IPC_STS_STATUS_SUCCESS FIELD_PREP16(AS2XXXX_REG_MASK_IPC_STS_STATUS, 0x4)
#define AS2XXXX_IPC_STS_STATUS_ERROR   FIELD_PREP16(AS2XXXX_REG_MASK_IPC_STS_STATUS, 0x8)
#define AS2XXXX_IPC_STS_STATUS_BUSY    FIELD_PREP16(AS2XXXX_REG_MASK_IPC_STS_STATUS, 0xe)
#define AS2XXXX_IPC_STS_STATUS_READY   FIELD_PREP16(AS2XXXX_REG_MASK_IPC_STS_STATUS, 0xf)

// ------------------------------------------------------------------------------------------------
// IPC commands
// ------------------------------------------------------------------------------------------------

/** @brief IPC commands (top-level). */
typedef enum {
    AS2XXXX_IPC_CMD_NOOP = U16(0x0),
    AS2XXXX_IPC_CMD_INFO = U16(0x1),
    AS2XXXX_IPC_CMD_CPU = U16(0x2),

    // Below set of commands is used for IPC operations
    AS2XXXX_IPC_CMD_DBGCMD = U16(0x16),
    AS2XXXX_IPC_CMD_POLL = U16(0x17),
    AS2XXXX_IPC_CMD_WR_BUF = U16(0x18),
    AS2XXXX_IPC_CMD_RD_BUF = U16(0x19),

    AS2XXXX_IPC_CMD_CFG_PARAM = U16(0x1a),
    AS2XXXX_IPC_CMD_CFG_IRQ = U16(0x22),
    AS2XXXX_IPC_CMD_CFG_LED = U16(0x23),
} as2xxxx_ipc_cmd_t;

/** @brief AS2XXXX_IPC_CMD_INFO subcommands (2nd-level). */
typedef enum {
    AS2XXXX_IPC_INFO_SUBCMD_VERSION = U16(0x1),
} as2xxxx_ipc_info_subcmd_t;

/** @brief AS2XXXX_IPC_CMD_CPU subcommands (2nd-level). */
typedef enum {
    AS2XXXX_IPC_CPU_SUBCMD_SYS_REBOOT = U16(0x3),
    AS2XXXX_IPC_CPU_SUBCMD_PHY_ENABLE = U16(0x6),
} as2xxxx_ipc_cpu_subcmd_t;

/** @brief Payload of AS2XXXX_IPC_CPU_SUBCMD_PHY_ENABLE. */
typedef enum {
    AS2XXXX_PHY_ENABLE_DISABLE = U16(0x0),
    AS2XXXX_PHY_ENABLE_ENABLE = U16(0x1),
} as2xxxx_phy_enable_t;

/** @brief AS2XXXX_IPC_CMD_DBGCMD subcommands (2nd-level). */
typedef enum {
    AS2XXXX_IPC_DBGCMD_SUBCMD_NGPHY = U16(0x80),
    AS2XXXX_IPC_DBGCMD_SUBCMD_DPC = U16(0x8B),
    AS2XXXX_IPC_DBGCMD_SUBCMD_SDS = U16(0x96),
    AS2XXXX_IPC_DBGCMD_SUBCMD_CU_AN = U16(0xA0),
} as2xxxx_ipc_dbgcmd_subcmd_t;

/** @brief AS2XXXX_IPC_CMD_CFG_PARAM subcommands (2nd-level). */
typedef enum {
    AS2XXXX_IPC_CFG_PARAM_SUBCMD_DIRECT = U16(0x4),
} as2xxxx_ipc_cfg_param_subcmd_t;

/** @brief AS2XXXX_IPC_CMD_CFG_IRQ subcommands (2nd-level). */
typedef enum {
    AS2XXXX_IPC_CFG_IRQ_SUBCMD_CONFIG = U16(0x0),
    AS2XXXX_IPC_CFG_IRQ_SUBCMD_QUERY = U16(0x1),
    AS2XXXX_IPC_CFG_IRQ_SUBCMD_CLEAR = U16(0x2),
} as2xxxx_ipc_cfg_irq_subcmd_t;

/** @brief AS2XXXX_IPC_SUBCMD_CFG_PARAM_DIRECT subcommands (3rd-level). */
typedef enum {
    AS2XXXX_IPC_CFG_PARAM_DIRECT_SUBCMD_CU_AN = U16(0x2),
    AS2XXXX_IPC_CFG_PARAM_DIRECT_SUBCMD_SDS_PCS = U16(0x3),
    AS2XXXX_IPC_CFG_PARAM_DIRECT_SUBCMD_DPC_RA = U16(0x6),
    AS2XXXX_IPC_CFG_PARAM_DIRECT_SUBCMD_TEMP_MONITOR = U16(0xB),
    /*  Datapath controller FSM start/stop. AS22xxx only. */
    AS2XXXX_IPC_CFG_PARAM_DIRECT_SUBCMD_DPC_FSM_START = U16(0x14),
} as2xxxx_ipc_cfg_param_direct_subcmd_t;

/** @brief AS2XXXX_IPC_CFG_PARAM_DIRECT_SUBCMD_DPC_FSM_START values (4th-level). */
typedef enum {
    AS2XXXX_CFG_DPC_FSM_DISABLE = U16(0x0),
    AS2XXXX_CFG_DPC_FSM_ENABLE = U16(0x1),
} as2xxxx_cfg_dpc_fsm_t;

/** @brief AS2XXXX_IPC_DBGCMD_SUBCMD_NGPHY subcommands (3rd-level).
 *
 * BEWARE: the two families reuse the same opcode numbers for different things.
 * Subcommand 0x5 clears the counter block on AS21xxx but reads the CFR counter
 * on AS22xxx, so these must never be used without checking the family first. */
typedef enum {
    // AS21xxx only
    AS2XXXX_IPC_DBGCMD_NGPHY_SUBCMD_AS21XXX_GET_CNT = U16(0x4),
    AS2XXXX_IPC_DBGCMD_NGPHY_SUBCMD_AS21XXX_CLEAR_CNT = U16(0x5),

    // AS22xxx only
    AS2XXXX_IPC_DBGCMD_NGPHY_SUBCMD_AS22XXX_CFR_CNT = U16(0x5),
    AS2XXXX_IPC_DBGCMD_NGPHY_SUBCMD_AS22XXX_CFR_CNT_CLR = U16(0x6),
    AS2XXXX_IPC_DBGCMD_NGPHY_SUBCMD_AS22XXX_LDPC_ERR = U16(0x10),
} as2xxxx_ipc_dbgcmd_ngphy_subcmd_t;

/** @brief AS2XXXX_IPC_DBGCMD_SUBCMD_DPC subcommands (3rd-level).
 *
 * BEWARE: Always dispatch on the family as commands are family dependent. */
typedef enum {
    /* Rate adaptation. Set happens to be opcode 0x2 on both families, but only
     * the opcode is shared - see as2xxxx_ra_cfg_t for the payload difference.
     * Get differs in opcode as well: 0x7 is CFG_DPC_EEE_GET_CFG on AS22xxx, so
     * using the AS21xxx opcode there reads the EEE configuration instead. */
    AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_RA_SET_CFG = U16(0x2),
    AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS21XXX_RA_GET_CFG = U16(0x7),
    AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS22XXX_RA_GET_CFG = U16(0x3),

    /* Host SerDes configuration - the word holding pcs_sel (host mode) and
     * sds_spd. AS21xxx keeps this separate from the rate adaptation command; on
     * AS22xxx they are the same opcode, so both names below resolve to 0x2/0x3. */
    AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS21XXX_HOST_CFG_SET = U16(0x0),
    AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS21XXX_HOST_CFG_GET = U16(0x1),
    AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS22XXX_HOST_CFG_SET = U16(0x2),
    AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS22XXX_HOST_CFG_GET = U16(0x3),

    /* AS22xxx datapath mode. Shares 0x0/0x1 with the AS21xxx host config above,
     * which is why sending the AS21xxx opcode to an AS22xxx writes a datapath
     * mode instead of a SerDes configuration. */
    AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS22XXX_DP_MODE_SET = U16(0x0),
    AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS22XXX_DP_MODE_GET = U16(0x1),

    // AS21xxx only
    AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS21XXX_GET_CNT = U16(0xD),
    AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS21XXX_CLEAR_CNT = U16(0xE),

    /* AS22xxx only: restart the datapath, one byte of payload. Unused: it does not clear the
     * remote loopback with either payload value, and nothing else needs it. Kept as a record of
     * what was tried - see the README. */
    AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS22XXX_DP_STOCK_RESTART = U16(0x11),

    // AS22xxx only: the whole packet-check counter block
    AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS22XXX_PKT_DUMP = U16(0x13),
    AS2XXXX_IPC_DBGCMD_DPC_SUBCMD_AS22XXX_PKT_CLR = U16(0x14),
} as2xxxx_ipc_dbgcmd_dpc_subcmd_t;

/** @brief AS2XXXX_IPC_DBGCMD_SUBCMD_SDS subcommands (3rd-level).
 *
 * BEWARE: 0x1B sets the Tx FIR on AS21xxx but reads the SerDes link-down
 * counter on AS22xxx. */
typedef enum {
    AS2XXXX_IPC_DBGCMD_SDS_SUBCMD_EYE_SCAN = U16(0x1),
    /** @brief Eye scan geometry: takes sds_id, returns 4 x uint32
     *         (groups, columns per group, y resolution, samples per point). */
    AS2XXXX_IPC_DBGCMD_SDS_SUBCMD_EYE_SCAN_CFG_GET = U16(0x2),

    // AS21xxx only
    AS2XXXX_IPC_DBGCMD_SDS_SUBCMD_AS21XXX_RESET = U16(0xD),
    AS2XXXX_IPC_DBGCMD_SDS_SUBCMD_AS21XXX_TXFIR_SET = U16(0x1B),

    // AS22xxx only
    /** @brief Host SerDes state. */
    /* Transmit FIR taps, three bytes: pre, main, post. Note the AS21xxx command takes a SerDes id
     * first and this one does not. */
    AS2XXXX_IPC_DBGCMD_SDS_SUBCMD_AS22XXX_TXFIR_SET = U16(0x10),
    AS2XXXX_IPC_DBGCMD_SDS_SUBCMD_AS22XXX_LINK_STAT = U16(0x11),
    AS2XXXX_IPC_DBGCMD_SDS_SUBCMD_AS22XXX_RESET = U16(0x1A),
    AS2XXXX_IPC_DBGCMD_SDS_SUBCMD_AS22XXX_LINK_DOWN_CNT = U16(0x1B),
    AS2XXXX_IPC_DBGCMD_SDS_SUBCMD_AS22XXX_LINK_DOWN_CNT_CLR = U16(0x1C),
} as2xxxx_ipc_dbgcmd_sds_subcmd_t;

/** @brief AS2XXXX_IPC_DBGCMD_SUBCMD_CU_AN subcommands (3rd-level).
 *  These are the same on both families. */
typedef enum {
    AS2XXXX_IPC_DBGCMD_CU_AN_SUBCMD_ENABLE = U16(0x0),
    AS2XXXX_IPC_DBGCMD_CU_AN_SUBCMD_GET_CNT = U16(0xA),
    AS2XXXX_IPC_DBGCMD_CU_AN_SUBCMD_CLEAR_CNT = U16(0xB),
} as2xxxx_ipc_dbgcmd_cu_an_subcmd_t;

/** @brief AS2XXXX_IPC_CFG_PARAM_DIRECT_SUBCMD_CU_AN subcommands (4th-level). */
typedef enum {
    AS2XXXX_CFG_CU_AN_SUBCMD_ENABLE = U16(0x1),
    AS2XXXX_CFG_CU_AN_SUBCMD_RESTART = U16(0xA),
    AS2XXXX_CFG_CU_AN_SUBCMD_TOP_SPEED = U16(0xC),
} as2xxxx_cfg_cu_an_subcmd_t;

/** @brief AS2XXXX_IPC_CFG_PARAM_DIRECT_SUBCMD_TEMP_MONITOR subcommands (4th-level). */
typedef enum {
    AS2XXXX_CFG_TEMP_MONITOR_SUBCMD_START = U16(0x1),
    AS2XXXX_CFG_TEMP_MONITOR_SUBCMD_STOP = U16(0x2),
    AS2XXXX_CFG_TEMP_MONITOR_SUBCMD_SET_CFG = U16(0x3),
    AS2XXXX_CFG_TEMP_MONITOR_SUBCMD_GET_SAMPLE = U16(0x4),
    AS2XXXX_CFG_TEMP_MONITOR_SUBCMD_SET_THRESHOLD = U16(0x5),
} as2xxxx_cfg_temp_monitor_subcmd_t;

/** @brief Temperature sample response words. Signed Q16.16, LSW first. */
typedef enum {
    AS2XXXX_TEMP_SAMPLE_WORD_ECHO = 0,
    AS2XXXX_TEMP_SAMPLE_WORD_LSB = 1,
    AS2XXXX_TEMP_SAMPLE_WORD_MSB = 2,
    AS2XXXX_TEMP_SAMPLE_WORD_COUNT = 3, /**< Keep last */
} as2xxxx_temp_sample_word_t;

/** @brief Fractional bits of the Q16.16 temperature sample */
#define AS2XXXX_TEMP_Q16_FRAC_BITS 16u
/** @brief One whole degree in Q16.16. Signed to keep the division signed. */
#define AS2XXXX_TEMP_Q16_SCALE (INT32_C(1) << AS2XXXX_TEMP_Q16_FRAC_BITS)
/** @brief Sub-degree steps the temperature is reported in. Must match "%02u". */
#define AS2XXXX_TEMP_FRAC_PER_DEGREE INT32_C(100)

// ------------------------------------------------------------------------------------------------
// Speed and encoding configurations
// ------------------------------------------------------------------------------------------------

/** @brief Line speed. */
typedef enum {
    AS2XXXX_MDI_SPEED_T100M = U16(0x4),
    AS2XXXX_MDI_SPEED_T1G = U16(0x8),
    AS2XXXX_MDI_SPEED_T2500M = U16(0x10),
    AS2XXXX_MDI_SPEED_T5G = U16(0x20),
    AS2XXXX_MDI_SPEED_T10G = U16(0x40),
    AS2XXXX_MDI_SPEED_INVALID = U16(0xFF)
} as2xxxx_mdi_speed_t;

/** @brief SerDes speed. */
typedef enum {
    AS2XXXX_SERDES_SPEED_1G = U16(0x0),
    AS2XXXX_SERDES_SPEED_2500M = U16(0x1),
    AS2XXXX_SERDES_SPEED_5G = U16(0x2),
    AS2XXXX_SERDES_SPEED_10G = U16(0x3),
    AS2XXXX_SERDES_SPEED_INVALID = U16(0xFF)
} as2xxxx_serdes_speed_t;

// ------------------------------------------------------------------------------------------------
// Host SerDes configuration word
// ------------------------------------------------------------------------------------------------

// The low byte is in-band auto-negotiation enable, the high byte is the host lane rate. The low
// field is named pcs_sel and documented as "1 : 64/66B, 0 : 8B/10B", which is how AS21xxx uses it,
// but on AS22xxx it is an in-band enable and nothing else - see the README. In-band off is XFI; on
// with rate 3 is USXGMII, on with rate 0 is SGMII, on with rate 1 or 2 is rate adaptation. Both
// fields share the word, so changing one means reading the other back and preserving it.

/** @brief Host SerDes config word: in-band auto-negotiation enable. */
#define AS2XXXX_REG_MASK_HOST_CFG_INBAND GENMASK16(7, 0)
/** @brief Host SerDes config word: host lane rate. */
#define AS2XXXX_REG_MASK_HOST_CFG_SPEED GENMASK16(15, 8)

/** @brief Words of the host SerDes config. AS21xxx adds an operating mode word. */
#define AS2XXXX_HOST_CFG_WORDS_AS21XXX 2u
#define AS2XXXX_HOST_CFG_WORDS_AS22XXX 1u

/** @brief Layout of the AS22xxx host SerDes link status word. Reports the state
 *  actually achieved, as opposed to the configuration above. The speed field carries
 *  an \ref as2xxxx_serdes_speed_t. */
#define AS2XXXX_REG_MASK_SDS_LINK_UP    GENMASK16(7, 0)
#define AS2XXXX_REG_MASK_SDS_LINK_SPEED GENMASK16(15, 8)

/** @brief Value the link-up field holds when the host SerDes link is up. */
#define AS2XXXX_SDS_LINK_UP_VAL 1u

/** @brief AS22xxx datapath mode, carried by its own command (0x0 set / 0x1 get). */
typedef enum {
    AS2XXXX_DP_MODE_DEFAULT = U16(0),
    AS2XXXX_DP_MODE_RMT_LOOPBACK = U16(3),
} as2xxxx_dp_mode_t;

// ------------------------------------------------------------------------------------------------
// SerDes mode
// ------------------------------------------------------------------------------------------------

/** @brief SerDes operation mode, AS21xxx only.
 *
 * A third byte of the AS21xxx host configuration command. AS22xxx dropped it and
 * moved loopback to the datapath mode command - see as2xxxx_dp_mode_t. */
typedef enum {
    AS2XXXX_SERDES_OPMODE_NORMAL = U16(0),
    AS2XXXX_SERDES_OPMODE_LOOPBACK = U16(3),
} as2xxxx_serdes_opmode_t;

// ------------------------------------------------------------------------------------------------
// Counters
// ------------------------------------------------------------------------------------------------

/** @brief Number of 32-bit counters returned by each sub-device query.
 *
 * AS21xxx splits its counters over three queries (NGPHY + CU_AN + DPC). AS22xxx
 * dropped that split: one DPC packet-check dump returns the whole block, and the
 * remaining values are single-counter queries. */
typedef enum {
    // AS21xxx
    AS2XXXX_COUNTER_NUMBER_NGPHY = 5,
    AS2XXXX_COUNTER_NUMBER_DPC = 3,

    // Both families
    AS2XXXX_COUNTER_NUMBER_CU_AN = 1,

    // AS22xxx: 4 blocks x (frm_cnt, crc_err, err_sym as 64-bit pairs + runt +
    // oversize) = 32, plus BER and errored blocks
    AS2XXXX_COUNTER_NUMBER_AS22XXX_PKT_DUMP = 34,
} as2xxxx_counter_number_t;

/** @brief Buffer size in words for a single AS22xxx counter query.
 *  Sized for the widest one (32-bit); the CFR counter is only 16-bit. */
#define AS2XXXX_COUNTER_AS22XXX_SINGLE_BUFFER_SIZE_WORDS 2

/** @brief Words occupied by a 16-bit counter */
#define AS2XXXX_COUNTER16_SIZE_WORDS 1u

/** @brief How many bytes the counters have */
#define AS2XXXX_COUNTER_SIZE_BYTES sizeof(uint32_t)

/** @brief Size of one counter in words (uint16_t) */
#define AS2XXXX_COUNTER_SIZE_WORDS (AS2XXXX_COUNTER_SIZE_BYTES / sizeof(uint16_t))

/** @brief Number of bits in one counter slot (uint32_t) */
#define AS2XXXX_COUNTER_SIZE_BITS (AS2XXXX_COUNTER_SIZE_BYTES * 8u)

/** @brief Slots per counter. A slot is 32-bit and is the unit of counter indices. */
#define AS2XXXX_COUNTER_SLOTS_U32 1u
#define AS2XXXX_COUNTER_SLOTS_U64 2u

/** @brief Counter slot offsets within one AS22xxx packet-check block */
typedef enum {
    AS2XXXX_PKT_BLOCK_SLOT_FRM_CNT = 0,
    AS2XXXX_PKT_BLOCK_SLOT_CRC_ERR = AS2XXXX_PKT_BLOCK_SLOT_FRM_CNT + AS2XXXX_COUNTER_SLOTS_U64,
    AS2XXXX_PKT_BLOCK_SLOT_ERR_SYM = AS2XXXX_PKT_BLOCK_SLOT_CRC_ERR + AS2XXXX_COUNTER_SLOTS_U64,
    AS2XXXX_PKT_BLOCK_SLOT_RUNT = AS2XXXX_PKT_BLOCK_SLOT_ERR_SYM + AS2XXXX_COUNTER_SLOTS_U64,
    AS2XXXX_PKT_BLOCK_SLOT_OVERSIZE = AS2XXXX_PKT_BLOCK_SLOT_RUNT + AS2XXXX_COUNTER_SLOTS_U32,

    /** Keep last */
    AS2XXXX_PKT_BLOCK_SLOT_COUNT = AS2XXXX_PKT_BLOCK_SLOT_OVERSIZE + AS2XXXX_COUNTER_SLOTS_U32,
} as2xxxx_pkt_block_slot_t;

/** @brief Number of 32-bit counters in one AS22xxx packet-check block */
#define AS2XXXX_COUNTER_AS22XXX_PKT_BLOCK_SIZE AS2XXXX_PKT_BLOCK_SLOT_COUNT
/** @brief Number of packet-check blocks in the AS22xxx dump */
#define AS2XXXX_COUNTER_AS22XXX_PKT_BLOCKS 4

/** @brief Calculate buffer size in words for N counters */
#define AS2XXXX_COUNTER_BUFFER_SIZE_WORDS(count) ((count) * AS2XXXX_COUNTER_SIZE_WORDS)

/** @brief Buffer sizes in words (uint16_t) for each sub-device counter type */
#define AS2XXXX_COUNTER_NGPHY_BUFFER_SIZE_WORDS                                                    \
    AS2XXXX_COUNTER_BUFFER_SIZE_WORDS(AS2XXXX_COUNTER_NUMBER_NGPHY)
#define AS2XXXX_COUNTER_DPC_BUFFER_SIZE_WORDS                                                      \
    AS2XXXX_COUNTER_BUFFER_SIZE_WORDS(AS2XXXX_COUNTER_NUMBER_DPC)
#define AS2XXXX_COUNTER_CU_AN_BUFFER_SIZE_WORDS                                                    \
    AS2XXXX_COUNTER_BUFFER_SIZE_WORDS(AS2XXXX_COUNTER_NUMBER_CU_AN)
#define AS2XXXX_COUNTER_AS22XXX_PKT_DUMP_BUFFER_SIZE_WORDS                                         \
    AS2XXXX_COUNTER_BUFFER_SIZE_WORDS(AS2XXXX_COUNTER_NUMBER_AS22XXX_PKT_DUMP)

// ------------------------------------------------------------------------------------------------
// IRQ configuration
// ------------------------------------------------------------------------------------------------

// Masks
#define AS2XXXX_REG_MASK_IRQ_STATUS_WAKE_ON_LAN BIT16(2)
#define AS2XXXX_REG_MASK_IRQ_STATUS_LINK_CHANGE BIT16(3)

// ------------------------------------------------------------------------------------------------
// LED configuration
// ------------------------------------------------------------------------------------------------

/** @brief LED behavior.
 * Note:
 *  FE = Fast Ethernet (100BASE-TX).
 *  GE = Gigabit Ethernet (1000BASE-T).
 *  NG = 2.5G, 5G and 10G.
 */
typedef enum {
    AS2XXXX_LED_OFF = U16(0x0),                // Force LED off
    AS2XXXX_LED_ON_NG_BLINK_ACT = U16(0x1),    // LED on in NG state; blink on activity
    AS2XXXX_LED_ON_FE_GE_BLINK_ACT = U16(0x2), // LED on when link FE or GE; blink on activity
    AS2XXXX_LED_LINK_EST = U16(0x3),           // LED on when link is established (link UP)
    AS2XXXX_LED_TX_RX_ACT = U16(0x4),          // LED shows TX/RX activity
    AS2XXXX_LED_LINK_EST_BLINK_ACT = U16(0x5), // LED on when link up; blink on activity
    AS2XXXX_LED_ON_NG_BLINK_FE_GE = U16(0x6),  // LED on in NG state; blink when link is FE/GE
    AS2XXXX_LED_ON_FE_GE = U16(0x7),           // LED on for FE or GE link speeds
    AS2XXXX_LED_ON_NG = U16(0x8),              // LED on in NG state
    AS2XXXX_LED_ON_FD = U16(0x9),              // LED on when full-duplex is negotiated
    AS2XXXX_LED_ON_COLL = U16(0xA),            // LED indicates collisions
    AS2XXXX_LED_TX_ACT = U16(0xB),             // LED blinks for transmit activity only
    AS2XXXX_LED_RX_ACT = U16(0xC),             // LED blinks for receive activity only
    AS2XXXX_LED_ON_2500M = U16(0xD),           // LED on when link is 2.5 Gbps
    AS2XXXX_LED_ON_1000BT = U16(0xE),          // LED on when link is 1000BASE-T (1 Gbps)
    AS2XXXX_LED_ON_5G = U16(0xF),              // LED on when link is 5 Gbps
    AS2XXXX_LED_ON_100TX = U16(0x11),          // LED on when link is 100BASE-TX (100 Mbps)
    AS2XXXX_LED_ON_10BT = U16(0x12),           // LED on when link is 10BASE-T (10 Mbps)
    AS2XXXX_LED_ON_10G = U16(0x13),            // LED on when link is 10 Gbps
    AS2XXXX_LED_ON_FD_BLINK_COLL = U16(0x14),  // LED on for full-duplex; blink on collisions
    AS2XXXX_LED_ON = U16(0x15)                 // Force LED on
} as2xxxx_led_behavior_t;

/** @brief LED polarity. */
typedef enum {
    AS2XXXX_LED_0_4_POLARITY_NO_SWAP = U16(0x0), // Do not swap polarity of LEDs 0-4
    AS2XXXX_LED_2_POLARITY_SWAP = U16(0x4),      // Swap polarity of LED 2
    AS2XXXX_LED_0_4_POLARITY_SWAP = U16(0xF)     // Swap polarity of LEDs 0-4
} as2xxxx_led_polarity_t;

/** @brief LED blink rate. */
typedef enum {
    AS2XXXX_LED_BLINK_RATE_15_625HZ = U16(0x1),  // 15.625Hz
    AS2XXXX_LED_BLINK_RATE_7_8125HZ = U16(0x2),  // 7.8125Hz
    AS2XXXX_LED_BLINK_RATE_3_9063HZ = U16(0x3),  // 3.9063Hz
    AS2XXXX_LED_BLINK_RATE_1_9531HZ = U16(0x4),  // 1.9531Hz
    AS2XXXX_LED_BLINK_RATE_0_97656HZ = U16(0x5), // 0.97656Hz
    AS2XXXX_LED_BLINK_RATE_0_48828HZ = U16(0x6), // 0.48828Hz
} as2xxxx_led_blink_rate_t;

#endif /* AS2XXXX_REGISTERS_H_ */
