// Copyright (c) 2004-2025 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#ifndef LAN867X_PRIVATE_H
#define LAN867X_PRIVATE_H

#include <stdbool.h>

#include <mepa_trace.h>

#define LAN867X_PHY_ID      0x0007c160U
#define LAN867X_PHY_ID_MASK 0xFFFFFFF0U

#define LAN867X_PHY_MMD_DEVAD(addr)     (((addr) >> 16U) & 0x001FU)
#define LAN867X_PHY_REG_ADDR(addr)      ((addr) & 0xFFFFU)

#ifdef MEPA_LAN867X_PHY_MAX
#define MAX_LAN867X_PHY MEPA_LAN867X_PHY_MAX
#else
#define MAX_LAN867X_PHY 1U
#endif

typedef struct {
    uint32_t id;
    uint8_t  model;
    uint8_t  rev;
} phy_dev_info_t;

typedef struct phy_t1s_config_t {
    mepa_t1s_plca_cfg_t plca_cfg;
} phy_t1s_cfg_t;

typedef struct {
    mepa_bool_t             init_done;
    mepa_bool_t             phy_aneg_dis;
    mepa_bool_t             link_status;
    mepa_port_no_t          port_no;
    mepa_port_interface_t   mac_if;
    mepa_media_interface_t  media_intf;
    mepa_conf_t             conf;
    mepa_event_t            events;
    mepa_loopback_t         loopback;
    phy_dev_info_t          dev;
    mepa_bool_t             ctx_status;
    mepa_bool_t             cdiags_start;
    phy_t1s_cfg_t           t1s_cfg;
} phy_data_t;

extern mepa_t1s_driver_t lan867x_t1s_driver;

mepa_rc lan867x_miim_reg_rd(mepa_device_t *const dev, uint16_t const offset, uint16_t *const value);
mepa_rc lan867x_miim_reg_wr(mepa_device_t *const dev, uint16_t const offset, uint16_t const value);
mepa_rc lan867x_mmd_reg_rd(mepa_device_t *const dev, uint8_t const devaddr, uint16_t const offset, uint16_t *const value);
mepa_rc lan867x_mmd_reg_wr(mepa_device_t *const dev, uint8_t const devaddr, uint16_t const offset, uint16_t const value);
mepa_rc lan867x_init_conf(mepa_device_t *const dev, const mepa_t1s_plca_cfg_t cfg);
mepa_rc lan867x_phy_conf_set(mepa_device_t *dev, const mepa_conf_t *config);
mepa_rc lan867x_get_link_status(mepa_device_t *const dev, mepa_status_t *const status);

#endif //LAN867X_PRIVATE_H
