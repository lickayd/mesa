// Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#include <stdbool.h>

#include <microchip/ethernet/phy/api.h>
#include <mepa_driver.h>
#include <mepa_utils.h>
#include <mepa_ts_driver.h>

#include "../../common/include/lan8814_registers.h"
#include "microchip/lan8814_cs.h"
#include "lan8814_ts_registers.h"

#include "lan8814_private.h"

#if !defined(MEPA_LAN8814_LIGHT)
extern mepa_ts_driver_t lan8814_ts_drivers;
#endif


// Return true if the PHY is the lan8814
static mepa_bool_t lan8814_is_lan8814(mepa_device_t *dev)
{
    lan8814_data_t *data = (lan8814_data_t *) dev->data;

    return data->dev.model == 0x26U;
}

// Return true if the PHY is the internal PHY of lan966x
static mepa_bool_t lan8814_is_lan8842(mepa_device_t *dev)
{
    lan8814_data_t *data = (lan8814_data_t *) dev->data;

    return data->dev.model == 0x2cU;
}

// Return true if the PHY is the internal PHY of lan966x
static mepa_bool_t lan8814_is_lan966x(mepa_device_t *dev)
{
    lan8814_data_t *data = (lan8814_data_t *) dev->data;

    return data->dev.model == 0x27U;
}

static const char *lan8814_get_name(mepa_device_t *dev)
{
    if (lan8814_is_lan8814(dev)) {
        return "lan8814";
    }
    if (lan8814_is_lan8842(dev)) {
        return "lan8842";
    }
    if (lan8814_is_lan966x(dev)) {
        return "lan966x internal";
    }

    T_W(MEPA_TRACE_GRP_GEN, "Unknown name for the device");
    return "";
}

static mepa_bool_t lan8814_has_ptp(mepa_device_t *dev)
{
    lan8814_data_t *data = (lan8814_data_t *) dev->data;

    if (lan8814_is_lan966x(dev)) {
        return FALSE;
    }

    if (lan8814_is_lan8814(dev)) {
        return data->dev.sku == 0x8814U || data->dev.sku == 0x8818U;
    }

    if (lan8814_is_lan8842(dev)) {
        return data->dev.sku == 0x8842U;
    }

    return FALSE;
}

mepa_rc lan8814_direct_reg_rd(mepa_device_t *dev, uint16_t addr, uint16_t *value)
{
    if (dev->callout->miim_read(dev->callout_ctx, addr, value) != MESA_RC_OK) {
        T_E(MEPA_TRACE_GRP_GEN, "Port %d miim read failed\n", dev->numeric_handle);
    }
    return MEPA_RC_OK;
}

mepa_rc lan8814_direct_reg_wr(mepa_device_t *dev, uint16_t addr, uint16_t value, uint16_t mask)
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

// Extended page read and write functions
// Extended page numbers range : 0 - 31
mepa_rc lan8814_ext_reg_rd(mepa_device_t *dev, uint16_t page, uint16_t addr, uint16_t *value)
{
    // Set-up to access extended page register.
    MEPA_RC(dev->callout->miim_write(dev->callout_ctx, LAN8814_EXT_PAGE_ACCESS_CTRL, page));
    MEPA_RC(dev->callout->miim_write(dev->callout_ctx, LAN8814_EXT_PAGE_ACCESS_ADDR_DATA, addr));
    MEPA_RC(dev->callout->miim_write(dev->callout_ctx, LAN8814_EXT_PAGE_ACCESS_CTRL,
                                     LAN8814_F_EXT_PAGE_ACCESS_CTRL_EP_FUNC | page));

    // Read the value
    MEPA_RC(lan8814_direct_reg_rd(dev, LAN8814_EXT_PAGE_ACCESS_ADDR_DATA, value));
    return MEPA_RC_OK;
}
mepa_rc lan8814_ext_reg_wr(mepa_device_t *dev, uint16_t page, uint16_t addr, uint16_t value, uint16_t mask)
{
    // Set-up to access extended page register.
    MEPA_RC(dev->callout->miim_write(dev->callout_ctx, LAN8814_EXT_PAGE_ACCESS_CTRL, page));
    MEPA_RC(dev->callout->miim_write(dev->callout_ctx, LAN8814_EXT_PAGE_ACCESS_ADDR_DATA, addr));
    MEPA_RC(dev->callout->miim_write(dev->callout_ctx, LAN8814_EXT_PAGE_ACCESS_CTRL,
                                     LAN8814_F_EXT_PAGE_ACCESS_CTRL_EP_FUNC | page));

    // write the value
    MEPA_RC(lan8814_direct_reg_wr(dev, LAN8814_EXT_PAGE_ACCESS_ADDR_DATA, value, mask));
    return MEPA_RC_OK;
}

// Extended page address incremental read. After reading one register address of would automatically be
// incremented to next location in this function. This is useful for reading contiguous group of registers.
mepa_rc lan8814_ext_incr_reg_rd(mepa_device_t *dev, uint16_t page, uint16_t addr, uint16_t *value, mepa_bool_t start_addr)
{
    if (start_addr) {
        // Set-up to access extended page register.
        MEPA_RC(dev->callout->miim_write(dev->callout_ctx, LAN8814_EXT_PAGE_ACCESS_CTRL, page));
        MEPA_RC(dev->callout->miim_write(dev->callout_ctx, LAN8814_EXT_PAGE_ACCESS_ADDR_DATA, addr));
        MEPA_RC(dev->callout->miim_write(dev->callout_ctx, LAN8814_EXT_PAGE_ACCESS_CTRL,
                                         LAN8814_F_EXT_PAGE_ACCESS_CTRL_INCR_RD_WR | page));
    }
    // Read the value
    MEPA_RC(lan8814_direct_reg_rd(dev, LAN8814_EXT_PAGE_ACCESS_ADDR_DATA, value));
    return MEPA_RC_OK;
}

// MMD read and write functions
// MMD device range : 0 - 31
mepa_rc lan8814_mmd_reg_rd(mepa_device_t *dev, uint16_t mmd, uint16_t addr, uint16_t *value)
{
    // Set-up to MMD register.
    MEPA_RC(lan8814_direct_reg_wr(dev, LAN8814_MMD_ACCESS_CTRL, mmd, LAN8814_DEF_MASK));
    MEPA_RC(lan8814_direct_reg_wr(dev, LAN8814_MMD_ACCESS_ADDR_DATA, addr, LAN8814_DEF_MASK));
    MEPA_RC(lan8814_direct_reg_wr(dev, LAN8814_MMD_ACCESS_CTRL,
                                  LAN8814_F_MMD_ACCESS_CTRL_MMD_FUNC | mmd, LAN8814_DEF_MASK));

    // Read the value
    MEPA_RC(lan8814_direct_reg_rd(dev, LAN8814_MMD_ACCESS_ADDR_DATA, value));
    return MEPA_RC_OK;
}

mepa_rc lan8814_mmd_reg_wr(mepa_device_t *dev, uint16_t mmd, uint16_t addr, uint16_t value, uint16_t mask)
{
    // Set-up to MMD register.
    MEPA_RC(lan8814_direct_reg_wr(dev, LAN8814_MMD_ACCESS_CTRL, mmd, LAN8814_DEF_MASK));
    MEPA_RC(lan8814_direct_reg_wr(dev, LAN8814_MMD_ACCESS_ADDR_DATA, addr, LAN8814_DEF_MASK));
    MEPA_RC(lan8814_direct_reg_wr(dev, LAN8814_MMD_ACCESS_CTRL,
                                  LAN8814_F_MMD_ACCESS_CTRL_MMD_FUNC | mmd, LAN8814_DEF_MASK));

    // write the value
    MEPA_RC(lan8814_direct_reg_wr(dev, LAN8814_MMD_ACCESS_ADDR_DATA, value, mask));
    return MEPA_RC_OK;
}

static mepa_rc lan8814_get_device_info(mepa_device_t *dev)
{
    lan8814_data_t *data = (lan8814_data_t *)dev->data;
    uint16_t id;

    (void)RD(dev, LAN8814_DEVICE_ID_2, &id);

    data->dev.model = (uint8_t)LAN8814_X_DEV_ID_MODEL(id);
    data->dev.rev = (uint8_t)LAN8814_X_DEV_ID_REV(id);
    T_I(MEPA_TRACE_GRP_GEN, "model 0x%x rev %d\n", data->dev.model, data->dev.rev);

    return MEPA_RC_OK;
}

static mepa_rc lan8814_init_conf(mepa_device_t *dev)
{
    lan8814_data_t *data = (lan8814_data_t *) dev->data;
    uint16_t val;

    (void)lan8814_get_device_info(dev);

    // Set config only for base port of phy.
    if (lan8814_is_lan8814(dev)) {
        if ((data->packet_idx % 4U) == 0U) {
            //EP_WR(dev, LAN8814_CHIP_HARD_RESET, 1);
            MEPA_MSLEEP(1);

            if (!data->qsgmii_phy_aneg_dis) {
                // Disable QSGMII auto-negotiation common for 4 ports.
                (void)EP_WRM(dev, LAN8814_QSGMII_AUTO_ANEG, 0, LAN8814_F_QSGMII_AUTO_ANEG_AUTO_ANEG_ENA);
                data->qsgmii_phy_aneg_dis = TRUE;
            }
        }
    }

    // Clear all GPHY interrupts during initialisation
    (void)WR(dev, LAN8814_GPHY_INTR_ENA, 0);
    (void)EP_WRM(dev, LAN8814_PTP_TSU_INT_EN, 0, LAN8814_DEF_MASK);
    (void)RD(dev, LAN8814_GPHY_INTR_STATUS, &val);
    // Enable chip level interrupt
    (void)EP_WRM(dev, LAN8814_INTR_CTRL, LAN8814_INTR_CTRL_CHIP_LVL_ENA, LAN8814_INTR_CTRL_CHIP_LVL_ENA);
    return MEPA_RC_OK;
}

// Configure qsgmii aneg advertisement capabilities
static void lan8814_qsgmii_tx_abilities(mepa_device_t *dev, mepa_port_speed_t speed, mepa_bool_t duplex)
{
    uint16_t value = 0x1; // default disable EEE enable & EEE clock stop
    uint16_t eee_val;

    (void)MMD_RD(dev, LAN8814_LINK_PARTNER_EEE_ABILITY, &eee_val);
    if (speed == MEPA_SPEED_1G) {
        value |= ((eee_val & LAN8814_F_LP_EEE_ABILITY_1000_BT) != 0U) ? 0x180U : 0U;
    } else if (speed == MEPA_SPEED_100M) {
        value |= ((eee_val & LAN8814_F_LP_EEE_ABILITY_100_BT) != 0U) ? 0x180U : 0U;
    } else {
        // other speeds: nothing to add
    }
    if (duplex) {
        value |= LAN8814_BIT(12);
    }
    value |= (speed == MEPA_SPEED_1G) ? LAN8814_BIT(10) : ((speed == MEPA_SPEED_100M) ? LAN8814_BIT(9) : 0U);
    value |= LAN8814_BIT(14);
    (void)EP_WR(dev, LAN8814_QSGMII_PCS1G_ANEG_TX_ADVERTISE_CAP, value);

    (void)EP_WRM(dev, LAN8814_QSGMII_PCS1G_ANEG_CONFIG, LAN8814_F_QSGMII_PCS1G_ANEG_CONFIG_ANEG_RESTART, LAN8814_F_QSGMII_PCS1G_ANEG_CONFIG_ANEG_RESTART);
}

static mepa_rc lan8814_qsgmii_aneg(mepa_device_t *dev, mepa_bool_t ena)
{
    if (lan8814_is_lan966x(dev)) {
        return MEPA_RC_OK;
    }
    T_I(MEPA_TRACE_GRP_GEN, "qsgmii aneg ena %d", ena);
    if (!ena) {
        // Disable QSGMII auto-negotiation
        (void)EP_WRM(dev, LAN8814_QSGMII_PCS1G_ANEG_CONFIG, 0, LAN8814_F_QSGMII_PCS1G_ANEG_CONFIG_ANEG_ENA);
    } else {
        // Enable QSGMII auto-negotiation.
        (void)EP_WRM(dev, LAN8814_QSGMII_PCS1G_ANEG_CONFIG, LAN8814_F_QSGMII_PCS1G_ANEG_CONFIG_ANEG_ENA, LAN8814_F_QSGMII_PCS1G_ANEG_CONFIG_ANEG_ENA);
    }
    return MEPA_RC_OK;
}

static mepa_rc lan8814_rev_workaround(mepa_device_t *dev)
{
    lan8814_data_t *data = (lan8814_data_t *) dev->data;
    uint16_t val;

    // work-arounds applicable for both models lan8814 & lan966x internal phy
    do {
        // work-around for Rev C done.
        if (data->dev.rev >= LAN8814_REV_C0) {
            break;
        }
        // MDI-X setting for swap A,B transmit
        (void)EP_WRM(dev, LAN8814_ALIGN_SWAP, LAN8814_F_ALIGN_TX_A_B_SWAP, LAN8814_M_ALIGN_TX_SWAP);
    } while (false);
    // work-around for model lan966x internal PHY only
    if (lan8814_is_lan966x(dev) && data->dev.rev <= LAN8814_REV_C0) {
        (void)EP_WR(dev, LAN8814_1000BT_FIX_LATENCY_ENABLE, 1);
        // In LAN8814 internal phy clock generation stops when link goes down.
        (void)EP_WR(dev, LAN8814_CLOCK_MANAGEMENT_MODE_5, 0x27e);
        (void)EP_RD(dev, LAN8814_LINK_QUALITY_MONITOR_SETTING, &val);
        // Enable cr_debug_mode
        // This forces LAN8814 internal phy clock generation even when link is down.
        (void)EP_WRM(dev, LAN8814_OPERATION_MODE_STRAP_LOW,  0x8, 0x8);
    }
    // work-around for model lan8814_is_lan966x done.
    if (lan8814_is_lan966x(dev)) {
        return MEPA_RC_OK;
    }
    // work-arounds applicable for only model lan8814
    // Rev A, B, C
    // PLL trim
    (void)EP_WR(dev, LAN8814_ANALOG_CONTROL_1, 0x40);
    (void)EP_WR(dev, LAN8814_ANALOG_CONTROL_10, 0x1);

    // Fix LED issue. It was noticed that when traffic is passing and then
    // the cable is removed the LED was still on. (UNG_INDY_QGPHY-800)
    (void)EP_WRM(dev, LAN8814_1000_EEE_STATE_REMAPPING, 0, LAN8814_F_1000_EEE_MASK2P5P);

    // Rev C work-around done.
    if (data->dev.rev >= LAN8814_REV_C0) {
        return MEPA_RC_OK;
    }
    // Rev B work-around done.
    if (data->dev.rev >= LAN8814_REV_B) {
        return MEPA_RC_OK;
    }
    (void)EP_WR(dev, LAN8814_OPERATION_MODE_STRAP_LOW, 0x2);
    (void)EP_WR(dev, LAN8814_OPERATION_MODE_STRAP_HIGH, 0xc001);

    T_D(MEPA_TRACE_GRP_GEN, "rev A work-around configured");
    return MEPA_RC_OK;
}

static mepa_rc lan8814_workaround_after_reset(mepa_device_t *dev)
{
    //737 Clause 14 UNH Fix
    (void)EP_WR(dev, LAN8814_AFED_CONTROL, 0xe214);
    (void)EP_WR(dev, LAN8814_ANALOG_CONTROL_4, 0x81e0);

    //639 Clause 40 EEE Fix
    (void)EP_WR(dev, LAN8814_EEE_WAKE_TX_TIMER, 0x1f);

    //Fix Intel PHY Interop issue JIRA 557
    (void)EP_WR(dev, LAN8814_ALIGN_SWAP, 0x02);

    //PLL trim fix for JIRA 564
    (void)EP_WR(dev, LAN8814_ANALOG_CONTROL_1, 0x40);
    (void)EP_WR(dev, LAN8814_ANALOG_CONTROL_10, 0x01);

    //Cable performance for 130m
    (void)EP_WR(dev, LAN8814_PD_CONTROLS, 0x248b);
    (void)EP_WR(dev, LAN8814_DFE_INIT2_100, 0x3c30);
    (void)EP_WR(dev, LAN8814_PGA_TABLE_1G_ENTRY_0, 0x10a);
    (void)EP_WR(dev, LAN8814_PGA_TABLE_1G_ENTRY_1, 0xed);
    (void)EP_WR(dev, LAN8814_PGA_TABLE_1G_ENTRY_2, 0xd3);
    (void)EP_WR(dev, LAN8814_PGA_TABLE_1G_ENTRY_3, 0xbc);
    (void)EP_WR(dev, LAN8814_PGA_TABLE_1G_ENTRY_4, 0xa8);
    (void)EP_WR(dev, LAN8814_PGA_TABLE_1G_ENTRY_5, 0x96);
    (void)EP_WR(dev, LAN8814_PGA_TABLE_1G_ENTRY_6, 0x85);
    (void)EP_WR(dev, LAN8814_PGA_TABLE_1G_ENTRY_7, 0x77);
    (void)EP_WR(dev, LAN8814_PGA_TABLE_1G_ENTRY_8, 0x6a);
    (void)EP_WR(dev, LAN8814_PGA_TABLE_1G_ENTRY_9, 0x5e);
    (void)EP_WR(dev, LAN8814_PGA_TABLE_1G_ENTRY_10, 0x54);
    (void)EP_WR(dev, LAN8814_PGA_TABLE_1G_ENTRY_11, 0x4b);
    (void)EP_WR(dev, LAN8814_PGA_TABLE_1G_ENTRY_12, 0x43);
    (void)EP_WR(dev, LAN8814_PGA_TABLE_1G_ENTRY_13, 0x3c);
    (void)EP_WR(dev, LAN8814_PGA_TABLE_1G_ENTRY_14, 0x35);
    (void)EP_WR(dev, LAN8814_PGA_TABLE_1G_ENTRY_15, 0x2f);
    (void)EP_WR(dev, LAN8814_PGA_TABLE_1G_ENTRY_16, 0x2a);
    (void)EP_WR(dev, LAN8814_PGA_TABLE_1G_ENTRY_17, 0x26);

    // Magjack center tapped ports
    (void)EP_WR(dev, LAN8814_POWER_MGMT_MODE_3, 0x6677);
    (void)EP_WR(dev, LAN8814_POWER_MGMT_MODE_4, 0x6677);
    (void)EP_WR(dev, LAN8814_POWER_MGMT_MODE_5, 0x6677);
    (void)EP_WR(dev, LAN8814_POWER_MGMT_MODE_6, 0x6677);
    (void)EP_WR(dev, LAN8814_POWER_MGMT_MODE_7, 0x0077);
    (void)EP_WR(dev, LAN8814_POWER_MGMT_MODE_8, 0x4377);
    (void)EP_WR(dev, LAN8814_POWER_MGMT_MODE_9, 0x4377);
    (void)EP_WR(dev, LAN8814_POWER_MGMT_MODE_10, 0x6677);
    (void)EP_WR(dev, LAN8814_POWER_MGMT_MODE_11, 0x0777);
    (void)EP_WR(dev, LAN8814_POWER_MGMT_MODE_12, 0x0777);
    (void)EP_WR(dev, LAN8814_POWER_MGMT_MODE_13, 0x6677);
    (void)EP_WR(dev, LAN8814_POWER_MGMT_MODE_14, 0x6677);

    //28.2.7.a and 28.2.1.d fix JIRA 569 & JIRA 568
    (void)WR(dev, LAN8814_BASIC_CONTROL, 0x1340);
    (void)WR(dev, LAN8814_ANEG_ADVERTISEMENT, 0x05e1);

    return MEPA_RC_OK;
}

// Work-around for 10M, 100M speed with half duplex configuration.
static mepa_rc lan8814_workaround_half_duplex(mepa_device_t *dev)
{
    (void)EP_WR(dev, LAN8814_RX_RA_FIFO_THRESHOLDS_1, 0x200);
    (void)EP_WR(dev, LAN8814_RX_RA_FIFO_THRESHOLDS_2, 0x404);
    (void)EP_WR(dev, LAN8814_RX_RA_FIFO_THRESHOLDS_3, 0x608);
    (void)EP_WR(dev, LAN8814_TX_RA_FIFO_THRESHOLDS_1, 0x200);
    (void)EP_WR(dev, LAN8814_TX_RA_FIFO_THRESHOLDS_2, 0x404);
    (void)EP_WR(dev, LAN8814_TX_RA_FIFO_THRESHOLDS_3, 0x608);
    return MEPA_RC_OK;
}

// If earlier speed,duplex are 10M,100M with half duplex config, work-around applied earlier needs to be cleaned up.
static mepa_rc lan8814_workaround_fifo_reset(mepa_device_t *dev)
{
    (void)EP_WR(dev, LAN8814_TX_RA_FIFO_RESET, 1);
    (void)EP_WR(dev, LAN8814_RX_RA_FIFO_RESET, 1);
    return MEPA_RC_OK;
}

// Frame pre-emption bit must be set before setting TSU_ENABLE bit in TSU_GENERAL_CONFIG register.
// It also implies that it must be called before lan8814_ts_mode_set API.
static mepa_rc lan8814_framepreempt_set_(mepa_device_t *dev, mepa_bool_t const enable)
{
    uint16_t val;
    lan8814_data_t *data = (lan8814_data_t *)dev->data;
    mepa_device_t *base_dev = data->base_dev;
    lan8814_data_t *base_data = (base_dev != NULL) ? ((lan8814_data_t *)(base_dev->data)) : NULL;

    if (base_data == NULL) {
        return MEPA_RC_OK;
    }

    //Update base port with reset_conf::framepreempt_en
    if (base_dev == dev) {
        base_data->framepreempt_en = enable;
    }

    //Set Frame Preemption
    val = 0;
    (void)EP_RD(dev, LAN8814_PTP_TSU_GEN_CONF, &val);
    if (base_data->framepreempt_en) {
        val |= LAN8814_PTP_TSU_GEN_CONF_PREEMPTION_EN;
    } else {
        val &= ~LAN8814_PTP_TSU_GEN_CONF_PREEMPTION_EN;
    }
    (void)EP_WRM(dev, LAN8814_PTP_TSU_GEN_CONF, val, LAN8814_DEF_MASK);

    return MEPA_RC_OK;
}

static mepa_rc lan8814_selftest_stop(struct mepa_device *dev)
{
    uint16_t val;

    (void)EP_RD(dev, LAN8814_SELFTEST_PGEN_EN, &val);

    if ((val & LAN8814_F_SELFTEST_PGEN_EN) != 0U) {
        // stop self-test
        (void)EP_WR(dev, LAN8814_SELFTEST_PGEN_EN, 0);
        // Disable Ext_lpbk bit in the Reserved Register
        (void)WR(dev, LAN8814_RESV_CON_LOOP, 0xfc00);
    }

    return MEPA_RC_OK;
}

static mepa_rc lan8814_conf_mdi_mode(mepa_device_t *dev, const mepa_media_mode_t mode)
{
    lan8814_data_t *data = (lan8814_data_t *)dev->data;
    mepa_media_mode_t old_mode = data->conf.mdi_mode;
    uint16_t val;

    if (old_mode == mode) {
        return MEPA_RC_OK;
    }

    /*
      BIT(7) = mdi_set
        mdi_set has no function when swapoff (reg28.6) is de-asserted.
        When swapoff is asserted, if mdi_set is asserted, chip will operate at MDI mode.
        When swapoff is asserted, if mdi_set is de-asserted, chip will operate at MDI-X
        mode.
      BIT(6) = swapoff
        1 = disable auto crossover function.
        0 = enable auto crossover function.
     */

    // Read current MDI settings
    (void)RD(dev, LAN8814_GPHY_DBG_CTL1, &val);
    val &= ~LAN8814_F_MDI_SET;
    switch (mode) {
    case MEPA_MEDIA_MODE_MDI:
        val |= LAN8814_F_MDI_SET;
        val |= LAN8814_F_SWAPOFF;
        break;
    case MEPA_MEDIA_MODE_MDIX:
        val |= LAN8814_F_SWAPOFF;
        break;
    default:
        val &= ~LAN8814_F_SWAPOFF;
        break;
    }
    // Set the current MDI
    (void)WRM(dev, LAN8814_GPHY_DBG_CTL1, val, LAN8814_DEF_MASK);

    // Update local cache
    data->conf.mdi_mode = mode;

    T_D(MEPA_TRACE_GRP_GEN, "port %d old mdi_mode %d, new mdi_mode %d",
        data->port_no, old_mode, data->conf.mdi_mode);

    return MEPA_RC_OK;
}

// Enable events
static mepa_rc lan8814_event_enable_set_(mepa_device_t *dev, mepa_event_t event, mepa_bool_t enable)
{
    mepa_rc rc = MEPA_RC_OK;
    uint16_t ev_mask = 0, i, val;
    lan8814_data_t *data = (lan8814_data_t *)dev->data;
    mepa_event_t ev_in = event;
    data->events = enable ? (data->events | event) :
                   (data->events & ~event);
    for (i = 0; i < (sizeof(mepa_event_t) * 8U); i++) {
        switch (ev_in & ((mepa_event_t)1U << i)) {
        case MEPA_LINK_LOS:
            ev_mask = ev_mask | LAN8814_F_GPHY_INTR_ENA_LINK_DOWN; // bit2
            ev_in = ev_in & ~MEPA_LINK_LOS;
            break;
        case MEPA_FAST_LINK_FAIL:
            // Enable Fast link config
            val = LAN8814_FLF_CFG_STAT_LINK_DOWN | LAN8814_FLF_CFG_STAT_FLF_ENABLE;
            (void)EP_WRM(dev, LAN8814_FLF_CONFIG_STATUS, enable ? val : 0U, val);

            ev_mask = ev_mask | LAN8814_F_GPHY_INTR_ENA_FLF_INTR; // bit 12
            ev_in = ev_in & ~MEPA_FAST_LINK_FAIL;
            break;
        default:
            // Not yet implemented
            break;
        }
        // If all events are processed, break.
        if (ev_in == 0U) {
            break;
        }
    }

    (void)WRM(dev, LAN8814_GPHY_INTR_ENA, enable ? ev_mask : 0U, ev_mask);
    T_I(MEPA_TRACE_GRP_GEN, "events enabled 0x%x \n", data->events);

    return rc;
}

static mepa_rc lan8814_conf_set_(mepa_device_t *dev, const mepa_conf_t *config)
{
    lan8814_data_t *data = (lan8814_data_t *)dev->data;
    uint16_t new_value, mask, old_value;
    mepa_bool_t restart_aneg = FALSE;
    mepa_bool_t qsgmii_aneg = config->speed != MEPA_SPEED_AUTO ? FALSE : config->mac_if_aneg_ena;

    if (config->admin.enable) {
        // Check & configure MDI mode as needed
        (void)lan8814_conf_mdi_mode(dev, config->mdi_mode);

        if (qsgmii_aneg != data->conf.mac_if_aneg_ena) {
            (void)lan8814_qsgmii_aneg(dev, qsgmii_aneg);
        }
        // Disable fast link failure during link configure to prevent false alarm.
        (void)WRM(dev, LAN8814_GPHY_INTR_ENA, 0, LAN8814_F_GPHY_INTR_ENA_FLF_INTR);
        if (config->speed == MEPA_SPEED_AUTO || config->speed == MEPA_SPEED_1G) {
            if ((data->conf.speed == MEPA_SPEED_10M || data->conf.speed == MEPA_SPEED_100M) &&
                !data->conf.fdx) {
                (void)lan8814_workaround_fifo_reset(dev);
            }
            if (data->conf.admin.enable != config->admin.enable) {
                restart_aneg = TRUE;
            }

            // 1G manual negotiation & speed
            new_value = config->aneg.speed_1g_fdx ? LAN8814_F_ANEG_MSTR_SLV_CTRL_1000_T_FULL_DUP : 0U;
            mask = LAN8814_F_ANEG_MSTR_SLV_CTRL_1000_T_FULL_DUP;
            if (config->man_neg != MEPA_MANUAL_NEG_DISABLED) {
                new_value |= LAN8814_F_ANEG_MSTR_SLV_CTRL_CFG_ENA;
                new_value |= (config->man_neg == MEPA_MANUAL_NEG_REF) ? LAN8814_F_ANEG_MSTR_SLV_CTRL_CFG_VAL : 0U;
            }
            mask |= (LAN8814_F_ANEG_MSTR_SLV_CTRL_CFG_VAL | LAN8814_F_ANEG_MSTR_SLV_CTRL_CFG_ENA);
            if (config->aneg.speed_1g_fdx != data->conf.aneg.speed_1g_fdx ||
                ((config->man_neg != MEPA_MANUAL_NEG_DISABLED) && (config->man_neg != data->conf.man_neg))) {
                restart_aneg = TRUE;
            }
            (void)WRM(dev, LAN8814_ANEG_MSTR_SLV_CTRL, new_value, mask);

            // Set up auo-negotiation advertisement in register 4
            new_value = (((uint16_t)(config->aneg.tx_remote_fault ? 1U : 0U) << 13U) |
                         ((uint16_t)(config->flow_control ? 1U : 0U) << 11U) |
                         ((uint16_t)(config->flow_control ? 1U : 0U) << 10U) |
                         ((uint16_t)(config->aneg.speed_100m_fdx ? 1U : 0U) << 8U) |
                         ((uint16_t)(config->aneg.speed_100m_hdx ? 1U : 0U) << 7U) |
                         ((uint16_t)(config->aneg.speed_10m_fdx ? 1U : 0U) << 6U) |
                         ((uint16_t)(config->aneg.speed_10m_hdx ? 1U : 0U) << 5U) |
                         ((uint16_t)1U << 0U)); // default selector field - 1
            (void)RD(dev, LAN8814_ANEG_ADVERTISEMENT, &old_value);
            if (old_value != new_value) {
                restart_aneg = TRUE;
            }
            (void)WR(dev, LAN8814_ANEG_ADVERTISEMENT, new_value);
            // Enable & restart auto-negotiation
            new_value = LAN8814_F_BASIC_CTRL_ANEG_ENA;
            (void)WRM(dev, LAN8814_BASIC_CONTROL, new_value, new_value | LAN8814_F_BASIC_CTRL_SOFT_POW_DOWN);
            if (restart_aneg) {
                T_I(MEPA_TRACE_GRP_GEN, "Aneg restarted on port %d", data->port_no);
                (void)WRM(dev, LAN8814_BASIC_CONTROL, LAN8814_F_BASIC_CTRL_RESTART_ANEG, LAN8814_F_BASIC_CTRL_RESTART_ANEG);
            }
        } else if (config->speed != MEPA_SPEED_UNDEFINED) {
            if ((data->conf.speed == MEPA_SPEED_10M || data->conf.speed == MEPA_SPEED_100M) &&
                !data->conf.fdx) {
                (void)lan8814_workaround_fifo_reset(dev);
            }
            T_I(MEPA_TRACE_GRP_GEN, "forced speed %d configured\n", config->speed);
            new_value = ((uint16_t)((config->speed == MEPA_SPEED_100M) ? 1U : 0U) << 13U) | ((uint16_t)0U << 12U) |
                        ((uint16_t)(config->fdx ? 1U : 0U) << 8U) |
                        ((uint16_t)((config->speed == MEPA_SPEED_1G) ? 1U : 0U) << 6U);
            mask = LAN8814_BIT(13) | LAN8814_BIT(12) | LAN8814_BIT(8) | LAN8814_BIT(6) | LAN8814_F_BASIC_CTRL_SOFT_POW_DOWN | LAN8814_F_BASIC_CTRL_ANEG_ENA;
            (void)WRM(dev, LAN8814_BASIC_CONTROL, new_value, mask);

            // half duplex work-around for 10M, 100M speeds
            if (!config->fdx) {
                (void)lan8814_workaround_half_duplex(dev);
            }
        } else {
            // speed not handled here
        }
    } else {
        T_I(MEPA_TRACE_GRP_GEN, "set power down\n");
        // set soft power down bit
        (void)WRM(dev, LAN8814_BASIC_CONTROL, LAN8814_F_BASIC_CTRL_SOFT_POW_DOWN, LAN8814_F_BASIC_CTRL_SOFT_POW_DOWN);
    }

    if (lan8814_is_lan966x(dev)) {
        /* APPL-5492:
           9662 platform: set bit 14 in reg 31. The bit is defined as reserved, but used
           as 'polarity invert' for CU-phy interrupts. Due to
           interrupts being constantly active (interrupt storm), interrupts needs to be
           changed to active high instead of active low. By flipping the bit, we instruct to do so
        */
        (void)WRM(dev, LAN8814_CONTROL, LAN8814_F_CONTROL_RESERVED, LAN8814_F_CONTROL_RESERVED);
    }

    // Poll before enabling interrupt events again.
    (void)RD(dev, LAN8814_GPHY_INTR_STATUS, &new_value);
    T_D(MEPA_TRACE_GRP_GEN, "events during configuration 0x%x\n", new_value);
    data->conf = *config;
    data->conf.mac_if_aneg_ena = qsgmii_aneg;

    // Enable events again.
    (void)lan8814_event_enable_set_(dev, data->events, TRUE);
    return MEPA_RC_OK;
}

#if !defined MEPA_LAN8814_LIGHT
static mepa_rc lan8814_eee_mode_conf_set_(mepa_device_t *dev, const mepa_phy_eee_conf_t conf)
{
    lan8814_data_t *data = (lan8814_data_t *)(dev->data);
    BOOL reconfigure = FALSE;

    if (conf.eee_mode == MEPA_EEE_REG_UPDATE) {
        // Called with re-configure registers but don't change state
        reconfigure = TRUE;
        data->eee_conf.eee_mode = conf.eee_ena_phy ? MEPA_EEE_ENABLE : MEPA_EEE_DISABLE;
        data->eee_conf.eee_ena_phy = conf.eee_ena_phy;
    } else if ((conf.eee_ena_phy) && (conf.eee_mode == MEPA_EEE_DISABLE)) {
        // current state is Enabled, New state is disable. Re-configure registers and change state.
        reconfigure = TRUE;
        data->eee_conf.eee_ena_phy = FALSE;
    } else if (!(conf.eee_ena_phy) && (conf.eee_mode == MEPA_EEE_ENABLE)) {
        // current state is Disabled, New state is enable. Re-configure registers and change state.
        reconfigure = TRUE;
        data->eee_conf.eee_ena_phy = TRUE;
    }

    // Copy the EEE mode to lan8814_data_t only when eee_mode is not REG_UPDATE
    if (conf.eee_mode != MEPA_EEE_REG_UPDATE) {
        data->eee_conf.eee_mode = conf.eee_mode;
    }

    if (reconfigure) {
        T_I(MEPA_TRACE_GRP_GEN, "New EEE Enable = %d, port = %d", data->eee_conf.eee_mode, data->port_no);
        // Enable Or Disable EEE
        if (data->eee_conf.eee_ena_phy) {
            T_I(MEPA_TRACE_GRP_GEN, "EEE Enabled: 100/1000BaseT Advertisements for Port = %d", data->port_no);
            MMD_WR(dev, LAN8814_EEE_ADVERTISEMENT, (LAN8814_EEE_100_BT | LAN8814_EEE_1000_BT)); // Enable 100BaseT and 1000BaseT advertisement
        } else {
            MMD_WR(dev, LAN8814_EEE_ADVERTISEMENT, 0); //Disable EEE Advertisement
        }
        // Restart Autonegotiation
        T_I(MEPA_TRACE_GRP_GEN, "restart auto neg - Needed for disable/enable EEE advertisement, port", data->port_no);
        WRM(dev, LAN8814_BASIC_CONTROL, LAN8814_F_BASIC_CTRL_RESTART_ANEG, LAN8814_F_BASIC_CTRL_RESTART_ANEG);
    }

    return MEPA_RC_OK;
}
#endif


static mepa_rc lan8814_reset_(mepa_device_t *dev, const mepa_reset_param_t *rst_conf)
{
    lan8814_data_t *data = (lan8814_data_t *) dev->data;

    switch (rst_conf->reset_point) {
    case MEPA_RESET_POINT_DEFAULT:
        if (!data->init_done) {
            (void)lan8814_init_conf(dev);
            (void)lan8814_rev_workaround(dev);
            (void)lan8814_qsgmii_aneg(dev, FALSE);
            data->init_done = TRUE;
            data->rep_cnt = (data->rep_cnt != 0U) ? data->rep_cnt : 1U;
            if (lan8814_is_lan8814(dev)) {
                data->crc_workaround = TRUE;
                data->aneg_after_link_up = FALSE;
            }
        }
        //Clear self-test if enabled before reset
        (void)lan8814_selftest_stop(dev);
        (void)WRM(dev, LAN8814_BASIC_CONTROL, LAN8814_F_BASIC_CTRL_SOFT_RESET, LAN8814_F_BASIC_CTRL_SOFT_RESET);
        MEPA_MSLEEP(1);
        // Some of the work-around registers get cleared after reset. So, they are called here
        // after every reset.
        (void)lan8814_workaround_after_reset(dev);
        T_I(MEPA_TRACE_GRP_GEN, "Reconfiguring the phy after reset");
        // Reconfigure the phy after reset
        (void)lan8814_conf_set_(dev, &data->conf); // Has its own sets of ENTER/EXIT
        // EEE is Disabled on Power Up
        data->eee_conf.eee_mode = MEPA_EEE_REG_UPDATE;
#if !defined MEPA_LAN8814_LIGHT
        lan8814_eee_mode_conf_set_(dev, data->eee_conf);
#endif
        if (data->events != 0U) {
            (void)lan8814_event_enable_set_(dev, data->events, TRUE);
        }
        // To avoid qsgmii serdes and Gphy blocks settling in different speeds, use qsgmii soft reset and restart aneg.
        // This must be applied after Mac serdes is configured
        if (lan8814_is_lan8814(dev)) {
            (void)EP_WR(dev, LAN8814_QSGMII_SOFT_RESET, 0x1);
            (void)WRM(dev, LAN8814_BASIC_CONTROL, LAN8814_F_BASIC_CTRL_RESTART_ANEG, LAN8814_F_BASIC_CTRL_RESTART_ANEG);
            data->post_mac_rst = TRUE;
        }
        break;
    default:
        T_D(MEPA_TRACE_GRP_GEN, "Unhandled reset point %d", rst_conf->reset_point);
        break;
    }

    /* Recommended to Use MEPA API "mepa_framepreempt_set" to Enable/Disable Frame Preemption */
    //Configure frame preemption
    (void)lan8814_framepreempt_set_(dev, rst_conf->framepreempt_en); // Has its own sets of ENTER/EXIT
    return MEPA_RC_OK;
}

mepa_rc lan8814_rep_count_set(mepa_device_t *dev, const uint8_t rep_cnt)
{
    lan8814_data_t *data = (lan8814_data_t *) dev->data;
    data->rep_cnt = rep_cnt;
    return MEPA_RC_OK;
}

mepa_rc lan8814_downshift_conf_set(mepa_device_t *dev, const lan8814_phy_downshift_t *dsh)
{
    lan8814_data_t *data = (lan8814_data_t *) dev->data;
    MEPA_ENTER(dev);

    // For rev D of lan8814, and lan8842 the downshift seems to be fixed in
    // the silicon.
    // According to the datasheet if it fails to link after 4 attempts it will
    // restart autonegotiation at lower speeds. Therefore it is required to
    // check what is the dsh_thr_cnt for this revision.
    if (data->dev.rev >= LAN8814_REV_D || lan8814_is_lan8842(dev)) {
        if (dsh->dsh_enable == TRUE &&
            dsh->dsh_thr_cnt != MEPA_PHY_DOWNSHIFT_CNT_4) {
            return MEPA_RC_ERR_PARM;
        }

        // Enable downshift in HW, it is not required to restart
        // auto-negotiation as it gets restarted when this is enabled.
        (void)EP_WRM(dev, LAN8814_PCS_1000_TEST_4,
               dsh->dsh_enable ? LAN8814_F_PCS_1000_TEST_4_AUTO_DOWNSHIFT_ENABLE : 0U,
               LAN8814_F_PCS_1000_TEST_4_AUTO_DOWNSHIFT_ENABLE);
    }

    if (!dsh->dsh_enable && data->dsh_conf.dsh_enable) {
        (void)WRM(dev, LAN8814_BASIC_CONTROL, LAN8814_F_BASIC_CTRL_RESTART_ANEG, LAN8814_F_BASIC_CTRL_RESTART_ANEG);
    }
    data->dsh_conf.dsh_enable = dsh->dsh_enable;
    data->dsh_conf.dsh_thr_cnt = dsh->dsh_thr_cnt;
    MEPA_EXIT(dev);
    return MEPA_RC_OK;
}

static mepa_rc lan8814_downshift(mepa_device_t *dev)
{
    lan8814_data_t *data = (lan8814_data_t *)dev->data;
    uint16_t val = 0;
    // Max_Timer is the no of attempts the link status needs to be checked with a time interval of 1 secs.
    // Default value for Max_Timer will be 4 .
    (void)RD(dev, LAN8814_ANEG_MSTR_SLV_CTRL, &val);
    T_I(MEPA_TRACE_GRP_GEN, "Starting Downshift to 100M on port:%d", data->port_no);
    val &= ~(LAN8814_F_ANEG_MSTR_SLV_CTRL_CFG_VAL | LAN8814_F_ANEG_MSTR_SLV_CTRL_1000_T_FULL_DUP);
    T_I(MEPA_TRACE_GRP_GEN, "Speed Changed to 100M");
    (void)WRM(dev, LAN8814_ANEG_MSTR_SLV_CTRL, val, LAN8814_F_ANEG_MSTR_SLV_CTRL_1000_T_FULL_DUP | LAN8814_F_ANEG_MSTR_SLV_CTRL_CFG_VAL);
    (void)WRM(dev, LAN8814_BASIC_CONTROL, LAN8814_F_BASIC_CTRL_ANEG_ENA, LAN8814_F_BASIC_CTRL_ANEG_ENA | LAN8814_F_BASIC_CTRL_SOFT_POW_DOWN);
    // Restart aneg After downshift has been performed in the port
    (void)WRM(dev, LAN8814_BASIC_CONTROL, LAN8814_F_BASIC_CTRL_RESTART_ANEG, LAN8814_F_BASIC_CTRL_RESTART_ANEG);
    data->dsh_complete = true; // set a downshift complete flag
    return MEPA_RC_OK;
}

static mepa_rc mas_if_set(mepa_device_t *dev,
                          mepa_port_interface_t mac_if)
{
    if (mac_if != MESA_PORT_INTERFACE_SGMII) {
        return MEPA_RC_ERROR;
    }
    return MEPA_RC_OK;
}

static mepa_rc mas_if_get(mepa_device_t *dev, mepa_port_speed_t speed,
                          mepa_port_interface_t *mac_if)
{
    *mac_if = MESA_PORT_INTERFACE_SGMII;
    return MEPA_RC_OK;
}

// Returns gpio using port number and led number
static uint8_t led_num_to_gpio_mapping(mepa_device_t *dev, mepa_led_num_t led_num)
{
    lan8814_data_t *data = (lan8814_data_t *) dev->data;
    uint8_t gpio = 11;// port 0 as default.
    switch (data->packet_idx % 4U) {
    case 0U:
        if (lan8814_is_lan8814(dev) ||
            lan8814_is_lan966x(dev)) {
                gpio = (led_num == MEPA_LED0) ? 11U : 12U;
        }
        if (lan8814_is_lan8842(dev)) {
                gpio = (led_num == MEPA_LED0) ? 14U : 15U;
        }
        break;
    case 1U:
        gpio = (led_num == MEPA_LED0) ? 17U : 18U;
        break;
    case 2U:
        gpio = (led_num == MEPA_LED0) ? 19U : 20U;
        break;
    case 3U:
        gpio = (led_num == MEPA_LED0) ? 13U : 14U;
        break;
    /* coverity[dead_error_begin] */
    default:
        // unreachable: packet_idx % 4U is always 0..3
        break;
    }
    T_I(MEPA_TRACE_GRP_GEN, "gpio mapped %d\n", gpio);
    return gpio;
}
static uint8_t led_mepa_mode_to_lan8814(mepa_gpio_mode_t mode)
{
    uint8_t ret = 0;
    switch (mode) {
    case MEPA_GPIO_MODE_LED_LINK_ACTIVITY:
        ret = 0;
        break;
    case MEPA_GPIO_MODE_LED_LINK1000_ACTIVITY:
        ret = 1;
        break;
    case MEPA_GPIO_MODE_LED_LINK100_ACTIVITY:
        ret = 2;
        break;
    case MEPA_GPIO_MODE_LED_LINK10_ACTIVITY:
        ret = 3;
        break;
    case MEPA_GPIO_MODE_LED_LINK100_1000_ACTIVITY:
        ret = 4;
        break;
    case MEPA_GPIO_MODE_LED_LINK10_1000_ACTIVITY:
        ret = 5;
        break;
    case MEPA_GPIO_MODE_LED_LINK10_100_ACTIVITY:
        ret = 6;
        break;
    case MEPA_GPIO_MODE_LED_DUPLEX_COLLISION:
        ret = 8;
        break;
    case MEPA_GPIO_MODE_LED_COLLISION:
        ret = 9;
        break;
    case MEPA_GPIO_MODE_LED_ACTIVITY:
        ret = 10;
        break;
    case MEPA_GPIO_MODE_LED_AUTONEGOTIATION_FAULT:
        ret = 12;
        break;
    case MEPA_GPIO_MODE_LED_FORCE_LED_OFF:
        ret = 14;
        break;
    case MEPA_GPIO_MODE_LED_FORCE_LED_ON:
        ret = 15;
        break;
    case MEPA_GPIO_MODE_LED_DISABLE_EXTENDED:
        ret = 0xf0;
        break;
    default:
        ret = 0xff; // Not valid for lan8814
        break;
    }
    return ret;
}

// In LAN8814, LED0 of software Api maps to LED1 of hardware,
//          LED1 of software Api maps to LED2 of hardware.
static mepa_rc lan8814_led_mode_set(mepa_device_t *dev, mepa_gpio_mode_t led_mode, mepa_led_num_t led_num)
{
    uint16_t mode;

    // LAN8814 supports only LED0 and LED1
    if ((led_num != MEPA_LED0) && (led_num != MEPA_LED1)) {
        T_W(MEPA_TRACE_GRP_GEN, "%s supports only leds 0 & 1\n", lan8814_get_name(dev));
        return MEPA_RC_NOT_IMPLEMENTED;
    }
    if ((mode = led_mepa_mode_to_lan8814(led_mode)) == 0xffU) {// Not valid
        T_W(MEPA_TRACE_GRP_GEN, "Not valid led mode");
        return MEPA_RC_NOT_IMPLEMENTED;
    }
    if (mode == (uint8_t)MEPA_GPIO_MODE_LED_DISABLE_EXTENDED) {
        // Normal operation
        (void)EP_WRM(dev, LAN8814_LED_CONTROL_REG1, LAN8814_F_LED_CONTROL_KSZ_LED_MODE, LAN8814_F_LED_CONTROL_KSZ_LED_MODE);
    } else { // extended mode
        (void)EP_WRM(dev, LAN8814_LED_CONTROL_REG1, 0, LAN8814_F_LED_CONTROL_KSZ_LED_MODE);
        (void)EP_WRM(dev, LAN8814_LED_CONTROL_REG2, LAN8814_ENCODE_BITFIELD(mode, (uint16_t)led_num * 4U, 4), LAN8814_ENCODE_BITMASK((uint16_t)led_num * 4U, 4));
    }
    return MEPA_RC_OK;
}
static mepa_rc lan8814_gpio_mode_private(mepa_device_t *dev, const mepa_gpio_conf_t *data)
{
    uint16_t gpio_en = 0, dir = 0, val = 0, gpio_no = data->gpio_no;
    mepa_gpio_mode_t mode = data->mode;

    if (mode == MEPA_GPIO_MODE_OUT || mode == MEPA_GPIO_MODE_IN) {
        gpio_en = 1U;
        dir = (mode == MEPA_GPIO_MODE_OUT) ? 1U : 0U;
    } else if (mode >= MEPA_GPIO_MODE_LED_LINK_ACTIVITY && mode <= MEPA_GPIO_MODE_LED_DISABLE_EXTENDED) {
        MEPA_RC(lan8814_led_mode_set(dev, mode, data->led_num));
        // Enable alternative gpio mode for led.
        gpio_no = led_num_to_gpio_mapping(dev, data->led_num);
    } else if (mode == MEPA_GPIO_MODE_RCVRD_CLK_OUT1 || mode == MEPA_GPIO_MODE_RCVRD_CLK_OUT2) {
        gpio_no = (mode == MEPA_GPIO_MODE_RCVRD_CLK_OUT1) ? 9U : 10U;
    } else {
        // unsupported mode
    }
    if (gpio_no < 16U) {
        val = (uint16_t)((uint16_t)1U << gpio_no);
        dir = (uint16_t)(dir << gpio_no);
        (void)EP_WRM(dev, LAN8814_GPIO_EN2, (gpio_en != 0U) ? val : 0U, val);
        if (gpio_en != 0U) {
            (void)EP_WRM(dev, LAN8814_GPIO_DIR2, dir, val);
        }
    } else if (gpio_no < 24U) {
        val = (uint16_t)((uint16_t)1U << (gpio_no - 16U));
        dir = (uint16_t)(dir << (gpio_no - 16U));
        (void)EP_WRM(dev, LAN8814_GPIO_EN1, (gpio_en != 0U) ? val : 0U, val);
        if (gpio_en != 0U) {
            (void)EP_WRM(dev, LAN8814_GPIO_DIR1, dir, val);
        }
    } else {
        // Not supported.
        T_W(MEPA_TRACE_GRP_GEN, "Not valid gpio on %s phy", lan8814_get_name(dev));
    }
    return MEPA_RC_OK;
}

// wait in loop while cable diagnostics is running.
static mepa_bool_t lan8814_wait_for_cable_diagnostics(mepa_device_t *dev)
{
    uint8_t cnt = 0;
    uint16_t value;
    mepa_bool_t ret = FALSE;

    while (cnt < 100U) { // wait for utmost 1 second
        (void)RD(dev, LAN8814_CABLE_DIAG, &value);
        if ((value & LAN8814_F_CABLE_DIAG_TEST_ENA) != 0U) {
            MEPA_MSLEEP(10);
            cnt++;
        } else {
            ret = TRUE;
            break;
        }
    }

    T_D(MEPA_TRACE_GRP_GEN, "ret = %d cnt= %d value=0x%x\n", ret, cnt, value);
    return ret;
}

// Before starting cable diagnostics, do necessary phy configuration like reset speed config.
static mepa_rc lan8814_cab_diag_enter_config(mepa_device_t *dev)
{
    //Steps to be done before cable diagnostics
    uint16_t value = 0;
    (void)WRM(dev, LAN8814_BASIC_CONTROL, LAN8814_F_BASIC_CTRL_SOFT_RESET, LAN8814_F_BASIC_CTRL_SOFT_RESET);
    MEPA_MSLEEP(1);
    value = (LAN8814_F_BASIC_CTRL_SPEED_SEL_BIT_1 | LAN8814_F_BASIC_CTRL_DUP_MODE);
    (void)WR(dev, LAN8814_BASIC_CONTROL, value);
    (void)WRM(dev, LAN8814_ANEG_MSTR_SLV_CTRL,
        LAN8814_F_ANEG_MSTR_SLV_CTRL_CFG_ENA,
        LAN8814_F_ANEG_MSTR_SLV_CTRL_CFG_ENA |
        LAN8814_F_ANEG_MSTR_SLV_CTRL_CFG_VAL);
    (void)WRM(dev, LAN8814_GPHY_DBG_CTL1, LAN8814_F_SWAPOFF, LAN8814_F_SWAPOFF);
    MEPA_MSLEEP(50);
    return MEPA_RC_OK;
}

// After exiting cable diagnostics/self-test , restore phy configuration.
static mepa_rc lan8814_restore_config_(mepa_device_t *dev)
{
    mepa_reset_param_t rst_cfg = {};

    rst_cfg.reset_point = MEPA_RESET_POINT_DEFAULT;
    // Restore configuration after cable diagnostics/self-test.
    (void)lan8814_reset_(dev, &rst_cfg);
    return MEPA_RC_OK;
}

static void lan8814_cab_diag_start_test(mepa_device_t *dev, uint8_t pair)
{
    uint16_t value, mask;

    // clear diag test ena before starting
    (void)WRM(dev, LAN8814_CABLE_DIAG, 0, LAN8814_F_CABLE_DIAG_TEST_ENA);

    value = 0;
    mask = 0;

    value |= LAN8814_F_CABLE_DIAG_TEST_ENA;
    value |= LAN8814_F_CABLE_TEST_PAIR(pair);
    mask |= LAN8814_F_CABLE_DIAG_TEST_ENA | LAN8814_M_CABLE_TEST_PAIR |
            LAN8814_F_CABLE_VCT_SEL;
    (void)WRM(dev, LAN8814_CABLE_DIAG, value, mask);
}

static void lan8814_cab_diag_read_result(mepa_device_t *dev, uint8_t pair)
{
    lan8814_data_t *data = (lan8814_data_t *)dev->data;
    mepa_cable_diag_result_t *res = &data->cable_diag;
    uint16_t status, value;

    (void)RD(dev, LAN8814_CABLE_DIAG, &value);
    status = LAN8814_X_CABLE_DIAG_STATUS(value);
    if ((status == LAN8814_CABLE_OPEN) || (status == LAN8814_CABLE_SHORT)) {
        res->status[pair] = (status == LAN8814_CABLE_SHORT) ? MEPA_CABLE_DIAG_STATUS_SHORT : MEPA_CABLE_DIAG_STATUS_OPEN;
        {
            uint16_t diag_data = LAN8814_X_CABLE_DIAG_DATA(value);
            uint16_t diff = (diag_data > 22U) ? (uint16_t)(diag_data - 22U) : (uint16_t)(22U - diag_data);
            res->length[pair] = (uint8_t)((8U * diff) / 10U);
        }
        T_I(MEPA_TRACE_GRP_GEN, "pair=%d status=%d length=%d\n", pair, res->status[pair], res->length[pair]);
    } else if (status == LAN8814_CABLE_FAIL) {
        res->status[pair] = MEPA_CABLE_DIAG_STATUS_ABNORM;
        T_I(MEPA_TRACE_GRP_GEN, "link status failed for pair %d \n", pair);
    } else { // status as LAN8814_CABLE_NORMAL
        res->status[pair] = MEPA_CABLE_DIAG_STATUS_OK;
        T_I(MEPA_TRACE_GRP_GEN, "pair=%d status=%d \n", pair, status);
    }
}

// LAN8814 phy dignostics is calculated only when there is no remote link partner for the port.
// For mode values {0,1} corresponding to {VTSS_PHY_MODE_ANEG, VTSS_PHY_MODE_FORCED}, diagnostics is calculated.
// For power down mode(2), diagnostics is not calculated.
static mepa_rc lan8814_cab_diag_start_(mepa_device_t *dev, int32_t mode,
                                       mepa_bool_t async)
{
    lan8814_data_t *data = (lan8814_data_t *)dev->data;
    uint16_t pair, value;
    mepa_cable_diag_result_t *res = &data->cable_diag;

    T_I(MEPA_TRACE_GRP_GEN, "port=%d mode=%d\n", data->port_no, mode);

    // Initialize diagnostics
    for (pair = 0; pair < LAN8814_PAIRS; pair++) {
        res->status[pair] = MEPA_CABLE_DIAG_STATUS_UNKNOWN;
        res->length[pair] = 0;
        res->link = FALSE;
    }

    // return for power down mode
    if (mode == (int32_t)LAN8814_CABLE_MODE_POWER_DOWN) {
        return MEPA_RC_ERROR;
    }

    /* Updated the res->link parameter of cable_diag result based upon the poll status */
    (void)RD(dev, LAN8814_BASIC_STATUS, &value);
    res->link = ((value & LAN8814_F_BASIC_STATUS_LINK_STATUS) != 0U) ? true : false;

    if (async) {
        // Initialize the cable diag state for async
        for (pair = 0; pair < LAN8814_PAIRS; ++pair) {
            data->cable_diag_state[pair] = LAN8814_CABLE_DIAG_STATE_INIT;
        }
    }

    /* If link is Up do not perform cable diagnostics operation just return */
    if (res->link) {
        T_W(MEPA_TRACE_GRP_GEN, "Can't perform cable diagnostics while link is up");
        return MEPA_RC_OK;
    }

    (void)lan8814_cab_diag_enter_config(dev);

    if (async) {
        // Just exit for now as it is required to call lan8814_cab_diag_poll to
        // start and check when the test is finished.
        return MEPA_RC_OK;
    }

    for (pair = 0; pair < LAN8814_PAIRS; pair++) {
        // clear diag test ena before starting
        lan8814_cab_diag_start_test(dev, (uint8_t)pair);

        if (lan8814_wait_for_cable_diagnostics(dev)) {
            lan8814_cab_diag_read_result(dev, (uint8_t)pair);
        }
    }

    (void)lan8814_restore_config_(dev);
    return MEPA_RC_OK;
}

static mepa_rc lan8814_cab_diag_stop_async(mepa_device_t *dev)
{
    lan8814_data_t *data = (lan8814_data_t *)dev->data;
    uint16_t pair;

    MEPA_ENTER(dev);

    for (pair = 0; pair < LAN8814_PAIRS; ++pair) {
        data->cable_diag_state[pair] = LAN8814_CABLE_DIAG_STATE_INIT;
    }
    (void)lan8814_restore_config_(dev);

    MEPA_EXIT(dev);

    return MEPA_RC_OK;
}

static mepa_rc lan8814_cab_diag_poll(mepa_device_t *dev)
{
    lan8814_data_t *data = (lan8814_data_t *)dev->data;
    mepa_rc rc = MEPA_RC_ERROR;
    uint16_t pair, value;

    MEPA_ENTER(dev);

    for (pair = 0; pair < LAN8814_PAIRS; ++pair) {
        if (data->cable_diag_state[pair] == LAN8814_CABLE_DIAG_STATE_DONE) {
            continue;
        }

        if (data->cable_diag_state[pair] == LAN8814_CABLE_DIAG_STATE_INIT) {
            lan8814_cab_diag_start_test(dev, (uint8_t)pair);
            data->cable_diag_state[pair] = LAN8814_CABLE_DIAG_STATE_POLL;
        }

        if (data->cable_diag_state[pair] == LAN8814_CABLE_DIAG_STATE_POLL) {
            (void)RD(dev, LAN8814_CABLE_DIAG, &value);
            if ((value & LAN8814_F_CABLE_DIAG_TEST_ENA) == 0U) {
                lan8814_cab_diag_read_result(dev, (uint8_t)pair);
                data->cable_diag_state[pair] = LAN8814_CABLE_DIAG_STATE_DONE;
            } else {
                rc = MEPA_RC_INCOMPLETE;
                goto out;
            }
        }
    }

    (void)lan8814_restore_config_(dev);
    rc = MEPA_RC_OK;

out:
    MEPA_EXIT(dev);

    return rc;
}

#if !defined MEPA_LAN8814_LIGHT
static mepa_rc lan8814_port_flow_mapping_get(uint8_t packet_idx, mepa_bool_t ingress, int *map_val)
{
    switch (packet_idx % 4) {
    case 0:
        *map_val = ingress ? 1 : 0;
        break;
    case 1:
        *map_val = ingress ? 3 : 2;
        break;
    case 2:
        *map_val = ingress ? 5 : 4;
        break;
    case 3:
        *map_val = ingress ? 7 : 6;
        break;
    default:
        return MEPA_RC_ERROR;
    }

    return MEPA_RC_OK;
}
#endif

#if !defined MEPA_LAN8814_LIGHT
static mepa_rc lan8814_selftest_start_(struct mepa_device *dev, const mepa_selftest_info_t *inf)
{
    uint16_t val;
    uint8_t retry = 0;
    mepa_bool_t link_up = FALSE;

    if (inf == NULL) {
        return MEPA_RC_ERROR;
    }

    if ((inf->speed != MEPA_SPEED_1G) && (inf->mdi == MEPA_MEDIA_MODE_AUTO)) {
        return MEPA_RC_ERROR;
    }

    EP_RD(dev, LAN8814_SELFTEST_PGEN_EN, &val);
    if (val & LAN8814_F_SELFTEST_PGEN_EN) {
        T_W(MEPA_TRACE_GRP_GEN, "Self-Test already enabled!\n");
        goto en_err;
    }

    //Reset PHY
    WRM(dev, LAN8814_BASIC_CONTROL, LAN8814_F_BASIC_CTRL_SOFT_RESET, LAN8814_F_BASIC_CTRL_SOFT_RESET);
    //wait for reset complete
    MEPA_MSLEEP(1);

    /* 1. Setup basic configuration */
    // Set AN
    val = LAN8814_F_BASIC_CTRL_SPEED_SEL_BIT_1 |
          LAN8814_F_BASIC_CTRL_DUP_MODE |
          LAN8814_F_BASIC_CTRL_RESTART_ANEG |
          LAN8814_F_BASIC_CTRL_ANEG_ENA;
    WR(dev, LAN8814_BASIC_CONTROL, val);
    MEPA_MSLEEP(10);

    if (inf->speed == MEPA_SPEED_1G) {
        // Enable Ext_lpbk bit in the Reserved Register
        WR(dev, LAN8814_RESV_CON_LOOP, 0xfc08);

        // Set Master-Slave configuration to master
        val = LAN8814_F_ANEG_MSTR_SLV_CTRL_1000_T_FULL_DUP |
              LAN8814_F_ANEG_MSTR_SLV_CTRL_CFG_VAL |
              LAN8814_F_ANEG_MSTR_SLV_CTRL_CFG_ENA;
        WR(dev, LAN8814_ANEG_MSTR_SLV_CTRL, val);

    } else { /*< 10BASE-T and 100BASE-TX */
        // Disable Auto-MDIX and set the desired configuration (MDI vs. MDIX)
        RD(dev, LAN8814_GPHY_DBG_CTL1, &val);
        val &= ~LAN8814_F_MDI_SET;
        switch (inf->mdi) {
        case MEPA_MEDIA_MODE_MDI:
            val |= LAN8814_F_MDI_SET;
        // Fall through
        default:
            val |= LAN8814_F_SWAPOFF;
            break;
        }
        // Set the current MDI
        WRM(dev, LAN8814_GPHY_DBG_CTL1, val, LAN8814_DEF_MASK);
    }

    // Set force speed
    val = LAN8814_F_BASIC_CTRL_DUP_MODE;
    if (inf->speed == MEPA_SPEED_1G) {
        val |= LAN8814_F_BASIC_CTRL_SPEED_SEL_BIT_1;
    } else if (inf->speed == MESA_SPEED_100M) {
        val |= LAN8814_F_BASIC_CTRL_SPEED_SEL_BIT_0;
    }
    WR(dev, LAN8814_BASIC_CONTROL, val);

    // Wait for link-up
    while (retry++ < 100) {
        RD(dev, LAN8814_BASIC_STATUS, &val);
        if (val & LAN8814_F_BASIC_STATUS_LINK_STATUS) {
            link_up = TRUE;
            break;
        }

        MEPA_MSLEEP(10);
    }

    if (!link_up) {
        T_W(MEPA_TRACE_GRP_GEN, "Link Down!\n");
        goto ret_err;
    }

    // 0.5secs delay before starting self-test
    MEPA_MSLEEP(500);

    // 2. setup number of packet gen. will run forever if not set
    EP_WR(dev, LAN8814_SELFTEST_FRAME_CNT, 0);
    EP_WR(dev, LAN8814_SELFTEST_PKT_CNT_LO, (inf->frames & 0xFFFF));
    EP_WR(dev, LAN8814_SELFTEST_PKT_CNT_HI, ((inf->frames & 0xFFFF0000) >> 16));
    EP_WR(dev, LAN8814_SELFTEST_FRAME_CNT, LAN8814_F_SELFTEST_FRAME_CNT_EN);

    // 3. enable the checking counter
    EP_WR(dev, LAN8814_SELFTEST_EN_REG, 0); //disble checker to clear counter
    val = LAN8814_F_SELFTEST_RX_CRC_CHK_EN |
          LAN8814_F_SELFTEST_TX_CRC_CHK_EN |
          LAN8814_F_SELFTEST_EN;
    EP_WR(dev, LAN8814_SELFTEST_EN_REG, val); //enable checker

    // 4. Start self-test
    EP_WR(dev, LAN8814_SELFTEST_PGEN_EN, LAN8814_F_SELFTEST_PGEN_EN);

    return MEPA_RC_OK;

ret_err:
    // Restore PHY Config
    lan8814_restore_config_(dev);
    return MEPA_RC_ERROR;

en_err:
    return MEPA_RC_ERROR;
}
#endif

static mepa_rc lan8814_serdes_set(mepa_device_t *dev, uint16_t addr, uint16_t data, uint8_t op_rd)
{
    uint16_t val;

    (void)EP_WR(dev, LAN8814_SERDES_CR_ADDR, addr);
    if (op_rd == 0U) {
        (void)EP_WR(dev, LAN8814_SERDES_CR_DATA, data);
        (void)EP_RD(dev, LAN8814_SERDES_CR_CONTROL, &val);
        val |= LAN8814_F_SERDES_CR_CONTROL_1 | LAN8814_F_SERDES_CR_CONTROL_0;
        (void)EP_WR(dev, LAN8814_SERDES_CR_CONTROL, val);
    } else {
        (void)EP_RD(dev, LAN8814_SERDES_CR_CONTROL, &val);
        val &= ~LAN8814_F_SERDES_CR_CONTROL_1;
        val |= LAN8814_F_SERDES_CR_CONTROL_0;
        (void)EP_WR(dev, LAN8814_SERDES_CR_CONTROL, val);
        (void)EP_RD(dev, LAN8814_SERDES_CR_DATA, &val);
    }
    return MEPA_RC_OK;
}

struct serd_set {
    uint16_t addr;
    uint16_t data;
    uint8_t op_rd;
};

static mepa_rc lan8814_prbs_init(mepa_device_t *dev)
{
    mepa_rc rc;
    size_t i;
    struct serd_set serdes_settings[] = {
        {0x1018, 0x0aa8, 0},
        {0x1018, 0x0550, 0},
        {0x0011, 0x004c, 0}, // Disable rtune req (SSP_CR_SUP_DIG_MPLL_OVRD_IN_LO.SSP_CR_SUP_DIG_MPLL_OVRD_IN_LO_RTUNE_REQ)
        {0x0011, 0x084c, 0}, // Enable override (SSP_CR_SUP_DIG_MPLL_OVRD_IN_LO.SSP_CR_SUP_DIG_MPLL_OVRD_IN_LO_RTUNE_REQ_OVRD)
        {0x1001, 0x0040, 0}, // Set Tx Reset high (LANEX_DIG_TX_OVRD_IN_HI.TX_RESET)
        {0x1001, 0x00c0, 0}, // Enable override (LANEX_DIG_TX_OVRD_IN_HI.TX_RESET_OVRD)
        {0x1006, 0x1000, 0}, // Set Rx reset high (LANEX_DIG_RX_OVRD_IN_HI.RX_RESET)
        {0x1006, 0x3000, 0}, // Enable override (LANEX_DIG_RX_OVRD_IN_HI.RX_RESET_OVRD)
        {0x0011, 0x084e, 0}, // Enable override (SUP_DIG_MPLL_OVRD_IN_LO.MPLL_EN_OVRD)
        {0x1000, 0x0200, 0}, // Enable override (LANEX.DIG.TX.OVRD_IN_LO.TX_CM_EN_OVRD)
        {0x1000, 0x0280, 0}, // Enable override (LANEX.DIG.TX.OVRD_IN_LO.TX_EN_OVRD)
        {0x1005, 0x0008, 0}, // Enable override (LANEX.DIG.RX.OVRD_IN_LO.RX_PLL_EN_OVRD)
        {0x1005, 0x0028, 0}, // Enable override (LANEX_DIG_RX_OVRD_IN_LO.RX_DATA_EN_OVRD)
        {0x1000, 0x02a0, 0}, // Enable override (LANEX_DIG_TX_OVRD_IN_LO.TX_DATA_EN_OVRD)
        {0x0015, 0x0000, 0}, // Set MPLL Half Rate to 0
        {0x0015, 0x1000, 0},
        {0x0012, 0x0000, 0}, // Do not use FSEL override pins for multiplier for ssp (SUP.DIG.MPLL_OVRD_IN_HI.FSEL)
        {0x0012, 0x0200, 0}, // Enable FSEL override (SUP.DIG.MPLL_OVRD_IN_HI.FSEL_OVRD)
        {0x0013, 0x0000, 0}, // Write Fraction multipler to: 0x000 (SUP_DIG_SSC_OVRD_IN.SSC_REF_CLK_SEL)
        {0x0013, 0x0000, 0}, // Disable ssc override (SUP_DIG_SSC_OVRD_IN.SSC_EN)
        {0x0013, 0x2000, 0}, // Enable override (SUP_DIG_SSC_OVRD_IN.SSC_OVRD_IN_EN)
        {0x1001, 0x00c0, 0}, // Set TX lane rate (LANEX_DIG_TX_OVRD_IN_HI.TX_RATE) to 00
        {0x1001, 0x00c4, 0}, // Enable override (LANEX_DIG_TX_OVRD_IN_HI.TX_RATE_OVRD)
        {0x1006, 0x3000, 0}, // Set RX lane rate (LANEX_DIG_RX_OVRD_IN_HI.RX_RATE) to 00
        {0x1006, 0x3004, 0}, // Enable override (LANEX_DIG_RX_OVRD_IN_HI.RX_RATE_OVRD)
        {0x0012, 0x8200, 0}, // Set tx_vboost_lvl
        {0x0015, 0x5000, 0}, // Set los_bias
        {0x0015, 0x5009, 0}, // Set los_level
        {0x0015, 0x5029, 0}, // Set acjt_level
        {0x0015, 0x5429, 0}, // Set enable for tx_vboost_lvl/los_bias/los_level/acjt_level
        {0x1002, 0x007f, 0}, // Set the TX amplitude (LANEX_DIG_TX_OVRD_DRV_LO.AMPLITUDE)
        {0x1002, 0x0c7f, 0}, // Set the TX preemphasis (LANEX_DIG_TX_OVRD_DRV_LO.PREEMPH)
        {0x1002, 0x4c7f, 0}, // Enable override (LANEX_DIG_TX_OVRD_DRV_LO.EN)
        {0x1003, 0x0000, 0}, // Override tx_term_offset to 0 (LANE0_DIG_TX_OVRD_DRV_HI.TERM_OFFSET)
        {0x1003, 0x0020, 0}, // Enable override (LANE0_DIG_TX_OVRD_DRV_HI.EN)
        {0x0003, 0x0010, 0}, // Set Rtune to TX(SUP.DIG.RTUNE_DEBUG.TYPE)
        {0x0003, 0x0012, 0}, // Force Rtune High(SUP.DIG.RTUNE_DEBUG.MAN_TUNE)
        {0x0003, 0x0010, 0}, // Force Rtune Low(SUP.DIG.RTUNE_DEBUG.MAN_TUNE)
        {0x1001, 0x01c4, 0}, // Set tx_vboost_en (LANEX.DIG.TX.OVRD_IN_HI.TX_VBOOST_EN
        {0x1001, 0x03c4, 0}, // Enable override (LANEX.DIG.TX.OVRD_IN_LO.TX_VBOOST_EN_OVRD)
        {0x1006, 0x3004, 0}, // Set Rx Eq to use a fixed setting (LANEX.DIG.RX.OVRD_IN_HI.RX_EQ_EN)
        {0x1006, 0x3084, 0}, // Enable override (LANEX.DIG.RX.OVRD_IN_HI.RX_EQ_EN_OVRD)
        {0x1006, 0x3284, 0}, // Set Rx Eq value(LANEX_DIG_RX_OVRD_IN_HI.RX_EQ)
        {0x1006, 0x3a84, 0}, // Enable override (LANEX_DIG_RX_OVRD_IN_HI.RX_EQ_OVRD)
        {0x1006, 0x3a9c, 0}, // Set Rx Los filter to XAUI mode (LANEX.DIG.RX.OVRD_IN_HI.RX_LOS_FILTER)
        {0x1006, 0x3abc, 0}, // Enable override (LANEX.DIG.RX.OVRD_IN_HI.RX_LOS_FILTER_OVRD)
        {0x1005, 0x0028, 0}, // Set Rx Bit shift to 0(LANEX.DIG.RX.OVRD_IN_LO.RX_BIT_SHIFT)
        {0x1005, 0x0228, 0}, // Enable override (LANEX.DIG.RX.OVRD_IN_LO.RX_BIT_SHIFT_OVRD)
        {0x1005, 0x0228, 0}, // Set Rx invert bit to: 0 (LANEX.DIG.RX.OVRD_IN_LO.RX_INVERT)
        {0x1005, 0x022a, 0}, // Enable override (LANEX.DIG.RX.OVRD_IN_LO.RX_INVERT_OVRD)
        {0x1005, 0x122a, 0}, // Enable rx los(LANEX.DIG.RX.OVRD_IN_LO.RX_LOS_EN)
        {0x1005, 0x322a, 0}, // Enable override (LANEX.DIG.RX.OVRD_IN_LO.RX_LOS_EN_OVRD)
        {0x1005, 0x326a, 0}, // Set Rx Align(LANEX_DIG_RX_OVRD_IN_LO.RX_ALIGN_EN)
        {0x1005, 0x32ea, 0}, // Enable override (LANEX_DIG_RX_OVRD_IN_LO.RX_ALIGN_EN_OVRD)
        {0x1005, 0x36ea, 0}, // Set Term enable(LANEX_DIG_RX_OVRD_IN_LO.RX_TERM_EN)
        {0x1005, 0x3eea, 0}, // Enable override (LANEX_DIG_RX_OVRD_IN_LO.RX_TERM_EN_OVRD)
        {0x0012, 0x8600, 0}, // Reset MPll. Set bit to 1 (SUP_DIG_MPLL_OVRD_IN_HI.MPLL_RST)
        {0x0012, 0x8200, 0}, // Reset MPll. Set bit to 0 (SUP_DIG_MPLL_OVRD_IN_HI.MPLL_RST)
        {0x1015, 0x03e0, 0}, // Set pattern generator to send commas, not commas (LANEX_DIG_TX_LBERT_CTL.PAT0)
        {0x1015, 0x03e6, 0}, // Set pattern generator to correct mode (LANEX_DIG_TX_LBERT_CTL.MODE)
        {0x1001, 0x0384, 0}, // Set Tx Reset low (LANEX_DIG_TX_OVRD_IN_HI.TX_RESET)
        {0x1006, 0x2abc, 0}, // Set Rx reset low (LANEX_DIG_RX_OVRD_IN_HI.RX_RESET)
        {0x1005, 0x3eee, 0}, // Turn on RX PLL Enable (LANEX.DIG.RX.OVRD_IN_LO.RX_PLL_EN)
        {0x1005, 0x3efe, 0}, // Enable Rx(LANEX_DIG_RX_OVRD_IN_LO.RX_DATA_EN)
        {0x101c, 0x0000, 1}, // Verify CDR has locked
        {0x1005, 0x3ebe, 0}, // Turn off data alignment(LANEX_DIG_RX_OVRD_IN_LO.RX_ALIGN_EN)
        {0x1015, 0x25c6, 0}, // Set patten generator to selected pattern(LANEX_DIG_TX_LBERT_CTL.PAT0)
    };
    size_t arr_len = (sizeof(serdes_settings) / sizeof(struct serd_set));

    for (i = 0; i < arr_len; i++) {
        rc = lan8814_serdes_set(dev, serdes_settings[i].addr, serdes_settings[i].data, serdes_settings[i].op_rd);
        if (rc < 0) {
            return rc;
        }
    }

    return MEPA_RC_OK;
}

static mepa_rc lan8814_prbs_clk(mepa_device_t *dev, mepa_prbs_clock_t clk)
{
    mepa_rc rc;
    size_t i;
    uint16_t val;

    (void)EP_RD(dev, LAN8814_SERDES_CLOCK_CONF, &val);
    if (clk == MEPA_PRBS_CLK125_MHZ && val == 0x0016U) {
        struct serd_set serdes_settings[] = {
            {0x0010, 0x0001, 0}, // Set ref_clock divider value to 1 (SUP_DIG_ATEOVRD.ref_clkdiv2)
            {0x0011, 0x08a2, 0}, // Set multiplier value (SUP_DIG_MPLL_OVRD_IN_LO.MPLL_MULTIPLIER)
            {0x0011, 0x0aa2, 0}, // Enable override (SUP_DIG_MPLL_OVRD_IN_LO.MPLL_MULTIPLIER_OVRD)
            {0x0011, 0x0aa3, 0}, // Enable MPLL (SUP_DIG_MPLL_OVRD_IN_LO.MPLL_EN)
        };
        size_t arr_len = (sizeof(serdes_settings) / sizeof(struct serd_set));

        for (i = 0; i < arr_len; i++) {
            rc = lan8814_serdes_set(dev, serdes_settings[i].addr, serdes_settings[i].data, serdes_settings[i].op_rd);
            if (rc < 0) {
                return rc;
            }
        }

    } else if (clk == MEPA_PRBS_CLK25_MHZ && val == 0x0006U) {
        struct serd_set serdes_settings[] = {
            {0x0010, 0x0000, 0}, // Set ref_clock divider value to 0 (SUP_DIG_ATEOVRD.ref_clkdiv2)
            {0x0011, 0x0992, 0}, // Set multiplier value (SUP_DIG_MPLL_OVRD_IN_LO.MPLL_MULTIPLIER)
            {0x0011, 0x0b92, 0}, // Enable override (SUP_DIG_MPLL_OVRD_IN_LO.MPLL_MULTIPLIER_OVRD)
            {0x0011, 0x0b93, 0}, // Enable MPLL (SUP_DIG_MPLL_OVRD_IN_LO.MPLL_EN)
        };
        size_t arr_len = (sizeof(serdes_settings) / sizeof(struct serd_set));

        for (i = 0; i < arr_len; i++) {
            rc = lan8814_serdes_set(dev, serdes_settings[i].addr, serdes_settings[i].data, serdes_settings[i].op_rd);
            if (rc < 0) {
                return rc;
            }
        }
    } else if (clk == MEPA_PRBS_CLK125_MHZ && val == 0x0006U) {
        return MEPA_RC_ERROR;
    } else {
        return MEPA_RC_ERROR;
    }
    return MEPA_RC_OK;
}

static mepa_rc lan8814_prbs_loopback(mepa_device_t *dev)
{
    mepa_rc rc;
    size_t i;
    struct serd_set serdes_settings[] = {
        {0x1000, 0x02a1, 0}, // Enable internal loopback (LANEX.DIG.TX.OVRD_IN_LO.LOOPBK_EN)
        {0x1000, 0x02a3, 0}, // Enable override (LANEX.DIG.TX.OVRD_IN_LO.TX_LOOPBK_EN_OVRD)
        {0x1000, 0x02a3, 0}, // Turn off Beacon Enable (LANEX.DIG.TX.OVRD_IN_LO.TX_BEACON_EN)
        {0x1000, 0x0aa3, 0}, // Enable override (LANEX.DIG.TX.OVRD_IN_LO.TX_BEACON_EN_OVRD)
        {0x1000, 0x0aa3, 0}, // Turn off tx_rx detect (LANEX.DIG.TX.OVRD_IN_LO.TX_DETECT_RX_REQ)
        {0x1000, 0x2aa3, 0}, // Enable override (LANEX.DIG.TX.OVRD_IN_LO.TX_DETECT_RX_REQ_OVRD)
        {0x1000, 0x2aa3, 0}, // Turn off tx invert (LANEX.DIG.TX.OVRD_IN_LO.TX_INVERT)
        {0x1000, 0x2aab, 0}, // Enable override (LANEX.DIG.TX.OVRD_IN_LO.TX_INVERT_OVRD)
        {0x1000, 0x2bab, 0}, // Set TX CM Enable (LANEX.DIG.TX.OVRD_IN_LO.TX_CM_EN)
        {0x1000, 0x2beb, 0}, // Turn on TX Enable (LANEX.DIG.TX.OVRD_IN_LO.TX_EN)
        {0x1000, 0x2bfb, 0}, // Set Tx Data Enable high(LANEX_DIG_TX_OVRD_IN_LO.TX_DATA_EN)
    };
    size_t arr_len = (sizeof(serdes_settings) / sizeof(struct serd_set));

    for (i = 0; i < arr_len; i++) {
        rc = lan8814_serdes_set(dev, serdes_settings[i].addr, serdes_settings[i].data, serdes_settings[i].op_rd);
        if (rc < 0) {
            return rc;
        }
    }

    return rc;
}

static mepa_rc lan8814_prbs_enable(mepa_device_t *dev, mepa_prbs_pattern_t prbs_pattern)
{
    mepa_rc rc;
    size_t i;
    struct serd_set serdes_settings[] = {
        {0x1015, 0x0000, 0},
        {0x1015, 0x25c0, 0}, // Set patten generator to selected mode(LANEX_DIG_TX_LBERT_CTL.MODE)
        {0x1016, 0x0000, 0}, // Set patten matcher to selected mode(LANEX_DIG_RX_LBERT_CTL.MODE)
        {0x1016, 0x0000, 0}, // Sync pattern matcher low(LANEX_DIG_RX_LBERT_CTL.SYNC)
        {0x1016, 0x0010, 0}, // Sync pattern matcher high(LANEX_DIG_RX_LBERT_CTL.SYNC)
        {0x1016, 0x0000, 0}, // Sync pattern matcher low(LANEX_DIG_RX_LBERT_CTL.SYNC)
        {0x1016, 0x0010, 0}, // Sync the patten matchers high (LANEX.DIG.RX.LBERT_CTL.SYNC)
        {0x1016, 0x0000, 0}, // Sync the pattern matchers low (LANEX.DIG.RX.LBERT_CTL.SYNC)
    };
    size_t arr_len = (sizeof(serdes_settings) / sizeof(struct serd_set));

    for (i = 0; i < arr_len; i++) {
        // The last 4 bits in the data describe which MODE to operate into. A
        // value of 0 means disable, value 1 means lfsr31, 2 lfsr23, 3 lfsr15, 4
        // lfsr7. There more modes which we are currently not supporting them.
        // This applies both to the LANEN_DIG_RX_LBERT_CTL and
        // LANEN_DIG_TX_LBERT_CTL registers.
        if (prbs_pattern == MEPA_PRBS7) {
            serdes_settings[i].data |= 0x4U;
        }
        if (prbs_pattern == MEPA_PRBS15) {
            serdes_settings[i].data |= 0x3U;
        }
        if (prbs_pattern == MEPA_PRBS23) {
            serdes_settings[i].data |= 0x2U;
        }
        if (prbs_pattern == MEPA_PRBS31) {
            serdes_settings[i].data |= 0x1U;
        }
        rc = lan8814_serdes_set(dev, serdes_settings[i].addr, serdes_settings[i].data, serdes_settings[i].op_rd);
        if (rc < 0) {
            return rc;
        }
    }
    return MEPA_RC_OK;
}

static mepa_rc lan8814_prbs_set_priv(mepa_device_t *dev, mepa_bool_t enable, mepa_prbs_clock_t clk, mepa_prbs_loopback_t loopback, mepa_prbs_pattern_t prbs_pattern)
{
    mepa_rc rc = MEPA_RC_OK;

    if (enable) {
        // Based on the tests, it looks like the prbs needs to be configured
        // differently if we run internal compared to external looback. For the
        // external loopback there is not need to run the need, init, loopback
        // or clock configuration.
        if (loopback == MEPA_PRBS_INTERNAL_LOOPBACK) {
            rc = lan8814_prbs_init(dev);
            if (rc < 0 ) {
                return rc;
            }
            rc = lan8814_prbs_loopback(dev);
            if (rc < 0 ) {
                return rc;
            }
            rc = lan8814_prbs_clk(dev, clk);
            if (rc < 0 ) {
                return rc;
            }
        }

        rc = lan8814_prbs_enable(dev, prbs_pattern);
        if (rc < 0 ) {
            return rc;
        }
    } else {
        rc = lan8814_serdes_set(dev, 0x1015, 0x0000, 0);
        if (rc < 0) {
            return rc;
        }

        //QSGMII Hard Reset
        if (loopback == MEPA_PRBS_INTERNAL_LOOPBACK) {
            (void)EP_WR(dev, LAN8814_QSGMII_HARD_RESET, 0x1);
        }
    }

    return MEPA_RC_OK;
}

#if !defined MEPA_LAN8814_LIGHT
static void lan8814_phy_deb_pr_reg (mepa_device_t *dev,
                                    lmu_ss_t *const ss,
                                    uint16_t mmd, uint16_t page, uint16_t addr,
                                    const char *str, uint16_t *value)
{
    mepa_rc rc = MEPA_RC_OK;
    lan8814_data_t *data = (lan8814_data_t *)dev->data;
    mepa_port_no_t port_no = data->port_no;
    uint16_t id = page;

    if (mmd) {
        id = mmd;
        rc = lan8814_mmd_reg_rd(dev, mmd, addr, value);
    } else if (page) {
        rc = lan8814_ext_reg_rd(dev, (page - 1), addr, value);
    } else {
        rc = lan8814_direct_reg_rd(dev, addr, value);
    }
    if (ss && (MEPA_RC_OK == rc)) {
        pr("%-45s:  0x%02x  0x%02x   0x%04x     0x%08x\n", str, to_u32(port_no), id, addr, *value);
    }
}
#endif

#if !defined MEPA_LAN8814_LIGHT
static mepa_rc lan8814_reg_dump(struct mepa_device *dev,
                                lmu_ss_t *const ss)
{
    uint16_t val = 0;
    uint16_t id = 0;

    //Direct registers
    pr("%-45s   PORT_NO PAGE_ID REG_ADDR   VALUE\n", "REG_NAME");
    pr("Main Page Registers\n");
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 0, "Basic Control Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 1, "Basic Status Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 2, "Device Identifier 1 Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 3, "Device Identifier 2 Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 4, "Auto-Negotiation Advertisement Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 5, "Auto-Negotiation Link Partner Base Page Ability Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 6, "Auto-Negotiation Expansion Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 7, "Auto-Negotiation Next Page TX Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 8, "Auto-Negotiation Next Page RX Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 9, "Auto-Negotiation Master Slave Control Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 10, "Auto-Negotiation Master Slave Status Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 13, "MMD Access Control Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 14, "MMD Access Address/Data Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 15, "Extended Status Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 16, "PCS Loop-back Lane Skew Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 17, "PCS Loop-back Swap/Polarity Control Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 18, "Cable Diagnostic Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 19, "Digital PMA/PCS Status Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 20, "Digital AX/AN Status Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 21, "RXER Counter Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 22, "EP Access Control Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 23, "EP Access Address/Data Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 24, "GPHY Interrupt Enable Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 25, "GPHY Revision Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 26, "UNH Test Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 27, "GPHY Interrupt Status Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 28, "Digital Debug Control 1 Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 29, "Digital Debug Control 2 Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 30, "Reserved Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 0, 31, "Control Register", &val);

    //Extended page-0,1,2,3,4,5,7,28,29,31 registers
    pr("Extended Page-0 Registers\n");
    lan8814_phy_deb_pr_reg(dev, ss, 0, 1, 0, "Debug-Mode First-Level-Select Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 1, 1, "Debug-Mode Second-Level-Select for DIGITOP Part 1 Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 1, 2, "Debug-Mode Second-Level-Select for DIGITOP Part 2 Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 1, 3, "Auto-Negotiation Timer Register 1", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 1, 4, "Auto-Negotiation Timer Register 2", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 1, 5, "Auto-Negotiation Timer Register 3", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 1, 6, "Auto-Negotiation Timer Register 4", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 1, 7, "Auto-Negotiation Timer Register 5", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 1, 8, "Auto-Negotiation Timer Register 6", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 1, 9, "Auto-Negotiation Timer Register 7", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 1, 10, "MDIX Select Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 1, 11, "Max-Timer for 1.24 Millisecond Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 1, 12, "Auto-Negotiation Wait Timer Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 1, 13, "Max Link Timer Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 1, 14, "Debug Bus Option Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 1, 15, "Fast Link Fail (FLF) Configuration and Status Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 1, 16, "Link Partner Force FD Override Register", &val);

    pr("Extended Page-1 Registers\n");
    for (id = 0; id < 239; id++) {
        lan8814_phy_deb_pr_reg(dev, ss, 0, 2, id, "Extended Page 1 Registers", &val);
    }

    pr("Extended Page-2 Registers\n");
    for (id = 0; id < 111; id++) {
        lan8814_phy_deb_pr_reg(dev, ss, 0, 3, id, "Extended Page 2 Registers", &val);
    }

    pr("Extended Page-3 Registers\n");
    for (id = 0; id < 28; id++) {
        lan8814_phy_deb_pr_reg(dev, ss, 0, 4, id, "Extended Page 3 Registers", &val);
    }

    pr("Extended Page-4 Registers\n");
    for (id = 0; id < 772; id++) {
        lan8814_phy_deb_pr_reg(dev, ss, 0, 5, id, "Extended Page 4 Registers", &val);
    }

    pr("Extended Page-5 Registers\n");
    for (id = 0; id < 708; id++) {
        lan8814_phy_deb_pr_reg(dev, ss, 0, 6, id, "Extended Page 5 Registers", &val);
    }

    pr("Extended Page-7 Registers\n");
    lan8814_phy_deb_pr_reg(dev, ss, 0, 8, 58, "EP7 Register 58", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 8, 59, "EP7 Register 59", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 8, 62, "EEE Link Partner Ability Override Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 8, 63, "EEE Message Code Register", &val);

    pr("Extended Page-28 Registers\n");
    for (id = 0; id < 80; id++) {
        lan8814_phy_deb_pr_reg(dev, ss, 0, 29, id, "Extended Page 28 Registers", &val);
    }

    pr("Extended Page-29 Registers\n");
    for (id = 0; id < 80; id++) {
        lan8814_phy_deb_pr_reg(dev, ss, 0, 30, id, "Extended Page 29 Registers", &val);
    }

    pr("Extended Page-31 Registers\n");
    lan8814_phy_deb_pr_reg(dev, ss, 0, 32, 0, "Speed Mode with TESTBUS Control Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 32, 8, "Clock Management Mode 0", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 32, 9, "Clock Management Mode 1", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 32, 10, "Clock Management Mode 2", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 32, 11, "Clock Management Mode 3", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 32, 12, "Clock Management Mode 4", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 32, 13, "Clock Management Mode 5", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 32, 14, "Clock Management Mode 6", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 32, 15, "Clock Management Mode 7", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 32, 16, "Clock Management Mode 8", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 0, 32, 17, "Clock Management Mode 9", &val);

    //MMD-3,7 registers
    pr("%-45s   PORT_NO DEV_ID REG_ADDR   VALUE\n", "REG_NAME");
    pr("MMD-3 Registers\n");
    lan8814_phy_deb_pr_reg(dev, ss, 3, 0x0, 0, "PCS Control 1 Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 3, 0x0, 1, "PCS Status 1 Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 3, 0x0, 20, "EEE Control and Capability Register", &val);

    pr("MMD-7 Registers\n");
    lan8814_phy_deb_pr_reg(dev, ss, 7, 0x0, 60, "EEE Advertisement Register", &val);
    lan8814_phy_deb_pr_reg(dev, ss, 7, 0x0, 61, "EEE Link Partner Ability Register", &val);

    return MEPA_RC_OK;
}
#endif

static mepa_rc lan8814_reset(mepa_device_t *dev, const mepa_reset_param_t *rst_conf)
{
    mepa_rc rc;

    MEPA_ENTER(dev);
    rc = lan8814_reset_(dev, rst_conf);
    MEPA_EXIT(dev);

    return rc;
}

static mepa_rc lan8814_delete(mepa_device_t *dev)
{
    return mepa_delete_int(dev);
}

mepa_rc lan8814_poll_priv(mepa_device_t *dev, mepa_status_t *status)
{
    uint16_t val, val2, val3 = 0;
    lan8814_data_t *data = (lan8814_data_t *) dev->data;
    uint8_t speed;

    // MEPA-835: Downshift happens when port is put into power down. Return the link status as slow
    if (!data->conf.admin.enable) {
        T_D(MEPA_TRACE_GRP_GEN, "Polling cannot be done as Port %d is powered Down", data->port_no);
        (void)memset(status, 0, sizeof(mepa_status_t));
        data->link_status = status->link;
        data->speed_status = status->speed;
        data->fdx_status   = status->fdx;
        data->loop_cnt = 0;
        data->aneg_flag = false;
        data->dsh_complete = false;
        return MESA_RC_OK;
    }
    (void)RD(dev, LAN8814_BASIC_STATUS, &val);
    status->link = ((val & LAN8814_F_BASIC_STATUS_LINK_STATUS) != 0U);

    if (data->loopback.near_end_ena == TRUE) {
        // loops back to Mac. Ignore Line side status to Link partner.
        status->link = true;
    } else if (data->loopback.connector_ena == TRUE) {
        uint8_t bit0;
        uint8_t bit1;
        (void)RD(dev, LAN8814_BASIC_CONTROL, &val2);
        bit0 = ((val2 & LAN8814_F_BASIC_CTRL_SPEED_SEL_BIT_0) != 0U) ? 1U : 0U;
        bit1 = ((val2 & LAN8814_F_BASIC_CTRL_SPEED_SEL_BIT_1) != 0U) ? 1U : 0U;
        speed = (uint8_t)(bit0 | (uint8_t)(bit1 << 1U));
        status->speed = (speed == 0U) ? MEPA_SPEED_10M :
                        (speed == 1U) ? MEPA_SPEED_100M :
                        (speed == 2U) ? MEPA_SPEED_1G : MEPA_SPEED_UNDEFINED;
        status->fdx = ((val2 & LAN8814_F_BASIC_CTRL_DUP_MODE) != 0U);
        goto end;
    } else {
        // no loopback active
    }

    if (data->conf.speed == MEPA_SPEED_AUTO || data->conf.speed == MEPA_SPEED_1G) {
        uint16_t lp_sym_pause = 0, lp_asym_pause = 0;
        // Default values
        status->speed = MEPA_SPEED_UNDEFINED;
        status->fdx = true;
        // check if auto-negotiation is completed or not
        if (!data->loopback.near_end_ena && status->link && ((val & LAN8814_F_BASIC_STATUS_ANEG_COMPLETE) == 0U)) {
            T_I(MEPA_TRACE_GRP_GEN, "Aneg is not completed for port %d", data->port_no);
            status->link = false;
        } else if (data->loopback.near_end_ena) {
            status->speed = MEPA_SPEED_1G;
        } else {
            // aneg complete and not in near-end loopback
        }

        // Auto Downshift Feature: Downshift allows an interface to link at a lower advertised speed when unable to establish a stable link at the maximum speed.
        // Both the Downshift and MEPA 555 ANEG State Machine Stuck Fix has same criteria. When Downshift is enabled, the Restart ANEG happens first and still couldn't
        // establish the link then perform downshift to 100M.
        // The Software workaround for downshift is needed only for lan8814 for
        // revisions previous to revision D and for lan966x internal ports. That
        // means the SW workaround is not needed for lan8814 revision D and
        // lan8842.
        (void)RD(dev, LAN8814_DIGITAL_AX_AN_STATUS, &val3);
        (void)RD(dev, LAN8814_CONTROL, &val2);
        if (data->dev.rev < LAN8814_REV_D && !lan8814_is_lan8842(dev)) {
            if (data->dsh_conf.dsh_enable && !status->link && (((val2 & LAN8814_F_1000T_SPEED_STATUS) != 0U) && ((val3 & LAN8814_F_LINK_DET) != 0U) && data->aneg_flag) && !data->dsh_complete) {
                data->loop_cnt++;
                if (data->loop_cnt > ((uint32_t)data->dsh_conf.dsh_thr_cnt * (uint32_t)data->rep_cnt)) {
                    (void)lan8814_downshift(dev);
                    data->loop_cnt = 0;
                    data->aneg_flag = FALSE;
                    T_I(MEPA_TRACE_GRP_GEN, "Downshift on port %d", data->port_no);
                }
                T_I(MEPA_TRACE_GRP_GEN, "Downshift capable on port %d dsh_loop_cnt%d rep_cnt %d", data->port_no, data->loop_cnt, data->rep_cnt);
            }
        }

        // MEPA 555: This is a SW workaround for the ANEG state machine hung.
        // Check link up and aneg status complete when a valid signal is detected from link partner wait for 2 secs, couldn't establish link restart ANEG state Machine.
        if (!status->link && ((val & LAN8814_F_BASIC_STATUS_ANEG_COMPLETE) == 0U) && ((val3 & LAN8814_F_SIG_DET) != 0U) && !data->aneg_flag) {
            data->loop_cnt++;
            T_I(MEPA_TRACE_GRP_GEN, "Aneg not complete on port %d loop_cnt%d rep_cnt %d", data->port_no, data->loop_cnt, data->rep_cnt);
            if (data->loop_cnt > (2U * data->rep_cnt)) { // default value for rep cnt to be 1sec
                T_I(MEPA_TRACE_GRP_GEN, "Aneg state machine stuck!! restarting ANEG on port %d", data->port_no);
                data->loop_cnt = 0;
                (void)WRM(dev, LAN8814_BASIC_CONTROL, LAN8814_F_BASIC_CTRL_RESTART_ANEG, LAN8814_F_BASIC_CTRL_RESTART_ANEG);
                data->aneg_flag = TRUE;
            }
        }

        if (!status->link || data->loopback.near_end_ena) {
            // No need to read aneg values when link is down or when near-end loopback enabled.
            goto end;
        }

        // Obtain speed and duplex from link partner's advertised capability.
        (void)RD(dev, LAN8814_ANEG_LP_BASE, &val);
        (void)RD(dev, LAN8814_ANEG_MSTR_SLV_STATUS, &val2);
        // 1G half duplex is not supported. Refer direct register - 9
        if ((((val2 & LAN8814_F_ANEG_MSTR_SLV_STATUS_1000_T_FULL_DUP) != 0U) &&
             data->conf.aneg.speed_1g_fdx) && !data->dsh_complete) {
            // Work-around for CRC errors begin.
            if (data->crc_workaround) {
                if ((((val2 & LAN8814_F_ANEG_MSTR_SLV_LOCAL_RCVR_STATUS) == 0U) ||
                      ((val2 & LAN8814_F_ANEG_MSTR_SLV_REMOTE_RCVR_STATUS) == 0U)) ||
                    !data->post_mac_rst) {
                    //link not completely up
                    status->link = false;
                } else if (!data->aneg_after_link_up) {// poll the status for 1 iteration assuming the polling interval is 1 second apart.
                    T_I(MEPA_TRACE_GRP_GEN, "Aneg restarted on port %d", data->port_no);
                    (void)WRM(dev, LAN8814_BASIC_CONTROL, LAN8814_F_BASIC_CTRL_RESTART_ANEG, LAN8814_F_BASIC_CTRL_RESTART_ANEG);
                    data->aneg_after_link_up = TRUE;
                    data->loop_cnt = 0;
                    status->link = false;
                } else if (data->aneg_after_link_up) {// After auto-negotiation restarted, set the link status as up.
                    status->speed = MEPA_SPEED_1G;
                    status->fdx = true;
                    data->crc_workaround = FALSE;
                    data->aneg_after_link_up = FALSE;
                } else {
                    T_I(MEPA_TRACE_GRP_GEN, "no link");
                    status->link = false;
                }
                // Work-around for CRC errors end.
            } else {
                status->speed = MEPA_SPEED_1G;
                status->fdx = true;
            }
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
            // no matching link partner advertisement
        }

        // Get flow control status
        lp_sym_pause = ((val & LAN8814_F_ANEG_LP_BASE_SYM_PAUSE) != 0U) ? 1U : 0U;
        lp_asym_pause = ((val & LAN8814_F_ANEG_LP_BASE_ASYM_PAUSE) != 0U) ? 1U : 0U;
        status->aneg.obey_pause = data->conf.flow_control && ((lp_sym_pause != 0U) || (lp_asym_pause != 0U));
        status->aneg.generate_pause = data->conf.flow_control && (lp_sym_pause != 0U);
    } else {
        uint8_t bit0;
        uint8_t bit1;
        // Forced speed
        (void)RD(dev, LAN8814_BASIC_CONTROL, &val2);
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

        // MEPA 503 workaround starts
        (void)RD(dev, LAN8814_DIGITAL_AX_AN_STATUS, &val2);
        (void)RD(dev, LAN8814_CONTROL, &val);
        if (status->speed == MEPA_SPEED_100M && (((val2 & LAN8814_F_LINK_DET) != 0U) && !status->link)) {
            if ((data->loop_cnt++ > (2U * data->rep_cnt))) {
                val |= LAN8814_F_CONTROL_SOFT_RESET;
                (void)WRM(dev, LAN8814_CONTROL, val, LAN8814_F_CONTROL_SOFT_RESET);
                T_I(MEPA_TRACE_GRP_GEN, "DSP soft reset for link up on Port %d", data->port_no);
                data->loop_cnt = 0;
            }
        }
        // MEPA 503 workaround ends here
    }

end:
    if (lan8814_is_lan8814(dev)) {
        if (status->link != data->link_status) {
            if (status->link) { // link up
                if ( data->conf.speed == MEPA_SPEED_AUTO || data->conf.speed == MEPA_SPEED_1G) {
                    if (data->conf.mac_if_aneg_ena) {
                        // copy the capabilities on host side
                        lan8814_qsgmii_tx_abilities(dev, status->speed, status->fdx);
                    }
                }
                if ((status->speed == MEPA_SPEED_10M || status->speed == MEPA_SPEED_100M) &&
                    (status->fdx == FALSE)) {
                    (void)lan8814_workaround_half_duplex(dev);
                }
            } else {// link down event.
                (void)lan8814_workaround_fifo_reset(dev);
                if (data->dsh_complete) {
                    val |= (LAN8814_F_ANEG_MSTR_SLV_CTRL_CFG_VAL | LAN8814_F_ANEG_MSTR_SLV_CTRL_1000_T_FULL_DUP);
                    T_I(MEPA_TRACE_GRP_GEN, "link Down Readvertising 1G Speed Changed on port: %d", data->port_no);
                    (void)WRM(dev, LAN8814_ANEG_MSTR_SLV_CTRL, val, LAN8814_F_ANEG_MSTR_SLV_CTRL_1000_T_FULL_DUP | LAN8814_F_ANEG_MSTR_SLV_CTRL_CFG_VAL);
                    data->dsh_complete = false;
                    data->loop_cnt = 0;
                    data->aneg_after_link_up = FALSE;
                }
            }
            data->loop_cnt = 0;
            data->aneg_flag = false;
            T_I(MEPA_TRACE_GRP_GEN, "After restart workaround on port->no:%d and data->dsh_loop_cnt:%d", data->port_no, data->loop_cnt);
        }
    }
    data->link_status = status->link;
    data->speed_status = status->speed;
    data->fdx_status   = status->fdx;
    T_D(MEPA_TRACE_GRP_GEN, "port %d status link %d, speed %d, fdx %d", data->port_no, status->link, status->speed, status->fdx);
    return MEPA_RC_OK;
}

static mepa_rc lan8814_poll(mepa_device_t *dev, mepa_status_t *status)
{
    mepa_rc rc;

    MEPA_ENTER(dev);
    rc = lan8814_poll_priv(dev, status);
    MEPA_EXIT(dev);

    return rc;
}

static mepa_rc lan8814_conf_set(mepa_device_t *dev, const mepa_conf_t *config)
{
    mepa_rc rc;

    MEPA_ENTER(dev);
    rc = lan8814_conf_set_(dev, config);
    MEPA_EXIT(dev);

    return rc;
}

static mepa_rc lan8814_conf_get(mepa_device_t *dev, mepa_conf_t *const config)
{
    lan8814_data_t *data = (lan8814_data_t *)dev->data;

    MEPA_ENTER(dev);
    *config = data->conf;
    MEPA_EXIT(dev);
    T_D(MEPA_TRACE_GRP_GEN, "returning phy config on port %d", data->port_no);
    return MEPA_RC_OK;
}

static mepa_rc lan8814_if_set(mepa_device_t *dev,
                              mepa_port_interface_t mac_if)
{
    if (mac_if != MESA_PORT_INTERFACE_QSGMII) {
        return MEPA_RC_ERROR;
    }

    return MEPA_RC_OK;
}

static mepa_rc lan8814_if_get(mepa_device_t *dev, mepa_port_speed_t speed,
                              mepa_port_interface_t *mac_if)
{

    *mac_if = MESA_PORT_INTERFACE_QSGMII;
    return MEPA_RC_OK;
}

static mepa_device_t *lan8814_probe(mepa_driver_t *drv,
                                    const mepa_callout_t    MEPA_SHARED_PTR *callout,
                                    struct mepa_callout_ctx MEPA_SHARED_PTR *callout_ctx,
                                    struct mepa_board_conf              *board_conf)
{
    mepa_device_t *dev;
    lan8814_data_t *data;

    // MEPA-692: A remapping of driver workaround for correctly identifying the LAN8814 SKU's
    mepa_drivers_t lan8814_drv = mepa_lan8814_driver_init();
    uint16_t sku = 0;
    if ((callout->miim_read != NULL) && (callout->miim_write != NULL)) {
        // Write Reg 22 as 4 to access EP4 registers space
        (void)callout->miim_write(callout_ctx, LAN8814_EXT_PAGE_ACCESS_CTRL, 4);
        // Write Reg 23 as 11 to read reg 11 in EP4
        (void)callout->miim_write(callout_ctx, LAN8814_EXT_PAGE_ACCESS_ADDR_DATA, 11);
        // Initiate Data read operation
        (void)callout->miim_write(callout_ctx, LAN8814_EXT_PAGE_ACCESS_CTRL, 0x4004);
        // Read Reg 23 for SKU value read in EP 4.11
        (void)callout->miim_read(callout_ctx, LAN8814_EXT_PAGE_ACCESS_ADDR_DATA, &sku);
    } else {
        T_E(MEPA_TRACE_GRP_GEN, "callout read/write is not present for port:%d", board_conf->numeric_handle);
        return NULL;
    }

    if (sku == LAN8804_SKU ||
        sku == LAN8808_SKU) {
        // In case the SKU is LAN8808_SKU will assign the driver with id
        // LAN8804_SKU. It is fine to do that because the driver will be
        // identical in this case.
        drv = &lan8814_drv.phy_drv[1];
    }
    if (sku == LAN8832_SKU) {
        drv = &lan8814_drv.phy_drv[4];
    }
    // MEPA-692: Workaround ends

    dev = mepa_create_int(drv, callout, callout_ctx, board_conf, (int)sizeof(lan8814_data_t));
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "Dev creation failed for Port %d", board_conf->numeric_handle);
        return NULL;
    }

    data = dev->data;
    data->port_no = board_conf->numeric_handle;
    data->events = 0;

    (void)lan8814_get_device_info(dev);
    data->dev.sku = sku;

#ifdef REG_DBG
    (void)lan8814_reg_dump(dev);
#endif

    T_I(MEPA_TRACE_GRP_GEN, "lan8814 driver probed for port %d", data->port_no);
    return dev;
}

static mepa_rc lan8814_aneg_status_get(mepa_device_t *dev, mepa_aneg_status_t *status)
{
    uint16_t val;
    lan8814_data_t *data = (lan8814_data_t *)dev->data;

    MEPA_ENTER(dev);
    (void)RD(dev, LAN8814_ANEG_MSTR_SLV_STATUS, &val);
    status->master_cfg_fault = ((val & LAN8814_F_ANEG_MSTR_SLV_STATUS_CFG_FAULT) != 0U) ? TRUE : FALSE;
    status->master = ((val & LAN8814_F_ANEG_MSTR_SLV_STATUS_CFG_RES) != 0U) ? TRUE : FALSE;
    MEPA_EXIT(dev);
    T_I(MEPA_TRACE_GRP_GEN, "aneg status get mstr %d port %d", status->master, data->port_no);
    return MEPA_RC_OK;
}

// read direct registers for debugging
static mepa_rc lan8814_direct_reg_read(mepa_device_t *dev, uint32_t address, uint16_t *const value)
{
    mepa_rc rc;
    uint16_t addr = (uint16_t)(address & 0x1fU);

    MEPA_ENTER(dev);
    rc = lan8814_direct_reg_rd(dev, addr, value);
    MEPA_EXIT(dev);
    return rc;
}

// write direct registers. Used for debugging.
static mepa_rc lan8814_direct_reg_write(mepa_device_t *dev, uint32_t address, uint16_t value)
{
    mepa_rc rc;
    uint16_t addr = (uint16_t)(address & 0x1fU);

    MEPA_ENTER(dev);
    rc = lan8814_direct_reg_wr(dev, addr, value, 0xFFFFU);
    MEPA_EXIT(dev);
    return rc;
}

// read extended page/mmd register for debugging
static mepa_rc lan8814_ext_mmd_reg_read(mepa_device_t *dev, uint32_t address, uint16_t *const value)
{
    mepa_rc rc;
    uint16_t page_mmd = (uint16_t)((address >> 16) & 0xffffU);
    uint16_t addr = (uint16_t)(address & 0xffffU);
    uint16_t mmd = (uint16_t)(page_mmd >> 11);

    MEPA_ENTER(dev);
    if (mmd != 0U) {
        rc = lan8814_mmd_reg_rd(dev, mmd, addr, value);
    } else {
        rc = lan8814_ext_reg_rd(dev, page_mmd, addr, value);
    }
    MEPA_EXIT(dev);
    return rc;
}

// write extended page/mmd register. Used for debugging.
static mepa_rc lan8814_ext_mmd_reg_write(mepa_device_t *dev, uint32_t address, uint16_t value)
{
    mepa_rc rc;
    uint16_t page_mmd = (uint16_t)((address >> 16) & 0xffffU);
    uint16_t addr = (uint16_t)(address & 0xffffU);
    uint16_t mmd = (uint16_t)(page_mmd >> 11);

    MEPA_ENTER(dev);
    if (mmd != 0U) {
        rc = lan8814_mmd_reg_wr(dev, mmd, addr, value, 0xFFFFU);
    } else {
        rc = lan8814_ext_reg_wr(dev, page_mmd, addr, value, 0xFFFFU);
    }
    MEPA_EXIT(dev);
    return rc;
}

static mepa_rc lan8814_event_enable_set(mepa_device_t *dev, mepa_event_t event, mepa_bool_t enable)
{
    mepa_rc rc;

    MEPA_ENTER(dev);
    rc = lan8814_event_enable_set_(dev, event, enable);
    MEPA_EXIT(dev);

    return rc;
}

// Get current enabled events
static mepa_rc lan8814_event_enable_get(mepa_device_t *dev, mepa_event_t *const event)
{
    mepa_rc rc = MEPA_RC_OK;
    lan8814_data_t *data = (lan8814_data_t *)dev->data;
    MEPA_ENTER(dev);
    *event = data->events;
    MEPA_EXIT(dev);
    return rc;
}

// Poll the status of currently enabled events
static mepa_rc lan8814_event_status_poll(mepa_device_t *dev, mepa_event_t *const status)
{
    uint16_t val;
    mepa_rc rc = MEPA_RC_OK;
    lan8814_data_t *data = (lan8814_data_t *)dev->data;
    *status = 0;
    MEPA_ENTER(dev);
    rc = RD(dev, LAN8814_GPHY_INTR_STATUS, &val);
    if ((val & LAN8814_F_GPHY_INTR_ENA_LINK_DOWN) != 0U) {
        *status |= data->events & MEPA_LINK_LOS;
    }
    if ((val & LAN8814_F_GPHY_INTR_ENA_FLF_INTR) != 0U) {
        *status |= data->events & MEPA_FAST_LINK_FAIL;
    }
    MEPA_EXIT(dev);
    T_I(MEPA_TRACE_GRP_GEN, " port %d events polled 0x%x val %x\n", data->port_no, *status, val);
    return rc;
}

// Set gpio mode to input, output or alternate function
static mepa_rc lan8814_gpio_mode_set(mepa_device_t *dev, const mepa_gpio_conf_t *gpio_conf)
{
    mepa_rc rc = MEPA_RC_OK;

    if ((lan8814_is_lan8814(dev) && gpio_conf->gpio_no > 23U) ||
        (lan8814_is_lan966x(dev) && gpio_conf->gpio_no > 23U) ||
        (lan8814_is_lan8842(dev) && gpio_conf->gpio_no > 15U)) {
        T_W(MEPA_TRACE_GRP_GEN, "Not valid gpio on %s phy", lan8814_get_name(dev));
        return MEPA_RC_NOT_IMPLEMENTED;
    }

    MEPA_ENTER(dev);
    rc = lan8814_gpio_mode_private(dev, gpio_conf);
    MEPA_EXIT(dev);
    return rc;
}

static mepa_rc lan8814_gpio_out_set(mepa_device_t *dev, uint8_t gpio_no, mepa_bool_t value)
{
    uint16_t val = 0;

    if ((lan8814_is_lan8814(dev) && gpio_no > 23U) ||
        (lan8814_is_lan966x(dev) && gpio_no > 23U) ||
        (lan8814_is_lan8842(dev) && gpio_no > 15U)) {
        T_W(MEPA_TRACE_GRP_GEN, "Not valid gpio on %s phy", lan8814_get_name(dev));
        return MEPA_RC_NOT_IMPLEMENTED;
    }

    MEPA_ENTER(dev);
    if (gpio_no < 16U) {
        val = (uint16_t)((uint16_t)1U << gpio_no);
        (void)EP_WRM(dev, LAN8814_GPIO_DATA2, value ? val : 0U, val);
    } else if (gpio_no < 24U) {
        val = (uint16_t)((uint16_t)1U << (gpio_no - 16U));
        (void)EP_WRM(dev, LAN8814_GPIO_DATA1, value ? val : 0U, val);
    } else {
        // Not supported. Illegal for LAN8814.
        T_W(MEPA_TRACE_GRP_GEN, "Not valid gpio on 8814 phy");
    }

    MEPA_EXIT(dev);
    return MEPA_RC_OK;
}

static mepa_rc lan8814_gpio_in_get(mepa_device_t *dev, uint8_t gpio_no, mepa_bool_t *const value)
{
    uint16_t val = 0;

    if ((lan8814_is_lan8814(dev) && gpio_no > 23U) ||
        (lan8814_is_lan966x(dev) && gpio_no > 23U) ||
        (lan8814_is_lan8842(dev) && gpio_no > 15U)) {
        T_W(MEPA_TRACE_GRP_GEN, "Not valid gpio on %s phy", lan8814_get_name(dev));
        return MEPA_RC_NOT_IMPLEMENTED;
    }

    MEPA_ENTER(dev);
    if (gpio_no < 16U) {
        (void)EP_RD(dev, LAN8814_GPIO_DATA2, &val);
        *value = (((val >> gpio_no) & 0x1U) != 0U) ? TRUE : FALSE;
    } else if (gpio_no < 24U) {
        (void)EP_RD(dev, LAN8814_GPIO_DATA1, &val);
        *value = (((val >> (gpio_no - 16U)) & 0x1U) != 0U) ? TRUE : FALSE;
    } else {
        // gpio out of range; nothing to do
    }

    MEPA_EXIT(dev);
    return MEPA_RC_OK;
}

// Link the base port
static mepa_rc lan8814_link_base_port(mepa_device_t *dev, mepa_device_t *base_dev, uint8_t packet_idx)
{
    lan8814_data_t *data = (lan8814_data_t *)dev->data;
    lan8814_data_t *base_data = (lan8814_data_t *)base_dev->data;

    MEPA_ENTER(dev);
    data->base_dev = base_dev;
    data->packet_idx = packet_idx;
    T_I(MEPA_TRACE_GRP_GEN, "Linking port %d with base-port %d", data->port_no, base_data->port_no);
    MEPA_EXIT(dev);
    return MEPA_RC_OK;
}

static uint32_t lan8814_capability_priv(mepa_device_t *dev, uint32_t capability)
{
    uint32_t c;

    switch (capability) {
    case (uint32_t)MEPA_CAP_TS_NONE:
        c = lan8814_has_ptp(dev) ? 0U : 1U;
        break;
    case (uint32_t)MEPA_CAP_TS_GEN_3:
        c = lan8814_has_ptp(dev) ? 1U : 0U;
        break;
    case (uint32_t)MEPA_CAP_SPEED_1G:
        c = 1U;
        break;
    case (uint32_t)MEPA_CAP_LOOPBACK:
        c = (uint32_t)MEPA_LOOPBACK_FAR_END | (uint32_t)MEPA_LOOPBACK_NEAR_END | (uint32_t)MEPA_LOOPBACK_CONNECTOR_END;
        if (lan8814_is_lan8842(dev) || lan8814_is_lan8814(dev)) {
            c |= (uint32_t)MEPA_LOOPBACK_QSGMII_PCS_TBI_ENA | (uint32_t)MEPA_LOOPBACK_QSGMII_PCS_GMII_ENA | (uint32_t)MEPA_LOOPBACK_QSGMII_SERDES_ENA;
        }
        break;
    default:
        c = 0U;
        break;
    }

    return c;
}

static uint32_t lan8814_capability(mepa_device_t *dev, uint32_t capability)
{
    uint32_t c;

    MEPA_ENTER(dev);
    c = lan8814_capability_priv(dev, capability);
    MEPA_EXIT(dev);
    return c;
}

static mepa_rc lan8814_info_get(mepa_device_t *dev, mepa_phy_info_t *const phy_info)
{
    lan8814_data_t *data = (lan8814_data_t *)(dev->data);
    lan8814_data_t *base_data = (data->base_dev != NULL) ? ((lan8814_data_t *)(data->base_dev->data)) : NULL;
    uint32_t cap_value = 0U;

    phy_info->manufactor_name = "Microchip";

    // Read SKU ID and assign Part no
    if (dev->drv->id == LAN8814_DEF_DRV_ID || dev->drv->id == LAN8814_INT_PHY_DRV_ID) {
        // For LAN8814 inside lan9668 the driver id is different and the SKU No is 0 upon read.
        // Assigning part No based on Driver
        phy_info->part_number = 8814;
        phy_info->model_name = "LAN8814";
    } else if (dev->drv->id == LAN8804_SKU) {
        // The driver with ID LAN8804_SKU covers both the LAN8804 and LAN8808
        // devices, so in this case we need to check which SKU is it to set the
        // correct part number and model
        if (data->dev.sku == LAN8804_SKU) {
            phy_info->part_number = 8804;
            phy_info->model_name = "LAN8804";
        } else {
            phy_info->part_number = 8808;
            phy_info->model_name = "LAN8808";
        }
    } else {
        // unknown SKU; leave part_number/model_name unset
    }
    phy_info->revision = data->dev.rev;

    if (lan8814_capability_priv(dev, (uint32_t)MEPA_CAP_TS_GEN_3) != 0U) {
        cap_value |= (uint32_t)MEPA_CAP_TS_MASK_GEN_3;
    }
    if (lan8814_capability_priv(dev, (uint32_t)MEPA_CAP_TS_NONE) != 0U) {
        cap_value |= (uint32_t)MEPA_CAP_TS_MASK_NONE;
    }
    if (lan8814_capability_priv(dev, (uint32_t)MEPA_CAP_SPEED_1G) != 0U) {
        cap_value |= (uint32_t)MEPA_CAP_SPEED_MASK_1G;
    }
    phy_info->cap = (mepa_phy_cap_t)cap_value;

    if (dev->drv->id == LAN8804_SKU) {
        phy_info->ts_base_port = 0;
        phy_info->ts_base = NULL;
    } else {
        phy_info->ts_base_port = (base_data != NULL) ? base_data->port_no : 0U;
        phy_info->ts_base = data->base_dev;
    }

    return MEPA_RC_OK;
}

#if !defined MEPA_LAN8814_LIGHT
static mepa_rc lan8814_eee_mode_conf_set(mepa_device_t *dev, const mepa_phy_eee_conf_t conf)
{
    mepa_rc rc;

    MEPA_ENTER(dev);
    rc = lan8814_eee_mode_conf_set_(dev, conf);
    MEPA_EXIT(dev);

    return rc;
}
#endif

#if !defined MEPA_LAN8814_LIGHT
static mepa_rc lan8814_eee_mode_conf_get(mepa_device_t *dev, mepa_phy_eee_conf_t *const config)
{
    lan8814_data_t *data = (lan8814_data_t *)(dev->data);

    MEPA_ENTER(dev);
    *config = data->eee_conf;
    MEPA_EXIT(dev);

    T_I(MEPA_TRACE_GRP_GEN, "returning EEE phy config on port %d", data->port_no);
    return MEPA_RC_OK;
}
#endif

#if !defined MEPA_LAN8814_LIGHT
static mepa_rc lan8814_eee_status_get(mepa_device_t *dev, u8 *const advertisement, BOOL *const rx_in_power_save_state, BOOL *const tx_in_power_save_state)
{
    u16 reg_value = 0;
    *rx_in_power_save_state = FALSE;
    *tx_in_power_save_state = FALSE;

    MMD_RD(dev, LAN8814_LINK_PARTNER_EEE_ABILITY, &reg_value); // Get the Link Partnr Advertisement
    *advertisement = reg_value >> 1; // Bit 0 is reserved.

    MMD_RD(dev, LAN8814_EEE_PCS_STATUS, &reg_value);

    //Bit 8 is Rx LPI Indication. See Datasheet
    if (reg_value & 0x0100) {
        *rx_in_power_save_state = TRUE;
    }
    //Bit 9 is tx LPI Indication. See Datsheet.
    if (reg_value & 0x0200) {
        *tx_in_power_save_state = TRUE;
    }

    return MEPA_RC_OK;
}
#endif

// LAN8814 phy dignostics is calculated only when there is no remote link partner for the port.
// For mode values {0,1} corresponding to {VTSS_PHY_MODE_ANEG, VTSS_PHY_MODE_FORCED}, diagnostics is calculated.
// For power down mode(2), diagnostics is not calculated.
static mepa_rc lan8814_cab_diag_start(mepa_device_t *dev, int32_t mode)
{
    mepa_rc rc;

    MEPA_ENTER(dev);
    rc = lan8814_cab_diag_start_(dev, mode, FALSE);
    MEPA_EXIT(dev);
    return rc;
}

static mepa_rc lan8814_cab_diag_start_async(mepa_device_t *dev, int32_t mode)
{
    mepa_rc rc;

    MEPA_ENTER(dev);
    rc = lan8814_cab_diag_start_(dev, mode, TRUE);
    MEPA_EXIT(dev);
    return rc;
}

static mepa_rc lan8814_cab_diag_get(mepa_device_t *dev, mepa_cable_diag_result_t *res)
{
    lan8814_data_t *data = (lan8814_data_t *)dev->data;
    MEPA_ENTER(dev);
    *res = data->cable_diag;
    MEPA_EXIT(dev);
    return MEPA_RC_OK;
}

// Set loopback modes in phy
static mepa_rc lan8814_loopback_set(mepa_device_t *dev, const mepa_loopback_t *loopback)
{
    lan8814_data_t *data = (lan8814_data_t *)dev->data;

    if ((loopback->mac_serdes_input_ena == TRUE) || (loopback->mac_serdes_facility_ena == TRUE) ||
        (loopback->mac_serdes_equip_ena == TRUE) || (loopback->media_serdes_input_ena == TRUE) ||
        (loopback->media_serdes_facility_ena == TRUE) || (loopback->media_serdes_equip_ena == TRUE)) {
        // Not supported on LAN8814
        return MEPA_RC_NOT_IMPLEMENTED;
    }

    if (lan8814_is_lan966x(dev) &&
        (loopback->qsgmii_pcs_tbi_ena == TRUE ||
         loopback->qsgmii_pcs_gmii_ena == TRUE ||
         loopback->qsgmii_serdes_ena == TRUE)) {
        // These loopbacks are not supported on the internal PHYs of lan966x
        return MEPA_RC_ERROR;
    }

    MEPA_ENTER(dev);
    // Far end loopback
    if (loopback->far_end_ena == TRUE) {
        (void)WRM(dev, LAN8814_PCS_LOOP_POLARITY_CTRL, LAN8814_F_PCS_LOOP_CTRL_PORT_LOOP,
            LAN8814_F_PCS_LOOP_CTRL_PORT_LOOP);
    } else if (data->loopback.far_end_ena == TRUE) {
        (void)WRM(dev, LAN8814_PCS_LOOP_POLARITY_CTRL, 0, LAN8814_F_PCS_LOOP_CTRL_PORT_LOOP);
    } else {
        // far-end loopback unchanged
    }
    if (loopback->near_end_ena == TRUE) {
        (void)WRM(dev, LAN8814_BASIC_CONTROL, LAN8814_F_BASIC_CTRL_LOOPBACK, LAN8814_F_BASIC_CTRL_LOOPBACK);
        if (data->conf.speed == MEPA_SPEED_AUTO || data->conf.speed == MEPA_SPEED_1G) {
            // Set 1000mbps speed for loopback when there is auto-negotiation mode. While removing loopback, restore the original mode.
            // Disable auto-negotiation
            (void)WRM(dev, LAN8814_BASIC_CONTROL, 0, LAN8814_F_BASIC_CTRL_ANEG_ENA);
            // Set 1000mbps speed
            (void)WRM(dev, LAN8814_BASIC_CONTROL, LAN8814_F_BASIC_CTRL_SPEED_SEL_BIT_1, LAN8814_F_BASIC_CTRL_SPEED_SEL_BIT_1 | LAN8814_F_BASIC_CTRL_SPEED_SEL_BIT_0);
        } else {
            // forced speed: no extra action
        }
    } else if (data->loopback.near_end_ena == TRUE) {
        (void)WRM(dev, LAN8814_BASIC_CONTROL, 0, LAN8814_F_BASIC_CTRL_LOOPBACK);
        if (data->conf.speed == MEPA_SPEED_AUTO || data->conf.speed == MEPA_SPEED_1G) {
            // Remove 1000mbps config applied while setting loopback.
            (void)WRM(dev, LAN8814_ANEG_MSTR_SLV_CTRL, 0, LAN8814_F_ANEG_MSTR_SLV_CTRL_CFG_ENA |
                LAN8814_F_ANEG_MSTR_SLV_CTRL_CFG_VAL);
            // Enable aneg
            (void)WRM(dev, LAN8814_BASIC_CONTROL, LAN8814_F_BASIC_CTRL_ANEG_ENA | LAN8814_F_BASIC_CTRL_RESTART_ANEG,
                LAN8814_F_BASIC_CTRL_ANEG_ENA | LAN8814_F_BASIC_CTRL_RESTART_ANEG);
        } else {
            // forced speed: no extra action
        }
        (void)EP_WR(dev, LAN8814_POWER_MGMT_MODE_5, 0x6677);
        (void)EP_WR(dev, LAN8814_POWER_MGMT_MODE_6, 0x6677);
        (void)EP_WR(dev, LAN8814_POWER_MGMT_MODE_8, 0x4377);
        (void)EP_WR(dev, LAN8814_POWER_MGMT_MODE_11, 0x4377);
    } else {
        // near-end loopback unchanged
    }
    if (loopback->connector_ena == TRUE) {
        // Disable auto-negotiation
        if (data->conf.speed == MEPA_SPEED_AUTO || data->conf.speed == MEPA_SPEED_1G) {
            // Set 1000mbps speed for loopback when there is auto-negotiation mode. While removing loopback, restore the original mode.
            (void)WRM(dev, LAN8814_BASIC_CONTROL, 0, LAN8814_F_BASIC_CTRL_ANEG_ENA);
            (void)WRM(dev, LAN8814_BASIC_CONTROL, LAN8814_F_BASIC_CTRL_SPEED_SEL_BIT_1, LAN8814_F_BASIC_CTRL_SPEED_SEL_BIT_1 | LAN8814_F_BASIC_CTRL_SPEED_SEL_BIT_0);
            // Set Master slave configuration to master
            (void)WRM(dev, LAN8814_ANEG_MSTR_SLV_CTRL, (LAN8814_F_ANEG_MSTR_SLV_CTRL_CFG_VAL | LAN8814_F_ANEG_MSTR_SLV_CTRL_CFG_ENA), (LAN8814_F_ANEG_MSTR_SLV_CTRL_CFG_VAL | LAN8814_F_ANEG_MSTR_SLV_CTRL_CFG_ENA));
        } else if (data->conf.speed == MEPA_SPEED_100M) {
            // Set Speed to 100M in Basic Control Register
            (void)WRM(dev, LAN8814_BASIC_CONTROL, LAN8814_F_BASIC_CTRL_SPEED_SEL_BIT_0, LAN8814_F_BASIC_CTRL_SPEED_SEL_BIT_1 | LAN8814_F_BASIC_CTRL_SPEED_SEL_BIT_0);
        } else if (data->conf.speed == MEPA_SPEED_10M) {
            // Set Speed to 10M in Basic Control Register
            (void)WRM(dev, LAN8814_BASIC_CONTROL, 0, LAN8814_F_BASIC_CTRL_SPEED_SEL_BIT_1 | LAN8814_F_BASIC_CTRL_SPEED_SEL_BIT_0);
        } else {
            // unsupported speed: no action
        }
        // Set Connector Loopback
        (void)WR(dev, LAN8814_RESV_CON_LOOP, 0xfc08);
    } else if (data->loopback.connector_ena == TRUE) { //Revert the Settings when connector loopback is disabled
        if (data->conf.speed == MEPA_SPEED_AUTO || data->conf.speed == MEPA_SPEED_1G) {
            // Remove 1000mbps config applied while setting loopback.
            (void)WRM(dev, LAN8814_ANEG_MSTR_SLV_CTRL, 0, LAN8814_F_ANEG_MSTR_SLV_CTRL_CFG_ENA |
                LAN8814_F_ANEG_MSTR_SLV_CTRL_CFG_VAL);
            // Enable aneg
            (void)WRM(dev, LAN8814_BASIC_CONTROL, LAN8814_F_BASIC_CTRL_ANEG_ENA | LAN8814_F_BASIC_CTRL_RESTART_ANEG,
                LAN8814_F_BASIC_CTRL_ANEG_ENA | LAN8814_F_BASIC_CTRL_RESTART_ANEG);
        } else {
            // forced speed: no extra action
        }
        (void)WR(dev, LAN8814_RESV_CON_LOOP, 0xfc00);
    } else {
        // connector loopback unchanged
    }
    if (loopback->qsgmii_pcs_tbi_ena == TRUE) { // Enable tbi loopback
        (void)EP_WRM(dev, LAN8814_QSGMII_PCS1G_DEBUG, LAN8814_F_QSGMII_PCS1G_DBG_TBI_HOST_LOOPBACK,
               LAN8814_F_QSGMII_PCS1G_DBG_TBI_HOST_LOOPBACK);
    } else if (data->loopback.qsgmii_pcs_tbi_ena == TRUE) { // Disable tbi loopback
        (void)EP_WRM(dev, LAN8814_QSGMII_PCS1G_DEBUG, 0, LAN8814_F_QSGMII_PCS1G_DBG_TBI_HOST_LOOPBACK);
    } else {
        // tbi loopback unchanged
    }
    if (loopback->qsgmii_pcs_gmii_ena == TRUE) { // Enable gmii loopback
        (void)EP_WRM(dev, LAN8814_QSGMII_PCS1G_DEBUG, LAN8814_F_QSGMII_PCS1G_DBG_GMII_LOOPBACK,
               LAN8814_F_QSGMII_PCS1G_DBG_GMII_LOOPBACK);
    } else if (data->loopback.qsgmii_pcs_gmii_ena == TRUE) { // Disable gmii loopback
        (void)EP_WRM(dev, LAN8814_QSGMII_PCS1G_DEBUG, 0, LAN8814_F_QSGMII_PCS1G_DBG_GMII_LOOPBACK);
    } else {
        // gmii loopback unchanged
    }
    if (loopback->qsgmii_serdes_ena == TRUE) { // Enable qsgmii serdes loopback.
        // Serdes configuration would affect all the 4 ports.
        (void)EP_WRM(dev, LAN8814_QSGMII_SERDES_TX_GENERAL, 0,
               LAN8814_QSGMII_SERDES_TX_GENERAL_TX_INV);
        (void)EP_WRM(dev, LAN8814_QSGMII_SERDES_MISC_CTRL, LAN8814_F_QSGMII_SERDES_MISC_CTRL_LB_MODE,
               LAN8814_F_QSGMII_SERDES_MISC_CTRL_LB_MODE);
        (void)EP_WRM(dev, LAN8814_QSGMII_SOFT_RESET, LAN8814_QSGMII_SOFT_RESET_BIT,
                     LAN8814_QSGMII_SOFT_RESET_BIT);
    } else if (data->loopback.qsgmii_serdes_ena == TRUE) {
        (void)EP_WRM(dev, LAN8814_QSGMII_SERDES_TX_GENERAL, LAN8814_QSGMII_SERDES_TX_GENERAL_TX_INV,
               LAN8814_QSGMII_SERDES_TX_GENERAL_TX_INV);
        (void)EP_WRM(dev, LAN8814_QSGMII_SERDES_MISC_CTRL, 0, LAN8814_F_QSGMII_SERDES_MISC_CTRL_LB_MODE);
        (void)EP_WRM(dev, LAN8814_QSGMII_SOFT_RESET, LAN8814_QSGMII_SOFT_RESET_BIT,
                     LAN8814_QSGMII_SOFT_RESET_BIT);
    } else {
        // serdes loopback unchanged
    }
    data->loopback = *loopback;
    MEPA_EXIT(dev);
    T_I(MEPA_TRACE_GRP_GEN, "loopback enabled\n");
    return MEPA_RC_OK;
}

static mepa_rc lan8814_loopback_get(struct mepa_device *dev, mepa_loopback_t *const loopback)
{
    lan8814_data_t *data = (lan8814_data_t *) dev->data;

    MEPA_ENTER(dev);
    *loopback = data->loopback;
    MEPA_EXIT(dev);
    return MEPA_RC_OK;
}

#if !defined(MEPA_LAN8814_LIGHT)
static mepa_rc lan8814_recovered_clk_set(mepa_device_t *dev, const mepa_synce_clock_conf_t *conf)
{
    lan8814_data_t *data = (lan8814_data_t *) dev->data;
    mepa_gpio_conf_t gpio_conf;
    uint16_t divider = 1;
    uint16_t clkout_src = 7;
    mepa_rc rc = MEPA_RC_OK;

    MEPA_ENTER(dev);
    // Enable recovered clock outputs in gpio
    gpio_conf.mode = (conf->dst == MEPA_SYNCE_CLOCK_DST_1) ? MEPA_GPIO_MODE_RCVRD_CLK_OUT1 : MEPA_GPIO_MODE_RCVRD_CLK_OUT2;
    gpio_conf.gpio_no = (conf->dst == MEPA_SYNCE_CLOCK_DST_1) ? 9 : 10;
    rc = lan8814_gpio_mode_private(dev, &gpio_conf);

    switch (conf->freq) {
    case MEPA_FREQ_25M:
        divider = 5; // 125Mhz/25Mhz = 5
        break;
    case MEPA_FREQ_31_25M:
        divider = 4; // 125Mhz/31.25Mhz = 4
        break;
    case MEPA_FREQ_125M:
    default:
        divider = 1; // 125Mhz/125Mhz = 1
        break;
    }
    switch (conf->src) {
    case MEPA_SYNCE_CLOCK_SRC_DISABLED:
        clkout_src = 7; // Forces Recovered Clock Output to 0
        break;
    case MEPA_SYNCE_CLOCK_SRC_COPPER_MEDIA:
        clkout_src = data->packet_idx % 4;
        break;
    case MEPA_SYNCE_CLOCK_SRC_CLOCK_IN_1:
        clkout_src = 0b100; // Recovered Clock Input 1
        break;
    case MEPA_SYNCE_CLOCK_SRC_CLOCK_IN_2:
        clkout_src = 0b101; // Recovered Clock Input 2
        break;
    case MEPA_SYNCE_CLOCK_SRC_SERDES_MEDIA:
    default:
        T_W(MEPA_TRACE_GRP_GEN, "Invalid valid clock source, port-no : %d\n", dev->numeric_handle);
        break;
    }
    T_I(MEPA_TRACE_GRP_GEN, "port_ena %d divider %d\n", clkout_src, divider);
    if (conf->dst == MEPA_SYNCE_CLOCK_DST_1) {
        EP_WR(dev, LAN8814_RCVRD_CLK_OUT_SEL_1, clkout_src);
        EP_WR(dev, LAN8814_RCVRD_CLK_OUT_DIV_1, divider);
    } else if (conf->dst == MEPA_SYNCE_CLOCK_DST_2) {
        EP_WR(dev, LAN8814_RCVRD_CLK_OUT_SEL_2, clkout_src);
        EP_WR(dev, LAN8814_RCVRD_CLK_OUT_DIV_2, divider);
    }

    MEPA_EXIT(dev);

    data->synce_conf = *conf;
    return rc;
}
#endif

#if !defined(MEPA_LAN8814_LIGHT)
// Set Isolate mode
static mepa_rc lan8814_isolate_mode_conf(mepa_device_t *dev, const mepa_bool_t iso_en)
{
    MEPA_ENTER(dev);
    if (iso_en == TRUE) {
        WRM(dev, LAN8814_BASIC_CONTROL, LAN8814_F_BASIC_CTRL_ISO_MODE_EN, LAN8814_F_BASIC_CTRL_ISO_MODE_EN);
    } else {
        WRM(dev, LAN8814_BASIC_CONTROL, 0, LAN8814_F_BASIC_CTRL_ISO_MODE_EN);
    }

    MEPA_EXIT(dev);
    return MEPA_RC_OK;
}
#endif

#if !defined(MEPA_LAN8814_LIGHT)
static const char *lan8814_mac_if_to_str(mesa_port_interface_t mac_if)
{
    switch (mac_if) {
    case MESA_PORT_INTERFACE_QSGMII:
        return "QSGMII";
    case MESA_PORT_INTERFACE_SGMII:
        return "SGMII";
    case MESA_PORT_INTERFACE_GMII:
        return "GMII";
    default:
        T_W(MEPA_TRACE_GRP_GEN, "Unknown interface type");
        return "";
    }
}


static mepa_rc lan8814_debug_info_dump(struct mepa_device *dev,
                                       lmu_ss_t *const ss,
                                       const mepa_debug_info_t   *const info)
{
    mepa_rc rc = MEPA_RC_OK;
    mepa_phy_info_t phy_info = {};
    mepa_port_interface_t mac_if;

    (void)lan8814_info_get(dev, &phy_info);
    (void)dev->drv->mepa_driver_if_get(dev, MEPA_SPEED_1G, &mac_if);

    MEPA_ENTER(dev);
    if (info->layer == MEPA_DEBUG_LAYER_AIL || info->layer == MEPA_DEBUG_LAYER_ALL) {
        pr("Port:%d   Family:LAN8814   Type:%d   Rev:%d   MacIf:%s\n", (int)dev->numeric_handle,
           phy_info.part_number, phy_info.revision, lan8814_mac_if_to_str(mac_if));
    }

    if (info->layer == MEPA_DEBUG_LAYER_CIL || info->layer == MEPA_DEBUG_LAYER_ALL) {
        if (info->group == MEPA_DEBUG_GROUP_ALL || info->group == MEPA_DEBUG_GROUP_PHY) {
            // PHY Debugging
            rc = lan8814_reg_dump(dev, ss);
        }
    }

    MEPA_EXIT(dev);

#if !defined(MEPA_LAN8814_LIGHT)
    // PHY_TS Debugging
    if (dev->drv->mepa_ts != NULL) {
        lan8814_ts_debug_info_dump(dev, ss, info);
    }
#endif

    return rc;
}
#endif

// Open Alliance TC1 / TC12 DCQ Signal Quality Index(SQI) method.
// Default values of MSE(mean square error) in SQI Table registers(1.232 - 1.238) is used for mapping signal quality indices 0-7.
// SQI for pair A is returned from this function.
static mepa_rc lan8814_sqi_read(mepa_device_t *dev, uint32_t *const value)
{
    lan8814_data_t *data = (lan8814_data_t *)dev->data;
    uint16_t   val;
    mepa_rc    rc;

    MEPA_ENTER(dev);

    // SQI is supported only for 100Mbps and 1Gbps
    // SQI values should not be available if link is down
    if ((data->conf.speed ==  MEPA_SPEED_10M) || !data->link_status || !data->conf.admin.enable) {
        rc = MEPA_RC_ERROR;
        goto do_exit;
    }

    (void)EP_RD(dev, LAN8814_DCQ_CTRL, &val);
    val &= ~LAN8814_M_DCQ_CTRL_CHANNEL_MASK; // Getting SQI value for channel 0;
    val |= LAN8814_F_DCQ_CTRL_READ_CAPTURE; // Enable this bit for capturing SQI values
    (void)EP_WR(dev, LAN8814_DCQ_CTRL, val);

    (void)EP_RD(dev, LAN8814_DCQ_SQI, &val);

    *value = LAN8814_X_DCQ_SQI_VALUE((uint32_t)val);

    rc = MEPA_RC_OK;

do_exit:
    MEPA_EXIT(dev);
    return rc;
}

#if !defined(MEPA_LAN8814_LIGHT)
static mepa_rc lan8814_start_of_frame_conf_set(mepa_device_t *dev, const mepa_start_of_frame_conf_t *const sof_conf)
{
    lan8814_data_t *data = (lan8814_data_t *)dev->data;
    mepa_gpio_conf_t gpio_conf;
    int rc;
    uint16_t val;
    int map_val;

    MEPA_ENTER(dev);

    rc = lan8814_port_flow_mapping_get(data->packet_idx, sof_conf->ingress, &map_val);
    if (rc != MEPA_RC_OK) {
        goto do_exit;
    }

    EP_RD(dev, LAN8814_GPIO_SOF_SEL, &val);
    switch (sof_conf->sof_no) {
    case 0:
        val &= ~LAN8814_M_GPIO_SOF_SEL_SOF0;
        val |= map_val;
        gpio_conf.gpio_no = 15;
        break;
    case 1:
        val &= ~LAN8814_M_GPIO_SOF_SEL_SOF1;
        val |= (map_val << 3);
        gpio_conf.gpio_no = 21;
        break;
    case 2:
        val &= ~LAN8814_M_GPIO_SOF_SEL_SOF2;
        val |= (map_val << 6);
        gpio_conf.gpio_no = 16;
        break;
    case 3:
        val &= ~LAN8814_M_GPIO_SOF_SEL_SOF3;
        val |= (map_val << 9);
        gpio_conf.gpio_no = 23;
        break;
    default:
        rc = MEPA_RC_ERROR;
        goto do_exit;
    }

    if (sof_conf->sof_preemption_mode > 3) {
        rc = MEPA_RC_ERROR;
        goto do_exit;
    }

    // Enabling SOF for corresponding port, direction and GPIO
    EP_WR(dev, LAN8814_GPIO_SOF_SEL, val);

    // Enabling SOF preemption
    EP_RD(dev, LAN8814_SOF, &val);
    val &= ~LAN8814_M_SOF_PREEMPTION_ENABLE;
    val |= sof_conf->sof_preemption_mode << 8;
    EP_WR(dev, LAN8814_SOF, val);

    gpio_conf.mode = MEPA_GPIO_MODE_OUT;
    rc = lan8814_gpio_mode_private(dev, &gpio_conf);
    if (rc != MEPA_RC_OK) {
        goto do_exit;
    }

    data->sof_conf = *sof_conf;
    rc = MEPA_RC_OK;

do_exit:
    MEPA_EXIT(dev);
    return rc;
}
#endif

#if !defined(MEPA_LAN8814_LIGHT)
static mepa_rc lan8814_start_of_frame_conf_get(mepa_device_t *dev, mepa_start_of_frame_conf_t *const value)
{
    lan8814_data_t *data = (lan8814_data_t *)dev->data;

    MEPA_ENTER(dev);
    *value = data->sof_conf;
    MEPA_EXIT(dev);

    return MEPA_RC_OK;
}
#endif

#if !defined(MEPA_LAN8814_LIGHT)
static mepa_rc lan8814_framepreempt_get(mepa_device_t *dev, mepa_bool_t *const value)
{
    lan8814_data_t *data = (lan8814_data_t *)dev->data;
    lan8814_data_t *base_data = data->base_dev ? ((lan8814_data_t *)(data->base_dev->data)) : NULL;

    MEPA_ASSERT(value == NULL);

    MEPA_ENTER(dev);
    *value = (base_data ? base_data->framepreempt_en : FALSE);
    MEPA_EXIT(dev);

    return MEPA_RC_OK;
}
#endif

#if !defined(MEPA_LAN8814_LIGHT)
static mepa_rc lan8814_framepreempt_set(mepa_device_t *dev, const mepa_bool_t enable)
{
    mepa_rc rc = MEPA_RC_ERROR;
    MEPA_ENTER(dev);
    rc = lan8814_framepreempt_set_(dev, enable);
    MEPA_EXIT(dev);
    return rc;
}
#endif

#if !defined(MEPA_LAN8814_LIGHT)
static mepa_rc lan8814_selftest_start(struct mepa_device *dev, const mepa_selftest_info_t *inf)
{
    mepa_rc rc;

    MEPA_ENTER(dev);
    rc = lan8814_selftest_start_(dev, inf);
    MEPA_EXIT(dev);

    return rc;
}
#endif

#if !defined(MEPA_LAN8814_LIGHT)
static mepa_rc lan8814_selftest_read(struct mepa_device *dev, mepa_selftest_info_t *const inf)
{
    uint16_t val;
    uint16_t lo, hi;
    mepa_rc  rc;

    if (inf == NULL) {
        return MEPA_RC_ERROR;
    }

    inf->frames = 0;
    inf->good_cnt = 0;
    inf->err_cnt = 0;

    MEPA_ENTER(dev);

    EP_RD(dev, LAN8814_SELFTEST_PGEN_EN, &val);
    if (!(val & LAN8814_F_SELFTEST_PGEN_EN)) {
        T_W(MEPA_TRACE_GRP_GEN, "Self-Test is not Enabled!\n");
        rc = MEPA_RC_ERROR;
        goto do_exit;
    }

    inf->mdi = MEPA_MEDIA_MODE_AUTO;
    RD(dev, LAN8814_GPHY_DBG_CTL1, &val);
    if (val & LAN8814_F_MDI_SET) {
        inf->mdi = MEPA_MEDIA_MODE_MDI;
    } else if (val & LAN8814_F_SWAPOFF) {
        inf->mdi = MEPA_MEDIA_MODE_MDIX;
    }

    inf->speed = MESA_SPEED_10M;
    RD(dev, LAN8814_BASIC_CONTROL, &val);
    if (val & LAN8814_F_BASIC_CTRL_SPEED_SEL_BIT_0) {
        inf->speed = MESA_SPEED_100M;
    } else if (val & LAN8814_F_BASIC_CTRL_SPEED_SEL_BIT_1) {
        inf->speed = MESA_SPEED_1G;
    }

    EP_RD(dev, LAN8814_SELFTEST_PKT_CNT_HI, &val);
    inf->frames = val;
    inf->frames = (inf->frames) << 16;
    EP_RD(dev, LAN8814_SELFTEST_PKT_CNT_LO, &val);
    inf->frames |= val;

    // 5. check counter value
    lo = hi = 0;
    //Self-Test Correct Count HI Register / Self-Test Correct Count LO Register
    EP_RD(dev, LAN8814_SELFTEST_GOOD_CNT_LO, &lo);
    EP_RD(dev, LAN8814_SELFTEST_GOOD_CNT_HI, &hi);
    inf->good_cnt = hi;
    inf->good_cnt = ((inf->good_cnt) << 16) | lo;

    lo = hi = 0;
    //Self-Test Error Count HI Register / Self-Test Error Count LO Register
    EP_RD(dev, LAN8814_SELFTEST_ERR_CNT_LO, &lo);
    EP_RD(dev, LAN8814_SELFTEST_ERR_CNT_HI, &hi);
    inf->err_cnt = hi;
    inf->err_cnt = ((inf->err_cnt) << 16) | lo;

    T_D(MEPA_TRACE_GRP_GEN, "speed=%d mdi=%d frames=0x%x good=0x%x err=0x%x\n",
        inf->speed, inf->mdi, inf->frames, inf->good_cnt, inf->err_cnt);

    if (inf->frames && (inf->frames == (inf->good_cnt + inf->err_cnt))) {
        lan8814_selftest_stop(dev);
    }

    rc = MEPA_RC_OK;

do_exit:
    MEPA_EXIT(dev);
    return rc;
}
#endif

static mepa_rc lan8814_prbs_set(mepa_device_t *dev, mepa_phy_prbs_type_t type, mepa_phy_prbs_direction_t direction,
                                const mepa_phy_prbs_generator_conf_t *const prbs_conf)
{
    lan8814_data_t *data = (lan8814_data_t *)dev->data;

    if (prbs_conf == NULL) {
        return MEPA_RC_ERROR;
    }

    if (type >= MEPA_PHY_PRBS_TYPE_CNT || direction >= MEPA_PHY_DIRECTION_CNT) {
        return MEPA_RC_ERROR;
    }

    if (direction == MEPA_PHY_DIRECTION_HOST && type == MEPA_PHY_PRBS_TYPE_SERDES) {
        mepa_rc rc = MEPA_RC_OK;

        MEPA_ENTER(dev);
        if (prbs_conf->prbsn_sel == MEPA_PRBS7 ||
            prbs_conf->prbsn_sel == MEPA_PRBS15 ||
            prbs_conf->prbsn_sel == MEPA_PRBS23 ||
            prbs_conf->prbsn_sel == MEPA_PRBS31) {
            rc = lan8814_prbs_set_priv(dev, prbs_conf->enable, prbs_conf->clk, prbs_conf->loopback, prbs_conf->prbsn_sel);
        } else {
            T_E(MEPA_TRACE_GRP_GEN, "Selected PRBS pattern invalid");
            rc = MEPA_RC_ERROR;
        }
        MEPA_EXIT(dev);
        data->prbs_conf = *prbs_conf;
        return rc;
    }

    return MEPA_RC_ERROR;
}

static mepa_rc lan8814_prbs_get(mepa_device_t *dev, mepa_phy_prbs_type_t type, mepa_phy_prbs_direction_t direction, mepa_phy_prbs_generator_conf_t *const prbs_conf)
{
    lan8814_data_t *data = (lan8814_data_t *)dev->data;

    MEPA_ENTER(dev);
    *prbs_conf = data->prbs_conf;
    MEPA_EXIT(dev);

    return MEPA_RC_OK;
}

static mepa_rc lan8814_prbs_monitor_set(mepa_device_t *dev, const mepa_phy_prbs_monitor_conf_t *const value)
{
    mepa_rc rc = MEPA_RC_ERROR;

    if (value->prbsn_sel == MEPA_PRBS7 ||
        value->prbsn_sel == MEPA_PRBS15 ||
        value->prbsn_sel == MEPA_PRBS23 ||
        value->prbsn_sel == MEPA_PRBS31) {
        MEPA_ENTER(dev);
        //Introducing one error into sequence
        rc = lan8814_serdes_set(dev, 0x1015, 0x0014, 0);
        MEPA_EXIT(dev);
    } else {
        T_E(MEPA_TRACE_GRP_GEN, "Selected PRBS pattern invalid");
    }

    return rc;
}

static mepa_rc lan8814_prbs_monitor_get(mepa_device_t *dev, mepa_phy_prbs_monitor_conf_t *const value)
{
    uint16_t val;
    mepa_rc  rc = MEPA_RC_ERROR;

    if (value->prbsn_sel == MEPA_PRBS7 ||
        value->prbsn_sel == MEPA_PRBS15 ||
        value->prbsn_sel == MEPA_PRBS23 ||
        value->prbsn_sel == MEPA_PRBS31) {
        MEPA_ENTER(dev);
        (void)EP_WR(dev, LAN8814_SERDES_CR_ADDR, 0x1017); // Check for  errors
        (void)EP_RD(dev, LAN8814_SERDES_CR_CONTROL, &val);
        val &= ~LAN8814_F_SERDES_CR_CONTROL_1;
        val |= LAN8814_F_SERDES_CR_CONTROL_0;
        (void)EP_WR(dev, LAN8814_SERDES_CR_CONTROL, val);
        (void)EP_RD(dev, LAN8814_SERDES_CR_DATA, &val);
        value->no_of_errors = val;
        MEPA_EXIT(dev);

        rc = MEPA_RC_OK;
    } else {
        T_E(MEPA_TRACE_GRP_GEN, "Selected PRBS pattern invalid");
    }

    return rc;
}

#if !defined(MEPA_LAN8814_LIGHT)
/**
 * This function is used to control the QSGMII serdes Tx eye amplitude with
 * TX_LEVEL[6:0] and TX_BOOST[13:7] to control the pre-emphasis level of
 * transmitter output (used upon frequency loss).
 */
static mepa_rc lan8814_serdes_tx_conf_set(mepa_device_t *dev, const mepa_serdes_tx_conf_t *const tx_conf)
{
    uint16_t      value     = 0;
    lan8814_data_t    *data     = (lan8814_data_t *)dev->data, *base_data;
    mepa_device_t *base_dev = (mepa_device_t *)data->base_dev;
    mepa_rc       rc;

    MEPA_ASSERT(base_dev == NULL);

    base_data = (lan8814_data_t *)base_dev->data;

    // Since QSGMII soft reset and serdes configuration registers are global,
    // this needs to be only configured via base port.
    if (base_dev != dev) {
        T_E(MEPA_TRACE_GRP_GEN, "Base port:%d needs to be passed", base_data->port_no);
        return MEPA_RC_ERROR;
    }

    MEPA_ENTER(dev);

    rc = MEPA_RC_ERROR;
    if (tx_conf->boost > 127) {
        T_E(MEPA_TRACE_GRP_GEN, "boost: %u exceeds maximum value of 127", tx_conf->boost);
        goto do_exit;
    }

    if (tx_conf->level > 127) {
        T_E(MEPA_TRACE_GRP_GEN, "level: %u exceeds maximum value of 127", tx_conf->level);
        goto do_exit;
    }

    value = ((LAN8814_QSGMII_SERDES_TX_LEVEL(tx_conf->level)) | LAN8814_QSGMII_SERDES_TX_BOOST(tx_conf->boost));
    T_I(MEPA_TRACE_GRP_GEN, "level: %u = 0x%x and boost: %u = 0x%x => value = 0x%x", tx_conf->level, tx_conf->level, tx_conf->boost, tx_conf->boost, value);

    EP_WRM(base_dev, LAN8814_QSGMII_SERDES_TX_CTRL, value, 0x3fff);
    EP_WRM(base_dev, LAN8814_QSGMII_SOFT_RESET, LAN8814_QSGMII_SOFT_RESET_BIT, LAN8814_QSGMII_SOFT_RESET_BIT);
    rc = MEPA_RC_OK;

do_exit:
    MEPA_EXIT(dev);
    return rc;
}
#endif

static mepa_rc lan8842_if_set(mepa_device_t *dev,
                              mepa_port_interface_t mac_if)
{
    if (mac_if != MESA_PORT_INTERFACE_SGMII) {
        return MEPA_RC_ERROR;
    }

    return MEPA_RC_OK;
}

static mepa_rc lan8842_if_get(mepa_device_t *dev, mepa_port_speed_t speed,
                              mepa_port_interface_t *mac_if)
{
    *mac_if = MESA_PORT_INTERFACE_SGMII;
    return MEPA_RC_OK;
}

static mepa_rc lan8842_get_device_info(mepa_device_t *dev)
{
    mepa_rc rc;

    MEPA_ENTER(dev);
    rc = lan8814_get_device_info(dev);
    MEPA_EXIT(dev);

    return rc;
}

static mepa_rc lan8842_info_get(mepa_device_t *dev, mepa_phy_info_t *const phy_info)
{
    lan8814_data_t *data = (lan8814_data_t *)dev->data;
    uint32_t cap_value;
    mepa_rc rc;

    rc = lan8842_get_device_info(dev);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    phy_info->manufactor_name = "Microchip";
    if (dev->drv->id == LAN8842_DRV_ID) {
        phy_info->part_number = 8842;
        phy_info->model_name = "LAN8842";
    } else {
        phy_info->part_number = 8832;
        phy_info->model_name = "LAN8832";
    }
    phy_info->revision = data->dev.rev;
    phy_info->ts_base_port = 0;
    phy_info->ts_base = NULL;

    cap_value = (uint32_t)phy_info->cap;
    if (lan8814_capability_priv(dev, (uint32_t)MEPA_CAP_TS_GEN_3) != 0U) {
        cap_value |= (uint32_t)MEPA_CAP_TS_MASK_GEN_3;
    }
    if (lan8814_capability_priv(dev, (uint32_t)MEPA_CAP_TS_NONE) != 0U) {
        cap_value |= (uint32_t)MEPA_CAP_TS_MASK_NONE;
    }
    if (lan8814_capability_priv(dev, (uint32_t)MEPA_CAP_SPEED_1G) != 0U) {
        cap_value |= (uint32_t)MEPA_CAP_SPEED_MASK_1G;
    }
    phy_info->cap = (mepa_phy_cap_t)cap_value;

    return MEPA_RC_OK;
}

#if !defined(MEPA_LAN8814_LIGHT)
static mepa_rc lan8842_start_of_frame_conf_set(mepa_device_t *dev,
                                               const mepa_start_of_frame_conf_t *const sof_conf)
{
    lan8814_data_t *data = (lan8814_data_t *)dev->data;
    mepa_gpio_conf_t gpio_conf;
    int rc;
    uint16_t val;

    MEPA_ENTER(dev);

    if (sof_conf->sof_no != 0) {
        rc = MEPA_RC_ERROR;
        goto do_exit;
    }

    if (sof_conf->sof_preemption_mode > 3) {
        rc = MEPA_RC_ERROR;
        goto do_exit;
    }

    // Enable SOF pulse to be generated on the GPIO
    EP_RD(dev, LAN8814_GPIO_SOF_SEL, &val);
    val = sof_conf->ingress ? 1 : 0;
    EP_WR(dev, LAN8814_GPIO_SOF_SEL, val);

    // Enabling SOF preemption
    EP_RD(dev, LAN8814_SOF, &val);
    val &= ~LAN8814_M_SOF_PREEMPTION_ENABLE;
    val |= sof_conf->sof_preemption_mode << 8;
    EP_WR(dev, LAN8814_SOF, val);

    // Enable GPIO as output to generated the SOF
    gpio_conf.gpio_no = 13;
    gpio_conf.mode = MEPA_GPIO_MODE_OUT;
    rc = lan8814_gpio_mode_private(dev, &gpio_conf);
    if (rc != MEPA_RC_OK) {
        goto do_exit;
    }

    data->sof_conf = *sof_conf;
    rc = MEPA_RC_OK;

do_exit:
    MEPA_EXIT(dev);
    return rc;
}
#endif

mepa_drivers_t mepa_lan8814_driver_init(void)
{
    // All the new drivers need to be added at the end of the array, otherwise
    // the lan8814_probe function might break as it is accessing directly this
    // array.
    static const int nr_lan8814_drivers = 5;
    static mepa_driver_t lan8814_drivers[] = {
        {
            .id = LAN8814_DEF_DRV_ID,  // LAN8814 QSGMII standalone PHY
            .mask = 0xfffff0,
            .mepa_driver_delete = lan8814_delete,
            .mepa_driver_reset = lan8814_reset,
            .mepa_driver_poll = lan8814_poll,
            .mepa_driver_conf_set = lan8814_conf_set,
            .mepa_driver_conf_get = lan8814_conf_get,
            .mepa_driver_if_set = lan8814_if_set,
            .mepa_driver_if_get = lan8814_if_get,
            .mepa_driver_probe = lan8814_probe,
            .mepa_driver_aneg_status_get = lan8814_aneg_status_get,
            .mepa_driver_clause22_read = lan8814_direct_reg_read,
            .mepa_driver_clause22_write = lan8814_direct_reg_write,
            .mepa_driver_clause45_read  = lan8814_ext_mmd_reg_read,
            .mepa_driver_clause45_write = lan8814_ext_mmd_reg_write,
            .mepa_driver_event_enable_set = lan8814_event_enable_set,
            .mepa_driver_event_enable_get = lan8814_event_enable_get,
            .mepa_driver_event_poll = lan8814_event_status_poll,
            .mepa_driver_gpio_mode_set = lan8814_gpio_mode_set,
            .mepa_driver_gpio_out_set = lan8814_gpio_out_set,
            .mepa_driver_gpio_in_get = lan8814_gpio_in_get,
            .mepa_driver_link_base_port = lan8814_link_base_port,
            .mepa_driver_capability = lan8814_capability,
            .mepa_driver_phy_info_get = lan8814_info_get,
            .mepa_driver_cable_diag_start = lan8814_cab_diag_start,
            .mepa_driver_cable_diag_get = lan8814_cab_diag_get,
            .mepa_driver_cable_diag_start_async = lan8814_cab_diag_start_async,
            .mepa_driver_cable_diag_stop_async = lan8814_cab_diag_stop_async,
            .mepa_driver_cable_diag_poll = lan8814_cab_diag_poll,
            .mepa_driver_loopback_set = lan8814_loopback_set,
            .mepa_driver_loopback_get = lan8814_loopback_get,
            .mepa_driver_prbs_set = lan8814_prbs_set,
            .mepa_driver_prbs_get = lan8814_prbs_get,
            .mepa_driver_prbs_monitor_set = lan8814_prbs_monitor_set,
            .mepa_driver_prbs_monitor_get = lan8814_prbs_monitor_get,
            .mepa_driver_sqi_read = lan8814_sqi_read,
#if !defined MEPA_LAN8814_LIGHT
            .mepa_driver_eee_mode_conf_set = lan8814_eee_mode_conf_set,
            .mepa_driver_eee_mode_conf_get = lan8814_eee_mode_conf_get,
            .mepa_driver_eee_status_get = lan8814_eee_status_get,
            .mepa_ts = &lan8814_ts_drivers,
            .mepa_driver_synce_clock_conf_set = lan8814_recovered_clk_set,
            .mepa_driver_isolate_mode_conf = lan8814_isolate_mode_conf,
            .mepa_driver_debug_info_dump = lan8814_debug_info_dump,
            .mepa_driver_start_of_frame_conf_set = lan8814_start_of_frame_conf_set,
            .mepa_driver_start_of_frame_conf_get = lan8814_start_of_frame_conf_get,
            .mepa_driver_framepreempt_get = lan8814_framepreempt_get,
            .mepa_driver_framepreempt_set = lan8814_framepreempt_set,
            .mepa_driver_selftest_start = lan8814_selftest_start,
            .mepa_driver_selftest_read = lan8814_selftest_read,
            .mepa_driver_serdes_tx_conf_set = lan8814_serdes_tx_conf_set,
#endif //!defined MEPA_LAN8814_LIGHT
        },
        {
            .id = LAN8804_SKU,  // Quad PHY without 1588
            .mask = 0xffff,
            .mepa_driver_delete = lan8814_delete,
            .mepa_driver_reset = lan8814_reset,
            .mepa_driver_poll = lan8814_poll,
            .mepa_driver_conf_set = lan8814_conf_set,
            .mepa_driver_conf_get = lan8814_conf_get,
            .mepa_driver_if_set = lan8814_if_set,
            .mepa_driver_if_get = lan8814_if_get,
            .mepa_driver_probe = lan8814_probe,
            .mepa_driver_aneg_status_get = lan8814_aneg_status_get,
            .mepa_driver_clause22_read = lan8814_direct_reg_read,
            .mepa_driver_clause22_write = lan8814_direct_reg_write,
            .mepa_driver_clause45_read  = lan8814_ext_mmd_reg_read,
            .mepa_driver_clause45_write = lan8814_ext_mmd_reg_write,
            .mepa_driver_event_enable_set = lan8814_event_enable_set,
            .mepa_driver_event_enable_get = lan8814_event_enable_get,
            .mepa_driver_event_poll = lan8814_event_status_poll,
            .mepa_driver_gpio_mode_set = lan8814_gpio_mode_set,
            .mepa_driver_gpio_out_set = lan8814_gpio_out_set,
            .mepa_driver_gpio_in_get = lan8814_gpio_in_get,
            .mepa_driver_link_base_port = lan8814_link_base_port,
            .mepa_driver_capability = lan8814_capability,
            .mepa_driver_phy_info_get = lan8814_info_get,
            .mepa_driver_cable_diag_start = lan8814_cab_diag_start,
            .mepa_driver_cable_diag_get = lan8814_cab_diag_get,
            .mepa_driver_cable_diag_start_async = lan8814_cab_diag_start_async,
            .mepa_driver_cable_diag_stop_async = lan8814_cab_diag_stop_async,
            .mepa_driver_cable_diag_poll = lan8814_cab_diag_poll,
            .mepa_driver_loopback_set = lan8814_loopback_set,
            .mepa_driver_loopback_get = lan8814_loopback_get,
            .mepa_driver_prbs_set = lan8814_prbs_set,
            .mepa_driver_prbs_get = lan8814_prbs_get,
            .mepa_driver_prbs_monitor_set = lan8814_prbs_monitor_set,
            .mepa_driver_prbs_monitor_get = lan8814_prbs_monitor_get,
            .mepa_driver_sqi_read = lan8814_sqi_read,
#if !defined MEPA_LAN8814_LIGHT
            .mepa_driver_eee_mode_conf_set = lan8814_eee_mode_conf_set,
            .mepa_driver_eee_mode_conf_get = lan8814_eee_mode_conf_get,
            .mepa_driver_eee_status_get = lan8814_eee_status_get,
            .mepa_driver_synce_clock_conf_set = lan8814_recovered_clk_set,
            .mepa_driver_isolate_mode_conf = lan8814_isolate_mode_conf,
            .mepa_driver_debug_info_dump = lan8814_debug_info_dump,
            .mepa_driver_start_of_frame_conf_set = lan8814_start_of_frame_conf_set,
            .mepa_driver_start_of_frame_conf_get = lan8814_start_of_frame_conf_get,
            .mepa_driver_framepreempt_get = lan8814_framepreempt_get,
            .mepa_driver_selftest_start = lan8814_selftest_start,
            .mepa_driver_selftest_read = lan8814_selftest_read,
            .mepa_driver_serdes_tx_conf_set = lan8814_serdes_tx_conf_set,
#endif //!defined MEPA_LAN8814_LIGHT
        },
        {
            .id = LAN8814_INT_PHY_DRV_ID,  // Single PHY based on LAN8814 instantiated in LAN966x
            .mask = 0xfffff0,
            .mepa_driver_delete = lan8814_delete,
            .mepa_driver_reset = lan8814_reset,
            .mepa_driver_poll = lan8814_poll,
            .mepa_driver_conf_set = lan8814_conf_set,
            .mepa_driver_conf_get = lan8814_conf_get,
            .mepa_driver_if_set = mas_if_set,
            .mepa_driver_if_get = mas_if_get,
            .mepa_driver_probe = lan8814_probe,
            .mepa_driver_aneg_status_get = lan8814_aneg_status_get,
            .mepa_driver_clause22_read = lan8814_direct_reg_read,
            .mepa_driver_clause22_write = lan8814_direct_reg_write,
            .mepa_driver_clause45_read  = lan8814_ext_mmd_reg_read,
            .mepa_driver_clause45_write = lan8814_ext_mmd_reg_write,
            .mepa_driver_event_enable_set = lan8814_event_enable_set,
            .mepa_driver_event_enable_get = lan8814_event_enable_get,
            .mepa_driver_event_poll = lan8814_event_status_poll,
            .mepa_driver_gpio_mode_set = lan8814_gpio_mode_set,
            .mepa_driver_gpio_out_set = lan8814_gpio_out_set,
            .mepa_driver_gpio_in_get = lan8814_gpio_in_get,
            .mepa_driver_capability = lan8814_capability,
            .mepa_driver_phy_info_get = lan8814_info_get,
            .mepa_driver_cable_diag_start = lan8814_cab_diag_start,
            .mepa_driver_cable_diag_get = lan8814_cab_diag_get,
            .mepa_driver_cable_diag_start_async = lan8814_cab_diag_start_async,
            .mepa_driver_cable_diag_stop_async = lan8814_cab_diag_stop_async,
            .mepa_driver_cable_diag_poll = lan8814_cab_diag_poll,
            .mepa_driver_loopback_set = lan8814_loopback_set,
            .mepa_driver_loopback_get = lan8814_loopback_get,
            .mepa_driver_prbs_set = lan8814_prbs_set,
            .mepa_driver_prbs_get = lan8814_prbs_get,
            .mepa_driver_prbs_monitor_set = lan8814_prbs_monitor_set,
            .mepa_driver_prbs_monitor_get = lan8814_prbs_monitor_get,
            .mepa_driver_sqi_read = lan8814_sqi_read,
#if !defined MEPA_LAN8814_LIGHT
            .mepa_driver_eee_mode_conf_set = lan8814_eee_mode_conf_set,
            .mepa_driver_eee_mode_conf_get = lan8814_eee_mode_conf_get,
            .mepa_driver_eee_status_get = lan8814_eee_status_get,
            .mepa_driver_synce_clock_conf_set = lan8814_recovered_clk_set,
            .mepa_driver_isolate_mode_conf = lan8814_isolate_mode_conf,
            .mepa_driver_debug_info_dump = lan8814_debug_info_dump,
            .mepa_driver_start_of_frame_conf_set = lan8814_start_of_frame_conf_set,
            .mepa_driver_start_of_frame_conf_get = lan8814_start_of_frame_conf_get,
            .mepa_driver_framepreempt_set = lan8814_framepreempt_set,
            .mepa_driver_framepreempt_get = lan8814_framepreempt_get,
            .mepa_driver_selftest_start = lan8814_selftest_start,
            .mepa_driver_selftest_read = lan8814_selftest_read,
            .mepa_driver_serdes_tx_conf_set = lan8814_serdes_tx_conf_set,
#endif //!defined MEPA_LAN8814_LIGHT
        },
        {
            .id = LAN8842_DRV_ID,
            .mask = 0xfffff0,
            .mepa_driver_delete = lan8814_delete,
            .mepa_driver_reset = lan8814_reset,
            .mepa_driver_poll = lan8814_poll,
            .mepa_driver_conf_set = lan8814_conf_set,
            .mepa_driver_conf_get = lan8814_conf_get,
            .mepa_driver_if_set = lan8842_if_set,
            .mepa_driver_if_get = lan8842_if_get,
            .mepa_driver_probe = lan8814_probe,
            .mepa_driver_aneg_status_get = lan8814_aneg_status_get,
            .mepa_driver_clause22_read = lan8814_direct_reg_read,
            .mepa_driver_clause22_write = lan8814_direct_reg_write,
            .mepa_driver_clause45_read  = lan8814_ext_mmd_reg_read,
            .mepa_driver_clause45_write = lan8814_ext_mmd_reg_write,
            .mepa_driver_event_enable_set = lan8814_event_enable_set,
            .mepa_driver_event_enable_get = lan8814_event_enable_get,
            .mepa_driver_event_poll = lan8814_event_status_poll,
            .mepa_driver_gpio_mode_set = lan8814_gpio_mode_set,
            .mepa_driver_gpio_out_set = lan8814_gpio_out_set,
            .mepa_driver_gpio_in_get = lan8814_gpio_in_get,
            .mepa_driver_capability = lan8814_capability,
            .mepa_driver_phy_info_get = lan8842_info_get,
            .mepa_driver_cable_diag_start = lan8814_cab_diag_start,
            .mepa_driver_cable_diag_get = lan8814_cab_diag_get,
            .mepa_driver_cable_diag_start_async = lan8814_cab_diag_start_async,
            .mepa_driver_cable_diag_stop_async = lan8814_cab_diag_stop_async,
            .mepa_driver_cable_diag_poll = lan8814_cab_diag_poll,
            .mepa_driver_loopback_set = lan8814_loopback_set,
            .mepa_driver_loopback_get = lan8814_loopback_get,
            .mepa_driver_prbs_set = lan8814_prbs_set,
            .mepa_driver_prbs_get = lan8814_prbs_get,
            .mepa_driver_prbs_monitor_set = lan8814_prbs_monitor_set,
            .mepa_driver_prbs_monitor_get = lan8814_prbs_monitor_get,
            .mepa_driver_sqi_read = lan8814_sqi_read,
#if !defined MEPA_LAN8814_LIGHT
            .mepa_driver_eee_mode_conf_set = lan8814_eee_mode_conf_set,
            .mepa_driver_eee_mode_conf_get = lan8814_eee_mode_conf_get,
            .mepa_driver_eee_status_get = lan8814_eee_status_get,
            .mepa_ts = &lan8814_ts_drivers,
            .mepa_driver_synce_clock_conf_set = lan8814_recovered_clk_set,
            .mepa_driver_isolate_mode_conf = lan8814_isolate_mode_conf,
            .mepa_driver_debug_info_dump = lan8814_debug_info_dump,
            .mepa_driver_start_of_frame_conf_set = lan8842_start_of_frame_conf_set,
            .mepa_driver_start_of_frame_conf_get = lan8814_start_of_frame_conf_get,
            .mepa_driver_framepreempt_set = lan8814_framepreempt_set,
            .mepa_driver_framepreempt_get = lan8814_framepreempt_get,
            .mepa_driver_selftest_start = lan8814_selftest_start,
            .mepa_driver_selftest_read = lan8814_selftest_read,
            .mepa_driver_serdes_tx_conf_set = lan8814_serdes_tx_conf_set,
#endif //!defined MEPA_LAN8814_LIGHT
        },
        {
            .id = LAN8832_SKU, // Without SyncE and 1588
            .mask = 0xffff,
            .mepa_driver_delete = lan8814_delete,
            .mepa_driver_reset = lan8814_reset,
            .mepa_driver_poll = lan8814_poll,
            .mepa_driver_conf_set = lan8814_conf_set,
            .mepa_driver_conf_get = lan8814_conf_get,
            .mepa_driver_if_set = lan8842_if_set,
            .mepa_driver_if_get = lan8842_if_get,
            .mepa_driver_probe = lan8814_probe,
            .mepa_driver_aneg_status_get = lan8814_aneg_status_get,
            .mepa_driver_clause22_read = lan8814_direct_reg_read,
            .mepa_driver_clause22_write = lan8814_direct_reg_write,
            .mepa_driver_clause45_read  = lan8814_ext_mmd_reg_read,
            .mepa_driver_clause45_write = lan8814_ext_mmd_reg_write,
            .mepa_driver_event_enable_get = lan8814_event_enable_get,
            .mepa_driver_event_enable_set = lan8814_event_enable_set,
            .mepa_driver_event_poll = lan8814_event_status_poll,
            .mepa_driver_gpio_mode_set = lan8814_gpio_mode_set,
            .mepa_driver_gpio_out_set = lan8814_gpio_out_set,
            .mepa_driver_gpio_in_get = lan8814_gpio_in_get,
            .mepa_driver_capability = lan8814_capability,
            .mepa_driver_phy_info_get = lan8842_info_get,
            .mepa_driver_cable_diag_start = lan8814_cab_diag_start,
            .mepa_driver_cable_diag_get = lan8814_cab_diag_get,
            .mepa_driver_cable_diag_start_async = lan8814_cab_diag_start_async,
            .mepa_driver_cable_diag_stop_async = lan8814_cab_diag_stop_async,
            .mepa_driver_cable_diag_poll = lan8814_cab_diag_poll,
            .mepa_driver_loopback_set = lan8814_loopback_set,
            .mepa_driver_loopback_get = lan8814_loopback_get,
            .mepa_driver_prbs_set = lan8814_prbs_set,
            .mepa_driver_prbs_get = lan8814_prbs_get,
            .mepa_driver_prbs_monitor_set = lan8814_prbs_monitor_set,
            .mepa_driver_prbs_monitor_get = lan8814_prbs_monitor_get,
            .mepa_driver_sqi_read = lan8814_sqi_read,
#if !defined MEPA_LAN8814_LIGHT
            .mepa_driver_eee_mode_conf_set = lan8814_eee_mode_conf_set,
            .mepa_driver_eee_mode_conf_get = lan8814_eee_mode_conf_get,
            .mepa_driver_eee_status_get = lan8814_eee_status_get,
            .mepa_driver_synce_clock_conf_set = lan8814_recovered_clk_set,
            .mepa_driver_isolate_mode_conf = lan8814_isolate_mode_conf,
            .mepa_driver_debug_info_dump = lan8814_debug_info_dump,
            .mepa_driver_start_of_frame_conf_set = lan8842_start_of_frame_conf_set,
            .mepa_driver_start_of_frame_conf_get = lan8814_start_of_frame_conf_get,
            .mepa_driver_framepreempt_set = lan8814_framepreempt_set,
            .mepa_driver_framepreempt_get = lan8814_framepreempt_get,
            .mepa_driver_selftest_start = lan8814_selftest_start,
            .mepa_driver_selftest_read = lan8814_selftest_read,
            .mepa_driver_serdes_tx_conf_set = lan8814_serdes_tx_conf_set,
#endif //!defined MEPA_LAN8814_LIGHT
        },
    };

    mepa_drivers_t result;
    result.phy_drv = lan8814_drivers;
    result.count = nr_lan8814_drivers;

    return result;
}
