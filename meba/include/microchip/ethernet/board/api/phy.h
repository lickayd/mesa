// Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT


#ifndef _MICROCHIP_ETHERNET_BOARD_PHY_DRIVER_H_
#define _MICROCHIP_ETHERNET_BOARD_PHY_DRIVER_H_

#include <microchip/ethernet/board/api/types.h>
#include <microchip/ethernet/phy/api/types.h>

#include <microchip/ethernet/hdr_start.h>

// Reset the PHY hardware
mepa_rc meba_phy_reset(meba_inst_t inst, mepa_port_no_t port_no,
                       const mepa_reset_param_t *rst_conf);

// Get the current status of the PHY.
mepa_rc meba_phy_status_poll(meba_inst_t inst, mepa_port_no_t port_no,
                             mepa_driver_status_t *status);

// Set the configuration to the PHY.
mepa_rc meba_phy_conf_set(meba_inst_t inst, mepa_port_no_t port_no,
                          const mepa_driver_conf_t *conf);

// Get the current PHY configuration.
mepa_rc meba_phy_conf_get(meba_inst_t inst, mepa_port_no_t port_no, mepa_driver_conf_t *const  conf);

// Get the PHY interface based on speed.
mepa_rc meba_phy_if_get(meba_inst_t inst, mepa_port_no_t port_no,
                        mepa_port_speed_t speed, mepa_port_interface_t *intf);

// Sets the power mode in PHY.
mepa_rc meba_phy_power_set(meba_inst_t inst, mepa_port_no_t port_no,
                           mepa_power_mode_t power);

// Start cable diagnostics on PHY port.
mepa_rc meba_phy_cable_diag_start(meba_inst_t inst, mepa_port_no_t port_no, int mode);

// Get the result of cable diagnostics.
mepa_rc meba_phy_cable_diag_get(meba_inst_t inst, mepa_port_no_t port_no, mepa_cable_diag_result_t *result);

// Sets the media type in case the port is a dual media port with external phy.
mepa_rc meba_phy_media_set(meba_inst_t inst, mepa_port_no_t port_no, mepa_media_interface_t phy_media_if);

// Gets Copper 1G PHY auto negotiation status.
mepa_rc meba_phy_aneg_status_get(meba_inst_t inst, mepa_port_no_t port_no, mepa_aneg_status_t *status);

// Read registers using clause22 format for debugging.
// address format : bits 0 - 4 : address within page i.e. 0 - 31 addresses.
//                       5 - 31: page number.
mepa_rc meba_phy_clause22_read(meba_inst_t inst, mepa_port_no_t port_no, uint32_t address, uint16_t *const value);

// Write registers using clause22 format for debugging.
// address format : bits 0 - 4 : address within page i.e. 0 - 31 addresses.
//                       5 - 31: page number.
mepa_rc meba_phy_clause22_write(meba_inst_t inst, mepa_port_no_t port_no, uint32_t address, uint16_t value);

// Read registers using clause45 format for debugging.
// address format : bits 0  - 15 : address within a extended page or mmd device.
//                       16 - 26 : extended page number
//                       27 - 31 : mmd device number, '0' for extended page.
mepa_rc meba_phy_clause45_read(meba_inst_t inst, mepa_port_no_t port_no, uint32_t address, uint16_t *const value);

// Write registers using clause45 format for debugging.
// address format : bits 0  - 15 : address within a extended page or mmd device.
//                       16 - 26 : extended page number
//                       27 - 31 : mmd device number, '0' for extended page.
mepa_rc meba_phy_clause45_write(meba_inst_t inst, mepa_port_no_t port_no, uint32_t address, uint16_t value);

// Enable the event in phy.
mepa_rc meba_phy_event_enable_set(meba_inst_t inst, mepa_port_no_t port_no, mepa_event_t event, mesa_bool_t enable);

// Get the events enabled currently in phy.
mepa_rc meba_phy_event_enable_get(meba_inst_t inst, mepa_port_no_t port_no, mepa_event_t *const event);

// Get the current status of events enabled in phy.
mepa_rc meba_phy_event_poll(meba_inst_t inst, mepa_port_no_t port_no, mepa_event_t *const event);

// Enable or disable the loopback in phy. Used for debugging purpose.
mepa_rc meba_phy_loopback_set(meba_inst_t inst, mepa_port_no_t port_no, const mepa_loopback_t *loopback);

// Get current loopback configuration.
mepa_rc meba_phy_loopback_get(meba_inst_t inst, mepa_port_no_t port_no, mepa_loopback_t *const loopback);

// Set the phy gpio mode.
mepa_rc meba_phy_gpio_mode_set(meba_inst_t inst, mepa_port_no_t port_no, const mepa_gpio_conf_t *gpio_conf);

// Set the phy gpio pin data value.
mepa_rc meba_phy_gpio_out_set(meba_inst_t inst, mepa_port_no_t port_no, uint8_t gpio_no, mepa_bool_t enable);

// Get the phy gpio pin data value.
mepa_rc meba_phy_gpio_in_get(meba_inst_t inst, mepa_port_no_t port_no, uint8_t gpio_no, mepa_bool_t *const enable);

// Set the phy recovered clock configuration
mepa_rc meba_phy_synce_clock_conf_set(meba_inst_t inst, mepa_port_no_t port_no, const mepa_synce_clock_conf_t *conf);

// Get the phy info
mepa_rc meba_phy_info_get(meba_inst_t inst, mepa_port_no_t port_no, mepa_phy_info_t *const phy_info);
#include <microchip/ethernet/hdr_end.h>
#endif //_MICROCHIP_ETHERNET_BOARD_PHY_DRIVER_H
