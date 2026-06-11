// Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#ifndef MEPA_LAN884x_PRIVATE_H
#define MEPA_LAN884x_PRIVATE_H

#include <stdint.h>
#include <microchip/ethernet/phy/api/types.h>
#include <microchip/ethernet/phy/api/phy_ts.h>

#define TRUE  1U
#define FALSE 0U
#define EXT_PAGE 1 // extended page access
#define MMD_DEV  2 // MMD device access

#define PFE_MMD_COMMON_CTRL_REG 2
#define PFE_RXC_DLL_CTRL        76
#define PFE_TXC_DLL_CTRL        77
#define PFE_DLL_ENABLE_DELAY    0

#define LAN8841_MMD_ANALOG_REG    28
#define LAN8841_ANALOG_CONTROL_11 14
#define LAN8841_ANALOG_CONTROL_11_LDO_REF(x) (((uint16_t)(x) & (uint16_t)0x7U) << 12U)
#define LAN8841_ANALOG_CONTROL_11_LDO_MASK   0x7000U

typedef enum {
    PHY_INTERFACE_MODE_RGMII,      // Reduced gigabit media-independent interface
    PHY_INTERFACE_MODE_RGMII_ID,   // RGMII with Internal RX+TX delay
    PHY_INTERFACE_MODE_RGMII_RXID, // RGMII with Internal RX delay
    PHY_INTERFACE_MODE_RGMII_TXID, // RGMII with Internal RX delay
} phy_interface_t;

#define DISABLE_DLL_TX_BIT LAN8814_BIT(14)
#define DISABLE_DLL_RX_BIT LAN8814_BIT(14)
#define DISABLE_DLL_MASK LAN8814_BIT(14)

#define LAN8840_OPERATION_MODE_STRAP_LOW_REGISTER 3
#define LAN8840_OPERATION_MODE_STRAP_LOW_REGISTER_STRAP_RGMII_EN 1U

typedef struct {
    uint8_t  model;
    uint8_t  rev;
} lan884x_dev_info_t;

typedef struct {
    mepa_port_no_t           port_no;
    mepa_conf_t              conf;
    mepa_event_t             events;
    mepa_loopback_t          loopback;
    lan884x_dev_info_t       dev;
    mepa_bool_t              link_status;
    mepa_port_speed_t        speed_status;
    mepa_bool_t              fdx_status;
    mesa_port_interface_t    mac_if;
} lan884x_data_t;

#endif
