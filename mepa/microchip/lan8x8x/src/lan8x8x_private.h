// Copyright (c) 2004-2024 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#ifndef LAN8X8X_PRIVATE_H
#define LAN8X8X_PRIVATE_H

#include <phy_lib.h>
#include "lan8x8x_registers.h"

static inline void LAN8X8X_NSLEEP(uint32_t ns)
{
    MEPA_NSLEEP((ns));
}

#ifdef MEPA_lan8x8x_static_mem
#ifdef MAX_LAN8X8X_PHY
#define LAN8X8X_PHY_MAX                        MAX_LAN8X8X_PHY
#else
#define LAN8X8X_PHY_MAX                        (MEPA_lan8x8x_phy_max)
#endif
#else // MEPA_lan8x8x_static_mem
#ifdef MAX_LAN8X8X_PHY
#define LAN8X8X_PHY_MAX                        MAX_LAN8X8X_PHY
#else
#define LAN8X8X_PHY_MAX                        (1U)
#endif
#endif //MEPA_lan8x8x_static_mem

#define PHY_ID_LAN878X          (0x002216A0U)
#define PHY_ID_LAN888X          (0x002216B0U)
#define PHY_ID_MASK     (0xFFFFFFF0U)
static inline uint8_t IS_LAN888X(uint32_t id)
{
    return ((((id) & PHY_ID_MASK) == PHY_ID_LAN888X) ? 1U : 0U);
}
static inline uint8_t IS_LAN878X(uint32_t id)
{
    return ((((id) & PHY_ID_MASK) == PHY_ID_LAN878X) ? 1U : 0U);
}

#define PHY_LINKUP                  (PHY_TRUE)
#define PHY_LINKDOWN                (PHY_FALSE)

//Retrieve PHY_ID
static inline uint32_t GET_PHY_ID1(uint32_t x)
{
    return (((x) << 2U) & 0x03FFFCU);
}
static inline uint32_t GET_PHY_ID2(uint32_t x)
{
    return ((EXTRACT_BITS(x, 10U, 6U) << 18U) & 0xFFFFFFFFU);
}
static inline uint32_t GET_PHY_REV(uint32_t x)
{
    return EXTRACT_BITS(x, 0U, 4U);
}
static inline uint32_t GET_PHY_MODEL(uint32_t x)
{
    return EXTRACT_BITS(x, 4U, 6U);
}

/*
 * Data structures
 */
typedef struct {
    mepa_bool_t dis_macsec;
    mepa_bool_t dis_1588;
    mepa_bool_t dis_1000;
    mepa_bool_t dis_100;
    mepa_bool_t is_sgmii;
} lan8x8x_otp_cap_t;

typedef struct {
    uint32_t id;
    uint16_t  part_id;
    uint16_t  model;
    uint16_t  rev;
    mepa_bool_t is_master;
    mepa_bool_t is_master_fault;
} phy_dev_info_t;

typedef struct {
    lan8x8x_otp_cap_t       t1_cap;
    mepa_bool_t             init_done;
    mepa_bool_t             link_status;
    mepa_port_no_t          port_no;
    mepa_port_interface_t   mac_if;
    mepa_media_interface_t  media_intf;
    mepa_conf_t             conf;
    mepa_event_t            events;
    mepa_loopback_t         loopback;
    phy_dev_info_t          dev;
    mepa_bool_t             ctx_status;
    mepa_cable_diag_result_t cd_res;
    mepa_gpio_conf_t        led_conf[4];
} phy_data_t;

#endif //LAN8X8X_PRIVATE_H
