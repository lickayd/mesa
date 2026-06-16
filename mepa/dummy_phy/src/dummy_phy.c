// Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#include <microchip/ethernet/phy/api.h>
#include <mepa_driver.h>
#include <stdbool.h>

#define MEBA_PORT_CAP_10M_HDX           0x000000002U
#define MEBA_PORT_CAP_10M_FDX           0x000000004U
#define MEBA_PORT_CAP_100M_HDX          0x000000008U
#define MEBA_PORT_CAP_100M_FDX          0x000000010U
#define MEBA_PORT_CAP_1G_FDX            0x000000020U
#define MEBA_PORT_CAP_2_5G_FDX          0x000000040U

typedef struct {
    u32 cap;
} priv_data_t;

static mepa_rc dummy_1g_poll(mepa_device_t *dev,
                            mepa_status_t *status)
{
    priv_data_t *priv;
    priv = dev->data;

    status->link = true;
    status->speed =
        ((priv->cap & MEBA_PORT_CAP_2_5G_FDX) != 0U) ? MESA_SPEED_2500M :
        ((priv->cap & MEBA_PORT_CAP_1G_FDX) != 0U)   ? MESA_SPEED_1G :
        ((priv->cap & MEBA_PORT_CAP_100M_FDX) != 0U) ? MESA_SPEED_100M :
                                                       MESA_SPEED_10M;
    status->fdx = true;

    return MEPA_RC_OK;
}

static mepa_rc dummy_conf_set(mepa_device_t *dev, const mepa_conf_t *config)
{
    return MEPA_RC_OK;
}

static uint32_t dummy_capability(mepa_device_t *dev, uint32_t capability)
{
    uint32_t c;

    if (capability == (uint32_t)MEPA_CAP_SPEED_10G) {
        c = 1U;
    } else {
        c = 0U;
    }

    return c;
}

static mepa_rc dummy_info_get(mepa_device_t *dev, mepa_phy_info_t *const phy_info)
{
    uint32_t cap_value = 0U;

    phy_info->part_number = 1234;
    phy_info->revision = 5678;
    if (dummy_capability(dev, (uint32_t)MEPA_CAP_SPEED_10G) != 0U) {
        cap_value |= (uint32_t)MEPA_CAP_SPEED_MASK_10G;
    }
    phy_info->cap = (mepa_phy_cap_t)cap_value;
    return MEPA_RC_OK;
}

static mepa_rc dummy_conf_get(mepa_device_t *dev, mepa_conf_t *const config)
{
    return MEPA_RC_OK;
}

static mepa_rc dummy_reset(mepa_device_t *dev, const mepa_reset_param_t *rst_conf)
{
    return MEPA_RC_OK;
}

static mepa_rc dummy_if_set(mepa_device_t *dev, mepa_port_interface_t mac_if)
{
    return MEPA_RC_OK;
}

static mepa_device_t *dummy_probe(mepa_driver_t                       *drv,
                                const mepa_callout_t    MEPA_SHARED_PTR *callout,
                                struct mepa_callout_ctx MEPA_SHARED_PTR *callout_ctx,
                                struct mepa_board_conf              *board_conf)
{
    priv_data_t *priv;
    mepa_device_t *dev;

    dev = mepa_create_int(drv, callout, callout_ctx, board_conf,
                          (int)sizeof(priv_data_t));
    if (dev == NULL) {
        return NULL;
    }

    priv = dev->data;
    priv->cap = board_conf->dummy_phy_cap;

    return dev;
}

mepa_drivers_t mepa_dummy_driver_init(void)
{
    mepa_drivers_t res;
    static mepa_driver_t dummy[1] = {};

    dummy[0].id = 0xdeadbeefU;
    dummy[0].mask = 0xffffffffU;
    dummy[0].mepa_driver_poll = dummy_1g_poll;
    dummy[0].mepa_driver_conf_set = dummy_conf_set;
    dummy[0].mepa_driver_probe = dummy_probe;
    dummy[0].mepa_driver_capability = dummy_capability;
    dummy[0].mepa_driver_phy_info_get = dummy_info_get,
    dummy[0].mepa_driver_conf_get = dummy_conf_get;
    dummy[0].mepa_driver_reset = dummy_reset,
    dummy[0].mepa_driver_if_set = dummy_if_set,
    res.phy_drv = dummy;
    res.count = 1;

    return res;
}
