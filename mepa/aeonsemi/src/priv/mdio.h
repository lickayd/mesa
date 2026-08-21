// Copyright (c) 2004-2026 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#ifndef AS2XXXX_MDIO_H_
#define AS2XXXX_MDIO_H_

#include "microchip/ethernet/phy/api/types.h"

/**
 * @brief Read MDIO Clause-22 register
 * @param[in] dev The device.
 * @param[in] reg_addr The address of the register.
 * @param[out] value A pointer to the value.
 * @return mepa_rc Return code.
 */
mepa_rc mdio_rd(mepa_device_t *const dev, const uint16_t reg_addr, uint16_t *const value);

/**
 * @brief Write MDIO Clause-22 register
 * @param[in] dev The device.
 * @param[in] reg_addr The address of the register.
 * @param[in] value The value to be written.
 * @return mepa_rc Return code.
 */
mepa_rc mdio_wr(mepa_device_t *const dev, const uint16_t reg_addr, uint16_t const value);

/**
 * @brief Read a MDIO Clause-45 register
 * @param[in] dev The device.
 * @param[in] mmd MDIO Manageable Device (MMD) address.
 * @param[in] reg_addr The address of the register.
 * @param[out] value A pointer to the value.
 * @return mepa_rc Return code.
 */
mepa_rc mdio_mmd_rd(mepa_device_t *const dev,
                    const uint16_t       mmd,
                    const uint16_t       reg_addr,
                    uint16_t *const      value);

/**
 * @brief Write a MDIO Clause-45 register
 * @param[in] dev The device.
 * @param[in] mmd MDIO Manageable Device (MMD) address.
 * @param[in] reg_addr The address of the register.
 * @param[in] value The value to be written.
 * @return mepa_rc Return code.
 */
mepa_rc mdio_mmd_wr(mepa_device_t *const dev,
                    const uint16_t       mmd,
                    const uint16_t       reg_addr,
                    uint16_t const       value);

/**
 * @brief Read a MDIO Clause-45 register of the vendor specific MMD device address.
 * @param[in] dev The device.
 * @param[in] reg_addr The address of value register.
 * @param[out] value A pointer to the value.
 * @return mepa_rc Return code.
 */
mepa_rc mdio_mmd_vendor_rd(mepa_device_t *const dev,
                           const uint16_t       reg_addr,
                           uint16_t *const      value);

/**
 * @brief Write a MDIO Clause-45 register of the vendor specific MMD device address.
 * @param[in] dev The device.
 * @param[in] reg_addr The address of the register.
 * @param[in] value The value to be written.
 * @return mepa_rc Return code.
 */
mepa_rc mdio_mmd_vendor_wr(mepa_device_t *const dev, const uint16_t reg_addr, uint16_t const value);

/**
 * @brief Modify a MDIO Clause-45 register.
 * @param[in] dev The device.
 * @param[in] mmd MDIO Manageable Device (MMD) address.
 * @param[in] reg_addr The address of the register.
 * @param[in] mask The bits to be cleared.
 * @param[in] value The value to be written.
 * @return mepa_rc Return code.
 */
mepa_rc mdio_mmd_modify(mepa_device_t *const dev,
                        const uint16_t       mmd,
                        uint16_t const       reg_addr,
                        uint16_t const       mask,
                        uint16_t const       value);

/**
 * @brief Modify a MDIO Clause-45 register of the vendor specific MMD device address.
 * @param[in] dev The device.
 * @param[in] reg_addr The address of the register.
 * @param[in] mask The bits to be cleared.
 * @param[in] value The value to be written.
 * @return mepa_rc Return code.
 */
mepa_rc mdio_mmd_vendor_modify(mepa_device_t *const dev,
                               uint16_t const       reg_addr,
                               uint16_t const       mask,
                               uint16_t const       value);

#endif /* AS2XXXX_MDIO_H_ */
