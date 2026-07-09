// Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#include <ctype.h>
#include <microchip/ethernet/board/api.h>
#include <unistd.h>
#include "../meba_aux.h"

/* Get array size */
#define VTSS_ARRSZ(t) (sizeof(t) / sizeof(t[0]))

typedef struct {
    mesa_port_no_t port_no;
    mesa_inst_t    inst;
    meba_inst_t    meba_inst;
} sfp_data_t;

static mesa_rc dev_delete(meba_sfp_device_t *dev)
{
    sfp_data_t *data;

    if (!dev) {
        return MESA_RC_OK;
    }

    data = (sfp_data_t *)dev->data;
    free(data);
    free(dev);
    dev = NULL;

    return MESA_RC_OK;
}

static mesa_rc dev_reset(meba_sfp_device_t *dev) { return MESA_RC_OK; }

static mesa_rc dev_poll(meba_sfp_device_t *dev, meba_sfp_driver_status_t *status)
{
    sfp_data_t        *data = (sfp_data_t *)(dev->data);
    mesa_port_status_t mesa_status = {};

    mesa_rc rc = meba_port_status_get(data->meba_inst, data->port_no, &mesa_status);
    if (rc != MESA_RC_OK)
        return rc;

    status->link = !mesa_status.link_down && mesa_status.link;
    status->speed = mesa_status.speed;
    status->fdx = mesa_status.fdx;
    status->aneg = mesa_status.aneg;

    if (data->meba_inst->api.meba_sfp_status_get)
        rc = data->meba_inst->api.meba_sfp_status_get(data->meba_inst, data->port_no, &dev->sfp);
    if (rc != MESA_RC_OK)
        return rc;

    // fill up status
    status->los = dev->sfp.los;

    return MESA_RC_OK;
}

static mesa_rc cisco_sgmii_phy_read(meba_inst_t    meba_inst,
                                    mesa_port_no_t port_no,
                                    uint8_t        addr,
                                    uint16_t      *value)
{
    uint8_t data[2];

    mesa_rc rc =
        meba_inst->api.meba_sfp_i2c_xfer(meba_inst, port_no, false, 0x56, addr, data, 2, true);
    if (rc != MESA_RC_OK)
        return MESA_RC_ERROR;

    *value = ((data[0] << 8) | data[1]);
    return MESA_RC_OK;
}

// Write PHY register via I2C
static mesa_rc cisco_sgmii_phy_write(meba_inst_t    meba_inst,
                                     mesa_port_no_t port_no,
                                     uint8_t        addr,
                                     uint16_t       value)
{
    uint8_t data[2];

    data[0] = ((value >> 8) & 0xff);
    data[1] = (value & 0xff);
    return meba_inst->api.meba_sfp_i2c_xfer(meba_inst, port_no, true, 0x56, addr, data, 2, true);
}

#define VTSS_MSLEEP(m) usleep((m) * 1000)

/* MDIO register addresses (IEEE 802.3 Clause 22 + Marvell vendor extensions). */
#define MII_REG_BMCR             0  /* Basic Mode Control Register (IEEE) */
#define MII_REG_PHY_ID1          2  /* PHY identifier 1 (high 16 bits of OUI) */
#define MII_REG_PHY_ID2          3  /* PHY identifier 2 (low OUI + model + rev) */
#define MII_REG_ANAR             4  /* Auto-Negotiation Advertisement Register */
#define MII_REG_GBCR             9  /* 1000BASE-T Control Register */
#define MARVELL_REG_EXT_PHY_CTRL 27 /* Marvell Extended PHY Specific Control */

/* Marvell PHY ID signature for the 88E1xxx family used in CuSFPs. */
#define MARVELL_PHY_ID1         0x0141
#define MARVELL_PHY_ID2_MASK    0xFFF0
#define MARVELL_PHY_ID2_88E1xxx 0x0CC0

/* SGMII reconfigure values applied to a Marvell 88E1xxx CuSFP to switch its
 * host-side interface from whatever was strapped to SGMII-with-clock. */
#define MARVELL_EXT_CTRL_SGMII     0x9084 /* HWCFG_MODE bits[2:0]=100 + soft reset */
#define MARVELL_GBCR_ADV_1000T_ALL 0x0F00 /* advertise 1000BASE-T FDX+HDX, manual master */
#define MARVELL_BMCR_RESET_1G_FDX  0x8140 /* soft reset + 1G + FDX, aneg off */
#define MARVELL_ANAR_ADV_10_100    0x0DE1 /* advertise 10/100 FDX+HDX, IEEE 802.3 selector */
#define MARVELL_BMCR_RESET_1G_ANEG 0x9140 /* soft reset + 1G + FDX + aneg enabled */

static mesa_bool_t cisco_sgmii_set(meba_inst_t meba_inst, mesa_port_no_t port_no)
{
    uint16_t    reg2 = 0, reg3 = 0;
    mesa_bool_t configure_phy;

    // Read PHY ID registers
    mesa_bool_t phy_present = false;
    for (int i = 0; i < 10; i++) {
        if (cisco_sgmii_phy_read(meba_inst, port_no, MII_REG_PHY_ID1, &reg2) == MESA_RC_OK &&
            cisco_sgmii_phy_read(meba_inst, port_no, MII_REG_PHY_ID2, &reg3) == MESA_RC_OK &&
            reg2 != 0xFFFF && reg3 != 0xFFFF) {
            phy_present = true;
            break;
        }
        VTSS_MSLEEP(50); // Wait while the SFP module wakes up
    }

    if (!phy_present) {
        return true; // Not a CuSFP, or I2C bridge unreachable.
    }

    /* Only reconfigure PHYs whose ID we recognize. Marvell 88E1xxx is
     * the typical CuSFP silicon; the all-zero ID is reported by the
     * Adtran/Methode Elec. SP7041-ADT. Any other ID is left alone. */
    configure_phy =
        (reg2 == MARVELL_PHY_ID1 && (reg3 & MARVELL_PHY_ID2_MASK) == MARVELL_PHY_ID2_88E1xxx) ||
        (reg2 == 0x0000 && reg3 == 0x0000);
    if (!configure_phy) {
        return true;
    }

    /* Write the SGMII-mode init sequence. Each step is documented in
     * the table; iteration retries the whole sequence if any write fails. */
    static const struct {
        uint8_t  reg;
        uint16_t value;
    } init_seq[] = {
        {MARVELL_REG_EXT_PHY_CTRL, MARVELL_EXT_CTRL_SGMII    },
        {MII_REG_GBCR,             MARVELL_GBCR_ADV_1000T_ALL},
        {MII_REG_BMCR,             MARVELL_BMCR_RESET_1G_FDX },
        {MII_REG_ANAR,             MARVELL_ANAR_ADV_10_100   },
        {MII_REG_BMCR,             MARVELL_BMCR_RESET_1G_ANEG},
    };

    for (int attempt = 0; attempt < 10; attempt++) {
        mesa_bool_t ok = true;
        for (size_t j = 0; j < VTSS_ARRSZ(init_seq); j++) {
            if (cisco_sgmii_phy_write(meba_inst, port_no, init_seq[j].reg, init_seq[j].value) !=
                MESA_RC_OK) {
                ok = false;
                break;
            }
        }
        if (ok) {
            return true;
        }
        VTSS_MSLEEP(50); // Wait while the SFP module wakes up
    }
    return false;
}

static mesa_rc cisco_sgmii_conf_set(meba_sfp_device_t *dev, const meba_sfp_driver_conf_t *conf)
{
    sfp_data_t *data = (sfp_data_t *)(dev->data);

    // Make sure that clause-37 aneg is disabled
    mesa_port_clause_37_control_t ctrl = {};
    mesa_port_clause_37_control_set(data->inst, data->port_no, &ctrl);

    if (conf->admin.enable && !cisco_sgmii_set(data->meba_inst, data->port_no))
        return MESA_RC_ERROR;

    return MESA_RC_OK;
}

static mesa_rc cisco_sgmii_if_get(meba_sfp_device_t     *dev,
                                  mesa_port_speed_t      speed,
                                  mesa_port_interface_t *mac_if)
{
    switch (speed) {
    case MESA_SPEED_10M:
    case MESA_SPEED_100M:
    case MESA_SPEED_1G:
    case MESA_SPEED_AUTO: *mac_if = MESA_PORT_INTERFACE_SGMII_CISCO; return MESA_RC_OK;
    default:
        // Set the same interface, even the user sets wrong speed,
        // so we will have one interface.
        *mac_if = MESA_PORT_INTERFACE_SGMII_CISCO;
        return MESA_RC_ERROR;
    }
}

static mesa_rc cisco_sgmii_2g5_if_get(meba_sfp_device_t     *dev,
                                      mesa_port_speed_t      speed,
                                      mesa_port_interface_t *mac_if)
{
    switch (speed) {
    case MESA_SPEED_10M:
    case MESA_SPEED_100M:
    case MESA_SPEED_1G:   *mac_if = MESA_PORT_INTERFACE_SGMII_CISCO; return MESA_RC_OK;
    default:
        // Default to the highest speed
        *mac_if = MESA_PORT_INTERFACE_SERDES;
        return MESA_RC_OK;
    }
}

static meba_sfp_device_t *dev_probe(meba_sfp_driver_t               *drv,
                                    const meba_sfp_driver_address_t *mode,
                                    const meba_sfp_device_info_t    *device_info)
{
    meba_sfp_device_t *device;
    sfp_data_t        *data;

    if (drv == NULL || mode == NULL || mode->mode != mscc_sfp_driver_address_mode ||
        device_info == NULL) {
        return NULL;
    }

    if ((device = (meba_sfp_device_t *)calloc(1, sizeof(meba_sfp_device_t))) == NULL) {
        goto out_device;
    }

    if ((data = (sfp_data_t *)calloc(1, sizeof(sfp_data_t))) == NULL) {
        goto out_data;
    }

    device->drv = drv;
    data->inst = mode->val.mscc_address.inst;
    data->meba_inst = mode->val.mscc_address.meba_inst;
    data->port_no = mode->val.mscc_address.port_no;
    device->data = data;
    device->info = *device_info;
    device->sfp.present = true;
    device->sfp.los = false;
    device->sfp.tx_fault = false;

    return device;

out_data:
    free(device);

out_device:
    return NULL;
}

static mesa_rc serdes_if_get(meba_sfp_device_t     *dev,
                             mesa_port_speed_t      speed,
                             mesa_port_interface_t *mac_if)
{
    switch (speed) {
    case MESA_SPEED_100M: *mac_if = MESA_PORT_INTERFACE_100FX; return MESA_RC_OK;
    case MESA_SPEED_AUTO:
    case MESA_SPEED_1G:   *mac_if = MESA_PORT_INTERFACE_SERDES; return MESA_RC_OK;
    default:              *mac_if = MESA_PORT_INTERFACE_SERDES; return MESA_RC_ERROR;
    }
}

static mesa_rc serdes_conf_set(meba_sfp_device_t *dev, const meba_sfp_driver_conf_t *conf)
{
    sfp_data_t                   *data = (sfp_data_t *)(dev->data);
    mesa_port_clause_37_control_t control = {};
    mesa_port_clause_37_adv_t    *adv = NULL;

    control.enable = conf->speed == MESA_SPEED_AUTO ? true : false;
    adv = &control.advertisement;
    adv->fdx = true;
    adv->hdx = false;
    adv->symmetric_pause = conf->flow_control;
    adv->asymmetric_pause = conf->flow_control;
    adv->remote_fault =
        conf->admin.enable ? MESA_PORT_CLAUSE_37_RF_LINK_OK : MESA_PORT_CLAUSE_37_RF_OFFLINE;
    adv->acknowledge = 0;
    adv->next_page = 0;

    mesa_port_clause_37_control_t ctrl_now = {};
    mesa_port_clause_37_control_get(data->inst, data->port_no, &ctrl_now);
    if (memcmp(&control, &ctrl_now, sizeof(control)) != 0) {
        return mesa_port_clause_37_control_set(data->inst, data->port_no, &control);
    }

    return MESA_RC_OK;
}

static mesa_rc clause_37_disable(meba_sfp_device_t *dev, const meba_sfp_driver_conf_t *conf)
{
    sfp_data_t                   *data = (sfp_data_t *)(dev->data);
    mesa_port_clause_37_control_t ctrl;

    mesa_port_clause_37_control_get(data->inst, data->port_no, &ctrl);
    if (ctrl.enable) {
        ctrl.enable = false;
        return mesa_port_clause_37_control_set(data->inst, data->port_no, &ctrl);
    }

    return MESA_RC_OK;
}

static mesa_rc fx_if_get(meba_sfp_device_t     *dev,
                         mesa_port_speed_t      speed,
                         mesa_port_interface_t *mac_if)
{
    *mac_if = MESA_PORT_INTERFACE_100FX;
    switch (speed) {
    case MESA_SPEED_AUTO:
    case MESA_SPEED_100M: return MESA_RC_OK;
    default:              return MESA_RC_ERROR;
    }
}

static mesa_rc sfi_if_get(meba_sfp_device_t     *dev,
                          mesa_port_speed_t      speed,
                          mesa_port_interface_t *mac_if)
{
    switch (speed) {
    case MESA_SPEED_AUTO:
    case MESA_SPEED_1G:    *mac_if = MESA_PORT_INTERFACE_SERDES; return MESA_RC_OK;
    case MESA_SPEED_2500M: *mac_if = MESA_PORT_INTERFACE_VAUI; return MESA_RC_OK;
    case MESA_SPEED_100M:  *mac_if = MESA_PORT_INTERFACE_100FX; return MESA_RC_OK;
    case MESA_SPEED_5G:
    case MESA_SPEED_10G:
    case MESA_SPEED_25G:   *mac_if = MESA_PORT_INTERFACE_SFI; return MESA_RC_OK;
    default:               *mac_if = MESA_PORT_INTERFACE_SFI; return MESA_RC_ERROR;
    }
}

static mesa_rc tr_2g5_if_get(meba_sfp_device_t     *dev,
                             mesa_port_speed_t      speed,
                             mesa_port_interface_t *mac_if)
{
    switch (speed) {
    case MESA_SPEED_AUTO:
    case MESA_SPEED_1G:    *mac_if = MESA_PORT_INTERFACE_SERDES; return MESA_RC_OK;
    case MESA_SPEED_100M:  *mac_if = MESA_PORT_INTERFACE_100FX; return MESA_RC_OK;
    case MESA_SPEED_2500M: *mac_if = MESA_PORT_INTERFACE_VAUI; return MESA_RC_OK;
    default:               *mac_if = MESA_PORT_INTERFACE_VAUI; return MESA_RC_ERROR;
    }
}

static mesa_rc tr_10gbaset_if_get(meba_sfp_device_t     *dev,
                                  mesa_port_speed_t      speed,
                                  mesa_port_interface_t *mac_if)
{
    *mac_if = MESA_PORT_INTERFACE_USXGMII;
    return MESA_RC_OK;
}

static mesa_rc sfi_mt_none_get(meba_sfp_device_t *dev, mesa_sd10g_media_type_t *mt)
{
    *mt = MESA_SD10G_MEDIA_PR_NONE;
    return MESA_RC_OK;
}

static mesa_rc sfi_mt_get(meba_sfp_device_t *dev, mesa_sd10g_media_type_t *mt)
{
    *mt = MESA_SD10G_MEDIA_SR;
    return MESA_RC_OK;
}

static mesa_rc sfi_mt_zr_get(meba_sfp_device_t *dev, mesa_sd10g_media_type_t *mt)
{

    *mt = MESA_SD10G_MEDIA_ZR;
    return MESA_RC_OK;
}

static mesa_rc sfi_mt_bp_get(meba_sfp_device_t *dev, mesa_sd10g_media_type_t *mt)
{
    *mt = MESA_SD10G_MEDIA_BP;
    return MESA_RC_OK;
}

static mesa_rc sfi_mt_dac_get(meba_sfp_device_t *dev, mesa_sd10g_media_type_t *mt)
{
    *mt = MESA_SD10G_MEDIA_DAC;
    return MESA_RC_OK;
}

static mesa_rc tr_1000_sx_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_1000BASE_SX;
    return MESA_RC_OK;
}

static mesa_rc tr_1000_lx_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_1000BASE_LX;
    return MESA_RC_OK;
}

static mesa_rc tr_1000_zx_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_1000BASE_ZX;
    return MESA_RC_OK;
}

static mesa_rc tr_1000_cx_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_1000BASE_CX;
    return MESA_RC_OK;
}

static mesa_rc tr_1000_t_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_1000BASE_T;
    return MESA_RC_OK;
}

static mesa_rc tr_1000_x_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_1000BASE_X;
    return MESA_RC_OK;
}

static mesa_rc tr_2g5_get_t(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_2G5_T;
    return MESA_RC_OK;
}

static mesa_rc tr_2g5_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_2G5;
    return MESA_RC_OK;
}

static mesa_rc tr_5g_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_5G;
    return MESA_RC_OK;
}

static mesa_rc tr_10g_baset_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_10GBASE_T;
    return MESA_RC_OK;
}

static mesa_rc tr_10g_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_10G;
    return MESA_RC_OK;
}

static mesa_rc tr_10g_dac_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_10G_DAC;
    return MESA_RC_OK;
}

static mesa_rc tr_10g_sr_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_10G_SR;
    return MESA_RC_OK;
}

static mesa_rc tr_10g_lr_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_10G_LR;
    return MESA_RC_OK;
}

static mesa_rc tr_10g_er_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_10G_ER;
    return MESA_RC_OK;
}

static mesa_rc tr_10g_lrm_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_10G_LRM;
    return MESA_RC_OK;
}

static mesa_rc tr_25g_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_25G;
    return MESA_RC_OK;
}

static mesa_rc tr_25g_cr_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_25G_DAC;
    return MESA_RC_OK;
}

static mesa_rc tr_25g_sr_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_25G_SR;
    return MESA_RC_OK;
}

static mesa_rc tr_25g_lr_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_25G_LR;
    return MESA_RC_OK;
}

static mesa_rc tr_25g_er_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_25G_ER;
    return MESA_RC_OK;
}

static mesa_rc tr_25g_lrm_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_25G_LRM;
    return MESA_RC_OK;
}

static mesa_rc tr_100_fx_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_100FX;
    return MESA_RC_OK;
}

static mesa_rc tr_100_lx_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_100BASE_LX;
    return MESA_RC_OK;
}

static mesa_rc tr_100_zx_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_100BASE_ZX;
    return MESA_RC_OK;
}

static mesa_rc tr_100_sx_get(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr)
{
    *tr = MEBA_SFP_TRANSRECEIVER_100BASE_SX;
    return MESA_RC_OK;
}

meba_sfp_drivers_t meba_cisco_driver_init()
{
    static meba_sfp_driver_t cisco_drivers[] = {
        {
         .product_name = "SP7041-R",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = cisco_sgmii_conf_set,
         .meba_sfp_driver_if_get = cisco_sgmii_if_get,
         .meba_sfp_driver_mt_get = NULL,
         .meba_sfp_driver_tr_get = tr_1000_t_get,
         .meba_sfp_driver_probe = dev_probe,
         },
        {
         .product_name = "AFBR-5715APZ-CS4",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = serdes_conf_set,
         .meba_sfp_driver_if_get = serdes_if_get,
         .meba_sfp_driver_mt_get = sfi_mt_get,
         .meba_sfp_driver_tr_get = tr_1000_sx_get,
         .meba_sfp_driver_probe = dev_probe,
         },
        {
         .product_name = "FTLF8519P2BCL-C4",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = serdes_conf_set,
         .meba_sfp_driver_if_get = serdes_if_get,
         .meba_sfp_driver_mt_get = sfi_mt_get,
         .meba_sfp_driver_tr_get = tr_1000_sx_get,
         .meba_sfp_driver_probe = dev_probe,
         },
        {
         .product_name = "SPP5100SR-C5",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = serdes_conf_set,
         .meba_sfp_driver_if_get = sfi_if_get,
         .meba_sfp_driver_mt_get = sfi_mt_get,
         .meba_sfp_driver_tr_get = tr_10g_sr_get,
         .meba_sfp_driver_probe = dev_probe,
         },
        {
         .product_name = "SFBR-7702SDZ-CS5",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = serdes_conf_set,
         .meba_sfp_driver_if_get = sfi_if_get,
         .meba_sfp_driver_mt_get = sfi_mt_get,
         .meba_sfp_driver_tr_get = tr_10g_sr_get,
         .meba_sfp_driver_probe = dev_probe,
         }
    };

    meba_sfp_drivers_t result;
    result.sfp_drv = cisco_drivers;
    result.count = VTSS_ARRSZ(cisco_drivers);

    return result;
}

meba_sfp_drivers_t meba_axcen_driver_init()
{
    static meba_sfp_driver_t axcen_drivers[] = {
        {
         .product_name = "AXFE-1314-0521",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = serdes_conf_set,
         .meba_sfp_driver_if_get = fx_if_get,
         .meba_sfp_driver_mt_get = NULL,
         .meba_sfp_driver_tr_get = tr_100_fx_get,
         .meba_sfp_driver_probe = dev_probe,
         },
        {
         .product_name = "AXGE-5854-0511",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = serdes_conf_set,
         .meba_sfp_driver_if_get = serdes_if_get,
         .meba_sfp_driver_mt_get = sfi_mt_get,
         .meba_sfp_driver_tr_get = tr_1000_sx_get,
         .meba_sfp_driver_probe = dev_probe,
         },
        {
         .product_name = "AXGT-R1T4-05I1",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = cisco_sgmii_conf_set,
         .meba_sfp_driver_if_get = cisco_sgmii_if_get,
         .meba_sfp_driver_mt_get = NULL,
         .meba_sfp_driver_tr_get = tr_1000_t_get,
         .meba_sfp_driver_probe = dev_probe,
         },
        {
         .product_name = "AXFE-1314-0531",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = serdes_conf_set,
         .meba_sfp_driver_if_get = fx_if_get,
         .meba_sfp_driver_mt_get = NULL,
         .meba_sfp_driver_tr_get = tr_100_lx_get,
         .meba_sfp_driver_probe = dev_probe,
         },
        {
         .product_name = "AXXE-5886-05B1",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = serdes_conf_set,
         .meba_sfp_driver_if_get = sfi_if_get,
         .meba_sfp_driver_mt_get = sfi_mt_get,
         .meba_sfp_driver_tr_get = tr_10g_sr_get,
         .meba_sfp_driver_probe = dev_probe,
         }
    };

    meba_sfp_drivers_t result;
    result.sfp_drv = axcen_drivers;
    result.count = VTSS_ARRSZ(axcen_drivers);

    return result;
}

meba_sfp_drivers_t meba_finisar_driver_init()
{
    static meba_sfp_driver_t finisar_drivers[] = {
        {
         .product_name = "FTLX8571D3BCL",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = serdes_conf_set,
         .meba_sfp_driver_if_get = sfi_if_get,
         .meba_sfp_driver_mt_get = sfi_mt_get,
         .meba_sfp_driver_tr_get = tr_10g_sr_get,
         .meba_sfp_driver_probe = dev_probe,
         },
        {
         .product_name = "SPT-SFP+C1",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = serdes_conf_set,
         .meba_sfp_driver_if_get = sfi_if_get,
         .meba_sfp_driver_mt_get = sfi_mt_dac_get,
         .meba_sfp_driver_tr_get = tr_10g_dac_get,
         .meba_sfp_driver_probe = dev_probe,
         },
        {
         // This one reports itself as an optical SFP, where in fact it is a
            // DAC.
            .product_name = "SPT-SFP+C2",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = serdes_conf_set,
         .meba_sfp_driver_if_get = sfi_if_get,
         .meba_sfp_driver_mt_get = sfi_mt_dac_get,
         .meba_sfp_driver_tr_get = tr_10g_dac_get,
         .meba_sfp_driver_probe = dev_probe,
         }
    };

    meba_sfp_drivers_t result;
    result.sfp_drv = finisar_drivers;
    result.count = VTSS_ARRSZ(finisar_drivers);

    return result;
}

meba_sfp_drivers_t meba_fs_driver_init()
{
    static meba_sfp_driver_t fs_drivers[] = {
        {
         .product_name = "SFP-2.5G-T",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = cisco_sgmii_conf_set,
         .meba_sfp_driver_if_get = cisco_sgmii_2g5_if_get,
         .meba_sfp_driver_mt_get = NULL,
         .meba_sfp_driver_tr_get = tr_2g5_get_t,
         .meba_sfp_driver_probe = dev_probe,
         },
        {
         .product_name = "SFP-2.5G-T-I",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = cisco_sgmii_conf_set,
         .meba_sfp_driver_if_get = cisco_sgmii_2g5_if_get,
         .meba_sfp_driver_mt_get = NULL,
         .meba_sfp_driver_tr_get = tr_2g5_get_t,
         .meba_sfp_driver_probe = dev_probe,
         },
    };

    meba_sfp_drivers_t result;
    result.sfp_drv = fs_drivers;
    result.count = VTSS_ARRSZ(fs_drivers);

    return result;
}

meba_sfp_drivers_t meba_hp_driver_init()
{
    static meba_sfp_driver_t hp_drivers[] = {
        {
         .product_name = "WST-SEAMCV-A6H",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = cisco_sgmii_conf_set,
         .meba_sfp_driver_if_get = cisco_sgmii_if_get,
         .meba_sfp_driver_mt_get = NULL,
         .meba_sfp_driver_tr_get = tr_1000_t_get,
         .meba_sfp_driver_probe = dev_probe,
         },
        {
         .product_name = "SP7041-HPB-3-R",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = cisco_sgmii_conf_set,
         .meba_sfp_driver_if_get = cisco_sgmii_if_get,
         .meba_sfp_driver_mt_get = NULL,
         .meba_sfp_driver_tr_get = tr_1000_t_get,
         .meba_sfp_driver_probe = dev_probe,
         },
        {
         .product_name = "TRF5326ANLB404",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = serdes_conf_set,
         .meba_sfp_driver_if_get = fx_if_get,
         .meba_sfp_driver_mt_get = NULL,
         .meba_sfp_driver_tr_get = tr_100_fx_get,
         .meba_sfp_driver_probe = dev_probe,
         },
        {
         .product_name = "RTXM191-400",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = serdes_conf_set,
         .meba_sfp_driver_if_get = serdes_if_get,
         .meba_sfp_driver_mt_get = sfi_mt_zr_get,
         .meba_sfp_driver_tr_get = tr_1000_lx_get,
         .meba_sfp_driver_probe = dev_probe,
         }
    };

    meba_sfp_drivers_t result;
    result.sfp_drv = hp_drivers;
    result.count = VTSS_ARRSZ(hp_drivers);

    return result;
}

meba_sfp_drivers_t meba_oem_driver_init()
{
    static meba_sfp_driver_t oem_drivers[] = {
        {
         .product_name = "SFP-T",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = cisco_sgmii_conf_set,
         .meba_sfp_driver_if_get = cisco_sgmii_if_get,
         .meba_sfp_driver_mt_get = NULL,
         .meba_sfp_driver_tr_get = tr_1000_t_get,
         .meba_sfp_driver_probe = dev_probe,
         },
        {
         // Fiberworks 10GBaseT/USXGMII
            .product_name = "10GB-SFP-SR",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = clause_37_disable,
         .meba_sfp_driver_if_get = tr_10gbaset_if_get,
         .meba_sfp_driver_mt_get = sfi_mt_get,
         .meba_sfp_driver_tr_get = tr_10g_baset_get,
         .meba_sfp_driver_probe = dev_probe,
         },
        {
         // FS 10GBaseT/USXGMII
            .product_name = "SFP-10GM-T-30",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = clause_37_disable,
         .meba_sfp_driver_if_get = tr_10gbaset_if_get,
         .meba_sfp_driver_mt_get = sfi_mt_get,
         .meba_sfp_driver_tr_get = tr_10g_baset_get,
         .meba_sfp_driver_probe = dev_probe,
         }
    };

    meba_sfp_drivers_t result;
    result.sfp_drv = oem_drivers;
    result.count = VTSS_ARRSZ(oem_drivers);

    return result;
}

meba_sfp_drivers_t meba_wavesplitter_driver_init()
{
    static meba_sfp_driver_t wavesplitter_drivers[] = {
        {
         .product_name = "WST-S3CCIM-401H",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = serdes_conf_set,
         .meba_sfp_driver_if_get = serdes_if_get,
         .meba_sfp_driver_mt_get = sfi_mt_zr_get,
         .meba_sfp_driver_tr_get = tr_1000_lx_get,
         .meba_sfp_driver_probe = dev_probe,
         }
    };

    meba_sfp_drivers_t result;
    result.sfp_drv = wavesplitter_drivers;
    result.count = VTSS_ARRSZ(wavesplitter_drivers);

    return result;
}

meba_sfp_drivers_t meba_d_link_driver_init()
{
    static meba_sfp_driver_t d_link_drivers[] = {
        {
         .product_name = "DEM-311GT",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = serdes_conf_set,
         .meba_sfp_driver_if_get = serdes_if_get,
         .meba_sfp_driver_mt_get = sfi_mt_get,
         .meba_sfp_driver_tr_get = tr_1000_sx_get,
         .meba_sfp_driver_probe = dev_probe,
         }
    };

    meba_sfp_drivers_t result;
    result.sfp_drv = d_link_drivers;
    result.count = VTSS_ARRSZ(d_link_drivers);

    return result;
}

meba_sfp_drivers_t meba_avago_driver_init()
{
    static meba_sfp_driver_t avago_drivers[] = {
        {
         .product_name = "AFBR-5710LZ",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = serdes_conf_set,
         .meba_sfp_driver_if_get = serdes_if_get,
         .meba_sfp_driver_mt_get = sfi_mt_get,
         .meba_sfp_driver_tr_get = tr_1000_sx_get,
         .meba_sfp_driver_probe = dev_probe,
         }
    };

    meba_sfp_drivers_t result;
    result.sfp_drv = avago_drivers;
    result.count = VTSS_ARRSZ(avago_drivers);

    return result;
}

meba_sfp_drivers_t meba_excom_driver_init()
{
    static meba_sfp_driver_t excom_drivers[] = {
        {
         .product_name = "SFP-SX-M1002",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = serdes_conf_set,
         .meba_sfp_driver_if_get = fx_if_get,
         .meba_sfp_driver_mt_get = NULL,
         .meba_sfp_driver_tr_get = tr_100_sx_get,
         .meba_sfp_driver_probe = dev_probe,
         },
        {
         .product_name = "SFP-LH-S1010",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = serdes_conf_set,
         .meba_sfp_driver_if_get = fx_if_get,
         .meba_sfp_driver_mt_get = NULL,
         .meba_sfp_driver_tr_get = tr_100_lx_get,
         .meba_sfp_driver_probe = dev_probe,
         },
        {
         .product_name = "SFP+10G-LR10",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = serdes_conf_set,
         .meba_sfp_driver_if_get = sfi_if_get,
         .meba_sfp_driver_mt_get = sfi_mt_zr_get,
         .meba_sfp_driver_tr_get = tr_10g_lr_get,
         .meba_sfp_driver_probe = dev_probe,
         }
    };

    meba_sfp_drivers_t result;
    result.sfp_drv = excom_drivers;
    result.count = VTSS_ARRSZ(excom_drivers);

    return result;
}

meba_sfp_drivers_t meba_mac_to_mac_driver_init()
{
    static meba_sfp_driver_t mac_to_mac_drivers[] = {
        {
         .product_name = "MAC-to-MAC-1G",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = serdes_conf_set,
         .meba_sfp_driver_if_get = serdes_if_get,
         .meba_sfp_driver_mt_get = sfi_mt_get,
         .meba_sfp_driver_tr_get = tr_1000_sx_get,
         .meba_sfp_driver_probe = dev_probe,
         },
        {
         .product_name = "MAC-to-MAC-2.5G",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = serdes_conf_set,
         .meba_sfp_driver_if_get = sfi_if_get,
         .meba_sfp_driver_mt_get = NULL,
         .meba_sfp_driver_tr_get = tr_2g5_get,
         .meba_sfp_driver_probe = dev_probe,
         },
        {
         .product_name = "MAC-to-MAC-10G",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = serdes_conf_set,
         .meba_sfp_driver_if_get = sfi_if_get,
         .meba_sfp_driver_mt_get = sfi_mt_bp_get,          // Backplane
            .meba_sfp_driver_tr_get = tr_10g_sr_get,
         .meba_sfp_driver_probe = dev_probe,
         },
        {
         .product_name = "MAC-to-MAC-25G",
         .meba_sfp_driver_delete = dev_delete,
         .meba_sfp_driver_reset = dev_reset,
         .meba_sfp_driver_poll = dev_poll,
         .meba_sfp_driver_conf_set = serdes_conf_set,
         .meba_sfp_driver_if_get = sfi_if_get,
         .meba_sfp_driver_mt_get = sfi_mt_bp_get,            // Backplane
            .meba_sfp_driver_tr_get = tr_25g_cr_get,
         .meba_sfp_driver_probe = dev_probe,
         }
    };

    meba_sfp_drivers_t result;
    result.sfp_drv = mac_to_mac_drivers;
    result.count = VTSS_ARRSZ(mac_to_mac_drivers);

    return result;
}

typedef mesa_rc (*tr_func_t)(meba_sfp_device_t *dev, meba_sfp_transreceiver_t *tr);
typedef mesa_rc (*if_func_t)(meba_sfp_device_t     *dev,
                             mesa_port_speed_t      speed,
                             mesa_port_interface_t *mac_if);
typedef mesa_rc (*mt_func_t)(meba_sfp_device_t *dev, mesa_sd10g_media_type_t *mt);
typedef mesa_rc (*conf_func_t)(meba_sfp_device_t *dev, const meba_sfp_driver_conf_t *conf);

#define SFP_MSA_1000BASE_SX 0x01
#define SFP_MSA_1000BASE_LX 0x02
#define SFP_MSA_1000BASE_CX 0x04
#define SFP_MSA_1000BASE_T  0x08
#define SFP_MSA_100BASE_LX  0x10
#define SFP_MSA_100BASE_FX  0x20
#define SFP_MSA_BASE_BX10   0x40
#define SFP_MSA_BASE_PX     0x80

#define SFP_MSA_10GBASE_SR  0x10
#define SFP_MSA_10GBASE_LR  0x20
#define SFP_MSA_10GBASE_LRM 0x40
#define SFP_MSA_10GBASE_ER  0x80
#define SFM_MSA_10G_ETHER   0xF0

#define SFP_MSA_SFP_PLUS_PASSIVE 0x04
#define SFP_MSA_SFP_PLUS_ACTIVE  0x08
#define SFP_MSA_SFP_PLUS_CABLE   (SFP_MSA_SFP_PLUS_PASSIVE | SFP_MSA_SFP_PLUS_ACTIVE)

#define SFP_MSA_25GBASE_SR    0x2
#define SFP_MSA_25GBASE_LR    0x3
#define SFP_MSA_25GBASE_ER    0x4
#define SFP_MSA_25GBASE_CR_FC 0xC
#define SFP_MSA_25GBASE_CR    0xD

// Nominal bit-rate codes (byte 12, units of 100 MBd) used as decision thresholds
#define SFP_BR_100M    1   // 100 MBd
#define SFP_BR_FE      2   // 200 MBd  (Fast Ethernet variant)
#define SFP_BR_1G      10  // 1.0 GBd
#define SFP_BR_1200MBD 12  // 1.2 GBd  (ZX-88km low-rate variant)
#define SFP_BR_1300MBD 13  // 1.3 GBd  (1G ZX-80km, 1000BASE-T CuSFP)
#define SFP_BR_2G5     25  // 2.5 GBd
#define SFP_BR_5G      50  // 5 GBd
#define SFP_BR_10G     100 // 10 GBd
#define SFP_BR_25G     250 // 25 GBd

// Length-byte sentinels
#define SFP_LEN_UNSPECIFIED  0x00 // Length field unspecified / not applicable
#define SFP_LEN_SMF_KM_ZX_80 0x50 // 80 km in byte 14 (SMF, units of km)
#define SFP_LEN_SMF_KM_ZX_88 0x58 // 88 km in byte 14
#define SFP_LEN_SMF_100M_MAX 0xFF // Saturated marker in byte 15 (>= 25.5 km)
#define SFP_LEN_OM_2KM       0xC8 // 2000 m in OM length byte (units of 10 m)

typedef struct {
    // Vendor strings
    char vendor_name[20]; // [20..35]
    char vendor_pn[20];   // [40..55]
    char vendor_rev[6];   // [56..59]
    char vendor_sn[20];   // [68..83]
    char date_code[9];    // [84..91]

    // SFF-8472 A0h byte fields used by the classifier.
    uint8_t identifier;     // [0]   0x03 = SFP/SFP+
    uint8_t connector;      // [2]
    uint8_t eth_10g_compl;  // [3]   masked to 10G/IB/ESCON bits
    uint8_t eth_compl;      // [6]   Ethernet compliance codes
    uint8_t cable_tech;     // [8]
    uint8_t nominal_br;     // [12]  units of 100 MBd
    uint8_t len_smf_km;     // [14]
    uint8_t len_smf_100m;   // [15]
    uint8_t len_om2;        // [16]
    uint8_t len_om1;        // [17]
    uint8_t len_om4_or_dac; // [18]
    uint8_t ext_compl;      // [36]  SFF-8024 extended compliance
    uint8_t dmi_type;       // [65]

    // Derived flags
    mesa_bool_t los_implemented; // (dmi_type & 0x02) != 0
} sfp_rom_t;

static tr_func_t tr_func_get(const sfp_rom_t *const rom)
{
    const uint8_t eth_10g = rom->eth_10g_compl;
    const uint8_t eth = rom->eth_compl;
    const uint8_t tech = rom->cable_tech;
    const uint8_t speed = rom->nominal_br;
    const uint8_t eth_25g = rom->ext_compl;
    const uint8_t len_smf_km = rom->len_smf_km;
    const uint8_t len_smf_100m = rom->len_smf_100m;
    const uint8_t len_om2 = rom->len_om2;
    const uint8_t len_om1 = rom->len_om1;
    const uint8_t len_om4_or_dac = rom->len_om4_or_dac;

    if (tech & SFP_MSA_SFP_PLUS_CABLE) {
        if (speed >= SFP_BR_10G && speed < SFP_BR_25G) {
            return tr_10g_dac_get;
        }

        if (speed >= SFP_BR_25G) {
            // DAC
            return tr_25g_cr_get;
        }
    }

    if (speed >= SFP_BR_10G && speed < SFP_BR_25G && eth_10g) {
        if (eth_10g & SFP_MSA_10GBASE_ER)
            return tr_10g_er_get;
        if (eth_10g & SFP_MSA_10GBASE_LRM)
            return tr_10g_lrm_get;
        if (eth_10g & SFP_MSA_10GBASE_LR)
            return tr_10g_lr_get;
        return tr_10g_sr_get;
    }

    // SFF-8024 Extended spec compliance reference (ROM address 36)
    if (speed >= SFP_BR_25G && eth_25g) {
        if (eth_25g == SFP_MSA_25GBASE_SR)
            return tr_25g_sr_get;
        if (eth_25g == SFP_MSA_25GBASE_LR)
            return tr_25g_lr_get;
        if (eth_25g == SFP_MSA_25GBASE_ER)
            return tr_25g_er_get;
        if (eth_25g == SFP_MSA_25GBASE_CR_FC || eth_25g == SFP_MSA_25GBASE_CR)
            return tr_25g_cr_get;
        return tr_25g_sr_get;
    }

    // Legacy support (for those 25G SFPs without rom[36] set).
    if (speed >= SFP_BR_25G && eth_10g) {
        if (eth_10g & SFP_MSA_10GBASE_ER)
            return tr_25g_er_get;
        if (eth_10g & SFP_MSA_10GBASE_LRM)
            return tr_25g_lrm_get;
        if (eth_10g & SFP_MSA_10GBASE_LR)
            return tr_25g_lr_get;
        return tr_25g_sr_get;
    }

    if (eth & SFP_MSA_1000BASE_SX)
        return (speed >= SFP_BR_2G5) ? tr_2g5_get : tr_1000_sx_get;
    if (eth & SFP_MSA_1000BASE_CX)
        return (speed >= SFP_BR_2G5) ? tr_2g5_get : tr_1000_cx_get;
    if (eth & SFP_MSA_1000BASE_T)
        return tr_1000_t_get;
    if (eth & SFP_MSA_1000BASE_LX) {
        if ((speed == SFP_BR_1300MBD && len_smf_km == SFP_LEN_SMF_KM_ZX_80 &&
             len_smf_100m == SFP_LEN_SMF_100M_MAX) ||
            (speed == SFP_BR_1200MBD && len_smf_km == SFP_LEN_SMF_KM_ZX_88 &&
             len_smf_100m == SFP_LEN_SMF_100M_MAX)) {
            return tr_1000_zx_get;
        } else {
            return (speed >= SFP_BR_2G5) ? tr_2g5_get : tr_1000_lx_get;
        }
    }

    if (eth & SFP_MSA_100BASE_FX)
        return tr_100_fx_get;
    if (eth & SFP_MSA_100BASE_LX)
        return tr_100_lx_get;

    if (eth == 0 || (eth & (SFP_MSA_BASE_BX10 | SFP_MSA_BASE_PX))) {
        if (speed == SFP_BR_100M && len_smf_km == SFP_LEN_SMF_KM_ZX_80 &&
            len_smf_100m == SFP_LEN_SMF_100M_MAX) {
            // This is a special SFP which is not defined in SFF-8472, but is
            // requested by a customer. See bugzilla#E2020
            return tr_100_zx_get;
        } else if (len_smf_km == SFP_LEN_UNSPECIFIED && len_smf_100m == SFP_LEN_UNSPECIFIED &&
                   len_om2 == SFP_LEN_OM_2KM && len_om1 == SFP_LEN_OM_2KM &&
                   len_om4_or_dac == SFP_LEN_UNSPECIFIED) {
            // This is a special SFP which is not defined in SFF-8472, but is
            // requested by a customer. See bugzilla#E2146
            if (speed == SFP_BR_100M) {
                return tr_100_sx_get;
            } else if (speed == SFP_BR_FE) {
                return tr_100_fx_get;
            } else {
                return tr_100_lx_get;
            }
        } else if (speed < SFP_BR_1G) {
            return tr_100_lx_get;
        } else if (speed < SFP_BR_2G5) {
            return tr_1000_x_get;
        } else if (speed < SFP_BR_5G) {
            return tr_2g5_get;
        } else if (speed < SFP_BR_10G) {
            return tr_5g_get;
        } else if (speed < SFP_BR_25G) {
            return tr_10g_get;
        } else {
            return tr_25g_get;
        }
    }

    return tr_1000_sx_get;
}

static if_func_t if_func_get(meba_sfp_transreceiver_t tr)
{
    switch (tr) {
    case MEBA_SFP_TRANSRECEIVER_100FX:
    case MEBA_SFP_TRANSRECEIVER_100BASE_LX:
    case MEBA_SFP_TRANSRECEIVER_100BASE_ZX:
    case MEBA_SFP_TRANSRECEIVER_100BASE_SX: return fx_if_get;

    case MEBA_SFP_TRANSRECEIVER_1000BASE_T: return cisco_sgmii_if_get;

    case MEBA_SFP_TRANSRECEIVER_1000BASE_BX10:
    case MEBA_SFP_TRANSRECEIVER_1000BASE_CX:
    case MEBA_SFP_TRANSRECEIVER_1000BASE_SX:
    case MEBA_SFP_TRANSRECEIVER_1000BASE_LX:
    case MEBA_SFP_TRANSRECEIVER_1000BASE_ZX:
    case MEBA_SFP_TRANSRECEIVER_1000BASE_LR:
    case MEBA_SFP_TRANSRECEIVER_1000BASE_X:    return serdes_if_get;

    case MEBA_SFP_TRANSRECEIVER_2G5:       return tr_2g5_if_get;
    case MEBA_SFP_TRANSRECEIVER_10GBASE_T: return tr_10gbaset_if_get;
    case MEBA_SFP_TRANSRECEIVER_5G:
    case MEBA_SFP_TRANSRECEIVER_10G:
    case MEBA_SFP_TRANSRECEIVER_10G_SR:
    case MEBA_SFP_TRANSRECEIVER_10G_LR:
    case MEBA_SFP_TRANSRECEIVER_10G_LRM:
    case MEBA_SFP_TRANSRECEIVER_10G_ER:
    case MEBA_SFP_TRANSRECEIVER_10G_DAC:
    case MEBA_SFP_TRANSRECEIVER_25G:
    case MEBA_SFP_TRANSRECEIVER_25G_SR:
    case MEBA_SFP_TRANSRECEIVER_25G_LR:
    case MEBA_SFP_TRANSRECEIVER_25G_LRM:
    case MEBA_SFP_TRANSRECEIVER_25G_ER:
    case MEBA_SFP_TRANSRECEIVER_25G_DAC:   return sfi_if_get;

    default: break;
    }

    return serdes_if_get;
}

static mt_func_t mt_func_get(meba_sfp_transreceiver_t tr)
{
    switch (tr) {
    case MEBA_SFP_TRANSRECEIVER_10G:
    case MEBA_SFP_TRANSRECEIVER_25G: return sfi_mt_none_get;

    case MEBA_SFP_TRANSRECEIVER_1000BASE_BX10:
    case MEBA_SFP_TRANSRECEIVER_1000BASE_CX:
    case MEBA_SFP_TRANSRECEIVER_1000BASE_SX:
    case MEBA_SFP_TRANSRECEIVER_1000BASE_X:
    case MEBA_SFP_TRANSRECEIVER_10G_SR:
    case MEBA_SFP_TRANSRECEIVER_25G_SR:
    case MEBA_SFP_TRANSRECEIVER_10G_LRM:
    case MEBA_SFP_TRANSRECEIVER_25G_LRM:       return sfi_mt_get;

    case MEBA_SFP_TRANSRECEIVER_1000BASE_LX:
    case MEBA_SFP_TRANSRECEIVER_1000BASE_ZX:
    case MEBA_SFP_TRANSRECEIVER_10G_ER:
    case MEBA_SFP_TRANSRECEIVER_25G_ER:
    case MEBA_SFP_TRANSRECEIVER_10G_LR:
    case MEBA_SFP_TRANSRECEIVER_25G_LR:      return sfi_mt_zr_get;

    case MEBA_SFP_TRANSRECEIVER_1000BASE_T:
    case MEBA_SFP_TRANSRECEIVER_10G_DAC:
    case MEBA_SFP_TRANSRECEIVER_25G_DAC:    return sfi_mt_dac_get;

    default: break;
    }

    return sfi_mt_none_get;
}

static conf_func_t conf_func_get(meba_sfp_transreceiver_t tr)
{
    return tr == MEBA_SFP_TRANSRECEIVER_1000BASE_T ? cisco_sgmii_conf_set : serdes_conf_set;
}

// The size of destination array argument should at least be (len + 1)
static void sfp_strncpy(char *dest, uint8_t *rom, uint32_t len)
{
    int i, c, replace = '\0';

    // Start from the end in order to replace trailing spaces with '\0'.
    // Only when the first printable, non-space character is seen will we start
    // replacing non-printable characters with spaces.
    for (i = len - 1; i >= 0; i--) {
        c = rom[i];

        if (isprint(c) && (c != ' ')) {
            replace = ' ';
            dest[i] = c;
        } else {
            dest[i] = replace;
        }
    }

    dest[len] = '\0';
}

static mesa_bool_t read_raw_sfp_rom(meba_inst_t    meba_inst,
                                    mesa_port_no_t port_no,
                                    uint8_t *const rom,
                                    const size_t   rom_size)
{
    for (int i = 0; i < 10; ++i) {
        if ((meba_inst->api.meba_sfp_i2c_xfer(meba_inst, port_no, false, 0x50, 0, rom, rom_size,
                                              false) == MESA_RC_OK)) {
            // rom[0] == 0x03 means SFP or SFP+
            if (rom[0] == 0x03) {
                return true;
            }
        }

        VTSS_MSLEEP(100); // Some SFPs are slow to start, wait 100ms
    }

    return false;
}

static mesa_bool_t get_sfp_rom(meba_inst_t meba_inst, mesa_port_no_t port_no, sfp_rom_t *const out)
{
    uint8_t rom[92];

    if (!read_raw_sfp_rom(meba_inst, port_no, rom, sizeof(rom))) {
        return false;
    }

    sfp_strncpy(out->vendor_name, &rom[20], 16);
    sfp_strncpy(out->vendor_pn, &rom[40], 16);
    sfp_strncpy(out->vendor_rev, &rom[56], 4);
    sfp_strncpy(out->vendor_sn, &rom[68], 16);
    sfp_strncpy(out->date_code, &rom[84], 8);

    out->identifier = rom[0];
    out->connector = rom[2];
    out->eth_10g_compl = rom[3] & SFM_MSA_10G_ETHER;
    out->eth_compl = rom[6];
    out->cable_tech = rom[8];
    out->nominal_br = rom[12];
    out->len_smf_km = rom[14];
    out->len_smf_100m = rom[15];
    out->len_om2 = rom[16];
    out->len_om1 = rom[17];
    out->len_om4_or_dac = rom[18];
    out->ext_compl = rom[36];
    out->dmi_type = rom[65];
    out->los_implemented = (rom[65] & 0x02) != 0;

    return true;
}

static mesa_bool_t device_info_get(struct meba_inst       *meba_inst,
                                   mesa_port_no_t          port_no,
                                   meba_sfp_device_info_t *device_info,
                                   tr_func_t              *tr_func)
{
    sfp_rom_t sfp_rom = {0};
    tr_func_t transceiver_func;

    if (!get_sfp_rom(meba_inst, port_no, &sfp_rom)) {
        return false;
    }

    // Copy vendor strings into the public device_info struct.
    memcpy(device_info->vendor_name, sfp_rom.vendor_name, sizeof(sfp_rom.vendor_name));
    memcpy(device_info->vendor_pn, sfp_rom.vendor_pn, sizeof(sfp_rom.vendor_pn));
    memcpy(device_info->vendor_rev, sfp_rom.vendor_rev, sizeof(sfp_rom.vendor_rev));
    memcpy(device_info->vendor_sn, sfp_rom.vendor_sn, sizeof(sfp_rom.vendor_sn));
    memcpy(device_info->date_code, sfp_rom.date_code, sizeof(sfp_rom.date_code));

    transceiver_func = tr_func_get(&sfp_rom);
    transceiver_func(NULL, &device_info->transceiver);
    device_info->connector = sfp_rom.connector;

    if (tr_func) {
        *tr_func = transceiver_func;
    }

    return true;
}

mesa_bool_t meba_fill_driver(meba_inst_t             meba_inst,
                             mesa_port_no_t          port_no,
                             meba_sfp_driver_t      *driver,
                             meba_sfp_device_info_t *device_info)
{
    tr_func_t tr_func;

    if (meba_inst == NULL || driver == NULL || device_info == NULL) {
        return false;
    }

    if (!device_info_get(meba_inst, port_no, device_info, &tr_func)) {
        return false;
    }

    if ((driver->product_name = (char *)malloc(17)) == NULL) {
        return false;
    }

    // Set functions common to any driver
    driver->meba_sfp_driver_probe = dev_probe;
    driver->meba_sfp_driver_delete = dev_delete;
    driver->meba_sfp_driver_reset = dev_reset;
    driver->meba_sfp_driver_poll = dev_poll;

    // Set functions based on transceiver type (which we got from the ROM).
    driver->meba_sfp_driver_tr_get = tr_func;
    driver->meba_sfp_driver_if_get =
        if_func_get(device_info->transceiver); // SFP type -> API interface type
    driver->meba_sfp_driver_mt_get =
        mt_func_get(device_info->transceiver); // SFP type -> 10G/25G media type
    driver->meba_sfp_driver_conf_set =
        conf_func_get(device_info->transceiver); // SFP type -> Serdes/Cisco-SGMII

    memcpy(driver->product_name, device_info->vendor_pn, 17);

    return true;
}

mesa_bool_t meba_sfp_device_info_get(struct meba_inst       *meba_inst,
                                     mesa_port_no_t          port_no,
                                     meba_sfp_device_info_t *device_info)
{
    if (meba_inst == NULL || device_info == NULL) {
        return false;
    }
    return device_info_get(meba_inst, port_no, device_info, NULL);
}

mesa_rc meba_sfp_cage_status_get(struct meba_inst  *meba_inst,
                                 mesa_port_no_t     port_no,
                                 meba_sfp_status_t *status)
{
    if (meba_inst == NULL || status == NULL || meba_inst->api.meba_sfp_status_get == NULL) {
        return MESA_RC_ERROR;
    }
    return meba_inst->api.meba_sfp_status_get(meba_inst, port_no, status);
}
