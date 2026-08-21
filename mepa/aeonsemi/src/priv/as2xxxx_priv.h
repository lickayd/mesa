// Copyright (c) 2004-2026 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#ifndef AS2XXXX_PRIV_H_
#define AS2XXXX_PRIV_H_

#include "microchip/ethernet/common.h"
#include "microchip/ethernet/phy/api/types.h"
#include <lm_utils.h>
#include <stdint.h>
#include "ipc.h"

// ------------------------------------------------------------------------------------------------
// Constants
// ------------------------------------------------------------------------------------------------

/** @brief AeonSemi vendor base of the clause-45 device ID (bits 31:8) */
#define AS2XXXX_PHY_ID_VENDOR 0x75009400
/** @brief Mask selecting the vendor base (ignores the model and revision nibbles) */
#define AS2XXXX_PHY_ID_MASK 0xFFFFFF00
/** @brief Firmware version string length */
#define AS2XXXX_FW_VER_STRING_LEN 8

// ------------------------------------------------------------------------------------------------
// Typedefs
// ------------------------------------------------------------------------------------------------

/** @brief Chip family.
 *
 * Also known as SEI1 and SEI2.
 *
 * The families share the register map, the IPC framing and the firmware boot
 * sequence, but a number of DBGCMD sub-opcodes differ, so most operations need
 * to know which one they are talking to. */
typedef enum {
    AS2XXXX_FAMILY_UNKNOWN = 0,
    AS2XXXX_FAMILY_AS21XXX, /**< SEI1 */
    AS2XXXX_FAMILY_AS22XXX, /**< SEI2 */
} as2xxxx_family_t;

/** @brief Optional firmware load progress reporting.
 *
 * Which image is loaded is decided from the chip family internally, so the caller
 * only supplies how it wants progress reported. Set progress_func to NULL for no
 * reporting. */
typedef struct {
    size_t step_size_bytes; /**< Report progress every N bytes (0 = report every byte) */
    void (*progress_func)(size_t done_bytes, size_t total_bytes);
} as2xxxx_fw_progress_t;

/** @brief Whether in-band auto-negotiation runs on the host lane. */
typedef enum {
    AS2XXXX_HOST_INBAND_DIS = 0,
    AS2XXXX_HOST_INBAND_ENA = 1,
    AS2XXXX_HOST_INBAND_UNKNOWN = 0xFF,
} as2xxxx_host_inband_t;

/** @brief Transmit FIR filter configuration. */
typedef struct {
    uint8_t pre;
    uint8_t main;
    uint8_t post;
} as2xxxx_tx_fir_conf_t;

/** @brief Temperature monitor config. */
typedef struct {
    enum {
        AS2XXXX_TEMP_MONITOR_ONE_SHOT_SAMPLE = 0,
        AS2XXXX_TEMP_MONITOR_CONTINUOUS_SAMPLING = 1,
    } config;
    mepa_bool_t enable;
} as2xxxx_temp_monitor_conf_t;

/** @brief IRQ events. */
typedef enum {
    // Intentionally starting from 3
    AS2XXXX_IRQ_EVENT_WAKE_ON_LAN = 3,
    AS2XXXX_IRQ_EVENT_LINK_STATUS_CHANGE = 4,
} as2xxxx_irq_event_t;

/** @brief IRQ config. */
typedef struct {
    as2xxxx_irq_event_t event;
    mepa_bool_t         enable;
    uint16_t            number_of_pulses;

    enum {
        AS2XXXX_IRQ_SIGNAL_TYPE_PULSE = 0,     /**< generate a pulse on the interrupt pin */
        AS2XXXX_IRQ_SIGNAL_TYPE_LOW_LEVEL = 1, /**< Pull interrupt pin low indefinetely */
    } signal_type;

} as2xxxx_irq_conf_t;

/** @brief AS21xxx counters: three separate sub-device queries. */
typedef struct {
    struct {
        uint32_t err_blk;
        uint32_t ber;
        uint32_t hi_ber;
        uint32_t cfr;
        uint32_t cfr_err;
    } ngphy;

    struct {
        uint32_t cu_an;
    } cu_an;

    struct {
        uint32_t ldpc_err;
        uint32_t eth_crc_err;
        uint32_t sds_crc_err;
    } dpc;

} as2xxxx_counters_as21xxx_t;

/** @brief One AS22xxx packet-check counter block. */
typedef struct {
    uint64_t frm_cnt;
    uint64_t crc_err;
    uint64_t err_sym;
    uint32_t runt;
    uint32_t oversize;
} as2xxxx_pkt_block_t;

/** @brief AS22xxx counters.
 *
 * The four packet-check blocks come from a single DPC dump and describe the
 * datapath at its four measurement points. The remaining values are individual
 * queries spread over NGPHY / CU_AN / SDS. */
typedef struct {
    as2xxxx_pkt_block_t from_sds;
    as2xxxx_pkt_block_t to_eth;
    as2xxxx_pkt_block_t from_eth;
    as2xxxx_pkt_block_t to_sds;

    uint32_t sds_rx_ber;     /**< SerDes Rx bit error rate counter */
    uint32_t sds_rx_err_blk; /**< SerDes Rx errored blocks */

    uint32_t ldpc_err;          /**< NGPHY LDPC decode errors */
    uint32_t cfr_cnt;           /**< NGPHY codeword frame count */
    uint32_t cu_an;             /**< Copper AN restart count */
    uint32_t sds_link_down_cnt; /**< SerDes link-down events */

} as2xxxx_counters_as22xxx_t;

/** @brief Counters. Which member is valid is decided by @c family. */
typedef struct {
    as2xxxx_family_t family;
    union {
        as2xxxx_counters_as21xxx_t as21xxx;
        as2xxxx_counters_as22xxx_t as22xxx;
    } u;

} as2xxxx_counters_t;

/** @brief Per-instance state owned by this layer: chip identity and firmware. */
typedef struct {
    /** @brief Owning device, set by \ref as2xxxx_priv_probe. Only ever used inside
     *  this layer, to reach the MEPA callouts and the device lock. */
    mepa_device_t *dev;

    uint32_t         dev_id;
    as2xxxx_family_t family;
    char             fw_version[AS2XXXX_FW_VER_STRING_LEN];
    mepa_bool_t      is_fw_ver_cached;
    ipc_state_t      ipc;
    mepa_bool_t      is_host_loopback_ena;

    /** @brief Line loopback state, AS22xxx. There is no way out of that loopback, so whether it
     *  was ever entered decides whether a request to disable can be honoured at all. */
    mepa_bool_t is_line_loopback_ena;
} as2xxxx_priv_data_t;

// ------------------------------------------------------------------------------------------------
// APIs
// ------------------------------------------------------------------------------------------------

/**
 * @brief Read the PHY ID.
 * @param[in] pd The device handle.
 * @param[out] devid The device ID to be populated.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 */
mepa_rc as2xxxx_priv_read_phyid(as2xxxx_priv_data_t *const pd, uint32_t *const devid);

/**
 * @brief Resolve the chip family from a device ID.
 * @param[in] phy_id The device ID as read by \ref as2xxxx_priv_read_phyid.
 * @return The family, or AS2XXXX_FAMILY_UNKNOWN if the ID is not one of this vendor's
 *         or carries a model this driver does not know.
 */
as2xxxx_family_t as2xxxx_priv_family_from_phy_id(uint32_t phy_id);

/**
 * @brief Chip model nibble of a device ID.
 * @param[in] phy_id The device ID.
 * @return The model nibble. Only meaningful for a device ID of this vendor.
 */
uint8_t as2xxxx_priv_model_from_phy_id(uint32_t phy_id);

/**
 * @brief Chip revision nibble of a device ID.
 * @param[in] phy_id The device ID.
 * @return The revision nibble. Revisions do not affect the family.
 */
uint8_t as2xxxx_priv_revision_from_phy_id(uint32_t phy_id);

/**
 * @brief Human readable family name.
 * @param[in] family The family.
 * @return A static string, never NULL.
 */
const char *as2xxxx_priv_family_to_string(as2xxxx_family_t family);

/**
 * @brief Human readable host in-band auto-negotiation state.
 * @param[in] inband The state.
 * @return A static string, never NULL.
 */
const char *as2xxxx_priv_host_inband_to_string(as2xxxx_host_inband_t inband);

/**
 * @brief Device ID recorded during probe.
 * @param[in] pd The device handle.
 */
uint32_t as2xxxx_priv_dev_id_get(const as2xxxx_priv_data_t *const pd);

/**
 * @brief Chip family resolved during probe.
 * @param[in] pd The device handle.
 */
as2xxxx_family_t as2xxxx_priv_family_get(const as2xxxx_priv_data_t *const pd);

/**
 * @brief Firmware version string, read from the device on first use.
 * @param[in] pd The device handle.
 * @return A NUL terminated string owned by this layer, never NULL.
 * @note May talk to the device, so the caller must hold the device lock.
 */
const char *as2xxxx_priv_fw_version_get(as2xxxx_priv_data_t *const pd);

/**
 * @brief Detect the chip, bring the firmware up and record the chip identity.
 * @param[in] pd The device handle.
 * @param[in] progress Optional firmware load progress reporting, may be NULL.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 * @note The individual boot steps are deliberately not exposed: the order between
 *       detection, the warm/cold decision and the post-boot ID check is part of the
 *       bring-up contract and must not be reassembled by callers.
 */
mepa_rc as2xxxx_priv_probe(as2xxxx_priv_data_t *const         pd,
                           mepa_device_t *const               dev,
                           const as2xxxx_fw_progress_t *const progress);

/**
 * @brief Get a firmware version from the device.
 * @param[in] pd The device handle.
 * @param[out] fw_version The firmware version buffer to be populated.
 * @param[in] fw_ver_size The size of the firmware version buffer.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 */
mepa_rc as2xxxx_priv_get_fw_version(as2xxxx_priv_data_t *const pd,
                                    char *const                fw_version,
                                    const size_t               fw_ver_size);
/**
 * @brief Resets the PHY.
 * @param[in] pd The device handle.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 */
mepa_rc as2xxxx_priv_reset(as2xxxx_priv_data_t *const pd);

/**
 * @brief Enables the PHY.
 * @param[in] pd The device handle.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 */
mepa_rc as2xxxx_priv_phy_enable(as2xxxx_priv_data_t *const pd);

/**
 * @brief Disables the PHY.
 * @param[in] pd The device handle.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 */
mepa_rc as2xxxx_priv_phy_disable(as2xxxx_priv_data_t *const pd);

/**
 * @brief Sets the host side in-band setting and lane rate together.
 *
 * The two are one setting as far as the host protocol is concerned, and are applied in a single
 * write where the family allows it, so the lane never passes through a combination the caller did
 * not ask for. The write is unconditional: the firmware wants it at every link-up, and skipping it
 * when the PHY already holds these values broke 100M on the bench.
 *
 * @param[in] pd The device handle.
 * @param[in] inband Whether in-band auto-negotiation runs on the host lane. Together with the lane
 *                   rate this is what selects XFI, SGMII, USXGMII or rate adaptation.
 * @param[in] speed The desired lane rate. The host SerDes has no rate below 1G.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise. MEPA_RC_ERR_PARM if the lane cannot
 *                 run at the given speed.
 */
mepa_rc as2xxxx_priv_set_host_cfg(as2xxxx_priv_data_t *const  pd,
                                  const as2xxxx_host_inband_t inband,
                                  const mesa_port_speed_t     speed);

/**
 * @brief Sets the speed and the pause advertisement of the line side, either auto-negotiated or
 *        forced.
 *
 * The firmware takes a single top speed rather than a per-speed advertisement, so the caller
 * reduces whatever set of rates it wants to the highest of them before calling here. Flow control
 * belongs in the same call because it shares the base page with the speed advertisement and so has
 * to be written in a fixed order relative to the auto-negotiation restart.
 *
 * @param[in] pd The device handle.
 * @param[in] speed The top auto-negotiation speed, or the speed to force. 10M is not supported by
 *                  the PHY. With aneg off only 100M can be asked for, since no faster BASE-T rate
 *                  can be established without auto-negotiation.
 * @param[in] is_full_duplex Must be true. The PHY runs full duplex only.
 * @param[in] aneg_enable Whether auto-negotiation is left running. Off means the speed above is
 *                        forced onto the line instead of negotiated.
 * @param[in] flow_control Whether symmetric pause is advertised. Applied before the speed, because
 *                         the base page has to carry it by the time auto-negotiation restarts.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise. MEPA_RC_ERR_PARM if half duplex or
 *         an unsupported speed is asked for, rather than silently applying something else.
 */
mepa_rc as2xxxx_priv_set_line_config(as2xxxx_priv_data_t *const pd,
                                     const mesa_port_speed_t    speed,
                                     const mepa_bool_t          is_full_duplex,
                                     const mepa_bool_t          aneg_enable,
                                     const mepa_bool_t          flow_control);

/**
 * @brief Gets the current operating speed and duplex for the line side.
 * @param[in] pd The device handle.
 * @param[out] speed Will be populated with the current line speed.
 * @param[out] is_full_duplex Will be set to true if full-duplex, false if half-duplex.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise. An error means the register could
 *         not be read. A line that has not settled on a speed is reported as success with
 *         MESA_SPEED_UNDEFINED, so a caller that needs a speed has to check for it.
 * @note Host side is always full-duplex at 1G/2.5G/10G speeds.
 */
mepa_rc as2xxxx_priv_get_speed_line(as2xxxx_priv_data_t *const pd,
                                    mesa_port_speed_t *const   speed,
                                    mepa_bool_t *const         is_full_duplex);

/**
 * @brief Checks if auto-negotiation has completed on the line side.
 * @param[in] pd The device handle.
 * @param[out] is_aneg_done Will be set to true if auto-negotiation is complete, false otherwise.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 */
mepa_rc as2xxxx_priv_get_aneg_done_flag_line(as2xxxx_priv_data_t *const pd,
                                             mepa_bool_t *const         is_aneg_done);

/**
 * @brief Gets the pause direction auto-negotiation settled on for the line side.
 * @param[in] pd The device handle.
 * @param[out] generate_pause Whether this end may send pause frames.
 * @param[out] obey_pause Whether this end has to honour received pause frames.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 */
mepa_rc as2xxxx_priv_get_flow_control_line(as2xxxx_priv_data_t *const pd,
                                           mepa_bool_t *const         generate_pause,
                                           mepa_bool_t *const         obey_pause);

/**
 * @brief Checks if the host SerDes link is up.

 * @param[in] pd The device handle.
 * @param[out] is_link_up Will be set to true if the host SerDes link is up.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 */
mepa_rc as2xxxx_priv_get_link_status_host(as2xxxx_priv_data_t *const pd,
                                          mepa_bool_t *const         is_link_up);

/**
 * @brief Host SerDes state as the SerDes itself reports it.
 *
 * Unlike \ref as2xxxx_priv_get_link_status_host, which reads a status bit, this asks the SerDes
 * what it actually achieved. Use it for reporting only: feeding an achieved speed into the
 * host/line speed matching would let a lane that is still settling trigger another retune.
 *
 * @param[in] pd The device handle.
 * @param[out] is_link_up Will be set to true if the host SerDes reports its link up.
 * @param[out] speed The speed the SerDes settled on, MESA_SPEED_UNDEFINED while it has not.
 * @return mepa_rc MEPA_RC_OK on success, MEPA_RC_NOT_IMPLEMENTED on AS21xxx, which has no such
 *         query, error code otherwise.
 */
mepa_rc as2xxxx_priv_get_host_sds_state(as2xxxx_priv_data_t *const pd,
                                        mepa_bool_t *const         is_link_up,
                                        mesa_port_speed_t *const   speed);

/**
 * @brief Checks if the link is up on the line side.
 * @param[in] pd The device handle.
 * @param[out] is_link_up Will be set to true if line link is up, false otherwise.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 */
mepa_rc as2xxxx_priv_get_link_status_line(as2xxxx_priv_data_t *const pd,
                                          mepa_bool_t *const         is_link_up);

/**
 * @brief Sets loopback mode on the host side.
 *
 * Enabling requires the host lane in XFI, and the loopback only carries traffic once the line link
 * is up. The lane mode is refused if wrong, the line link only warned about, since it comes up
 * on its own.
 *
 * @param[in] pd The device handle.
 * @param[in] set true to enable loopback. Disabling is not possible on this PHY.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise. MEPA_RC_NOT_IMPLEMENTED for a
 *         disable of a loopback that was actually enabled: the PHY offers no way out of it and
 *         needs a hard reset. A disable is accepted when the loopback was never enabled, so a
 *         caller that simply wants no loopback is not refused.
 */
mepa_rc as2xxxx_priv_set_loopback_mode_host(as2xxxx_priv_data_t *const pd, mepa_bool_t set);

/**
 * @brief Sets loopback mode on the line side.
 * @param[in] pd The device handle.
 * @param[in] set true to enable loopback, false to disable. Disabling works on AS21xxx only.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise. On AS22xxx, MEPA_RC_NOT_IMPLEMENTED
 *         for a disable of a loopback that was actually enabled - same limitation, and same
 *         accept-if-never-enabled behaviour, as the host loopback above.
 */
mepa_rc as2xxxx_priv_set_loopback_mode_line(as2xxxx_priv_data_t *const pd, mepa_bool_t set);

/**
 * @brief Checks if loopback mode is enabled on the host side.
 * @param[in] pd The device handle.
 * @param[out] is_loopback Will be set to true if loopback is enabled, false otherwise.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 * @note Reports what this driver applied rather than reading the PHY, which has no working readback
 *       for this loopback. A loopback enabled before this driver probed reads as disabled.
 */
mepa_rc as2xxxx_priv_is_loopback_mode_host(as2xxxx_priv_data_t *const pd,
                                           mepa_bool_t *const         is_loopback);

/**
 * @brief Checks if loopback mode is enabled on the line side.
 * @param[in] pd The device handle.
 * @param[out] is_loopback Will be set to true if loopback is enabled, false otherwise.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 */
mepa_rc as2xxxx_priv_is_loopback_mode_line(as2xxxx_priv_data_t *const pd,
                                           mepa_bool_t *const         is_loopback);

/**
 * @brief Sets the Tx FIR configuration on the PHY.
 * @param[in] pd The device handle.
 * @param[in] cfg The configuration to be set
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 */
mepa_rc as2xxxx_priv_set_tx_fir(as2xxxx_priv_data_t *const         pd,
                                const as2xxxx_tx_fir_conf_t *const cfg);

/**
 * @brief Gets whether in-band auto-negotiation is running on the host lane.
 * @param[in] pd The device handle.
 * @param[out] inband Will be filled with the in-band state.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 */
mepa_rc as2xxxx_priv_get_host_inband(as2xxxx_priv_data_t *const   pd,
                                     as2xxxx_host_inband_t *const inband);

/**
 * @brief Gets the host lane rate the PHY is configured for.
 * @param[in] pd The device handle.
 * @param[out] speed The configured lane rate, MESA_SPEED_UNDEFINED if the PHY reports a code that
 *                   is not one of the four the lane can run.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 */
mepa_rc as2xxxx_priv_get_host_lane_rate(as2xxxx_priv_data_t *const pd,
                                        mesa_port_speed_t *const   speed);

/**
 * @brief Sets the temperature monitor configuration.
 * @param[in] pd The device handle.
 * @param[in] conf Configuration to be set.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 */
mepa_rc as2xxxx_priv_set_temp_monitor_conf(as2xxxx_priv_data_t *const               pd,
                                           const as2xxxx_temp_monitor_conf_t *const conf);

/**
 * @brief Gets the last temperature sample from the PHY's internal sensor.
 * @param[in]  pd The device handle.
 * @param[out] temp_celsius Whole degrees Celsius, truncated towards zero. May be
 *                          NULL if only the fraction is wanted.
 * @param[out] temp_celsius_frac Hundredths of a degree, 0..99, always positive.
 *                               May be NULL if only whole degrees are wanted.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 */
mepa_rc as2xxxx_priv_get_temp_monitor_last_sample(as2xxxx_priv_data_t *const pd,
                                                  int16_t *const             temp_celsius,
                                                  uint16_t *const            temp_celsius_frac);

/**
 * @brief Enable IRQ event.
 * @param[in] pd The device handle.
 * @param[in] conf Configuration
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 */
mepa_rc as2xxxx_priv_set_irq_event_conf(as2xxxx_priv_data_t *const      pd,
                                        const as2xxxx_irq_conf_t *const conf);

/**
 * @brief Get state of a given event. This function will clear the event upon returning.
 * @param[in] pd The device handle.
 * @param[in] event Event.
 * @param[out] is_active True if the given event is active.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 */
mepa_rc as2xxxx_priv_get_irq_event_state(as2xxxx_priv_data_t *const pd,
                                         as2xxxx_irq_event_t        event,
                                         mepa_bool_t *const         is_active);

/**
 * @brief Get counters from the PHY.
 * @param[in] pd The device handle.
 * @param[out] counters Counters struct to be populated
 * @param[in] clear If true, counters will be cleared.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 */
mepa_rc as2xxxx_priv_get_counters(as2xxxx_priv_data_t *const pd,
                                  as2xxxx_counters_t *const  counters,
                                  mepa_bool_t                clear);

/**
 * @brief Prints the eye diagram into the given string stream. BEWARE: this is a long process.
 * @param[in] pd The device handle.
 * @param[in] ss The string stream to print into.
 * @param[in] progress_func Called after every fetched group, NULL to disable. The string
 *                          stream is not flushed until the caller is done with it, so this
 *                          is the only way to show progress while the scan runs.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 */
mepa_rc as2xxxx_priv_print_eye(as2xxxx_priv_data_t *const pd,
                               lmu_ss_t *const            ss,
                               void (*progress_func)(size_t done_groups, size_t total_groups));

/**
 * @brief Reads a MDIO Clause-22 register.
 * @param[in] pd The device handle.
 * @param[in] reg_addr Register address (0-31).
 * @param[out] value Pointer to store the 16-bit register value.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 */
mepa_rc as2xxxx_priv_reg_rd(as2xxxx_priv_data_t *const pd,
                            const uint16_t             reg_addr,
                            uint16_t *const            value);

/**
 * @brief Reads a MDIO Clause-45 register.
 * @param[in] pd The device handle.
 * @param[in] mmd MDIO Managed Device (MMD) address (0-31).
 * @param[in] reg_addr Register address within the MMD.
 * @param[out] value Pointer to store the 16-bit register value.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 */
mepa_rc as2xxxx_priv_reg_rd_mmd(as2xxxx_priv_data_t *const pd,
                                const uint16_t             mmd,
                                const uint16_t             reg_addr,
                                uint16_t *const            value);

/**
 * @brief Writes a MDIO Clause-22 register.
 * @param[in] pd The device handle.
 * @param[in] reg_addr Register address (0-31).
 * @param[in] value The 16-bit value to write.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 */
mepa_rc as2xxxx_priv_reg_wr(as2xxxx_priv_data_t *const pd,
                            const uint16_t             reg_addr,
                            uint16_t const             value);

/**
 * @brief Writes a MDIO Clause-45 register.
 * @param[in] pd The device handle.
 * @param[in] mmd MDIO Managed Device (MMD) address (0-31).
 * @param[in] reg_addr Register address within the MMD.
 * @param[in] value The 16-bit value to write.
 * @return mepa_rc MEPA_RC_OK on success, error code otherwise.
 */
mepa_rc as2xxxx_priv_reg_wr_mmd(as2xxxx_priv_data_t *const pd,
                                const uint16_t             mmd,
                                const uint16_t             reg_addr,
                                uint16_t const             value);

#endif /* AS2XXXX_PRIV_H_ */
