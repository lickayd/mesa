// Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#include <stdbool.h>

#include <microchip/ethernet/phy/api.h>
#include <mepa_driver.h>
#include <mepa_utils.h>

#include "vsc8574_private.h"

// This driver is a subset of the driver found in the vtss folder.
// The driver supports only the vsc8574 family and only few features like:
// 1. Host interface QSGMII
// 2. It can have media type only Cu, SGMII or 1000BASE-X, there is no support
//    for dual media
// 3. There is no PTP, SQI, Cable diagnostics features, it just supports to link
//    up and the speeds 10/100/1000.

static mepa_rc vsc8574_phy_rd_wr_masked(mepa_device_t *dev,
                                        BOOL           read,
                                        const u32      addr,
                                        u16 *const     value,
                                        const u16      mask)
{
    u16     reg, page, val;
    mepa_rc rc = MEPA_RC_OK;

    // Page is encoded in address
    page = (u16)(addr >> 5);
    reg = (u16)(addr & 0x1fU);

    // Change page
    if (page != 0U) {
        rc = dev->callout->miim_write(dev->callout_ctx, 31, page);
    }
    if (rc == MEPA_RC_OK) {
        if (read) {
            // Read
            rc = dev->callout->miim_read(dev->callout_ctx, reg, value);
        } else if (mask != 0xffffU) {
            // Read-modify-write
            if ((rc = dev->callout->miim_read(dev->callout_ctx, reg, &val)) == MEPA_RC_OK) {
                rc = dev->callout->miim_write(dev->callout_ctx, reg, (val & ~mask) | (*value & mask));
            }
        } else {
            // Write
            rc = dev->callout->miim_write(dev->callout_ctx, reg, *value);
        }
    }

    // Restore standard page
    if ((page != 0U) && (rc == MEPA_RC_OK)) {
        rc = dev->callout->miim_write(dev->callout_ctx, 31, VTSS_PHY_PAGE_STANDARD);
    }

    return rc;
}

static mepa_rc vsc8574_phy_wr(mepa_device_t *dev,
                              const u32      addr,
                              const u16      value)
{
    u16 val = value;
    return vsc8574_phy_rd_wr_masked(dev, false, addr, &val, 0xffffU);
}

static mepa_rc vsc8574_phy_rd(mepa_device_t *dev,
                              const u32      addr,
                              u16 *const     value)
{
    return vsc8574_phy_rd_wr_masked(dev, true, addr, value, 0U);
}

static mepa_rc vsc8574_phy_page_std(mepa_device_t *dev)
{
    return vsc8574_phy_wr(dev, 31, VTSS_PHY_PAGE_STANDARD);
}

static mepa_rc vsc8574_phy_page_ext(mepa_device_t *dev)
{
    return vsc8574_phy_wr(dev, 31, VTSS_PHY_PAGE_EXTENDED);
}

static mepa_rc vsc8574_phy_page_ext3(mepa_device_t *dev)
{
    return vsc8574_phy_wr(dev, 31, VTSS_PHY_PAGE_EXTENDED_3);
}

static mepa_rc vsc8574_phy_page_ext2(mepa_device_t *dev)
{
    return vsc8574_phy_wr(dev, 31, VTSS_PHY_PAGE_EXTENDED_2);
}

static mepa_rc vsc8574_phy_page_gpio(mepa_device_t *dev)
{
    return vsc8574_phy_wr(dev, 31, VTSS_PHY_PAGE_GPIO);
}

static mepa_rc vsc8574_phy_page_test(mepa_device_t *dev)
{
    return vsc8574_phy_wr(dev, 31, VTSS_PHY_PAGE_TEST);
}

static mepa_rc vsc8574_phy_page_tr(mepa_device_t *dev)
{
    return vsc8574_phy_wr(dev, 31, VTSS_PHY_PAGE_TR);
}

static mepa_rc vsc8574_phy_wr_page(mepa_device_t *dev,
                                   const u16      page,
                                   const u32      addr,
                                   const u16      value,
                                   const u16      line)
{
    u16 val = value;
    return vsc8574_phy_rd_wr_masked(dev, false, addr, &val, 0xFFFFU);
}

static mepa_rc vsc8574_phy_rd_page(mepa_device_t *dev,
                                   const u16      page,
                                   const u32      addr,
                                   u16           *const value,
                                   const u16      line)
{
    return vsc8574_phy_rd_wr_masked(dev, true, addr, value, 0U);
}

static mepa_rc vsc8574_phy_wr_masked_page(mepa_device_t *dev,
                                          const u16      page,
                                          const u32      addr,
                                          const u16      value,
                                          const u16      mask,
                                          const u16      line)
{
    u16 val = value;
    return vsc8574_phy_rd_wr_masked(dev, false, addr, &val, mask);
}

#define PHY_WR_PAGE(dev, page_addr, value) vsc8574_phy_wr_page(dev, page_addr, value, __LINE__)
#define PHY_RD_PAGE(dev, page_addr, value) vsc8574_phy_rd_page(dev, page_addr, value, __LINE__)
#define PHY_WR_MASKED_PAGE(dev, page_addr, value, mask) vsc8574_phy_wr_masked_page(dev, page_addr, value, mask, __LINE__)

static void vsc8574_phy_decode_status_reg(u16 mii_status_reg, vsc8574_port_status_t *const status)
{
    // Link up/down
    status->link                   = ((mii_status_reg & VTSS_PHY_BIT(2)) != 0U);
    status->link_down              = ((mii_status_reg & VTSS_PHY_BIT(2)) == 0U);
    status->aneg_complete          = ((mii_status_reg & VTSS_PHY_BIT(5)) != 0U);
    status->remote_fault           = ((mii_status_reg & VTSS_PHY_BIT(4)) != 0U);
    status->unidirectional_ability = ((mii_status_reg & VTSS_F_PHY_UNIDIRECTIONAL_ABILITY) != 0U);
}

static void vsc8574_phy_flowcontrol_decode_status_priv(u16 lp_auto_neg_advertisment_reg, const vtss_phy_conf_t phy_setup, vsc8574_port_status_t *const status)
{
    BOOL                  sym_pause, asym_pause, lp_sym_pause, lp_asym_pause;
    sym_pause = phy_setup.aneg.symmetric_pause;
    asym_pause = phy_setup.aneg.asymmetric_pause;
    lp_sym_pause = ((lp_auto_neg_advertisment_reg & VTSS_PHY_BIT(10)) != 0U);
    lp_asym_pause = ((lp_auto_neg_advertisment_reg & VTSS_PHY_BIT(11)) != 0U);
    status->aneg.obey_pause =
        (sym_pause && (lp_sym_pause || (asym_pause && lp_asym_pause)));
    status->aneg.generate_pause =
        (lp_sym_pause && (sym_pause || (asym_pause && lp_asym_pause)));
}

static BOOL vsc8574_is_media_if_passthru(const vtss_phy_media_interface_t media_if)
{
    return media_if == VTSS_PHY_MEDIA_IF_SFP_PASSTHRU ||
           media_if == VTSS_PHY_MEDIA_IF_AMS_FI_PASSTHRU ||
           media_if == VTSS_PHY_MEDIA_IF_AMS_CU_PASSTHRU;
}

static mepa_rc vsc8574_phy_cl37_lp_abil_get_priv(mepa_device_t       *dev,
                                                 vsc8574_port_status_t *const status)
{
    vsc8574_data_t *data = (vsc8574_data_t *)(dev->data);
    u16                   reg;

    if (vsc8574_is_media_if_passthru(data->media_if)) {
        switch (data->conf.mode) {
        case VTSS_PHY_MODE_ANEG:
            // Vitesse PHY, use register 0 to determine if ANEG is Enabled
            MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_MODE_CONTROL, &reg));
            if ((reg & VTSS_F_PHY_MODE_CONTROL_AUTO_NEG_ENA) != 0U) {
                // Vitesse PHY, use register 28 to determine if ANEG has completed
                MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_AUXILIARY_CONTROL_AND_STATUS, &reg));
                if ((reg & VTSS_F_PHY_AUXILIARY_CONTROL_AND_STATUS_ANEG_COMPLETE) != 0U) {
                    ;
                } else {
                    return MEPA_RC_OK;
                }
            } else {
                return MEPA_RC_OK;
            }

            // Read link status from register 26E3 - This is Link Partner Status!
            // By "ANDING" these values, this is what is happening:
            // 1. the Link Status passed in from Reg01 must be OK - Indicates MAC & MEDIA Status
            // 2. the MEDIA Link Status from the Link Partner (Remote PHY) must also be OK
            MEPA_RC(vsc8574_phy_page_ext3(dev));
            MEPA_RC(PHY_RD_PAGE(dev,
                                VTSS_PHY_MEDIA_SERDES_CLAUSE_37_LP_ABILITY, &reg));
            if ((reg & VTSS_PHY_BIT(0)) != 0U) {   // Make sure we are dealing with SGMII Extensions
                status->link = (status->link && ((reg & VTSS_PHY_BIT(15)) != 0U));
                status->fdx = ((reg & VTSS_PHY_BIT(12)) != 0U);
                switch ((reg >> 10) & 0x3U) {
                case 0:
                    status->speed = VTSS_SPEED_10M;
                    break;
                case 1:
                    status->speed = VTSS_SPEED_100M;
                    break;
                case 2:
                    status->speed = VTSS_SPEED_1G;
                    break;
                default:
                    // Only two bits are decoded, so 3 is the last case.
                    status->speed = VTSS_SPEED_UNDEFINED;
                    break;
                }
            }
            break;

        case VTSS_PHY_MODE_FORCED:
            // In Forced Mode, Reg24E3 provides the status between the VSC PHY and the SFP
            // For CuSPF, which has another PHY in it, it is the Media i/f with the PHY inside the SFP
            // Therefore, Really Need ANEG enabled for CuSFP to get Cl37 Media info for link w/LP
            // For Fiber SPF, There is no PHY so it is the actual Media i/f coming out of the SFP
            // Vitesse PHY, use register 24E3 to determine Media i/f Status
            // Reg 24E3 Media PCS Status:
            // Reg 24E3.12 = SerDes Protocol Transfer 10Mb Speed
            // Reg 24E3.13 = SerDes Proto Transfer 100M Speed
            // Reg 24E3.13:12=00 (both bits clear) SerDes Protocol Transfer 1Gb.
            MEPA_RC(vsc8574_phy_page_ext3(dev));
            MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_MEDIA_SERDES_PCS_STATUS, &reg));
            status->link = (status->link && ((reg & VTSS_F_PHY_MEDIA_SERDES_PCS_STATUS_MEDIA_LINK_STATUS) != 0U));

            // Read link status from register 24E3 - Cl37 Advertisements of what we can do
            status->fdx = true;

            // Reg 24E3 Media PCS Status - 24E3.13:12=00 SerDes Protocol Transfer 1Gb.
            if ((reg & (VTSS_F_PHY_MEDIA_SERDES_PCS_STATUS_10MB_LINK_STATUS |
                        VTSS_F_PHY_MEDIA_SERDES_PCS_STATUS_100BASEFX_PROTO_XFER_LINK_STATUS)) == 0U) {
                status->speed = VTSS_SPEED_1G;
            } else if ((reg & VTSS_F_PHY_MEDIA_SERDES_PCS_STATUS_100BASEFX_PROTO_XFER_LINK_STATUS) != 0U) { // 24E3.13=SerDes ProtoXfer 100M
                status->speed = VTSS_SPEED_100M;
            } else if ((reg & VTSS_F_PHY_MEDIA_SERDES_PCS_STATUS_10MB_LINK_STATUS) != 0U) { // 24E3.12=SerDes Protocol Transfer 10Mb
                status->speed = VTSS_SPEED_10M;
            } else { // Reg 24E3 Media PCS Status, 24E3.13:12=11 - This should never happen as bits 12/13 are checked above
                status->speed = VTSS_SPEED_UNDEFINED;
            }

            break;
        default:
            // The remaining PHY modes carry no Cl37 link-partner status.
            break;
        }

        MEPA_RC(vsc8574_phy_page_std(dev));
    }
    return MEPA_RC_OK;
}

// Dynamic Transmit Side Power Optimizations
#define WORST_SUBCHAN_MSE 120U
#define WORST_AVERAGE_MSE 100U

// - EC length power-down optimzations
// - 1:Full-length(0), 2:64 taps(5), 3:48 taps(9), 4:32 taps(12), 5:16 taps(14)
static const u16 ecpdset[] = { 0, 5, 9, 12, 14 };
#define NUM_ECPD_SETTINGS (sizeof(ecpdset)/sizeof(ecpdset[0]))

// Computes optimal power setting level reductions based on
// calculated noise values from the specific cable being used
static mepa_rc vsc8574_phy_power_opt(mepa_device_t *dev)
{
    vsc8574_data_t *data = (vsc8574_data_t *)(dev->data);
    u16 reg17 = 0U, reg18 = 0U, ecpd_idx = 0U;
    u16 ncpd = 1U, half_adc = 1U;
    u32 maxMse = 0U, meanMse = 0U, tempMse, c, r, popt_idx, set[5][3];
    i16 done = 0;

    // - Prepare TR node for MSE Averaging - Normkmse_m7 = 3 (default 0)
    MEPA_RC(vsc8574_phy_page_tr(dev));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0xa3aaU));
    MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_PAGE_TR_17, &reg17));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0003U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, reg17));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x83aaU));

    for (c = 0U; c < 3U; c++) {
        for (r = 0U; r < 5U; r++) {
            set[r][c] = (5U * c) + (r + 1U);
        }
    }

    while (done < 2) {
        MEPA_RC(vsc8574_phy_page_test(dev));
        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_TEST_PAGE_12, ((ecpdset[ecpd_idx] << 12) | (ncpd << 10)), 0xfc00U));

                // Read-modify-write word containing half_comp_en
        MEPA_RC(vsc8574_phy_page_tr(dev));
        MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0xafe4U));
        MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_PAGE_TR_17, &reg17));
        reg17 = (reg17 & 0xffefU) | ((half_adc & 1U) << 4); //Seat or clear half_adc as desired
        MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, reg17));
        MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8fe4U));

        if (done != 0) {
            if (done < 0) {
                MEPA_MSLEEP(50);
            }
            ++done;
        } else {
            // - Read MSE A and B
            MEPA_RC(vsc8574_phy_page_tr(dev));
            MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0xa3c0U));
            MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_PAGE_TR_17, &reg17));
            MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_PAGE_TR_18, &reg18));

            maxMse = (u32)reg17 & 0x0fffU;
            tempMse = ((u32)reg17 >> 12) | ((u32)reg18 << 4);
            meanMse = maxMse + tempMse;
            if (tempMse > maxMse) {
                maxMse = tempMse;
            }

            // - Read MSE C and D
            MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0xa3c2U));
            MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_PAGE_TR_17, &reg17));
            MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_PAGE_TR_18, &reg18));

            meanMse += (u32)reg17 & 0x0fffU;
            if (((u32)reg17 & 0x0fffU) > maxMse) {
                maxMse = (u32)reg17 & 0x0fffU;
            }
            tempMse = ((u32)reg17 >> 12) | ((u32)reg18 << 4);
            meanMse += tempMse;
            if (tempMse > maxMse) {
                maxMse = tempMse;
            }
            meanMse /= 4U;

            if ((maxMse >= WORST_SUBCHAN_MSE) || (meanMse >= WORST_AVERAGE_MSE)) {
                if (ncpd == 0U) {    // - ADC Optimization went too far
                    half_adc--;
                    ncpd = 1;
                } else if (ecpd_idx == 0U) {    // - NC Optimization went too far
                    ncpd--;
                    done = -1;
                    ecpd_idx = 1;
                } else {                // - EC Optimization went too far
                    ecpd_idx--;
                    done = 1;
                }
            } else if ((ecpd_idx == 0U) && (ncpd < 2U)) {
                ncpd++;
            } else if (++ecpd_idx >= NUM_ECPD_SETTINGS) {
                done = 2;
            } else {
                // Keep optimizing with the newly incremented EC length.
            }
        }
    }

    if (ecpd_idx >= 1U) {
        popt_idx = set[ecpd_idx - 1U][ncpd];
    } else {
        popt_idx = set[0][ncpd];
    }


    data->power.pwrusage = (((ncpd > 0U) || (ecpd_idx > 0U)) ? popt_idx : VTSS_PHY_LINK_UP_FULL_PWR);

    // - Restore Normkmse_m7 = 0
    MEPA_RC(vsc8574_phy_page_tr(dev));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0xa3aaU));
    MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_PAGE_TR_17, &reg17));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, reg17));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x83aaU));
    MEPA_RC(vsc8574_phy_page_std(dev));

    return MEPA_RC_OK;
}

static mepa_rc vsc8574_phy_optimize_receiver_reconfig(mepa_device_t *dev)
{
    vsc8574_data_t *data = (vsc8574_data_t *)(dev->data);
    u16                vga_state_a;
    i16                max_vga_state_to_optimize;

    max_vga_state_to_optimize = -9;

    // BZ 1776/1860/2095/2107 part 3/3
    MEPA_RC(vsc8574_phy_page_tr(dev));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0xaff0U));
    MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_PAGE_TR_17, &vga_state_a));

    vga_state_a = ((vga_state_a >> 4) & 0x01fU);
    // vga_state_a is a 2's complement signed number ranging from -13 to +8
    // Test for vga_state_a < 16 is really a check for positive vga_state_a
    if ((vga_state_a < 16U) || (vga_state_a > ((u16)max_vga_state_to_optimize & 0x1fU))) {
        MEPA_RC(vsc8574_phy_page_test(dev));
        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_TEST_PAGE_12, 0x0000U, 0x0300U));
        data->power.pwrusage = VTSS_PHY_LINK_UP_FULL_PWR;
    } else {    // - Short loop
        if (data->power.vtss_phy_power_dynamic) {
            MEPA_RC(vsc8574_phy_page_test(dev));

            // - Dynamic Power Reduction
            MEPA_RC(vsc8574_phy_power_opt(dev));
        } else {
            data->power.pwrusage = VTSS_PHY_LINK_UP_FULL_PWR;
        }
    }
    return vsc8574_phy_page_std(dev);
}

static mepa_rc vsc8574_phy_status_get_priv(mepa_device_t            *dev,
                                           vsc8574_port_status_t *const status)
{
    vsc8574_data_t *data = (vsc8574_data_t *)(dev->data);
    u16                reg, reg10, reg17, reg24;
    mepa_rc            rc = MEPA_RC_OK;

    MEPA_RC(vsc8574_phy_page_std(dev));

    // Read link status from register 1
    MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_MODE_STATUS, &reg));

    // Read link status on host side of the phy from register 17E3
    MEPA_RC(vsc8574_phy_page_ext3(dev));
    MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_MAC_SERDES_PCS_STATUS, &reg17));
     // Read link status on Media side of the phy from register 24E3
    MEPA_RC(vsc8574_phy_page_ext3(dev));
    MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_MEDIA_SERDES_PCS_STATUS, &reg24));

    // Populates the Local PHY Status from Reg01 and Reg09
    vsc8574_phy_decode_status_reg(reg, status);

    if((data->mac_if == MESA_PORT_INTERFACE_QSGMII) || (data->mac_if == MESA_PORT_INTERFACE_SGMII)) {
        // Set Link Down Indication based on latched in link_status in Reg01, Reg17 and Reg24
        status->link_down = !((((reg & VTSS_PHY_BIT(2)) != 0U) ||
                               ((reg24 & VTSS_PHY_BIT(2)) != 0U)) &&
                              ((reg17 & VTSS_PHY_BIT(2)) != 0U));
    }

    MEPA_RC(vsc8574_phy_page_std(dev));

    if (status->link_down) {
        // Read status again if link down (latch low field)
        MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_MODE_STATUS, &reg));

        MEPA_RC(vsc8574_phy_page_ext3(dev));
        MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_MAC_SERDES_PCS_STATUS, &reg17));

        MEPA_RC(vsc8574_phy_page_ext3(dev));
        MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_MEDIA_SERDES_PCS_STATUS, &reg24));

        // Checks Family ,MAC interface and updates the link status based on Reg 01, Reg 24E3, Reg 17E3
        if((data->mac_if == MESA_PORT_INTERFACE_QSGMII) || (data->mac_if == MESA_PORT_INTERFACE_SGMII)) {
            status->link = ((((reg & VTSS_PHY_BIT(2)) != 0U) ||
                             ((reg24 & VTSS_PHY_BIT(2)) != 0U)) &&
                            ((reg17 & VTSS_PHY_BIT(2)) != 0U));
        }

        MEPA_RC(vsc8574_phy_page_std(dev));
    } else {
        status->link = true;
    }

    // Check if link has been down due to port has been reset
    // If the link has been down, we need to pass that indication up to Application
    if (data->link_down_due_to_port_reset) {
        status->link_down = TRUE; // Link due the port reset
        status->link = FALSE;     // Force Status to Link Down so Application is notified of the event
        data->link_down_due_to_port_reset = FALSE; // OK - Link down is now detected by the Application.
    }

    if (status->link) {
        switch (data->conf.mode) {
        case VTSS_PHY_MODE_ANEG:
            if ((reg & VTSS_PHY_BIT(5)) == 0U) {
                MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_AUXILIARY_CONTROL_AND_STATUS, &reg));
                status->mdi_cross = ((reg & VTSS_F_PHY_AUXILIARY_CONTROL_AND_STATUS_HP_AUTO_MDIX_CROSSOVER_INDICATION) != 0U);
                // Link up can not be trusted if auto-neg has not completed.
                if ((reg & 0x1bU) != 0xaU) {
                    // Auto negotiation not complete, link considered down
                    status->link = false;
                }
            }

            // Use register 5 to determine flow control result
            MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_AUTONEGOTIATION_LINK_PARTNER_ABILITY, &reg));
            vsc8574_phy_flowcontrol_decode_status_priv(reg, data->conf, status); // Decode type of flow control

            // Vitesse PHY, use register 28 to determine speed/duplex
            MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_AUXILIARY_CONTROL_AND_STATUS, &reg));
            status->mdi_cross = ((reg & VTSS_F_PHY_AUXILIARY_CONTROL_AND_STATUS_HP_AUTO_MDIX_CROSSOVER_INDICATION) != 0U);

            switch ((reg >> 3) & 0x3U) {
            case 0:
                status->speed = VTSS_SPEED_10M;
                break;
            case 1:
                status->speed = VTSS_SPEED_100M;
                break;
            case 2:
                status->speed = VTSS_SPEED_1G;
                break;
            default:
                // Only two bits are decoded, so 3 is the last case.
                status->speed = VTSS_SPEED_UNDEFINED;
                break;
            }
            status->fdx = ((reg & VTSS_PHY_BIT(5)) != 0U);

            break;
        case VTSS_PHY_MODE_FORCED:
            MEPA_RC(vsc8574_phy_page_std(dev));
            MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_AUXILIARY_CONTROL_AND_STATUS, &reg));
            status->mdi_cross = ((reg & VTSS_F_PHY_AUXILIARY_CONTROL_AND_STATUS_HP_AUTO_MDIX_CROSSOVER_INDICATION) != 0U);

            switch ((reg >> 3) & 0x3U) {
            case 0:
                status->speed = VTSS_SPEED_10M;
                break;
            case 1:
                status->speed = VTSS_SPEED_100M;
                break;
            case 2:
                status->speed = VTSS_SPEED_1G;
                break;
            default:
                // Only two bits are decoded, so 3 is the last case.
                status->speed = VTSS_SPEED_UNDEFINED;
                break;
            }
            status->fdx = ((reg & VTSS_PHY_BIT(5)) != 0U);

            break;
        case VTSS_PHY_MODE_POWER_DOWN:
            break;
        default:
            // Unsupported PHY mode
            rc = MEPA_RC_ERROR;
            break;
        }
    }

    if (rc != MEPA_RC_OK) {
        return rc;
    }

    // Fetch link, duplex and speed for PASSTHRU media mode

    // Determine if it is a fiber or CU port
    MEPA_RC(vsc8574_phy_page_ext(dev));
    MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_EXTENDED_PHY_CONTROL_3, &reg));
    status->fiber = (((reg >> 6) & 0x3U) == 2U);
    status->copper = (((reg >> 6) & 0x3U) == 1U);
    MEPA_RC(vsc8574_phy_page_std(dev));

    if (status->fiber) {
        // Errors are ignored on purpose: the link status collected so far is
        // still reported to the caller.
        (void)vsc8574_phy_cl37_lp_abil_get_priv(dev, status);
    }

    // Handle link down event
    // If the prev status was link up and there has been a link down event since last status update
    if ((!status->link || status->link_down) && data->status.link) {
        // -  Start of TESLA_AMS_WORK_AROUND_100FX_RJ45
        MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_EXTENDED_PHY_CONTROL, &reg));
        if ((reg & VTSS_F_PHY_EXTENDED_PHY_CONTROL_AMS_ENABLED) != 0U) {  // Is AMS Set for this port, bit 10: 0x0400 ?
            u16   regVal = 0x1000U; // Default for 100FX - AMS
            if ((reg & VTSS_F_PHY_EXTENDED_PHY_CONTROL_AMS_PREFERENCE) == 0U) {  // AMS Set for Fibre Preference
                reg = (reg & VTSS_M_PHY_EXTENDED_PHY_CONTROL_MEDIA_OPERATING_MODE) >> 8;
                if (reg == 7U) {  // Only for AMS - 100FX
                    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_MODE_CONTROL, regVal, regVal));
                }
            }
        } // -  End of TESLA_AMS_WORK_AROUND_100FX_RJ45
    }

    // Handle link up event
    if (status->link && (!data->status.link || status->link_down)) {// Don't do optimize if the status get is done during warmstart.
        // To avoid bz#21483
        MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_1000BASE_T_CONTROL, &reg10));
        // Determine if it is a fiber or CU port
        MEPA_RC(vsc8574_phy_page_ext(dev));
        MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_EXTENDED_PHY_CONTROL_3, &reg));
        status->fiber = (((reg >> 6) & 0x3U) == 2U);
        status->copper = (((reg >> 6) & 0x3U) == 1U);
        MEPA_RC(vsc8574_phy_page_std(dev));

        data->status = *status;

        if (status->speed == VTSS_SPEED_1G) {
            MEPA_RC(vsc8574_phy_optimize_receiver_reconfig(dev));
        }
    }

    // Determine if it is a fiber or CU port
    MEPA_RC(vsc8574_phy_page_ext(dev));
    MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_EXTENDED_PHY_CONTROL_3, &reg));
    status->fiber = (((reg >> 6) & 0x3U) == 2U);
    status->copper = (((reg >> 6) & 0x3U) == 1U);
    MEPA_RC(vsc8574_phy_page_std(dev));

    // Save status
    data->status = *status;
    return MEPA_RC_OK;
}

static mepa_rc vsc8574_phy_conf_1g_set_priv(mepa_device_t *dev,
                                            const vtss_phy_conf_1g_t *const conf)
{
    vsc8574_data_t *data = (vsc8574_data_t *)(dev->data);
    u16                reg, reg_cont = 0, reg_status = 0;

    data->conf_1g = *conf;

    MEPA_RC(vsc8574_phy_page_std(dev));
    MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_1000BASE_T_CONTROL, &reg_cont));
    MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_1000BASE_T_STATUS, &reg_status));

    reg = (u16)((data->conf_1g.master.cfg ? VTSS_PHY_BIT(12) : 0U) |
                (data->conf_1g.master.val ? VTSS_PHY_BIT(11) : 0U));

    // Commit 1000-BT Control values
    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_1000BASE_T_CONTROL, reg, 0x1800U));

    // Re-start Auto-Neg if Master/Slave Manual Config/Value changed
    if (((reg_cont ^ reg) & 0x1800U) != 0U) {
        if (((reg & 0x1000U) != 0U) || ((reg_status & 0x8000U) != 0U)) { // Only start Auto-neg if Manuel is selected or master/slave resolution failed
            MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_MODE_CONTROL, 0x0200U, 0x0200U));
        }
    }

    return MEPA_RC_OK;
}

static mepa_rc vsc8574_phy_mdi_control_reg(mepa_device_t *dev)
{
    vsc8574_port_status_t status;

    (void)memset(&status, 0, sizeof(status));

    MEPA_RC(vsc8574_phy_status_get_priv(dev, &status))

    if (status.speed == VTSS_SPEED_1G) {
        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_BYPASS_CONTROL, 0x0000U,
                                   VTSS_F_PHY_BYPASS_CONTROL_HP_AUTO_MDIX_AT_FORCE));
    } else {
        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_BYPASS_CONTROL, VTSS_F_PHY_BYPASS_CONTROL_HP_AUTO_MDIX_AT_FORCE,
                                   VTSS_F_PHY_BYPASS_CONTROL_HP_AUTO_MDIX_AT_FORCE));
    }

    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_BYPASS_CONTROL,
                               0,
                               VTSS_F_PHY_BYPASS_CONTROL_DISABLE_PARI_SWAP_CORRECTION));

    return MEPA_RC_OK;
}

// Function for configuring MDI for a given port, with the MDI selected in the vtss_state.
static mesa_rc vsc8574_phy_mdi_setup(mepa_device_t *dev)
{
    vsc8574_data_t *data = (vsc8574_data_t *)(dev->data);

    switch (data->conf.mdi) {
    case VTSS_PHY_MDIX_AUTO:
        // Enable  HP AUto-MDIX, and en pair swap correction.
        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_BYPASS_CONTROL,
                                   0x0000U,
                                   VTSS_F_PHY_BYPASS_CONTROL_HP_AUTO_MDIX_AT_FORCE | VTSS_F_PHY_BYPASS_CONTROL_DISABLE_PARI_SWAP_CORRECTION));

        MEPA_RC(vsc8574_phy_page_ext(dev));
        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_EXTENDED_MODE_CONTROL,
                                   0x0U, // Cu media forced normal HP-AUTO-MDIX operation, See datasheet
                                   VTSS_M_PHY_EXTENDED_MODE_CONTROL_FORCE_MDI_CROSSOVER));

        break;

    case VTSS_PHY_MDIX:
        MEPA_RC(vsc8574_phy_mdi_control_reg(dev));

        MEPA_RC(vsc8574_phy_page_ext(dev));
        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_EXTENDED_MODE_CONTROL,
                                   0x000CU, // Cu media forced MDI-X, See datasheet
                                   VTSS_M_PHY_EXTENDED_MODE_CONTROL_FORCE_MDI_CROSSOVER));
        break;

    case VTSS_PHY_MDI:
        MEPA_RC(vsc8574_phy_mdi_control_reg(dev));

        MEPA_RC(vsc8574_phy_page_ext(dev));
        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_EXTENDED_MODE_CONTROL,
                                   0x0008U, // Cu media forced MDI, See datasheet
                                   VTSS_M_PHY_EXTENDED_MODE_CONTROL_FORCE_MDI_CROSSOVER));


        break;
    default:
        // MDI mode is already handled above; nothing to set up.
        break;

    }
    MEPA_RC(vsc8574_phy_page_std(dev));
    return MEPA_RC_OK;
}

static mepa_rc vsc8574_phy_wait_for_micro_complete(mepa_device_t *dev)
{
    u16 timeout = 1000;
    u16 reg18g = 0;
    mepa_rc rc = MEPA_RC_OK;

    MEPA_RC(vsc8574_phy_page_gpio(dev));

    // Wait for micro to complete MCB command (bit 15)
    MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_MICRO_PAGE, &reg18g));
    while (((reg18g & 0x8000U) != 0U) && (timeout > 0U)) {
        MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_MICRO_PAGE, &reg18g));
        timeout--; // Make sure that we don't run forever
        MEPA_MSLEEP(1);
    }

    if (timeout == 0U) {
        rc = MEPA_RC_ERROR;
    }

    MEPA_RC(vsc8574_phy_page_std(dev));

    return rc;
}

//Function for suspending / resuming the 8051 patch.
//
// In : port_no - Any port within the chip where to supend 8051 patch
//      suspend - True if 8051 patch shall be suspended, else patch is resumed.
// Return : MEPA_RC_OK if patch was suspended else error code.
static mepa_rc vsc8574_atom_patch_suspend(mepa_device_t *dev, BOOL suspend)
{
    u16 word;

   MEPA_RC(vsc8574_phy_page_gpio(dev)); // Change to GPIO page

    if (suspend) {
        // Suspending 8051 patch
        MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_MICRO_PAGE, &word));
        if ((word & 0x4000U) == 0U) {
            MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_MICRO_PAGE, 0x800FU)); // Suspend 8051 EEE patch
        }
        MEPA_RC(vsc8574_phy_wait_for_micro_complete(dev));
    } else {
        // Resuming 8051 patch

        // On page 0x10 (Reg31 = 0x10), write register 18 with 0x8009 to turn on the EEE patch.
        // Once this is done all code that might attempt to access page 0x52b5 will fail and likely cause issues.
        // If you need to access page 0x52b5 or run another page 0x10, register 18 function, you must first disable
        // the patch by writing 0x8009 again to register 18.  In response, the error bit (bit 14) will be set,
        // but the micro is now freed from running the EEE patch and you may issue your other micro requests.
        // When the other requests are complete, you will want to rerun the EEE patch by writing 0x8009 to register 18 on page 0x10.
        // The events that are handled by the micro patch occur occasionally, say one event across 12 ports every 30 seconds.
        // As a result, suspending the EEE patch for short durations Is unlikely to result in link drops, but it is possible.
        MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_MICRO_PAGE, &word));
        if ((word & 0x4000U) != 0U) {
            MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_MICRO_PAGE, 0x8009U));
        }
        MEPA_RC(vsc8574_phy_wait_for_micro_complete(dev));
    }
    MEPA_RC(vsc8574_phy_page_std(dev)); // Change to standard page

    return MEPA_RC_OK;
}

static mepa_rc vsc8574_phy_set_private_atom(mepa_device_t *dev, const vtss_phy_mode_t mode)
{
    MEPA_RC(vsc8574_atom_patch_suspend(dev, TRUE));
    MEPA_RC(vsc8574_phy_page_test(dev));
    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_TEST_PAGE_8, 0x8000U, 0x8000U)); //Ensure RClk125 enabled even in powerdown
    // Clear Cl40AutoCrossover in forced-speed mode, but set it in non-forced modes
    MEPA_RC(vsc8574_phy_page_tr(dev));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0xa7faU));  // issue token-ring read request
    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_PAGE_TR_17, (mode == VTSS_PHY_MODE_FORCED)
                               ? 0x0000U : 0x1000U, 0x1000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x87faU));  // issue token-ring write request

    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0xaf82U));
    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x2U, 0xfU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8f82U));

    MEPA_RC(vsc8574_phy_page_test(dev));
    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_TEST_PAGE_8, 0x0000U, 0x8000U)); //Restore RClk125 gating
    MEPA_RC(vsc8574_atom_patch_suspend(dev, FALSE));
    MEPA_RC(vsc8574_phy_page_std(dev));

    // Enable HP Auto-MDIX in forced-mode (by clearing disable bit)
    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_BYPASS_CONTROL, 0x0000U, VTSS_F_PHY_BYPASS_CONTROL_HP_AUTO_MDIX_AT_FORCE));

    return MEPA_RC_OK;
}

// Function for setting phy in pass through mode according to "Application Note : Protocol transfer mode guide"
static mepa_rc vsc8574_phy_pass_through_speed_mode(mepa_device_t *dev)
{
    vsc8574_data_t *data = (vsc8574_data_t *)(dev->data);

    // From Protocol Transfer mode Guide (Written by James McIntosh and Jim Barnette)
    MEPA_RC(vsc8574_phy_page_std(dev));
    //Protocol Transfer mode Guide : Section 4.1.1 - Aneg must be enabled
    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_MODE_CONTROL, VTSS_F_PHY_MODE_CONTROL_AUTO_NEG_ENA, VTSS_F_PHY_MODE_CONTROL_AUTO_NEG_ENA));

    MEPA_RC(vsc8574_phy_page_ext3(dev));

    // Default clear "force advertise ability" bit as well
    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_MAC_SERDES_PCS_CONTROL,
                               VTSS_F_MAC_SERDES_PCS_CONTROL_ANEG_ENA,
                               VTSS_F_MAC_SERDES_PCS_CONTROL_ANEG_ENA | VTSS_F_MAC_SERDES_PCS_CONTROL_FORCE_ADV_ABILITY));

    // Protocol Transfer mode Guide : Section 4.1.3
    if (data->conf.mode == VTSS_PHY_MODE_FORCED) {
        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_MAC_SERDES_PCS_CONTROL, VTSS_F_MAC_SERDES_PCS_CONTROL_FORCE_ADV_ABILITY, VTSS_F_MAC_SERDES_PCS_CONTROL_FORCE_ADV_ABILITY));

        switch (data->conf.forced.speed) {
        case VTSS_SPEED_100M:
            MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_MAC_SERDES_CLAUSE_37_ADVERTISED_ABILITY, 0x8401U));
            break;

        case VTSS_SPEED_10M:
            MEPA_RC(PHY_WR_PAGE(dev,VTSS_PHY_MAC_SERDES_CLAUSE_37_ADVERTISED_ABILITY, 0x8001U));
            break;

        case VTSS_SPEED_1G:
            MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_MAC_SERDES_CLAUSE_37_ADVERTISED_ABILITY, 0x8801U));
            break;

        default:
            MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_MAC_SERDES_CLAUSE_37_ADVERTISED_ABILITY, 0x8801U));
            break;
        }
    }

    MEPA_RC(vsc8574_phy_page_std(dev));

    // Restart ANEG to ensure that the Cu SFP sends idle and config code words to the
    // MAC. This is for the VSC PHY, NOT the PHY inside the CuSFP module.
    //
    // We only want to restart ANEG the very first time we switch into pass-through
    // mode: if we are already in pass-through and there is a speed change, or the
    // user simply re-applies the current config, forcing ANEG would drop the link
    // temporarily. The warm-start branch of the original is dropped - vsc8574 has
    // no warm-start support, and there the flag is only set, never acted on.
    if (!data->cu_sfp_config_complete && data->conf.mode == VTSS_PHY_MODE_ANEG) {
        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_MODE_CONTROL,
                                   VTSS_F_PHY_MODE_CONTROL_AUTO_NEG_ENA |
                                   VTSS_F_PHY_MODE_CONTROL_RESTART_AUTO_NEG,
                                   VTSS_F_PHY_MODE_CONTROL_AUTO_NEG_ENA |
                                   VTSS_F_PHY_MODE_CONTROL_RESTART_AUTO_NEG));
        data->cu_sfp_config_complete = TRUE;
    }

    MEPA_RC(vsc8574_phy_page_std(dev));
    return MEPA_RC_OK;
}

static mepa_rc vsc8574_phy_conf_set_priv(mepa_device_t *dev,
                                         const vtss_phy_conf_t *const conf)
{
    vsc8574_data_t *data = (vsc8574_data_t *)(dev->data);
    mepa_device_t     *base_dev = data->base_dev;
    u16                current_reg_value, new_reg_value;
    u16                prev_mdi_value;
    u16                advertise_reg_bit_mask = 0;
    BOOL               restart_aneg = FALSE;
    mepa_rc            rc = MEPA_RC_OK;

    data->conf = *conf;

    // Read the current MDI Settings before changing any config
    MEPA_RC(vsc8574_phy_page_ext(dev));
    MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_EXTENDED_MODE_CONTROL, &prev_mdi_value));
    prev_mdi_value &= 0x000CU;

    // Ensure that we are on the Std Page Registers before we start programming
    MEPA_RC(vsc8574_phy_page_std(dev));
    // If in forced mdi mode and ANEG, we have to make sure to set MDI mode before ANEG restart
    MEPA_RC(vsc8574_phy_mdi_setup(dev));

    switch (conf->mode) {
    case VTSS_PHY_MODE_ANEG:
        // Setup register 4
        new_reg_value = (u16)((conf->aneg.tx_remote_fault   ? VTSS_PHY_BIT(13) : 0U) |
                              (conf->aneg.asymmetric_pause ? VTSS_PHY_BIT(11) : 0U) |
                              (conf->aneg.symmetric_pause  ? VTSS_PHY_BIT(10) : 0U) |
                              (conf->aneg.speed_100m_fdx   ? VTSS_PHY_BIT(8)  : 0U) |
                              (conf->aneg.speed_100m_hdx   ? VTSS_PHY_BIT(7)  : 0U) |
                              (conf->aneg.speed_10m_fdx    ? VTSS_PHY_BIT(6)  : 0U) |
                              (conf->aneg.speed_10m_hdx    ? VTSS_PHY_BIT(5)  : 0U) |
                              VTSS_PHY_BIT(0));

        if (vsc8574_is_media_if_passthru(data->media_if)) {
            advertise_reg_bit_mask = 0xac1fU; // In Pass though mode the advertisement is done in the cu SFP.
        } else {
            advertise_reg_bit_mask = 0xbfffU;// Bit 14 is reserved.
        }

        MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_DEVICE_AUTONEG_ADVERTISEMENT, &current_reg_value));

        if (current_reg_value != new_reg_value) {
            restart_aneg = TRUE;
        }

        // Normally, we are in MDIX_AUTO.
        // However, If we are in Forced MDI or MDIX mode, In order to assure that we are in the correct mode, restart ANEG
        // In some situations in 1G mode, the chip tries to maintain the link-up regardless of forced MDI settings
        // See Bugzilla 18532: MDI config is not taking effect immediately
        switch (conf->mdi) {
        case VTSS_PHY_MDIX_AUTO:
            if (prev_mdi_value != 0x0U) {
                restart_aneg = TRUE;
            }
            break;
        case VTSS_PHY_MDIX:
            if (prev_mdi_value != 0xCU) {
                restart_aneg = TRUE;
            }
            // The original fell through into the VTSS_PHY_MDI clause below;
            // the check is repeated here instead, as MISRA C-2023 Rule 16.3
            // requires every clause to end with an unconditional break.
            if (prev_mdi_value != 0x8U) {
                restart_aneg = TRUE;
            }
            break;
        case VTSS_PHY_MDI:
            if (prev_mdi_value != 0x8U) {
                restart_aneg = TRUE;
            }
            break;
        default:
            // No other MDI mode exists; nothing to restart for.
            break;
        }

        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_DEVICE_AUTONEG_ADVERTISEMENT,
                                   new_reg_value, advertise_reg_bit_mask));

        // Setup register 9 for 1G advertisement
        MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_1000BASE_T_CONTROL, &current_reg_value));
        new_reg_value = 0;
        if (conf->aneg.speed_1g_fdx) {
            new_reg_value |= VTSS_PHY_1000BASE_T_CONTROL_1000BASE_T_FDX_CAPABILITY;
        }
        // bug 17917:
        // For 1000BaseT-HDX, although allowed per 802.3, is not supported in industry
        // We are unaware of anyone making a Gigabit Ethernet hub, so testing 1000BT hdx with any vendors L2 device may be problematic.
        if (conf->aneg.speed_1g_hdx) {
            new_reg_value |= VTSS_PHY_1000BASE_T_CONTROL_1000BASE_T_HDX_CAPABILITY;
        }

        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_1000BASE_T_CONTROL, new_reg_value,
                                   (VTSS_PHY_1000BASE_T_CONTROL_1000BASE_T_FDX_CAPABILITY |
                                   VTSS_PHY_1000BASE_T_CONTROL_1000BASE_T_HDX_CAPABILITY)));

        if ((current_reg_value & (VTSS_PHY_1000BASE_T_CONTROL_1000BASE_T_FDX_CAPABILITY |
                                  VTSS_PHY_1000BASE_T_CONTROL_1000BASE_T_HDX_CAPABILITY)) != new_reg_value) {
            restart_aneg = TRUE;
        }

        MEPA_RC(vsc8574_phy_set_private_atom(dev, conf->mode));

        // Use register 0 to restart auto negotiation
        // Don't restart auton-neg at warm start if something has changed
        if (!conf->aneg.no_restart_aneg) {
            if (restart_aneg) {
                MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_MODE_CONTROL,
                                           VTSS_F_PHY_MODE_CONTROL_AUTO_NEG_ENA | VTSS_F_PHY_MODE_CONTROL_RESTART_AUTO_NEG,
                                           VTSS_F_PHY_MODE_CONTROL_AUTO_NEG_ENA | VTSS_F_PHY_MODE_CONTROL_RESTART_AUTO_NEG | VTSS_F_PHY_MODE_CONTROL_POWER_DOWN));
            } else {
                // If warm start then don't restart auto-negotiation.
                MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_MODE_CONTROL,
                                           VTSS_F_PHY_MODE_CONTROL_AUTO_NEG_ENA,
                                           VTSS_F_PHY_MODE_CONTROL_AUTO_NEG_ENA | VTSS_F_PHY_MODE_CONTROL_POWER_DOWN));
            }
        }
        break;

    case VTSS_PHY_MODE_FORCED:
        if (vsc8574_is_media_if_passthru(data->media_if)) {
            ;
        } else {
            // Bit 14 (data->loopback.near_end_enable) is not ported to
            // vsc8574, and bit 12 is always written as 0.
            new_reg_value = (u16)(((conf->forced.speed == VTSS_SPEED_100M) ? VTSS_PHY_BIT(13) : 0U) |
                                  (conf->forced.fdx                       ? VTSS_PHY_BIT(8)  : 0U) |
                                  ((conf->forced.speed == VTSS_SPEED_1G)  ? VTSS_PHY_BIT(6)  : 0U) |
                                  ((conf->unidir == VTSS_PHY_UNIDIRECTIONAL_ENABLE) ? VTSS_PHY_BIT(5) : 0U));

            MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_MODE_CONTROL, new_reg_value));

            if ((data->media_if == VTSS_PHY_MEDIA_IF_CU) && (data->conf.forced.speed == VTSS_SPEED_1G)) {
                // Work-around for Bug#9805/9806/1282 *** also *** Not in Warm-Start mode
                // Setting AUTO NEG advertisement to 0 in order to make sure that ANEG is restarted when return to aneg mode.
                MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_DEVICE_AUTONEG_ADVERTISEMENT, 0x0U,
                                           (VTSS_PHY_DEVICE_AUTONEG_ADVERTISEMENT_100BASETX_FDX |
                                           VTSS_PHY_DEVICE_AUTONEG_ADVERTISEMENT_100BASETX_HDX |
                                           VTSS_PHY_DEVICE_AUTONEG_ADVERTISEMENT_10BASET_FDX |
                                           VTSS_PHY_DEVICE_AUTONEG_ADVERTISEMENT_10BASET_HDX)));
            } else {
                // Enable Auto MDI/MDI-X in forced 10/100 mode
                MEPA_RC(vsc8574_phy_set_private_atom(dev, conf->mode));
            }   // End of forced.speed != VTSS_SPEED_1G
        } // End of !VTSS_PHY_MEDIA_IF_SFP_PASSTHRU

        break;

    case VTSS_PHY_MODE_POWER_DOWN:
        new_reg_value = 0;

        MEPA_RC(vsc8574_phy_page_std(dev));
        // Setup register 0
        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_MODE_CONTROL, VTSS_F_PHY_MODE_CONTROL_POWER_DOWN, VTSS_F_PHY_MODE_CONTROL_POWER_DOWN));

        break;
    default:
        // Unsupported PHY mode
        rc = MEPA_RC_ERROR;
        break;
    }

    if (rc != MEPA_RC_OK) {
        return rc;
    }

    if (conf->mode == VTSS_PHY_MODE_POWER_DOWN) {
        ;
    } else {
        // If in Not in ANEG mode, need to set mdi after vtss_phy_set_private_atom()
        MEPA_RC(vsc8574_phy_mdi_setup(dev));
    }

    if (vsc8574_is_media_if_passthru(data->media_if))  {
        MEPA_RC(vsc8574_phy_pass_through_speed_mode(dev));
    } else {
        MEPA_RC(vsc8574_phy_page_ext3(dev));
        // Setup Reg16E3
        new_reg_value = (u16)((conf->mac_if_pcs.disable            ? VTSS_PHY_BIT(15) : 0U) |
                              (conf->mac_if_pcs.restart           ? VTSS_PHY_BIT(14) : 0U) |
                              (conf->mac_if_pcs.pd_enable         ? VTSS_PHY_BIT(13) : 0U) |
                              (conf->mac_if_pcs.aneg_restart      ? VTSS_PHY_BIT(12) : 0U) |
                              (conf->mac_if_pcs.force_adv_ability ? VTSS_PHY_BIT(11) : 0U) |
                              ((u16)((u16)conf->mac_if_pcs.sgmii_in_pre << 9)) |
                              (conf->mac_if_pcs.sgmii_out_pre      ? VTSS_PHY_BIT(8) : 0U) |
                              (conf->mac_if_pcs.serdes_aneg_ena    ? VTSS_PHY_BIT(7) : 0U) |
                              (conf->mac_if_pcs.serdes_pol_inv_in  ? VTSS_PHY_BIT(6) : 0U) |
                              (conf->mac_if_pcs.serdes_pol_inv_out ? VTSS_PHY_BIT(5) : 0U) |
                              (conf->mac_if_pcs.fast_link_stat_ena ? VTSS_PHY_BIT(4) : 0U) |
                              (conf->mac_if_pcs.inhibit_odd_start  ? VTSS_PHY_BIT(2) : 0U));

        // Recommended by chip designers - Setting VTSS_F_MAC_SERDES_PCS_CONTROL_MAC_IF_PD_ENA,
        // This should allow link-up if the MAC is not doing auto-neg.
        // Enable "MAC interface autonegotiation parallel detect",
        //    else data flow is stopped for the CU ports if PHY has MAC ANEG enabled and the switch is connected to isn't
        new_reg_value |= VTSS_F_MAC_SERDES_PCS_CONTROL_MAC_IF_PD_ENA;
        // If the bits are ON by Default, leave them ON - We don't want to break default behavior
        // If clearing bit is desired, Clear the bit in the Register either before or after this Write
        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_MAC_SERDES_PCS_CONTROL, 0xFFFFU, new_reg_value));

        // MEPA:984: Handling this to clear the ANEG Control bit (Bit 7) in register 16E3
        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_MAC_SERDES_PCS_CONTROL, new_reg_value, 0x0080U));

        // Setup Reg23E3
        new_reg_value = (u16)(((u16)((u16)conf->media_if_pcs.remote_fault << 14)) |
                              (conf->media_if_pcs.aneg_pd_detect     ? VTSS_PHY_BIT(13) : 0U) |
                              (conf->media_if_pcs.force_adv_ability  ? VTSS_PHY_BIT(11) : 0U) |
                              (conf->media_if_pcs.serdes_pol_inv_in  ? VTSS_PHY_BIT(6)  : 0U) |
                              (conf->media_if_pcs.serdes_pol_inv_out ? VTSS_PHY_BIT(5)  : 0U) |
                              (conf->media_if_pcs.inhibit_odd_start  ? VTSS_PHY_BIT(4)  : 0U) |
                              (conf->media_if_pcs.force_hls          ? VTSS_PHY_BIT(2)  : 0U) |
                              (conf->media_if_pcs.force_fefi         ? VTSS_PHY_BIT(1)  : 0U) |
                              (conf->media_if_pcs.force_fefi_value   ? VTSS_PHY_BIT(0)  : 0U));

        // If the bits are ON by Default, leave them ON - We don't want to break default behavior
        // If clearing bit is desired, Clear the bit in the Register either before or after this Write
        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_MEDIA_SERDES_PCS_CONTROL, 0xFFFFU, new_reg_value));
        // This is setting the ANEG Advertisements for 1000BaseX mode of Operation for Fiber and CU SFP media interface.
        if ((conf->mode == VTSS_PHY_MODE_ANEG) && ((data->media_if == VTSS_PHY_MEDIA_IF_FI_1000BX) || (data->media_if == VTSS_PHY_MEDIA_IF_AMS_FI_1000BX) || (data->media_if == VTSS_PHY_MEDIA_IF_AMS_CU_1000BX))) {
            MEPA_RC(vsc8574_phy_page_ext3(dev));
            MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_MEDIA_SERDES_CL37_ADV_ABILITY, 0x0020U));
        }

        MEPA_RC(vsc8574_phy_page_std(dev));

        // Force AMS Override: Reg23.7:6 - 0:Normal; 1:SerDes; 2:Copper
        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_EXTENDED_PHY_CONTROL,
                                   VTSS_F_PHY_EXTENDED_PHY_CONTROL_AMS_OVERRIDE(conf->force_ams_sel),
                                   VTSS_M_PHY_EXTENDED_PHY_CONTROL_AMS_OVERRIDE));
    }
    // Set Sigdet pin polarity active high/low. Reg19E1.0
    switch (conf->sigdet) {
    case VTSS_PHY_SIGDET_POLARITY_ACT_LOW:
        MEPA_RC(vsc8574_phy_page_ext(dev));
        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_EXTENDED_MODE_CONTROL,
                                   1U << VTSS_S_PHY_EXTENDED_MODE_CONTROL_SIGDET_POLARITY,
                                   1U << 0));
        MEPA_RC(vsc8574_phy_page_std(dev));
        break;
    case VTSS_PHY_SIGDET_POLARITY_ACT_HIGH:
        MEPA_RC(vsc8574_phy_page_ext(dev));
        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_EXTENDED_MODE_CONTROL,
                                   0U << VTSS_S_PHY_EXTENDED_MODE_CONTROL_SIGDET_POLARITY,
                                   1U << 0));
        MEPA_RC(vsc8574_phy_page_std(dev));
        break;
    default:
        // Default is to keep the sigdet polarity active high
        break;
    }

    // Enable/disable fast link failure. Reg19E1.4
    switch (conf->flf) {
    case VTSS_PHY_FAST_LINK_FAIL_DISABLE:
        // This manipulates GPIO9 output, therefore it can only be done from PHY Port 0
        if (dev == base_dev) {
            MEPA_RC(vsc8574_phy_page_ext(dev));
            MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_EXTENDED_MODE_CONTROL,
                                       0x0U, VTSS_F_PHY_EXTENDED_MODE_CONTROL_FAST_LINK_FAILURE));
            MEPA_RC(vsc8574_phy_page_std(dev));
        }
        break;
    case VTSS_PHY_FAST_LINK_FAIL_ENABLE:
        // This manipulates GPIO9 output, therefore it can only be done from PHY Port 0
        if (dev == base_dev) {
            MEPA_RC(vsc8574_phy_page_ext(dev));
            MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_EXTENDED_MODE_CONTROL,
                                       VTSS_F_PHY_EXTENDED_MODE_CONTROL_FAST_LINK_FAILURE,
                                       VTSS_F_PHY_EXTENDED_MODE_CONTROL_FAST_LINK_FAILURE));
            MEPA_RC(vsc8574_phy_page_std(dev));
        }
        break;
    default:
        // Default is to keep the fast link failure pin disabled
        break;
    }

    return MEPA_RC_OK;
}

// The COMA_MODE pin is configured through micro/GPIO register 14G.[13:12].
// Bit 13 is an active-low output enable (defaults to 1, output disabled).
// Bit 12 is the value driven when the output is enabled.
static mepa_rc vsc8574_phy_coma_mode_priv(mepa_device_t *dev, BOOL low)
{
    MEPA_RC(vsc8574_phy_page_gpio(dev));
    if (low) {
        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_GPIO_CONTROL_2, 0x0000U,
                                   VTSS_F_PHY_GPIO_CONTROL_2_COMA_MODE_OUTPUT_ENABLE |
                                   VTSS_F_PHY_GPIO_CONTROL_2_COMA_MODE_OUTPUT_DATA));
    } else {
        // The coma mode pin MUST be pulled high by an external pull-up resistor,
        // so only the output enable is changed
        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_GPIO_CONTROL_2,
                                   VTSS_F_PHY_GPIO_CONTROL_2_COMA_MODE_OUTPUT_ENABLE,
                                   VTSS_F_PHY_GPIO_CONTROL_2_COMA_MODE_OUTPUT_ENABLE));
    }
    MEPA_RC(vsc8574_phy_page_std(dev));
    return MEPA_RC_OK;
}

static mepa_rc vsc8574_phy_soft_reset_port(mepa_device_t *dev)
{
    mepa_mtimer_t timer;
    u16           reg;
    mepa_rc       rc;

    // See bugzilla 9450 and 17849: Tesla PHY only - writing 0xc040
    reg = VTSS_F_PHY_MODE_CONTROL_SW_RESET | VTSS_F_PHY_MODE_CONTROL_LOOP | (1U << 6);
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_MODE_CONTROL, reg));

    MEPA_MSLEEP(1);                  // pause after reset
    MEPA_MTIMER_START(&timer, 5000); // Wait up to 5 seconds
    while (true) {
        if (PHY_RD_PAGE(dev, VTSS_PHY_MODE_CONTROL, &reg) == MEPA_RC_OK &&
            (reg & VTSS_F_PHY_MODE_CONTROL_SW_RESET) == 0U) {
            break;
        }
        MEPA_MSLEEP(1);
        if (MEPA_MTIMER_TIMEOUT(&timer) != 0) {
            T_E(MEPA_TRACE_GRP_GEN, "reset timeout, reg = 0x%X", reg);
            return MEPA_RC_ERROR;
        }
    }

    // After reset of a port, we need to re-configure it
    MEPA_RC(vsc8574_phy_conf_1g_set_priv(dev, &((vsc8574_data_t *)dev->data)->conf_1g));

    rc = vsc8574_phy_conf_set_priv(dev, &((vsc8574_data_t *)dev->data)->conf);
    if (rc != MEPA_RC_OK) {
        T_E(MEPA_TRACE_GRP_GEN, "vsc8574_phy_conf_set_priv failed");
        return rc;
    }

    return MEPA_RC_OK;
}

static mepa_rc vsc8574_port_reset(mepa_device_t *dev)
{
    MEPA_RC(vsc8574_atom_patch_suspend(dev, TRUE));  // Suspend micro patch while resetting
    MEPA_RC(vsc8574_phy_soft_reset_port(dev));
    MEPA_RC(vsc8574_atom_patch_suspend(dev, FALSE)); // Restart micro patch

    return MEPA_RC_OK;
}

// Returns TRUE if the MAC interface differs from the
// last one programmed for this port, and remembers the new one.
static BOOL vsc8574_mac_if_changed(mepa_device_t *dev)
{
    vsc8574_data_t *data = (vsc8574_data_t *)(dev->data);
    BOOL               mac_if_chged;

    // The MAC i/f can be SGMII, QSGMII or no-connection. A difference here may also
    // simply mean the state has not been set yet, not that the HW changed.
    mac_if_chged = (data->mac_if_old == data->mac_if) ? FALSE : TRUE;

    data->mac_if_old = data->mac_if; // Remember the last mac interface configured

    return mac_if_chged;
}

static mepa_rc vsc8574_phy_get_media_if_config(vtss_phy_media_interface_t media_if,
                                               u16                       *micro_cmd_100fx,
                                               u8                        *media_operating_mode,
                                               BOOL                      *cu_prefered)
{
    mepa_rc rc = MEPA_RC_OK;

    switch (media_if) {
    case VTSS_PHY_MEDIA_IF_CU:
        *media_operating_mode = 0;
        *cu_prefered = TRUE;
        break;
    case VTSS_PHY_MEDIA_IF_SFP_PASSTHRU:
        *media_operating_mode = 1;
        *cu_prefered = TRUE;
        break;
    case VTSS_PHY_MEDIA_IF_FI_1000BX:
        *media_operating_mode = 2;
        *cu_prefered = FALSE;
        break;
    case VTSS_PHY_MEDIA_IF_FI_100FX:
        *media_operating_mode = 3;
        *cu_prefered = FALSE;
        *micro_cmd_100fx = 1U << 4;
        break;
    case VTSS_PHY_MEDIA_IF_AMS_CU_PASSTHRU:
        *media_operating_mode = 5;
        *cu_prefered = TRUE;
        break;
    case VTSS_PHY_MEDIA_IF_AMS_FI_PASSTHRU:
        *media_operating_mode = 5;
        *cu_prefered = FALSE;
        break;
    case VTSS_PHY_MEDIA_IF_AMS_CU_1000BX:
        *media_operating_mode = 6;
        *cu_prefered = TRUE;
        break;
    case VTSS_PHY_MEDIA_IF_AMS_FI_1000BX:
        *media_operating_mode = 6;
        *cu_prefered = FALSE;
        break;
    case VTSS_PHY_MEDIA_IF_AMS_CU_100FX:
        *media_operating_mode = 7;
        *cu_prefered = TRUE;
        *micro_cmd_100fx = 1U << 4;
        break;
    case VTSS_PHY_MEDIA_IF_AMS_FI_100FX:
        *media_operating_mode = 7;
        *cu_prefered = FALSE;
        *micro_cmd_100fx = 1U << 4;
        break;
    default:
        // Media interface not supported by vsc8574
        rc = MESA_RC_ERR_PHY_MEDIA_IF_NOT_SUPPORTED;
        break;
    }

    return rc;
}

static mepa_rc vsc8574_phy_chip_port(mepa_device_t *dev, u16 *chip_port)
{
    u16 reg_val;
    u16 current_page;

    MEPA_RC(vsc8574_phy_rd(dev, 31, &current_page));
    MEPA_RC(vsc8574_phy_page_ext(dev));
    MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_EXTENDED_PHY_CONTROL_4, &reg_val));
    MEPA_RC(vsc8574_phy_wr(dev, 31, current_page));

    *chip_port = (reg_val & VTSS_M_VTSS_PHY_EXTENDED_PHY_CONTROL_4_PHY_ADDRESS) >> 11;
    return MEPA_RC_OK;
}

static mepa_rc vsc8574_phy_mac_media_if_tesla_setup(mepa_device_t *dev, BOOL *force_reset)
{
    vsc8574_data_t *data = (vsc8574_data_t *)(dev->data);
    u16                micro_cmd_100fx = 0;
    u16                reg_val;
    u16                reg_mask;
    u16                chip_port = 0;
    u8                 media_operating_mode = 0;
    BOOL               cu_prefered = FALSE;
    BOOL               mac_if_serdes_init = FALSE;
    BOOL               mac_if_has_chged_in_sw;
    mepa_rc            rc;

    mac_if_has_chged_in_sw = vsc8574_mac_if_changed(dev);

    // Setup MAC configuration
    MEPA_RC(vsc8574_phy_page_gpio(dev));
    switch (data->mac_if) {
    case MESA_PORT_INTERFACE_SGMII:
        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_MAC_MODE_AND_FAST_LINK, 0,
                                   VTSS_M_MAC_MODE_AND_FAST_LINK_MAC_IF_MODE_SELECT));
        break;
    case MESA_PORT_INTERFACE_QSGMII:
        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_MAC_MODE_AND_FAST_LINK, 0x4000U,
                                   VTSS_M_MAC_MODE_AND_FAST_LINK_MAC_IF_MODE_SELECT));
        break;
    case MESA_PORT_INTERFACE_NO_CONNECTION:
        break;
    default:
        // RGMII is deliberately not handled - this PHY does not support it, so it
        // falls through to the error below along with every other interface.
        T_E(MEPA_TRACE_GRP_GEN, "Mac interface %d not supported", data->mac_if);
        return MEPA_RC_ERROR;
    }

    // Setup media interface
    if ((rc = vsc8574_phy_get_media_if_config(data->media_if, &micro_cmd_100fx,
                                                 &media_operating_mode, &cu_prefered))
        != MEPA_RC_OK) {
        T_E(MEPA_TRACE_GRP_GEN, "Media interface %d not supported", data->media_if);
        return rc;
    }

    MEPA_RC(vsc8574_phy_page_gpio(dev));
    if (mac_if_has_chged_in_sw) {
        switch (data->mac_if) {
        case MESA_PORT_INTERFACE_SGMII:
        case MESA_PORT_INTERFACE_QSGMII:
            // The power-on default is SGMII, so a PHY whose MAC i/f has not been
            // initialised before still needs the init. See the SIMPLIFIED note above.
            mac_if_serdes_init = (dev == data->base_dev) ? FALSE : TRUE;
            break;
        case MESA_PORT_INTERFACE_NO_CONNECTION:
        default:
            mac_if_serdes_init = FALSE;
            break;
        }

        // Note: setting to GPIO MUST occur before writing to PHY_MICRO_PAGE
        MEPA_RC(vsc8574_phy_page_gpio(dev));
        if (data->mac_if == MESA_PORT_INTERFACE_QSGMII) {
            // Configure SerDes macros for QSGMII MAC interface (see TN1080).
            // Bug:16492 - don't reconfigure unless necessary, because reconfiguring
            // the SerDes means the SerDes patch must be reconfigured too. If the
            // MAC i/f changes a PHY reset is needed for the settings to take effect.
            if (!mac_if_serdes_init) {
                MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_MICRO_PAGE, 0x80E0U, 0xFFFFU));
                *force_reset = TRUE;
            }
        } else if (data->mac_if == MESA_PORT_INTERFACE_SGMII) {
            // Configure SerDes macros for 4xSGMII MAC interface (see TN1080).
            // Bug:16492 - all ports need to get initialised.
            if (!mac_if_serdes_init) {
                MEPA_RC(vsc8574_phy_page_gpio(dev));
                MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_MICRO_PAGE, 0x80F0U, 0xFFFFU));
                *force_reset = TRUE;
            }
        }
        MEPA_RC(vsc8574_phy_wait_for_micro_complete(dev));
    }
    MEPA_MSLEEP(10);

    // The original reads the chip port inline in each micro-command below; hoisted
    // here so the page save/restore it performs only happens once.
    MEPA_RC(vsc8574_phy_chip_port(dev, &chip_port));

    if (data->media_if == VTSS_PHY_MEDIA_IF_CU) {
        // Setup media in micro program
        MEPA_RC(vsc8574_phy_page_gpio(dev));
        // Turn off SerDes for 100Base-FX
        MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_MICRO_PAGE, 0x80f1U | (0x0100U << (chip_port % 4))));
        MEPA_RC(vsc8574_phy_wait_for_micro_complete(dev));
        // Turn off SerDes for 1000Base-X
        MEPA_RC(vsc8574_phy_page_gpio(dev));
        MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_MICRO_PAGE, 0x80e1U | (0x0100U << (chip_port % 4))));
        MEPA_RC(vsc8574_phy_wait_for_micro_complete(dev));
        MEPA_MSLEEP(10);
    } else {
        // Setup media in micro program. Bits 8-11 select the port (see TN1080)
        MEPA_RC(vsc8574_phy_page_gpio(dev));
        MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_MICRO_PAGE,
                            0x80C1U | (0x0100U << (chip_port % 4)) | micro_cmd_100fx));
        MEPA_RC(vsc8574_phy_wait_for_micro_complete(dev));
        MEPA_MSLEEP(10);
    }

    // Setup media interface
    MEPA_RC(vsc8574_phy_page_std(dev));
    reg_val = VTSS_F_PHY_EXTENDED_PHY_CONTROL_MEDIA_OPERATING_MODE(media_operating_mode) |
              (cu_prefered ? VTSS_F_PHY_EXTENDED_PHY_CONTROL_AMS_PREFERENCE : 0);
    reg_mask = VTSS_M_PHY_EXTENDED_PHY_CONTROL_MEDIA_OPERATING_MODE |
               VTSS_F_PHY_EXTENDED_PHY_CONTROL_AMS_PREFERENCE;
    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_EXTENDED_PHY_CONTROL, reg_val, reg_mask));

    // Set packet mode
    MEPA_RC(vsc8574_phy_page_std(dev));
    switch (data->pkt_mode) {
    case MESA_PHY_PKT_MODE_JUMBO_9_KB:
    case MESA_PHY_PKT_MODE_JUMBO_12_KB:
    case MESA_PHY_PKT_MODE_IEEE_1_5_KB:
        MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_EXTENDED_PHY_CONTROL_2,
                                   data->pkt_mode << 4, 0x30U));
        break;
    default:
        // The remaining packet modes need no preamble adjustment.
        break;
    }

    return MEPA_RC_OK;
}

static mepa_rc vsc8574_phy_micro_assert_reset(mepa_device_t *dev)
{
    // Set to micro/GPIO-page
    MEPA_RC(vsc8574_phy_page_gpio(dev));

    //----------------------------------------------------------------------
    // Pass the NOP cmd to Micro to insure that any consumptive patch exits
    // There is no issue with doing this on any revision since it is just a NOP on any Vitesse PHY.
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_MICRO_PAGE, 0x800fU));

    // Poll on 18G.15 to clear
    MEPA_RC(vsc8574_phy_wait_for_micro_complete(dev));
    //----------------------------------------------------------------------

    // Set to micro/GPIO-page (Has been set to std page by vsc8574_phy_wait_for_micro_complete
    MEPA_RC(vsc8574_phy_page_gpio(dev));

    // force micro into a loop, preventing any SMI accesses
    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_GPIO_12, 0x0000U, 0x0800U)); // Disable patch vector 3 (just in case)
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_GPIO_9, 0x005bU));     // Setup patch vector 3 to trap MicroWake interrupt
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_GPIO_10, 0x005bU));     // Loop forever on MicroWake interrupts
    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_GPIO_12, 0x0800U, 0x0800U)); // Enable patch vector 3
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_MICRO_PAGE, 0x800fU));     // Trigger MicroWake interrupt to make safe to reset

    // Assert reset after micro is trapped in a loop (averts micro-SMI access deadlock at reset)
    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_GPIO_0, 0x0000U, 0x8000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_MICRO_PAGE, 0x0000U));     // Make sure no MicroWake persists after reset
    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_GPIO_12, 0x0000U, 0x0800U)); // Disable patch vector 3
    return vsc8574_phy_page_std(dev);
}

static mepa_rc vsc8574_download_8051_code(mepa_device_t *dev, u8 const *code_array,
                                          u16 code_size)
{
    u16 i;

    // Note that the micro/GPIO-page, Reg31=0x10, is a global page, one per PHY chip
    // thus even though broadcast is turned off, it is still sufficient to do once`
    // Hold the micro in reset during patch download
    MEPA_RC(vsc8574_phy_micro_assert_reset(dev));
    MEPA_RC(vsc8574_phy_page_gpio(dev));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_GPIO_0, 0x7009U));      //  Hold 8051 in SW Reset,Enable auto incr address and patch clock,Disable the 8051 clock

    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_GPIO_12, 0x5002U));     // write to addr 4000= 02
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_GPIO_11, 0x0000U));     // write to address reg.

    for (i = 0; i < code_size; i++) {
        MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_GPIO_12, 0x5000U | code_array[i]));
    }

    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_GPIO_12, 0x0000U));     // Clear internal memory access

    // Signaling the micropatch has been downloaded. Note: port_no MUST be the base port no at this point.
    MEPA_RC(vsc8574_phy_page_std(dev)); // Switch back to standard register-page
    return MEPA_RC_OK;
}

static mepa_rc vsc8574_phy_is_8051_crc_ok_private(mepa_device_t *dev, u16 start_addr,
                                                  u16 code_length, u16 expected_crc,
                                                  BOOL first_time)
{
    vsc8574_data_t *data = (vsc8574_data_t *)(dev->data);

    MEPA_RC(vsc8574_phy_page_ext(dev));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_VERIPHY_CTRL_REG2, start_addr));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_VERIPHY_CTRL_REG3, code_length));

    MEPA_RC(vsc8574_phy_page_gpio(dev)); // Change to GPIO page
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_MICRO_PAGE, 0x8008U)); // Start Mirco command

    //MISSING MEPA_RC() is not an omission, it is intended so we get a CRC error
    (void) vsc8574_phy_wait_for_micro_complete(dev);

    // Get the CRC
    MEPA_RC(vsc8574_phy_page_ext(dev));
    MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_VERIPHY_CTRL_REG2, &data->micro_patch_crc));

    MEPA_RC(vsc8574_phy_page_std(dev)); // return to standard page

    if (data->micro_patch_crc == expected_crc) {
        return MEPA_RC_OK;
    } else {
        // The First Time this get called and the code has not been downloaded, It is not an error
        if (!first_time) {
        }
        return MEPA_RC_ERROR;
    }
}

static mepa_rc vsc8574_tesla_revB_8051_patch(mepa_device_t *dev)
{
    u16 reg_val = 0;
    u16 chip_port_no = 0;
    BOOL skip_dnload = FALSE;
    BOOL patch_ok = FALSE;

    const u8 patch_arr[] = {
        0x46U, 0x4aU, 0x02U, 0x43U, 0x37U, 0x02U, 0x46U, 0x26U, 0x02U, 0x46U,
        0x77U, 0x02U, 0x45U, 0x60U, 0x02U, 0x45U, 0xafU, 0xedU, 0xffU, 0xe5U,
        0xfcU, 0x54U, 0x38U, 0x64U, 0x20U, 0x70U, 0x08U, 0x65U, 0xffU, 0x70U,
        0x04U, 0xedU, 0x44U, 0x80U, 0xffU, 0x22U, 0x8fU, 0x19U, 0x7bU, 0xbbU,
        0x7dU, 0x0eU, 0x7fU, 0x04U, 0x12U, 0x3dU, 0xd7U, 0xefU, 0x4eU, 0x60U,
        0x03U, 0x02U, 0x41U, 0xf9U, 0xe4U, 0xf5U, 0x1aU, 0x74U, 0x01U, 0x7eU,
        0x00U, 0xa8U, 0x1aU, 0x08U, 0x80U, 0x05U, 0xc3U, 0x33U, 0xceU, 0x33U,
        0xceU, 0xd8U, 0xf9U, 0xffU, 0xefU, 0x55U, 0x19U, 0x70U, 0x03U, 0x02U,
        0x41U, 0xedU, 0x85U, 0x1aU, 0xfbU, 0x7bU, 0xbbU, 0xe4U, 0xfdU, 0xffU,
        0x12U, 0x3dU, 0xd7U, 0xefU, 0x4eU, 0x60U, 0x03U, 0x02U, 0x41U, 0xedU,
        0xe5U, 0x1aU, 0x54U, 0x02U, 0x75U, 0x1dU, 0x00U, 0x25U, 0xe0U, 0x25U,
        0xe0U, 0xf5U, 0x1cU, 0xe4U, 0x78U, 0xc5U, 0xf6U, 0xd2U, 0x0aU, 0x12U,
        0x41U, 0xfaU, 0x7bU, 0xffU, 0x7dU, 0x12U, 0x7fU, 0x07U, 0x12U, 0x3dU,
        0xd7U, 0xefU, 0x4eU, 0x60U, 0x03U, 0x02U, 0x41U, 0xe7U, 0xc2U, 0x0aU,
        0x74U, 0xc7U, 0x25U, 0x1aU, 0xf9U, 0x74U, 0xe7U, 0x25U, 0x1aU, 0xf8U,
        0xe6U, 0x27U, 0xf5U, 0x1bU, 0xe5U, 0x1dU, 0x24U, 0x5bU, 0x12U, 0x45U,
        0xeaU, 0x12U, 0x3eU, 0xdaU, 0x7bU, 0xfcU, 0x7dU, 0x11U, 0x7fU, 0x07U,
        0x12U, 0x3dU, 0xd7U, 0x78U, 0xccU, 0xefU, 0xf6U, 0x78U, 0xc1U, 0xe6U,
        0xfeU, 0xefU, 0xd3U, 0x9eU, 0x40U, 0x06U, 0x78U, 0xccU, 0xe6U, 0x78U,
        0xc1U, 0xf6U, 0x12U, 0x41U, 0xfaU, 0x7bU, 0xecU, 0x7dU, 0x12U, 0x7fU,
        0x07U, 0x12U, 0x3dU, 0xd7U, 0x78U, 0xcbU, 0xefU, 0xf6U, 0xbfU, 0x07U,
        0x06U, 0x78U, 0xc3U, 0x76U, 0x1aU, 0x80U, 0x1fU, 0x78U, 0xc5U, 0xe6U,
        0xffU, 0x60U, 0x0fU, 0xc3U, 0xe5U, 0x1bU, 0x9fU, 0xffU, 0x78U, 0xcbU,
        0xe6U, 0x85U, 0x1bU, 0xf0U, 0xa4U, 0x2fU, 0x80U, 0x07U, 0x78U, 0xcbU,
        0xe6U, 0x85U, 0x1bU, 0xf0U, 0xa4U, 0x78U, 0xc3U, 0xf6U, 0xe4U, 0x78U,
        0xc2U, 0xf6U, 0x78U, 0xc2U, 0xe6U, 0xffU, 0xc3U, 0x08U, 0x96U, 0x40U,
        0x03U, 0x02U, 0x41U, 0xd1U, 0xefU, 0x54U, 0x03U, 0x60U, 0x33U, 0x14U,
        0x60U, 0x46U, 0x24U, 0xfeU, 0x60U, 0x42U, 0x04U, 0x70U, 0x4bU, 0xefU,
        0x24U, 0x02U, 0xffU, 0xe4U, 0x33U, 0xfeU, 0xefU, 0x78U, 0x02U, 0xceU,
        0xa2U, 0xe7U, 0x13U, 0xceU, 0x13U, 0xd8U, 0xf8U, 0xffU, 0xe5U, 0x1dU,
        0x24U, 0x5cU, 0xcdU, 0xe5U, 0x1cU, 0x34U, 0xf0U, 0xcdU, 0x2fU, 0xffU,
        0xedU, 0x3eU, 0xfeU, 0x12U, 0x46U, 0x0dU, 0x7dU, 0x11U, 0x80U, 0x0bU,
        0x78U, 0xc2U, 0xe6U, 0x70U, 0x04U, 0x7dU, 0x11U, 0x80U, 0x02U, 0x7dU,
        0x12U, 0x7fU, 0x07U, 0x12U, 0x3eU, 0x9aU, 0x8eU, 0x1eU, 0x8fU, 0x1fU,
        0x80U, 0x03U, 0xe5U, 0x1eU, 0xffU, 0x78U, 0xc5U, 0xe6U, 0x06U, 0x24U,
        0xcdU, 0xf8U, 0xa6U, 0x07U, 0x78U, 0xc2U, 0x06U, 0xe6U, 0xb4U, 0x1aU,
        0x0aU, 0xe5U, 0x1dU, 0x24U, 0x5cU, 0x12U, 0x45U, 0xeaU, 0x12U, 0x3eU,
        0xdaU, 0x78U, 0xc5U, 0xe6U, 0x65U, 0x1bU, 0x70U, 0x82U, 0x75U, 0xdbU,
        0x20U, 0x75U, 0xdbU, 0x28U, 0x12U, 0x46U, 0x02U, 0x12U, 0x46U, 0x02U,
        0xe5U, 0x1aU, 0x12U, 0x45U, 0xf5U, 0xe5U, 0x1aU, 0xc3U, 0x13U, 0x12U,
        0x45U, 0xf5U, 0x78U, 0xc5U, 0x16U, 0xe6U, 0x24U, 0xcdU, 0xf8U, 0xe6U,
        0xffU, 0x7eU, 0x08U, 0x1eU, 0xefU, 0xa8U, 0x06U, 0x08U, 0x80U, 0x02U,
        0xc3U, 0x13U, 0xd8U, 0xfcU, 0xfdU, 0xc4U, 0x33U, 0x54U, 0xe0U, 0xf5U,
        0xdbU, 0xefU, 0xa8U, 0x06U, 0x08U, 0x80U, 0x02U, 0xc3U, 0x13U, 0xd8U,
        0xfcU, 0xfdU, 0xc4U, 0x33U, 0x54U, 0xe0U, 0x44U, 0x08U, 0xf5U, 0xdbU,
        0xeeU, 0x70U, 0xd8U, 0x78U, 0xc5U, 0xe6U, 0x70U, 0xc8U, 0x75U, 0xdbU,
        0x10U, 0x02U, 0x40U, 0xfdU, 0x78U, 0xc2U, 0xe6U, 0xc3U, 0x94U, 0x17U,
        0x50U, 0x0eU, 0xe5U, 0x1dU, 0x24U, 0x62U, 0x12U, 0x42U, 0x08U, 0xe5U,
        0x1dU, 0x24U, 0x5cU, 0x12U, 0x42U, 0x08U, 0x20U, 0x0aU, 0x03U, 0x02U,
        0x40U, 0x76U, 0x05U, 0x1aU, 0xe5U, 0x1aU, 0xc3U, 0x94U, 0x04U, 0x50U,
        0x03U, 0x02U, 0x40U, 0x3aU, 0x22U, 0xe5U, 0x1dU, 0x24U, 0x5cU, 0xffU,
        0xe5U, 0x1cU, 0x34U, 0xf0U, 0xfeU, 0x12U, 0x46U, 0x0dU, 0x22U, 0xffU,
        0xe5U, 0x1cU, 0x34U, 0xf0U, 0xfeU, 0x12U, 0x46U, 0x0dU, 0x22U, 0xe4U,
        0xf5U, 0x19U, 0x12U, 0x46U, 0x43U, 0x20U, 0xe7U, 0x1eU, 0x7bU, 0xfeU,
        0x12U, 0x42U, 0xf9U, 0xefU, 0xc4U, 0x33U, 0x33U, 0x54U, 0xc0U, 0xffU,
        0xc0U, 0x07U, 0x7bU, 0x54U, 0x12U, 0x42U, 0xf9U, 0xd0U, 0xe0U, 0x4fU,
        0xffU, 0x74U, 0x2aU, 0x25U, 0x19U, 0xf8U, 0xa6U, 0x07U, 0x12U, 0x46U,
        0x43U, 0x20U, 0xe7U, 0x03U, 0x02U, 0x42U, 0xdfU, 0x54U, 0x03U, 0x64U,
        0x03U, 0x70U, 0x03U, 0x02U, 0x42U, 0xcfU, 0x7bU, 0xcbU, 0x12U, 0x43U,
        0x2cU, 0x8fU, 0xfbU, 0x7bU, 0x30U, 0x7dU, 0x03U, 0xe4U, 0xffU, 0x12U,
        0x3dU, 0xd7U, 0xc3U, 0xefU, 0x94U, 0x02U, 0xeeU, 0x94U, 0x00U, 0x50U,
        0x2aU, 0x12U, 0x42U, 0xecU, 0xefU, 0x4eU, 0x70U, 0x23U, 0x12U, 0x43U,
        0x04U, 0x60U, 0x0aU, 0x12U, 0x43U, 0x12U, 0x70U, 0x0cU, 0x12U, 0x43U,
        0x1fU, 0x70U, 0x07U, 0x12U, 0x46U, 0x39U, 0x7bU, 0x03U, 0x80U, 0x07U,
        0x12U, 0x46U, 0x39U, 0x12U, 0x46U, 0x43U, 0xfbU, 0x7aU, 0x00U, 0x7dU,
        0x54U, 0x80U, 0x3eU, 0x12U, 0x42U, 0xecU, 0xefU, 0x4eU, 0x70U, 0x24U,
        0x12U, 0x43U, 0x04U, 0x60U, 0x0aU, 0x12U, 0x43U, 0x12U, 0x70U, 0x0fU,
        0x12U, 0x43U, 0x1fU, 0x70U, 0x0aU, 0x12U, 0x46U, 0x39U, 0xe4U, 0xfbU,
        0xfaU, 0x7dU, 0xeeU, 0x80U, 0x1eU, 0x12U, 0x46U, 0x39U, 0x7bU, 0x01U,
        0x7aU, 0x00U, 0x7dU, 0xeeU, 0x80U, 0x13U, 0x12U, 0x46U, 0x39U, 0x12U,
        0x46U, 0x43U, 0x54U, 0x40U, 0xfeU, 0xc4U, 0x13U, 0x13U, 0x54U, 0x03U,
        0xfbU, 0x7aU, 0x00U, 0x7dU, 0xeeU, 0x12U, 0x38U, 0xbdU, 0x7bU, 0xffU,
        0x12U, 0x43U, 0x2cU, 0xefU, 0x4eU, 0x70U, 0x07U, 0x74U, 0x2aU, 0x25U,
        0x19U, 0xf8U, 0xe4U, 0xf6U, 0x05U, 0x19U, 0xe5U, 0x19U, 0xc3U, 0x94U,
        0x02U, 0x50U, 0x03U, 0x02U, 0x42U, 0x15U, 0x22U, 0xe5U, 0x19U, 0x24U,
        0x17U, 0xfdU, 0x7bU, 0x20U, 0x7fU, 0x04U, 0x12U, 0x3dU, 0xd7U, 0x22U,
        0xe5U, 0x19U, 0x24U, 0x17U, 0xfdU, 0x7fU, 0x04U, 0x12U, 0x3dU, 0xd7U,
        0x22U, 0x7bU, 0x22U, 0x7dU, 0x18U, 0x7fU, 0x06U, 0x12U, 0x3dU, 0xd7U,
        0xefU, 0x64U, 0x01U, 0x4eU, 0x22U, 0x7dU, 0x1cU, 0xe4U, 0xffU, 0x12U,
        0x3eU, 0x9aU, 0xefU, 0x54U, 0x1bU, 0x64U, 0x0aU, 0x22U, 0x7bU, 0xccU,
        0x7dU, 0x10U, 0xffU, 0x12U, 0x3dU, 0xd7U, 0xefU, 0x64U, 0x01U, 0x4eU,
        0x22U, 0xe5U, 0x19U, 0x24U, 0x17U, 0xfdU, 0x7fU, 0x04U, 0x12U, 0x3dU,
        0xd7U, 0x22U, 0xd2U, 0x08U, 0x75U, 0xfbU, 0x03U, 0xabU, 0x7eU, 0xaaU,
        0x7dU, 0x7dU, 0x19U, 0x7fU, 0x03U, 0x12U, 0x3eU, 0xdaU, 0xe5U, 0x7eU,
        0x54U, 0x0fU, 0x24U, 0xf3U, 0x60U, 0x03U, 0x02U, 0x43U, 0xe9U, 0x12U,
        0x46U, 0x5aU, 0x12U, 0x46U, 0x61U, 0xd8U, 0xfbU, 0xffU, 0x20U, 0xe2U,
        0x35U, 0x13U, 0x92U, 0x0cU, 0xefU, 0xa2U, 0xe1U, 0x92U, 0x0bU, 0x30U,
        0x0cU, 0x2aU, 0xe4U, 0xf5U, 0x10U, 0x7bU, 0xfeU, 0x12U, 0x43U, 0xffU,
        0xefU, 0xc4U, 0x33U, 0x33U, 0x54U, 0xc0U, 0xffU, 0xc0U, 0x07U, 0x7bU,
        0x54U, 0x12U, 0x43U, 0xffU, 0xd0U, 0xe0U, 0x4fU, 0xffU, 0x74U, 0x2aU,
        0x25U, 0x10U, 0xf8U, 0xa6U, 0x07U, 0x05U, 0x10U, 0xe5U, 0x10U, 0xc3U,
        0x94U, 0x02U, 0x40U, 0xd9U, 0x12U, 0x46U, 0x5aU, 0x12U, 0x46U, 0x61U,
        0xd8U, 0xfbU, 0x54U, 0x05U, 0x64U, 0x04U, 0x70U, 0x27U, 0x78U, 0xc4U,
        0xe6U, 0x78U, 0xc6U, 0xf6U, 0xe5U, 0x7dU, 0xffU, 0x33U, 0x95U, 0xe0U,
        0xefU, 0x54U, 0x0fU, 0x78U, 0xc4U, 0xf6U, 0x12U, 0x44U, 0x0aU, 0x20U,
        0x0cU, 0x0cU, 0x12U, 0x46U, 0x5aU, 0x12U, 0x46U, 0x61U, 0xd8U, 0xfbU,
        0x13U, 0x92U, 0x0dU, 0x22U, 0xc2U, 0x0dU, 0x22U, 0x12U, 0x46U, 0x5aU,
        0x12U, 0x46U, 0x61U, 0xd8U, 0xfbU, 0x54U, 0x05U, 0x64U, 0x05U, 0x70U,
        0x1eU, 0x78U, 0xc4U, 0x7dU, 0xb8U, 0x12U, 0x43U, 0xf5U, 0x78U, 0xc1U,
        0x7dU, 0x74U, 0x12U, 0x43U, 0xf5U, 0xe4U, 0x78U, 0xc1U, 0xf6U, 0x22U,
        0x7bU, 0x01U, 0x7aU, 0x00U, 0x7dU, 0xeeU, 0x7fU, 0x92U, 0x12U, 0x38U,
        0xbdU, 0x22U, 0xe6U, 0xfbU, 0x7aU, 0x00U, 0x7fU, 0x92U, 0x12U, 0x38U,
        0xbdU, 0x22U, 0xe5U, 0x10U, 0x24U, 0x17U, 0xfdU, 0x7fU, 0x04U, 0x12U,
        0x3dU, 0xd7U, 0x22U, 0x78U, 0xc1U, 0xe6U, 0xfbU, 0x7aU, 0x00U, 0x7dU,
        0x74U, 0x7fU, 0x92U, 0x12U, 0x38U, 0xbdU, 0xe4U, 0x78U, 0xc1U, 0xf6U,
        0xf5U, 0x11U, 0x74U, 0x01U, 0x7eU, 0x00U, 0xa8U, 0x11U, 0x08U, 0x80U,
        0x05U, 0xc3U, 0x33U, 0xceU, 0x33U, 0xceU, 0xd8U, 0xf9U, 0xffU, 0x78U,
        0xc4U, 0xe6U, 0xfdU, 0xefU, 0x5dU, 0x60U, 0x44U, 0x85U, 0x11U, 0xfbU,
        0xe5U, 0x11U, 0x54U, 0x02U, 0x25U, 0xe0U, 0x25U, 0xe0U, 0xfeU, 0xe4U,
        0x24U, 0x5bU, 0xfbU, 0xeeU, 0x12U, 0x45U, 0xedU, 0x12U, 0x3eU, 0xdaU,
        0x7bU, 0x40U, 0x7dU, 0x11U, 0x7fU, 0x07U, 0x12U, 0x3dU, 0xd7U, 0x74U,
        0xc7U, 0x25U, 0x11U, 0xf8U, 0xa6U, 0x07U, 0x7bU, 0x11U, 0x7dU, 0x12U,
        0x7fU, 0x07U, 0x12U, 0x3dU, 0xd7U, 0xefU, 0x4eU, 0x60U, 0x09U, 0x74U,
        0xe7U, 0x25U, 0x11U, 0xf8U, 0x76U, 0x04U, 0x80U, 0x07U, 0x74U, 0xe7U,
        0x25U, 0x11U, 0xf8U, 0x76U, 0x0aU, 0x05U, 0x11U, 0xe5U, 0x11U, 0xc3U,
        0x94U, 0x04U, 0x40U, 0x9aU, 0x78U, 0xc6U, 0xe6U, 0x70U, 0x15U, 0x78U,
        0xc4U, 0xe6U, 0x60U, 0x10U, 0x75U, 0xd9U, 0x38U, 0x75U, 0xdbU, 0x10U,
        0x7dU, 0xfeU, 0x12U, 0x44U, 0xb8U, 0x7dU, 0x76U, 0x12U, 0x44U, 0xb8U,
        0x79U, 0xc6U, 0xe7U, 0x78U, 0xc4U, 0x66U, 0xffU, 0x60U, 0x03U, 0x12U,
        0x40U, 0x25U, 0x78U, 0xc4U, 0xe6U, 0x70U, 0x09U, 0xfbU, 0xfaU, 0x7dU,
        0xfeU, 0x7fU, 0x8eU, 0x12U, 0x38U, 0xbdU, 0x22U, 0x7bU, 0x01U, 0x7aU,
        0x00U, 0x7fU, 0x8eU, 0x12U, 0x38U, 0xbdU, 0x22U, 0xe4U, 0xf5U, 0xfbU,
        0x7dU, 0x1cU, 0xe4U, 0xffU, 0x12U, 0x3eU, 0x9aU, 0xadU, 0x07U, 0xacU,
        0x06U, 0xecU, 0x54U, 0xc0U, 0xffU, 0xedU, 0x54U, 0x3fU, 0x4fU, 0xf5U,
        0x20U, 0x30U, 0x06U, 0x2cU, 0x30U, 0x01U, 0x08U, 0xa2U, 0x04U, 0x72U,
        0x03U, 0x92U, 0x07U, 0x80U, 0x21U, 0x30U, 0x04U, 0x06U, 0x7bU, 0xccU,
        0x7dU, 0x11U, 0x80U, 0x0dU, 0x30U, 0x03U, 0x06U, 0x7bU, 0xccU, 0x7dU,
        0x10U, 0x80U, 0x04U, 0x7bU, 0x66U, 0x7dU, 0x16U, 0xe4U, 0xffU, 0x12U,
        0x3dU, 0xd7U, 0xeeU, 0x4fU, 0x24U, 0xffU, 0x92U, 0x07U, 0xafU, 0xfbU,
        0x74U, 0x26U, 0x2fU, 0xf8U, 0xe6U, 0xffU, 0xa6U, 0x20U, 0x20U, 0x07U,
        0x39U, 0x8fU, 0x20U, 0x30U, 0x07U, 0x34U, 0x30U, 0x00U, 0x31U, 0x20U,
        0x04U, 0x2eU, 0x20U, 0x03U, 0x2bU, 0xe4U, 0xf5U, 0xffU, 0x75U, 0xfcU,
        0xc2U, 0xe5U, 0xfcU, 0x30U, 0xe0U, 0xfbU, 0xafU, 0xfeU, 0xefU, 0x20U,
        0xe3U, 0x1aU, 0xaeU, 0xfdU, 0x44U, 0x08U, 0xf5U, 0xfeU, 0x75U, 0xfcU,
        0x80U, 0xe5U, 0xfcU, 0x30U, 0xe0U, 0xfbU, 0x8fU, 0xfeU, 0x8eU, 0xfdU,
        0x75U, 0xfcU, 0x80U, 0xe5U, 0xfcU, 0x30U, 0xe0U, 0xfbU, 0x05U, 0xfbU,
        0xafU, 0xfbU, 0xefU, 0xc3U, 0x94U, 0x04U, 0x50U, 0x03U, 0x02U, 0x44U,
        0xc5U, 0xe4U, 0xf5U, 0xfbU, 0x22U, 0xe5U, 0x7eU, 0x54U, 0x0fU, 0x64U,
        0x01U, 0x70U, 0x23U, 0xe5U, 0x7eU, 0x30U, 0xe4U, 0x1eU, 0x90U, 0x47U,
        0xd0U, 0xe0U, 0x44U, 0x02U, 0xf0U, 0x54U, 0xfbU, 0xf0U, 0x90U, 0x47U,
        0xd4U, 0xe0U, 0x44U, 0x04U, 0xf0U, 0x7bU, 0x03U, 0x7dU, 0x5bU, 0x7fU,
        0x5dU, 0x12U, 0x36U, 0x29U, 0x7bU, 0x0eU, 0x80U, 0x1cU, 0x90U, 0x47U,
        0xd0U, 0xe0U, 0x54U, 0xfdU, 0xf0U, 0x44U, 0x04U, 0xf0U, 0x90U, 0x47U,
        0xd4U, 0xe0U, 0x54U, 0xfbU, 0xf0U, 0x7bU, 0x02U, 0x7dU, 0x5bU, 0x7fU,
        0x5dU, 0x12U, 0x36U, 0x29U, 0x7bU, 0x06U, 0x7dU, 0x60U, 0x7fU, 0x63U,
        0x12U, 0x36U, 0x29U, 0x22U, 0xe5U, 0x7eU, 0x30U, 0xe5U, 0x35U, 0x30U,
        0xe4U, 0x0bU, 0x7bU, 0x02U, 0x7dU, 0x33U, 0x7fU, 0x35U, 0x12U, 0x36U,
        0x29U, 0x80U, 0x10U, 0x7bU, 0x01U, 0x7dU, 0x33U, 0x7fU, 0x35U, 0x12U,
        0x36U, 0x29U, 0x90U, 0x47U, 0xd2U, 0xe0U, 0x44U, 0x04U, 0xf0U, 0x90U,
        0x47U, 0xd2U, 0xe0U, 0x54U, 0xf7U, 0xf0U, 0x90U, 0x47U, 0xd1U, 0xe0U,
        0x44U, 0x10U, 0xf0U, 0x7bU, 0x05U, 0x7dU, 0x84U, 0x7fU, 0x86U, 0x12U,
        0x36U, 0x29U, 0x22U, 0xfbU, 0xe5U, 0x1cU, 0x34U, 0xf0U, 0xfaU, 0x7dU,
        0x10U, 0x7fU, 0x07U, 0x22U, 0x54U, 0x01U, 0xc4U, 0x33U, 0x54U, 0xe0U,
        0xf5U, 0xdbU, 0x44U, 0x08U, 0xf5U, 0xdbU, 0x22U, 0xf5U, 0xdbU, 0x75U,
        0xdbU, 0x08U, 0xf5U, 0xdbU, 0x75U, 0xdbU, 0x08U, 0x22U, 0xabU, 0x07U,
        0xaaU, 0x06U, 0x7dU, 0x10U, 0x7fU, 0x07U, 0x12U, 0x3eU, 0xdaU, 0x7bU,
        0xffU, 0x7dU, 0x10U, 0x7fU, 0x07U, 0x12U, 0x3dU, 0xd7U, 0xefU, 0x4eU,
        0x60U, 0xf3U, 0x22U, 0x12U, 0x44U, 0xc2U, 0x30U, 0x0cU, 0x03U, 0x12U,
        0x42U, 0x12U, 0x78U, 0xc4U, 0xe6U, 0xffU, 0x60U, 0x03U, 0x12U, 0x40U,
        0x25U, 0x22U, 0xe5U, 0x19U, 0x24U, 0x17U, 0x54U, 0x1fU, 0x44U, 0x80U,
        0xffU, 0x22U, 0x74U, 0x2aU, 0x25U, 0x19U, 0xf8U, 0xe6U, 0x22U, 0x12U,
        0x46U, 0x72U, 0x12U, 0x46U, 0x68U, 0x90U, 0x47U, 0xfaU, 0xe0U, 0x54U,
        0xf8U, 0x44U, 0x02U, 0xf0U, 0x22U, 0xe5U, 0x7eU, 0xaeU, 0x7dU, 0x78U,
        0x04U, 0x22U, 0xceU, 0xa2U, 0xe7U, 0x13U, 0xceU, 0x13U, 0x22U, 0xe4U,
        0x78U, 0xc4U, 0xf6U, 0xc2U, 0x0dU, 0x78U, 0xc1U, 0xf6U, 0x22U, 0xc2U,
        0x0cU, 0xc2U, 0x0bU, 0x22U, 0x22U
    };

    // Only download 8051 Patches to Port 0 of PHY!
    // Check to see if this is PHY Port 0, If it is not, the 8051 CRC Calculation will Fail on other PHY Ports
    MEPA_RC(vsc8574_phy_page_ext(dev));
    MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_EXTENDED_PHY_CONTROL_4, &reg_val));
    chip_port_no = (reg_val & VTSS_M_VTSS_PHY_EXTENDED_PHY_CONTROL_4_PHY_ADDRESS) >> 11;
    if (chip_port_no != 0U) {
    }
    // Check to see if the code has already been downloaded correctly, if so, Just return, No need to do it again
    // The size passed is one larger than the array: the download function
    // auto-adds one byte.
    if (vsc8574_phy_is_8051_crc_ok_private(dev, FIRMWARE_START_ADDR,
                                        (u16)(sizeof(patch_arr) + 1U),
                                        0x29E8U, true) == MEPA_RC_OK) {

        // CRC is Correct, So at this point, we can skip the Micro Dnload
        skip_dnload = TRUE;
        patch_ok = TRUE;

        // CRC is Correct, Make sure that all the patches are still present
        MEPA_RC(vsc8574_phy_page_gpio(dev));
        MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_GPIO_3, &reg_val));
        // Check: Trap ROM at _MicroSmiRead+0x1d to spoof patch-presence
        if (reg_val != 0x3eb7U) {
            patch_ok = FALSE;
        }

        // Check: Branch to starting address of SpoofPatchPresence
        MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_GPIO_4, &reg_val));
        if (reg_val != 0x4012U) {
            patch_ok = FALSE;
        }

        // Check: Enable patch fram trap described in register 3-4
        MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_GPIO_12, &reg_val));
        if (reg_val != 0x0100U) {
            patch_ok = FALSE;
        }

        // Check: Micro not in Reset, Enable 8051 clock enable; operate at 125 MHz
        MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_GPIO_0, &reg_val));
        if ((reg_val & 0xf1ffU) != 0xc018U) {
            // mask off bits 11:9 and check that speed, etc., are set as expected; i.e., "normal" operating mode with 125 MHz clock
            patch_ok = FALSE;
        }

        MEPA_RC(vsc8574_phy_page_std(dev));

    }

    if (!(skip_dnload && patch_ok)) {
        if (skip_dnload) {
            // The patch is already in PRAM but not running - just reset the micro
            MEPA_RC(vsc8574_phy_micro_assert_reset(dev));
        } else {
            MEPA_RC(vsc8574_download_8051_code(dev, &patch_arr[0], (u16)sizeof(patch_arr)));
        }

        // At this point the patch is either freshly downloaded or known not to
        // be running, so always (re-)install the trap and restart the 8051.
        MEPA_RC(vsc8574_phy_page_gpio(dev));
        MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_GPIO_3, 0x3eb7U));  // Trap ROM at _MicroSmiRead+0x1d to spoof patch-presence
        MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_GPIO_4, 0x4012U));  // Branch to starting address of SpoofPatchPresence
        MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_GPIO_12, 0x0100U)); // Enable patch fram trap described in register 3-4
        // Enable 8051 clock; CLEAR patch present; disable PRAM clock override and addr. auto-incr; operate at 125 MHz
        MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_GPIO_0, 0x4018U));
        MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_GPIO_0, 0xc018U));  // Release 8051 SW Reset

        MEPA_RC(vsc8574_phy_page_std(dev));

        // Check that code is downloaded correctly.
        MEPA_RC(vsc8574_phy_is_8051_crc_ok_private(dev, FIRMWARE_START_ADDR,
                                                (u16)(sizeof(patch_arr) + 1U), // Add one for the byte auto-added in the download function
                                                0x29E8U, false));
    }

    return MEPA_RC_OK;
}

// Port of vtss_phy_pre_init_seq_tesla_rev_e(). Register script copied verbatim.
static mepa_rc vsc8574_phy_pre_init_seq_tesla_rev_e(mepa_device_t *dev)
{
    MEPA_RC(vsc8574_phy_page_std(dev));
    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_EXTENDED_CONTROL_AND_STATUS, 0x0001U, 0x0001U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_EXTENDED_PHY_CONTROL_2, 0x0040U));
    MEPA_RC(vsc8574_phy_page_ext2(dev));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_CU_PMD_TX_CTRL, 0x02beU));
    MEPA_RC(vsc8574_phy_page_test(dev));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_TEST_PAGE_20, 0x4320U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_TEST_PAGE_24, 0x0c00U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_TEST_PAGE_9, 0x18caU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_TEST_PAGE_5, 0x1b20U));
    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_TEST_PAGE_8, 0x8000U, 0x8000U));
    MEPA_RC(vsc8574_phy_page_tr(dev));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0004U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x01bdU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8faeU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x000fU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x000fU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8facU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x00a0U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0xf147U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x97a0U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0005U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x2f54U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8fe4U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0027U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x303dU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x9792U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x0704U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x87feU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0006U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x0150U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8fe0U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0012U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0xb00aU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8f82U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x0d74U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8f80U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x0012U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x82e0U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0005U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x0208U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x83a2U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x9186U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x83b2U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x000eU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x3700U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8fb0U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0004U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x9f81U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x9688U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0xffffU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8fd2U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0003U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x9fa2U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x968aU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0020U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x640bU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x9690U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x2220U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8258U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x2a20U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x825aU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x3060U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x825cU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x3fa0U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x825eU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0xe0f0U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x83a6U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x1489U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8f92U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x7000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x96a2U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0007U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x1448U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x96a6U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x00eeU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0xffddU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x96a0U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0091U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0xb06cU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8fe8U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0004U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x1600U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8feaU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x00eeU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0xff00U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x96b0U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x7000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x96b2U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x0814U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x96b4U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0068U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x8980U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8f90U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0xd8f0U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x83a4U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x0400U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8fc0U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0050U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x100fU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x87faU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x0003U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8796U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x00c3U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0xff98U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x87f8U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0018U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x292aU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8fa4U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x00d2U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0xc46fU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x968cU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x0620U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x97a2U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0013U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x132fU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x96a4U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x96a8U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x00c0U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0xa028U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8ffcU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0090U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x1c09U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8fecU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0004U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0xa6a1U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8feeU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x00b0U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x1807U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8ffeU));
    MEPA_RC(vsc8574_phy_page_ext2(dev));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x028eU));
    MEPA_RC(vsc8574_phy_page_tr(dev));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0008U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0xa518U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8486U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x006dU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0xc696U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x8488U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x0912U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x848aU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x0db6U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x848eU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0059U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x6596U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x849cU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x0514U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x849eU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0041U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x0280U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x84a2U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x84a4U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x84a6U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x84a8U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x0000U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x84aaU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x007dU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0xf7ddU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x84aeU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x006dU));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x95d4U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x84b0U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_18, 0x0049U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_17, 0x2410U));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_PAGE_TR_16, 0x84b2U));
    MEPA_RC(vsc8574_phy_page_test(dev));
    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_TEST_PAGE_8, 0x0000U, 0x8000U));
    MEPA_RC(vsc8574_phy_page_std(dev));
    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_EXTENDED_CONTROL_AND_STATUS, 0x0000U, 0x0001U));

    MEPA_RC(vsc8574_tesla_revB_8051_patch(dev)); // This is the OLD-Patch (Non-Middle-Man), where rev B, C, & D have the same patch.

    return MEPA_RC_OK;
}

static mepa_rc vsc8574_phy_reset_lcpll_priv(mepa_device_t *dev)
{
    u16 reg_val = 0;
    u16 chip_port_no = 0;

    // Get the chip physical port number; if it is 0 this is the base port
    MEPA_RC(vsc8574_phy_chip_port(dev, &chip_port_no));
    if (chip_port_no != 0U) {
        return MESA_RC_ERR_PHY_BASE_NO_NOT_FOUND;
    }

    MEPA_RC(vsc8574_phy_page_gpio(dev));                                  // micro/GPIO register-page
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_MICRO_PAGE, 0x8023U));                  // Read LCPLL Config Vector into PRAM
    (void)vsc8574_phy_wait_for_micro_complete(dev);

    MEPA_RC(vsc8574_phy_page_gpio(dev));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_MICRO_PAGE, 0xd7d5U));                  // Set Address to Poke
    (void)vsc8574_phy_wait_for_micro_complete(dev);

    MEPA_RC(vsc8574_phy_page_gpio(dev));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_MICRO_PAGE, 0x8d06U));                  // Reset PLL startup state machine, set disable_fsm:bit 119
    (void)vsc8574_phy_wait_for_micro_complete(dev);

    MEPA_RC(vsc8574_phy_page_gpio(dev));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_MICRO_PAGE, 0x80c0U));                  // Rewrite PLL Config Vector
    MEPA_RC(vsc8574_phy_wait_for_micro_complete(dev));

    MEPA_MSLEEP(10);

    MEPA_RC(vsc8574_phy_page_gpio(dev));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_MICRO_PAGE, 0x8506U));                  // De-assert reset of PLL state machine, clear disable_fsm:bit 119
    MEPA_RC(vsc8574_phy_wait_for_micro_complete(dev));

    MEPA_RC(vsc8574_phy_page_gpio(dev));
    MEPA_RC(PHY_WR_PAGE(dev, VTSS_PHY_MICRO_PAGE, 0x80c0U));                  // Rewrite PLL Config Vector
    MEPA_RC(vsc8574_phy_wait_for_micro_complete(dev));

    MEPA_MSLEEP(10);

    MEPA_RC(vsc8574_phy_page_ext3(dev));
    MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_MAC_SERDES_STATUS, &reg_val));

    MEPA_RC(vsc8574_phy_page_ext3(dev));
    MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_MAC_SERDES_STATUS, &reg_val));

    MEPA_MSLEEP(110); // Allow re-calibration of the LCPLL

    return MEPA_RC_OK;
}

// Read the PHY revision from the identifier registers.
//
// A Tesla part that reports an earlier revision in Reg3 can still be rev E. For
// every Tesla SKU - VSC8574 (vtss_phy.c:1492), VSC8504 (:1508), VSC8572 (:1523)
// and VSC8552 (:1538) - vtss_phy_detect() additionally reads the extended revision
// register 30G and overrides the revision to rev E when
// VTSS_F_PHY_EXTENDED_REVISION_TESLA_E is set.
static mepa_rc vsc8574_phy_revision_get(mepa_device_t *dev)
{
    vsc8574_data_t *data = (vsc8574_data_t *)(dev->data);
    u16                reg3;
    u16                reg30;

    MEPA_RC(vsc8574_phy_page_std(dev));
    MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_IDENTIFIER_2, &reg3));
    data->revision = reg3 & 0xFU;

    MEPA_RC(vsc8574_phy_page_gpio(dev));   // Switch to micro/GPIO register-page
    MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_EXTENDED_REVISION, &reg30));
    if ((reg30 & VTSS_F_PHY_EXTENDED_REVISION_TESLA_E) != 0U) {
        data->revision = VTSS_PHY_TESLA_REV_E;
    }
    MEPA_RC(vsc8574_phy_page_std(dev));    // Switch back to STD register-page

    return MEPA_RC_OK;
}

static mepa_rc vsc8574_phy_pre_init_seq_tesla(mepa_device_t *dev)
{
    vsc8574_data_t *data = (vsc8574_data_t *)(dev->data);
    mepa_rc            rc;

    switch (data->revision) {
    case VTSS_PHY_TESLA_REV_E:
        rc = vsc8574_phy_pre_init_seq_tesla_rev_e(dev);
        break;

    case VTSS_PHY_TESLA_REV_A:
    case VTSS_PHY_TESLA_REV_B:
    case VTSS_PHY_TESLA_REV_D:
        T_E(MEPA_TRACE_GRP_GEN,
            "Tesla rev %d pre-init script not supported by vsc8574", (int32_t)data->revision);
        rc = MEPA_RC_ERROR;
        break;

    default:
        // Note: EEE settings for Tesla Rev. E are significantly different from the
        // previous versions. Default to the latest known script, as the original does.
        T_I(MEPA_TRACE_GRP_GEN,
            "Pre_init script not implemented for rev:%d, DEFAULTING to Rev E", (int32_t)data->revision);
        rc = vsc8574_phy_pre_init_seq_tesla_rev_e(dev);
        break;
    }

    return rc;
}

static mepa_rc vsc8574_phy_pre_reset_priv(mepa_device_t *dev)
{
    MEPA_RC(vsc8574_phy_revision_get(dev));
    MEPA_RC(vsc8574_phy_reset_lcpll_priv(dev));
    MEPA_RC(vsc8574_phy_pre_init_seq_tesla(dev));

    return MEPA_RC_OK;
}

// Port of vtss_phy_post_reset_private(), Tesla case only.
static mepa_rc vsc8574_phy_post_reset_priv(mepa_device_t *dev)
{
    // Apply COMA_MODE default (disable) in POST_RESET
    return vsc8574_phy_coma_mode_priv(dev, TRUE);
}

static mepa_rc vsc8574_reset_phy_priv(mepa_device_t *dev)
{
    vsc8574_data_t *data = (vsc8574_data_t *)(dev->data);
    u16                reg;
    BOOL               force_reset = TRUE;

    // -- Step 1: If the link is up we have to remember that it went down due to
    //            the port reset --
    MEPA_RC(vsc8574_phy_page_std(dev));
    MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_MODE_STATUS, &reg));
    data->link_down_due_to_port_reset = ((reg & VTSS_F_PHY_STATUS_LINK_STATUS) != 0U);

    // -- Step 2: Pre-reset setup of MAC and media interface --
    MEPA_RC(vsc8574_phy_mac_media_if_tesla_setup(dev, &force_reset));

    // -- Step 3: Reset PHY --
    MEPA_RC(vsc8574_phy_page_std(dev));
    if (force_reset) {
        // Clear this flag if we are resetting the PHY MAC/media i/f
        data->cu_sfp_config_complete = FALSE;
        MEPA_RC(vsc8574_port_reset(dev));
    } else {
        data->link_down_due_to_port_reset = FALSE;
    }

    return MEPA_RC_OK;
}

// The temperature sensor sits in the GPIO page, which is shared by all four
// PHYs of the package, so this configures the sensor for the whole chip even
// though the one-shot flag is kept per port.
static mepa_rc vsc8574_chip_temp_init(mepa_device_t *dev)
{
    MEPA_RC(vsc8574_phy_page_gpio(dev));

    // 28G.15:12 = 0
    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_TEMP_VAL, 0x0U, 0xf000U));
    // Deassert TMON0, reset and enable background monitoring
    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_TEMP_CONF, 0x0080U, 0x0080U));
    // Disable TMON1
    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_TEMP_CONF, 0x00C0U, 0x00C0U));

    return vsc8574_phy_page_std(dev);
}

// Trigger a conversion and read the raw ADC value.
static mepa_rc vsc8574_temp_read(mepa_device_t *dev, u8 *temp_reading)
{
    u16 reg;

    MEPA_RC(vsc8574_phy_page_gpio(dev));

    // Workaround because background temperature monitoring does not work: pulse
    // 26G.6 to start a new conversion
    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_TEMP_CONF, 0x0U, 0x0040U));
    MEPA_RC(PHY_WR_MASKED_PAGE(dev, VTSS_PHY_TEMP_CONF, 0x0040U, 0x0040U));

    MEPA_RC(PHY_RD_PAGE(dev, VTSS_PHY_TEMP_VAL, &reg));
    *temp_reading = (u8)(reg & 0xffU); // adc_val, only the bottom 8 bits

    return vsc8574_phy_page_std(dev);
}

// Public API bellow

static mepa_device_t *vsc8574_probe(mepa_driver_t *drv,
                                    const mepa_callout_t    MEPA_SHARED_PTR *callout,
                                    struct mepa_callout_ctx MEPA_SHARED_PTR *callout_ctx,
                                    struct mepa_board_conf              *board_conf)
{
    mepa_device_t *dev;
    vsc8574_data_t *data;

    dev = mepa_create_int(drv, callout, callout_ctx, board_conf, (int)sizeof(vsc8574_data_t));
    if (dev == NULL) {
        return NULL;
    }

    data = dev->data;
    data->port_no = board_conf->numeric_handle;

    return dev;
}

static mepa_rc vsc8574_delete(mepa_device_t *dev)
{
    return mepa_delete_int(dev);
}

static mepa_rc vsc8574_if_set(mepa_device_t *dev,
                              mepa_port_interface_t mac_if)
{
    vsc8574_data_t *data = (vsc8574_data_t *)dev->data;

    data->mac_if = mac_if;
    return MEPA_RC_OK;
}

static mepa_rc vsc8574_link_base_port(mepa_device_t *dev,
                                      mepa_device_t *base_dev,
                                      uint8_t packet_idx)
{
    vsc8574_data_t *base_data = (vsc8574_data_t *)(base_dev->data);
    vsc8574_data_t *data = (vsc8574_data_t *)(dev->data);

    data->base_dev = base_dev;
    base_data->base_dev = base_dev;

    return MEPA_RC_OK;
}

static mepa_rc vsc8574_reset(mepa_device_t *dev,
                             const mepa_reset_param_t *rst_conf)
{
    vsc8574_data_t *data = (vsc8574_data_t *)(dev->data);
    mepa_rc            rc = MEPA_RC_OK;

    // The temperature sensor configuration does not survive a PHY reset, so
    // force vsc8574_chip_temp_get() to initialise it again
    data->temp_init_flag = FALSE;

    switch (rst_conf->reset_point) {
    case MEPA_RESET_POINT_DEFAULT:
        if (dev == data->base_dev) {
            MEPA_RC(vsc8574_phy_pre_reset_priv(dev));
        }
        data->force = VSC8574_FORCE_RESET;
        data->media_if = (vtss_phy_media_interface_t)rst_conf->media_intf;
        MEPA_RC(vsc8574_reset_phy_priv(dev));

        if (dev == data->base_dev) {
            MEPA_RC(vsc8574_phy_post_reset_priv(dev));
        }
        break;
    default:
        // No action needed for the remaining reset points.
        break;
    }

    return rc;
}

static mepa_rc vsc8574_conf_set(mepa_device_t *dev,
                                const mepa_conf_t *config)
{
    vsc8574_data_t *data = (vsc8574_data_t *)dev->data;
    vtss_phy_conf_t phy_config = {};
    vtss_phy_conf_1g_t cfg_neg = {};
    mepa_rc rc = MEPA_RC_OK;

    phy_config = data->conf;

    if (config->admin.enable) {
        if (config->speed == MESA_SPEED_AUTO ||
                config->speed == MESA_SPEED_1G) {
            phy_config.mode = VTSS_PHY_MODE_ANEG;
        } else {
            phy_config.mode = VTSS_PHY_MODE_FORCED;
        }
    } else {
        phy_config.mode = VTSS_PHY_MODE_POWER_DOWN;
    }

    phy_config.aneg.speed_2g5_fdx = config->aneg.speed_2g5_fdx;
    phy_config.aneg.speed_5g_fdx = config->aneg.speed_5g_fdx;
    phy_config.aneg.speed_10g_fdx = config->aneg.speed_10g_fdx;
    phy_config.aneg.speed_10m_hdx = config->aneg.speed_10m_hdx;
    phy_config.aneg.speed_10m_fdx = config->aneg.speed_10m_fdx;
    phy_config.aneg.speed_100m_hdx = config->aneg.speed_100m_hdx;
    phy_config.aneg.speed_100m_fdx = config->aneg.speed_100m_fdx;
    phy_config.aneg.speed_1g_fdx = config->aneg.speed_1g_fdx;
    phy_config.aneg.no_restart_aneg = config->aneg.no_restart_aneg;

    // Translate MDI mode
    phy_config.mdi = VTSS_PHY_MDIX_AUTO;
    if (config->mdi_mode == MEPA_MEDIA_MODE_MDI) {
        phy_config.mdi = VTSS_PHY_MDI;
    } else if (config->mdi_mode == MEPA_MEDIA_MODE_MDIX) {
        phy_config.mdi = VTSS_PHY_MDIX;
    } else {
        // Auto: keep the VTSS_PHY_MDIX_AUTO default set above
    }
    // We don't support 1G half duplex
    phy_config.aneg.speed_1g_hdx = false;
    phy_config.aneg.symmetric_pause = config->flow_control;
    phy_config.aneg.asymmetric_pause = config->flow_control;

    // manual negotiation
    if (config->man_neg != MEPA_MANUAL_NEG_DISABLED) {
        cfg_neg.master.cfg = true;
        cfg_neg.master.val = (config->man_neg == MEPA_MANUAL_NEG_REF);
    } else {
        cfg_neg.master.cfg = false;
        cfg_neg.master.val = false;
    }

    // Force AMS Media Select MEPA:104
    if (config->force_ams_mode_sel != MEPA_PHY_MEDIA_FORCE_AMS_SEL_NORMAL) {
        phy_config.force_ams_sel = (vtss_phy_media_force_ams_sel_t)(config->force_ams_mode_sel == MEPA_PHY_MEDIA_FORCE_AMS_SEL_SERDES ?
                                   MEPA_PHY_MEDIA_FORCE_AMS_SEL_SERDES : MEPA_PHY_MEDIA_FORCE_AMS_SEL_COPPER);
    }
    else {
        phy_config.force_ams_sel = (vtss_phy_media_force_ams_sel_t)MEPA_PHY_MEDIA_FORCE_AMS_SEL_NORMAL;
    }

    rc = vsc8574_phy_conf_1g_set_priv(dev, &cfg_neg);
    if (rc != MEPA_RC_OK) {
        T_E(MEPA_TRACE_GRP_GEN, "Failed to confiured speed\n");
        return MEPA_RC_ERROR;
    }
    phy_config.forced.speed = (vsc8574_port_speed_t)config->speed;
    phy_config.forced.fdx = config->fdx;

    if (phy_config.mac_if_pcs.serdes_aneg_ena != config->mac_if_aneg_ena) {
        phy_config.mac_if_pcs.aneg_restart = true;
    }
    phy_config.mac_if_pcs.serdes_aneg_ena = config->mac_if_aneg_ena;

    return vsc8574_phy_conf_set_priv(dev, &phy_config);
}

static mepa_rc vsc8574_conf_get(mepa_device_t *dev,
                                mepa_conf_t *const conf)
{
    vsc8574_data_t *data = (vsc8574_data_t *)dev->data;
    // Both caches are maintained by vsc8574_phy_conf_set_priv() and
    // vsc8574_phy_conf_1g_set_priv(), so nothing needs to be read from the
    // PHY here.
    const vtss_phy_conf_t    *phy_conf = &data->conf;
    const vtss_phy_conf_1g_t *cfg_neg = &data->conf_1g;

    *conf = (const mepa_conf_t) {};

    conf->flow_control = phy_conf->aneg.symmetric_pause;
    conf->aneg.speed_2g5_fdx = phy_conf->aneg.speed_2g5_fdx;
    conf->aneg.speed_5g_fdx = phy_conf->aneg.speed_5g_fdx;
    conf->aneg.speed_10g_fdx = phy_conf->aneg.speed_10g_fdx;
    conf->aneg.speed_10m_hdx = phy_conf->aneg.speed_10m_hdx;
    conf->aneg.speed_10m_fdx = phy_conf->aneg.speed_10m_fdx;
    conf->aneg.speed_100m_hdx = phy_conf->aneg.speed_100m_hdx;
    conf->aneg.speed_100m_fdx = phy_conf->aneg.speed_100m_fdx;
    conf->aneg.speed_1g_fdx = phy_conf->aneg.speed_1g_fdx;
    conf->aneg.no_restart_aneg = phy_conf->aneg.no_restart_aneg;

    // Translate MDI mode
    conf->mdi_mode = MEPA_MEDIA_MODE_AUTO;
    if (phy_conf->mdi == VTSS_PHY_MDI) {
        conf->mdi_mode = MEPA_MEDIA_MODE_MDI;
    } else if (phy_conf->mdi == VTSS_PHY_MDIX) {
        conf->mdi_mode = MEPA_MEDIA_MODE_MDIX;
    } else {
        // Auto: keep the MEPA_MEDIA_MODE_AUTO default set above
    }

    // Force AMS Media Select MEPA:104
    conf->force_ams_mode_sel = (phy_conf->force_ams_sel == VTSS_PHY_MEDIA_FORCE_AMS_SEL_NORMAL) ? MEPA_PHY_MEDIA_FORCE_AMS_SEL_NORMAL :
                               phy_conf->force_ams_sel == VTSS_PHY_MEDIA_FORCE_AMS_SEL_SERDES ?
                               MEPA_PHY_MEDIA_FORCE_AMS_SEL_SERDES : MEPA_PHY_MEDIA_FORCE_AMS_SEL_COPPER;

    if (phy_conf->mode == VTSS_PHY_MODE_ANEG) {
        conf->speed = MESA_SPEED_AUTO;

        // Get manual negotiation options
        conf->man_neg = !cfg_neg->master.cfg ? MEPA_MANUAL_NEG_DISABLED :
                        cfg_neg->master.val ? MEPA_MANUAL_NEG_REF : MEPA_MANUAL_NEG_CLIENT;
    } else if (phy_conf->mode == VTSS_PHY_MODE_FORCED) {
        conf->speed = (mepa_port_speed_t)phy_conf->forced.speed;
    } else {
        // Power down: the speed reported by the caller is left untouched
    }
    conf->fdx = phy_conf->forced.fdx;
    conf->mac_if_aneg_ena = phy_conf->mac_if_pcs.serdes_aneg_ena;
    conf->admin.enable = phy_conf->mode != VTSS_PHY_MODE_POWER_DOWN ? true : false;

    return MEPA_RC_OK;
}

static mepa_rc vsc8574_poll(mepa_device_t *dev,
                            mepa_status_t *status)
{
    vsc8574_port_status_t mesa_status = {};

    mepa_rc rc = vsc8574_phy_status_get_priv(dev, &mesa_status);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    // Fill up status
    status->link = mesa_status.link;
    status->speed = (mepa_port_speed_t)mesa_status.speed;
    status->fdx = mesa_status.fdx;
    status->aneg.obey_pause = mesa_status.aneg.obey_pause;
    status->aneg.generate_pause = mesa_status.aneg.generate_pause;
    status->copper = mesa_status.copper;
    status->fiber = mesa_status.fiber;

    return MEPA_RC_OK;
}

static mepa_rc vsc8574_chip_temp_get(mepa_device_t *dev, i16 *const temp)
{
    vsc8574_data_t *data = (vsc8574_data_t *)dev->data;
    u8 temp_reading = 0;

    if (temp == NULL) {
        return MEPA_RC_ERROR;
    }

    if (!data->temp_init_flag) {
        MEPA_RC(vsc8574_chip_temp_init(dev));
        data->temp_init_flag = TRUE;
    }

    MEPA_RC(vsc8574_temp_read(dev, &temp_reading));

    // 135.3 degC - 0.71 degC * ADCOUT, see the datasheet section on register 28G
    *temp = (i16)((13530 - (71 * (int)temp_reading)) / 100);

    return MEPA_RC_OK;
}

mepa_drivers_t mepa_vsc8574_driver_init(void)
{
    static const int nr_vsc8574_drivers = 1;
    static mepa_driver_t vsc8574_drivers[] = {
        {
            .id = 0x000704a0U,
            .mask = 0xfffffff0U,
            .mepa_driver_probe = vsc8574_probe,
            .mepa_driver_delete = vsc8574_delete,
            .mepa_driver_if_set = vsc8574_if_set,
            .mepa_driver_link_base_port = vsc8574_link_base_port,
            .mepa_driver_reset = vsc8574_reset,
            .mepa_driver_conf_set = vsc8574_conf_set,
            .mepa_driver_conf_get = vsc8574_conf_get,
            .mepa_driver_poll = vsc8574_poll,
            .mepa_driver_chip_temp_get = vsc8574_chip_temp_get,
        },
    };

    mepa_drivers_t result;
    result.phy_drv = vsc8574_drivers;
    result.count = nr_vsc8574_drivers;

    return result;
}
