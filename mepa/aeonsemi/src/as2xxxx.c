// Copyright (c) 2004-2026 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

#include "priv/as2xxxx_priv.h"
#include "priv/as2xxxx_bitfields.h"
#include "as2xxxx_utils.h"

#include "mepa_driver.h"
#include "mepa_utils.h"
#include "microchip/ethernet/common.h"
#include "microchip/ethernet/phy/api/types.h"

// ------------------------------------------------------------------------------------------------
// Logging
// ------------------------------------------------------------------------------------------------

/** @brief Extract the port number for logging */
#define LOG_PORT (((const as2xxxx_phy_data_t *)dev->data)->port_no)

/** @brief Logging helper macros */
#define LOG_D(fmt, ...) AS2XXXX_T_D(MEPA_TRACE_GRP_GEN, "port_no %u: " fmt, LOG_PORT, ##__VA_ARGS__)
#define LOG_I(fmt, ...) AS2XXXX_T_I(MEPA_TRACE_GRP_GEN, "port_no %u: " fmt, LOG_PORT, ##__VA_ARGS__)
#define LOG_W(fmt, ...) AS2XXXX_T_W(MEPA_TRACE_GRP_GEN, "port_no %u: " fmt, LOG_PORT, ##__VA_ARGS__)
#define LOG_E(fmt, ...) AS2XXXX_T_E(MEPA_TRACE_GRP_GEN, "port_no %u: " fmt, LOG_PORT, ##__VA_ARGS__)

/** @brief Evaluate EXPR. On failure log at error level and jump to the "error" label.
 * Requires the enclosing function to have a label named "error". RC_VAR is named explicitly
 * rather than assumed, because several functions here keep more than one return code. */
#define RC_GOTO(RC_VAR, EXPR, MSG, ...)                                                            \
    do {                                                                                           \
        (RC_VAR) = (EXPR);                                                                         \
        if ((RC_VAR) != MEPA_RC_OK) {                                                              \
            LOG_E(MSG " (rc=%d)", ##__VA_ARGS__, (int)(RC_VAR));                                   \
            goto error;                                                                            \
        }                                                                                          \
    } while (0)

/** @brief Evaluate EXPR. On failure log at warning level and carry on.
 * Assigns to a local named "rc", which the enclosing function must declare. */
#define RC_WARN(EXPR, MSG, ...)                                                                    \
    do {                                                                                           \
        rc = (EXPR);                                                                               \
        if (rc != MEPA_RC_OK) {                                                                    \
            LOG_W(MSG " (rc=%d)", ##__VA_ARGS__, (int)rc);                                         \
        }                                                                                          \
    } while (0)

// ------------------------------------------------------------------------------------------------
// Macros
// ------------------------------------------------------------------------------------------------

/** @brief Extract MMD device address from combined address value */
#define GET_MMD_DEVADR(val) ((uint16_t)FIELD_GET32(GENMASK32(20, 16), val))
/** @brief Extract MMD register address from combined address value */
#define GET_MMD_REGADR(val) ((uint16_t)FIELD_GET32(U32_LOW_WORD, val))
/** @brief Manufacturer string */
#define AEONSEMI_MANUFACTURER_STRING "AeonSemi"
/** @brief PHY model name */
#define AEONSEMI_PHY_MODEL_NAME "ChronoPHY"
/** @brief Loopbacks this PHY implements, as reported by MEPA_CAP_LOOPBACK */
#define AS2XXXX_LOOPBACK_CAP (MEPA_LOOPBACK_FAR_END | MEPA_LOOPBACK_NEAR_END)
/** @brief Number of pulses for the IRQ pulse trigger */
#define AS2XXXX_IRQ_NUM_OF_PULSES_DEFAULT 10
/** @brief Character width of the firmware load progress bar */
#define PROGRESS_BAR_WIDTH 40u
/** @brief Step size for reporting firmware image loading progress */
#define FW_LOAD_PROGRESS_STEP_BYTES (16 * 1024)
/** @brief Width of a label in debug info prints */
#define DEBUG_LABEL_WIDTH 30
/** @brief Width of a nested label in debug info prints */
#define DEBUG_SUBLABEL_WIDTH 28
/** @brief Label prefix format for debug info prints, takes the label string as argument */
#define DEBUG_LABEL "  %-" STRINGIFY(DEBUG_LABEL_WIDTH) "s: "
/** @brief Nested label prefix format for debug info prints */
#define DEBUG_SUBLABEL "    %-" STRINGIFY(DEBUG_SUBLABEL_WIDTH) "s: "
/** @brief Header row of the AS22xxx packet-check counter table */
#define PKT_BLOCK_HEAD "    %-16s%14s%14s%14s%10s%10s\n"
/** @brief One measurement point of the AS22xxx packet-check counter table */
#define PKT_BLOCK_ROW "    %-16s%14llu%14llu%14llu%10u%10u\n"

// ------------------------------------------------------------------------------------------------
// Typedefs
// ------------------------------------------------------------------------------------------------

/** @brief Per-instance private data. */
typedef struct {
    as2xxxx_priv_data_t priv; /**< Owned by as2xxxx_priv.c */
    mepa_conf_t         mepa_conf;
    mepa_event_t        enabled_events;
    uint32_t            port_no; /**< Only for logging. Board handle given at probe. */
} as2xxxx_phy_data_t;

// ------------------------------------------------------------------------------------------------
// Local function declarations
// ------------------------------------------------------------------------------------------------

static void print_progress_bar(const char *label, size_t done, size_t total);
static void fw_load_progress(size_t done_bytes, size_t total_bytes);
static void eye_scan_progress(size_t done_groups, size_t total_groups);

// ------------------------------------------------------------------------------------------------
// Local instances
// ------------------------------------------------------------------------------------------------

/** @brief Static driver instance */
static mepa_driver_t m_driver = {0};

/** @brief Supported events map */
static const struct {
    uint32_t            mepa_event_mask;
    as2xxxx_irq_event_t phy_event;
    const char         *name;
} m_supported_events_map[] = {
    {MESA_PHY_LINK_WAKE_ON_LAN_INT_EV, AS2XXXX_IRQ_EVENT_WAKE_ON_LAN,        "wake-on-LAN"       },
    {MESA_PHY_LINK_LOS_EV,             AS2XXXX_IRQ_EVENT_LINK_STATUS_CHANGE, "link status change"},
};

/** @brief How the firmware load reports progress */
static const as2xxxx_fw_progress_t m_fw_progress = {
    .step_size_bytes = FW_LOAD_PROGRESS_STEP_BYTES,
    .progress_func = fw_load_progress,
};

/** @brief Default temperature monitor configuration */
static const as2xxxx_temp_monitor_conf_t m_def_temp_mon_conf = {
    .config = AS2XXXX_TEMP_MONITOR_CONTINUOUS_SAMPLING,
    .enable = true,
};

// ------------------------------------------------------------------------------------------------
// Local helper functions
// ------------------------------------------------------------------------------------------------

static void print_progress_bar(const char *label, size_t done, size_t total)
{
    uint32_t percent = 100u;
    uint32_t filled = PROGRESS_BAR_WIDTH;
    uint32_t i = 0;

    if (total != 0) {
        percent = (uint32_t)((done * 100u) / total);
        if (percent > 100u) {
            percent = 100u;
        }

        filled = (uint32_t)((done * PROGRESS_BAR_WIDTH) / total);
        if (filled > PROGRESS_BAR_WIDTH) {
            filled = PROGRESS_BAR_WIDTH;
        }
    }

    putchar('\r');
    fputs(label, stdout);
    fputs(" ", stdout);

    putchar('[');
    for (i = 0; i < PROGRESS_BAR_WIDTH; i++) {
        putchar(i < filled ? '#' : ' ');
    }
    printf("] %3u%%", percent);
    fflush(stdout);

    if (done >= total) {
        putchar('\n');
    }
}

static void fw_load_progress(size_t done_bytes, size_t total_bytes)
{
    print_progress_bar("Loading AS2XXXX firmware...", done_bytes, total_bytes);
}

// The eye diagram is rendered into the caller's string stream, which MEPA only
// flushes once the whole debug dump has been built. The scan itself takes tens of
// seconds, so without this the console stays silent and the CLI looks hung.
static void eye_scan_progress(size_t done_groups, size_t total_groups)
{
    print_progress_bar("Scanning AS2XXXX eye...   ", done_groups, total_groups);
}

static const char *speed_to_string(mepa_port_speed_t speed)
{
    switch (speed) {
    case MESA_SPEED_100M:      return "100M";
    case MESA_SPEED_1G:        return "1G";
    case MESA_SPEED_2500M:     return "2.5G";
    case MESA_SPEED_5G:        return "5G";
    case MESA_SPEED_10G:       return "10G";
    case MESA_SPEED_AUTO:      return "Auto";
    case MESA_SPEED_UNDEFINED: return "Undefined";
    // Named rather than lumped into "Unsupported": these reach the log through a rejected
    // configuration, and the log line has to say what was asked for. The refusal that follows it
    // says why it was not granted.
    case MESA_SPEED_10M: return "10M";
    case MESA_SPEED_12G: return "12G";
    case MESA_SPEED_25G: return "25G";
    default:             return "Unknown";
    }
}

/** @brief Interfaces this PHY can be asked about. Anything else prints as "Other",
 *         so log lines pair this with the raw value. */
static const char *port_if_to_string(mepa_port_interface_t mac_if)
{
    switch (mac_if) {
    case MESA_PORT_INTERFACE_NO_CONNECTION: return "No connection";
    case MESA_PORT_INTERFACE_SGMII:         return "SGMII";
    case MESA_PORT_INTERFACE_SGMII_2G5:     return "SGMII 2G5";
    case MESA_PORT_INTERFACE_SGMII_CISCO:   return "SGMII Cisco";
    case MESA_PORT_INTERFACE_SERDES:        return "SERDES";
    case MESA_PORT_INTERFACE_SFI:           return "SFI";
    case MESA_PORT_INTERFACE_USXGMII:       return "USXGMII";
    default:                                return "Other";
    }
}

static const char *host_mode_to_string(as2xxxx_host_inband_t inband, mepa_port_speed_t lane_speed)
{
    // Not a field of its own: with in-band off there is no config word, so the lane rate is all
    // there is to go by - 1G is plain SGMII, anything faster is XFI following the line side. In-band
    // on means a fixed lane rate, and that rate determines the interface type.
    if (inband == AS2XXXX_HOST_INBAND_DIS) {
        return (lane_speed == MESA_SPEED_1G) ? "SGMII, no in-band" : "XFI";
    }

    if (inband != AS2XXXX_HOST_INBAND_ENA) {
        return "UNKNOWN";
    }

    switch (lane_speed) {
    case MESA_SPEED_10G:   return "USXGMII";
    case MESA_SPEED_1G:    return "SGMII";
    case MESA_SPEED_2500M:
    case MESA_SPEED_5G:    return "rate adaptation";
    // Only AS22xxx reports the lane rate, so an in-band lane cannot be named on AS21xxx.
    default: return "lane rate unknown";
    }
}

static mepa_port_speed_t top_aneg_speed(mepa_device_t *dev, const mepa_aneg_adv_t *const adv)
{
    // The AS2xxxx takes a single top speed rather than a per-speed advertisement, so only the
    // highest advertised rate is passed down. Half duplex and 10M are not looked at
    // because the PHY supports neither.
    if (adv->speed_10m_hdx || adv->speed_100m_hdx || adv->speed_1g_hdx) {
        LOG_W("Half duplex advertisement is ignored, the PHY runs full duplex only");
    }

    if (adv->speed_10g_fdx) {
        return MESA_SPEED_10G;
    }
    if (adv->speed_5g_fdx) {
        return MESA_SPEED_5G;
    }
    if (adv->speed_2g5_fdx) {
        return MESA_SPEED_2500M;
    }
    if (adv->speed_1g_fdx) {
        return MESA_SPEED_1G;
    }
    if (adv->speed_100m_fdx) {
        return MESA_SPEED_100M;
    }

    LOG_W("No supported speed is advertised, defaulting to 10G as the top aneg speed");
    return MESA_SPEED_10G;
}

// ------------------------------------------------------------------------------------------------
// Driver API
// ------------------------------------------------------------------------------------------------

static mepa_rc as2xxxx_delete(mepa_device_t *dev) { return mepa_delete_int(dev); }

static mepa_device_t *as2xxxx_probe(mepa_driver_t                           *drv,
                                    const mepa_callout_t MEPA_SHARED_PTR    *callout,
                                    struct mepa_callout_ctx MEPA_SHARED_PTR *callout_ctx,
                                    struct mepa_board_conf                  *conf)
{
    mepa_device_t      *dev = NULL;
    as2xxxx_phy_data_t *pdata = NULL;
    mepa_rc             rc = MEPA_RC_OK;

    if (drv == NULL || conf == NULL) {
        return NULL;
    }

    dev = mepa_create_int(drv, callout, callout_ctx, conf, (int32_t)(sizeof(as2xxxx_phy_data_t)));

    if (dev == NULL) {
        return NULL;
    }

    dev->drv = drv;
    dev->callout = callout;
    dev->callout_ctx = callout_ctx;
    pdata = (as2xxxx_phy_data_t *)dev->data;

    // Recorded before anything logs, since every message from this layer reports it.
    pdata->port_no = conf->numeric_handle;

    MEPA_ENTER(dev);
    rc = as2xxxx_priv_probe(&pdata->priv, dev, &m_fw_progress);

    if (rc == MEPA_RC_OK) {
        // Enabling the temperature monitor is a policy, not part of bring-up, so a failure only
        // costs temperature readings.
        if (as2xxxx_priv_set_temp_monitor_conf(&pdata->priv, &m_def_temp_mon_conf) != MEPA_RC_OK) {
            LOG_W("Failed to set the temperature monitor config. Getting temperatures might "
                  "fail");
        }
    }
    MEPA_EXIT(dev);

    if (rc != MEPA_RC_OK) {
        // More log about failure comes from the core logic layer, which is silent unless it is
        // enabled in as2xxxx_utils.h. Say at least that the probe failed.
        LOG_E("PHY probe failed (rc=%d)", (int)rc);
        as2xxxx_delete(dev);
        return NULL;
    }

    return dev;
}

static mepa_rc as2xxxx_reset(mepa_device_t *dev, const mepa_reset_param_t *rst_conf)
{
    as2xxxx_phy_data_t *pdata = (as2xxxx_phy_data_t *)dev->data;
    mepa_rc             rc = MEPA_RC_OK;
    NULL_CHECK(rst_conf);

    if (rst_conf->reset_point == MEPA_RESET_POINT_DEFAULT) {
        MEPA_ENTER(dev);
        rc = as2xxxx_priv_reset(&pdata->priv);
        if (rc != MEPA_RC_OK) {
            LOG_E("Failed to reset the PHY");
        }
        MEPA_EXIT(dev);
    }

    return rc;
}

static mepa_rc as2xxxx_poll(mepa_device_t *dev, mepa_status_t *status)
{
    mepa_rc             rc = MEPA_RC_OK;
    as2xxxx_phy_data_t *pdata = (as2xxxx_phy_data_t *)dev->data;
    mepa_bool_t         line_aneg_done = false, line_link = false, line_fdx = false;
    mepa_bool_t         generate_pause = false, obey_pause = false;
    mepa_port_speed_t   line_speed = MESA_SPEED_UNDEFINED;

    NULL_CHECK(status);

    MEPA_ENTER(dev);

    RC_WARN(as2xxxx_priv_get_link_status_line(&pdata->priv, &line_link),
            "Failed to get line link status");
    RC_WARN(as2xxxx_priv_get_aneg_done_flag_line(&pdata->priv, &line_aneg_done),
            "Failed to get aneg done flag");

    // In auto-negotiation mode only, cover the window where the PHY has already
    // cleared "AN complete" but has not yet dropped the link.
    if (pdata->mepa_conf.speed == MESA_SPEED_AUTO && !line_aneg_done) {
        line_link = false;
    }

    // A speed is only reported while the line link is up
    if (line_link) {
        RC_WARN(as2xxxx_priv_get_speed_line(&pdata->priv, &line_speed, &line_fdx),
                "Failed to get line speed");

        RC_WARN(as2xxxx_priv_get_flow_control_line(&pdata->priv, &generate_pause, &obey_pause),
                "Failed to get the resolved flow control");
    }

    // Update status. The resolved pause is gated on what was asked for, because the line side
    // advertises pause on AS22xxx whether or not it was configured - the firmware owns that part.
    // Without the gate the negotiation alone decides, and turning flow control off
    // has no effect at all: the switch keeps generating and obeying pause because the link partner
    // agreed to it. The gate costs nothing where the advertisement does follow the setting, since
    // the negotiation cannot resolve pause that was never advertised.
    status->link = line_link;
    status->speed = line_speed;
    status->fdx = line_fdx;
    status->aneg.generate_pause = generate_pause && pdata->mepa_conf.flow_control;
    status->aneg.obey_pause = obey_pause && pdata->mepa_conf.flow_control;

    MEPA_EXIT(dev);

    // In case something fails, above, it warns and continues
    // Returning early would mean only partial status is delivered to the caller
    UNUSED(rc);
    return MEPA_RC_OK;
}

static mepa_rc as2xxxx_event_enable_set(mepa_device_t *dev, mepa_event_t event, mesa_bool_t enable)
{
    mepa_rc            rc = MEPA_RC_OK;
    as2xxxx_irq_conf_t irq_conf = {0};
    size_t             i;
    mepa_bool_t        any_matched = false;

    as2xxxx_phy_data_t *pdata = (as2xxxx_phy_data_t *)dev->data;

    MEPA_ENTER(dev);
    irq_conf.signal_type = AS2XXXX_IRQ_SIGNAL_TYPE_PULSE;
    irq_conf.number_of_pulses = AS2XXXX_IRQ_NUM_OF_PULSES_DEFAULT;
    irq_conf.enable = enable;

    for (i = 0; i < ARRAY_SIZE(m_supported_events_map); i++) {

        if (!FIELD_GET32(m_supported_events_map[i].mepa_event_mask, event)) {
            continue;
        }

        any_matched = true;
        LOG_I("%s event %s", m_supported_events_map[i].name, enable ? "enabled" : "disabled");

        // Enable/disable event as per config
        irq_conf.event = m_supported_events_map[i].phy_event;
        RC_GOTO(rc, as2xxxx_priv_set_irq_event_conf(&pdata->priv, &irq_conf),
                "Failed to set IRQ config");
        // Cache event enable/disable state
        BIT_MODIFY32(pdata->enabled_events, m_supported_events_map[i].mepa_event_mask, enable);
    }

    if (!any_matched) {
        LOG_E("Given event with mask 0x%08x is not supported by the PHY", event);
        rc = MEPA_RC_ERR_PARM;
        goto error;
    }

error:
    MEPA_EXIT(dev);
    return rc;
}

static mepa_rc as2xxxx_event_enable_get(mepa_device_t *dev, mepa_event_t *const event)
{
    as2xxxx_phy_data_t *pdata = (as2xxxx_phy_data_t *)dev->data;
    NULL_CHECK(event);
    *event = pdata->enabled_events;
    return MEPA_RC_OK;
}

static mepa_rc as2xxxx_event_poll(struct mepa_device *dev, mepa_event_t *const ev_mask)
{
    as2xxxx_phy_data_t *pdata = (as2xxxx_phy_data_t *)dev->data;
    mepa_rc             rc = MEPA_RC_OK;
    mepa_bool_t         is_event_active;
    size_t              i;

    NULL_CHECK(ev_mask);

    MEPA_ENTER(dev);
    for (i = 0; i < ARRAY_SIZE(m_supported_events_map); i++) {
        RC_GOTO(rc,
                as2xxxx_priv_get_irq_event_state(&pdata->priv, m_supported_events_map[i].phy_event,
                                                 &is_event_active),
                "Failed to get event state");

        BIT_MODIFY32(*ev_mask, m_supported_events_map[i].mepa_event_mask, is_event_active);
    }

error:
    MEPA_EXIT(dev);
    return rc;
}

static mepa_rc as2xxxx_conf_set(mepa_device_t *dev, const mepa_conf_t *config)
{
    mepa_rc             rc = MEPA_RC_OK;
    as2xxxx_phy_data_t *pdata = (as2xxxx_phy_data_t *)dev->data;
    mepa_port_speed_t   line_speed;
    mepa_bool_t         line_fdx;
    mepa_bool_t         aneg_enable;

    NULL_CHECK(config);

    aneg_enable = (config->speed == MESA_SPEED_AUTO);

    // 10M is deliberately absent from the advertisement list: the PHY has no 10M, top_aneg_speed()
    // ignores the bit, and printing it would suggest it was passed on.
    LOG_I("admin %s, speed %s%s, flow control %s%s%s%s%s%s%s",
          config->admin.enable ? "enable" : "disable", speed_to_string(config->speed),
          aneg_enable ? "" : (config->fdx ? " fdx" : " hdx"), config->flow_control ? "on" : "off",
          aneg_enable ? ", advertising " : "",
          (aneg_enable && config->aneg.speed_100m_fdx) ? "100M " : "",
          (aneg_enable && config->aneg.speed_1g_fdx) ? "1G " : "",
          (aneg_enable && config->aneg.speed_2g5_fdx) ? "2.5G " : "",
          (aneg_enable && config->aneg.speed_5g_fdx) ? "5G " : "",
          (aneg_enable && config->aneg.speed_10g_fdx) ? "10G" : "");

    MEPA_ENTER(dev);
    // Enable/disable the PHY
    if (config->admin.enable) {
        RC_GOTO(rc, as2xxxx_priv_phy_enable(&pdata->priv), "Failed to enable the PHY");
    } else {
        RC_GOTO(rc, as2xxxx_priv_phy_disable(&pdata->priv), "Failed to disable the PHY");
    }

    // Set the line config. A forced speed is taken as given, while an advertisement has to be
    // reduced to the single top speed the firmware accepts, and is always full duplex because the
    // PHY has no other.
    line_speed = aneg_enable ? top_aneg_speed(dev, &config->aneg) : config->speed;
    line_fdx = aneg_enable ? true : config->fdx;

    RC_GOTO(rc,
            as2xxxx_priv_set_line_config(&pdata->priv, line_speed, line_fdx, aneg_enable,
                                         config->flow_control),
            "Failed to set the line configuration");

    // Store config
    pdata->mepa_conf = *config;

error:
    MEPA_EXIT(dev);
    return rc;
}

static mepa_rc as2xxxx_conf_get(mepa_device_t *dev, mepa_conf_t *const config)
{
    as2xxxx_phy_data_t *pdata = (as2xxxx_phy_data_t *)dev->data;
    NULL_CHECK(config);
    *config = pdata->mepa_conf;
    return MEPA_RC_OK;
}

static mepa_rc as2xxxx_if_set(mepa_device_t *dev, mepa_port_interface_t intf)
{
    as2xxxx_phy_data_t   *pdata = (as2xxxx_phy_data_t *)dev->data;
    as2xxxx_host_inband_t host_inband;
    mepa_port_speed_t     line_speed = MESA_SPEED_UNDEFINED;
    mepa_port_speed_t     host_speed = MESA_SPEED_UNDEFINED;
    mepa_bool_t           line_fdx = false;
    mepa_bool_t           line_link = false;
    mepa_rc               rc = MEPA_RC_OK;

    switch (intf) {
    case MESA_PORT_INTERFACE_USXGMII:
        host_inband = AS2XXXX_HOST_INBAND_ENA;
        host_speed = MESA_SPEED_10G;
        break;

    case MESA_PORT_INTERFACE_SGMII:
        host_inband = AS2XXXX_HOST_INBAND_DIS;
        host_speed = MESA_SPEED_1G;
        break;
    case MESA_PORT_INTERFACE_SGMII_CISCO:
        host_inband = AS2XXXX_HOST_INBAND_ENA;
        host_speed = MESA_SPEED_1G;
        break;
    case MESA_PORT_INTERFACE_SGMII_2G5:
    case MESA_PORT_INTERFACE_SFI:
        host_inband = AS2XXXX_HOST_INBAND_DIS;
        host_speed = MESA_SPEED_UNDEFINED;
        break;

    default:
        LOG_E("Host interface %s is not supported by this PHY", port_if_to_string(intf));
        return MEPA_RC_ERROR;
    }

    LOG_I("Setting host interface to: %s. In-band %s. Rate: %s", port_if_to_string(intf),
          as2xxxx_priv_host_inband_to_string(host_inband),
          (host_speed == MESA_SPEED_UNDEFINED) ? "follows the line side"
                                               : speed_to_string(host_speed));

    MEPA_ENTER(dev);

    // XFI runs the host lane at whatever the line side negotiated, so the two have to agree.
    if (host_speed == MESA_SPEED_UNDEFINED) {
        RC_GOTO(rc, as2xxxx_priv_get_speed_line(&pdata->priv, &line_speed, &line_fdx),
                "Failed to get the line speed for XFI matching");

        if (line_speed == MESA_SPEED_UNDEFINED) {
            // Nothing to follow, so the host rate is left alone and success is still reported: at
            // probe there legitimately is no line speed yet. The line link state is logged because
            // it decides whether this heals itself. Down, the caller gets another link event when
            // the line comes back and configures the host then. Up, there is no further event and
            // the host keeps the wrong rate until the line next flaps.
            RC_WARN(as2xxxx_priv_get_link_status_line(&pdata->priv, &line_link),
                    "Failed to read the line link status");

            LOG_W("No line speed to follow with the line link %s, leaving the host lane at its "
                  "current rate%s",
                  line_link ? "UP" : "down",
                  line_link ? " - nothing will correct this until the line next flaps" : "");
            rc = MEPA_RC_OK;
            goto error;
        }

        // The host SerDes has no rate below 1G, so a line side that slow still leaves the lane at
        // 1G and relies on in-band signalling.
        host_speed = (line_speed <= MESA_SPEED_1G) ? MESA_SPEED_1G : line_speed;

        LOG_I("Reconfiguring the host lane to %s to match the line side at %s",
              speed_to_string(host_speed), speed_to_string(line_speed));
    }

    RC_GOTO(rc, as2xxxx_priv_set_host_cfg(&pdata->priv, host_inband, host_speed),
            "Failed to set the host interface");

error:
    MEPA_EXIT(dev);
    return rc;
}

static mepa_rc as2xxxx_if_get(mepa_device_t         *dev,
                              mepa_port_speed_t      speed,
                              mepa_port_interface_t *mac_if)
{
    UNUSED(dev);
    NULL_CHECK(mac_if);

    switch (speed) {
    case MESA_SPEED_10G:
    case MESA_SPEED_5G:    *mac_if = MESA_PORT_INTERFACE_SFI; break;
    case MESA_SPEED_2500M: *mac_if = MESA_PORT_INTERFACE_SGMII_2G5; break;
    case MESA_SPEED_1G:
    case MESA_SPEED_100M:  *mac_if = MESA_PORT_INTERFACE_SGMII_CISCO; break;
    case MESA_SPEED_10M:
    case MESA_SPEED_12G:
    case MESA_SPEED_25G:
    case MESA_SPEED_AUTO:
    case MESA_SPEED_UNDEFINED:
    default:
        *mac_if = MESA_PORT_INTERFACE_NO_CONNECTION;
        LOG_E("No host interface for speed %s, the PHY does not support it",
              speed_to_string(speed));
        return MEPA_RC_ERR_PARM;
    }

    LOG_D("speed %s -> host interface %s (%u)", speed_to_string(speed), port_if_to_string(*mac_if),
          *mac_if);

    return MEPA_RC_OK;
}

static mepa_rc as2xxxx_media_get(mepa_device_t *dev, mepa_media_interface_t *media_if)
{
    UNUSED(dev);
    NULL_CHECK(media_if);

    // This is a copper PHY
    *media_if = MESA_PHY_MEDIA_IF_CU;
    return MEPA_RC_OK;
}

static mepa_rc as2xxxx_info_get(mepa_device_t *dev, mepa_phy_info_t *const phy_info)
{
    mepa_rc             rc = MEPA_RC_OK;
    as2xxxx_phy_data_t *pdata = (as2xxxx_phy_data_t *)dev->data;

    NULL_CHECK(phy_info);
    MEPA_ENTER(dev);

    phy_info->manufactor_name = AEONSEMI_MANUFACTURER_STRING;
    phy_info->model_name = AEONSEMI_PHY_MODEL_NAME;
    phy_info->firmware_rev = as2xxxx_priv_fw_version_get(&pdata->priv);
    phy_info->revision = as2xxxx_priv_revision_from_phy_id(as2xxxx_priv_dev_id_get(&pdata->priv));
    phy_info->part_number = as2xxxx_priv_model_from_phy_id(as2xxxx_priv_dev_id_get(&pdata->priv));
    phy_info->cap = MEPA_CAP_SPEED_MASK_10G;

    MEPA_EXIT(dev);

    return rc;
}

static mepa_rc as2xxxx_loopback_set(struct mepa_device *dev, const mepa_loopback_t *loopback)
{
    as2xxxx_phy_data_t *pdata = (as2xxxx_phy_data_t *)dev->data;
    mepa_rc             rc_line, rc_host;
    NULL_CHECK(loopback);

    LOG_I("loopback_set: near-end %u, far-end %u (connector %u, mac sds in/fac/eq %u/%u/%u, "
          "media sds in/fac/eq %u/%u/%u, qsgmii tbi/gmii/sds %u/%u/%u)",
          loopback->near_end_ena, loopback->far_end_ena, loopback->connector_ena,
          loopback->mac_serdes_input_ena, loopback->mac_serdes_facility_ena,
          loopback->mac_serdes_equip_ena, loopback->media_serdes_input_ena,
          loopback->media_serdes_facility_ena, loopback->media_serdes_equip_ena,
          loopback->qsgmii_pcs_tbi_ena, loopback->qsgmii_pcs_gmii_ena, loopback->qsgmii_serdes_ena);

    MEPA_ENTER(dev);

    rc_line = as2xxxx_priv_set_loopback_mode_line(&pdata->priv, loopback->far_end_ena);
    if (rc_line != MEPA_RC_OK) {
        LOG_E("Failed to set far-end loopback");
    }

    rc_host = as2xxxx_priv_set_loopback_mode_host(&pdata->priv, loopback->near_end_ena);
    if (rc_host != MEPA_RC_OK) {
        LOG_E("Failed to set near-end loopback");
    }

    // Warn about unsupported loopback modes
    if (loopback->connector_ena) {
        LOG_W("Connector loopback is not supported by this PHY");
    }
    if (loopback->mac_serdes_input_ena) {
        LOG_W("MAC SerDes input loopback is not supported by this PHY");
    }
    if (loopback->mac_serdes_facility_ena) {
        LOG_W("MAC SerDes facility loopback is not supported by this PHY");
    }
    if (loopback->mac_serdes_equip_ena) {
        LOG_W("MAC SerDes equipment loopback is not supported by this PHY");
    }
    if (loopback->media_serdes_input_ena) {
        LOG_W("Media SerDes input loopback is not supported by this PHY");
    }
    if (loopback->media_serdes_facility_ena) {
        LOG_W("Media SerDes facility loopback is not supported by this PHY");
    }
    if (loopback->media_serdes_equip_ena) {
        LOG_W("Media SerDes equipment loopback is not supported by this PHY");
    }
    if (loopback->qsgmii_pcs_tbi_ena) {
        LOG_W("QSGMII PCS TBI loopback is not supported by this PHY");
    }
    if (loopback->qsgmii_pcs_gmii_ena) {
        LOG_W("QSGMII PCS GMII loopback is not supported by this PHY");
    }
    if (loopback->qsgmii_serdes_ena) {
        LOG_W("QSGMII SerDes loopback is not supported by this PHY");
    }

    MEPA_EXIT(dev);
    return (rc_line != MEPA_RC_OK) ? rc_line : rc_host;
}

static mepa_rc as2xxxx_loopback_get(struct mepa_device *dev, mepa_loopback_t *const loopback)
{
    as2xxxx_phy_data_t *pdata = (as2xxxx_phy_data_t *)dev->data;
    mepa_rc             rc = MEPA_RC_OK;
    NULL_CHECK(loopback);

    MEPA_ENTER(dev);
    RC_GOTO(rc, as2xxxx_priv_is_loopback_mode_line(&pdata->priv, &loopback->far_end_ena),
            "Failed to get far-end loopback");
    LOG_D("loopback_get: far-end %u", loopback->far_end_ena);

    // Near-end comes from what the driver applied, not from the PHY - see
    // as2xxxx_priv_is_loopback_mode_host().
    RC_GOTO(rc, as2xxxx_priv_is_loopback_mode_host(&pdata->priv, &loopback->near_end_ena),
            "Failed to get near-end loopback");

error:
    MEPA_EXIT(dev);
    return rc;
}

static uint32_t as2xxxx_capability(struct mepa_device *dev, uint32_t capability)
{
    uint32_t value;

    UNUSED(dev);

    switch (capability) {
    case MEPA_CAP_HOST_RATE_FOLLOWS_LINE: value = 1; break;
    case MEPA_CAP_TS_NONE:                value = 1; break;
    case MEPA_CAP_LOOPBACK:               value = AS2XXXX_LOOPBACK_CAP; break;
    default:                              value = 0; break;
    }

    return value;
}

static mepa_rc as2xxxx_serdes_tx_conf_set(struct mepa_device                *dev,
                                          const mepa_serdes_tx_conf_t *const tx_conf)
{
    as2xxxx_phy_data_t   *pdata = (as2xxxx_phy_data_t *)dev->data;
    as2xxxx_tx_fir_conf_t fir = {0};
    mepa_rc               rc = MEPA_RC_OK;

    NULL_CHECK(tx_conf);
    fir.main = tx_conf->level;
    fir.pre = tx_conf->pre;
    fir.post = tx_conf->post;

    LOG_I("Setting Tx FIR: pre %u, main %u, post %u", fir.pre, fir.main, fir.post);

    MEPA_ENTER(dev);
    rc = as2xxxx_priv_set_tx_fir(&pdata->priv, &fir);

    if (rc != MEPA_RC_OK) {
        LOG_E("Failed to set the Tx FIR");
    }

    MEPA_EXIT(dev);
    return rc;
}

static mepa_rc as2xxxx_chip_temp_get(struct mepa_device *dev, i16 *const temp)
{
    as2xxxx_phy_data_t *pdata = (as2xxxx_phy_data_t *)dev->data;
    mepa_rc             rc = MEPA_RC_OK;

    NULL_CHECK(temp);
    MEPA_ENTER(dev);

    // MEPA reports whole degrees only, so the fractional part is dropped here.
    rc = as2xxxx_priv_get_temp_monitor_last_sample(&pdata->priv, temp, NULL);
    if (rc != MEPA_RC_OK) {
        LOG_E("Failed to get the die temperature");
    }

    MEPA_EXIT(dev);
    return rc;
}

static mepa_rc as2xxxx_debug_info(struct mepa_device            *dev,
                                  lmu_ss_t *const                ss,
                                  const mepa_debug_info_t *const info)
{
    as2xxxx_phy_data_t       *pdata = (as2xxxx_phy_data_t *)dev->data;
    const as2xxxx_irq_event_t link_change_evt = AS2XXXX_IRQ_EVENT_LINK_STATUS_CHANGE;
    int16_t                   temp_celsius = 0;
    uint16_t                  temp_celsius_frac = 0;
    mepa_bool_t               link_evt_active = false;
    as2xxxx_counters_t        counters = {0};
    mepa_bool_t               live_line_link = false, live_host_link = false, live_line_fdx = false;
    mepa_bool_t               live_aneg_done = false;
    mepa_port_speed_t         live_line_speed = MESA_SPEED_UNDEFINED;
    as2xxxx_host_inband_t     live_host_inband = AS2XXXX_HOST_INBAND_UNKNOWN;
    mepa_bool_t               live_sds_link = false;
    mepa_port_speed_t         live_sds_speed = MESA_SPEED_UNDEFINED;
    mepa_port_speed_t         cfg_lane_speed = MESA_SPEED_UNDEFINED;
    mepa_bool_t               live_line_loopback = false;
    mepa_bool_t               cached_host_loopback = false;
    mepa_rc rc_temp, rc_irq, rc_counters, rc_line_link, rc_host_link, rc_aneg, rc_line_speed,
        rc_host_inband, rc_sds_state, rc_line_lb, rc_host_lb, rc_lane_rate;
    size_t i;

    // Shorthands for the counter set of each family.
    const as2xxxx_counters_as21xxx_t *c21 = &counters.u.as21xxx;
    const as2xxxx_counters_as22xxx_t *c22 = &counters.u.as22xxx;

    // The four measurement points around the AS22xxx datapath, SerDes side first
    const struct {
        const char                *name;
        const as2xxxx_pkt_block_t *blk;
    } blocks[] = {
        {"From SerDes",   &counters.u.as22xxx.from_sds},
        {"To Ethernet",   &counters.u.as22xxx.to_eth  },
        {"From Ethernet", &counters.u.as22xxx.from_eth},
        {"To SerDes",     &counters.u.as22xxx.to_sds  },
    };

    NULL_CHECK(ss);
    NULL_CHECK(info);

    // This driver has one group to report. Anything else is answered by saying nothing, which is
    // what the other MEPA drivers do - printing the PHY dump under a group it does not belong to
    // would read as that group's answer.
    if (info->group != MEPA_DEBUG_GROUP_ALL && info->group != MEPA_DEBUG_GROUP_PHY) {
        return MEPA_RC_OK;
    }

    // =========================================================================
    // Fetch all data from hardware
    // =========================================================================

    // One critical section so the whole dump describes a single point in time.
    // The printing below deliberately runs unlocked.
    MEPA_ENTER(dev);

    rc_line_link = as2xxxx_priv_get_link_status_line(&pdata->priv, &live_line_link);
    rc_aneg = as2xxxx_priv_get_aneg_done_flag_line(&pdata->priv, &live_aneg_done);

    // A speed is only reported while the link is up.
    rc_line_speed = MEPA_RC_OK;

    if (live_line_link) {
        rc_line_speed = as2xxxx_priv_get_speed_line(&pdata->priv, &live_line_speed, &live_line_fdx);
    }

    rc_host_inband = as2xxxx_priv_get_host_inband(&pdata->priv, &live_host_inband);
    rc_lane_rate = as2xxxx_priv_get_host_lane_rate(&pdata->priv, &cfg_lane_speed);

    // Neither loopback can be switched off, so worth stating it here.
    rc_line_lb = as2xxxx_priv_is_loopback_mode_line(&pdata->priv, &live_line_loopback);
    rc_host_lb = as2xxxx_priv_is_loopback_mode_host(&pdata->priv, &cached_host_loopback);

    // AS22xxx only, so a failure here is normal on AS21xxx rather than a fault.
    rc_sds_state = as2xxxx_priv_get_host_sds_state(&pdata->priv, &live_sds_link, &live_sds_speed);

    // The mapped status bit is only the fallback for a family that has no SerDes report, so it is
    // read solely when there is none. It is the less trustworthy of the two sources and nothing
    // would print it otherwise.
    rc_host_link = MEPA_RC_ERROR;

    if (rc_sds_state != MEPA_RC_OK) {
        rc_host_link = as2xxxx_priv_get_link_status_host(&pdata->priv, &live_host_link);
    }

    rc_temp =
        as2xxxx_priv_get_temp_monitor_last_sample(&pdata->priv, &temp_celsius, &temp_celsius_frac);
    rc_irq = as2xxxx_priv_get_irq_event_state(&pdata->priv, link_change_evt, &link_evt_active);
    rc_counters = as2xxxx_priv_get_counters(&pdata->priv, &counters, info->clear);

    MEPA_EXIT(dev);

    // =========================================================================
    // Print all collected data
    // =========================================================================

    // Print header
    pr("\n");
    pr("=============================================================================\n");
    pr("  AeonSemi AS2XXXX PHY Debug Information\n");
    pr("=============================================================================\n");
    pr("\n");

    // Device identification
    pr("Device Information:\n");
    pr(DEBUG_LABEL "0x%08X\n", "PHY ID (Device ID)", as2xxxx_priv_dev_id_get(&pdata->priv));
    pr(DEBUG_LABEL "%s\n", "Chip Family",
       as2xxxx_priv_family_to_string(as2xxxx_priv_family_get(&pdata->priv)));
    pr(DEBUG_LABEL "0x%01X\n", "Chip Model (nibble)",
       as2xxxx_priv_model_from_phy_id(as2xxxx_priv_dev_id_get(&pdata->priv)));
    pr(DEBUG_LABEL "0x%01X\n", "Chip Revision",
       as2xxxx_priv_revision_from_phy_id(as2xxxx_priv_dev_id_get(&pdata->priv)));
    pr(DEBUG_LABEL "%s\n", "Firmware Version", as2xxxx_priv_fw_version_get(&pdata->priv));
    pr("\n");

    // Configuration
    pr("Configuration:\n");
    pr(DEBUG_LABEL "%s\n", "Admin", pdata->mepa_conf.admin.enable ? "ENABLED" : "DISABLED");
    if (pdata->mepa_conf.speed == MESA_SPEED_AUTO) {
        pr(DEBUG_LABEL "AUTO (", "Speed");
        pr("%s", pdata->mepa_conf.aneg.speed_100m_fdx ? "100M " : "");
        pr("%s", pdata->mepa_conf.aneg.speed_1g_fdx ? "1G " : "");
        pr("%s", pdata->mepa_conf.aneg.speed_2g5_fdx ? "2.5G " : "");
        pr("%s", pdata->mepa_conf.aneg.speed_5g_fdx ? "5G " : "");
        pr("%s", pdata->mepa_conf.aneg.speed_10g_fdx ? "10G " : "");
        pr(")\n");
    } else {
        pr(DEBUG_LABEL "%s %s\n", "Speed", speed_to_string(pdata->mepa_conf.speed),
           pdata->mepa_conf.fdx ? "fdx" : "hdx");
    }
    pr("\n");

    pr("Status:\n");
    if (rc_line_link != MEPA_RC_OK) {
        pr(DEBUG_LABEL "FAILED TO RETRIEVE\n", "Line link");
    } else if (!live_line_link) {
        pr(DEBUG_LABEL "DOWN\n", "Line link");
    } else if (rc_line_speed != MEPA_RC_OK) {
        pr(DEBUG_LABEL "UP (speed failed to retrieve)\n", "Line link");
    } else {
        pr(DEBUG_LABEL "UP %s %s\n", "Line link", speed_to_string(live_line_speed),
           live_line_fdx ? "fdx" : "hdx");
    }

    if (rc_aneg != MEPA_RC_OK) {
        pr(DEBUG_LABEL "FAILED TO RETRIEVE\n", "Line aneg complete");
    } else {
        pr(DEBUG_LABEL "%s\n", "Line aneg complete", live_aneg_done ? "YES" : "NO");
    }

    // Two sources for one fact, and only AS22xxx has the reliable one.
    if (rc_sds_state == MEPA_RC_OK) {
        if (live_sds_link) {
            pr(DEBUG_LABEL "UP %s\n", "Host SerDes link (achieved)",
               speed_to_string(live_sds_speed));
        } else {
            // The rate is reported rather than named: it comes from the same word as the link
            // flag, and what the field means with the link down is not documented. Kept because a
            // rate here that disagrees with the configured one is how an unapplied lane rate
            // shows up.
            pr(DEBUG_LABEL "DOWN, PHY reports %s rate\n", "Host SerDes link (achieved)",
               speed_to_string(live_sds_speed));
        }
    } else if (rc_host_link == MEPA_RC_OK) {
        pr(DEBUG_LABEL "%s (status bit, may be stale)\n", "Host SerDes link",
           live_host_link ? "UP" : "DOWN");
    } else {
        pr(DEBUG_LABEL "FAILED TO RETRIEVE\n", "Host SerDes link");
    }

    if (rc_lane_rate != MEPA_RC_OK) {
        pr(DEBUG_LABEL "FAILED TO RETRIEVE\n", "Host lane rate (from config)");
    } else {
        pr(DEBUG_LABEL "%s\n", "Host lane rate (from config)", speed_to_string(cfg_lane_speed));
    }

    // In XFI the lane must run at the line side speed and nothing in the PHY bridges a difference,
    // so either disagreement below loses frames with nothing to report it - measured at 80% loss.
    // They are separate faults: a rate the lane never applied, and a host that was never moved to
    // the speed the line settled on.
    if (rc_lane_rate == MEPA_RC_OK) {
        if (live_host_inband == AS2XXXX_HOST_INBAND_DIS) {
            if (rc_sds_state == MEPA_RC_OK && live_sds_link && cfg_lane_speed != live_sds_speed) {
                pr(DEBUG_LABEL "configured %s but the lane is running %s - the rate was not "
                               "applied\n",
                   "*** HOST RATE MISMATCH", speed_to_string(cfg_lane_speed),
                   speed_to_string(live_sds_speed));
            }

            // Above 1G only: the lane has no rate below it, so a slower line legitimately leaves
            // the lane at 1G and signals the rest in-band.
            if (rc_line_speed == MEPA_RC_OK && live_line_link &&
                cfg_lane_speed != live_line_speed && live_line_speed > MESA_SPEED_1G) {
                pr(DEBUG_LABEL "line is %s but the host lane is %s - the host was never moved to "
                               "match it\n",
                   "*** LINE/HOST MISMATCH", speed_to_string(live_line_speed),
                   speed_to_string(cfg_lane_speed));
            }
        }
    }

    if (rc_host_inband != MEPA_RC_OK) {
        pr(DEBUG_LABEL "FAILED TO RETRIEVE\n", "Host mode");
    } else {
        // The protocol name is derived rather than read: the in-band bit is only half of it, the
        // other half being the lane rate, which comes from the SerDes report that only AS22xxx
        // has. The bit is shown alongside because without it the name cannot be checked.
        pr(DEBUG_LABEL "%s (in-band aneg %s)\n", "Host mode",
           host_mode_to_string(live_host_inband, (rc_sds_state == MEPA_RC_OK)
                                                     ? live_sds_speed
                                                     : MESA_SPEED_UNDEFINED),
           as2xxxx_priv_host_inband_to_string(live_host_inband));
    }

    if (rc_line_lb != MEPA_RC_OK) {
        pr(DEBUG_LABEL "FAILED TO RETRIEVE\n", "Line loopback");
    } else {
        pr(DEBUG_LABEL "%s\n", "Line loopback", live_line_loopback ? "ON" : "off");
    }

    if (rc_host_lb != MEPA_RC_OK) {
        pr(DEBUG_LABEL "FAILED TO RETRIEVE\n", "Host loopback");
    } else {
        pr(DEBUG_LABEL "%s\n", "Host loopback",
           cached_host_loopback ? "ON (driver state, no query exists)" : "off");
    }
    pr("\n");

    // Temperature
    pr("Die temperature:\n");
    if (rc_temp == MEPA_RC_OK) {
        pr(DEBUG_LABEL "%d.%02u\n", "Temperature [deg C]", temp_celsius, temp_celsius_frac);
    } else {
        pr(DEBUG_LABEL "FAILED TO RETRIEVE\n", "Temperature [deg C]");
    }
    pr("\n");

    // Events
    pr("Events status (IRQs):\n");
    if (rc_irq == MEPA_RC_OK) {
        pr(DEBUG_LABEL "%s\n", "Link change event", link_evt_active ? "ACTIVE" : "NOT ACTIVE");
    } else {
        pr(DEBUG_LABEL "FAILED TO RETRIEVE\n", "Link change event");
    }
    pr("\n");

    // Counters
    pr("Counters:%s\n",
       info->clear ? "  (values are pre-clear, counters cleared after this dump)" : "");
    if (rc_counters != MEPA_RC_OK) {
        pr("  Failed to get counters\n");
    } else if (counters.family == AS2XXXX_FAMILY_AS21XXX) {
        pr("  NGPHY Counters:\n");
        pr(DEBUG_SUBLABEL "%u\n", "Errored Blocks", c21->ngphy.err_blk);
        pr(DEBUG_SUBLABEL "%u\n", "Bit Error Rate Counter", c21->ngphy.ber);
        pr(DEBUG_SUBLABEL "%u\n", "High BER Events", c21->ngphy.hi_ber);
        pr(DEBUG_SUBLABEL "%u\n", "Codeword Frame Count", c21->ngphy.cfr);
        pr(DEBUG_SUBLABEL "%u\n", "Codeword Frame Errors", c21->ngphy.cfr_err);

        pr("  DPC Counters:\n");
        pr(DEBUG_SUBLABEL "%u\n", "LDPC Decode Errors", c21->dpc.ldpc_err);
        pr(DEBUG_SUBLABEL "%u\n", "Ethernet CRC Errors", c21->dpc.eth_crc_err);
        pr(DEBUG_SUBLABEL "%u\n", "SerDes CRC Errors", c21->dpc.sds_crc_err);

        pr("  Copper Auto-Negotiation:\n");
        pr(DEBUG_SUBLABEL "%u\n", "AN Restart Count", c21->cu_an.cu_an);
    } else if (counters.family == AS2XXXX_FAMILY_AS22XXX) {
        // One row per measurement point rather than repeating the five field names four times.
        pr("  Packet Check Counters:\n");
        pr(PKT_BLOCK_HEAD, "Block", "Frames", "CRC Err", "Err Sym", "Runt", "Oversize");
        for (i = 0; i < ARRAY_SIZE(blocks); i++) {
            pr(PKT_BLOCK_ROW, blocks[i].name, (uint64_t)blocks[i].blk->frm_cnt,
               (uint64_t)blocks[i].blk->crc_err, (uint64_t)blocks[i].blk->err_sym,
               blocks[i].blk->runt, blocks[i].blk->oversize);
        }

        pr("  SerDes Rx:\n");
        pr(DEBUG_SUBLABEL "%u\n", "Bit Error Rate Counter", c22->sds_rx_ber);
        pr(DEBUG_SUBLABEL "%u\n", "Errored Blocks", c22->sds_rx_err_blk);
        pr(DEBUG_SUBLABEL "%u\n", "Link Down Events", c22->sds_link_down_cnt);

        pr("  NGPHY Counters:\n");
        pr(DEBUG_SUBLABEL "%u\n", "LDPC Decode Errors", c22->ldpc_err);
        pr(DEBUG_SUBLABEL "%u\n", "Codeword Frame Count", c22->cfr_cnt);

        pr("  Copper Auto-Negotiation:\n");
        pr(DEBUG_SUBLABEL "%u\n", "AN Restart Count", c22->cu_an);
    }

    pr("\n");

    // Print eye diagram if full is requested
    if (info->full) {
        // Eye diagram (note: this function does its own printing)
        if (as2xxxx_priv_print_eye(&pdata->priv, ss, eye_scan_progress) != MEPA_RC_OK) {
            pr("Failed to print eye\n");
        }
        pr("\n");
    }

    pr("=============================================================================\n");
    pr("\n");

    return MEPA_RC_OK;
}

static mepa_rc as2xxxx_reg_read(mepa_device_t *dev, uint32_t addr, uint16_t *const value)
{
    as2xxxx_phy_data_t *pdata = (as2xxxx_phy_data_t *)dev->data;
    mepa_rc             rc;

    MEPA_ENTER(dev);
    rc = as2xxxx_priv_reg_rd(&pdata->priv, GET_MMD_REGADR(addr), value);
    MEPA_EXIT(dev);

    return rc;
}

static mepa_rc as2xxxx_reg_write(mepa_device_t *dev, uint32_t addr, uint16_t value)
{
    as2xxxx_phy_data_t *pdata = (as2xxxx_phy_data_t *)dev->data;
    mepa_rc             rc;

    MEPA_ENTER(dev);
    rc = as2xxxx_priv_reg_wr(&pdata->priv, GET_MMD_REGADR(addr), value);
    MEPA_EXIT(dev);

    return rc;
}

static mepa_rc as2xxxx_mmd_reg_read(mepa_device_t *dev, uint32_t addr, uint16_t *const value)
{
    as2xxxx_phy_data_t *pdata = (as2xxxx_phy_data_t *)dev->data;
    mepa_rc             rc;

    MEPA_ENTER(dev);
    rc = as2xxxx_priv_reg_rd_mmd(&pdata->priv, GET_MMD_DEVADR(addr), GET_MMD_REGADR(addr), value);
    MEPA_EXIT(dev);

    return rc;
}

static mepa_rc as2xxxx_mmd_reg_write(mepa_device_t *dev, uint32_t addr, uint16_t value)
{
    as2xxxx_phy_data_t *pdata = (as2xxxx_phy_data_t *)dev->data;
    mepa_rc             rc;

    MEPA_ENTER(dev);
    rc = as2xxxx_priv_reg_wr_mmd(&pdata->priv, GET_MMD_DEVADR(addr), GET_MMD_REGADR(addr), value);
    MEPA_EXIT(dev);

    return rc;
}

// ------------------------------------------------------------------------------------------------
// Public functions
// ------------------------------------------------------------------------------------------------

mepa_drivers_t mepa_as2xxxx_driver_init()
{
    mepa_drivers_t result;

    // Device ID and Mask
    m_driver.id = AS2XXXX_PHY_ID_VENDOR;
    m_driver.mask = AS2XXXX_PHY_ID_MASK;

    // Driver APIs
    m_driver.mepa_driver_delete = as2xxxx_delete;
    m_driver.mepa_driver_probe = as2xxxx_probe;
    m_driver.mepa_driver_reset = as2xxxx_reset;
    m_driver.mepa_driver_poll = as2xxxx_poll;
    m_driver.mepa_driver_event_enable_set = as2xxxx_event_enable_set;
    m_driver.mepa_driver_event_enable_get = as2xxxx_event_enable_get;
    m_driver.mepa_driver_event_poll = as2xxxx_event_poll;
    m_driver.mepa_driver_conf_set = as2xxxx_conf_set;
    m_driver.mepa_driver_conf_get = as2xxxx_conf_get;
    m_driver.mepa_driver_if_set = as2xxxx_if_set;
    m_driver.mepa_driver_if_get = as2xxxx_if_get;
    m_driver.mepa_driver_media_get = as2xxxx_media_get;
    m_driver.mepa_driver_phy_info_get = as2xxxx_info_get;
    m_driver.mepa_driver_loopback_set = as2xxxx_loopback_set;
    m_driver.mepa_driver_loopback_get = as2xxxx_loopback_get;
    m_driver.mepa_driver_capability = as2xxxx_capability;
    m_driver.mepa_driver_serdes_tx_conf_set = as2xxxx_serdes_tx_conf_set;
    m_driver.mepa_driver_chip_temp_get = as2xxxx_chip_temp_get;
    m_driver.mepa_driver_debug_info_dump = as2xxxx_debug_info;
    m_driver.mepa_driver_clause22_read = as2xxxx_reg_read;
    m_driver.mepa_driver_clause22_write = as2xxxx_reg_write;
    m_driver.mepa_driver_clause45_read = as2xxxx_mmd_reg_read;
    m_driver.mepa_driver_clause45_write = as2xxxx_mmd_reg_write;

    result.phy_drv = &m_driver;
    result.count = 1U;

    return result;
}
