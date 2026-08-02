// Copyright (c) 2004-2024 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#include <microchip/ethernet/phy/api.h>
#include <mepa_driver.h>
#include <mepa_utils.h>
#include <phy_defines_macsec.h>
#include <string.h>
#include "lan8x8x_private.h"

#ifdef MEPA_lan8x8x_static_mem
static mepa_device_t lan8x8x_device[LAN8X8X_PHY_MAX];
static lan8x8x_data_t lan8x8x_data[LAN8X8X_PHY_MAX];
#endif

/**********************************
 * Internal APIs
 *********************************/
//Retrieve PHY information
static mepa_rc phy_get_device_info(mepa_device_t *const dev)
{
    lan8x8x_data_t *const data = (lan8x8x_data_t *const)dev->data;
    uint32_t id1 = 0, id2 = 0;
    uint16_t val = 0;

    //Init structure
    data->dev.id = ZERO;
    data->dev.model = ZERO;
    data->dev.rev = ZERO;
    data->dev.is_master = PHY_FALSE;
    data->dev.is_master_fault = PHY_FALSE;

    (void) phy_reg_rd(dev, MII_PHYSID1, &val);
    id1 = val;

    val = 0;
    (void) phy_reg_rd(dev, MII_PHYSID2, &val);
    id2 = val;

    //Setup structure
    data->dev.rev   = (uint16_t)GET_PHY_REV(id2);
    data->dev.model = (uint16_t)GET_PHY_MODEL(id2);
    // PHY_ID_NUM0: Assigned to the 3rd through 18th bits of the Organizationally Unique Identifier (OUI), respectively.
    data->dev.id = GET_PHY_ID1(id1);
    // PHY_ID_NUM1: Assigned to the 19th through 24th bits of the OUI.
    data->dev.id |= GET_PHY_ID2(id2);

    return MEPA_RC_OK;
}

static void lan8x8x_read_capabilities(mepa_device_t *const dev)
{
    lan8x8x_data_t *const priv = (lan8x8x_data_t *const)dev->data;
    uint16_t val = 0U;

    // Model/Part ID from OTP
    (void) phy_mmd_reg_rd(dev, MDIO_MMD_VEND1, T1_OTP_RO_PART_ID, &val);
    priv->dev.part_id = val;
    T_I(MEPA_TRACE_GRP_GEN, "PHY port=%u part_id 0x%x!\n", priv->port_no, priv->dev.part_id);

    //set mac-interface
    priv->mac_if = MESA_PORT_INTERFACE_RGMII_RXID;
    priv->t1_cap.is_sgmii = PHY_FALSE;
    if ((val == 0x8782U) || (val == 0x878BU) ||
        (val == 0x8882U) || (val == 0x888BU) ||
        (val == 0x8884U) || (val == 0x888DU)) {
        priv->t1_cap.is_sgmii = PHY_TRUE;
        priv->mac_if = MESA_PORT_INTERFACE_SGMII;
        T_I(MEPA_TRACE_GRP_GEN, "PHY port=%u mac_mode is SGMII!\n", priv->port_no);
    }

    //Read Feature disable from OTP
    (void) phy_mmd_reg_rd(dev, MDIO_MMD_VEND1, T1_OTP_RO_FEAT_DIS, &val);
    priv->t1_cap.dis_1000 = PHY_FALSE;
    if ((IS_LAN888X(dev->drv->id) == ONE) &&
        ((val & T1_OTP_RO_FEAT_DIS_1000M) != 0U)) {
        priv->t1_cap.dis_1000 = PHY_TRUE;
    }
    priv->t1_cap.dis_100    = PHY_FALSE;
    if ((val & T1_OTP_RO_FEAT_DIS_100M) != 0U) {
        priv->t1_cap.dis_100    = PHY_TRUE;
    }
    priv->t1_cap.dis_macsec = PHY_FALSE;
    if ((val & T1_OTP_RO_FEAT_DIS_MS) != 0U) {
        priv->t1_cap.dis_macsec = PHY_TRUE;
    }

    //Read STRAPs
    (void) phy_mmd_reg_rd(dev, MDIO_MMD_VEND1, OTP_STRAP_OVERRIDE, &val);
    if ((val & OTP_STRAP_OVERRIDE_EN) != 0U) {
        if ((val & OTP_STRAP_AUTO_NEG_EN) != 0U) {
            priv->conf.speed = MESA_SPEED_AUTO;
        } else {
            priv->conf.speed = MESA_SPEED_100M;
            if ((IS_LAN888X(dev->drv->id) == ONE) &&
                ((val & OTP_STRAP_SPEED_SEL) != 0U)) {
                priv->conf.speed = MESA_SPEED_1G;
            }
        }

        priv->conf.man_neg = MEPA_MANUAL_NEG_CLIENT;
        if ((val & OTP_STRAP_MST_SLV_SEL) != 0U) {
            priv->conf.man_neg = MEPA_MANUAL_NEG_REF;
        }
    } else {
        (void) phy_mmd_reg_rd(dev, MDIO_MMD_VEND1, OTP_STRAP_READ_REG, &val);
        if ((val & OTP_STRAP_READ_AUTO_NEG_EN) != 0U) {
            priv->conf.speed = MESA_SPEED_AUTO;
            //FIXME: LAN878X aneg is not working
            if (IS_LAN878X(dev->drv->id) == ONE) {
                priv->conf.speed = MESA_SPEED_100M;
            }
        } else {
            priv->conf.speed = MESA_SPEED_100M;
            if ((IS_LAN888X(dev->drv->id) == ONE) &&
                ((val & OTP_STRAP_SPEED_SEL) != 0U)) {
                priv->conf.speed = MESA_SPEED_1G;
            }
        }

        priv->conf.man_neg = MEPA_MANUAL_NEG_CLIENT;
        if ((val & OTP_STRAP_READ_MST_SLV_SEL) != 0U) {
            priv->conf.man_neg = MEPA_MANUAL_NEG_REF;
        }
    }
}

static mepa_rc lan8x8x_setup_lpbk(mepa_device_t *const dev,
                                  const mepa_loopback_t *loopback)
{
    lan8x8x_data_t *const data = (lan8x8x_data_t *const)dev->data;
    mepa_rc rc = MEPA_RC_OK;
    uint16_t reg = CHIPTOP_H3P_LPBK;
    // misra_c_2025_rule_2_5_violation
    uint16_t val = 0U;

    (void) memset(&data->loopback, 0, sizeof(mepa_loopback_t));

    //if loopback == NULL, it could be during init clear loopback case
    if (loopback != NULL) {
        if (loopback->near_end_ena == PHY_TRUE) {
            reg = CHIPTOP_H3P_LPBK;
            val = H3P_LPBK;
            data->loopback.near_end_ena = PHY_TRUE;
        } else if (loopback->far_end_ena == PHY_TRUE) {
            reg = CHIPTOP_L3P_LPBK;
            val = L3P_LPBK;
            data->loopback.far_end_ena = PHY_TRUE;
        } else {
            //misra_c_2023_rule_15_7_violation
            val = 0;
        }
    }

    if (val == 0U) {
        // Disable loopback
        rc =  phy_mmd_reg_wr(dev, MDIO_MMD_VEND1, CHIPTOP_H3P_LPBK, val);
        if (rc == MEPA_RC_OK) {
            rc = phy_mmd_reg_wr(dev, MDIO_MMD_VEND1, CHIPTOP_L3P_LPBK, val);
        }
    } else {
        // Enable loopback
        rc = phy_mmd_reg_wr(dev, MDIO_MMD_VEND1, reg, val);
    }
    return rc;
}

struct lan8x8x_regwr_map {
    u8  mmd;
    u16 reg;
    u32 val;
    u8 is32;
    u8 isrd;
};

static mepa_rc lan8x8x_phy_regsetup(mepa_device_t *dev,
                                    const struct lan8x8x_regwr_map *reg_map, u8 cnt)
{
    mepa_rc rc = MEPA_RC_OK;

    for (u8 i = 0; i < cnt; i++) {
        if (reg_map[i].is32 == 1U) {
            uint32_t val;

            if (reg_map[i].isrd == 1U) {
                rc = phy_mmd_reg_rd32(dev, reg_map[i].mmd,
                                      reg_map[i].reg, &val);
                if (rc != MEPA_RC_OK) {
                    return rc;
                }
            } else {
                rc = phy_mmd_reg_wr32(dev, reg_map[i].mmd,
                                      reg_map[i].reg, reg_map[i].val);
                if (rc != MEPA_RC_OK) {
                    return rc;
                }
            }
        } else {
            uint16_t val;

            if (reg_map[i].isrd == 1U) {
                rc = phy_mmd_reg_rd(dev, reg_map[i].mmd,
                                    reg_map[i].reg, &val);
                if (rc != MEPA_RC_OK) {
                    return rc;
                }
            } else {
                val = (uint16_t)reg_map[i].val;

                rc = phy_mmd_reg_wr(dev, reg_map[i].mmd,
                                    reg_map[i].reg, val);
                if (rc != MEPA_RC_OK) {
                    return rc;
                }
            }
        }
    }

    return rc;
}

static mepa_rc lan8x8x_sgmii_init(mepa_device_t *dev)
{
    mepa_rc rc = MEPA_RC_OK;
    const lan8x8x_data_t *const data = (const lan8x8x_data_t *const)dev->data;

    //Setup QSGMII
    rc = phy_mmd_reg_mod32(dev, MDIO_MMD_VEND1,
                           SERDES_TXPLL_REFCLK_CTRL_0,
                           0x0U, SERDES_TXPLL_REFCLK_SET);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    rc = phy_mmd_reg_mod32(dev, MDIO_MMD_VEND1,
                           SERDES_TXPLL_CONTROL_0,
                           SERDES_TXPLL_CTL_TXPLL_PD, 0x0U);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    rc = phy_mmd_reg_wr32(dev, MDIO_MMD_VEND1,
                          SERDES_LANEA_TXPWR_CTRL_0,
                          SERDES_LANEA_TXPWR_CTRL_TXPCLK_ENA);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    rc = phy_mmd_reg_mod32(dev, MDIO_MMD_VEND1,
                           SERDES_LANEA_POWERDOWN_0,
                           SERDES_LANEA_POWERDOWN_SET, 0x0U);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    rc = phy_mmd_reg_wr32(dev, MDIO_MMD_VEND1, SERDES_TOP_PD_RST_0, 0x0U);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    if (data->conf.mac_if_aneg_ena) {
        rc = phy_mmd_reg_wr(dev, MDIO_MMD_VEND1, QSGMII_ANEG_EN_REG,
                            (QSGMII_ANEG_CFG | QSGMII_AUTO_ANEG_EN));
        if (rc != MEPA_RC_OK) {
            return rc;
        }

        rc = phy_mmd_reg_wr(dev, MDIO_MMD_VEND1, QSGMII_PCS1G_ANEG_CONFIG,
                            (QSGMII_PCS1G_ANEG_CFG_EN | QSGMII_PCS1G_ANEG_ENA));
        if (rc != MEPA_RC_OK) {
            return rc;
        }
    } else {
        rc = phy_mmd_reg_wr(dev, MDIO_MMD_VEND1, QSGMII_ANEG_EN_REG,
                            QSGMII_ANEG_CFG);
        if (rc != MEPA_RC_OK) {
            return rc;
        }

        rc = phy_mmd_reg_wr(dev, MDIO_MMD_VEND1, QSGMII_PCS1G_ANEG_CONFIG,
                            QSGMII_PCS1G_ANEG_CFG_EN);
        if (rc != MEPA_RC_OK) {
            return rc;
        }
    }

    return phy_mmd_reg_wr(dev, MDIO_MMD_VEND1, QSGMII_PCS1G_CONFIG,
                          QSGMII_PCS1G_CFG_EN);
}

static mepa_rc lan8x8x_rgmii_init(mepa_device_t *dev)
{
    const lan8x8x_data_t *const data = (const lan8x8x_data_t *const)dev->data;
    mepa_rc rc = MEPA_RC_OK;
    uint16_t txc = 0;
    uint16_t rxc = 0;

    switch (data->mac_if) {
    case MESA_PORT_INTERFACE_RGMII:
        txc &= ((~LAN8X8X_RGMII_DLL_CONF) & 0xFFFFU);
        rxc &= ((~LAN8X8X_RGMII_DLL_CONF) & 0xFFFFU);
        break;
    case MESA_PORT_INTERFACE_RGMII_ID:
        txc |= LAN8X8X_RGMII_DLL_CONF;
        rxc |= LAN8X8X_RGMII_DLL_CONF;
        break;
    case MESA_PORT_INTERFACE_RGMII_RXID:
        txc &= ((~LAN8X8X_RGMII_DLL_CONF) & 0xFFFFU);
        rxc |= LAN8X8X_RGMII_DLL_CONF;
        break;
    case MESA_PORT_INTERFACE_RGMII_TXID:
        txc |= LAN8X8X_RGMII_DLL_CONF;
        rxc &= ((~LAN8X8X_RGMII_DLL_CONF) & 0xFFFFU);
        break;
    default:
        rc = MEPA_RC_ERR_KR_CONF_NOT_SUPPORTED;
        break;
    }

    if (rc == MEPA_RC_OK) {
        // Set RX DELAY
        rc = phy_mmd_reg_modify(dev, MDIO_MMD_VEND1,
                                LAN8X8X_RGMII_RX_DLL_CFG,
                                LAN8X8X_RGMII_DLL_CONF, rxc);
        if (rc != MEPA_RC_OK) {
            return rc;
        }
        // Set TX DELAY
        rc = phy_mmd_reg_modify(dev, MDIO_MMD_VEND1,
                                LAN8X8X_RGMII_TX_DLL_CFG,
                                LAN8X8X_RGMII_DLL_CONF, txc);
        if (rc != MEPA_RC_OK) {
            return rc;
        }
    }

    return rc;
}

static mepa_rc lan8x8x_config_mac(mepa_device_t *dev)
{
    const lan8x8x_data_t *const data = (const lan8x8x_data_t *const)dev->data;
    uint16_t clk = 0U;
    mepa_rc rc;

    if (data->mac_if == MESA_PORT_INTERFACE_SGMII) {
        if (!data->t1_cap.is_sgmii) {
            return MEPA_RC_INV_STATE;
        }
        rc = lan8x8x_sgmii_init(dev);
        clk = 0xc0U;
    } else {
        if (data->t1_cap.is_sgmii) {
            return MEPA_RC_INV_STATE;
        }
        rc = lan8x8x_rgmii_init(dev);
    }
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    //UNG_EWOOD-599: clock source setting as workaround for SGMII issue
    return phy_mmd_reg_wr(dev, MDIO_MMD_VEND1, SPARE_REG_1, clk);
}

static mepa_rc lan8x8x_phy_init(mepa_device_t *const dev)
{
    mepa_rc rc;

    if (IS_LAN888X(dev->drv->id) == ONE) {
        rc = phy_mmd_reg_mod32(dev, MDIO_MMD_PCS, T1_1G_E1000T1_PCS_EN_REG,
                               T1_1G_E1000T1_PCS_EN_, T1_1G_E1000T1_PCS_EN_);
    } else {
        rc = phy_mmd_reg_wr32(dev, MDIO_MMD_PCS, T1_1G_E1000T1_PCS_EN_REG, 0);
    }
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    //MAC Setup
    rc = lan8x8x_config_mac(dev);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    T_I(MEPA_TRACE_GRP_GEN, "phy_init complete!!\r\n");

    return MEPA_RC_OK;
}

static mepa_rc lan8x8x_check_media(const mepa_device_t *const dev,
                                   mepa_media_interface_t media_if,
                                   mesa_port_speed_t speed)
{
    const lan8x8x_data_t *const data = (const lan8x8x_data_t *const)dev->data;
    mepa_rc rc = MEPA_RC_ERR_KR_CONF_NOT_SUPPORTED;

    if (((media_if == MESA_PHY_MEDIA_IF_T1_100FX) && (!data->t1_cap.dis_100)) ||
        (((media_if == MESA_PHY_MEDIA_IF_T1_1000FX) && (!data->t1_cap.dis_1000)) &&
         (IS_LAN888X(dev->drv->id) == ONE))) {

        rc = MEPA_RC_OK;
        // speed selection based on media type
        if (media_if == MESA_PHY_MEDIA_IF_T1_100FX) {
            if (((speed == MESA_SPEED_AUTO) &&
                 (data->conf.aneg.speed_100m_fdx != PHY_TRUE)) ||
                ((speed != MESA_SPEED_AUTO) &&
                 (speed != MESA_SPEED_100M))) {
                rc = MEPA_RC_ERR_PARM;
            }
        }
    }

    T_I(MEPA_TRACE_GRP_GEN,   "phy_id=0x%x: media=%d, speed=%d, rc=%d\n",
        dev->drv->id, media_if, speed, rc);
    return rc;
}

static mepa_rc lan8x8x_int_events_set(mepa_device_t *dev,
                                      mepa_event_t event,
                                      mesa_bool_t enable)
{
    lan8x8x_data_t *const data = (lan8x8x_data_t *const)dev->data;
    uint16_t en0_mask = 0, en1_mask = 0;
    uint16_t macsec_mask = 0;
    uint32_t tmp_evt = event;
    uint16_t int_mask = 0;
    uint32_t i;
    mepa_rc rc;

    data->events = enable ? (data->events | tmp_evt) : (data->events & ~tmp_evt);
    for (i = 0; i < (sizeof(mepa_event_t) * 8U); i++) {
        uint32_t mask = (uint32_t)BIT32(i);

        switch (data->events & mask) {
        case MESA_PHY_LINK_SPEED_STATE_CHANGE_EV:
            tmp_evt &= ~mask;
            int_mask |= LAN8X8X_INT_LINK_CHANGE;
            int_mask |= LAN8X8X_INT_LINK_CHANGE_1G;
            en0_mask |= LAN8X8X_INT_EN0_T1_DATA_FAULT;
            break;
        case MESA_PHY_LINK_AUTO_NEG_COMPLETE_EV:
            tmp_evt &= ~mask;
            int_mask |= LAN8X8X_INT_MS_TRAINING_COMP;
            en0_mask |= LAN8X8X_INT_EN0_T1_DATA_FAULT;
            break;
        case MESA_PHY_LINK_EXT_TC10_SLEEP_FAIL:
            tmp_evt &= ~mask;
            int_mask |= LAN8X8X_INT_SLEEP_FAIL;
            en0_mask |= (LAN8X8X_INT_EN0_T1_DATA_FAULT |
                         LAN8X8X_INT_EN0_TC10_PRT);
            break;
        case MESA_PHY_LINK_EXT_TC10_SLEEP_MODE:
            tmp_evt &= ~mask;
            int_mask |= LAN8X8X_INT_SLEEP_MODE;
            en0_mask |= (LAN8X8X_INT_EN0_T1_DATA_FAULT |
                         LAN8X8X_INT_EN0_TC10_PRT);
            break;
        case MESA_PHY_LINK_TX_FIFO_OVERFLOW_INT_EV:
        case MESA_PHY_LINK_RX_FIFO_OVERFLOW_INT_EV:
            tmp_evt &= ~mask;
            en1_mask |= LAN8X8X_INT_EN1_PTP_PRT;
            break;
        default:
            //Not implemented
            tmp_evt &= ~mask;
            break;
        }

        if (tmp_evt == ZERO) {
            break;
        }
    }

    //Enable T1 top level interrupts
    rc = phy_mmd_reg_wr(dev, MDIO_MMD_VEND1,
                        LAN8X8X_INT_EN0_SC, en0_mask);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    //Enable IRQ interrupts
    rc = phy_mmd_reg_wr32(dev, MDIO_MMD_PMAPMD,
                          LAN8X8X_INT_IRQ_FUNC_MSK, int_mask);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    rc = phy_mmd_reg_wr(dev, MDIO_MMD_VEND1,
                        LAN8X8X_INT_EN1_SC, macsec_mask | en1_mask);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    T_D(MEPA_TRACE_GRP_GEN, "set events=0x%x, int_mask=0x%x! rc=%d\r\n",
        data->events, int_mask, rc);

    return MEPA_RC_OK;
}

static mepa_rc lan8x8x_config_led(mepa_device_t *const dev,
                                  const mepa_gpio_conf_t *conf)
{
    mepa_rc rc = MEPA_RC_OK;
    uint16_t reg_offset = 0;
    uint16_t reg_mask;
    uint16_t reg_val = LAN8X8X_LED_LINK_ACT_ANY_SPEED;

    switch (conf->mode) {
    case MEPA_GPIO_MODE_LED_LINK1000_ACTIVITY:
        reg_val = LAN8X8X_LED_LINK_ACT_1000_SPEED;
        break;
    case MEPA_GPIO_MODE_LED_LINK100_ACTIVITY:
        reg_val = LAN8X8X_LED_LINK_ACT_100_SPEED;
        break;
    case MEPA_GPIO_MODE_LED_LINK_NO_ACT_ANY_SPEED:
        reg_val = LAN8X8X_LED_LINK_NO_ACT_ANY_SPEED;
        break;
    case MEPA_GPIO_MODE_LED_LOCAL_RXER_STATUS:
        reg_val = LAN8X8X_LED_LOCAL_RXER_STATUS;
        break;
    case MEPA_GPIO_MODE_LED_REMOTE_RXER_STATUS:
        reg_val = LAN8X8X_LED_REMOTE_RXER_STATUS;
        break;
    case MEPA_GPIO_MODE_LED_NEGOTIATED_SPEED:
        reg_val = LAN8X8X_LED_NEGOTIATED_SPEED;
        break;
    case MEPA_GPIO_MODE_LED_MASTER_SLAVE_MODE:
        reg_val = LAN8X8X_LED_MASTER_SLAVE_MODE;
        break;
    case MEPA_GPIO_MODE_LED_PCS_TX_ERR_STATUS:
        reg_val = LAN8X8X_LED_PCS_TX_ERR_STATUS;
        break;
    case MEPA_GPIO_MODE_LED_PCS_RX_ERR_STATUS:
        reg_val = LAN8X8X_LED_PCS_RX_ERR_STATUS;
        break;
    case MEPA_GPIO_MODE_LED_PCS_TX_ACTIVITY:
        reg_val = LAN8X8X_LED_PCS_TX_ACTIVITY;
        break;
    case MEPA_GPIO_MODE_LED_PCS_RX_ACTIVITY:
        reg_val = LAN8X8X_LED_PCS_RX_ACTIVITY;
        break;
    case MEPA_GPIO_MODE_LED_WAKE_ON_LAN:
        reg_val = LAN8X8X_LED_WAKE_ON_LAN;
        break;
    case MEPA_GPIO_MODE_LED_FORCE_LED_OFF:
        reg_val = LAN8X8X_LED_FORCED_LED_OFF;
        break;
    case MEPA_GPIO_MODE_LED_FORCE_LED_ON:
        reg_val = LAN8X8X_LED_FORCED_LED_ON;
        break;
    case MEPA_GPIO_MODE_LED_LINK_ACTIVITY:
        reg_val = LAN8X8X_LED_LINK_ACT_ANY_SPEED;
        break;
    default:
        rc = MEPA_RC_ERR_KR_CONF_NOT_SUPPORTED;
        break;
    }

    if (rc == MEPA_RC_OK) {
        if ((conf->led_num == MEPA_LED0) || (conf->led_num == MEPA_LED1)) {
            reg_offset = LAN8X8X_COMM_LED1_LED0;
        } else if ((conf->led_num == MEPA_LED2) || (conf->led_num == MEPA_LED3)) {
            reg_offset = LAN8X8X_COMM_LED3_LED2;
        } else {
            rc = MEPA_RC_ERR_KR_CONF_NOT_SUPPORTED;
        }

        if (rc == MEPA_RC_OK) {
            reg_mask = LAN8X8X_COMM_LED1_LED3_M;
            if ((conf->led_num == MEPA_LED1) || (conf->led_num == MEPA_LED3)) {
                reg_val <<= 8U;
                reg_mask = LAN8X8X_COMM_LED2_LED4_M;
            }
            rc = phy_mmd_reg_modify(dev, MDIO_MMD_VEND1, LAN8X8X_COMMON_LED,
                                    LED_REG_COMMON_LED_BUF_TYPE_M,
                                    LED_REG_COMMON_LED_BUF_TYPE((uint8_t)conf->led_num));
            if (rc == MEPA_RC_OK) {
                rc = phy_mmd_reg_modify(dev, MDIO_MMD_VEND1, reg_offset,
                                        reg_mask, reg_val);
            }
        }
    }

    return rc;
}

static mepa_rc lan8x8x_config_leds(mepa_device_t *const dev, mepa_bool_t isolate)
{
    lan8x8x_data_t *data = (lan8x8x_data_t *) dev->data;
    mepa_gpio_conf_t conf[4];
    mepa_rc rc;
    int32_t i;

    for (i = 0; i < 4; i ++) {
        if ((data->led_conf[i].mode >= MEPA_GPIO_MODE_LED_LINK_ACTIVITY) &&
            (data->led_conf[i].mode <= MEPA_GPIO_MODE_LED_DISABLE_EXTENDED)) {
            conf[i].led_num = data->led_conf[i].led_num;
            if (isolate) {
                conf[i].mode = MEPA_GPIO_MODE_LED_FORCE_LED_OFF;
            } else {
                conf[i].mode = data->led_conf[i].mode;
            }
            rc = lan8x8x_config_led(dev, &conf[i]);
            if (rc != MEPA_RC_OK) {
                return rc;
            }
        }
    }

    return MEPA_RC_OK;
}

static mepa_rc lan8x8x_isolate_mode_int(struct mepa_device *dev, mepa_bool_t const en)
{
    mepa_rc rc;

    if (en) {
        rc = phy_reg_set_bits(dev, MII_BMCR, BMCR_ISOLATE);
    } else {
        rc = phy_reg_clear_bits(dev, MII_BMCR, BMCR_ISOLATE);
    }
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    //LED setup after isolate mode disabled
    return lan8x8x_config_leds(dev, en);
}

static mepa_rc lan8x8x_phy_cfg_clr(mepa_device_t *const dev)
{
    lan8x8x_data_t *data = (lan8x8x_data_t *) dev->data;
    uint16_t reg_val = 0;
    mepa_rc rc;

    //clear tc10 interrupt register
    rc = phy_mmd_reg_rd(dev, MDIO_MMD_VEND1, 0xc22U, &reg_val);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    //clear isolate bit
    rc = lan8x8x_isolate_mode_int(dev, PHY_FALSE);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    //clear loopback if it is setup in prev. config
    rc = lan8x8x_setup_lpbk(dev, NULL);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    //Setup events
    return lan8x8x_int_events_set(dev, data->events, PHY_TRUE);
}

static mepa_rc lan8x8x_phy_reset(mepa_device_t *dev)
{
    lan8x8x_data_t *const data = (lan8x8x_data_t *const)dev->data;
    mepa_rc rc;
    uint16_t val;
    uint32_t v32;

    // CL22 soft-reset to let the link re-train
    rc = phy_reg_set_bits(dev, MII_BMCR, BMCR_RESET);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    rc = phy_mmd_reg_poll(dev, 0U, MII_BMCR,
                          BMCR_RESET, BMCR_RESET,
                          0U, 4000U, &val);
    if (rc != MEPA_RC_OK) {
        T_D(MEPA_TRACE_GRP_GEN, "Reset failed(0x%x! rc=%d\r\n", val, rc);
    }

    //clear config
    rc = lan8x8x_phy_cfg_clr(dev);
    if (rc != MEPA_RC_OK) {
        T_D(MEPA_TRACE_GRP_GEN, "config clear failed! rc=%d\r\n", rc);
    }

    // disable aneg
    rc = phy_mmd_reg_clear_bits(dev, MDIO_MMD_AN, MDIO_AN_T1_CTRL,
                                MDIO_AN_CTRL1_ENABLE | MDIO_AN_CTRL1_RESTART);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    // Clear LINK_CONTROL and PHY_CONFIG_DONE
    rc = phy_mmd_reg_wr32(dev, MDIO_MMD_PMAPMD, T1_1G_TOP_CTRL_CONFIG, 0U);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    // Set SYSTEM_CONTROL_SOFT_RESET
    rc = phy_mmd_reg_wr32(dev, MDIO_MMD_PMAPMD, T1_1G_TOP_CTRL_CONFIG,
                          T1_1G_TOP_CTRL_SOFT_RESET);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    // Wait for RESET done
    rc = phy_mmd_reg_poll32(dev, MDIO_MMD_PMAPMD,
                            T1_1G_TOP_CTRL_CONFIG,
                            T1_1G_TOP_CTRL_SOFT_RESET,
                            T1_1G_TOP_CTRL_SOFT_RESET,
                            ZERO, 4000U, &v32);
    if (rc != MEPA_RC_OK) {
        T_D(MEPA_TRACE_GRP_GEN, "CHIP reset failed(0x%x! rc=%d\r\n", v32, rc);
    }

    data->init_done = PHY_FALSE;

    T_I(MEPA_TRACE_GRP_GEN, "reset complete!!\r\n");

    return MEPA_RC_OK;
}

static mepa_rc lan8x8x_gpio_mode_set_private(mepa_device_t *dev,
                                             const mepa_gpio_conf_t *const gpio_conf)
{
    mepa_rc rc;
    uint16_t dir, val = 0, gpio_no = gpio_conf->gpio_no;
    mepa_bool_t clk_en = PHY_FALSE, gpio_en = PHY_FALSE;
    mepa_gpio_mode_t mode = gpio_conf->mode;

    if ((mode == MEPA_GPIO_MODE_IN) || (mode == MEPA_GPIO_MODE_OUT)) {
        gpio_en = PHY_TRUE;
        dir = ((mode == MEPA_GPIO_MODE_OUT) ? 1U : 0U);
    } else if ((mode >= MEPA_GPIO_MODE_LED_LINK_ACTIVITY) &&
               (mode <= MEPA_GPIO_MODE_LED_DISABLE_EXTENDED)) {
        lan8x8x_data_t *const data = (lan8x8x_data_t *const)dev->data;
        rc = lan8x8x_config_led(dev, gpio_conf);
        if (rc != MEPA_RC_OK) {
            return rc;
        }

        //updata metadata
        (void) memcpy(&data->led_conf[gpio_conf->led_num], gpio_conf, sizeof(mepa_gpio_conf_t));
    } else if ((mode == MEPA_GPIO_MODE_PTP_REF_CLK) ||
               (mode == MEPA_GPIO_MODE_PTP_REF_ADJ)) {
        clk_en = PHY_TRUE;
        gpio_no = ((mode == MEPA_GPIO_MODE_PTP_REF_CLK) ? 1U : 2U);
    } else {
        // Event misra_c_2012_rule_15_7_violation:  No non-empty terminating "else" statement.
    }
    if (gpio_no < 3U) {
        if (gpio_en) {
            val = ((uint16_t)1U << (4U + gpio_no));
            dir = dir << gpio_no;
            rc = phy_mmd_reg_modify(dev, MDIO_MMD_VEND1, LAN8X8X_GPIO_DIR, dir, dir);
            if (rc != MEPA_RC_OK) {
                return rc;
            }
        } else if (clk_en) {
            val = ((uint16_t)1U << (8U + gpio_no));
        } else {
            // Event misra_c_2012_rule_15_7_violation: No non-empty terminating "else" statement.
        }
        rc = phy_mmd_reg_modify(dev, MDIO_MMD_VEND1, LAN8X8X_REG_CONTROL1,
                                ((gpio_en || clk_en) ? val : 0U), val);
        if (rc != MEPA_RC_OK) {
            return rc;
        }
    } else {
        rc = MEPA_RC_NOT_IMPLEMENTED;
    }

    return rc;
}

static void lan8x8x_macsec_disable(mepa_device_t *const dev)
{
    static const struct lan8x8x_regwr_map mac_dis[] = {
        // Bypass Egress
        {0x9U, 0x1880U, 0x103U, 1U, 0U},
        {0x9U, 0x1880U, 0x101U, 1U, 0U},
        // Bypass Ingress
        {0x8U, 0x1880U, 0x103U, 1U, 0U},
        {0x8U, 0x1880U, 0x101U, 1U, 0U},
        // Bypass MACsec mega block
        {MDIO_MMD_VEND1, XGMII_GMII_BYPASS, 0x1U, 0U, 0U},
    };

    (void) lan8x8x_phy_regsetup(dev, mac_dis, (uint8_t)(sizeof(mac_dis) / sizeof((mac_dis)[0])));
}

static mepa_rc lan8x8x_mac_init(mepa_device_t *dev, mepa_bool_t is_host, uint32_t base)
{
    uint8_t mmd = MMD_LINE_HOST;
    mepa_rc rc = MEPA_RC_ERROR;
    uint32_t val;

    //Host or Line MACSec block clock enable and sw_rst
    rc = phy_mmd_reg_wr32(dev, mmd,
                          MCHP_LH_MAC_ENA_CFG(base),
                          MCHP_LH_MAC_MAC_ENA_CFG_MS_RST);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    //Host or Line MACSec block clock enable and out of sw_rst
    rc = phy_mmd_reg_wr32(dev, mmd,
                          MCHP_LH_MAC_ENA_CFG(base),
                          MCHP_LH_MAC_MAC_ENA_CFG_MS_EN);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    //adv_chk
    rc = phy_mmd_reg_wr32(dev, mmd,
                          MCHP_LH_MAC_ADV_CHK_CFG(base),
                          MCHP_LH_MAC_MAC_ADV_CHK_INIT);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    //LFS Config
    rc = phy_mmd_reg_wr32(dev, mmd,
                          MCHP_LH_MAC_LFS_CFG(base), 0);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    //mac_maxlen
    rc = phy_mmd_reg_wr32(dev, mmd,
                          MCHP_LH_MAC_MAXLEN_CFG(base),
                          (MCHP_LH_MAC_MAXLEN_TAG_CHK |
                           (0xFF00U)));
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    //packet interface
    rc = phy_mmd_reg_wr32(dev, mmd,
                          MCHP_LH_MAC_PKTINF_CFG(base),
                          ((is_host) ?
                           (MCHP_LH_MAC_PKTINF_CFG_INIT |
                            MCHP_LH_MAC_PKTINF_CFG_ENABLE_TX_PADDING) :
                           MCHP_LH_MAC_PKTINF_CFG_INIT));
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    //Setup pause_cfg
    val = ((is_host) ? 0x34C6U : 0x2429U);
    rc = phy_mmd_reg_wr32(dev, mmd,
                          MCHP_LH_MAC_PAUSE_TX_FRAME_CTL(base),
                          MCHP_LH_MAC_TX_PAUSE_VALUE(val));
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    rc = phy_mmd_reg_wr32(dev, mmd,
                          MCHP_LH_MAC_PAUSE_TX_FRAME_CTL_2(base),
                          0xffffU);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    //mac_mode enable
    rc = phy_mmd_reg_wr32(dev, mmd,
                          MCHP_LH_MAC_MODE_CFG(base),
                          (MCHP_LH_MAC_MODE_UNDERSIZED_FRAME_DROP_DIS |
                           MCHP_LH_MAC_MODE_FORCE_CW_UPDATE_INTERVAL(64U) |
                           MCHP_LH_MAC_MODE_DISABLE_DIC));
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    if (is_host) {
        rc = phy_mmd_reg_wr32(dev, mmd,
                              MCHP_LH_MAC_NUM_TAGS_CFG(base),
                              0U);
        if (rc != MEPA_RC_OK) {
            return rc;
        }
    }

    return MEPA_RC_OK;
}

static mepa_rc lan8x8x_fc_buff_init(mepa_device_t *dev, uint32_t base)
{
    uint8_t mmd = MMD_FCB;
    mepa_rc rc = MEPA_RC_ERROR;
    uint32_t val = ZERO;

    rc = phy_mmd_reg_wr32(dev, mmd,
                          MCHP_MAC_FCB_FC_ENA_CFG(base),
                          MCHP_FCB_ENA_CFG_TX_ENA |
                          MCHP_FCB_ENA_CFG_RX_ENA);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    rc = phy_mmd_reg_wr32(dev, mmd,
                          MCHP_MAC_FCB_FC_MODE_CFG(base),
                          MCHP_FCB_MODE_CFG_TX_CTRL_QUEUE_ENA);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    rc = phy_mmd_reg_wr32(dev, mmd,
                          MCHP_MAC_FCB_PPM_RATE_ADAPT_THRESH_CFG(base),
                          (0x28U |
                           MCHP_FCB_PPM_RATE_ADAPT_THRESH_CFG_TX_OFFSET(val) |
                           MCHP_FCB_PPM_RATE_ADAPT_THRESH_CFG_RX_THRESH(0x32U)));
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    rc = phy_mmd_reg_wr32(dev, mmd,
                          MCHP_FCB_FC_STICKY_MASK(base),
                          MCHP_FCB_STICKY_MASK_SET);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    val = 0x0006U;
    return phy_mmd_reg_wr32(dev, mmd,
                            MCHP_MAC_FCB_FC_READ_THRESH_CFG(base),
                            (MCHP_FCB_FC_READ_THRESH_CFG_TX_THRESH(val) |
                             MCHP_FCB_FC_READ_THRESH_CFG_RX_THRESH(val)));
}

//one-time configuration to be done after CONFIG_DONE
static mepa_rc lan8x8x_onetime_post_config_done(mepa_device_t *const dev)
{
    lan8x8x_data_t *const data = (lan8x8x_data_t *const)dev->data;
    mepa_rc rc;

    if (!data->init_done) {
        rc = lan8x8x_mac_init(dev, PHY_TRUE, T1_BASE_HOST);
        if (rc != MEPA_RC_OK) {
            return rc;
        }

        rc = lan8x8x_mac_init(dev, PHY_FALSE, T1_BASE_LINE);
        if (rc != MEPA_RC_OK) {
            return rc;
        }

        rc = lan8x8x_fc_buff_init(dev, T1_BASE_FCB);
        if (rc != MEPA_RC_OK) {
            return rc;
        }

        lan8x8x_macsec_disable(dev);

        //MAC Setup - SGMII-aneg must be initiated post T1 link-up
        rc = lan8x8x_config_mac(dev);
        if (rc != MEPA_RC_OK) {
            return rc;
        }

        data->init_done = PHY_TRUE;
    }

    return MEPA_RC_OK;
}

static mepa_rc lan8x8x_config_done(mepa_device_t *const dev, mepa_bool_t skip_config)
{
    mepa_rc rc;

    // Enable LINK_CONTROL + CONFIG_DONE
    rc = phy_mmd_reg_wr32(dev, MDIO_MMD_PMAPMD, T1_1G_TOP_CTRL_CONFIG,
                          T1_1G_TOP_CTRL_CONFIG_SET);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    if (!skip_config) {
        rc = lan8x8x_onetime_post_config_done(dev);
        if (rc != MEPA_RC_OK) {
            return rc;
        }
    }

    return MEPA_RC_OK;
}

static mepa_bool_t lan8x8x_speed_config(mepa_device_t *const dev, mesa_port_speed_t speed)
{
    mepa_bool_t valid = PHY_TRUE;

    if (speed == MEPA_SPEED_100M) {
        (void) phy_mmd_reg_wr32(dev, MDIO_MMD_PMAPMD,
                                T1_1G_E100T1_PMD_ADPLL_CFG_0, 0x600E2804U);

        // AFE Config
        (void) phy_mmd_reg_wr32(dev, MDIO_MMD_PMAPMD,
                                T1_1G_E100T1_PMA_ADFE_CFG2, 0x14F4040CU);

        (void) phy_mmd_reg_wr32(dev, MDIO_MMD_PMAPMD,
                                T1_1G_E100T1_PMA_ADFE_CFG3, 0x43U);
    } else if (speed == MEPA_SPEED_1G) {
        (void) phy_mmd_reg_wr32(dev, MDIO_MMD_PMAPMD,
                                T1_1G_E1000T1_PMD_LCPLL_CFG_0, 0x5FE34C08U);

        (void) phy_mmd_reg_wr32(dev, MDIO_MMD_PMAPMD,
                                T1_1G_E1000T1_PMA_ADFE_CFG3, 0x1043U);
    } else {
        valid = PHY_FALSE;
    }

    return valid;
}

static mepa_rc lan8x8x_phy_config(mepa_device_t *const dev, mesa_port_speed_t speed, mepa_bool_t skip_config)
{
    if (lan8x8x_speed_config(dev, speed)) {
        return lan8x8x_config_done(dev, skip_config);
    }

    return MEPA_RC_OK;
}

static mepa_rc lan8x8x_pma_baset1_setup_forced(mepa_device_t *const dev)
{
    lan8x8x_data_t *data = (lan8x8x_data_t *)dev->data;
    uint16_t reg_val = 0;

    // mode configuration
    if (data->conf.man_neg == MEPA_MANUAL_NEG_REF) {
        reg_val |= MDIO_PMA_PMD_BT1_CTRL_CFG_MST;
    }

    // speed configuration
    if ((data->media_intf != MESA_PHY_MEDIA_IF_T1_100FX) &&
        (data->conf.speed == MESA_SPEED_1G)) {
        reg_val |= LAN8X8X_PMA_COMM_100T1_CTL_T1_TYPE_1000;
    }

    return phy_mmd_reg_wr(dev, MDIO_MMD_PMAPMD, MDIO_PMA_PMD_BT1_CTRL, reg_val);
}

static mepa_rc lan8x8x_pma_baset1_setup_aneg(mepa_device_t *const dev)
{
    lan8x8x_data_t *data = (lan8x8x_data_t *)dev->data;
    mepa_rc rc;
    u16 adv_r1 = 0;
    u16 adv_r2_mask = 0;
    u16 adv_r2 = 0;

    adv_r2_mask = (MDIO_AN_T1_ADV_M_B1000 |
                   MDIO_AN_T1_ADV_M_B100 |
                   MDIO_AN_T1_ADV_M_MST);

    // Advertise preferred master/slave mode
    if (data->conf.man_neg != MEPA_MANUAL_NEG_CLIENT) {
        adv_r2 |= MDIO_AN_T1_ADV_M_MST;
    }

    // Advertise 1G and 100M speed
    if ((data->media_intf != MESA_PHY_MEDIA_IF_T1_100FX)
        && (data->conf.aneg.speed_1g_fdx == PHY_TRUE)) {
        adv_r2 |= MDIO_AN_T1_ADV_M_B100 | MDIO_AN_T1_ADV_M_B1000;
    } else {
        adv_r2 |= MDIO_AN_T1_ADV_M_B100;
    }

    rc = phy_mmd_reg_modify(dev, MDIO_MMD_AN, MDIO_AN_T1_ADV_M,
                            adv_r2_mask, adv_r2);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    /* NOTE:
     *  The Base Page value is transferred to mr_adv_ability when register 7.514 is written.
     *  Therefore, registers 7.515 and 7.516 should be written before 7.514.
     */
    rc = phy_mmd_reg_wr(dev, MDIO_MMD_AN, MDIO_AN_T1_ADV_L, adv_r1);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    return phy_mmd_reg_set_bits(dev, MDIO_MMD_AN, MDIO_AN_T1_CTRL,
                                MDIO_AN_CTRL1_ENABLE | MDIO_AN_CTRL1_RESTART);
}

static mepa_rc lan8x8x_phy_setup(mepa_device_t *const dev)
{
    mepa_rc rc;
    lan8x8x_data_t *const data = (lan8x8x_data_t *const)dev->data;

    rc = phy_get_device_info(dev);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    rc = lan8x8x_phy_reset(dev);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    rc = lan8x8x_phy_init(dev);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    if (data->conf.speed == MESA_SPEED_AUTO) {
        rc = lan8x8x_pma_baset1_setup_aneg(dev);
        if (rc != MEPA_RC_OK) {
            return rc;
        }

        rc = lan8x8x_config_done(dev, PHY_FALSE);
        if (rc != MEPA_RC_OK) {
            return rc;
        }
    } else {
        rc = lan8x8x_pma_baset1_setup_forced(dev);
        if (rc != MEPA_RC_OK) {
            return rc;
        }

        rc = lan8x8x_phy_config(dev, data->conf.speed, PHY_FALSE);
        if (rc != MEPA_RC_OK) {
            return rc;
        }
    }

    T_I(MEPA_TRACE_GRP_GEN,   "PHY port=%u setup complete!\n", data->port_no);

    return MEPA_RC_OK;
}

static mepa_rc lan8x8x_config_set(mepa_device_t *dev, const mepa_conf_t *config)
{
    mepa_rc rc;
    lan8x8x_data_t *const data = (lan8x8x_data_t *const)dev->data;
    mepa_bool_t re_config = PHY_FALSE;

    T_I(MEPA_TRACE_GRP_GEN,   "PHY port=%u config_set start!\n", data->port_no);
    if (((config->man_neg != MEPA_MANUAL_NEG_CLIENT) &&
         (config->man_neg != MEPA_MANUAL_NEG_REF)) ||
        (config->fdx != PHY_TRUE) ||
        ((config->speed != MESA_SPEED_100M) &&
         (config->speed != MESA_SPEED_1G) &&
         (config->speed != MESA_SPEED_AUTO))) {
        rc = MEPA_RC_ERR_PARM;
    } else {
        mesa_port_speed_t speed = config->speed;

        data->conf.fdx = PHY_TRUE;
        data->conf.flow_control = config->flow_control;
        data->conf.aneg.speed_100m_fdx = config->aneg.speed_100m_fdx;
        data->conf.aneg.speed_1g_fdx = config->aneg.speed_1g_fdx;
        data->conf.mac_if_aneg_ena = config->mac_if_aneg_ena;

        // Setup Speed
        if (data->conf.speed != speed) {
            //check media/speed/aneg capabilities
            rc = lan8x8x_check_media(dev, data->media_intf, speed);
            if (rc != MEPA_RC_OK) {
                return rc;
            }

            re_config = PHY_TRUE;
            data->conf.speed = speed;
        }

        // Setup Master/Slave Mode
        if (data->conf.man_neg != config->man_neg) {
            re_config = PHY_TRUE;
            data->conf.man_neg = config->man_neg;
        }

        // handle admin enable/disable
        if (data->conf.admin.enable != config->admin.enable) {
            re_config = PHY_TRUE;
            data->conf.admin.enable = config->admin.enable;
        }

        if (re_config == PHY_TRUE) {
            //config change and admin enable
            if (data->conf.admin.enable == PHY_TRUE) {
                //clear power down bit
                rc = phy_mmd_reg_clear_bits(dev, MDIO_MMD_PMAPMD,
                                            MDIO_CTRL1, BMCR_PDOWN);
                if (rc != MEPA_RC_OK) {
                    return rc;
                }

                //Setup PHY
                rc = lan8x8x_phy_setup(dev);
                if (rc != MEPA_RC_OK) {
                    return rc;
                }
            } else {
                //Power down
                rc = phy_mmd_reg_set_bits(dev, MDIO_MMD_PMAPMD,
                                          MDIO_CTRL1, BMCR_PDOWN);
                if (rc != MEPA_RC_OK) {
                    return rc;
                }
            }
        }

        rc = MEPA_RC_OK;
    }

    T_I(MEPA_TRACE_GRP_GEN,   "PHY port=%u config_set complete! rc=%d\n", data->port_no, rc);

    return rc;
}

static mepa_rc lan8x8x_aneg_resolve_master_slave(mepa_device_t *dev,
                                                 uint8_t *mode)
{
    uint32_t val;
    mepa_rc rc;

    rc = phy_mmd_reg_rd32(dev, MDIO_MMD_AN, T1_AUTONEG_STATUS, &val);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    *mode = MASTER_SLAVE_STATE_SLAVE;
    if ((val & T1_AUTONEG_MS_CONFIG_FAULT) != 0U) {
        *mode = MASTER_SLAVE_STATE_ERR;
    } else if ((val & T1_AUTONEG_CONFIG_AS_MASTER) != 0U) {
        *mode = MASTER_SLAVE_STATE_MASTER;
    } else {
        //misra_c_2023_rule_15_7_violation
    }

    return MEPA_RC_OK;
}

static mepa_rc lan8x8x_aneg_read_status(mepa_device_t *dev,
                                        mepa_status_t *status)
{
    mepa_rc rc;
    uint16_t val = 0, lp_l = 0, lp_m = 0;
    lan8x8x_data_t *data = (lan8x8x_data_t *)dev->data;

    status->aneg.obey_pause = PHY_FALSE;
    status->aneg.generate_pause = PHY_FALSE;
    status->link = PHY_FALSE;
    status->speed = MESA_SPEED_UNDEFINED;

    rc = phy_mmd_reg_rd(dev, MDIO_MMD_AN, MDIO_AN_T1_STAT, &val);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    if ((val & MDIO_AN_STAT1_COMPLETE) == ZERO) {
        T_D(MEPA_TRACE_GRP_GEN, "aneg is not completed(0x%x)\r\n", val);
    } else {
        T_I(MEPA_TRACE_GRP_GEN, "aneg is complete!\r\n");
        rc = phy_mmd_reg_rd(dev, MDIO_MMD_AN, MDIO_AN_T1_LP_L, &lp_l);
        if (rc != MEPA_RC_OK) {
            return rc;
        }

        rc = phy_mmd_reg_rd(dev, MDIO_MMD_AN, MDIO_AN_T1_LP_M, &lp_m);
        if (rc != MEPA_RC_OK) {
            return rc;
        }

        T_I(MEPA_TRACE_GRP_GEN, "aneg lp_l=0x%x, lp_m=0x%x\r\n", lp_l, lp_m);
        if (((lp_m & T1_LPA_1000FULL) == T1_LPA_1000FULL) &&
            (data->conf.aneg.speed_1g_fdx == PHY_TRUE)) {
            status->speed = MEPA_SPEED_1G;
        } else if (((lp_m & T1_LPA_100FULL) == T1_LPA_100FULL) &&
                   (data->conf.aneg.speed_100m_fdx == PHY_TRUE)) {
            status->speed = MEPA_SPEED_100M;
        } else {
            status->speed = MESA_SPEED_UNDEFINED;
        }
    }

    return rc;
}

static void lan8x8x_fill_probe_data(mepa_driver_t *drv,
                                    mepa_device_t *dev,
                                    lan8x8x_data_t *data,
                                    const mepa_callout_t MEPA_SHARED_PTR *callout,
                                    struct mepa_callout_ctx MEPA_SHARED_PTR *callout_ctx,
                                    const struct mepa_board_conf *const conf)
{
    data->ctx_status = PHY_TRUE;
    data->port_no = conf->numeric_handle;

    dev->numeric_handle  = conf->numeric_handle;
    dev->drv = drv;
    dev->data = (void *)data;
    dev->callout = callout;
    dev->callout_ctx = callout_ctx;
    data->init_done = PHY_FALSE;
    data->events = 0;
    //Default is preferred master when autoneg is enabled and forced master when aneg is disabled
    data->conf.admin.enable = PHY_TRUE;
    data->conf.fdx = PHY_TRUE;
    //mac-if aneg must be enabled always
    data->conf.mac_if_aneg_ena = PHY_TRUE;

    //Read OTP capabilities
    lan8x8x_read_capabilities(dev);

    //phy aneg
    data->conf.aneg.speed_1g_fdx = PHY_FALSE;
    data->conf.aneg.speed_100m_fdx = PHY_FALSE;
    if ((IS_LAN878X(dev->drv->id) == ONE) || !data->t1_cap.dis_100) {
        data->conf.aneg.speed_100m_fdx = PHY_TRUE;
        data->media_intf = MESA_PHY_MEDIA_IF_T1_100FX;
    }
    if ((IS_LAN888X(dev->drv->id) == ONE) && !data->t1_cap.dis_1000) {
        data->media_intf = MESA_PHY_MEDIA_IF_T1_1000FX;
        data->conf.aneg.speed_1g_fdx = PHY_TRUE;
    }

    //Default LED settings
    data->led_conf[MEPA_LED0].led_num = MEPA_LED0;
    data->led_conf[MEPA_LED0].mode = MEPA_GPIO_MODE_LED_LINK1000_ACTIVITY;
    data->led_conf[MEPA_LED1].led_num = MEPA_LED1;
    data->led_conf[MEPA_LED1].mode = MEPA_GPIO_MODE_LED_LINK100_ACTIVITY;
    data->led_conf[MEPA_LED2].led_num = MEPA_LED2;
    data->led_conf[MEPA_LED2].mode = MEPA_GPIO_MODE_LED_MASTER_SLAVE_MODE;
    data->led_conf[MEPA_LED3].led_num = MEPA_LED3;
    data->led_conf[MEPA_LED3].mode = MEPA_GPIO_MODE_LED_PCS_RX_ERR_STATUS;

    //Cable diag data reset
    data->cd_res.link = PHY_LINKDOWN;
    data->cd_res.length[0] = 0;
    data->cd_res.status[0] = MESA_VERIPHY_STATUS_UNKNOWN;

    (void) lan8x8x_phy_setup(dev);

    T_I(MEPA_TRACE_GRP_GEN,   "phy probe port=%u probed phy_id=0x%x\n",
        data->port_no, dev->drv->id);
}

static mepa_rc lan8x8x_delete(mepa_device_t *dev)
{
    mepa_rc rc = MEPA_RC_ERROR;

    if (dev != NULL) {

        //cleanup
#ifdef MEPA_lan8x8x_static_mem
        T_D(MEPA_TRACE_GRP_GEN,   "static driver cleanup!!\n");
        (void) memset(dev->data, 0, sizeof(lan8x8x_data_t));
        (void) memset(dev, 0, sizeof(mepa_device_t));
        rc = MEPA_RC_OK;
#else
        T_D(MEPA_TRACE_GRP_GEN,   "dynamic driver cleanup!!\n");
        rc = mepa_delete_int(dev);
#endif
    }

    return rc;
}

/**********************************
 * Internal APIs must be above this
 *********************************/

/**********************************
 * External driver callbacks
 *********************************/
static mepa_rc lan8x8x_sqi_read(mepa_device_t *dev, uint32_t *const value)
{
    mepa_rc rc = MEPA_RC_ERROR;
    uint32_t sqi_reg = LAN8X8X_SQI_1000_REG, sqi_val = 0;

    if ((dev != NULL) && (value != NULL)) {
        MEPA_ENTER(dev);

        const lan8x8x_data_t *const data = (const lan8x8x_data_t *const)dev->data;
        *value = 0;

        if (data->conf.speed == MESA_SPEED_100M) {
            sqi_reg = LAN8X8X_SQI_100_REG;
        }

        rc = phy_mmd_reg_rd32(dev, MDIO_MMD_PMAPMD, sqi_reg, &sqi_val);
        if (rc == MEPA_RC_OK) {
            *value = LAN8X8X_SQI_GET(sqi_val);
        }
        MEPA_EXIT(dev);
    }

    return rc;
}

static mepa_device_t *lan8x8x_probe(mepa_driver_t *drv,
                                    const mepa_callout_t    MEPA_SHARED_PTR *callout,
                                    struct mepa_callout_ctx MEPA_SHARED_PTR *callout_ctx,
                                    struct mepa_board_conf                  *conf)
{
    mepa_device_t   *dev = NULL;

    if (drv != NULL) {
        lan8x8x_data_t      *data = NULL;

#ifdef MEPA_lan8x8x_static_mem
        uint8_t pidx = 0;

        T_D(MEPA_TRACE_GRP_GEN,   "static driver create!!\n");
        for (pidx = 0; pidx < LAN8X8X_PHY_MAX; pidx++) {
            if (lan8x8x_data[pidx].ctx_status) {
                T_D(MEPA_TRACE_GRP_GEN, "LAN8X8X driver already @ idx=%d[port-%d]!!\n",
                    pidx, lan8x8x_device[pidx].numeric_handle);
                continue;
            }

            dev = &lan8x8x_device[pidx];
            data = &lan8x8x_data[pidx];

            T_D(MEPA_TRACE_GRP_GEN, "LAN8X8X driver probe @ idx=%d, port=%d!!\n",
                pidx, conf->numeric_handle);
            lan8x8x_fill_probe_data(drv, dev, data, callout, callout_ctx, conf);

            break;
        }
#else
        T_D(MEPA_TRACE_GRP_GEN,   "dynamic driver create!!\n");
        //MISRA C-2023 Rule 11.5 - use static pointer assigned during probe
        dev = mepa_create_int(drv, callout, callout_ctx, conf, (int32_t)(sizeof(lan8x8x_data_t)));

        if (dev != NULL) {
            data = dev->data;
            data->port_no = conf->numeric_handle;

            T_I(MEPA_TRACE_GRP_GEN, "\n lan8x8x created (%d) at %p data %p\n",
                conf->numeric_handle, (uintptr_t)dev, (uintptr_t)data);
            lan8x8x_fill_probe_data(drv, dev, data, callout, callout_ctx, conf);
        }
#endif
    }

    return dev;
}

static mepa_rc lan8x8x_conf_get(mepa_device_t *dev,
                                mepa_conf_t *const config)
{
    mepa_rc rc = MEPA_RC_ERROR;

    if ((dev != NULL) && (config != NULL)) {
        const lan8x8x_data_t *const data = (const lan8x8x_data_t *const)dev->data;

        rc = MEPA_RC_OK;
        MEPA_ENTER(dev);
        (void) memcpy(config, &(data->conf), sizeof(mepa_conf_t));
        MEPA_EXIT(dev);
    }

    return rc;
}

static mepa_rc lan8x8x_conf_set(mepa_device_t *dev, const mepa_conf_t *config)
{
    mepa_rc rc = MEPA_RC_ERROR;

    if ((dev != NULL) && (config != NULL)) {
        MEPA_ENTER(dev);
        rc = lan8x8x_config_set(dev, config);
        MEPA_EXIT(dev);
    }

    return rc;
}

static mepa_rc lan8x8x_reset(mepa_device_t *dev, const mepa_reset_param_t *rst_conf)
{
    mepa_rc rc = MEPA_RC_ERROR;
    mepa_bool_t is_reset = PHY_TRUE;

    if (dev != NULL) {
        lan8x8x_data_t *const data = (lan8x8x_data_t *const)dev->data;

        MEPA_ENTER(dev);
        if (rst_conf != NULL) {
            // reset points are not supported
            if ((rst_conf->reset_point != MEPA_RESET_POINT_DEFAULT) &&
                (rst_conf->reset_point != MEPA_RESET_POINT_PRE)) {
                rc = MEPA_RC_OK;
                is_reset = PHY_FALSE;
            } else {
                rc = lan8x8x_check_media(dev, rst_conf->media_intf, data->conf.speed);
                if (rc == MEPA_RC_OK) {
                    //re-configure media interface
                    data->media_intf = rst_conf->media_intf;
                    if ((rst_conf->media_intf == MESA_PHY_MEDIA_IF_T1_100FX) &&
                        (data->conf.speed != MESA_SPEED_AUTO)) {
                        data->conf.speed = MESA_SPEED_100M;
                    }
                } else {
                    //Invalid media type
                    is_reset = PHY_FALSE;
                }
            }
        }
        if (is_reset == PHY_TRUE) {
            rc = lan8x8x_phy_setup(dev);
        }

        MEPA_EXIT(dev);
    }

    return rc;
}

// read direct registers
static mepa_rc lan8x8x_reg_read(mepa_device_t *dev, uint32_t addr, uint16_t *const value)
{
    mepa_rc rc = MESA_RC_ERROR;

    if ((dev != NULL) && (value != NULL)) {
        MEPA_ENTER(dev);
        rc = phy_reg_rd(dev, PHY_REG_ADDR(addr), value);
        MEPA_EXIT(dev);
    }

    return rc;
}

// write direct registers
static mepa_rc lan8x8x_reg_write(mepa_device_t *dev, uint32_t addr, uint16_t value)
{
    mepa_rc rc = MESA_RC_ERROR;

    if (dev != NULL) {
        MEPA_ENTER(dev);
        rc = phy_reg_wr(dev, PHY_REG_ADDR(addr), value);
        MEPA_EXIT(dev);
    }

    return rc;
}

// read mmd registers
static mepa_rc lan8x8x_mmd_reg_read(mepa_device_t *dev,
                                    uint32_t addr, uint16_t *const value)
{
    mepa_rc rc = MESA_RC_ERROR;

    if ((dev != NULL) && (value != NULL)) {
        MEPA_ENTER(dev);
        rc = phy_mmd_reg_rd(dev, PHY_MMD_DEVAD(addr), PHY_REG_ADDR(addr), value);
        MEPA_EXIT(dev);
    }

    return rc;
}

// write mmd registers
static mepa_rc lan8x8x_mmd_reg_write(mepa_device_t *dev,
                                     uint32_t addr, uint16_t value)
{
    mepa_rc rc = MESA_RC_ERROR;

    if (dev != NULL) {
        MEPA_ENTER(dev);
        rc = phy_mmd_reg_wr(dev, PHY_MMD_DEVAD(addr), PHY_REG_ADDR(addr), value);
        MEPA_EXIT(dev);
    }

    return rc;
}

static mepa_rc lan8x8x_if_set(mepa_device_t *dev, mepa_port_interface_t mac_if)
{
    mepa_rc rc = MEPA_RC_ERROR;

    if (dev != NULL) {
        rc = MEPA_RC_ERR_KR_CONF_NOT_SUPPORTED;

        if (((mac_if >= MESA_PORT_INTERFACE_RGMII) &&
             (mac_if <= MESA_PORT_INTERFACE_RGMII_TXID)) ||
            (mac_if == MESA_PORT_INTERFACE_SGMII)) {
            lan8x8x_data_t *const data = (lan8x8x_data_t *const)dev->data;

            if (((data->t1_cap.is_sgmii) &&
                 (mac_if != MESA_PORT_INTERFACE_SGMII)) ||
                ((!data->t1_cap.is_sgmii) &&
                 (mac_if == MESA_PORT_INTERFACE_SGMII))) {
                return rc;
            }
            rc = MEPA_RC_OK;
            if (data->mac_if != mac_if) {
                MEPA_ENTER(dev);
                //Setup mac_if
                data->mac_if = mac_if;
                rc = lan8x8x_phy_setup(dev);
                MEPA_EXIT(dev);
            }
        }
    }

    return rc;
}

static mepa_rc lan8x8x_if_get(mepa_device_t *dev,
                              mepa_port_speed_t speed, mepa_port_interface_t *mac_if)
{
    mepa_rc rc = MESA_RC_ERROR;

    (void) speed;

    if ((dev != NULL) && (mac_if != NULL)) {
        const lan8x8x_data_t *const data = (const lan8x8x_data_t *const)dev->data;

        rc = MEPA_RC_OK;
        MEPA_ENTER(dev);
        *mac_if = data->mac_if;
        MEPA_EXIT(dev);

        T_D(MEPA_TRACE_GRP_GEN,   "Get MAC Interface type %d\r\n", *mac_if);
    }

    return rc;
}

static mepa_rc lan8x8x_media_set(mepa_device_t *dev,
                                 mepa_media_interface_t media_if)
{
    mepa_rc rc = MESA_RC_ERROR;

    if (dev != NULL) {
        rc = MEPA_RC_ERR_KR_CONF_NOT_SUPPORTED;
        if ((media_if == MESA_PHY_MEDIA_IF_T1_100FX) ||
            (media_if == MESA_PHY_MEDIA_IF_T1_1000FX)) {
            lan8x8x_data_t *const data = (lan8x8x_data_t *const)dev->data;

            rc = MEPA_RC_OK;
            MEPA_ENTER(dev);

            if (data->media_intf != media_if) {
                //check media/speed/aneg capabilities
                rc = lan8x8x_check_media(dev, media_if, data->conf.speed);
                if (rc == MEPA_RC_OK) {
                    data->media_intf = media_if;

                    /* re-configure media interface
                       when aneg = true, apply change
                       when aneg = false, then following
                       old_media_type new_media_type old_speed  new_speed
                       1000_t1         100_t1        1000       100    ==> reset
                       1000_t1         100_t1         100       100    ===> No reset
                       100t1           1000t1         100       100   ===> No reset */
                    if ((data->conf.speed != MESA_SPEED_100M) ||
                        (data->conf.speed == MESA_SPEED_AUTO)) {
                        rc = lan8x8x_phy_setup(dev);
                    }
                    T_D(MEPA_TRACE_GRP_GEN,   "Set media type %d! rc=%d.\r\n", media_if, rc);
                }
            }
            MEPA_EXIT(dev);
        }
    }

    return rc;
}

static mepa_rc lan8x8x_media_get(mepa_device_t *dev,
                                 mepa_media_interface_t *media_if)
{
    mepa_rc rc = MESA_RC_ERROR;

    if ((dev != NULL) && (media_if != NULL)) {
        const lan8x8x_data_t *const data = (const lan8x8x_data_t *const)dev->data;

        rc = MEPA_RC_OK;
        MEPA_ENTER(dev);
        *media_if = data->media_intf;
        MEPA_EXIT(dev);

        T_D(MEPA_TRACE_GRP_GEN,   "Get media type %d\r\n", *media_if);
    }

    return rc;
}

// Set gpio mode to input, output or alternate function
static mepa_rc lan8x8x_gpio_mode_set(mepa_device_t *dev, const mepa_gpio_conf_t *gpio_conf)
{
    mepa_rc rc = MEPA_RC_ERROR;

    if ((dev != NULL) && (gpio_conf != NULL)) {
        MEPA_ENTER(dev);
        rc = lan8x8x_gpio_mode_set_private(dev, gpio_conf);
        MEPA_EXIT(dev);
    }

    return rc;
}

static mepa_rc lan8x8x_gpio_out_set(mepa_device_t *dev, uint8_t gpio_no, mepa_bool_t value)
{
    uint16_t val = 0;
    mepa_rc rc = MEPA_RC_ERROR;

    if (dev != NULL) {
        MEPA_ENTER(dev);
        if (gpio_no < 3U) {
            val = ((uint16_t)1U << (gpio_no));
            rc = phy_mmd_reg_modify(dev, MDIO_MMD_VEND1, LAN8X8X_GPIO_DATA, value ? val : 0U, val);
        } else {
            T_W(MEPA_TRACE_GRP_GEN,  "Not valid gpio on LAN8x8x phy");
        }
        MEPA_EXIT(dev);
    }

    return rc;
}

static mepa_rc lan8x8x_gpio_in_get(mepa_device_t *dev, uint8_t gpio_no, mepa_bool_t *const value)
{
    uint16_t val = 0;
    mepa_rc rc = MEPA_RC_ERROR;

    if (dev != NULL) {
        MEPA_ENTER(dev);
        if (gpio_no < 3U) {
            rc = phy_mmd_reg_rd(dev, MDIO_MMD_VEND1, LAN8X8X_GPIO_DATA, &val);
            *value = ((((val >> gpio_no) & ONE) != ZERO) ? PHY_TRUE : PHY_FALSE);
        } else {
            T_E(MEPA_TRACE_GRP_GEN, "Not valid gpio on LAN8x8x phy");
        }
        MEPA_EXIT(dev);
    }

    return rc;
}

/**
 * Loopback Modes:
 * MAC Loopback: Digital loopback at PCS.
 * PMA Loopback: Analog loopback inside AFE.
 * Far End Loopback: Loopback from wire to PCS and back to wire.
**/
static mepa_rc lan8x8x_loopback_set(mepa_device_t *dev, const mepa_loopback_t *loopback)
{
    mepa_rc rc = MEPA_RC_ERROR;

    if (dev != NULL) {
        MEPA_ENTER(dev);
        rc = lan8x8x_setup_lpbk(dev, loopback);
        MEPA_EXIT(dev);
    }

    return rc;
}

static mepa_rc lan8x8x_loopback_get(mepa_device_t *dev, mepa_loopback_t *const loopback)
{
    mepa_rc rc = MEPA_RC_ERROR;

    if ((dev != NULL) && (loopback != NULL)) {
        const lan8x8x_data_t *const data = (const lan8x8x_data_t *const)dev->data;

        rc = MEPA_RC_OK;
        MEPA_ENTER(dev);
        (void) memcpy(loopback, &data->loopback, sizeof(mepa_loopback_t));
        MEPA_EXIT(dev);

    }

    return rc;
}

// Enable events/interrupts
static mepa_rc lan8x8x_event_enable_set(mepa_device_t *dev,
                                        mepa_event_t event,
                                        mesa_bool_t enable)
{
    mepa_rc rc = MEPA_RC_ERROR;

    if (dev != NULL) {
        MEPA_ENTER(dev);
        rc = lan8x8x_int_events_set(dev, event, enable);
        MEPA_EXIT(dev);
    }

    return rc;
}

// Get current enabled events/interrupts
static mepa_rc lan8x8x_event_enable_get(mepa_device_t *dev, mepa_event_t *const event)
{
    mepa_rc rc = MEPA_RC_ERROR;

    if ((dev != NULL) && (event != NULL)) {
        const lan8x8x_data_t *const data = (const lan8x8x_data_t *const)dev->data;

        rc = MEPA_RC_OK;
        MEPA_ENTER(dev);
        *event = data->events;
        MEPA_EXIT(dev);
    }

    return rc;
}

// Poll the status of currently enabled events/interrupts
static mepa_rc lan8x8x_event_status_poll_int(mepa_device_t *dev,
                                             mepa_event_t *const status)
{
    const lan8x8x_data_t *const data = (const lan8x8x_data_t *const)dev->data;
    uint32_t int_sts = 0, int_mask = 0;
    uint16_t en0_sts = 0, en0_mask = 0;
    uint16_t en1_sts = 0, en1_mask = 0;
    mepa_rc rc;
    uint16_t event_set = 0, i;
    *status = 0;

    rc = phy_mmd_reg_rd(dev, MDIO_MMD_VEND1, LAN8X8X_INT_EN0_SC, &en0_mask);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    rc = phy_mmd_reg_rd(dev, MDIO_MMD_VEND1, LAN8X8X_INT_STS0_SC, &en0_sts);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    en0_sts = en0_sts & en0_mask;

    rc = phy_mmd_reg_rd(dev, MDIO_MMD_VEND1, LAN8X8X_INT_EN1_SC, &en1_mask);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    rc = phy_mmd_reg_rd(dev, MDIO_MMD_VEND1, LAN8X8X_INT_STS1_SC, &en1_sts);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    en1_sts = en1_sts & en1_mask;

    if ((en0_sts == ZERO) && (en1_sts == ZERO)) {
        return rc;
    }

    if (en1_sts != ZERO) {
        for (i = ZERO; i < 9U; i++) {
            uint16_t mask = (ONE << i);

            if ((en1_sts & mask) == LAN8X8X_INT_EN1_PTP_PRT) {
                en1_sts &= ~mask;
                *status |= MESA_PHY_LINK_TX_FIFO_OVERFLOW_INT_EV;
            } else if ((en1_sts & mask) == LAN8X8X_INT_EN1_MS_HMAC_EN) {
                en1_sts &= ~mask;
                *status |= MESA_PHY_LINK_EXT_MACSEC_HOST_MAC_EV;
            } else if ((en1_sts & mask) == LAN8X8X_INT_EN1_MS_LMAC_EN) {
                en1_sts &= ~mask;
                *status |= MESA_PHY_LINK_EXT_MACSEC_LINE_MAC_EV;
            } else if ((en1_sts & mask) == LAN8X8X_INT_EN1_MS_FCB_EN) {
                en1_sts &= ~mask;
                *status |= MESA_PHY_LINK_EXT_MACSEC_FC_BUFF_EV;
            } else if ((en1_sts & mask) == LAN8X8X_INT_EN1_MS_IG_EN) {
                en1_sts &= ~mask;
                *status |= MESA_PHY_LINK_EXT_MACSEC_INGRESS_EV;
            } else if ((en1_sts & mask) == LAN8X8X_INT_EN1_MS_EG_EN) {
                en1_sts &= ~mask;
                *status |= MESA_PHY_LINK_EXT_MACSEC_EGRESS_EV;
            } else {
                //Not implemented
                en1_sts &= ~mask;
            }

            if (en1_sts == ZERO) {
                break;
            }
        }
        *status &= data->events;
    }

    event_set = (en0_sts & LAN8X8X_INT_EN0_T1_DATA_FAULT);
    if (event_set == LAN8X8X_INT_EN0_T1_DATA_FAULT) {
        rc = phy_mmd_reg_rd32(dev, MDIO_MMD_PMAPMD, LAN8X8X_INT_IRQ_FUNC_MSK,
                              &int_mask);
        if (rc != MEPA_RC_OK) {
            return rc;
        }

        rc = phy_mmd_reg_rd32(dev, MDIO_MMD_PMAPMD, LAN8X8X_INT_IRQ_FUNC_STS,
                              &int_sts);
        if (rc != MEPA_RC_OK) {
            return rc;
        }

        //Clear interrupts that are set in status
        (void) phy_mmd_reg_wr32(dev, MDIO_MMD_PMAPMD,
                                LAN8X8X_INT_IRQ_FUNC_CLR,
                                int_sts);

        int_sts = int_sts & int_mask;
    }

    if (int_sts != ZERO) {
        for (i = ONE; i < 9U; i++) {
            uint32_t mask = BIT32(i);

            if (((int_sts & mask) == LAN8X8X_INT_LINK_CHANGE_1G) ||
                ((int_sts & mask) == LAN8X8X_INT_LINK_CHANGE)) {
                int_sts &= ~(uint32_t)mask;
                *status |= MESA_PHY_LINK_SPEED_STATE_CHANGE_EV;
            } else if ((int_sts & mask) == LAN8X8X_INT_MS_TRAINING_COMP) {
                int_sts &= ~(uint32_t)mask;
                *status |= MESA_PHY_LINK_AUTO_NEG_COMPLETE_EV;
            } else if ((int_sts & mask) == LAN8X8X_INT_SLEEP_FAIL) {
                int_sts &= ~(uint32_t)mask;
                *status |= MESA_PHY_LINK_EXT_TC10_SLEEP_FAIL;
            } else if ((int_sts & mask) == LAN8X8X_INT_SLEEP_MODE) {
                int_sts &= ~(uint32_t)mask;
                *status |= MESA_PHY_LINK_EXT_TC10_SLEEP_MODE;
            } else {
                //Not implemented
                int_sts &= ~(uint32_t)mask;
            }

            if (int_sts == ZERO) {
                break;
            }
        }
    }
    *status &= data->events;

    return rc;
}

static mepa_rc lan8x8x_event_status_poll(mepa_device_t *dev,
                                         mepa_event_t *const status)
{
    mepa_rc rc = MEPA_RC_ERROR;

    if ((dev != NULL) && (status != NULL)) {
        MEPA_ENTER(dev);
        rc = lan8x8x_event_status_poll_int(dev, status);
        MEPA_EXIT(dev);
    }

    return rc;
}

static mepa_rc lan8x8x_isolate_mode_set(mepa_device_t *dev, mepa_bool_t const val)
{
    mepa_rc rc = MEPA_RC_ERROR;

    if (dev != NULL) {

        MEPA_ENTER(dev);
        rc = lan8x8x_isolate_mode_int(dev, val);
        MEPA_EXIT(dev);
    }

    return rc;
}

static uint32_t lan8x8x_capability_priv(mepa_device_t *dev, uint32_t capability)
{
    lan8x8x_data_t *data = (lan8x8x_data_t *)(dev->data);
    uint32_t c;

    if ((capability == (uint32_t)MEPA_CAP_SPEED_1G) &&
        (data->t1_cap.dis_1000 == PHY_FALSE)) {
        c = ONE;
    } else if (capability == (uint32_t)MEPA_CAP_TS_NONE) {
        c = ONE;
    } else {
        c = ZERO;
    }

    return c;
}

static uint32_t lan8x8x_capability(mepa_device_t *dev, uint32_t capability)
{
    uint32_t c;

    MEPA_ENTER(dev);
    c = lan8x8x_capability_priv(dev, capability);
    MEPA_EXIT(dev);

    return c;
}

static mepa_rc lan8x8x_info_get(mepa_device_t *dev,
                                mepa_phy_info_t *const phy_info)
{
    mepa_rc rc = MEPA_RC_ERROR;
    uint32_t cap = 0;

    if ((dev != NULL) && (phy_info != NULL)) {
        const lan8x8x_data_t *const data = (const lan8x8x_data_t *const)dev->data;

        MEPA_ENTER(dev);
        phy_info->part_number = data->dev.part_id;
        phy_info->revision = data->dev.rev;

        if ((lan8x8x_capability_priv(dev, (uint32_t)MEPA_CAP_SPEED_1G) > 0U) &&
            (data->t1_cap.dis_1000 == PHY_FALSE)) {
            cap |= (uint32_t)MEPA_CAP_SPEED_MASK_1G;
        }

        if (lan8x8x_capability_priv(dev, (uint32_t)MEPA_CAP_TS_NONE) > 0U) {
            cap |= (uint32_t)MEPA_CAP_TS_MASK_NONE;
        }
        phy_info->cap = (mepa_phy_cap_t)cap;

        phy_info->manufactor_name = "Microchip";
        if (data->dev.id == PHY_ID_LAN878X) {
            phy_info->model_name = "LAN878X";
        } else {
            if ((data->dev.part_id == 0x8782U) ||
                (data->dev.part_id == 0x878BU)) {
                phy_info->model_name = "LAN878X";
            } else {
                phy_info->model_name = "LAN888X";
            }
        }
        rc = MEPA_RC_OK;
        MEPA_EXIT(dev);
    }
    T_D(MEPA_TRACE_GRP_GEN,   "phy_info_get rc=%d\r\n", rc);

    return rc;
}

static mepa_rc lan8x8x_debug_info(mepa_device_t *dev,
                                  lmu_ss_t *const ss,
                                  const mepa_debug_info_t   *const info)
{
    static const struct phy_reg_dbg lan8x8x_regs[] = {
        {"CL22 Control",              0U, 0U, 0U, 0U, 0U},
        {"CL22 Status",           0U, 1U, 0U, 0U, 0U},
        {"CHIPTOP:MACSEC_STATUS",     MDIO_MMD_VEND1, (CHIPTOP + 0x2U), 0U, 0U, 0U},
        {"CHIPTOP:T1_PHY_COMM_READY", MDIO_MMD_VEND1, (CHIPTOP + 0x2DU), 0U, 0U, 0U},
        {"CHIPTOP:STRAP_READ_REG",  MDIO_MMD_VEND1, (CHIPTOP + 0x36U), 0U, 0U, 0U},
        {"CHIPTOP:OTP_STRAP_REG",   MDIO_MMD_VEND1, (OTP_STRAP_READ_REG), 0U, 0U, 0U},
        {"AN:MDIO_AN_T1_ADV_L", MDIO_MMD_AN, MDIO_AN_T1_ADV_L, 0U, 0U, 0U},
        {"AN:MDIO_AN_T1_ADV_M", MDIO_MMD_AN, MDIO_AN_T1_ADV_M, 0U, 0U, 0U},
        {"AN:MDIO_AN_T1_LP_L",  MDIO_MMD_AN, MDIO_AN_T1_LP_L, 0U, 0U, 0U},
        {"AN:MDIO_AN_T1_LP_M",  MDIO_MMD_AN, MDIO_AN_T1_LP_M, 0U, 0U, 0U},
        {"AN:BASE_T1_AN_CONTROL", MDIO_MMD_AN, MDIO_AN_T1_CTRL, 0U, 0U, 0U},
        {"AN:BASE_T1_AN_STATUS",  MDIO_MMD_AN, MDIO_AN_T1_STAT, 0U, 0U, 0U},
        {"AN:AUTONEG_STATUS",     MDIO_MMD_AN, 0x8002U, 0U, 0U, 1U},
        {"SERDES_CLOCK_CONTROL",  MDIO_MMD_VEND1, SERDES_CLOCK_CONTROL, 0U, 0U, 0U},
        {"QSGMII_PCS1G_CONFIG",   MDIO_MMD_VEND1, QSGMII_PCS1G_CONFIG, 0U, 0U, 0U},
        {"QSGMII_PCS1G_ANEG_CONFIG", MDIO_MMD_VEND1, QSGMII_PCS1G_ANEG_CONFIG, 0U, 0U, 0U},
        {"QSGMII_ANEG_EN_REG",      MDIO_MMD_VEND1, QSGMII_ANEG_EN_REG, 0U, 0U, 0U},
        {"T1_1G_RF_CRC:STATUS",         MDIO_MMD_PMAPMD, 0x8400, 0U, 0U, 1U},
        {"T1_1G_RF_CRC:EXPECTED",       MDIO_MMD_PMAPMD, 0x8402, 0U, 0U, 1U},
        {"T1_1G_RF_CRC:CURRENT",    MDIO_MMD_PMAPMD, 0x8404, 0U, 0U, 1U},
        {"T1_1G_RF_CRC:AUTO_CTRL",  MDIO_MMD_PMAPMD, 0x8406, 0U, 0U, 1U},
        {"T1_1G_PRBS_CTL",      MDIO_MMD_PCS, 0x8A00, 0U, 0U, 1U},
        {"T1_1G_PRBS_STS1:CRC_CNT_OVF", MDIO_MMD_PCS, 0x8A08, 0U, 0U, 1U},
        {"T1_1G_PRBS_STS2:CRC_ERRORS",  MDIO_MMD_PCS, 0x8A0A, 0U, 0U, 1U},
        {"EPG:TX_CRCERR_CNTR",  MDIO_MMD_VEND1,  0xF10A, 0U, 0U, 0U},
        {"EPG:RX_CRCERR_CNTR",  MDIO_MMD_VEND1,  0xF10B, 0U, 0U, 0U},
        {"RX_FIFO_LW_WRCNT",    MDIO_MMD_VEND1, 0xF0AF, 0U, 0U, 0U},
        {"RX_FIFO_UW_WRCNT",    MDIO_MMD_VEND1, 0xF0B0, 0U, 0U, 0U},
        {"RX_FIFO_LW_RDCNT",    MDIO_MMD_VEND1, 0xF0B1, 0U, 0U, 0U},
        {"RX_FIFO_UW_RDCNT",    MDIO_MMD_VEND1, 0xF0B2, 0U, 0U, 0U},
        {"TX_FIFO_LW_WRCNT",    MDIO_MMD_VEND1, 0xF0B3, 0U, 0U, 0U},
        {"TX_FIFO_UW_WRCNT",    MDIO_MMD_VEND1, 0xF0B4, 0U, 0U, 0U},
        {"TX_FIFO_LW_RDCNT",    MDIO_MMD_VEND1, 0xF0B5, 0U, 0U, 0U},
        {"TX_FIFO_UW_RDCNT",    MDIO_MMD_VEND1, 0xF0B6, 0U, 0U, 0U},
        {"HMAC:RX_TAGGED_FRAME",    0x3, 0xF176, 0U, 0U, 1U},
        {"HMAC:RX_UNTAGGED_FRAME",  0x3, 0xF178, 0U, 0U, 1U},
        {"HMAC:TX_TAGGED_FRAME",    0x3, 0xF17A, 0U, 0U, 1U},
        {"HMAC:TX_UNTAGGED_FRAME",  0x3, 0xF17C, 0U, 0U, 1U},
        {"LINE:RX_TAGGED_FRAME",    0x3, 0xF376, 0U, 0U, 1U},
        {"LINE:RX_UNTAGGED_FRAME",  0x3, 0xF378, 0U, 0U, 1U},
        {"LINE:TX_TAGGED_FRAME",    0x3, 0xF37A, 0U, 0U, 1U},
        {"LINE:TX_UNTAGGED_FRAME",  0x3, 0xF37C, 0U, 0U, 1U},
    };
    mepa_rc rc = MEPA_RC_ERROR;

    if ((dev != NULL) && (ss != NULL) && (info != NULL)) {
        //PHY Debugging
        switch (info->group) {
        case MEPA_DEBUG_GROUP_ALL:
        case MEPA_DEBUG_GROUP_PHY: {
            MEPA_ENTER(dev);
            phy_reg_dump(dev, ss, lan8x8x_regs,
                         (uint8_t)(sizeof(lan8x8x_regs) / sizeof((lan8x8x_regs)[0])), 0U);
            rc = MEPA_RC_OK;
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

static mepa_rc lan8x8x_poll_int(mepa_device_t *dev, mepa_status_t *status)
{
    lan8x8x_data_t *const data = (lan8x8x_data_t *const)dev->data;
    mepa_port_speed_t old_speed = status->speed;
    mepa_rc rc;
    uint8_t master_slave;
    uint16_t val;

    //Current link status
    data->link_status = PHY_FALSE;

    if (data->conf.speed != MESA_SPEED_AUTO) {
        if (data->conf.speed == MESA_SPEED_100M) {
            //Latch-low register
            rc = phy_mmd_reg_rd(dev, MDIO_MMD_PMAPMD, MDIO_STAT1, &val);
            if (rc != MEPA_RC_OK) {
                return rc;
            }

            rc = phy_mmd_reg_rd(dev, MDIO_MMD_PMAPMD, MDIO_STAT1, &val);
            if (rc != MEPA_RC_OK) {
                return rc;
            }

            status->link = ((val & MDIO_STAT1_LSTATUS) != ZERO);
        } else {
            rc = phy_mmd_reg_rd(dev, MDIO_MMD_PMAPMD, MDIO_PMA_PMD_BT1_STAT, &val);
            if (rc != MEPA_RC_OK) {
                return rc;
            }

            rc = phy_mmd_reg_rd(dev, MDIO_MMD_PMAPMD, MDIO_PMA_PMD_BT1_STAT, &val);
            if (rc != MEPA_RC_OK) {
                return rc;
            }

            status->link = ((val & MDIO_PMA_PMD_BT1_LINK_STS) != ZERO);
            data->link_status = status->link;
        }
        status->master = ((data->conf.man_neg == MEPA_MANUAL_NEG_REF) ?
                          PHY_TRUE : PHY_FALSE);
        status->speed = data->conf.speed;
    } else {
        //Resolve mode
        rc = lan8x8x_aneg_resolve_master_slave(dev, &master_slave);
        if (rc != MEPA_RC_OK) {
            return rc;
        }

        status->master = (master_slave == MASTER_SLAVE_STATE_MASTER) ?
                         PHY_TRUE : PHY_FALSE;

        //Resolve speed from lp_adv
        rc = lan8x8x_aneg_read_status(dev, status);
        if (rc != MEPA_RC_OK) {
            return rc;
        }

        if (IS_LAN888X(dev->drv->id) == ONE) {
            if ((status->speed != old_speed) && (old_speed != MESA_SPEED_UNDEFINED)) {
                (void) lan8x8x_speed_config(dev, status->speed);
            }
        }

        //Resolve link status
        rc = phy_mmd_reg_rd(dev, MDIO_MMD_AN, MDIO_AN_T1_STAT, &val);
        if (rc != MEPA_RC_OK) {
            return rc;
        }

        rc = phy_mmd_reg_rd(dev, MDIO_MMD_AN, MDIO_AN_T1_STAT, &val);
        if (rc != MEPA_RC_OK) {
            return rc;
        }

        status->link = ((val & MDIO_STAT1_LSTATUS) != ZERO);
    }

    //T1 PHY supports only Full Duplex
    status->fdx = PHY_TRUE;
    data->link_status =  status->link;
    data->dev.is_master = status->master;

    return rc;
}

static mepa_rc lan8x8x_aneg_status_get(mepa_device_t *dev, mepa_aneg_status_t *status)
{
    mepa_rc rc = MEPA_RC_ERROR;
    uint8_t master_slave_state;

    if ((dev != NULL) && (status != NULL)) {
        lan8x8x_data_t *data = (lan8x8x_data_t *)dev->data;

        rc = MEPA_RC_INV_STATE;
        if (data->conf.speed == MESA_SPEED_AUTO) {

            MEPA_ENTER(dev);

            rc = lan8x8x_aneg_resolve_master_slave(dev, &master_slave_state);
            if (rc == MEPA_RC_OK) {
                status->master_cfg_fault = (master_slave_state == MASTER_SLAVE_STATE_ERR) ? PHY_TRUE : PHY_FALSE;
                status->master = (master_slave_state == MASTER_SLAVE_STATE_MASTER) ? PHY_TRUE : PHY_FALSE;
            }

            MEPA_EXIT(dev);
        }
    }

    return rc;
}

static mepa_rc lan8x8x_poll(mepa_device_t *dev, mepa_status_t *status)
{
    mepa_rc rc = MEPA_RC_ERROR;

    if ((dev != NULL) && (status != NULL)) {
        MEPA_ENTER(dev);
        rc = lan8x8x_poll_int(dev, status);
        MEPA_EXIT(dev);
    }

    return rc;
}

static uint8_t lan888x_cd_len(uint32_t location)
{
    uint32_t len_cm;

    switch (location) {
    case LAN8X8X_CD_LOC0:
        len_cm = 0;
        break;
    case LAN8X8X_CD_LOC1:
        len_cm = 150;
        break;
    case LAN8X8X_CD_LOC2:
    case LAN8X8X_CD_LOC3:
        len_cm = 300;
        break;
    case LAN8X8X_CD_LOC4:
        len_cm = 450;
        break;
    case LAN8X8X_CD_LOC5:
    case LAN8X8X_CD_LOC6:
        len_cm = 600;
        break;
    case LAN8X8X_CD_LOC7:
        len_cm = 750;
        break;
    case LAN8X8X_CD_LOC8:
    case LAN8X8X_CD_LOC9:
        len_cm = 900;
        break;
    case LAN8X8X_CD_LOC10:
        len_cm = 1050;
        break;
    case LAN8X8X_CD_LOC11:
        len_cm = 1200;
        break;
    case LAN8X8X_CD_LOC12:
        len_cm = 1350;
        break;
    case LAN8X8X_CD_LOC13:
    case LAN8X8X_CD_LOC14:
        len_cm = 1500;
        break;
    default:
        len_cm = 0;
        break;
    }

    return (uint8_t)(len_cm / 100U);
}

static uint8_t lan878x_cd_len(uint32_t location)
{
    uint32_t len_cm;

    switch (location) {
    case LAN8X8X_CD_LOC0:
        len_cm = 150;
        break;
    case LAN8X8X_CD_LOC1:
        len_cm = 300;
        break;
    case LAN8X8X_CD_LOC2:
        len_cm = 450;
        break;
    case LAN8X8X_CD_LOC3:
        len_cm = 600;
        break;
    case LAN8X8X_CD_LOC4:
        len_cm = 750;
        break;
    case LAN8X8X_CD_LOC5:
        len_cm = 900;
        break;
    case LAN8X8X_CD_LOC6:
        len_cm = 1050;
        break;
    case LAN8X8X_CD_LOC7:
        len_cm = 1200;
        break;
    case LAN8X8X_CD_LOC8:
        len_cm = 1350;
        break;
    case LAN8X8X_CD_LOC9:
    case LAN8X8X_CD_LOC10:
        len_cm = 1500;
        break;
    default:
        len_cm = 0;
        break;
    }

    return (uint8_t)(len_cm / 100U);
}

static mepa_rc lan8x8x_cable_test_report(mepa_device_t *dev,
                                         mepa_cable_diag_result_t *res,
                                         uint32_t cd_sts)
{
    uint32_t status = LAN8X8X_CD_STS_GET(cd_sts);
    uint32_t location;

    // Set status
    switch (status) {
    case LAN8X8X_CD_STS_NONE:
    case LAN8X8X_LINK_UP:
    case LAN8X8X_CBL_OK:
        res->status[LAN8X8X_PAIR_0] = MESA_VERIPHY_STATUS_OK;
        break;
    case LAN8X8X_CBL_SHORT:
        res->status[LAN8X8X_PAIR_0] = MESA_VERIPHY_STATUS_SHORT;
        break;
    case LAN8X8X_CBL_OPEN:
        res->status[LAN8X8X_PAIR_0] = MESA_VERIPHY_STATUS_OPEN;
        break;
    case LAN8X8X_TST_ACTIVE:
        res->status[LAN8X8X_PAIR_0] = MESA_VERIPHY_STATUS_RUNNING;
        break;
    default:
        res->status[LAN8X8X_PAIR_0] = MESA_VERIPHY_STATUS_UNKNOWN;
        break;
    }

    if ((status == LAN8X8X_CBL_OPEN) ||
        (status == LAN8X8X_CBL_SHORT)) {
        if (IS_LAN888X(dev->drv->id) == ONE) {
            location = LAN888X_CD_LOC(cd_sts);
            res->length[LAN8X8X_PAIR_0] = lan888x_cd_len(location);
        } else {
            location = LAN878X_CD_LOC(cd_sts);
            res->length[LAN8X8X_PAIR_0] = lan878x_cd_len(location);
        }
    }

    return MEPA_RC_OK;
}

// Start cable-diag test without waiting for completion.
static mepa_rc lan8x8x_cable_diag_async(mepa_device_t *dev)
{
    lan8x8x_data_t *const data = (lan8x8x_data_t *const)dev->data;
    mepa_rc rc = MEPA_RC_INV_STATE;

    if ((data->init_done) && (data->conf.admin.enable)) {
        data->cd_res.status[LAN8X8X_PAIR_0] = MESA_VERIPHY_STATUS_ABNORM;

        if (IS_LAN878X(dev->drv->id) == ONE) {
            return phy_mmd_reg_mod32(dev, MDIO_MMD_PMAPMD, LAN878X_CD_CFG,
                                     LAN878X_CD_EN, LAN878X_CD_EN);
        }

        rc = lan8x8x_phy_reset(dev);
        if (rc != MEPA_RC_OK) {
            return rc;
        }

        // DUT to master mode
        rc = phy_mmd_reg_wr(dev, MDIO_MMD_PMAPMD, MDIO_PMA_PMD_BT1_CTRL,
                            MDIO_PMA_PMD_BT1_CTRL_CFG_MST);
        if (rc != MEPA_RC_OK) {
            return rc;
        }

        rc = phy_mmd_reg_wr32(dev, MDIO_MMD_PMAPMD, T1_1G_TOP_CTRL_CONFIG,
                              T1_1G_TOP_CTRL_CONFIG_DONE);
        if (rc != MEPA_RC_OK) {
            return rc;
        }

        LAN8X8X_NSLEEP(10000U);

        // Enable CD
        rc = phy_mmd_reg_mod32(dev, MDIO_MMD_PMAPMD, LAN8X8X_CD_STS,
                               LAN888X_CD_ACTIVE, LAN888X_CD_ENABLE);
        if (rc != MEPA_RC_OK) {
            return rc;
        }
    }

    return rc;
}

static mepa_rc lan8x8x_cable_diag_poll_int(mepa_device_t *dev)
{
    lan8x8x_data_t *const data = (lan8x8x_data_t *const)dev->data;
    uint32_t cd_sts = 0U;
    mepa_bool_t done;
    mepa_rc rc;

    if (IS_LAN878X(dev->drv->id) == ONE) {
        rc = phy_mmd_reg_rd32(dev, MDIO_MMD_PMAPMD, LAN878X_CD_CFG, &cd_sts);
        done = ((cd_sts & LAN878X_CD_DONE) == LAN878X_CD_DONE);
    } else {
        rc = phy_mmd_reg_rd32(dev, MDIO_MMD_PMAPMD, LAN8X8X_CD_STS, &cd_sts);
        done = LAN8X8X_CD_TEST_DONE(cd_sts);
    }

    if (rc != MEPA_RC_OK) {
        return rc;
    }

    if (!done) {
        return MEPA_RC_INCOMPLETE;
    }

    if (IS_LAN878X(dev->drv->id) == ONE) {
        rc = phy_mmd_reg_rd32(dev, MDIO_MMD_PMAPMD, LAN8X8X_CD_STS, &cd_sts);
        if (rc != MEPA_RC_OK) {
            return rc;
        }
    }

    // Stop cable diag
    if (IS_LAN878X(dev->drv->id) == ONE) {
        (void) phy_mmd_reg_mod32(dev, MDIO_MMD_PMAPMD, LAN878X_CD_CFG,
                                 LAN878X_CD_DIS, LAN878X_CD_DIS);
    } else {
        (void) phy_mmd_reg_mod32(dev, MDIO_MMD_PMAPMD, LAN8X8X_CD_STS,
                                 LAN888X_CD_ACTIVE, LAN888X_CD_DISABLE);
    }

    // Restore configuration and record cable diag report
    (void) lan8x8x_phy_setup(dev);

    return lan8x8x_cable_test_report(dev, &data->cd_res, cd_sts);
}

static mepa_rc lan8x8x_phy_cable_diag_start(mepa_device_t *dev)
{
    lan8x8x_data_t *const data = (lan8x8x_data_t *const)dev->data;
    mepa_rc rc = MEPA_RC_INV_STATE;

    if ((data->init_done) && (data->conf.admin.enable)) {
        uint32_t cd_sts;

        mepa_cable_diag_result_t *res = (mepa_cable_diag_result_t *const) & (data->cd_res);

        res->status[0] = MESA_VERIPHY_STATUS_ABNORM;

        if (IS_LAN878X(dev->drv->id) == ONE) {
            rc = phy_mmd_reg_mod32(dev, MDIO_MMD_PMAPMD, LAN878X_CD_CFG,
                                   LAN878X_CD_EN, LAN878X_CD_EN);
            if (rc != MEPA_RC_OK) {
                return rc;
            }

            rc = phy_mmd_reg_poll32(dev, MDIO_MMD_PMAPMD, LAN878X_CD_CFG,
                                    LAN878X_CD_DONE, LAN878X_CD_DONE,
                                    1U, 4000U, &cd_sts);
            if (rc != MEPA_RC_OK) {
                return rc;
            }

            rc = phy_mmd_reg_rd32(dev, MDIO_MMD_PMAPMD, LAN8X8X_CD_STS, &cd_sts);
            if (rc != MEPA_RC_OK) {
                return rc;
            }

            rc = phy_mmd_reg_mod32(dev, MDIO_MMD_PMAPMD, LAN878X_CD_CFG,
                                   LAN878X_CD_DIS, LAN878X_CD_DIS);
            if (rc != MEPA_RC_OK) {
                return rc;
            }
        } else {
            // Soft Reset
            rc = lan8x8x_phy_reset(dev);
            if (rc != MEPA_RC_OK) {
                return rc;
            }

            // DUT to master mode
            rc = phy_mmd_reg_wr(dev, MDIO_MMD_PMAPMD, MDIO_PMA_PMD_BT1_CTRL,
                                MDIO_PMA_PMD_BT1_CTRL_CFG_MST);
            if (rc != MEPA_RC_OK) {
                return rc;
            }

            // Config done set
            rc = phy_mmd_reg_wr32(dev, MDIO_MMD_PMAPMD, T1_1G_TOP_CTRL_CONFIG,
                                  T1_1G_TOP_CTRL_CONFIG_DONE);
            if (rc != MEPA_RC_OK) {
                return rc;
            }

            LAN8X8X_NSLEEP(10000U);

            // Enable CD
            rc = phy_mmd_reg_mod32(dev, MDIO_MMD_PMAPMD, LAN8X8X_CD_STS,
                                   LAN888X_CD_ACTIVE, LAN888X_CD_ENABLE);
            if (rc != MEPA_RC_OK) {
                return rc;
            }

            // Poll until the TDR test is no longer active (test done)
            rc = phy_mmd_reg_poll32(dev, MDIO_MMD_PMAPMD, LAN8X8X_CD_STS,
                                    (LAN8X8X_TST_ACTIVE << 4), LAN8X8X_CD_STS_MASK,
                                    0U, 4000U, &cd_sts);
            if (rc != MEPA_RC_OK) {
                return rc;
            }

            //Stop cable diag
            rc = phy_mmd_reg_mod32(dev, MDIO_MMD_PMAPMD, LAN8X8X_CD_STS,
                                   LAN888X_CD_ACTIVE, LAN888X_CD_DISABLE);
            if (rc != MEPA_RC_OK) {
                return rc;
            }
        }

        //Re-store configuration
        (void) lan8x8x_phy_setup(dev);

        //cable diag report
        rc = lan8x8x_cable_test_report(dev, res, cd_sts);
        if (rc != MEPA_RC_OK) {
            return rc;
        }
    }

    return rc;
}

static mepa_rc lan8x8x_cable_diag_start_int(mepa_device_t *dev)
{
    lan8x8x_data_t *const data = (lan8x8x_data_t *const)dev->data;
    mepa_status_t link_status = { 0 };
    mepa_rc rc;

    rc = lan8x8x_poll_int(dev, &link_status);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    data->cd_res.link = data->link_status;
    data->cd_res.length[LAN8X8X_PAIR_0] = 0U;
    data->cd_res.status[LAN8X8X_PAIR_0] = MESA_VERIPHY_STATUS_UNKNOWN;

    if (data->link_status == PHY_LINKUP) {
        rc = MEPA_RC_OK;
        data->cd_res.status[LAN8X8X_PAIR_0] = MESA_VERIPHY_STATUS_OK;
    } else {
        rc = lan8x8x_phy_cable_diag_start(dev);
        if (rc != MEPA_RC_OK) {
            return rc;
        }
    }

    return rc;
}

static mepa_rc lan8x8x_cable_diag_start(mepa_device_t *dev, int32_t mode)
{
    mepa_rc rc = MEPA_RC_ERROR;
    (void)mode;

    if (dev != NULL) {
        MEPA_ENTER(dev);
        rc = lan8x8x_cable_diag_start_int(dev);
        MEPA_EXIT(dev);
    }

    return rc;
}

static mepa_rc lan8x8x_cable_diag_start_async(mepa_device_t *dev, int32_t mode)
{
    mepa_rc rc = MEPA_RC_ERROR;
    (void)mode;

    if (dev != NULL) {
        lan8x8x_data_t *const data = (lan8x8x_data_t *const)dev->data;
        mepa_status_t link_status = { 0 };

        MEPA_ENTER(dev);

        if (data->cd_state == LAN8X8X_CD_RUNNING) {
            // Async cable diag already in progress - do not restart it
            rc = MEPA_RC_OK;
        } else {
            rc = lan8x8x_poll_int(dev, &link_status);
            if (rc == MEPA_RC_OK) {
                data->cd_res.link = data->link_status;
                data->cd_res.length[LAN8X8X_PAIR_0] = 0U;
                data->cd_res.status[LAN8X8X_PAIR_0] = MESA_VERIPHY_STATUS_UNKNOWN;

                if (data->link_status == PHY_LINKUP) {
                    data->cd_res.status[LAN8X8X_PAIR_0] =
                        MESA_VERIPHY_STATUS_OK;
                    data->cd_state = LAN8X8X_CD_DONE;
                } else {
                    rc = lan8x8x_cable_diag_async(dev);
                    data->cd_state = (rc == MEPA_RC_OK) ? LAN8X8X_CD_RUNNING : LAN8X8X_CD_IDLE;
                }
            }
        }

        MEPA_EXIT(dev);
    }

    return rc;
}

static mepa_rc lan8x8x_cable_diag_poll(mepa_device_t *dev)
{
    mepa_rc rc = MEPA_RC_ERROR;

    if (dev != NULL) {
        lan8x8x_data_t *const data = (lan8x8x_data_t *const)dev->data;

        MEPA_ENTER(dev);

        if (data->cd_state == LAN8X8X_CD_IDLE) {
            rc = MEPA_RC_ERROR;
        } else if (data->cd_state == LAN8X8X_CD_DONE) {
            rc = MEPA_RC_OK;
            data->cd_state = LAN8X8X_CD_IDLE;
        } else {
            rc = lan8x8x_cable_diag_poll_int(dev);
            if (rc != MEPA_RC_INCOMPLETE) {
                data->cd_state = LAN8X8X_CD_IDLE;
            }
        }

        MEPA_EXIT(dev);
    }

    return rc;
}

static mepa_rc lan8x8x_cable_diag_stop_async(mepa_device_t *dev)
{
    mepa_rc rc = MEPA_RC_ERROR;

    if (dev != NULL) {
        lan8x8x_data_t *const data = (lan8x8x_data_t *const)dev->data;

        MEPA_ENTER(dev);

        if (data->cd_state != LAN8X8X_CD_RUNNING) {
            // No async cable diag pending - nothing to stop
            rc = MEPA_RC_OK;
        } else {
            // Stop cable diagnostic test
            if (IS_LAN878X(dev->drv->id) == ONE) {
                rc = phy_mmd_reg_mod32(dev, MDIO_MMD_PMAPMD, LAN878X_CD_CFG,
                                       LAN878X_CD_DIS, LAN878X_CD_DIS);
            } else {
                rc = phy_mmd_reg_mod32(dev, MDIO_MMD_PMAPMD, LAN8X8X_CD_STS,
                                       LAN888X_CD_ACTIVE, LAN888X_CD_DISABLE);
            }

            // Restore configuration after cable diagnostics
            (void) lan8x8x_phy_setup(dev);

            // Clear the stored cable diag result
            data->cd_res.length[LAN8X8X_PAIR_0] = 0U;
            data->cd_res.status[LAN8X8X_PAIR_0] = MESA_VERIPHY_STATUS_UNKNOWN;
            data->cd_state = LAN8X8X_CD_IDLE;
        }

        MEPA_EXIT(dev);
    }

    return rc;
}

static mepa_rc lan8x8x_cable_diag_get(mepa_device_t *dev, mepa_cable_diag_result_t *res)
{
    mepa_rc rc = MEPA_RC_ERROR;

    if ((dev != NULL) && (res != NULL)) {
        lan8x8x_data_t *const data = (lan8x8x_data_t *const)dev->data;

        rc = MEPA_RC_OK;

        MEPA_ENTER(dev);
        *res = data->cd_res;
        MEPA_EXIT(dev);
    }

    return rc;
}

static void fill_driver_info(uint32_t id, uint32_t mask, mepa_driver_t *drv_inst)
{
    T_D(MEPA_TRACE_GRP_GEN,   "Fill driver info for phy_id=0x%x\n", id);
    // Device ID & Mask
    drv_inst->id             = id;
    drv_inst->mask           = mask;
    // LAN8X8X Driver APIs
    drv_inst->mepa_driver_delete             = &lan8x8x_delete;
    drv_inst->mepa_driver_reset              = &lan8x8x_reset;
    drv_inst->mepa_driver_poll               = &lan8x8x_poll;
    drv_inst->mepa_driver_probe              = &lan8x8x_probe;
    drv_inst->mepa_driver_aneg_status_get    = &lan8x8x_aneg_status_get;
    drv_inst->mepa_driver_conf_set           = &lan8x8x_conf_set;
    drv_inst->mepa_driver_conf_get           = &lan8x8x_conf_get;
    drv_inst->mepa_driver_if_set             = &lan8x8x_if_set;
    drv_inst->mepa_driver_if_get             = &lan8x8x_if_get;
    drv_inst->mepa_driver_media_set          = &lan8x8x_media_set;
    drv_inst->mepa_driver_media_get          = &lan8x8x_media_get;
    drv_inst->mepa_driver_gpio_mode_set      = &lan8x8x_gpio_mode_set;
    drv_inst->mepa_driver_gpio_out_set       = &lan8x8x_gpio_out_set;
    drv_inst->mepa_driver_gpio_in_get        = &lan8x8x_gpio_in_get;
    drv_inst->mepa_driver_sqi_read           = &lan8x8x_sqi_read;
    drv_inst->mepa_driver_loopback_set       = &lan8x8x_loopback_set;
    drv_inst->mepa_driver_loopback_get       = &lan8x8x_loopback_get;
    drv_inst->mepa_driver_capability         = &lan8x8x_capability;
    drv_inst->mepa_driver_phy_info_get       = &lan8x8x_info_get;
    drv_inst->mepa_driver_debug_info_dump    = &lan8x8x_debug_info;
    drv_inst->mepa_driver_clause22_read      = &lan8x8x_reg_read;
    drv_inst->mepa_driver_clause22_write     = &lan8x8x_reg_write;
    drv_inst->mepa_driver_clause45_read      = &lan8x8x_mmd_reg_read;
    drv_inst->mepa_driver_clause45_write     = &lan8x8x_mmd_reg_write;
    drv_inst->mepa_driver_isolate_mode_conf  = &lan8x8x_isolate_mode_set;
    drv_inst->mepa_driver_event_enable_set   = &lan8x8x_event_enable_set;
    drv_inst->mepa_driver_event_enable_get   = &lan8x8x_event_enable_get;
    drv_inst->mepa_driver_event_poll         = &lan8x8x_event_status_poll;
    drv_inst->mepa_driver_cable_diag_start       = &lan8x8x_cable_diag_start;
    drv_inst->mepa_driver_cable_diag_get         = &lan8x8x_cable_diag_get;
    drv_inst->mepa_driver_cable_diag_start_async = &lan8x8x_cable_diag_start_async;
    drv_inst->mepa_driver_cable_diag_stop_async  = &lan8x8x_cable_diag_stop_async;
    drv_inst->mepa_driver_cable_diag_poll        = &lan8x8x_cable_diag_poll;
}

mepa_drivers_t mepa_lan8x8x_driver_init(void)
{
    uint32_t lan8x8x_ids[] = {PHY_ID_LAN888X, PHY_ID_LAN878X};
    static mepa_driver_t lan8x8x_driver[2U];
    mepa_drivers_t result;
    uint8_t idx = 0;

    for (idx = 0; idx < (sizeof(lan8x8x_driver) / sizeof((lan8x8x_driver)[0])); idx++) {
        fill_driver_info(lan8x8x_ids[idx], PHY_ID_MASK, &lan8x8x_driver[idx]);
    }

    result.phy_drv = &lan8x8x_driver[0];
    result.count = (uint32_t)(sizeof(lan8x8x_driver) / sizeof((lan8x8x_driver)[0]));

    return result;
}
