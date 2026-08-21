// Copyright (c) 2004-2026 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#include "mdio.h"
#include "as2xxxx_registers.h" // For the vendor specific MMD device number
#include "mepa_driver.h"
#include "microchip/ethernet/phy/api/types.h"

static inline mepa_bool_t is_device_valid(mepa_device_t *const dev)
{
    return (dev != NULL && dev->callout != NULL);
}

mepa_rc mdio_rd(mepa_device_t *const dev, const uint16_t reg_addr, uint16_t *const value)
{
    if (!is_device_valid(dev) || value == NULL) {
        return MEPA_RC_ERR_PARM;
    }

    if (!dev->callout->miim_read) {
        return MEPA_RC_NOT_IMPLEMENTED;
    }

    *value = 0;
    return dev->callout->miim_read(dev->callout_ctx, reg_addr, value);
}

mepa_rc mdio_wr(mepa_device_t *const dev, const uint16_t reg_addr, uint16_t const value)
{
    if (!is_device_valid(dev)) {
        return MEPA_RC_ERR_PARM;
    }

    if (!dev->callout->miim_write) {
        return MEPA_RC_NOT_IMPLEMENTED;
    }

    return dev->callout->miim_write(dev->callout_ctx, reg_addr, value);
}

mepa_rc mdio_mmd_rd(mepa_device_t *const dev,
                    const uint16_t       mmd,
                    const uint16_t       reg_addr,
                    uint16_t *const      value)
{
    if (!is_device_valid(dev) || value == NULL) {
        return MEPA_RC_ERR_PARM;
    }

    if (!dev->callout->mmd_read) {
        return MEPA_RC_NOT_IMPLEMENTED;
    }

    *value = 0;

    return dev->callout->mmd_read(dev->callout_ctx, mmd, reg_addr, value);
}

mepa_rc mdio_mmd_vendor_rd(mepa_device_t *const dev, const uint16_t reg_addr, uint16_t *const value)
{
    return mdio_mmd_rd(dev, AS2XXXX_DEV_VENDOR, reg_addr, value);
}

mepa_rc mdio_mmd_wr(mepa_device_t *const dev,
                    const uint16_t       mmd,
                    const uint16_t       reg_addr,
                    uint16_t const       value)
{
    if (!is_device_valid(dev)) {
        return MEPA_RC_ERR_PARM;
    }

    if (!dev->callout->mmd_write) {
        return MEPA_RC_NOT_IMPLEMENTED;
    }

    return dev->callout->mmd_write(dev->callout_ctx, mmd, reg_addr, value);
}

mepa_rc mdio_mmd_vendor_wr(mepa_device_t *const dev, const uint16_t reg_addr, uint16_t const value)
{
    return mdio_mmd_wr(dev, AS2XXXX_DEV_VENDOR, reg_addr, value);
}

mepa_rc mdio_mmd_modify(mepa_device_t *const dev,
                        const uint16_t       mmd,
                        uint16_t const       reg_addr,
                        uint16_t const       mask,
                        uint16_t const       value)
{
    mepa_rc  rc;
    uint16_t read = 0;
    uint16_t write = 0;

    rc = mdio_mmd_rd(dev, mmd, reg_addr, &read);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    write = (read & ~mask) | value;
    return mdio_mmd_wr(dev, mmd, reg_addr, write);
}

mepa_rc mdio_mmd_vendor_modify(mepa_device_t *const dev,
                               uint16_t const       reg_addr,
                               uint16_t const       mask,
                               uint16_t const       value)
{
    return mdio_mmd_modify(dev, AS2XXXX_DEV_VENDOR, reg_addr, mask, value);
}