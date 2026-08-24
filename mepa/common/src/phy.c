// Copyright (c) 2004-2021 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#include <mepa_driver.h>
#include <mepa_utils.h>
#include <microchip/ethernet/phy/api.h>

#define PHY_FAMILIES 17U

#define MEPA_GLOBAL_REG_DEV_ID      0x1E /* MMD ID of GLOBAL Registers */
#define MEPA_SILICON_REVISION_REG   0x2  /* Silicon Revision register */
#define MEPA_REG_DEV_ID_1           0x1  /* MMD ID 1 */
#define MEPA_REG_ADDR_0             0    /* Register Address 0x0 */
#define MEPA_REG_ADDR_5             5    /* Register Address 0x5 */
#define MEPA_REG_ADDR_2             2    /* Register Address 0x2 */
#define MEPA_REG_ADDR_3             3    /* Register Address 0x3 */

static mepa_drivers_t MEPA_phy_lib[PHY_FAMILIES] = {};
static int MEPA_init_done = 0;
mepa_trace_func_t MEPA_TRACE_FUNCTION = NULL;

#if defined(MEPA_OPSYS_VELOCITYSP)
void MEPA_trace(mepa_trace_group_t  group,
                mepa_trace_level_t  level,
                const char         *location,
                uint32_t            line,
                const char         *file,
                const char         *msg)
{
    mepa_trace_data_t data = {
        .group    = group,
        .level    = level,
        .location = location,
        .line     = line,
        .file     = file,
        .format   = msg,
    };

    if (MEPA_TRACE_FUNCTION != NULL) {
        MEPA_TRACE_FUNCTION(&data, msg);
    }
}
#else
void MEPA_trace(mepa_trace_group_t  group,
                mepa_trace_level_t  level,
                const char         *location,
                uint32_t            line,
                const char         *file,
                const char         *format,
                ...)
{
    va_list args;
    mepa_trace_data_t data = {
        .group    = group,
        .level    = level,
        .location = location,
        .line     = line,
        .file     = file,
        .format   = format,
    };

    if (MEPA_TRACE_FUNCTION != NULL) {
        va_start(args, format);
        MEPA_TRACE_FUNCTION(&data, args);
        va_end(args);
    }
}
#endif

uint32_t mepa_phy_id_get(const mepa_callout_t    MEPA_SHARED_PTR *callout,
                         struct mepa_callout_ctx MEPA_SHARED_PTR *callout_ctx,
                         uint32_t       port_no)
{
    uint32_t i;
    uint32_t phy_id = 0;
    uint32_t reg2 = 0;
    uint32_t reg3 = 0;
    uint16_t reg2_16 = 0U;
    uint16_t reg3_16 = 0U;
    // 8488, Venice and Malibu are special and does not report the PHY on the
    // normal addresses.
    const uint16_t special[] = { 0x8484, 0x8487, 0x8488, 0x8489, 0x8490, 0x8491,
                                 0x8254, 0x8256, 0x8257, 0x8258, 0x8044, 0x8043,
                                 0x8042, 0x8024, 0x8023, 0x8022, 0x8268, 0x8267,
                                 0x8264, 0x8263, 0x8262
                               };


    // TODO, this check would be more robust if we combine it with the values of
    // mmd=1 reg 2 and reg3 (on venice this is 0x0007 0x0400)
    if (callout->spi_read != NULL) {
        (void)callout->spi_read(callout_ctx,  port_no, MEPA_GLOBAL_REG_DEV_ID, MEPA_REG_ADDR_0, &reg3);
        reg3 = (uint32_t)(reg3 & 0xFFFFU);
    } else if (callout->mmd_read != NULL) {
        (void)callout->mmd_read(callout_ctx, MEPA_GLOBAL_REG_DEV_ID, MEPA_REG_ADDR_0, &reg3_16);
        reg3 = (uint32_t)reg3_16;
    } else {
        /* no read method available */
    }
    for (i = 0; i < sizeof(special) / sizeof(special[0]); i++) {
        if (reg3 == special[i]) {
            return reg3;
        }
    }
    /* LAN80XX PHY A0 silicon SW Workarround
     * A0 silicon of LAN80XX PHYs have DEVICE_ID = 0, so read Revision ID register and check whether it is A0
     * If A0 then considering it as LAN8044 PHY.
     */
    if (callout->spi_read != NULL) {
        (void)callout->spi_read(callout_ctx,  port_no, MEPA_GLOBAL_REG_DEV_ID, MEPA_SILICON_REVISION_REG, &reg2);
        reg2 = (uint32_t)(reg2 & 0xFFFFU);
    } else if (callout->mmd_read != NULL) {
        (void)callout->mmd_read(callout_ctx, MEPA_GLOBAL_REG_DEV_ID, MEPA_SILICON_REVISION_REG, &reg2_16);
        reg2 = (uint32_t)reg2_16;
    } else {
        /* no read method available */
    }
    if ((reg3 == 0U) && (reg2 == 0xA0U)) {
        return 0x8044;
    }

    reg2_16 = 0U;
    reg3_16 = 0U;

    if (callout->miim_read != NULL) {
        (void)callout->miim_read(callout_ctx, MEPA_REG_ADDR_2, &reg2_16);
        (void)callout->miim_read(callout_ctx, MEPA_REG_ADDR_3, &reg3_16);
        reg2 = (uint32_t)reg2_16;
        reg3 = (uint32_t)reg3_16;
    }

    // Maybe it is a PHY responding to MMD and not MIIM
    if ((callout->mmd_read != NULL) && (reg2 == 0U) && (reg3 == 0U)) {
        (void)callout->mmd_read(callout_ctx, MEPA_REG_DEV_ID_1, MEPA_REG_ADDR_2, &reg2_16);
        (void)callout->mmd_read(callout_ctx, MEPA_REG_DEV_ID_1, MEPA_REG_ADDR_3, &reg3_16);
        reg2 = (uint32_t)reg2_16;
        reg3 = (uint32_t)reg3_16;
    }

    // PHY responding to APB register access
    if ((callout->apb_read != NULL) && (reg2 == 0U) && (reg3 == 0U)) {
        (void)callout->apb_read(callout_ctx, MEPA_REGACC_APB_PORT_BASE_ADDR_IDX, (MEPA_REG_ADDR_2 * 4), &reg2);
        (void)callout->apb_read(callout_ctx, MEPA_REGACC_APB_PORT_BASE_ADDR_IDX, (MEPA_REG_ADDR_3 * 4), &reg3);
    }

    reg2 = (uint32_t)(reg2 & 0xFFFFU);
    reg3 = (uint32_t)(reg3 & 0xFFFFU);
    phy_id = (reg2 << 16) | reg3;
    return phy_id;
}

static size_t size_align(size_t s)
{
    if ((s % 8U) != 0U) {
        s /= 8U;
        s += 1U;
        s *= 8U;
    }

    return s;
}

void *mepa_mem_alloc_int(const mepa_callout_t    MEPA_SHARED_PTR *callout,
                         struct mepa_callout_ctx MEPA_SHARED_PTR *callout_ctx,
                         size_t                                   size)
{
    void *mem;
    uint64_t *mem64;
    size_t cnt;

    if (callout->mem_alloc == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "No mem_alloc callout");
        return NULL;
    }

    size = size_align(size);

    mem = callout->mem_alloc(callout_ctx, size);
    if (mem == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "Out of memory? %z", size);
        return NULL;
    }

    mem64 = (uint64_t *)mem;

    size /= 8U;
    for (cnt = 0; cnt < size; cnt++) {
        mem64[cnt] = 0;
    }

    return mem;
}

void mepa_mem_free_int(const mepa_callout_t    MEPA_SHARED_PTR *callout,
                       struct mepa_callout_ctx MEPA_SHARED_PTR *callout_ctx,
                       void                                    *ptr)
{
    if (callout->mem_free == NULL) {
        return;
    }

    callout->mem_free(callout_ctx, ptr);
}

struct mepa_device *mepa_create_int(
    mepa_driver_t *drv,
    const mepa_callout_t    MEPA_SHARED_PTR *callout,
    struct mepa_callout_ctx MEPA_SHARED_PTR *callout_ctx,
    struct mepa_board_conf  *conf,
    int size_of_private_data)
{
    void            *mem;
    mepa_device_t   *dev;
    void            *priv;

    size_t dev_aligned = size_align(sizeof(mepa_device_t));
    size_t priv_aligned = size_align((size_t)size_of_private_data);

    mem = mepa_mem_alloc_int(callout, callout_ctx, dev_aligned + priv_aligned);
    if (mem == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "Alloc failed. Port: %d, size: %d", conf->numeric_handle, dev_aligned + priv_aligned);
        return NULL;
    }

    dev = (mepa_device_t *)mem;
    priv = (void *)((uint8_t *)mem + dev_aligned);

    dev->drv = drv;
    dev->data = priv;
    dev->callout = callout;
    dev->callout_ctx = callout_ctx;
    dev->numeric_handle = conf->numeric_handle;

    T_I(MEPA_TRACE_GRP_GEN, "mepa_device created (%d) at %p/%z, private data: %p/%z", conf->numeric_handle, (uintptr_t)dev, dev_aligned, (uintptr_t)(const uint8_t *)dev->data, priv_aligned);

    return dev;
}

mepa_rc mepa_delete_int(mepa_device_t *dev)
{
    mepa_mem_free_int(dev->callout, dev->callout_ctx, dev);
    return MEPA_RC_OK;
}

static void mepa_initialize_libraries(void)
{
    // Initialize all the drivers needed
    if (MEPA_init_done == 0) {
        // Raise conditions does not matter here. Multiple threads can do this,
        // it will waste a bit of CPU, but do no harm.
        MEPA_init_done = 1;

#if defined(MEPA_HAS_VTSS)
        MEPA_phy_lib[0] = mepa_mscc_driver_init();
        MEPA_phy_lib[1] = mepa_malibu_driver_init();
        MEPA_phy_lib[2] = mepa_venice_driver_init();
#endif

#if defined(MEPA_HAS_AQR)
        MEPA_phy_lib[3] = mepa_aqr_driver_init();
#endif

#if defined(MEPA_HAS_GPY2211)
        MEPA_phy_lib[4] = mepa_intel_driver_init();
#endif

#if defined(MEPA_HAS_LAN8814)
        MEPA_phy_lib[5] = mepa_lan8814_driver_init();
#endif

#if defined(MEPA_HAS_KSZ9031)
        MEPA_phy_lib[6] = mepa_ksz9031_driver_init();
#endif

#if defined(MEPA_HAS_LAN8770)
        MEPA_phy_lib[7] = mepa_lan8770_driver_init();
#endif
#if defined(MEPA_HAS_LAN884x)
        MEPA_phy_lib[8] = mepa_lan884x_driver_init();
#endif
#if defined(MEPA_HAS_LAN887X)
        MEPA_phy_lib[9] = mepa_lan887x_driver_init();
#endif
#if defined(MEPA_HAS_LAN867X)
        MEPA_phy_lib[10] = mepa_lan867x_driver_init();
#endif
#if defined(MEPA_HAS_LAN80XX)
        MEPA_phy_lib[11] = mepa_lan80xx_driver_init();
#endif
#if defined(MEPA_HAS_DUMMY_PHY)
        MEPA_phy_lib[12] = mepa_dummy_driver_init();
#endif
#if defined(MEPA_HAS_LAN8X8X)
        MEPA_phy_lib[13] = mepa_lan8x8x_driver_init();
#endif
#if defined(MEPA_HAS_VSC8574)
        MEPA_phy_lib[14] = mepa_vsc8574_driver_init();
#endif
#if defined(MEPA_HAS_AS2XXXX)
        MEPA_phy_lib[15] = mepa_as2xxxx_driver_init();
#endif
        // Shall be last
#if defined(MEPA_HAS_VTSS)
        MEPA_phy_lib[16] = mepa_default_phy_driver_init();
#endif
    }
}

static struct mepa_device *mepa_probe_phy(const mepa_callout_t    MEPA_SHARED_PTR *callout,
                                          struct mepa_callout_ctx MEPA_SHARED_PTR *callout_ctx,
                                          struct mepa_board_conf  *conf,
                                          uint32_t id,
                                          uint8_t use_driver_id)
{
    mepa_device_t *dev = NULL;

    for (uint32_t i = 0U; i < PHY_FAMILIES; i++) {
        if ((MEPA_phy_lib[i].count == 0U) || (MEPA_phy_lib[i].phy_drv == NULL)) {
            continue;
        }

        for (uint32_t j = 0U; j < MEPA_phy_lib[i].count; j++) {
            mepa_driver_t *driver = &MEPA_phy_lib[i].phy_drv[j];
            uint8_t match;

            if (use_driver_id == 1U) {
                match = (uint8_t)(driver->id == id);
            } else {
                match = (uint8_t)((driver->id & driver->mask) == (id & driver->mask));
            }

            if (match == 1U) {
                dev = driver->mepa_driver_probe(driver, callout, callout_ctx, conf);
                if (dev != NULL) {
                    T_I(MEPA_TRACE_GRP_GEN, "probe completed for port %d with driver id %x phy_id %x phy_family %d j %d", conf->numeric_handle, driver->id, id, i, j);
                    return dev;
                }
            }
        }
    }

    return NULL;
}

struct mepa_device *mepa_create_by_driver_id(const mepa_callout_t    MEPA_SHARED_PTR *callout,
                                             struct mepa_callout_ctx MEPA_SHARED_PTR *callout_ctx,
                                             struct mepa_board_conf  *conf,
                                             uint32_t driver_id)
{
    mepa_initialize_libraries();

    return mepa_probe_phy(callout, callout_ctx, conf, driver_id, 1U);
}

struct mepa_device *mepa_create(const mepa_callout_t    MEPA_SHARED_PTR *callout,
                                struct mepa_callout_ctx MEPA_SHARED_PTR *callout_ctx,
                                struct mepa_board_conf  *conf)
{
    uint32_t phy_id;

    mepa_initialize_libraries();

    if (conf->dummy_phy_cap > 0U) {
        phy_id = 0xdeadbeefU;
    } else {
        phy_id = mepa_phy_id_get(callout, callout_ctx, conf->numeric_handle);
    }

    return mepa_probe_phy(callout, callout_ctx, conf, phy_id, 0U);
}

mepa_rc mepa_delete(struct mepa_device *dev)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_delete == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_delete(dev);
}

mepa_rc mepa_reset(struct mepa_device *dev,
                   const mepa_reset_param_t *rst_conf)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_reset == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_reset(dev, rst_conf);
}

mepa_rc mepa_poll(struct mepa_device *dev,
                  mepa_status_t *status)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_poll == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_poll(dev, status);
}

mepa_rc mepa_conf_set(struct mepa_device *dev,
                      const mepa_conf_t *conf)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_conf_set == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_conf_set(dev, conf);
}

mepa_rc mepa_conf_get(struct mepa_device *dev,
                      mepa_conf_t *const conf)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_conf_get == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_conf_get(dev, conf);
}

mepa_rc mepa_if_set(struct mepa_device *dev,
                    mepa_port_interface_t intf)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_if_set == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_if_set(dev, intf);
}

mepa_rc mepa_if_get(struct mepa_device *dev,
                    mepa_port_speed_t speed,
                    mepa_port_interface_t *intf)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_if_get == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_if_get(dev, speed, intf);
}

mepa_rc mepa_power_set(struct mepa_device *dev,
                       mepa_power_mode_t power)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_power_set == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_power_set(dev, power);
}

mepa_rc mepa_cable_diag_start(struct mepa_device *dev,
                              int mode)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_cable_diag_start == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_cable_diag_start(dev, mode);
}

mepa_rc mepa_cable_diag_get(struct mepa_device *dev,
                            mepa_cable_diag_result_t *result)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_cable_diag_get == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_cable_diag_get(dev, result);
}

mepa_rc mepa_cable_diag_start_async(struct mepa_device *dev,
                                    int mode)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_cable_diag_start_async == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_cable_diag_start_async(dev, mode);
}

mepa_rc mepa_cable_diag_stop_async(struct mepa_device *dev)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_cable_diag_stop_async == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_cable_diag_stop_async(dev);
}

mepa_rc mepa_cable_diag_poll(struct mepa_device *dev)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_cable_diag_poll == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_cable_diag_poll(dev);
}

mepa_rc mepa_media_set(struct mepa_device *dev,
                       mepa_media_interface_t phy_media_if)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_media_set == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_media_set(dev, phy_media_if);
}

mepa_rc mepa_media_get(struct mepa_device *dev,
                       mepa_media_interface_t *phy_media_if)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_media_get == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_media_get(dev, phy_media_if);
}

mepa_rc mepa_aneg_status_get(struct mepa_device *dev,
                             mepa_aneg_status_t *status)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_aneg_status_get == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_aneg_status_get(dev, status);
}

mepa_rc mepa_clause22_read(struct mepa_device *dev,
                           uint32_t address,
                           uint16_t *const value)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_clause22_read == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_clause22_read(dev, address, value);
}

mepa_rc mepa_clause22_write(struct mepa_device *dev,
                            uint32_t address,
                            uint16_t value)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_clause22_write == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_clause22_write(dev, address, value);
}

mepa_rc mepa_clause45_read(struct mepa_device *dev,
                           uint32_t address,
                           uint16_t *const value)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_clause45_read == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_clause45_read(dev, address, value);
}

mepa_rc mepa_clause45_write(struct mepa_device *dev,
                            uint32_t address,
                            uint16_t value)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_clause45_write == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_clause45_write(dev, address, value);
}

mepa_rc mepa_event_enable_set(struct mepa_device *dev,
                              mepa_event_t event,
                              mesa_bool_t enable)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_event_enable_set == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_event_enable_set(dev, event, enable);
}

mepa_rc mepa_event_enable_get(struct mepa_device *dev,
                              mepa_event_t *const event)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_event_enable_get == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_event_enable_get(dev, event);
}

mepa_rc mepa_event_poll(struct mepa_device *dev,
                        mepa_event_t *const ev_mask)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_event_poll == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_event_poll(dev, ev_mask);
}

mepa_rc mepa_loopback_set(struct mepa_device *dev,
                          const mepa_loopback_t *loopback)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_loopback_set == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_loopback_set(dev, loopback);
}

mepa_rc mepa_loopback_get(struct mepa_device *dev,
                          mepa_loopback_t *const loopback)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_loopback_get == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_loopback_get(dev, loopback);
}

mepa_rc mepa_gpio_mode_set(struct mepa_device *dev,
                           const mepa_gpio_conf_t *data)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_gpio_mode_set == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_gpio_mode_set(dev, data);
}

mepa_rc mepa_gpio_out_set(struct mepa_device *dev,
                          uint8_t gpio_no,
                          mepa_bool_t value)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_gpio_out_set == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_gpio_out_set(dev, gpio_no, value);
}

mepa_rc mepa_gpio_in_get(struct mepa_device *dev,
                         uint8_t gpio_no,
                         mepa_bool_t *const value)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_gpio_in_get == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_gpio_in_get(dev, gpio_no, value);
}

mepa_rc mepa_synce_clock_conf_set(struct mepa_device *dev,
                                  const mepa_synce_clock_conf_t *conf)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_synce_clock_conf_set == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_synce_clock_conf_set(dev, conf);
}

mepa_rc mepa_link_base_port(struct mepa_device *dev,
                            struct mepa_device *base_dev,
                            uint8_t packet_idx)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_link_base_port == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_link_base_port(dev, base_dev, packet_idx);
}

mepa_rc mepa_phy_info_get(struct mepa_device *dev,
                          mepa_phy_info_t *const phy_info)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_phy_info_get == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    (void)memset(phy_info, 0, sizeof(mepa_phy_info_t));

    return dev->drv->mepa_driver_phy_info_get(dev, phy_info);
}

mepa_rc mepa_isolate_mode_conf(struct mepa_device *dev,
                               const mepa_bool_t iso_en)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_isolate_mode_conf == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_isolate_mode_conf(dev, iso_en);
}

mepa_rc mepa_i2c_read(mepa_device_t      *dev,
                      const uint8_t      i2c_mux,
                      const uint8_t      i2c_reg_addr,
                      const uint8_t      i2c_dev_addr,
                      const mepa_bool_t  word_access,
                      uint8_t            cnt,
                      uint8_t  *const    value)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_phy_i2c_read == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_phy_i2c_read(dev, i2c_mux, i2c_reg_addr, i2c_dev_addr, word_access, cnt, value);
}

mepa_rc mepa_i2c_write(mepa_device_t      *dev,
                       const uint8_t      i2c_mux,
                       const uint8_t      i2c_reg_addr,
                       const uint8_t      i2c_dev_addr,
                       const mepa_bool_t  word_access,
                       uint8_t            cnt,
                       const uint8_t           *value)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_phy_i2c_write == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_phy_i2c_write(dev, i2c_mux, i2c_reg_addr, i2c_dev_addr, word_access, cnt, value);
}

mepa_rc mepa_i2c_clock_select(mepa_device_t *dev, mepa_i2c_clk_select_t const *clk_value)
{

    if ((dev == NULL) || (dev->drv->mepa_driver_phy_i2c_write == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_phy_i2c_clock_select(dev, clk_value);
}

mepa_rc mepa_fefi_set(struct mepa_device *dev, const mepa_fefi_mode_t *fefi_conf)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_phy_fefi_get == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }
    return dev->drv->mepa_driver_phy_fefi_set(dev, fefi_conf);
}

mepa_rc mepa_fefi_get(struct mepa_device *dev, mepa_fefi_mode_t *const fefi_conf)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_phy_fefi_get == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }
    return dev->drv->mepa_driver_phy_fefi_get(dev, fefi_conf);
}

mepa_rc mepa_fefi_detect(struct mepa_device *dev, mepa_bool_t *const fefi_detect)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_phy_fefi_detect == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }
    return dev->drv->mepa_driver_phy_fefi_detect(dev, fefi_detect);
}

mepa_rc mepa_chip_temp_get(struct mepa_device *dev, i16 *const temp)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_chip_temp_get == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }
    return dev->drv->mepa_driver_chip_temp_get(dev, temp);
}

mepa_rc mepa_eee_mode_conf_set(struct mepa_device *dev,  const mepa_phy_eee_conf_t conf)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_eee_mode_conf_set == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }
    return dev->drv->mepa_driver_eee_mode_conf_set(dev, conf);
}

mepa_rc mepa_eee_mode_conf_get(struct mepa_device *dev,  mepa_phy_eee_conf_t *const conf)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_eee_mode_conf_get == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }
    return dev->drv->mepa_driver_eee_mode_conf_get(dev, conf);
}

mepa_rc mepa_eee_status_get(struct mepa_device *dev, uint8_t *const advertisement, mepa_bool_t *const rx_in_power_save_state, mepa_bool_t *const tx_in_power_save_state)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_eee_status_get == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }
    return dev->drv->mepa_driver_eee_status_get(dev, advertisement, rx_in_power_save_state, tx_in_power_save_state);
}

mepa_rc mepa_ts_mode_set(struct mepa_device *dev,
                         const mepa_bool_t enable)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_mode_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_mode_set(dev, enable);
}

mepa_rc mepa_ts_mode_get(struct mepa_device *dev,
                         mepa_bool_t *const enable)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_mode_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_mode_get(dev, enable);
}

mepa_rc mepa_ts_reset(struct mepa_device *dev,
                      const mepa_ts_reset_conf_t *const reset)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_reset == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_reset(dev, reset);
}

mepa_rc mepa_ts_init_conf_set(struct mepa_device              *dev,
                              const mepa_ts_init_conf_t       *const conf)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_init_conf_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_init_conf_set(dev, conf);
}

mepa_rc mepa_ts_init_conf_get(struct mepa_device              *dev,
                              mepa_ts_init_conf_t             *const conf)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_init_conf_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_init_conf_get(dev, conf);
}

mepa_rc mepa_ts_ltc_ls_en(struct mepa_device                  *dev,
                          mepa_ts_ls_type_t                    const type)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_ltc_ls_en == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_ltc_ls_en(dev, type);
}

mepa_rc mepa_ts_ltc_get(struct mepa_device                    *dev,
                        mepa_timestamp_t                      *const ts)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_ltc_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_ltc_get(dev, ts);
}

mepa_rc mepa_ts_ltc_set(struct mepa_device                    *dev,
                        const mepa_timestamp_t                *const ts)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_ltc_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_ltc_set(dev, ts);
}

mepa_rc mepa_ts_delay_asymmetry_get(struct mepa_device        *dev,
                                    mepa_timeinterval_t       *const delay)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_delay_asymmetry_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_delay_asymmetry_get(dev, delay);
}

mepa_rc mepa_ts_delay_asymmetry_set(struct mepa_device        *dev,
                                    const mepa_timeinterval_t *const delay)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_delay_asymmetry_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_delay_asymmetry_set(dev, delay);
}

mepa_rc mepa_ts_path_delay_get(struct mepa_device             *dev,
                               mepa_timeinterval_t            *const delay)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_path_delay_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_path_delay_get(dev, delay);
}

mepa_rc mepa_ts_path_delay_set(struct mepa_device *dev,
                               const mepa_timeinterval_t      *const delay)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_path_delay_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_path_delay_set(dev, delay);
}

mepa_rc mepa_ts_egress_latency_get(struct mepa_device         *dev,
                                   mepa_timeinterval_t        *const latency)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_egress_latency_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_egress_latency_get(dev, latency);
}

mepa_rc mepa_ts_egress_latency_set(struct mepa_device         *dev,
                                   const mepa_timeinterval_t  *const latency)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_egress_latency_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_egress_latency_set(dev, latency);
}

mepa_rc mepa_ts_ingress_latency_get(struct mepa_device        *dev,
                                    mepa_timeinterval_t       *const latency)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_ingress_latency_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_ingress_latency_get(dev, latency);
}

mepa_rc mepa_ts_ingress_latency_set(struct mepa_device        *dev,
                                    const mepa_timeinterval_t *const latency)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_ingress_latency_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_ingress_latency_set(dev, latency);
}

mepa_rc mepa_ts_clock_rateadj_get(struct mepa_device          *dev,
                                  mepa_ts_scaled_ppb_t        *const rateadj)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_clock_rateadj_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_clock_rateadj_get(dev, rateadj);
}

mepa_rc mepa_ts_clock_rateadj_set(struct mepa_device          *dev,
                                  const mepa_ts_scaled_ppb_t  *const rateadj)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_clock_rateadj_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_clock_rateadj_set(dev, rateadj);
}

mepa_rc mepa_ts_clock_adj1ns(struct mepa_device               *dev,
                             const mepa_bool_t                 incr)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_clock_adj1ns == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_clock_adj1ns(dev, incr);
}

mepa_rc mepa_ts_pps_conf_get(struct mepa_device               *dev,
                             mepa_ts_pps_conf_t               *const phy_pps_conf)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_pps_conf_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_pps_conf_get(dev, phy_pps_conf);
}

mepa_rc mepa_ts_pps_conf_set(struct mepa_device              *dev,
                             const mepa_ts_pps_conf_t        *const phy_pps_conf)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_pps_conf_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_pps_conf_set(dev, phy_pps_conf);
}

mepa_rc mepa_ts_rx_classifier_conf_get(struct mepa_device         *dev,
                                       uint16_t                    flow_index,
                                       mepa_ts_classifier_t       *const conf)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_rx_classifier_conf_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_rx_classifier_conf_get(dev, flow_index, conf);
}

mepa_rc mepa_ts_tx_classifier_conf_get(struct mepa_device         *dev,
                                       uint16_t                    flow_index,
                                       mepa_ts_classifier_t       *const conf)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_tx_classifier_conf_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_tx_classifier_conf_get(dev, flow_index, conf);
}

mepa_rc mepa_ts_rx_classifier_conf_set(struct mepa_device         *dev,
                                       uint16_t                    flow_index,
                                       const mepa_ts_classifier_t *const conf)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_rx_classifier_conf_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_rx_classifier_conf_set(dev, flow_index, conf);
}

mepa_rc mepa_ts_tx_classifier_conf_set(struct mepa_device         *dev,
                                       uint16_t                    flow_index,
                                       const mepa_ts_classifier_t *const conf)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_tx_classifier_conf_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_tx_classifier_conf_set(dev, flow_index, conf);
}

mepa_rc mepa_ts_rx_clock_conf_get(struct mepa_device              *dev,
                                  uint16_t                         clock_id,
                                  mepa_ts_ptp_clock_conf_t        *const conf)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_rx_clock_conf_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_rx_clock_conf_get(dev, clock_id, conf);
}

mepa_rc mepa_ts_tx_clock_conf_get(struct mepa_device              *dev,
                                  uint16_t                         clock_id,
                                  mepa_ts_ptp_clock_conf_t        *const conf)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_tx_clock_conf_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_tx_clock_conf_get(dev, clock_id, conf);
}

mepa_rc mepa_ts_rx_clock_conf_set(struct mepa_device              *dev,
                                  uint16_t                         clock_id,
                                  const mepa_ts_ptp_clock_conf_t  *const conf)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_rx_clock_conf_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_rx_clock_conf_set(dev, clock_id, conf);
}

mepa_rc mepa_ts_tx_clock_conf_set(struct mepa_device              *dev,
                                  uint16_t                         clock_id,
                                  const mepa_ts_ptp_clock_conf_t  *const conf)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_tx_clock_conf_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_tx_clock_conf_set(dev, clock_id, conf);
}

mepa_rc mepa_ts_stats_get(struct mepa_device                    *dev,
                          mepa_ts_stats_t                       *const stat)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_stats_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_stats_get(dev, stat);
}

mepa_rc mepa_ts_event_set(struct mepa_device                      *dev,
                          const mepa_bool_t                        enable,
                          const mepa_ts_event_t                    ev_mask)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_event_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_event_set(dev, enable, ev_mask);
}

mepa_rc mepa_ts_event_get(struct mepa_device                      *dev,
                          mepa_ts_event_t                         *const ev_mask)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_event_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_event_get(dev, ev_mask);
}

mepa_rc mepa_ts_event_poll(struct mepa_device                     *dev,
                           mepa_ts_event_t                        *const status)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_event_poll == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_event_poll(dev, status);
}

mepa_rc mepa_ts_fifo_read_install(struct mepa_device *dev, mepa_ts_fifo_read_t rd_cb)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_fifo_read_install == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    dev->drv->mepa_ts->mepa_driver_ts_fifo_read_install(dev, rd_cb);
    return MESA_RC_OK;
}

mepa_rc mepa_ts_fifo_empty(struct mepa_device                     *dev)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_fifo_empty == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_fifo_empty(dev);
}

mepa_rc mepa_ts_fifo_get(struct mepa_device *dev, mepa_fifo_ts_entry_t ts_list[], const size_t size, uint32_t *const num)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_fifo_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_fifo_get(dev, ts_list, size, num);
}

mepa_rc mepa_ts_fifo_signature_set(struct mepa_device *dev, const mepa_ts_fifo_sig_mask_t sig_mask)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL) || (dev->drv->mepa_ts->mepa_driver_ts_fifo_signature_set == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_fifo_signature_set(dev, sig_mask);
}

mepa_rc mepa_ts_fifo_signature_get(struct mepa_device *dev, mepa_ts_fifo_sig_mask_t *const sig_mask)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL) || (dev->drv->mepa_ts->mepa_driver_ts_fifo_signature_get == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_fifo_signature_get(dev, sig_mask);
}

mepa_rc mepa_ts_test_config(struct mepa_device                    *dev,
                            uint16_t                               test_id,
                            mepa_bool_t                            reg_dump)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_test_config == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_test_config(dev, test_id, reg_dump);
}

mepa_rc mepa_ts_pch_mch_error_info_get(struct mepa_device *dev, mepa_pch_mch_mismatch_info_t *const info)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_ts->mepa_driver_ts_pch_mch_error_info_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_pch_mch_error_info_get(dev, info);
}

mepa_rc mepa_ts_csr_reg_read(struct mepa_device *dev, const uint16_t mmd,
                             const uint16_t csr_address, uint32_t *const regvalue)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL) || (dev->drv->mepa_ts->mepa_driver_ts_csr_reg_read == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_csr_reg_read(dev, mmd, csr_address, regvalue);
}

mepa_rc mepa_ts_csr_reg_write(struct mepa_device *dev, const uint16_t mmd,
                              const uint16_t csr_address, const uint32_t *const regvalue)
{
    if ((dev == NULL) || (dev->drv->mepa_ts == NULL) || (dev->drv->mepa_ts->mepa_driver_ts_csr_reg_write == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_ts->mepa_driver_ts_csr_reg_write(dev, mmd, csr_address, regvalue);
}

mepa_rc mepa_debug_info_dump(struct mepa_device *dev,
                             const mepa_debug_print_t prntf,
                             const mepa_debug_info_t   *const info)
{
    mepa_rc rc;
    size_t  i = 0, j = 0;
    size_t  len = (1024 * 1024);
    char    c, *buf = dev->callout->mem_alloc(dev->callout_ctx, len);

    if (buf == NULL) {
        return MEPA_RC_ERROR;
    }

    *buf = '\0';
    len--;
    rc = mepa_debug_info_print_buf(dev, info, (int)len, buf);
    // Print in chunks for backward compatibility
    while (i < len) {
        c = buf[i++];
        if (c == '\0') {
            (void)prntf("%s", buf + j);
            break;
        } else if (i == (j + 128U)) {
            c = buf[i];
            buf[i] = '\0';
            (void)prntf("%s", buf + j);
            buf[i] = c;
            j = i;
        } else {
            // Empty on purpose
        }
    }
    if (i >= len) {
        (void)prntf("\n--- Truncated due to buffer size ---\n");
    }
    dev->callout->mem_free(dev->callout_ctx, buf);
    return rc;
}

mepa_rc mepa_debug_info_print_buf(struct mepa_device *dev,
                                  const mepa_debug_info_t   *const info,
                                  int len,
                                  char *const buf)
{
    lmu_ss_t ss = {};
    mepa_rc rc;

    if ((dev == NULL) || (dev->drv->mepa_driver_debug_info_dump == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    ss.buf.begin = buf;
    ss.buf.end = (buf + len);

    rc = dev->drv->mepa_driver_debug_info_dump(dev, &ss, info);
    if (ss.overflow != 0) {
        rc = MEPA_RC_ERROR;
    }

    return rc;
}

mepa_rc mepa_sqi_read(struct mepa_device *dev, uint32_t *const value)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_sqi_read == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_sqi_read(dev, value);
}

mepa_rc mepa_start_of_frame_conf_set(struct mepa_device *dev, const mepa_start_of_frame_conf_t *const conf)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_start_of_frame_conf_set == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_start_of_frame_conf_set(dev, conf);
}

mepa_rc mepa_start_of_frame_conf_get(struct mepa_device *dev, mepa_start_of_frame_conf_t *const value)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_start_of_frame_conf_get == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_start_of_frame_conf_get(dev, value);
}

mepa_rc mepa_framepreempt_set(struct mepa_device *dev, const mepa_bool_t value)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_framepreempt_set == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_framepreempt_set(dev, value);
}

mepa_rc mepa_framepreempt_get(struct mepa_device *dev, mepa_bool_t *const value)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_framepreempt_get == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_framepreempt_get(dev, value);
}

mepa_rc mepa_selftest_start(struct mepa_device *dev, const mepa_selftest_info_t *inf)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_selftest_start == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_selftest_start(dev, inf);
}

mepa_rc mepa_selftest_read(struct mepa_device *dev, mepa_selftest_info_t *const selftest_inf)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_selftest_read == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_selftest_read(dev, selftest_inf);
}

mepa_rc mepa_macsec_init_set(struct mepa_device *dev, const
                             mepa_macsec_init_t *const macsec_init)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_init_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_init_set(dev, macsec_init);
}

mepa_rc mepa_macsec_init_get(struct mepa_device *dev,
                             mepa_macsec_init_t *const macsec_init)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_init_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_init_get(dev, macsec_init);
}

mepa_rc mepa_macsec_secy_conf_add(struct mepa_device *dev,
                                  const mepa_macsec_port_t port,
                                  const mepa_macsec_secy_conf_t *const conf)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_secy_conf_add == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_secy_conf_add(dev, port, conf);
}

mepa_rc mepa_macsec_secy_conf_update(struct mepa_device *dev,
                                     const mepa_macsec_port_t port,
                                     const mepa_macsec_secy_conf_t *const conf)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_secy_conf_update == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_secy_conf_update(dev, port, conf);
}

mepa_rc mepa_macsec_secy_conf_get(struct mepa_device *dev,
                                  const mepa_macsec_port_t port,
                                  mepa_macsec_secy_conf_t *const conf)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_secy_conf_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_secy_conf_get(dev, port, conf);
}

mepa_rc mepa_macsec_secy_conf_del(struct mepa_device *dev,
                                  const mepa_macsec_port_t port)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_secy_conf_del == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_secy_conf_del(dev, port);
}

mepa_rc mepa_macsec_secy_controlled_set(struct mepa_device *dev,
                                        const mepa_macsec_port_t port,
                                        const mepa_bool_t enable)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_secy_controlled_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_secy_controlled_set(dev, port, enable);

}

mepa_rc mepa_macsec_secy_controlled_get(struct mepa_device *dev,
                                        const mepa_macsec_port_t port,
                                        mepa_bool_t *const enable)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_secy_controlled_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_secy_controlled_get(dev, port, enable);

}

mepa_rc mepa_macsec_secy_port_status_get(struct mepa_device *dev,
                                         const mepa_macsec_port_t port,
                                         mepa_macsec_secy_port_status_t *const status)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_secy_port_status_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_secy_port_status_get(dev, port, status);

}

mepa_rc mepa_macsec_port_get_next(struct mepa_device *dev,
                                  const mepa_port_no_t port_no,
                                  const mepa_macsec_port_t *const search_macsec_port,
                                  mepa_macsec_port_t *const found_macsec_port)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_port_get_next == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_port_get_next(dev, port_no, search_macsec_port, found_macsec_port);
}

mepa_rc mepa_macsec_rx_sc_add(struct mepa_device *dev,
                              const mepa_macsec_port_t port,
                              const mepa_macsec_sci_t *const sci)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_rx_sc_add == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_rx_sc_add(dev, port, sci);
}

mepa_rc mepa_macsec_rx_sc_update(struct mepa_device *dev,
                                 const mepa_macsec_port_t port,
                                 const mepa_macsec_sci_t  *const sci,
                                 const mepa_macsec_rx_sc_conf_t *const conf)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_rx_sc_update == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_rx_sc_update(dev, port, sci, conf);
}

mepa_rc mepa_macsec_rx_sc_get_conf(struct mepa_device *dev,
                                   const mepa_macsec_port_t port,
                                   const mepa_macsec_sci_t *const sci,
                                   mepa_macsec_rx_sc_conf_t *const conf)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_rx_sc_get_conf == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_rx_sc_get_conf(dev, port, sci, conf);
}

mepa_rc mepa_macsec_rx_sc_get_next(struct mepa_device *dev,
                                   const mepa_macsec_port_t port,
                                   const mepa_macsec_sci_t *const search_sci,
                                   mepa_macsec_sci_t *const found_sci)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_rx_sc_get_next == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_rx_sc_get_next(dev, port, search_sci, found_sci);
}

mepa_rc mepa_macsec_rx_sc_del(struct mepa_device *dev,
                              const mepa_macsec_port_t port,
                              const mepa_macsec_sci_t *const sci)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_rx_sc_del == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_rx_sc_del(dev, port, sci);
}

mepa_rc mepa_macsec_rx_sc_status_get(struct mepa_device *dev,
                                     const mepa_macsec_port_t port,
                                     const mepa_macsec_sci_t *const sci,
                                     mepa_macsec_rx_sc_status_t *const status)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_rx_sc_status_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_rx_sc_status_get(dev, port, sci, status);
}

mepa_rc mepa_macsec_tx_sc_set(struct mepa_device *dev,
                              const mepa_macsec_port_t port)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_tx_sc_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_tx_sc_set(dev, port);
}

mepa_rc mepa_macsec_tx_sc_update(struct mepa_device *dev,
                                 const mepa_macsec_port_t port,
                                 const mepa_macsec_tx_sc_conf_t *const conf)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_tx_sc_update == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_tx_sc_update(dev, port, conf);
}

mepa_rc mepa_macsec_tx_sc_get_conf(struct mepa_device *dev,
                                   const mepa_macsec_port_t port,
                                   mepa_macsec_tx_sc_conf_t *const conf)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_tx_sc_get_conf == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_tx_sc_get_conf(dev, port, conf);
}

mepa_rc mepa_macsec_tx_sc_del(struct mepa_device *dev,
                              const mepa_macsec_port_t port)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_tx_sc_del == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_tx_sc_del(dev, port);
}

mepa_rc mepa_macsec_tx_sc_status_get(struct mepa_device *dev,
                                     const mepa_macsec_port_t port,
                                     mepa_macsec_tx_sc_status_t *const status)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_tx_sc_status_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_tx_sc_status_get(dev, port, status);
}

mepa_rc mepa_macsec_rx_sa_set(struct mepa_device *dev,
                              const mepa_macsec_port_t port,
                              const mepa_macsec_sci_t *const sci,
                              const uint16_t an,
                              const uint32_t lowest_pn,
                              const mepa_macsec_sak_t *const sak)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_rx_sa_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_rx_sa_set(dev, port, sci, an, lowest_pn, sak);
}

mepa_rc mepa_macsec_rx_sa_get(struct mepa_device *dev,
                              const mepa_macsec_port_t port,
                              const mepa_macsec_sci_t *const sci,
                              const uint16_t an,
                              uint32_t *const lowest_pn,
                              mepa_macsec_sak_t *const sak,
                              mepa_bool_t *const active)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_rx_sa_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_rx_sa_get(dev, port, sci, an, lowest_pn, sak, active);
}

mepa_rc mepa_macsec_rx_sa_activate(struct mepa_device *dev,
                                   const mepa_macsec_port_t port,
                                   const mepa_macsec_sci_t *const sci,
                                   const uint16_t an)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_rx_sa_activate == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_rx_sa_activate(dev, port, sci, an);
}

mepa_rc mepa_macsec_rx_sa_disable(struct mepa_device *dev,
                                  const mepa_macsec_port_t port,
                                  const mepa_macsec_sci_t *const sci,
                                  const uint16_t an)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_rx_sa_disable == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_rx_sa_disable(dev, port, sci, an);
}

mepa_rc mepa_macsec_rx_sa_del(struct mepa_device *dev,
                              const mepa_macsec_port_t port,
                              const mepa_macsec_sci_t *const sci,
                              const uint16_t an)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_rx_sa_del == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_rx_sa_del(dev, port, sci, an);
}

mepa_rc mepa_macsec_rx_sa_lowest_pn_update(struct mepa_device *dev,
                                           const mepa_macsec_port_t port,
                                           const mepa_macsec_sci_t *const sci,
                                           const uint16_t an,
                                           const uint32_t lowest_pn)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_rx_sa_lowest_pn_update == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_rx_sa_lowest_pn_update(dev, port, sci, an, lowest_pn);
}

mepa_rc mepa_macsec_rx_sa_status_get(struct mepa_device *dev,
                                     const mepa_macsec_port_t port,
                                     const mepa_macsec_sci_t *const sci,
                                     const uint16_t an,
                                     mepa_macsec_rx_sa_status_t *const status)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_rx_sa_status_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_rx_sa_status_get(dev, port, sci, an, status);
}

mepa_rc mepa_macsec_rx_seca_set(struct mepa_device *dev,
                                const mepa_macsec_port_t port,
                                const mepa_macsec_sci_t *const sci,
                                const uint16_t an,
                                const mepa_macsec_pkt_num_t lowest_pn,
                                const mepa_macsec_sak_t *const sak,
                                const mepa_macsec_ssci_t *const ssci)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_rx_seca_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_rx_seca_set(dev, port, sci, an, lowest_pn, sak, ssci);
}

mepa_rc mepa_macsec_rx_seca_get(struct mepa_device *dev,
                                const mepa_macsec_port_t port,
                                const mepa_macsec_sci_t *const sci,
                                const uint16_t an,
                                mepa_macsec_pkt_num_t *const lowest_pn,
                                mepa_macsec_sak_t *const sak,
                                mepa_bool_t *const active,
                                mepa_macsec_ssci_t *const ssci)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_rx_seca_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_rx_seca_get(dev, port, sci, an, lowest_pn, sak, active, ssci);
}

mepa_rc mepa_macsec_rx_seca_lowest_pn_update(struct mepa_device *dev,
                                             const mepa_macsec_port_t port,
                                             const mepa_macsec_sci_t *const sci,
                                             const uint16_t an,
                                             const mepa_macsec_pkt_num_t lowest_pn)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_rx_seca_lowest_pn_update == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_rx_seca_lowest_pn_update(dev, port, sci, an, lowest_pn);
}

mepa_rc mepa_macsec_tx_sa_set(struct mepa_device *dev,
                              const mepa_macsec_port_t port,
                              const uint16_t an,
                              const uint32_t next_pn,
                              const mepa_bool_t confidentiality,
                              const mepa_macsec_sak_t *const sak)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_tx_sa_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_tx_sa_set(dev, port, an, next_pn, confidentiality, sak);
}

mepa_rc mepa_macsec_tx_sa_get(struct mepa_device *dev,
                              const mepa_macsec_port_t port,
                              const uint16_t an,
                              uint32_t *const next_pn,
                              mepa_bool_t *const confidentiality,
                              mepa_macsec_sak_t *const sak,
                              mepa_bool_t *const active)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_tx_sa_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_tx_sa_get(dev, port, an, next_pn, confidentiality, sak, active);
}

mepa_rc mepa_macsec_tx_sa_activate(struct mepa_device *dev,
                                   const mepa_macsec_port_t port,
                                   const uint16_t an)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_tx_sa_activate == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_tx_sa_activate(dev, port, an);
}

mepa_rc mepa_macsec_tx_sa_disable(struct mepa_device *dev,
                                  const mepa_macsec_port_t port,
                                  const uint16_t an)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_tx_sa_disable == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_tx_sa_disable(dev, port, an);
}

mepa_rc mepa_macsec_tx_sa_del(struct mepa_device *dev,
                              const mepa_macsec_port_t port,
                              const uint16_t an)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_tx_sa_del == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_tx_sa_del(dev, port, an);
}

mepa_rc mepa_macsec_tx_sa_status_get(struct mepa_device *dev,
                                     const mepa_macsec_port_t port,
                                     const uint16_t an,
                                     mepa_macsec_tx_sa_status_t *const status)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_tx_sa_status_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_tx_sa_status_get(dev, port, an, status);
}

mepa_rc mepa_macsec_tx_seca_set(struct mepa_device *dev,
                                const mepa_macsec_port_t port,
                                const uint16_t an,
                                const mepa_macsec_pkt_num_t next_pn,
                                const mepa_bool_t confidentiality,
                                const mepa_macsec_sak_t *const sak,
                                const mepa_macsec_ssci_t *const ssci)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_tx_seca_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_tx_seca_set(dev, port, an, next_pn, confidentiality, sak, ssci);
}

mepa_rc mepa_macsec_tx_seca_get(struct mepa_device *dev,
                                const mepa_macsec_port_t port,
                                const uint16_t an,
                                mepa_macsec_pkt_num_t *const next_pn,
                                mepa_bool_t *const confidentiality,
                                mepa_macsec_sak_t *const sak,
                                mepa_bool_t *const active,
                                mepa_macsec_ssci_t *const ssci)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_tx_seca_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_tx_seca_get(dev, port, an, next_pn, confidentiality, sak, active, ssci);
}

#pragma coverity compliance deviate                                            \
    "MISRA C-2023 Rule 5.1"                                                    \
    "MACsec API identifiers share long common prefixes; names are part of the public API"
mepa_rc mepa_macsec_controlled_counters_get(struct mepa_device *dev,
                                            const mepa_macsec_port_t port,
                                            mepa_macsec_secy_port_counters_t *const counters)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_controlled_counters_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_controlled_counters_get(dev, port, counters);
}

#pragma coverity compliance deviate                                            \
    "MISRA C-2023 Rule 5.1"                                                    \
    "MACsec API identifiers share long common prefixes; names are part of the public API"
mepa_rc mepa_macsec_uncontrolled_counters_get(struct mepa_device                   *dev,
                                              const mepa_port_no_t                port_no,
                                              mepa_macsec_uncontrolled_counters_t *const counters)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_uncontrolled_counters_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_uncontrolled_counters_get(dev, port_no, counters);
}

mepa_rc mepa_macsec_common_counters_get(struct mepa_device *dev,
                                        const mepa_port_no_t port_no,
                                        mepa_macsec_common_counters_t *const counters)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_common_counters_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_common_counters_get(dev, port_no, counters);
}

mepa_rc mepa_macsec_secy_cap_get(struct mepa_device *dev,
                                 const mepa_port_no_t port_no,
                                 mepa_macsec_secy_cap_t *const cap)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_secy_cap_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_secy_cap_get(dev, port_no, cap);
}

mepa_rc mepa_macsec_secy_counters_get(struct mepa_device *dev,
                                      const mepa_macsec_port_t port,
                                      mepa_macsec_secy_counters_t *const counters)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_secy_counters_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_secy_counters_get(dev, port, counters);
}

mepa_rc mepa_macsec_counters_update(struct mepa_device *dev,
                                    const mepa_port_no_t port_no)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_counters_update == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_counters_update(dev, port_no);
}

mepa_rc mepa_macsec_counters_clear(struct mepa_device *dev,
                                   const mepa_port_no_t port_no)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_counters_clear == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_counters_clear(dev, port_no);
}

mepa_rc mepa_macsec_rx_sc_counters_get(struct mepa_device *dev,
                                       const mepa_macsec_port_t port,
                                       const mepa_macsec_sci_t *const sci,
                                       mepa_macsec_rx_sc_counters_t *const counters)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_rx_sc_counters_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_rx_sc_counters_get(dev, port, sci, counters);
}

mepa_rc mepa_macsec_tx_sc_counters_get(struct mepa_device *dev,
                                       const mepa_macsec_port_t port,
                                       mepa_macsec_tx_sc_counters_t *const counters)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_tx_sc_counters_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_tx_sc_counters_get(dev, port, counters);
}

mepa_rc mepa_macsec_tx_sa_counters_get(struct mepa_device *dev,
                                       const mepa_macsec_port_t port,
                                       const uint16_t an,
                                       mepa_macsec_tx_sa_counters_t *const counters)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_tx_sa_counters_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_tx_sa_counters_get(dev, port, an, counters);
}

mepa_rc mepa_macsec_rx_sa_counters_get(struct mepa_device *dev,
                                       const mepa_macsec_port_t port,
                                       const mepa_macsec_sci_t *const sci,
                                       const uint16_t an,
                                       mepa_macsec_rx_sa_counters_t *const counters)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_rx_sa_counters_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_rx_sa_counters_get(dev, port, sci, an, counters);
}

#pragma coverity compliance deviate                                            \
    "MISRA C-2023 Rule 5.1"                                                    \
    "MACsec API identifiers share long common prefixes; names are part of the public API"
mepa_rc mepa_macsec_control_frame_match_conf_set(struct mepa_device *dev,
                                                 const mepa_port_no_t port_no,
                                                 const mepa_macsec_control_frame_match_conf_t *const conf,
                                                 uint32_t *const rule_id)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_control_frame_match_conf_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_control_frame_match_conf_set(dev, port_no, conf, rule_id);
}

mepa_rc mepa_macsec_control_frame_match_conf_del(struct mepa_device *dev,
                                                 const mepa_port_no_t port_no,
                                                 const uint32_t rule_id)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_control_frame_match_conf_del == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_control_frame_match_conf_del(dev, port_no, rule_id);
}

#pragma coverity compliance deviate                                            \
    "MISRA C-2023 Rule 5.1"                                                    \
    "MACsec API identifiers share long common prefixes; names are part of the public API"
mepa_rc mepa_macsec_control_frame_match_conf_get(struct mepa_device *dev,
                                                 const mepa_port_no_t port_no,
                                                 mepa_macsec_control_frame_match_conf_t *const conf,
                                                 uint32_t rule_id)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_control_frame_match_conf_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_control_frame_match_conf_get(dev, port_no, conf, rule_id);
}

mepa_rc mepa_macsec_pattern_set(struct mepa_device *dev,
                                const mepa_macsec_port_t port,
                                const mepa_macsec_direction_t direction,
                                const mepa_macsec_match_action_t action,
                                const mepa_macsec_match_pattern_t *const pattern)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_pattern_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_pattern_set(dev, port, direction, action, pattern);
}

mepa_rc mepa_macsec_pattern_del(struct mepa_device *dev,
                                const mepa_macsec_port_t port,
                                const mepa_macsec_direction_t direction,
                                const mepa_macsec_match_action_t action)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_pattern_del == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_pattern_del(dev, port, direction, action);
}

mepa_rc mepa_macsec_pattern_get(struct mepa_device *dev,
                                const mepa_macsec_port_t port,
                                const mepa_macsec_direction_t direction,
                                const mepa_macsec_match_action_t action,
                                mepa_macsec_match_pattern_t *const pattern)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_pattern_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_pattern_get(dev, port, direction, action, pattern);
}

mepa_rc mepa_macsec_default_action_set(struct mepa_device *dev,
                                       const mepa_port_no_t port_no,
                                       const mepa_macsec_default_action_policy_t *const policy)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_default_action_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_default_action_set(dev, port_no, policy);
}

mepa_rc mepa_macsec_default_action_get(struct mepa_device *dev,
                                       const mepa_port_no_t port_no,
                                       mepa_macsec_default_action_policy_t *const policy)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_default_action_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_default_action_get(dev, port_no, policy);
}

mepa_rc mepa_macsec_bypass_mode_set(struct mepa_device *dev,
                                    const mepa_port_no_t port_no,
                                    const mepa_macsec_bypass_mode_t *const bypass)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_bypass_mode_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_bypass_mode_set(dev, port_no, bypass);
}

mepa_rc mepa_macsec_bypass_mode_get(struct mepa_device *dev,
                                    const mepa_port_no_t port_no,
                                    mepa_macsec_bypass_mode_t *const bypass)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_bypass_mode_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_bypass_mode_get(dev, port_no, bypass);
}

mepa_rc mepa_macsec_bypass_tag_set(struct mepa_device *dev,
                                   const mepa_macsec_port_t port,
                                   const mepa_macsec_tag_bypass_t tag)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_bypass_tag_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_bypass_tag_set(dev, port, tag);
}

mepa_rc mepa_macsec_bypass_tag_get(struct mepa_device *dev,
                                   const mepa_macsec_port_t port,
                                   mepa_macsec_tag_bypass_t *const tag)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_bypass_tag_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_bypass_tag_get(dev, port, tag);
}

mepa_rc mepa_macsec_mtu_set(struct mepa_device *dev,
                            const mepa_port_no_t port_no,
                            const mepa_macsec_mtu_t *const mtu_conf)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_mtu_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_mtu_set(dev, port_no, mtu_conf);
}

mepa_rc mepa_macsec_mtu_get(struct mepa_device *dev,
                            const mepa_port_no_t port_no,
                            mepa_macsec_mtu_t *mtu_conf)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_mtu_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_mtu_get(dev, port_no, mtu_conf);
}

mepa_rc mepa_macsec_frame_capture_set(struct mepa_device *dev,
                                      const mepa_port_no_t port_no,
                                      const mepa_macsec_frame_capture_t capture)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_frame_capture_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_frame_capture_set(dev, port_no, capture);
}

mepa_rc mepa_macsec_frame_get(struct mepa_device *dev,
                              const mepa_port_no_t port_no,
                              const uint32_t buf_length,
                              uint32_t *const return_length,
                              uint8_t *const frame)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_frame_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_frame_get(dev, port_no, buf_length, return_length, frame);
}

mepa_rc mepa_macsec_event_enable_set(struct mepa_device *dev,
                                     const mepa_port_no_t port_no,
                                     const mepa_macsec_event_t ev_mask,
                                     const mepa_bool_t enable)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_event_enable_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_event_enable_set(dev, port_no, ev_mask, enable);
}

mepa_rc mepa_macsec_event_enable_get(struct mepa_device *dev,
                                     const mepa_port_no_t port_no,
                                     mepa_macsec_event_t *const ev_mask)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_event_enable_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_event_enable_get(dev, port_no, ev_mask);
}

mepa_rc mepa_macsec_event_poll(struct mepa_device *dev,
                               const mepa_port_no_t port_no,
                               mepa_macsec_event_t *const ev_mask)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_event_poll == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_event_poll(dev, port_no, ev_mask);
}

#pragma coverity compliance deviate                                            \
    "MISRA C-2023 Rule 5.1"                                                    \
    "MACsec API identifiers share long common prefixes; names are part of the public API"
mepa_rc mepa_macsec_event_seq_threshold_set(struct mepa_device *dev,
                                            const mepa_port_no_t port_no,
                                            const uint32_t threshold)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_event_seq_threshold_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_event_seq_threshold_set(dev, port_no, threshold);
}

mepa_rc mepa_macsec_event_seq_threshold_get(struct mepa_device *dev,
                                            const mepa_port_no_t port_no,
                                            uint32_t *const threshold)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_event_seq_threshold_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_event_seq_threshold_get(dev, port_no, threshold);
}

#pragma coverity compliance deviate                                            \
    "MISRA C-2023 Rule 5.1"                                                    \
    "MACsec API identifiers share long common prefixes; names are part of the public API"
mepa_rc mepa_macsec_event_xpn_seq_threshold_set(struct mepa_device *dev,
                                                const mepa_port_no_t port_no,
                                                const uint64_t threshold)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_event_xpn_seq_threshold_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_event_xpn_seq_threshold_set(dev, port_no, threshold);
}

mepa_rc mepa_macsec_event_xpn_seq_threshold_get(struct mepa_device *dev,
                                                const mepa_port_no_t port_no,
                                                uint64_t *const threshold)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_event_xpn_seq_threshold_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_event_xpn_seq_threshold_get(dev, port_no, threshold);
}

mepa_rc mepa_macsec_egr_intr_sa_get(struct mepa_device *dev,
                                    const mepa_port_no_t port_no,
                                    mepa_macsec_port_t *const port,
                                    uint16_t *const an)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_egr_intr_sa_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_egr_intr_sa_get(dev, port_no, port, an);
}

mepa_rc mepa_macsec_csr_read(struct mepa_device *dev,
                             const mepa_port_no_t port_no,
                             const uint16_t mmd,
                             const uint32_t addr,
                             uint32_t *const value)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_csr_read == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_csr_read(dev, port_no, mmd, addr, value);
}

mepa_rc mepa_macsec_csr_write(struct mepa_device *dev,
                              const mepa_port_no_t port_no,
                              const uint32_t mmd,
                              const uint32_t addr,
                              const uint32_t value)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_csr_write == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_csr_write(dev, port_no, mmd, addr, value);
}

mepa_rc mepa_macsec_dbg_counter_get(struct mepa_device *dev,
                                    const mepa_port_no_t port_no,
                                    mepa_macsec_rc_dbg_counters_t *const counters)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_dbg_counter_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_dbg_counter_get(dev, port_no, counters);
}

mepa_rc mepa_macsec_hmac_counters_get(struct mepa_device *dev,
                                      const mepa_port_no_t port_no,
                                      mepa_macsec_mac_counters_t *const counters,
                                      const mepa_bool_t clear)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_hmac_counters_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_hmac_counters_get(dev, port_no, counters, clear);
}

mepa_rc mepa_macsec_lmac_counters_get(struct mepa_device *dev,
                                      const mepa_port_no_t port_no,
                                      mepa_macsec_mac_counters_t *const counters,
                                      const mepa_bool_t clear)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_lmac_counters_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_lmac_counters_get(dev, port_no, counters, clear);
}

mepa_rc mepa_macsec_is_capable(struct mepa_device *dev,
                               const mepa_port_no_t port_no,
                               mepa_bool_t *capable)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_is_capable == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_is_capable(dev, port_no, capable);
}

mepa_rc mepa_macsec_dbg_reg_dump(struct mepa_device *dev,
                                 const mepa_port_no_t port_no,
                                 const mepa_debug_print_t prntf)
{
    mepa_rc rc;
    size_t  i = 0, j = 0;
    size_t  len = (1024 * 1024);
    char    c, *buf = dev->callout->mem_alloc(dev->callout_ctx, len);

    if (buf == NULL) {
        return MEPA_RC_ERROR;
    }

    *buf = '\0';
    len--;
    rc = mepa_macsec_dbg_reg_print_buf(dev, port_no, (int)len, buf);
    // Print in chunks for backward compatibility
    while (i < len) {
        c = buf[i++];
        if (c == '\0') {
            (void)prntf("%s", buf + j);
            break;
        } else if (i == (j + 128U)) {
            c = buf[i];
            buf[i] = '\0';
            (void)prntf("%s", buf + j);
            buf[i] = c;
            j = i;
        } else {
            // Empty on purpose
        }
    }
    if (i >= len) {
        (void)prntf("\n--- Truncated due to buffer size ---\n");
    }
    dev->callout->mem_free(dev->callout_ctx, buf);
    return rc;
}

mepa_rc mepa_macsec_dbg_reg_print_buf(struct mepa_device *dev,
                                      const mepa_port_no_t port_no,
                                      int len,
                                      char *const buf)
{
    lmu_ss_t ss = {};
    mepa_rc rc;

    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_dbg_reg_dump == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    ss.buf.begin = buf;
    ss.buf.end = (buf + len);

    rc = dev->drv->mepa_macsec->mepa_driver_macsec_dbg_reg_dump(dev, port_no, &ss);
    if (ss.overflow != 0) {
        rc = MEPA_RC_ERROR;
    }

    return rc;
}

mepa_rc mepa_macsec_inst_count_get(struct mepa_device *dev,
                                   const mepa_port_no_t port_no,
                                   mepa_macsec_inst_count_t *count)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_inst_count_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_inst_count_get(dev, port_no, count);
}

mepa_rc mepa_macsec_lmac_counters_clear(struct mepa_device *dev,
                                        const mepa_port_no_t port_no)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_lmac_counters_clear == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_lmac_counters_clear(dev, port_no);
}

mepa_rc mepa_macsec_hmac_counters_clear(struct mepa_device *dev,
                                        const mepa_port_no_t port_no)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_hmac_counters_clear == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_hmac_counters_clear(dev, port_no);
}

mepa_rc mepa_macsec_debug_counters_clear(struct mepa_device *dev,
                                         const mepa_port_no_t port_no)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_debug_counters_clear == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_debug_counters_clear(dev, port_no);
}

mepa_rc mepa_macsec_common_counters_clear(struct mepa_device *dev,
                                          const mepa_port_no_t port_no)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_common_counters_clear == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_common_counters_clear(dev, port_no);
}

mepa_rc mepa_macsec_uncontrolled_counters_clear(struct mepa_device *dev,
                                                const mepa_port_no_t port_no)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_uncontrolled_counters_clear == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_uncontrolled_counters_clear(dev, port_no);
}

mepa_rc mepa_macsec_controlled_counters_clear(struct mepa_device *dev,
                                              const mepa_macsec_port_t port)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_controlled_counters_clear == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_controlled_counters_clear(dev, port);
}

mepa_rc mepa_macsec_rxsa_counters_clear(struct mepa_device *dev,
                                        const mepa_macsec_port_t port,
                                        const mepa_macsec_sci_t *const sci,
                                        const uint16_t an)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_rxsa_counters_clear == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_rxsa_counters_clear(dev, port, sci, an);
}

mepa_rc mepa_macsec_rxsc_counters_clear(struct mepa_device *dev,
                                        const mepa_macsec_port_t port,
                                        const mepa_macsec_sci_t  *const sci)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_rxsc_counters_clear == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_rxsc_counters_clear(dev, port, sci);
}

mepa_rc mepa_macsec_txsa_counters_clear(struct mepa_device *dev,
                                        const mepa_macsec_port_t  port,
                                        const uint16_t  an)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_txsa_counters_clear == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_txsa_counters_clear(dev, port, an);
}

mepa_rc mepa_macsec_txsc_counters_clear(struct mepa_device *dev,
                                        const mepa_macsec_port_t port)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_txsc_counters_clear == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_txsc_counters_clear(dev, port);
}

mepa_rc mepa_macsec_secy_counters_clear(struct mepa_device *dev,
                                        const mepa_macsec_port_t port)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_secy_counters_clear == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_secy_counters_clear(dev, port);
}

mepa_rc mepa_macsec_port_enable_status_get(struct mepa_device *dev,
                                           const mepa_port_no_t port_no,
                                           mepa_bool_t *status)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_port_enable_status_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_port_enable_status_get(dev, port_no, status);
}

mepa_rc mepa_macsec_rxsa_an_status_get (struct mepa_device *dev,
                                        const mepa_macsec_port_t port,
                                        const mepa_macsec_sci_t *const sci,
                                        const uint16_t an,
                                        mepa_bool_t *status)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_rxsa_an_status_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_rxsa_an_status_get(dev, port, sci, an, status);
}

mepa_rc mepa_mac_block_mtu_get(struct mepa_device *dev,
                               const mepa_port_no_t port_no,
                               uint16_t *const mtu_value,
                               mepa_bool_t *const mtu_tag_check)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_mac_block_mtu_get == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_mac_block_mtu_get(dev, port_no, mtu_value, mtu_tag_check);
}

mepa_rc mepa_mac_block_mtu_set(struct mepa_device *dev,
                               const mepa_port_no_t port_no,
                               const uint16_t mtu_value,
                               const mepa_bool_t mtu_tag_check)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_mac_block_mtu_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_mac_block_mtu_set(dev, port_no, mtu_value, mtu_tag_check);
}

mepa_rc mepa_macsec_fcbuf_frame_gap_comp_set(struct mepa_device *dev,
                                             const mepa_port_no_t port_no,
                                             const uint8_t frm_gap)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_fcbuf_frame_gap_comp_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_fcbuf_frame_gap_comp_set(dev, port_no, frm_gap);
}

mepa_rc mepa_macsec_dbg_fcb_block_reg_dump(struct mepa_device *dev,
                                           const mepa_port_no_t port_no,
                                           const mepa_debug_print_t prntf)
{
    mepa_rc rc;
    size_t  i = 0, j = 0;
    size_t  len = (1024 * 1024);
    char    c, *buf = dev->callout->mem_alloc(dev->callout_ctx, len);

    if (buf == NULL) {
        return MEPA_RC_ERROR;
    }

    *buf = '\0';
    len--;
    rc = mepa_macsec_dbg_fcb_block_reg_print_buf(dev, port_no, (int)len, buf);
    // Print in chunks for backward compatibility
    while (i < len) {
        c = buf[i++];
        if (c == '\0') {
            (void)prntf("%s", buf + j);
            break;
        } else if (i == (j + 128U)) {
            c = buf[i];
            buf[i] = '\0';
            (void)prntf("%s", buf + j);
            buf[i] = c;
            j = i;
        } else {
            // Empty on purpose
        }
    }
    if (i >= len) {
        (void)prntf("\n--- Truncated due to buffer size ---\n");
    }
    dev->callout->mem_free(dev->callout_ctx, buf);
    return rc;
}

mepa_rc mepa_macsec_dbg_fcb_block_reg_print_buf(struct mepa_device *dev,
                                                const mepa_port_no_t port_no,
                                                int len,
                                                char *const buf)
{
    lmu_ss_t ss = {};
    mepa_rc rc;

    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_dbg_fcb_block_reg_dump == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    ss.buf.begin = buf;
    ss.buf.end = (buf + len);

    rc = dev->drv->mepa_macsec->mepa_driver_macsec_dbg_fcb_block_reg_dump(dev, port_no, &ss);
    if (ss.overflow != 0) {
        rc = MEPA_RC_ERROR;
    }

    return rc;
}

#pragma coverity compliance deviate                                            \
    "MISRA C-2023 Rule 5.1"                                                    \
    "MACsec API identifiers share long common prefixes; names are part of the public API"
mepa_rc mepa_macsec_dbg_frm_match_handling_ctrl_reg_dump(struct mepa_device *dev,
                                                         const mepa_port_no_t port_no,
                                                         const mepa_debug_print_t prntf)
{
    mepa_rc rc;
    size_t  i = 0, j = 0;
    size_t  len = (1024 * 1024);
    char    c, *buf = dev->callout->mem_alloc(dev->callout_ctx, len);

    if (buf == NULL) {
        return MEPA_RC_ERROR;
    }

    *buf = '\0';
    len--;
    rc = mepa_macsec_dbg_frm_match_handling_ctrl_reg_print_buf(dev, port_no, (int)len, buf);
    // Print in chunks for backward compatibility
    while (i < len) {
        c = buf[i++];
        if (c == '\0') {
            (void)prntf("%s", buf + j);
            break;
        } else if (i == (j + 128U)) {
            c = buf[i];
            buf[i] = '\0';
            (void)prntf("%s", buf + j);
            buf[i] = c;
            j = i;
        } else {
            // Empty on purpose
        }
    }
    if (i >= len) {
        (void)prntf("\n--- Truncated due to buffer size ---\n");
    }
    dev->callout->mem_free(dev->callout_ctx, buf);
    return rc;
}

#pragma coverity compliance deviate                                            \
    "MISRA C-2023 Rule 5.1"                                                    \
    "MACsec API identifiers share long common prefixes; names are part of the public API"
mepa_rc mepa_macsec_dbg_frm_match_handling_ctrl_reg_print_buf(struct mepa_device *dev,
                                                              const mepa_port_no_t port_no,
                                                              int len,
                                                              char *const buf)
{
    lmu_ss_t ss = {};
    mepa_rc rc;

    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_dbg_frm_match_handling_ctrl_reg_dump == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    ss.buf.begin = buf;
    ss.buf.end = (buf + len);

    rc = dev->drv->mepa_macsec->mepa_driver_macsec_dbg_frm_match_handling_ctrl_reg_dump(dev, port_no, &ss);
    if (ss.overflow != 0) {
        rc = MEPA_RC_ERROR;
    }

    return rc;
}

#ifdef MEPA_MACSEC_FIFO_OVERFLOW_WORKAROUND

mepa_rc mepa_macsec_dbg_reconfig(struct mepa_device *dev,
                                 const mepa_port_no_t port_no)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_dbg_reconfig == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_dbg_reconfig(dev, port_no);
}

#endif


mepa_rc mepa_macsec_dbg_update_seq_set(struct mepa_device *dev,
                                       const mepa_macsec_port_t port,
                                       const mepa_macsec_sci_t *const sci,
                                       uint16_t an,
                                       mepa_bool_t egr,
                                       const mepa_bool_t disable)
{
    if ((dev == NULL) || (dev->drv->mepa_macsec == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_macsec->mepa_driver_macsec_dbg_update_seq_set == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_macsec->mepa_driver_macsec_dbg_update_seq_set(dev, port, sci, an, egr, disable);
}

mepa_rc mepa_prbs_set(struct mepa_device *dev, mepa_phy_prbs_type_t type, mepa_phy_prbs_direction_t direction, const mepa_phy_prbs_generator_conf_t *const mepa_conf)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_prbs_set == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_prbs_set(dev, type, direction, mepa_conf);
}

mepa_rc mepa_prbs_get(struct mepa_device *dev, mepa_phy_prbs_type_t type, mepa_phy_prbs_direction_t direction, mepa_phy_prbs_generator_conf_t *const mepa_conf)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_prbs_get == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_prbs_get(dev, type, direction, mepa_conf);
}

mepa_rc mepa_prbs_monitor_set(struct mepa_device *dev, const mepa_phy_prbs_monitor_conf_t *const value)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_prbs_monitor_set == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_prbs_monitor_set(dev, value);
}

mepa_rc mepa_prbs_monitor_get(struct mepa_device *dev, mepa_phy_prbs_monitor_conf_t *const value)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_prbs_monitor_get == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_prbs_monitor_get(dev, value);
}

mepa_rc mepa_serdes_tx_conf_set(struct mepa_device *dev, const mepa_serdes_tx_conf_t *const tx_conf)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_serdes_tx_conf_set == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_serdes_tx_conf_set(dev, tx_conf);
}

uint32_t mepa_capability(struct mepa_device *dev, uint32_t capability)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_capability == NULL)) {
        return 0;
    }

    return dev->drv->mepa_driver_capability(dev, capability);
}

mepa_rc mepa_tc10_set_sleep_support(struct mepa_device          *dev,
                                    const mepa_bool_t           enable)
{
    if ((dev == NULL) || (dev->drv->mepa_tc10 == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_tc10->mepa_driver_tc10_set_sleep_support == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_tc10->mepa_driver_tc10_set_sleep_support(dev, enable);
}

mepa_rc mepa_tc10_get_sleep_support(struct mepa_device        *dev,
                                    mepa_bool_t               *const enable)
{
    if ((dev == NULL) || (dev->drv->mepa_tc10 == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_tc10->mepa_driver_tc10_get_sleep_support == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_tc10->mepa_driver_tc10_get_sleep_support(dev, enable);
}

mepa_rc mepa_tc10_set_wakeup_support(struct mepa_device                 *dev,
                                     const mepa_tc10_wakeup_mode_t      mode)
{
    if ((dev == NULL) || (dev->drv->mepa_tc10 == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_tc10->mepa_driver_tc10_set_wakeup_support == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_tc10->mepa_driver_tc10_set_wakeup_support(dev, mode);
}

mepa_rc mepa_tc10_get_wakeup_support(struct mepa_device                 *dev,
                                     mepa_tc10_wakeup_mode_t            *const mode)
{
    if ((dev == NULL) || (dev->drv->mepa_tc10 == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_tc10->mepa_driver_tc10_get_wakeup_support == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_tc10->mepa_driver_tc10_get_wakeup_support(dev, mode);
}


mepa_rc mepa_tc10_set_wakeup_fwd_support(struct mepa_device                     *dev,
                                         const mepa_tc10_wakeup_fwd_mode_t      mode)
{
    if ((dev == NULL) || (dev->drv->mepa_tc10 == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_tc10->mepa_driver_tc10_set_wakeup_fwd_support == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_tc10->mepa_driver_tc10_set_wakeup_fwd_support(dev, mode);
}

mepa_rc mepa_tc10_get_wakeup_fwd_support(struct mepa_device                     *dev,
                                         mepa_tc10_wakeup_fwd_mode_t            *const mode)
{
    if ((dev == NULL) || (dev->drv->mepa_tc10 == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_tc10->mepa_driver_tc10_get_wakeup_fwd_support == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_tc10->mepa_driver_tc10_get_wakeup_fwd_support(dev, mode);
}

mepa_rc mepa_tc10_set_wake_pin_polarity(struct mepa_device              *dev,
                                        const mepa_tc10_pin_t           pin,
                                        const mepa_gpio_mode_t          polarity)
{
    if ((dev == NULL) || (dev->drv->mepa_tc10 == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_tc10->mepa_driver_tc10_set_wake_pin_polarity == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_tc10->mepa_driver_tc10_set_wake_pin_polarity(dev, pin, polarity);
}

mepa_rc mepa_tc10_get_wake_pin_polarity(struct mepa_device              *dev,
                                        const mepa_tc10_pin_t           pin,
                                        mepa_gpio_mode_t                *const polarity)
{
    if ((dev == NULL) || (dev->drv->mepa_tc10 == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_tc10->mepa_driver_tc10_get_wake_pin_polarity == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_tc10->mepa_driver_tc10_get_wake_pin_polarity(dev, pin, polarity);
}

mepa_rc mepa_tc10_set_pin_mode(struct mepa_device           *dev,
                               const mepa_tc10_pin_t        pin,
                               const mepa_gpio_mode_t       mode)
{
    if ((dev == NULL) || (dev->drv->mepa_tc10 == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_tc10->mepa_driver_tc10_set_pin_mode == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_tc10->mepa_driver_tc10_set_pin_mode(dev, pin, mode);
}

mepa_rc mepa_tc10_get_pin_mode(struct mepa_device           *dev,
                               const mepa_tc10_pin_t        pin,
                               mepa_gpio_mode_t             *const mode)
{
    if ((dev == NULL) || (dev->drv->mepa_tc10 == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_tc10->mepa_driver_tc10_get_pin_mode == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_tc10->mepa_driver_tc10_get_pin_mode(dev, pin, mode);
}

mepa_rc mepa_tc10_send_sleep_request(struct mepa_device                     *dev,
                                     const mepa_tc10_sleep_request_t        req)
{
    if ((dev == NULL) || (dev->drv->mepa_tc10 == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_tc10->mepa_driver_tc10_send_sleep_request == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_tc10->mepa_driver_tc10_send_sleep_request(dev, req);
}

mepa_rc mepa_tc10_get_state(struct mepa_device      *dev,
                            mepa_tc10_state_t       *const state,
                            uint16_t            *const indication)
{
    if ((dev == NULL) || (dev->drv->mepa_tc10 == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_tc10->mepa_driver_tc10_get_state == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_tc10->mepa_driver_tc10_get_state(dev, state, indication);
}

mepa_rc mepa_tc10_send_wake_request(struct mepa_device *dev)
{
    if ((dev == NULL) || (dev->drv->mepa_tc10 == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_tc10->mepa_driver_tc10_send_wake_request == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_tc10->mepa_driver_tc10_send_wake_request(dev);
}

mepa_rc mepa_warmstart_conf_end(struct mepa_device *dev)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_warmrestart_conf_end == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_warmrestart_conf_end(dev);

}

mepa_rc mepa_warmstart_conf_get(struct mepa_device *dev, mepa_restart_t *const restart)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_warmrestart_conf_get == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_warmrestart_conf_get(dev, restart);

}

mepa_rc mepa_warmstart_conf_set(struct mepa_device *dev, const mepa_restart_t restart)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_warmrestart_conf_set == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_warmrestart_conf_set(dev, restart);

}

mepa_rc mepa_phy_qsgmii_sync(struct mepa_device *dev)
{
    if ((dev == NULL) || (dev->drv->mepa_driver_phy_qsgmii_sync == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_driver_phy_qsgmii_sync(dev);

}

mepa_rc mepa_t1s_set_plca_config(struct mepa_device *dev,
                                 const mepa_t1s_plca_cfg_t cfg)
{
    if ((dev == NULL) || (dev->drv->mepa_t1s == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_t1s->mepa_driver_t1s_set_plca_config == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_t1s->mepa_driver_t1s_set_plca_config(dev, cfg);
}

mepa_rc mepa_t1s_get_plca_config(struct mepa_device *dev,
                                 mepa_t1s_plca_cfg_t *const cfg)
{
    if ((dev == NULL) || (dev->drv->mepa_t1s == NULL)) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    if (dev->drv->mepa_t1s->mepa_driver_t1s_get_plca_config == NULL) {
        return MESA_RC_NOT_IMPLEMENTED;
    }

    return dev->drv->mepa_t1s->mepa_driver_t1s_get_plca_config(dev, cfg);
}

#if defined(MEPA_OPSYS_VELOCITYSP)
static void mepa_trace_buf_init(lmu_fmt_state_buf128_t *buf, const char *fmt)
{
    lmu_fmt_state_buf128_init(buf, fmt);
    buf->ss.buf.end--;
    *buf->ss.buf.end = '\0';
}

#define MEPA_TRACE_TYPE_X(TYPE, BASE, SINGLE, FIRST, LAST)                                         \
    void SINGLE(const mepa_trace_group_t group,                    \
                const mepa_trace_level_t level, const char *func, uint32_t line,                   \
                const char *file, const char *fmt, const TYPE val)                                 \
    {                                                                                              \
        lmu_fmt_state_buf128_t lmu_fmt_state__;                                                    \
        mepa_trace_buf_init(&lmu_fmt_state__, fmt);                                                \
        BASE(&lmu_fmt_state__.state, val);                                                         \
        MEPA_trace(group, level, func, line, file, lmu_fmt_state__.ss.buf.begin);                  \
    }                                                                                              \
    bool FIRST(const mepa_trace_group_t group,                                                     \
               const mepa_trace_level_t level, const char *fmt, lmu_fmt_state_buf128_t *state,     \
               const TYPE val)                                                                     \
    {                                                                                              \
        mepa_trace_buf_init(state, fmt);                                                           \
        BASE(&state->state, val);                                                                  \
        return true;                                                                               \
    }                                                                                              \
    void LAST(const mepa_trace_group_t group,                                                      \
              const mepa_trace_level_t level, const char *func, uint32_t line, const char *file,   \
              lmu_fmt_state_t *state, const TYPE val)                                              \
    {                                                                                              \
        BASE(state, val);                                                                          \
        MEPA_trace(group, level, func, line, file, state->ss->buf.begin);                          \
    }
MEPA_TRACE_TYPES
#undef MEPA_TRACE_TYPE_X
#endif
