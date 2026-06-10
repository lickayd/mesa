// Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#include <stdbool.h>

#include <microchip/ethernet/phy/api.h>
#include <mepa_driver.h>
#include <mepa_trace.h>
#include <phy_lib.h>

#include "../../common/include/lan8814_registers.h" // Re-use LAN8814 register defines
#include "lan884x_private.h"

static mepa_rc pfe_direct_reg_rd(mepa_device_t *dev, uint16_t addr, uint16_t *value)
{
    if (dev->callout->miim_read(dev->callout_ctx, addr, value) != MESA_RC_OK) {
        T_E(MEPA_TRACE_GRP_GEN, "Port %d miim read failed\n", dev->numeric_handle);
    }
    return MEPA_RC_OK;
}

static mepa_rc pfe_direct_reg_wr(mepa_device_t *dev, uint16_t addr, uint16_t value, uint16_t mask)
{
    uint16_t reg_val = value;
    mesa_rc rc;

    rc = dev->callout->miim_read(dev->callout_ctx, addr, &reg_val);
    if (rc == MESA_RC_OK) {
        if (mask != LAN8814_DEF_MASK) {
            reg_val = (reg_val & ~mask) | (value & mask);
        } else {
            reg_val = value;
        }
        rc = dev->callout->miim_write(dev->callout_ctx, addr, reg_val);
        if (rc != MESA_RC_OK) {
            T_E(MEPA_TRACE_GRP_GEN, "Port %d miim write failed\n", dev->numeric_handle);
        }
    } else {
        T_E(MEPA_TRACE_GRP_GEN, "Port %d miim write failed\n", dev->numeric_handle);
    }
    return MEPA_RC_OK;
}

// MMD read and write functions
// MMD device range : 0 - 31
static mepa_rc pfe_mmd_reg_rd(mepa_device_t *dev, uint16_t mmd, uint16_t addr, uint16_t *value)
{
    // Set-up to MMD register.
    MEPA_RC(pfe_direct_reg_wr(dev, LAN8814_MMD_ACCESS_CTRL, mmd, LAN8814_DEF_MASK));
    MEPA_RC(pfe_direct_reg_wr(dev, LAN8814_MMD_ACCESS_ADDR_DATA, addr, LAN8814_DEF_MASK));
    MEPA_RC(pfe_direct_reg_wr(dev, LAN8814_MMD_ACCESS_CTRL,
                              LAN8814_F_MMD_ACCESS_CTRL_MMD_FUNC | mmd, LAN8814_DEF_MASK));

    // Read the value
    MEPA_RC(pfe_direct_reg_rd(dev, LAN8814_MMD_ACCESS_ADDR_DATA, value));
    return MEPA_RC_OK;
}

static mepa_rc pfe_mmd_reg_wr(mepa_device_t *dev, uint16_t mmd, uint16_t addr, uint16_t value, uint16_t mask)
{
    // Set-up to MMD register.
    MEPA_RC(pfe_direct_reg_wr(dev, LAN8814_MMD_ACCESS_CTRL, mmd, LAN8814_DEF_MASK));
    MEPA_RC(pfe_direct_reg_wr(dev, LAN8814_MMD_ACCESS_ADDR_DATA, addr, LAN8814_DEF_MASK));
    MEPA_RC(pfe_direct_reg_wr(dev, LAN8814_MMD_ACCESS_CTRL,
                              LAN8814_F_MMD_ACCESS_CTRL_MMD_FUNC | mmd, LAN8814_DEF_MASK));

    // write the value
    MEPA_RC(pfe_direct_reg_wr(dev, LAN8814_MMD_ACCESS_ADDR_DATA, value, mask));
    return MEPA_RC_OK;
}

static mepa_rc pfe_delete(mepa_device_t *dev)
{
    return mepa_delete_int(dev);
}

static mepa_rc pfe_get_device_info(mepa_device_t *dev)
{
    lan884x_data_t *data = (lan884x_data_t *)dev->data;
    uint16_t id;

    (void)pfe_direct_reg_rd(dev, LAN8814_DEVICE_ID_2, &id);

    data->dev.model = (uint8_t)LAN8814_X_DEV_ID_MODEL(id);
    data->dev.rev = (uint8_t)LAN8814_X_DEV_ID_REV(id);
    T_I(MEPA_TRACE_GRP_GEN, "model 0x%x rev %d\n", data->dev.model, data->dev.rev);

    return MEPA_RC_OK;
}

static const char *if2txt(mesa_port_interface_t if_type)
{
    const char *res;

    switch (if_type) {
    case MESA_PORT_INTERFACE_GMII:
        res = "GMII";
        break;
    case MESA_PORT_INTERFACE_RGMII:
        res = "RGMII";
        break;
    case MESA_PORT_INTERFACE_RGMII_ID:
        res = "RGMII_ID";
        break;
    case MESA_PORT_INTERFACE_RGMII_RXID:
        res = "RGMII_RXID";
        break;
    case MESA_PORT_INTERFACE_RGMII_TXID:
        res = "RGMII_TXID";
        break;
    default:
        res = "?   ";
        break;
    }
    return res;
}

static mepa_device_t *pfe_probe(mepa_driver_t *drv,
                                const mepa_callout_t    MEPA_SHARED_PTR *callout,
                                struct mepa_callout_ctx MEPA_SHARED_PTR *callout_ctx,
                                struct mepa_board_conf              *board_conf)
{
    mepa_device_t  *dev;
    lan884x_data_t *data;

    dev = mepa_create_int(drv, callout, callout_ctx, board_conf, (int)sizeof(lan884x_data_t));
    if (dev == NULL) {
        return NULL;
    }

    data = dev->data;
    data->port_no = board_conf->numeric_handle;
    data->events = 0;

    T_I(MEPA_TRACE_GRP_GEN, "pfe driver probed for port %d", data->port_no);
    return dev;
}

static mepa_rc pfe_conf_get(mepa_device_t *dev, mepa_conf_t *const config)
{
    lan884x_data_t *data = (lan884x_data_t *)dev->data;

    MEPA_ENTER(dev);
    *config = data->conf;
    MEPA_EXIT(dev);
    T_D(MEPA_TRACE_GRP_GEN, "returning phy config on port %d", data->port_no);
    return MEPA_RC_OK;
}

static mesa_rc pfe_conf_set(mepa_device_t      *dev,
                            const mepa_conf_t  *config)
{
    lan884x_data_t *data = (lan884x_data_t *)dev->data;
    uint16_t old_adv, new_adv;
    uint16_t val;
    mepa_bool_t restart_aneg = false;
    mepa_rc rc = MEPA_RC_OK;

    MEPA_ENTER(dev);
    if (!config->admin.enable) {
        rc = phy_reg_modify(dev, MII_BMCR, BMCR_PDOWN, BMCR_PDOWN);
    } else {
        if (config->speed == MEPA_SPEED_AUTO || config->speed == MEPA_SPEED_1G) {
            if (config->admin.enable != data->conf.admin.enable) {
                restart_aneg = true;
            }

            /* Check the 1000 advertise */
            rc = phy_reg_rd(dev, MII_CTRL1000, &old_adv);
            if (rc != MEPA_RC_OK) {
                goto out;
            }

            new_adv = config->aneg.speed_1g_fdx ? CTRL1000_1000FULL : 0U;
            if (config->man_neg != MEPA_MANUAL_NEG_DISABLED) {
                new_adv |= (config->man_neg == MEPA_MANUAL_NEG_REF) ? CTRL1000_AS_MASTER : 0U;
                new_adv |= CTRL1000_ENABLE_MASTER;
            }

            if (old_adv != new_adv) {
                restart_aneg = true;

                rc = phy_reg_wr(dev, MII_CTRL1000, new_adv);
                if (rc != MEPA_RC_OK) {
                    goto out;
                }
            }

            /* Check the 10/100 advertise */
            rc = phy_reg_rd(dev, MII_ADVERTISE, &old_adv);
            if (rc != MEPA_RC_OK) {
                goto out;
            }

            new_adv = (config->aneg.tx_remote_fault ? ADVERTISE_RFAULT : 0U) |
                      (config->flow_control ? ADVERTISE_PAUSE_ASYM : 0U) |
                      (config->flow_control ? ADVERTISE_PAUSE_CAP : 0U) |
                      (config->aneg.speed_100m_fdx ? ADVERTISE_100FULL : 0U) |
                      (config->aneg.speed_100m_hdx ? ADVERTISE_100HALF : 0U) |
                      (config->aneg.speed_10m_fdx ? ADVERTISE_10FULL : 0U) |
                      (config->aneg.speed_10m_hdx ? ADVERTISE_10HALF : 0U) |
                      ADVERTISE_CSMA;

            if (old_adv != new_adv) {
                restart_aneg = true;

                rc = phy_reg_wr(dev, MII_ADVERTISE, new_adv);
                if (rc != MEPA_RC_OK) {
                    goto out;
                }
            }

            rc = phy_reg_modify(dev, MII_BMCR, BMCR_PDOWN | BMCR_ANENABLE,
                                BMCR_ANENABLE);
            if (rc != MEPA_RC_OK) {
                goto out;
            }

            if (restart_aneg) {
                rc = phy_reg_modify(dev, MII_BMCR, BMCR_ANRESTART, BMCR_ANRESTART);
            }
        } else {
            if (config->speed == MEPA_SPEED_UNDEFINED) {
                goto out;
            }

            val = (config->speed == MEPA_SPEED_100M ? BMCR_SPEED100 : 0U) |
                  (config->fdx ? BMCR_FULLDPLX : 0U);
            rc = phy_reg_modify(dev, MII_BMCR, BMCR_PDOWN | BMCR_ANENABLE | BMCR_SPEED1000 |
                                BMCR_SPEED100 | BMCR_FULLDPLX, val);
        }
    }
    data->conf = *config;

out:
    MEPA_EXIT(dev);
    return rc;
}

static mesa_rc pfe_if_get(mepa_device_t *dev, mesa_port_speed_t speed,
                          mesa_port_interface_t *mac_if)
{
    uint16_t val = 0;
    mesa_rc rc;
    lan884x_data_t *data = (lan884x_data_t *)dev->data;

    rc = pfe_mmd_reg_rd(dev, 2, LAN8840_OPERATION_MODE_STRAP_LOW_REGISTER, &val);
    if (MEPA_RC_OK != rc) {
        T_E(MEPA_TRACE_GRP_GEN, "Could not read from port %d", data->port_no);
    }

    if ((val & LAN8840_OPERATION_MODE_STRAP_LOW_REGISTER_STRAP_RGMII_EN) != 0U) {
        *mac_if = data->mac_if;
    } else {
        *mac_if = MESA_PORT_INTERFACE_GMII;
    }


    return MESA_RC_OK;
}

static mepa_rc pfe_config_rgmii_delay(mepa_device_t *dev, mepa_port_interface_t interface)
{
    u16 rxcdll_val, txcdll_val;
    mepa_rc rc;

    // Note:
    // Extra phy delay in CONTROL_PAD_SKEW, RX_DATA_PAD_SKEW, TX_DATA_PAD_SKEW, CLK_PAD_SKEW
    // currently not supported

    rxcdll_val = 0U;
    txcdll_val = 0U;
    rc = MEPA_RC_OK;

    switch (interface) {
    case MESA_PORT_INTERFACE_RGMII:
        rxcdll_val = DISABLE_DLL_RX_BIT;
        txcdll_val = DISABLE_DLL_TX_BIT;
        break;
    case MESA_PORT_INTERFACE_RGMII_ID:
        rxcdll_val = PFE_DLL_ENABLE_DELAY;
        txcdll_val = PFE_DLL_ENABLE_DELAY;
        break;
    case MESA_PORT_INTERFACE_RGMII_RXID:
        rxcdll_val = PFE_DLL_ENABLE_DELAY;
        txcdll_val = DISABLE_DLL_TX_BIT;
        break;
    case MESA_PORT_INTERFACE_RGMII_TXID:
        rxcdll_val = DISABLE_DLL_RX_BIT;
        txcdll_val = PFE_DLL_ENABLE_DELAY;
        break;
    default:
        rc = MEPA_RC_ERROR;
        break;
    }

    if (rc != MEPA_RC_OK) {
        return rc;
    }

    rc = pfe_mmd_reg_wr(dev, PFE_MMD_COMMON_CTRL_REG,
                        PFE_RXC_DLL_CTRL,
                        rxcdll_val, DISABLE_DLL_MASK);

    if (rc != MEPA_RC_OK) {
        return rc;
    }

    return pfe_mmd_reg_wr(dev, PFE_MMD_COMMON_CTRL_REG,
                          PFE_TXC_DLL_CTRL,
                          txcdll_val, DISABLE_DLL_MASK);
}

static mepa_rc pfe_if_set(mepa_device_t *dev,
                          mepa_port_interface_t mac_if)
{
    lan884x_data_t *data = (lan884x_data_t *)dev->data;
    mepa_rc rc = MEPA_RC_OK;

    if (mac_if == MESA_PORT_INTERFACE_RGMII ||
        mac_if == MESA_PORT_INTERFACE_RGMII_ID ||
        mac_if == MESA_PORT_INTERFACE_RGMII_RXID ||
        mac_if == MESA_PORT_INTERFACE_RGMII_TXID) {
        rc = pfe_config_rgmii_delay(dev, mac_if);
        if (rc == MEPA_RC_OK) {
            data->mac_if = mac_if;
        }
    }

    return rc;
}

static uint32_t pfe_capability(mepa_device_t *dev, uint32_t capability)
{
    uint32_t c;

    switch (capability) {
    case (uint32_t)MEPA_CAP_SPEED_1G:
        c = 1U;
        break;
    default:
        c = 0U;
        break;
    }

    return c;
}

static mepa_rc pfe_reset(mepa_device_t *dev, const mepa_reset_param_t *rst_conf)
{
    lan884x_data_t *data = (lan884x_data_t *)dev->data;
    mepa_rc rc;

    if (rst_conf->reset_point == MEPA_RESET_POINT_DEFAULT) {
        (void)pfe_direct_reg_wr(dev, LAN8814_BASIC_CONTROL, LAN8814_F_BASIC_CTRL_SOFT_RESET, LAN8814_F_BASIC_CTRL_SOFT_RESET);
        // The reset of the PHY will reset also RMII delays which are based on
        // the MAC interface. Therfore after a reset of the PHY it is required
        // to set again the interface to set the correct delays.
        (void)pfe_if_set(dev, data->mac_if);
    }
    MEPA_MSLEEP(1);

    rc = pfe_mmd_reg_wr(dev, LAN8841_MMD_ANALOG_REG,
                        LAN8841_ANALOG_CONTROL_11,
                        LAN8841_ANALOG_CONTROL_11_LDO_REF(1U),
                        LAN8841_ANALOG_CONTROL_11_LDO_MASK);

    (void)pfe_get_device_info(dev);
    return rc;
}

static mepa_rc pfe_info_get(mepa_device_t *dev, mepa_phy_info_t *const phy_info)
{
    lan884x_data_t *data = (lan884x_data_t *)dev->data;

    phy_info->part_number = 8841;
    phy_info->revision = data->dev.rev;
    if (pfe_capability(dev, (uint32_t)MEPA_CAP_SPEED_1G) != 0U) {
        phy_info->cap = MEPA_CAP_SPEED_MASK_1G;
    }

    phy_info->manufactor_name = "Microchip";
    phy_info->model_name = "LAN884X";

    return MEPA_RC_OK;
}

static mepa_rc pfe_poll(mepa_device_t *dev, mepa_status_t *status)
{
    uint16_t val, val2 = 0;
    lan884x_data_t *data = (lan884x_data_t *) dev->data;

    MEPA_ENTER(dev);
    (void)pfe_direct_reg_rd(dev, LAN8814_BASIC_STATUS, &val);
    status->link = ((val & LAN8814_F_BASIC_STATUS_LINK_STATUS) != 0U);

    if (data->loopback.near_end_ena == TRUE) {
        // loops back to Mac. Ignore Line side status to Link partner.
        status->link = true;
    }
    if (data->conf.speed == MEPA_SPEED_AUTO || data->conf.speed == MEPA_SPEED_1G) {
        uint16_t lp_sym_pause = 0, lp_asym_pause = 0;
        // Default values
        status->speed = MEPA_SPEED_UNDEFINED;
        status->fdx = true;
        // check if auto-negotiation is completed or not.
        if (!data->loopback.near_end_ena && status->link && ((val & LAN8814_F_BASIC_STATUS_ANEG_COMPLETE) == 0U)) {
            T_I(MEPA_TRACE_GRP_GEN, "Aneg is not completed for port %d", data->port_no);
            status->link = false;
        } else if (data->loopback.near_end_ena) {
            status->speed = MEPA_SPEED_1G;
        } else {
            // aneg complete, no loopback
        }
        if (!status->link || data->loopback.near_end_ena) {
            // No need to read aneg values when link is down or when near-end loopback enabled.
            goto end;
        }
        // Obtain speed and duplex from link partner's advertised capability.
        (void)pfe_direct_reg_rd(dev, LAN8814_ANEG_LP_BASE, &val);
        (void)pfe_direct_reg_rd(dev, LAN8814_ANEG_MSTR_SLV_STATUS, &val2);
        // 1G half duplex is not supported. Refer direct register - 9
        if (((val2 & LAN8814_F_ANEG_MSTR_SLV_STATUS_1000_T_FULL_DUP) != 0U) &&
            data->conf.aneg.speed_1g_fdx) {
            status->speed = MEPA_SPEED_1G;
            status->fdx = true;
        } else if (((val & LAN8814_F_ANEG_LP_BASE_100_X_FULL_DUP) != 0U) &&
                   data->conf.aneg.speed_100m_fdx) {
            status->speed = MEPA_SPEED_100M;
            status->fdx = true;
        } else if (((val & LAN8814_F_ANEG_LP_BASE_100_X_HALF_DUP) != 0U) &&
                   data->conf.aneg.speed_100m_hdx) {
            status->speed = MEPA_SPEED_100M;
            status->fdx = false;
        } else if (((val & LAN8814_F_ANEG_LP_BASE_10_T_FULL_DUP) != 0U) &&
                   data->conf.aneg.speed_10m_fdx) {
            status->speed = MEPA_SPEED_10M;
            status->fdx = true;
        } else if (((val & LAN8814_F_ANEG_LP_BASE_10_T_HALF_DUP) != 0U) &&
                   data->conf.aneg.speed_10m_hdx) {
            status->speed = MEPA_SPEED_10M;
            status->fdx = false;
        } else {
            // no matching link partner capability
        }
        // Get flow control status
        lp_sym_pause = ((val & LAN8814_F_ANEG_LP_BASE_SYM_PAUSE) != 0U) ? 1U : 0U;
        lp_asym_pause = ((val & LAN8814_F_ANEG_LP_BASE_ASYM_PAUSE) != 0U) ? 1U : 0U;
        status->aneg.obey_pause = data->conf.flow_control && ((lp_sym_pause != 0U) || (lp_asym_pause != 0U));
        status->aneg.generate_pause = data->conf.flow_control && (lp_sym_pause != 0U);
    } else {
        uint8_t speed;
        uint8_t bit0;
        uint8_t bit1;
        // Forced speed
        (void)pfe_direct_reg_rd(dev, LAN8814_BASIC_CONTROL, &val2);
        bit0 = ((val2 & LAN8814_F_BASIC_CTRL_SPEED_SEL_BIT_0) != 0U) ? 1U : 0U;
        bit1 = ((val2 & LAN8814_F_BASIC_CTRL_SPEED_SEL_BIT_1) != 0U) ? 1U : 0U;
        speed = (uint8_t)(bit0 | (uint8_t)(bit1 << 1U));
        status->speed = (speed == 0U) ? MEPA_SPEED_10M :
                        (speed == 1U) ? MEPA_SPEED_100M :
                        (speed == 2U) ? MEPA_SPEED_1G : MEPA_SPEED_UNDEFINED;
        status->fdx = ((val2 & LAN8814_F_BASIC_CTRL_DUP_MODE) != 0U);
        //check that aneg is not enabled.
        if ((val2 & LAN8814_F_BASIC_CTRL_ANEG_ENA) != 0U) {
            T_W(MEPA_TRACE_GRP_GEN, "Aneg is enabled for forced speed config on port %d", data->port_no);
        }
    }
end:
    data->link_status = status->link;
    data->speed_status = status->speed;
    data->fdx_status   = status->fdx;
    MEPA_EXIT(dev);
    T_D(MEPA_TRACE_GRP_GEN, "port %d status link %d, speed %d, fdx %d", data->port_no, status->link, status->speed, status->fdx);
    return MEPA_RC_OK;
}

static mepa_rc pfe_aneg_status_get(mepa_device_t *dev, mepa_aneg_status_t *status)
{
    uint16_t val;

    MEPA_ENTER(dev);
    (void)pfe_direct_reg_rd(dev, LAN8814_ANEG_MSTR_SLV_STATUS, &val);
    status->master_cfg_fault = ((val & LAN8814_F_ANEG_MSTR_SLV_STATUS_CFG_FAULT) != 0U) ? TRUE : FALSE;
    status->master = ((val & LAN8814_F_ANEG_MSTR_SLV_STATUS_CFG_RES) != 0U) ? TRUE : FALSE;
    MEPA_EXIT(dev);
    T_I(MEPA_TRACE_GRP_GEN, "aneg status get mstr %d", status->master);
    return MEPA_RC_OK;
}

// read direct registers for debugging
static mepa_rc pfe_direct_reg_read(mepa_device_t *dev, uint32_t address, uint16_t *const value)
{
    mepa_rc rc;
    uint16_t addr = (uint16_t)(address & 0x1fU);

    MEPA_ENTER(dev);
    rc = pfe_direct_reg_rd(dev, addr, value);
    MEPA_EXIT(dev);
    return rc;
}

// write direct registers. Used for debugging.
static mepa_rc pfe_direct_reg_write(mepa_device_t *dev, uint32_t address, uint16_t value)
{
    mepa_rc rc;
    uint16_t addr = (uint16_t)(address & 0x1fU);

    MEPA_ENTER(dev);
    rc = pfe_direct_reg_wr(dev, addr, value, 0xFFFFU);
    MEPA_EXIT(dev);
    return rc;
}

// read extended page/mmd register for debugging
static mepa_rc pfe_ext_mmd_reg_read(mepa_device_t *dev, uint32_t address, uint16_t *const value)
{
    mepa_rc rc = MEPA_RC_OK;
    uint16_t mmd = (uint16_t)((address >> 16) & 0xffffU);
    uint16_t addr = (uint16_t)(address & 0xffffU);

    MEPA_ENTER(dev);
    if (mmd != 0U) {
        rc = pfe_mmd_reg_rd(dev, mmd, addr, value);
    }
    MEPA_EXIT(dev);
    return rc;
}

// write extended page/mmd register. Used for debugging.
static mepa_rc pfe_ext_mmd_reg_write(mepa_device_t *dev, uint32_t address, uint16_t value)
{
    mepa_rc rc = MEPA_RC_OK;
    uint16_t mmd = (uint16_t)((address >> 16) & 0xffffU);
    uint16_t addr = (uint16_t)(address & 0xffffU);

    MEPA_ENTER(dev);
    if (mmd != 0U) {
        rc = pfe_mmd_reg_wr(dev, mmd, addr, value, 0xFFFFU);
    }
    MEPA_EXIT(dev);
    return rc;
}

static void pfe_phy_deb_pr_reg(mepa_device_t *dev,
                               lmu_ss_t *const ss,
                               uint16_t mmd, uint16_t page, uint16_t addr,
                               const char *str, uint16_t *value)
{
    mepa_rc rc = MEPA_RC_OK;
    lan884x_data_t *data = (lan884x_data_t *)dev->data;
    mepa_port_no_t port_no = data->port_no;
    uint16_t id = page;

    if (mmd != 0U) {
        id = mmd;
        rc = pfe_mmd_reg_rd(dev, mmd, addr, value);
    } else {
        rc = pfe_direct_reg_rd(dev, addr, value);
    }
    if (MEPA_RC_OK == rc) {
        pr("%-45s:  0x%02x  0x%02x   0x%04x     0x%08x\n", str, to_u32(port_no), id, addr, *value);
    }
}

static mepa_rc reg_dump(struct mepa_device *dev,
                        lmu_ss_t *const ss)
{
    uint16_t val = 0;

    //Direct registers
    pr("%-45s   PORT_NO PAGE_ID REG_ADDR   VALUE\n", "REG_NAME");
    pr("Main Page Registers\n");
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 0, "Basic Control Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 1, "Basic Status Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 2, "Device Identifier 1 Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 3, "Device Identifier 2 Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 4, "Auto-Negotiation Advertisement Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 5, "Auto-Negotiation Link Partner Base Page Ability Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 6, "Auto-Negotiation Expansion Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 7, "Auto-Negotiation Next Page TX Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 8, "Auto-Negotiation Next Page RX Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 9, "Auto-Negotiation Master Slave Control Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 10, "Auto-Negotiation Master Slave Status Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 13, "MMD Access Control Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 14, "MMD Access Address/Data Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 15, "Extended Status Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 16, "PCS Loop-back Lane Skew Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 17, "PCS Loop-back Swap/Polarity Control Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 18, "Cable Diagnostic Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 19, "Digital PMA/PCS Status Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 20, "Digital AX/AN Status Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 21, "RXER Counter Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 22, "LED Mode Select Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 23, "LED Behavior Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 24, "Interrupt Enable Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 25, "Output control Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 26, "UNH Test Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 27, "Interrupt Status Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 28, "Digital Debug Control 1 Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 29, "Digital Debug Control 2 Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 30, "Reserved Register", &val);
    pfe_phy_deb_pr_reg(dev, ss, 0, 0, 31, "Control Register", &val);
    return MEPA_RC_OK;
}


static mepa_rc pfe_debug_info_dump(struct mepa_device *dev,
                                   lmu_ss_t *const ss,
                                   const mepa_debug_info_t   *const info)
{
    mepa_rc rc = MEPA_RC_OK;
    mepa_phy_info_t phy_info;
    mesa_port_interface_t mac_if;

    (void)pfe_info_get(dev, &phy_info);
    (void)pfe_if_get(dev, MESA_SPEED_1G, &mac_if);

    if (info->layer == MEPA_DEBUG_LAYER_AIL || info->layer == MEPA_DEBUG_LAYER_ALL) {
        MEPA_ENTER(dev);
        pr("Port:%d   Family:Pfeiffer   Type:%d   Rev:%d   MacIf:%s\n", dev->numeric_handle,
           phy_info.part_number, phy_info.revision, if2txt(mac_if));
        MEPA_EXIT(dev);
    }

    if (info->layer == MEPA_DEBUG_LAYER_CIL || info->layer == MEPA_DEBUG_LAYER_ALL) {

        switch (info->group) {
        case MEPA_DEBUG_GROUP_ALL:
        case MEPA_DEBUG_GROUP_PHY: {
            MEPA_ENTER(dev);
            rc = reg_dump(dev, ss);
            MEPA_EXIT(dev);
        }
        break;
        default:
            rc = MEPA_RC_OK;
            break;
        }

    }
    return rc;
}

mepa_drivers_t mepa_lan884x_driver_init(void)
{
    static const int nr_pfeiffer_drivers = 1;
    static mepa_driver_t pfeiffer_drivers[] = {
        {
            .id = 0x221650,  // Pfeiffer RGMII/GMII PHY
            .mask = 0xfffff0,
            .mepa_driver_delete = pfe_delete,
            .mepa_driver_reset = pfe_reset,
            .mepa_driver_poll = pfe_poll,
            .mepa_driver_conf_set = pfe_conf_set,
            .mepa_driver_conf_get = pfe_conf_get,
            .mepa_driver_if_set = pfe_if_set,
            .mepa_driver_if_get = pfe_if_get,
            .mepa_driver_probe = pfe_probe,
            .mepa_driver_aneg_status_get = pfe_aneg_status_get,
            .mepa_driver_clause22_read = pfe_direct_reg_read,
            .mepa_driver_clause22_write = pfe_direct_reg_write,
            .mepa_driver_clause45_read  = pfe_ext_mmd_reg_read,
            .mepa_driver_clause45_write = pfe_ext_mmd_reg_write,
            .mepa_driver_capability = pfe_capability,
            .mepa_driver_phy_info_get = pfe_info_get,
            .mepa_driver_debug_info_dump = pfe_debug_info_dump,
        },
    };

    mepa_drivers_t result;
    result.phy_drv = pfeiffer_drivers;
    result.count = nr_pfeiffer_drivers;

    return result;
}
