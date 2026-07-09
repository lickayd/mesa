// Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#include <microchip/ethernet/phy/api.h>
#include <mepa_driver.h>
#include <mepa_macsec_driver.h>
#include <mepa_ts_driver.h>
#include <mepa_utils.h>
#include <stdarg.h>
#include <stdio.h>
#include <vtss_phy_api.h>
#include "vtss_private.h"
#include "phy_1g/vtss_phy.h"
#include "phy_10g/vtss_phy_10g.h"

#define VTSS_TRACE_GROUP VTSS_TRACE_GROUP_PHY

#define VTSS_MACSEC_10G_MAX_SA MEPA_MACSEC_10G_MAX_SA
#define VTSS_MACSEC_1G_MAX_SA  MEPA_MACSEC_1G_MAX_SA

#include "common/vtss_phy_common.h"

extern mepa_ts_driver_t vtss_ts_drivers;
extern mepa_macsec_driver_t vtss_macsec_drivers;
static vtss_inst_t vtss_inst;
static int vtss_inst_cnt;

// MIIM/MMD wrapper functions
static vtss_rc miim_read(vtss_state_t        *inst,
                         const vtss_port_no_t port_no,
                         const u8             addr,
                         u16                  *const value)
{
    return inst->callout[port_no]->miim_read(inst->callout_ctx[port_no], addr, value);
}

static vtss_rc miim_write(vtss_state_t        *inst,
                          const vtss_port_no_t port_no,
                          const u8             addr,
                          const u16            value)
{
    return inst->callout[port_no]->miim_write(inst->callout_ctx[port_no], addr, value);
}

static vtss_rc mmd_read(vtss_state_t        *inst,
                        const vtss_port_no_t port_no,
                        const u8             mmd,
                        const u16            addr,
                        u16                  *const value)
{
    return inst->callout[port_no]->mmd_read(inst->callout_ctx[port_no], mmd, addr, value);
}

static vtss_rc mmd_read_inc(vtss_state_t        *inst,
                            const vtss_port_no_t port_no,
                            const u8             mmd,
                            const u16            addr,
                            u16                  *const buf,
                            u8                   cnt)
{
    return inst->callout[port_no]->mmd_read_inc(inst->callout_ctx[port_no], mmd, addr, buf,
            cnt);
}

static vtss_rc mmd_write(vtss_state_t        *inst,
                         const vtss_port_no_t port_no,
                         const u8             mmd,
                         const u16            addr,
                         const u16            value)
{
    return inst->callout[port_no]->mmd_write(inst->callout_ctx[port_no], mmd, addr, value);
}

static vtss_rc spi_read_write (const vtss_inst_t inst,
                               vtss_port_no_t port_no,
                               BOOL           read,
                               u8             dev,
                               u16            reg_num,
                               u32            *const data)
{

    if(read) {
        return inst->callout[port_no]->spi_read(inst->callout_ctx[port_no], port_no, dev, reg_num, data);
    }
    return inst->callout[port_no]->spi_write(inst->callout_ctx[port_no], port_no, dev, reg_num, data);
}

static void trace_func(const vtss_phy_trace_group_t group,
                       const vtss_phy_trace_level_t level,
                       const char                   *location,
                       const int                    line,
                       const char                   *format,
                       ...)
{
    mepa_trace_data_t data = {
        .location = location,
        .line = line,
        .format = format,
    };

    va_list args;

    if (MEPA_TRACE_FUNCTION) {
        // There must be a one-to-one-mapping between vtss_phy_trace_group_t and
        // mepa_trace_group_t, or this piece of code won't work.
        data.group = group;
        data.level = (level == VTSS_PHY_TRACE_LEVEL_ERROR ? MEPA_TRACE_LVL_ERROR :
                      level == VTSS_PHY_TRACE_LEVEL_INFO ? MEPA_TRACE_LVL_INFO :
                      level == VTSS_PHY_TRACE_LEVEL_DEBUG ? MEPA_TRACE_LVL_DEBUG :
                      MEPA_TRACE_LVL_NOISE);


        va_start(args, format);
        MEPA_TRACE_FUNCTION(&data, args);
        va_end(args);
    }
}

#if defined (VTSS_FEATURE_MACSEC)
static vtss_rc vtss_macsec_port_mem_free(const mepa_callout_t    *callout,
        struct mepa_callout_ctx *callout_ctx,
        vtss_inst_t             *inst,
        uint32_t                 port_no)
{
    vtss_state_t *vtss_state = *inst;
    u32 max_secy = vtss_state->macsec_capability[port_no].max_secy_cnt;
    VTSS_I("Deallocating MACsec Memory");

    if(vtss_state->macsec_conf[port_no].rx_sa != NULL) {
        mepa_mem_free_int(callout, callout_ctx, vtss_state->macsec_conf[port_no].rx_sa);
        vtss_state->macsec_conf[port_no].rx_sa = NULL;
    }

    if(vtss_state->macsec_conf[port_no].tx_sa != NULL) {
        mepa_mem_free_int(callout, callout_ctx, vtss_state->macsec_conf[port_no].tx_sa);
        vtss_state->macsec_conf[port_no].tx_sa = NULL;
    }
    /* The Memory for rx_sc is allocated as a bulk for all SecY's so deallocating the first SecY memory will free the entire rx_sc */
    if(vtss_state->macsec_conf[port_no].secy[0].rx_sc != NULL) {
        mepa_mem_free_int(callout, callout_ctx, vtss_state->macsec_conf[port_no].secy[0].rx_sc);
        for(u8 j = 0; j < max_secy; j++) {
            vtss_state->macsec_conf[port_no].secy[j].rx_sc = NULL;
        }
    }

    if(vtss_state->macsec_conf[port_no].rx_sc != NULL) {
        mepa_mem_free_int(callout, callout_ctx, vtss_state->macsec_conf[port_no].rx_sc);
        vtss_state->macsec_conf[port_no].rx_sc = NULL;
    }

    if(vtss_state->macsec_conf[port_no].secy != NULL) {
        mepa_mem_free_int(callout, callout_ctx, vtss_state->macsec_conf[port_no].secy);
        vtss_state->macsec_conf[port_no].secy = NULL;
    }
    if(vtss_state->macsec_capability[port_no].inst_counts.secy_vport != NULL) {
        mepa_mem_free_int(callout, callout_ctx, vtss_state->macsec_capability[port_no].inst_counts.secy_vport);
        vtss_state->macsec_capability[port_no].inst_counts.secy_vport = NULL;
    }

    if(vtss_state->macsec_capability[port_no].inst_counts.secy_inst_count != NULL) {
        if(vtss_state->macsec_capability[port_no].inst_counts.secy_inst_count->rxsc_id != NULL) {
            mepa_mem_free_int(callout, callout_ctx, vtss_state->macsec_capability[port_no].inst_counts.secy_inst_count->rxsc_id);
            vtss_state->macsec_capability[port_no].inst_counts.secy_inst_count->rxsc_id = NULL;
        }

        if(vtss_state->macsec_capability[port_no].inst_counts.secy_inst_count->rx_sci != NULL) {
            mepa_mem_free_int(callout, callout_ctx, vtss_state->macsec_capability[port_no].inst_counts.secy_inst_count->rx_sci);
            vtss_state->macsec_capability[port_no].inst_counts.secy_inst_count->rx_sci = NULL;
        }

        if(vtss_state->macsec_capability[port_no].inst_counts.secy_inst_count->rxsc_inst_count != NULL) {
            mepa_mem_free_int(callout, callout_ctx, vtss_state->macsec_capability[port_no].inst_counts.secy_inst_count->rxsc_inst_count);
            vtss_state->macsec_capability[port_no].inst_counts.secy_inst_count->rxsc_inst_count = NULL;
        }
        mepa_mem_free_int(callout, callout_ctx, vtss_state->macsec_capability[port_no].inst_counts.secy_inst_count);
        vtss_state->macsec_capability[port_no].inst_counts.secy_inst_count = NULL;
    }
    return MEPA_RC_OK;
}

static vtss_rc vtss_macsec_port_mem_alloc(const mepa_callout_t    *callout,
        struct mepa_callout_ctx *callout_ctx,
        vtss_inst_t             *inst,
        uint32_t                 port_no)
{
    vtss_state_t *vtss_state = *inst;
    u32 max_secy,max_sa, max_sc;
    u32 phy_id = mepa_phy_id_get(callout, callout_ctx, port_no);
    mepa_bool_t is_phy_1g;
    vtss_macsec_internal_secy_t *secy;
    vtss_macsec_internal_secy_t *macsec_conf_secy = NULL;
    vtss_macsec_internal_rx_sc_t *macsec_conf_rx_sc = NULL;
    vtss_macsec_internal_rx_sa_t *macsec_conf_rx_sa = NULL;
    vtss_macsec_internal_tx_sa_t *macsec_conf_tx_sa = NULL;
    vtss_macsec_internal_rx_sc_t **secy_rx_sc = NULL;

    uint8_t *rxsc_id = NULL;
    vtss_macsec_sci_t *sci = NULL;
    vtss_sc_inst_count_t *sc_inst = NULL;
    u8 *secy_vport = NULL;
    vtss_secy_inst_count_t *secy_cnt = NULL;
    VTSS_I("Memory allocation for MACsec Port on port number : %d", port_no);
    /* VSC PHY's which supports MACsec */
    if((phy_id >> 4) == VTSS_PHY_VIPER_ID) {
        VTSS_I("The Connect PHY is viper on port no : %d", port_no);
        is_phy_1g = TRUE;
    } else if(phy_id == VTSS_PHY_MALIBU10G_8258_ID || phy_id == VTSS_PHY_MALIBU10G_8254_ID) { /* Skew number of Malibu10G which supports MACsec */
        VTSS_I("The Connect PHY is Malibu 10G on port no : %d", port_no);
        is_phy_1g = FALSE;
    } else {
        VTSS_I("The PHY is not MACsec capable");
        return MEPA_RC_OK;
    }

    if (is_phy_1g) {
        max_sc = VTSS_MACSEC_1G_MAX_SA / 2;
        max_sa = VTSS_MACSEC_1G_MAX_SA;
        max_secy = VTSS_MACSEC_1G_MAX_SA / 2;
    } else {
        max_sc = VTSS_MACSEC_10G_MAX_SA / 2;
        max_sa = VTSS_MACSEC_10G_MAX_SA;
        max_secy = VTSS_MACSEC_10G_MAX_SA / 2;
    }

    /* Allocating Memory to vtss_macsec_internal_conf_t structure depending on PHY connected on the Port */
    if((macsec_conf_secy = mepa_mem_alloc_int(callout, callout_ctx, sizeof(vtss_macsec_internal_secy_t) * max_secy)) == 0) {
        VTSS_E("Error in allocating memory for SecY on port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    vtss_state->macsec_conf[port_no].secy = macsec_conf_secy;
    memset(vtss_state->macsec_conf[port_no].secy, 0 , sizeof(vtss_macsec_internal_secy_t) * max_secy);
    vtss_state->macsec_capability[port_no].max_secy_cnt = max_secy; /* Storing MAX secy */

    /* Maximum Secure channel memory allocation  on a port*/
    if((macsec_conf_rx_sc = mepa_mem_alloc_int(callout, callout_ctx, sizeof(vtss_macsec_internal_rx_sc_t) * max_sc)) == 0) {
        VTSS_E("Error in allocating memory for Rx Secure channel on port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    vtss_state->macsec_conf[port_no].rx_sc = macsec_conf_rx_sc;
    memset(vtss_state->macsec_conf[port_no].rx_sc, 0 , sizeof(vtss_macsec_internal_rx_sc_t) * max_sc);
    vtss_state->macsec_capability[port_no].max_sc_cnt = max_sc;  /* Storing MAX SC */

    /* Memory allocation for Receive secure channel */
    secy_rx_sc = mepa_mem_alloc_int(callout, callout_ctx, sizeof(vtss_macsec_internal_rx_sc_t) * max_sc * max_secy);
    if(!secy_rx_sc) {
        VTSS_E("Error in allocating memory for RX SC on SecY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    memset(secy_rx_sc, 0 , sizeof(vtss_macsec_internal_rx_sc_t) * max_sc * max_secy);
    for(u8 j = 0; j < max_secy; j++) {
        secy = &vtss_state->macsec_conf[port_no].secy[j];
        secy->rx_sc = secy_rx_sc + (max_sc * j);
        vtss_state->macsec_conf[port_no].secy[j].rx_sc = secy->rx_sc;
    }

    /* Maximum Secure Assosiation allocation on Port */
    if((macsec_conf_tx_sa = mepa_mem_alloc_int(callout, callout_ctx, sizeof(vtss_macsec_internal_tx_sa_t) * max_sa)) == 0) {
        VTSS_E("Error in allocating memory for Tx Secure assosiation on port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    vtss_state->macsec_conf[port_no].tx_sa = macsec_conf_tx_sa;
    memset(vtss_state->macsec_conf[port_no].tx_sa, 0 , sizeof(vtss_macsec_internal_tx_sa_t) * max_sa);

    if((macsec_conf_rx_sa = mepa_mem_alloc_int(callout, callout_ctx, sizeof(vtss_macsec_internal_rx_sa_t) * max_sa)) == 0) {
        VTSS_E("Error in allocating memory for Rx Secure assosiation on port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    vtss_state->macsec_conf[port_no].rx_sa = macsec_conf_rx_sa;
    memset(vtss_state->macsec_conf[port_no].rx_sa, 0 , sizeof(vtss_macsec_internal_rx_sa_t) * max_sa);
    vtss_state->macsec_capability[port_no].max_sa_cnt = max_sa; /* Storing MAX SA */

    /* Allocating memory to vtss_macsec_inst_count_t structure */
    secy_cnt = mepa_mem_alloc_int(callout, callout_ctx, sizeof(vtss_secy_inst_count_t) * max_secy);
    if(!secy_cnt) {
        VTSS_E("Error in allocating memory to Secy Instance count on port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    memset(secy_cnt, 0 , sizeof(vtss_secy_inst_count_t));
    vtss_macsec_inst_count_t  inst_counts = {0};

    rxsc_id = mepa_mem_alloc_int(callout, callout_ctx, sizeof(uint8_t) * max_sc * max_secy);
    if(!rxsc_id) {
        VTSS_E("Error in allocating memory for Rx SC Id on port : %d", port_no);
        goto macsec_mem_dealloc;
    }
    memset(rxsc_id, 0 ,sizeof(uint8_t) * max_sc * max_secy);

    sci = mepa_mem_alloc_int(callout, callout_ctx, sizeof(vtss_macsec_sci_t) * max_sc * max_secy);
    if(!sci) {
        VTSS_E("Error in allocating memory to SCI on port : %d", port_no);
        goto macsec_mem_dealloc;
    }
    memset(sci, 0 , sizeof(vtss_macsec_sci_t) * max_sc *max_secy);

    sc_inst = mepa_mem_alloc_int(callout, callout_ctx, sizeof(vtss_sc_inst_count_t) * max_sc * max_secy);
    if(!sc_inst) {
        VTSS_E("Error in allocating memory to SC Instance : %d", port_no);
        goto macsec_mem_dealloc;
    }
    memset(sc_inst, 0 , sizeof(vtss_sc_inst_count_t) * max_sc * max_secy);

    for(u8 c = 0; c < max_secy; c++) {
        secy_cnt[c].rxsc_id = rxsc_id + (max_sc * c);
        secy_cnt[c].rx_sci = sci + (max_sc * c);
        secy_cnt[c].rxsc_inst_count = sc_inst + (max_sc * c);
    }

    secy_vport = mepa_mem_alloc_int(callout, callout_ctx, sizeof(uint8_t) * max_secy);
    if(!secy_vport) {
        VTSS_E("Error in allocating memory to SecY virtual port count on port : %d", port_no);
        goto macsec_mem_dealloc;
    }
    memset(secy_vport, 0 , sizeof(uint8_t) * max_secy);
    inst_counts.secy_vport = secy_vport;
    inst_counts.secy_inst_count = secy_cnt;
    memcpy(&vtss_state->macsec_capability[port_no].inst_counts, &inst_counts, sizeof(vtss_macsec_inst_count_t));

    return MEPA_RC_OK;

macsec_mem_dealloc:
    if(sc_inst != NULL) {
        mepa_mem_free_int(callout, callout_ctx, sc_inst);
    }
    if(sci != NULL) {
        mepa_mem_free_int(callout, callout_ctx, sci);
    }
    if(rxsc_id != NULL) {
        mepa_mem_free_int(callout, callout_ctx, rxsc_id);
    }
    if(secy_cnt != NULL) {
        mepa_mem_free_int(callout, callout_ctx, secy_cnt);
    }
    if(secy_vport != NULL) {
        mepa_mem_free_int(callout, callout_ctx, secy_vport);
    }
    return MEPA_RC_ERROR;
}
#endif

static mepa_rc mscc_vtss_create(const mepa_callout_t    MEPA_SHARED_PTR *callout,
                                struct mepa_callout_ctx MEPA_SHARED_PTR *callout_ctx,
                                struct mepa_board_conf              *board_conf)
{
    vtss_rc              rc;
    vtss_inst_t          inst;
    vtss_phy_init_conf_t conf;
    // Check that port does not exceed PHY instance maximum
    if (board_conf->numeric_handle >= VTSS_PORTS) {
        return MEPA_RC_ERROR;
    }
    // Using and creating instance are mutual exclusive.
    if (board_conf->vtss_instance_create && board_conf->vtss_instance_use) {
        return MEPA_RC_ERROR;
    }

    if (board_conf->vtss_instance_use) {
        // Notice - NULL is a valid instance!
        inst = board_conf->vtss_instance_ptr;
    } else if (vtss_inst_cnt == 0 || board_conf->vtss_instance_create) {
        // Check that PHY instance can be created
        if (board_conf->vtss_instance_create) {
            // Instances are externally managed - we need to create it on their
            // behalf
            rc = vtss_phy_inst_create(callout, callout_ctx,
                                      &board_conf->vtss_instance_ptr);
            inst = board_conf->vtss_instance_ptr;
        } else {
            rc = vtss_phy_inst_create(callout, callout_ctx, &vtss_inst);
            inst = vtss_inst;
        }
        if (rc != VTSS_RC_OK) {
            // Failed to create instance!
            return MEPA_RC_ERROR;
        }

        // Get default config
        if (vtss_phy_init_conf_get(inst, &conf) != VTSS_RC_OK) {
            return MEPA_RC_ERROR;
        }

        // Use the pre-cooked handler functions which just delegate to the
        // callout outs.
        conf.miim_read = miim_read;
        conf.miim_write = miim_write;
        conf.mmd_read = mmd_read;
        conf.mmd_read_inc = mmd_read_inc;
        conf.mmd_write = mmd_write;
        conf.trace_func = trace_func;
        if(callout->spi_read && callout->spi_write)
            conf.spi_32bit_read_write = spi_read_write;
        // No need for delegate as the callouts are binary compatible
        conf.lock_enter = callout->lock_enter;
        conf.lock_exit = callout->lock_exit;

        // Apply the callouts
        (void)vtss_phy_init_conf_set(inst, &conf);
    } else {
        // Both vtss_instance_use and vtss_instance_create are null, and we have
        // created the instance already.
        inst = vtss_inst;
    }

    if (vtss_phy_callout_set(inst, board_conf->numeric_handle, callout, callout_ctx) == MEPA_RC_OK) {
        vtss_inst_cnt++;
    } else {
        return MEPA_RC_ERROR;
    }
#if defined (VTSS_FEATURE_MACSEC)
    /* Allocating memory for MACsec Port */
    if(vtss_macsec_port_mem_alloc(callout, callout_ctx, &inst, board_conf->numeric_handle) != MEPA_RC_OK) {
        (void)vtss_macsec_port_mem_free(callout, callout_ctx, &inst, board_conf->numeric_handle);
        VTSS_E("Error in configuring Memory for MACsec Port Port no : %d", board_conf->numeric_handle);
        return MEPA_RC_ERROR;
    }
#endif

    // Always ensure that the used instance is returned
    board_conf->vtss_instance_ptr = inst;

    return MEPA_RC_OK;
}

static mepa_rc mscc_vtss_destroy(mepa_device_t *dev)
{
    if (vtss_inst_cnt) {
        if (vtss_phy_callout_del(vtss_inst, dev->numeric_handle) == VTSS_RC_OK) {
            vtss_inst_cnt--;
        } else {
            return MEPA_RC_ERROR;
        }
#if defined (VTSS_FEATURE_MACSEC)
        if(vtss_macsec_port_mem_free(dev->callout, dev->callout_ctx, &vtss_inst, dev->numeric_handle) != VTSS_RC_OK) {
            VTSS_E("Error in Destroying allocated MACsec memory");
            return MEPA_RC_ERROR;
        }
#endif
        if (vtss_inst_cnt == 0 && vtss_phy_inst_destroy(dev->callout, dev->callout_ctx, vtss_inst) != VTSS_RC_OK) {
            return MEPA_RC_ERROR;
        }
    }
    return MEPA_RC_OK;
}

static mepa_rc mscc_1g_delete(mepa_device_t *dev)
{
    (void)mscc_vtss_destroy(dev);
    return mepa_delete_int(dev);
}

static mepa_rc reset_phy(phy_data_t *data, vtss_phy_reset_conf_t *conf)
{
    mepa_event_t events;

    MEPA_RC(vtss_phy_event_enable_get(data->vtss_instance, data->port_no, &events));
    MEPA_RC(vtss_phy_reset(data->vtss_instance, data->port_no, conf));
    return vtss_phy_event_enable_set(data->vtss_instance, data->port_no, events, 1);
}

static mepa_rc mscc_1g_reset(mepa_device_t *dev,
                             const mepa_reset_param_t *rst_conf)
{
    vtss_phy_reset_conf_t conf = {};
    phy_data_t *data = (phy_data_t *)(dev->data);
    mepa_rc rc = MEPA_RC_OK;
    data->temp_init_flag = false;
    switch (rst_conf->reset_point) {
    case MEPA_RESET_POINT_PRE:
        /* MEPA-823 - Base port handling is done only in PRE_RESET and POST_RESET */
        if(data->base_dev!= NULL) {
            mepa_device_t *base_dev = (mepa_device_t *)data->base_dev;
            phy_data_t *base_data = (phy_data_t*)base_dev->data;
            if((data->port_no) == (base_data->port_no)) {
                rc = vtss_phy_pre_reset(data->vtss_instance, data->port_no);
            }
        }
        else {
            T_E(MEPA_TRACE_GRP_GEN, "Base Dev is not linked for the port %d",data->port_no);
            return MEPA_RC_ERROR;
        }
        break;
    case MEPA_RESET_POINT_DEFAULT:
        /* MEPA-823 - DEFAULT_RESET is allowed for all ports inlcuding NPI Port without Base Port check */
        vtss_phy_reset_get(data->vtss_instance, data->port_no, &conf);
        conf.force = VTSS_PHY_FORCE_RESET;
        conf.mac_if = data->mac_if;
        conf.media_if = rst_conf->media_intf;
        conf.i_cpu_en = 0;
        rc = reset_phy(data, &conf);
        break;
    case MEPA_RESET_POINT_POST:
        /* MEPA-823 - Base port handling is done only in PRE_RESET and POST_RESET */
        if(data->base_dev!= NULL) {
            mepa_device_t *base_dev = (mepa_device_t *)data->base_dev;
            phy_data_t *base_data = (phy_data_t*)base_dev->data;
            if((data->port_no) == (base_data->port_no)) {
                rc = vtss_phy_post_reset(data->vtss_instance, data->port_no);
            }
        }
        else {
            T_E(MEPA_TRACE_GRP_GEN, "Base Dev is not linked for the port %d",data->port_no);
            return MEPA_RC_ERROR;
        }
        break;
    default:
        break;
    }
    return rc;
}

static mepa_rc phy_1g_warmrestart_conf_set(struct mepa_device *dev, const mepa_restart_t restart)
{
    mepa_rc rc = MEPA_RC_OK;
    phy_data_t *data =(phy_data_t*)dev->data;
    mepa_device_t *base_dev = (mepa_device_t *)data->base_dev;
    phy_data_t *base_data = (phy_data_t*)base_dev->data;
    data->vtss_instance->restart_cur = restart;
    data->vtss_instance->init_conf.restart_info_port = base_data->port_no;
    data->vtss_instance->init_conf.warm_start_enable = TRUE;
    data->vtss_instance->init_conf.restart_info_src = VTSS_RESTART_INFO_SRC_CU_PHY;
    if((rc = vtss_phy_restart_conf_set(data->vtss_instance)) != MEPA_RC_OK) {
        return rc;
    }
    return MEPA_RC_OK;
}

static mepa_rc phy_1g_warmrestart_conf_end(struct mepa_device *dev)
{
    mepa_rc rc = MEPA_RC_OK;
    phy_data_t *data =(phy_data_t *)dev->data;
    mepa_device_t *base_dev = (mepa_device_t *)data->base_dev;
    phy_data_t *base_data = (phy_data_t *)base_dev->data;
    data->vtss_instance->init_conf.restart_info_port = base_data->port_no;
    data->vtss_instance->init_conf.warm_start_enable = TRUE;
    data->vtss_instance->init_conf.restart_info_src = VTSS_RESTART_INFO_SRC_CU_PHY;
    if(data->vtss_instance->warm_start_cur) {
        data->vtss_instance->warm_start_cur = 0; /* To sync up the registers with the previous instance */

        /* Apply sync configurations */
        if((rc = vtss_phy_sync(data->vtss_instance, base_data->port_no) != MEPA_RC_OK)) {
            T_D(MEPA_TRACE_GRP_GEN, "vtss_phy_10g_sync port(%d) return rc(0x%04X)", base_data->port_no, rc);
            return rc;
        }
#if defined (VTSS_FEATURE_PHY_TIMESTAMP)
        if((rc = vtss_phy_ts_sync(data->vtss_instance, base_data->port_no)) != MEPA_RC_OK) {
            T_D(MEPA_TRACE_GRP_GEN, "vtss_phy_ts_sync port(%d) return rc(0x%04X)", base_data->port_no, rc);
            return rc;
        }
#endif /* VTSS_FEATURE_PHY_TIMESTAMP */
#if defined (VTSS_FEATURE_MACSEC)
        if((rc = vtss_macsec_sync(data->vtss_instance, base_data->port_no)) != MEPA_RC_OK) {
            T_D(MEPA_TRACE_GRP_GEN, "vtss_macsec_sync port(%d) return rc(0x%04X)", base_data->port_no, rc);
            return rc;
        }
#endif /* VTSS_FEATURE_MACSEC */
    }
    if((rc = vtss_phy_restart_conf_set(data->vtss_instance)) != MEPA_RC_OK ) {
        return rc;
    }

    return MEPA_RC_OK;
}

static mepa_rc phy_1g_warmrestart_conf_get(struct mepa_device *dev, mepa_restart_t *const restart)
{
    mepa_rc rc = MEPA_RC_OK;
    phy_data_t *data =(phy_data_t*)dev->data;
    *restart = data->vtss_instance->restart_cur;
    return rc;
}

static mepa_rc mscc_1g_poll(mepa_device_t *dev,
                            mepa_status_t *status)
{
    phy_data_t *data = (phy_data_t *)dev->data;
    vtss_port_status_t mesa_status = {};

    mepa_rc rc = vtss_phy_status_get(data->vtss_instance, data->port_no, &mesa_status);
    if (rc != MEPA_RC_OK) {
        return rc;
    }

    // fill up status
    status->link = mesa_status.link;
    status->speed = mesa_status.speed;
    status->fdx = mesa_status.fdx;
    status->aneg.obey_pause = mesa_status.aneg.obey_pause;
    status->aneg.generate_pause = mesa_status.aneg.generate_pause;
    status->copper = mesa_status.copper;
    status->fiber = mesa_status.fiber;

    return MEPA_RC_OK;
}

static mepa_rc mscc_1g_conf_set(mepa_device_t *dev, const mepa_conf_t *config)
{
    phy_data_t *data = (phy_data_t *)dev->data;
    vtss_phy_conf_t phy_config = {};
    vtss_phy_conf_1g_t cfg_neg = {};
    mepa_rc rc = MEPA_RC_OK;
    if (vtss_phy_conf_get(data->vtss_instance, data->port_no, &phy_config) == MESA_RC_OK) {
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

        /* Fix for MEPA-233 and MEPA-296 */
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
        }
        // We don't support 1G half duplex
        phy_config.aneg.speed_1g_hdx = 0;
        phy_config.aneg.symmetric_pause = config->flow_control;
        phy_config.aneg.asymmetric_pause = config->flow_control;

        // manual negotiation
        if (config->man_neg) {
            cfg_neg.master.cfg = true;
            cfg_neg.master.val = config->man_neg == MEPA_MANUAL_NEG_REF ? true : false;
        } else {
            cfg_neg.master.cfg = false;
            cfg_neg.master.val = MEPA_MANUAL_NEG_DISABLED;
        }

        // Force AMS Media Select MEPA:104
        if (config->force_ams_mode_sel) {
            phy_config.force_ams_sel = config->force_ams_mode_sel == MEPA_PHY_MEDIA_FORCE_AMS_SEL_SERDES ?
                                       MEPA_PHY_MEDIA_FORCE_AMS_SEL_SERDES : MEPA_PHY_MEDIA_FORCE_AMS_SEL_COPPER;
        }
        else {
            phy_config.force_ams_sel = MEPA_PHY_MEDIA_FORCE_AMS_SEL_NORMAL;
        }

        rc = vtss_phy_conf_1g_set(data->vtss_instance, data->port_no, &cfg_neg);
        if (rc != MEPA_RC_OK) {
            T_E(MEPA_TRACE_GRP_GEN, "Failed to confiured speed\n");
            return MEPA_RC_ERROR;
        }
        phy_config.forced.speed = config->speed;
        phy_config.forced.fdx = config->fdx;

        if (phy_config.mac_if_pcs.serdes_aneg_ena != config->mac_if_aneg_ena) {
            phy_config.mac_if_pcs.aneg_restart = true;
        }
        phy_config.mac_if_pcs.serdes_aneg_ena = config->mac_if_aneg_ena;
    }
    return vtss_phy_conf_set(data->vtss_instance, data->port_no, &phy_config);
}

static mepa_rc phy_1g_conf_get(mepa_device_t *dev, mepa_conf_t *const conf)
{
    vtss_phy_conf_t phy_conf;
    vtss_phy_conf_1g_t cfg_neg = {};
    phy_data_t *data = (phy_data_t *)dev->data;
    *conf = (const mepa_conf_t) {};

    if (vtss_phy_conf_get(data->vtss_instance, data->port_no, &phy_conf) != MESA_RC_OK) {
        return (MEPA_RC_ERROR);
    }
    conf->flow_control = phy_conf.aneg.symmetric_pause;
    conf->aneg.speed_2g5_fdx = phy_conf.aneg.speed_2g5_fdx;
    conf->aneg.speed_5g_fdx = phy_conf.aneg.speed_5g_fdx;
    conf->aneg.speed_10g_fdx = phy_conf.aneg.speed_10g_fdx;
    conf->aneg.speed_10m_hdx = phy_conf.aneg.speed_10m_hdx;
    conf->aneg.speed_10m_fdx = phy_conf.aneg.speed_10m_fdx;
    conf->aneg.speed_100m_hdx = phy_conf.aneg.speed_100m_hdx;
    conf->aneg.speed_100m_fdx = phy_conf.aneg.speed_100m_fdx;
    conf->aneg.speed_1g_fdx = phy_conf.aneg.speed_1g_fdx;
    conf->aneg.no_restart_aneg = phy_conf.aneg.no_restart_aneg;
    // Translate MDI Mode
    conf->mdi_mode = MEPA_MEDIA_MODE_AUTO;
    if (phy_conf.mdi == VTSS_PHY_MDI) {
        conf->mdi_mode = MEPA_MEDIA_MODE_MDI;
    } else if (phy_conf.mdi == VTSS_PHY_MDIX) {
        conf->mdi_mode = MEPA_MEDIA_MODE_MDIX;
    }

    // Force AMS Media Select
    conf->force_ams_mode_sel = !phy_conf.force_ams_sel ? VTSS_PHY_MEDIA_FORCE_AMS_SEL_NORMAL :
                               phy_conf.force_ams_sel == VTSS_PHY_MEDIA_FORCE_AMS_SEL_SERDES ?
                               VTSS_PHY_MEDIA_FORCE_AMS_SEL_SERDES : VTSS_PHY_MEDIA_FORCE_AMS_SEL_COPPER;

    if (phy_conf.mode == VTSS_PHY_MODE_ANEG) {
        conf->speed = MEPA_SPEED_AUTO;

        // Get manual negotiation options
        if (vtss_phy_conf_1g_get(data->vtss_instance, data->port_no, &cfg_neg) == MESA_RC_OK) {
            conf->man_neg = !cfg_neg.master.cfg ? MEPA_MANUAL_NEG_DISABLED :
                            cfg_neg.master.val ? MEPA_MANUAL_NEG_REF : MEPA_MANUAL_NEG_CLIENT;
        }
    } else if (phy_conf.mode == VTSS_PHY_MODE_FORCED) {
        conf->speed = phy_conf.forced.speed;
    }
    conf->fdx = phy_conf.forced.fdx;
    conf->mac_if_aneg_ena = phy_conf.mac_if_pcs.serdes_aneg_ena;
    conf->admin.enable = phy_conf.mode != VTSS_PHY_MODE_POWER_DOWN ? true : false;
    return MEPA_RC_OK;
}

static mepa_rc mscc_if_set(mepa_device_t *dev,
                           mepa_port_interface_t mac_if)
{
    phy_data_t *data = (phy_data_t *)dev->data;
    // TODO, check that the configured interface is supported!
    data->mac_if = mac_if;
    return MEPA_RC_OK;
}


static mepa_rc mscc_1g_if_get(mepa_device_t *dev, mepa_port_speed_t speed,
                              mepa_port_interface_t *mac_if)
{
    phy_data_t *data = (phy_data_t *)dev->data;

    *mac_if = data->mac_if;
    return MEPA_RC_OK;
}

static mepa_rc mscc_1g_power_set(mepa_device_t *dev,
                                 mepa_power_mode_t power)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    vtss_phy_power_conf_t power_conf = {};
    power_conf.mode = power;
    return vtss_phy_power_conf_set(data->vtss_instance, data->port_no, &power_conf);
}

static mepa_rc mscc_1g_veriphy_start(mepa_device_t *dev, int mode)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    return vtss_phy_veriphy_start(data->vtss_instance, data->port_no, mode);
}

static mepa_rc mscc_1g_veriphy_get(mepa_device_t *dev,
                                   mepa_cable_diag_result_t *res)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    vtss_phy_veriphy_result_t result;
    mepa_rc rc;

    if ((rc = vtss_phy_veriphy_get(data->vtss_instance, data->port_no, &result)) != MEPA_RC_ERROR) {
        res->link = result.link;
        memcpy(&res->status, &result.status, sizeof(res->status));
        memcpy(&res->length, &result.length, sizeof(res->length));
    }
    return rc;
}

static mepa_rc mscc_1g_media_set(mepa_device_t *dev,
                                 mepa_media_interface_t phy_media_if)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    vtss_phy_reset_conf_t conf = {};
    vtss_phy_reset_get(data->vtss_instance, data->port_no, &conf);

    conf.mac_if = data->mac_if;
    conf.media_if = phy_media_if;

    return vtss_phy_reset(data->vtss_instance, data->port_no, &conf);
}

static mepa_device_t *mscc_1g_probe(mepa_driver_t *drv,
                                    const mepa_callout_t    MEPA_SHARED_PTR *callout,
                                    struct mepa_callout_ctx MEPA_SHARED_PTR *callout_ctx,
                                    struct mepa_board_conf              *board_conf)
{
    int i;
    mepa_device_t *dev;
    phy_data_t *data;

    if (mscc_vtss_create(callout, callout_ctx, board_conf) != MEPA_RC_OK) {
        return NULL;
    }

    dev = mepa_create_int(drv, callout, callout_ctx, board_conf, sizeof(phy_data_t));
    if (!dev) {
        return 0;
    }

    data = dev->data;
    data->vtss_instance = board_conf->vtss_instance_ptr;
    data->port_no = board_conf->numeric_handle;
    data->cap = PHY_CAP_1G;

    for (i = 0; i < MAX_PORTS_PER_PHY; i++) {
        data->other_dev[i] = NULL;
    }
    T_I(MEPA_TRACE_GRP_GEN, "probed port %d, instance: %p",
        data->port_no, data->vtss_instance);

    return dev;
}

static mepa_rc mscc_1g_status_1g_get(mepa_device_t    *dev,
                                     mepa_aneg_status_t *status)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    mepa_rc rc;
    vtss_phy_status_1g_t vtss_status;

    rc = vtss_phy_status_1g_get(data->vtss_instance, data->port_no, &vtss_status);
    status->master_cfg_fault = vtss_status.master_cfg_fault;
    status->master = vtss_status.master;
    return rc;
}

static mepa_rc phy_1g_event_enable_set(mepa_device_t *dev, mepa_event_t event,
                                       mesa_bool_t enable)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    return vtss_phy_event_enable_set(data->vtss_instance, data->port_no, event, enable);
}

static mepa_rc phy_1g_event_enable_get(mepa_device_t *dev, mepa_event_t *ev_mask)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    return vtss_phy_event_enable_get(data->vtss_instance, data->port_no, ev_mask);
}

static mepa_rc phy_1g_event_poll(mepa_device_t *dev, mepa_event_t *status)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    return vtss_phy_event_poll(data->vtss_instance, data->port_no, status);
}

static mepa_rc phy_1g_loopback_set(mepa_device_t *dev, const mepa_loopback_t *loopback)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    vtss_phy_loopback_t phy_loop = {};

    if (loopback->qsgmii_pcs_tbi_ena == true || loopback->qsgmii_pcs_gmii_ena == true ||
            loopback->qsgmii_serdes_ena == true) {
        return MEPA_RC_NOT_IMPLEMENTED;
    }
    phy_loop.near_end_enable = loopback->near_end_ena;
    phy_loop.far_end_enable = loopback->far_end_ena;
    phy_loop.connector_enable = loopback->connector_ena;
    phy_loop.mac_serdes_input_enable = loopback->mac_serdes_input_ena;
    phy_loop.mac_serdes_facility_enable = loopback->mac_serdes_facility_ena;
    phy_loop.mac_serdes_equipment_enable = loopback->mac_serdes_equip_ena;
    phy_loop.media_serdes_input_enable = loopback->media_serdes_input_ena;
    phy_loop.media_serdes_facility_enable = loopback->media_serdes_facility_ena;
    phy_loop.media_serdes_equipment_enable = loopback->media_serdes_equip_ena;
    return vtss_phy_loopback_set(data->vtss_instance, data->port_no, phy_loop);
}

static mepa_rc phy_1g_loopback_get(mepa_device_t *dev, mepa_loopback_t *const loopback)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    vtss_phy_loopback_t loop = {};
    if (vtss_phy_loopback_get(data->vtss_instance, data->port_no, &loop) == MESA_RC_OK) {
        loopback->far_end_ena = loop.far_end_enable;
        loopback->near_end_ena = loop.near_end_enable;
        loopback->connector_ena = loop.connector_enable;
        loopback->mac_serdes_input_ena = loop.mac_serdes_input_enable;
        loopback->mac_serdes_facility_ena = loop.mac_serdes_facility_enable;
        loopback->mac_serdes_equip_ena = loop.mac_serdes_equipment_enable;
        loopback->media_serdes_input_ena = loop.media_serdes_input_enable;
        loopback->media_serdes_facility_ena = loop.media_serdes_facility_enable;
        loopback->media_serdes_equip_ena = loop.media_serdes_equipment_enable;
    }
    return MEPA_RC_OK;
}

static mepa_rc phy_1g_read(mepa_device_t *dev, uint32_t address, uint16_t *const value)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    return vtss_phy_read(data->vtss_instance, data->port_no, address, value);
}

static mepa_rc phy_1g_write(mepa_device_t *dev, uint32_t address, uint16_t value)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    return vtss_phy_write(data->vtss_instance, data->port_no, address, value);
}

static mepa_rc phy_1g_gpio_mode(mepa_device_t *dev, const mepa_gpio_conf_t *gpio_conf)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    vtss_phy_gpio_mode_t gpio_mode;

    // VSC8584 has 0-13 gpios
    if (gpio_conf->gpio_no > 13) {
        return MEPA_RC_NOT_IMPLEMENTED;
    }
    if (gpio_conf->mode == MEPA_GPIO_MODE_LED_DISABLE_EXTENDED) {
        return MEPA_RC_NOT_IMPLEMENTED;
    }
    switch (gpio_conf->mode) {
    case MEPA_GPIO_MODE_OUT:
        gpio_mode = VTSS_PHY_GPIO_OUT;
        break;
    case MEPA_GPIO_MODE_IN:
        gpio_mode = VTSS_PHY_GPIO_IN;
        break;
    default:
        gpio_mode = VTSS_PHY_GPIO_ALT_0;
        break;
    }
    if (gpio_conf->mode >= MEPA_GPIO_MODE_LED_LINK_ACTIVITY && gpio_conf->mode < MEPA_GPIO_MODE_LED_DISABLE_EXTENDED) {
        vtss_phy_led_mode_select_t mode_select;
        mode_select.number = gpio_conf->led_num == MEPA_LED0 ? VTSS_PHY_LED0 : VTSS_PHY_LED1;
        mode_select.mode = VTSS_PHY_LED_MODE_LINK_ACTIVITY + (gpio_conf->mode - MEPA_GPIO_MODE_LED_LINK_ACTIVITY);
        vtss_phy_led_mode_set(data->vtss_instance, data->port_no, mode_select);
    }
    return vtss_phy_gpio_mode(data->vtss_instance, data->port_no, gpio_conf->gpio_no, gpio_mode);
}

static mepa_rc phy_1g_gpio_set(mepa_device_t *dev, uint8_t gpio_no, mepa_bool_t enable)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    // VSC8584 has 0-13 gpios
    if (gpio_no > 13) {
        return MEPA_RC_NOT_IMPLEMENTED;
    }
    return vtss_phy_gpio_set(data->vtss_instance, data->port_no, gpio_no, enable);
}

// Enable/Disable Isolate mode
static mepa_rc phy_isolate_mode_conf(mepa_device_t *dev, const mepa_bool_t iso_en)
{
    phy_data_t *data = (phy_data_t *)(dev->data);

    return vtss_phy_isolate_mode_conf(data->vtss_instance, data->port_no, iso_en);
}

static mepa_rc phy_1g_gpio_get(mepa_device_t *dev, uint8_t gpio_no, mepa_bool_t *const enable)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    // VSC8584 has 0-13 gpios
    if (gpio_no > 13) {
        return MEPA_RC_NOT_IMPLEMENTED;
    }
    return vtss_phy_gpio_get(data->vtss_instance, data->port_no, gpio_no, enable);
}

static mepa_rc phy_1g_synce_clk_conf_set(mepa_device_t *dev, const mepa_synce_clock_conf_t *conf)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    vtss_phy_clock_conf_t phy_conf = {};
    vtss_phy_recov_clk_t clk_port = conf->dst == MEPA_SYNCE_CLOCK_DST_1 ? VTSS_PHY_RECOV_CLK1 : VTSS_PHY_RECOV_CLK2;

    phy_conf.squelch = conf->src == MEPA_SYNCE_CLOCK_SRC_DISABLED ? VTSS_PHY_CLK_SQUELCH_NONE : VTSS_PHY_CLK_SQUELCH_MAX;
    phy_conf.src = conf->src == MEPA_SYNCE_CLOCK_SRC_SERDES_MEDIA ? VTSS_PHY_SERDES_MEDIA :
                   conf->src == MEPA_SYNCE_CLOCK_SRC_COPPER_MEDIA ? VTSS_PHY_COPPER_MEDIA : VTSS_PHY_CLK_DISABLED;
    phy_conf.freq = conf->freq == MEPA_FREQ_25M ? VTSS_PHY_FREQ_25M :
                    conf->freq == MEPA_FREQ_31_25M ? VTSS_PHY_FREQ_3125M : VTSS_PHY_FREQ_125M;

    return vtss_phy_clock_conf_set(data->vtss_instance, data->port_no, clk_port, &phy_conf);
}

// Store base_dev info in dev and store dev info in base_dev.
static mepa_rc phy_1g_link_base_port(mepa_device_t *dev, mepa_device_t *base_dev, uint8_t packet_idx)
{
    phy_data_t *base_data = (phy_data_t *)(base_dev->data);
    phy_data_t *data = (phy_data_t *)(dev->data);
    vtss_phy_type_t base_id;
    mesa_rc rc;
    int i;

    T_D(MEPA_TRACE_GRP_GEN, "for port %d base port linked as %d\n", data->port_no, base_data->port_no);
    data->base_dev = base_dev;
    base_data->base_dev = base_dev;
    base_data->other_dev[0] = base_dev; // needed later for comparison.
    base_data->all_phy_ports[0] = base_data->port_no;
    if (dev->drv->mepa_ts) {
        for (i = 1; i < MAX_PORTS_PER_PHY; i++) {
            if ((dev != base_dev) && base_data->other_dev[i] == NULL) {
                base_data->other_dev[i] = dev;
                base_data->all_phy_ports[i] = data->port_no;
                break;
            }
            T_D(MEPA_TRACE_GRP_GEN, "base linked i %d port %d\n", i, base_data->all_phy_ports[i]);
        }
    }
    rc = vtss_phy_id_get(data->vtss_instance, base_data->port_no, &base_id);
    if (rc == MESA_RC_OK && base_id.channel_id) {
        T_W(MEPA_TRACE_GRP_GEN, "base port channel id is not 0");
        return MEPA_RC_ERROR;
    }
    return MEPA_RC_OK;
}

static mepa_rc phy_1g_info_get(mepa_device_t *dev, mepa_phy_info_t *const phy_info)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    vtss_phy_type_t phy_id;
    mesa_rc rc = MESA_RC_OK;

    phy_info->cap = 0;
    rc = vtss_phy_id_get(data->vtss_instance, data->port_no, &phy_id);
    if (rc == MESA_RC_OK) {
        phy_info->part_number = phy_id.part_number;
        phy_info->revision = phy_id.revision;

        if (mepa_capability(dev, MEPA_CAP_SPEED_1G)) {
            phy_info->cap = MEPA_CAP_SPEED_MASK_1G;
        }

        if (mepa_capability(dev, MEPA_CAP_TS_GEN_2)) {
            phy_info->cap |= MEPA_CAP_TS_MASK_GEN_2;
        } else if (mepa_capability(dev, MEPA_CAP_TS_GEN_1)) {
            phy_info->cap |= MEPA_CAP_TS_MASK_GEN_1;
        } else {
            phy_info->cap |= MEPA_CAP_TS_MASK_NONE;
        }
        phy_info->ts_base_port = phy_id.phy_api_base_no;
        phy_info->ts_base = data->base_dev;
    }
    return rc == MESA_RC_OK ? MEPA_RC_OK : MEPA_RC_ERROR;
}

static mepa_rc phy_1g_chip_temp_get(mepa_device_t *dev,
                                    i16 *const temp)
{
    phy_data_t *data = (phy_data_t*)(dev->data);
    mesa_rc rc = MESA_RC_OK;
    if (!(data->temp_init_flag)) {
        if ((rc = vtss_phy_chip_temp_init(data->vtss_instance, data->port_no)) != MESA_RC_OK) {
            return MEPA_RC_ERROR;
        }
        data->temp_init_flag = true;
    }
    return vtss_phy_chip_temp_get(data->vtss_instance,data->port_no,temp);
}

static mepa_rc phy_10g_delete(mepa_device_t *dev)
{
    (void)mscc_vtss_destroy(dev);
    return mepa_delete_int(dev);
}

static mepa_rc malibu_10g_reset(mepa_device_t *dev,
                                const mepa_reset_param_t *rst_conf)
{
    phy_data_t *data = (phy_data_t *)(dev->data);

    data->temp_init_flag = false;
    switch(rst_conf->reset_point) {
    case MEPA_RESET_POINT_PRE:
        if(vtss_phy_10g_init(data->vtss_instance, data->port_no, NULL) != VTSS_RC_OK) {
            return MEPA_RC_ERROR;
        }
        break;
    default:
        // No other RESET POINTs needed
        break;
    }

    return MEPA_RC_OK;
}

static mepa_rc venice_10g_reset(mepa_device_t *dev,
                                const mepa_reset_param_t *rst_conf)
{
    vtss_phy_10g_mode_t oper_mode = {};
    vtss_phy_10g_id_t phy_10g_id;
    phy_data_t *data = (phy_data_t *)(dev->data);

    oper_mode.oper_mode = VTSS_PHY_LAN_MODE;
    if (vtss_phy_10g_id_get(data->vtss_instance, data->port_no, &phy_10g_id) == MEPA_RC_OK) {
        if ((phy_10g_id.part_number == 0x8487) || (phy_10g_id.part_number == 0x8488)) {
            oper_mode.xfi_pol_invert = 0;
        } else {
            oper_mode.xfi_pol_invert = 1;
        }
    }
    oper_mode.l_clk_src.is_high_amp = true;
    oper_mode.l_media = VTSS_MEDIA_TYPE_SR;
    return vtss_phy_10g_mode_set(data->vtss_instance, data->port_no, &oper_mode);
}

/* CuSFP in 1G_MODE non-repeater with SGMII on both PCS1G blocks. PCS1G_LINK_STATUS reflects only
 * symbol sync. The real RJ-45 link/fdx/speed come from the Line-side SGMII partner page. When
 * partner speed changes, re-run sgmii_mode_set so the host side re-advertises the new speed to the
 * switch MAC. Sub-1G cannot cross Malibu-internal MACs on Rev D silicon (no byte-decimation
 * circuit) — link is forced down and an error is logged once per speed change. */
static mepa_rc phy_10g_malibu_1g_sgmii_status(mepa_device_t *dev, mepa_status_t *status)
{
    phy_data_t                 *data = (phy_data_t *)dev->data;
    vtss_inst_t                 vtss_inst = data->vtss_instance;
    vtss_phy_10g_sgmii_status_t sgmii = {};

    if (vtss_phy_10g_sgmii_status_get(vtss_inst, data->port_no, &sgmii) != VTSS_RC_OK) {
        return MEPA_RC_ERROR;
    }

    status->link = sgmii.link;
    if (!sgmii.link) {
        data->sgmii_passthru_spd = VTSS_SPEED_UNDEFINED;
        return MEPA_RC_OK;
    }

    status->fdx = sgmii.fdx;

    if (sgmii.speed != VTSS_SPEED_1G) {
        /* Sub-1G CuSFP: silicon cannot de-duplicate the byte-repeated SGMII
         * stream. Force link down; warn on each speed transition. */
        if (data->sgmii_passthru_spd != sgmii.speed) {
            const char *spd_str = (sgmii.speed == VTSS_SPEED_10M)    ? "10M"
                                  : (sgmii.speed == VTSS_SPEED_100M) ? "100M"
                                                                     : "sub-1G";
            T_E(MEPA_TRACE_GRP_GEN,
                "port %u: CuSFP negotiated %s - not supported in 1G_MODE "
                "non-repeater on Malibu-10. Use REPEATER mode for sub-1G "
                "(loses MACsec/1588).",
                data->port_no, spd_str);
            data->sgmii_passthru_spd = sgmii.speed;
        }
        status->link = 0;
        return MEPA_RC_OK;
    }

    status->speed = sgmii.speed;

    if (data->sgmii_passthru_spd != sgmii.speed) {
        if (vtss_phy_10g_sgmii_mode_set(vtss_inst, data->port_no, TRUE) != VTSS_RC_OK) {
            T_E(MEPA_TRACE_GRP_GEN,
                "port %u: failed to re-apply SGMII mode after speed change", data->port_no);
            return MEPA_RC_ERROR;
        }
        data->sgmii_passthru_spd = sgmii.speed;
    }

    return MEPA_RC_OK;
}

static mepa_rc phy_10g_poll(mepa_device_t *dev,
                            mepa_status_t *status)
{
    phy_data_t *data = (phy_data_t *)dev->data;
    vtss_phy_10g_status_t status_10g;
    vtss_phy_10g_mode_t mode_cur = {};

    memset(status, 0, sizeof(*status));

    if (vtss_phy_10g_status_get(data->vtss_instance, data->port_no, &status_10g) != VTSS_RC_OK) {
        return MEPA_RC_ERROR;
    }

    if (vtss_phy_10g_mode_get(data->vtss_instance, data->port_no, &mode_cur)) {
        return MEPA_RC_ERROR;
    }

    if (mode_cur.oper_mode == VTSS_PHY_REPEATER_MODE) {
        /* PCS is bypassed in repeater; link reflects PMA sync only.
         * Speed/fdx/aneg come from the MAC (meba_port_status_get handles this). */
        status->link = status_10g.pma.rx_link && status_10g.hpma.rx_link;
    } else {
        status->link = status_10g.status;
        if (status_10g.pma.rx_link && status_10g.hpma.rx_link && status_10g.pcs.rx_link && status_10g.hpcs.rx_link)
        {
            status->speed = MESA_SPEED_10G;
            status->fiber = 1;
            status->fdx = 1;
        }
        else if (status_10g.pma.rx_link && status_10g.hpma.rx_link && status_10g.lpcs_1g && status_10g.hpcs_1g)
        {
            /* 1000BASE-X Clause 37 supports FDX only. Reading clause-37 advertisement.fdx
             * is unreliable when ANEG is disabled, so hardcode FDX here. */
            status->speed = MESA_SPEED_1G;
            status->fdx = 1;
        }
    }

    if (mode_cur.oper_mode == VTSS_PHY_1G_MODE && mode_cur.enable_pass_thru) {
        return phy_10g_malibu_1g_sgmii_status(dev, status);
    }
    return MEPA_RC_OK;
}

/* 1G / AUTO path for Malibu-10: always repeater at 1.25 Gbps regardless of SFP
 * type. Works for CuSFP (SGMII 10/100/1000M), 1000BASE-X fiber, DAC, and
 * 100BASE-FX — the line OB enable added by MEPA-1354 in
 * malibu_phy_10g_lane_sync_set_internal makes this safe on all SFPs. */
static mepa_rc phy_10g_1g_mode_set(mepa_device_t             *dev,
                                   const mepa_conf_t         *config,
                                   vtss_phy_10g_mode_t *const mode)
{
    phy_data_t *data = (phy_data_t *)dev->data;
    vtss_inst_t vtss_inst = data->vtss_instance;

    /* Flip lanes to match JR XAUI-lane-0 / 8487 XAUI-lane-0 for XAUI MACs. */
    mode->xaui_lane_flip = true;

    /* Repeater is a bit-level SerDes relay; PCS1G is bypassed on both sides,
     * so clause-37 aneg / sgmii_mode_set are not applicable here. For CuSFP /
     * 1G media the SerDes rate must be 1.25 Gbps — without it the default
     * 10.3125 Gbps rate has no link to the 1G partner. oper_mode is already
     * carried in from config->conf_10g.oper_mode; skip the SFP-type branches
     * below so they don't overwrite it to 1G_MODE. */
    if (config->conf_10g.oper_mode == MEPA_PHY_REPEATER_MODE &&
        data->mac_if == MESA_PORT_INTERFACE_SGMII_CISCO) {
        mode->rate = VTSS_RPTR_RATE_1_25;
        return vtss_phy_10g_mode_set(vtss_inst, data->port_no, mode);
    }

    if (data->mac_if == MESA_PORT_INTERFACE_SGMII_CISCO) {
        /* CuSFP (1G): use the 1 GbE data path (datasheet Figure 99) with
         * both Line and Host PCS in SGMII mode. Host MAC / MACsec / 1588
         * blocks are engaged instead of being bypassed, so PHY features
         * become usable. The vtss_phy_10g_sgmii_mode_set() call reads the
         * CuSFP's partner SGMII aneg page on the line side and programs
         * the matching advertisement on the host side toward the MAC. */
        mode->oper_mode = VTSS_PHY_1G_MODE;
        if (vtss_phy_10g_mode_set(vtss_inst, data->port_no, mode) != VTSS_RC_OK) {
            return MEPA_RC_ERROR;
        }
        if (vtss_phy_10g_sgmii_mode_set(vtss_inst, data->port_no, TRUE) != VTSS_RC_OK) {
            return MEPA_RC_ERROR;
        }
        return MEPA_RC_OK;
    }

    /* Fiber / DAC: non-repeater 1000BASE-X with clause-37 aneg. Keeps
     * PHY-level features (MACsec / PTP / 1588) available. */
    mode->oper_mode = VTSS_PHY_1G_MODE;
    if (vtss_phy_10g_mode_set(vtss_inst, data->port_no, mode) != MEPA_RC_OK) {
        return MEPA_RC_ERROR;
    }

    /* Select 1000BASE-X (SERDES) line/host PCS1G, not SGMII. The chip defaults
     * to SGMII_MODE_ENA=1; without this a fiber SFP stays in SGMII mode and
     * clause-37 aneg never resolves against a 1000BASE-X partner. Passing
     * FALSE also clears enable_pass_thru (left stuck TRUE after a prior CuSFP). */
    if (vtss_phy_10g_sgmii_mode_set(vtss_inst, data->port_no, FALSE) != VTSS_RC_OK) {
        return MEPA_RC_ERROR;
    }

    vtss_phy_10g_clause_37_control_t ctrl = {};
    ctrl.enable = (config->speed == MESA_SPEED_AUTO) ? 1 : 0;
    ctrl.advertisement.fdx = 1;
    ctrl.advertisement.symmetric_pause = config->flow_control;
    ctrl.advertisement.asymmetric_pause = config->flow_control;
    ctrl.advertisement.remote_fault = (config->admin.enable ? VTSS_PHY_10G_CLAUSE_37_RF_LINK_OK
                                                            : VTSS_PHY_10G_CLAUSE_37_RF_OFFLINE);
    ctrl.l_h = true;
    if (vtss_phy_10g_clause_37_control_set(vtss_inst, data->port_no, &ctrl) != MEPA_RC_OK) {
        return MEPA_RC_ERROR;
    }
    return MEPA_RC_OK;
}

static mepa_rc phy_10g_conf_set(mepa_device_t *dev, const mepa_conf_t *config)
{
    phy_data_t *data = (phy_data_t *)dev->data;
    vtss_phy_10g_mode_t mode = {0};
    if(vtss_phy_10g_init(data->vtss_instance, data->port_no, NULL) != VTSS_RC_OK) {
        return MEPA_RC_ERROR;
    }
    if (config->conf_10g.h_media > MEPA_MEDIA_TYPE_KR_SC || config->conf_10g.l_media > MEPA_MEDIA_TYPE_KR_SC) {
        T_E(MEPA_TRACE_GRP_GEN, "\n PHY doesn't support the given host/line media");
        return MEPA_RC_ERROR;
    }
    mode.oper_mode = config->conf_10g.oper_mode;
    mode.interface  = config->conf_10g.interface_mode;
    mode.channel_id = config->conf_10g.channel_id;
    mode.h_media = config->conf_10g.h_media;
    mode.l_media = config->conf_10g.l_media;
    mode.channel_high_to_low = config->conf_10g.channel_high_to_low;
    mode.xfi_pol_invert = config->conf_10g.xfi_pol_invert;
    mode.polarity.host_rx = config->conf_10g.polarity.host_rx;
    mode.polarity.line_rx = config->conf_10g.polarity.line_rx;
    mode.polarity.host_tx = config->conf_10g.polarity.host_tx;
    mode.polarity.line_tx = config->conf_10g.polarity.line_tx;
    mode.is_host_wan = config->conf_10g.is_host_wan;
    mode.lref_for_host = config->conf_10g.lref_for_host;
    mode.h_clk_src.is_high_amp = config->conf_10g.h_clk_src_is_high_amp;
    mode.l_clk_src.is_high_amp = config->conf_10g.l_clk_src_is_high_amp;
    if (config->speed == MESA_SPEED_1G || config->speed == MESA_SPEED_AUTO) {
        return phy_10g_1g_mode_set(dev, config, &mode);
    } else if (config->speed == MESA_SPEED_10G) {
        // mode.oper_mode is set by the application
        if (vtss_phy_10g_mode_set(data->vtss_instance, data->port_no, &mode) != MEPA_RC_OK) {
            return MEPA_RC_ERROR;
        }
    } else {
        T_E(MEPA_TRACE_GRP_GEN, "Speed not specified for 10g conf set");
        return MEPA_RC_ERROR;
    }

    return MEPA_RC_OK;
}

static mepa_rc phy_10g_conf_get(mepa_device_t *dev, mepa_conf_t *config)
{
    phy_data_t *data = (phy_data_t *)dev->data;
    vtss_phy_10g_mode_t mode = {};
    vtss_phy_10g_clause_37_control_t ctrl_get;
    memset(config, 0 , sizeof(mepa_conf_t));
    if (vtss_phy_10g_mode_get(data->vtss_instance, data->port_no, &mode) != MEPA_RC_OK) {
        return MEPA_RC_ERROR;
    }

    if (vtss_phy_10g_clause_37_control_get(data->vtss_instance, data->port_no, &ctrl_get) != MEPA_RC_OK) {
        return MEPA_RC_ERROR;
    }

    if(mode.oper_mode == VTSS_PHY_1G_MODE) {
        config->speed = MESA_SPEED_1G;
    } else {
        config->speed = MESA_SPEED_10G;
    }

    config->adv_dis = true;
    config->conf_10g.oper_mode = mode.oper_mode;
    config->conf_10g.interface_mode = mode.interface;
    config->conf_10g.channel_id = mode.channel_id + 1;
    config->conf_10g.h_media = mode.h_media;
    config->conf_10g.l_media = mode.l_media;
    config->conf_10g.channel_high_to_low = mode.channel_high_to_low;
    config->conf_10g.xfi_pol_invert = mode.xfi_pol_invert;
    config->conf_10g.polarity.host_rx = mode.polarity.host_rx;
    config->conf_10g.polarity.line_rx = mode.polarity.line_rx;
    config->conf_10g.polarity.host_tx = mode.polarity.host_tx;
    config->conf_10g.polarity.line_tx = mode.polarity.line_tx;
    config->conf_10g.is_host_wan = mode.is_host_wan;
    config->conf_10g.lref_for_host = mode.lref_for_host;
    config->conf_10g.h_clk_src_is_high_amp = mode.h_clk_src.is_high_amp;
    config->conf_10g.l_clk_src_is_high_amp = mode.l_clk_src.is_high_amp;

    if(ctrl_get.enable == TRUE) {
        config->speed = MESA_SPEED_AUTO;
        config->adv_dis = false;
        config->aneg.speed_1g_fdx = true;
        config->flow_control = (ctrl_get.advertisement.symmetric_pause || ctrl_get.advertisement.asymmetric_pause) ? true:false;
        config->mac_if_aneg_ena = true;
    }
    config->fdx = true;
    return MEPA_RC_OK;
}

static mepa_rc malibu_10g_if_get(mepa_device_t *dev,
                                 mepa_port_speed_t speed,
                                 mepa_port_interface_t *mac_if)
{
    switch (speed) {
    case MESA_SPEED_AUTO:
    case MESA_SPEED_1G:
        *mac_if = MESA_PORT_INTERFACE_SERDES;
        break;
    default:
        *mac_if = MESA_PORT_INTERFACE_SFI;
        break;
    }
    return MEPA_RC_OK;
}

static mepa_rc venice_10g_if_get(mepa_device_t *dev,
                                 mepa_port_speed_t speed,
                                 mepa_port_interface_t *mac_if)
{
    switch (speed) {
    case MESA_SPEED_AUTO:
    case MESA_SPEED_1G:
        *mac_if = MESA_PORT_INTERFACE_SERDES;
        break;
    default:
        *mac_if = MESA_PORT_INTERFACE_XAUI;
        break;
    }
    return MEPA_RC_OK;

}

static mepa_rc phy_10g_info_get(struct mepa_device *dev, mepa_phy_info_t *const phy_info)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    vtss_phy_10g_id_t phy_id = {};
    mesa_rc rc = MESA_RC_OK;

    phy_info->cap = 0;
    rc = vtss_phy_10g_id_get(data->vtss_instance, data->port_no, &phy_id);
    if (rc == MESA_RC_OK) {
        phy_info->part_number = phy_id.part_number;
        phy_info->revision = phy_id.revision;

        if (mepa_capability(dev, MEPA_CAP_SPEED_10G)) {
            phy_info->cap |= MEPA_CAP_SPEED_MASK_10G;
        }

        if (mepa_capability(dev, MEPA_CAP_TS_GEN_1)) {
            phy_info->cap |= MEPA_CAP_TS_MASK_GEN_1;
        } else if (mepa_capability(dev, MEPA_CAP_TS_GEN_2)) {
            phy_info->cap |= MEPA_CAP_TS_MASK_GEN_2;
        } else {
            phy_info->cap |= MEPA_CAP_TS_MASK_NONE;
        }
        phy_info->ts_base_port = (phy_id.channel_id > 1) ? (phy_id.phy_api_base_no + 2) : phy_id.phy_api_base_no;
        phy_info->ts_base = data->base_dev;
    }
    return rc == MESA_RC_OK ? MEPA_RC_OK : MEPA_RC_ERROR;
}

static mepa_device_t *phy_10g_probe(mepa_driver_t *drv,
                                    const mepa_callout_t    MEPA_SHARED_PTR *callout,
                                    struct mepa_callout_ctx MEPA_SHARED_PTR *callout_ctx,
                                    struct mepa_board_conf              *board_conf)
{
    mepa_device_t *dev;
    phy_data_t *data;

    if (mscc_vtss_create(callout, callout_ctx, board_conf) != MEPA_RC_OK) {
        return NULL;
    }

    dev = mepa_create_int(drv, callout, callout_ctx, board_conf, sizeof(phy_data_t));
    if (!dev) {
        return 0;
    }

    data = dev->data;
    data->vtss_instance = board_conf->vtss_instance_ptr;
    data->port_no = board_conf->numeric_handle;
    data->cap = PHY_CAP_10G;
    data->sgmii_passthru_spd = VTSS_SPEED_UNDEFINED;

    return dev;
}

static mepa_rc phy_1g_i2c_read(mepa_device_t *dev,
                               const uint8_t i2c_mux,
                               const uint8_t i2c_reg_addr,
                               const uint8_t i2c_dev_addr,
                               const mepa_bool_t word_access,
                               uint8_t cnt,
                               uint8_t *const value)
{
    phy_data_t *data = (phy_data_t *)(dev->data);

    return vtss_phy_i2c_read(data->vtss_instance, data->port_no, i2c_mux,
                             i2c_reg_addr, i2c_dev_addr, word_access, cnt, value);
}

static mepa_rc phy_1g_i2c_write(mepa_device_t *dev,
                                const uint8_t i2c_mux,
                                const uint8_t i2c_reg_addr,
                                const uint8_t i2c_dev_addr,
                                const mepa_bool_t word_access,
                                uint8_t cnt,
                                const uint8_t *value)
{
    phy_data_t *data = (phy_data_t *)(dev->data);

    return vtss_phy_i2c_write(data->vtss_instance, data->port_no, i2c_mux,
                              i2c_reg_addr, i2c_dev_addr, word_access, cnt, value);
}

static mepa_rc phy_1g_i2c_clock_select(mepa_device_t *dev,
                                       const mepa_i2c_clk_select_t *clk_value)
{
    phy_data_t *data = (phy_data_t *)(dev->data);

    return vtss_phy_i2c_clock_select(data->vtss_instance, data->port_no, clk_value);
}

static mepa_rc phy_1g_fefi_set(struct mepa_device *dev,
                               const mepa_fefi_mode_t *conf)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    vtss_fefi_mode_t fefi_conf;
    mepa_rc rc = MEPA_RC_OK;
    if((rc = vtss_phy_fefi_get(data->vtss_instance, data->port_no, &fefi_conf)) != MEPA_RC_OK) {
        return MEPA_RC_ERROR;
    }
    fefi_conf = *conf;
    return vtss_phy_fefi_set(data->vtss_instance, data->port_no, fefi_conf);
}

// Read FEFI Configurations API
static mepa_rc phy_1g_fefi_get(struct mepa_device *dev,
                               mepa_fefi_mode_t *const conf)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    vtss_fefi_mode_t fefi_conf;
    mepa_rc rc = MEPA_RC_OK;
    if((rc = vtss_phy_fefi_get(data->vtss_instance, data->port_no, &fefi_conf)) != MEPA_RC_OK) {
        return MEPA_RC_ERROR;
    }
    *conf = fefi_conf;
    return MEPA_RC_OK;
}

// FEFI detect API
static mepa_rc phy_1g_fefi_detect(struct mepa_device *dev,
                                  mepa_bool_t *const fefi_detect)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    mepa_bool_t detect = FALSE;
    mepa_rc rc = MEPA_RC_OK;
    if((rc = vtss_phy_fefi_detect(data->vtss_instance, data->port_no, &detect)) != MEPA_RC_OK) {
        return MEPA_RC_ERROR;
    }
    *fefi_detect = detect;
    return MEPA_RC_OK;
}

static mepa_rc phy_eee_mode_conf_set(mepa_device_t *dev, const mepa_phy_eee_conf_t conf)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    mepa_rc rc = MEPA_RC_OK;
    mepa_bool_t capable = FALSE;
    vtss_phy_eee_conf_t eee_conf = {};
    if ((rc = vtss_phy_port_eee_capable(data->vtss_instance, data->port_no, &capable)) != MEPA_RC_OK) {
        return rc;
    }
    eee_conf.eee_mode = (conf.eee_mode == MEPA_EEE_DISABLE ? VTSS_EEE_DISABLE : conf.eee_mode == MEPA_EEE_ENABLE ? VTSS_EEE_ENABLE : VTSS_EEE_REG_UPDATE);
    eee_conf.eee_ena_phy = conf.eee_ena_phy;
    return vtss_phy_eee_conf_set(data->vtss_instance, data->port_no, eee_conf);
}

static mepa_rc phy_eee_mode_conf_get(mepa_device_t *dev, mepa_phy_eee_conf_t *const conf)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    mepa_rc rc = MEPA_RC_OK;
    mepa_bool_t capable = FALSE;
    vtss_phy_eee_conf_t eee_conf = {0};
    if ((rc = vtss_phy_port_eee_capable(data->vtss_instance, data->port_no, &capable)) != MEPA_RC_OK) {
        return rc;
    }
    if ((rc = vtss_phy_eee_conf_get(data->vtss_instance, data->port_no, &eee_conf)) != MEPA_RC_OK) {
        return rc;
    }
    conf->eee_mode = (eee_conf.eee_mode == VTSS_EEE_DISABLE ? MEPA_EEE_DISABLE : eee_conf.eee_mode == VTSS_EEE_ENABLE ? MEPA_EEE_ENABLE : MEPA_EEE_REG_UPDATE);
    conf->eee_ena_phy = eee_conf.eee_ena_phy;
    return MEPA_RC_OK;
}

static mepa_rc phy_eee_status_get(mepa_device_t *dev, u8 *const advertisement, BOOL *const rx_in_power_save_state, BOOL *const tx_in_power_save_state)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    mepa_rc rc = MEPA_RC_OK;
    u8 eee_link_advertisement;
    mepa_bool_t  eee_rx_in_power_save_state, eee_tx_in_power_save_state;
    if((rc = vtss_phy_eee_link_partner_advertisements_get(data->vtss_instance, data->port_no, &eee_link_advertisement)) != MEPA_RC_OK) {
        return rc;
    }
    if((rc =vtss_phy_eee_power_save_state_get(data->vtss_instance, data->port_no, &eee_rx_in_power_save_state, &eee_tx_in_power_save_state)) != MEPA_RC_OK) {
        return rc;
    }
    *advertisement = eee_link_advertisement;
    *rx_in_power_save_state = eee_rx_in_power_save_state;
    *tx_in_power_save_state = eee_tx_in_power_save_state;
    return MEPA_RC_OK;
}

// To get Viper capability
static uint32_t viper_1g_capability(struct mepa_device *dev , uint32_t capability)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    vtss_phy_type_t phy_id;
    uint32_t c = 0;
    mesa_rc rc;

    switch(capability) {
#ifdef VTSS_FEATURE_MACSEC
    case MEPA_CAP_MACSEC_SECY_CNT:
        c = MEPA_MACSEC_1G_MAX_SA/2;
        break;
    case MEPA_CAP_MACSEC_MAX_SA:
        c = MEPA_MACSEC_1G_MAX_SA;
        break;
    case MEPA_CAP_MACSEC_MAX_SC:
        c = MEPA_MACSEC_1G_MAX_SA/2;
        break;
#endif
    case MEPA_CAP_SPEED_1G:
        c = 1;
        break;
    case MEPA_CAP_TS_GEN_2:
        rc = vtss_phy_id_get(data->vtss_instance, data->port_no, &phy_id);
        if (rc != MESA_RC_OK) {
            goto out;
        }

        if (phy_id.part_number == VTSS_PHY_TYPE_8582 ||
            phy_id.part_number == VTSS_PHY_TYPE_8584 ||
            phy_id.part_number == VTSS_PHY_TYPE_8575 ||
            phy_id.part_number == VTSS_PHY_TYPE_8586) {
            c = 1;
        }
        break;
    case MEPA_CAP_TS_NONE:
        if (!viper_1g_capability(dev, MEPA_CAP_TS_GEN_2)) {
            c = 1;
        }
        break;
    default:
        c = 0;
        break;
    }

out:
    return c;
}

// To get Tesla capability
static uint32_t tesla_1g_capability(struct mepa_device *dev , uint32_t capability)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    vtss_phy_type_t phy_id;
    uint32_t c = 0;
    mesa_rc rc;

    switch(capability) {
    case MEPA_CAP_SPEED_1G:
        c = 1;
        break;
    case MEPA_CAP_TS_GEN_1:
#if defined(VTSS_OPT_PHY_TIMESTAMP)
        // Do not report TS capability when PHY timestamping is opt-out
        rc = vtss_phy_id_get(data->vtss_instance, data->port_no, &phy_id);
        if (rc != MESA_RC_OK) {
            goto out;
        }

        if (phy_id.part_number == VTSS_PHY_TYPE_8574 ||
            phy_id.part_number == VTSS_PHY_TYPE_8572) {
            c = 1;
        }
#endif
        break;
    case MEPA_CAP_TS_NONE:
        if (!tesla_1g_capability(dev, MEPA_CAP_TS_GEN_1)) {
            c = 1;
        }
        break;
    default:
        c = 0;
        break;
    }

out:
    return c;
}

// To get 1G capability
static uint32_t phy_1g_capability(struct mepa_device *dev , uint32_t capability)
{
    uint32_t c;

    switch (capability) {
    case MEPA_CAP_SPEED_1G:
        c = 1;
        break;
    case MEPA_CAP_TS_NONE:
        c = 1;
        break;
    default:
        c = 0;
        break;
    }

    return c;
}

// The vtss base debug-print API (vtss_phy_debug_info_print / vtss_macsec_dbg_reg_dump)
// renders through a printf-style vtss_debug_printf_t callback. This shim renders each
// invocation with vsnprintf and appends it to the active lmu_ss_t string-stream, so the
// MEPA ss-based debug dump can drive the unchanged vtss base. The dump path is
// serialized by MEPA_ENTER(), so the file-scope stream pointer needs no further locking.
// Shared with vtss_macsec.c via vtss_private.h.
lmu_ss_t *vtss_dbg_ss;

int vtss_dbg_ss_printf(const char *fmt, ...)
{
    va_list args;
    char    buf[512];

    va_start(args, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (vtss_dbg_ss != NULL) {
        LMU_SS_FMT(vtss_dbg_ss, "%s", buf);
    }
    return 0;
}

// Debug dump API for PHY
mepa_rc phy_debug_info_dump(struct mepa_device *dev,
                            lmu_ss_t *const ss,
                            const mepa_debug_info_t   *const info)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    vtss_port_no_t    port_no = data->port_no;
    vtss_debug_info_t phy_info;

    memset(&phy_info, 0, sizeof(phy_info));

    // Map from MESA to PHY info
    phy_info.layer = (info->layer == MEPA_DEBUG_LAYER_AIL ? VTSS_DEBUG_LAYER_AIL :
                      info->layer == MEPA_DEBUG_LAYER_CIL ? VTSS_DEBUG_LAYER_CIL :
                      VTSS_DEBUG_LAYER_ALL);
    if (info->group == MEPA_DEBUG_GROUP_ALL) {
        phy_info.group = VTSS_DEBUG_GROUP_ALL;
    } else if (info->group == MEPA_DEBUG_GROUP_PHY) {
        phy_info.group = VTSS_DEBUG_GROUP_PHY;
    } else if (info->group == MEPA_DEBUG_GROUP_PHY_TS) {
        phy_info.group = VTSS_DEBUG_GROUP_PHY_TS;
    } else if (info->group == MEPA_DEBUG_GROUP_MACSEC) {
        phy_info.group = VTSS_DEBUG_GROUP_MACSEC;
    } else {
        return MESA_RC_OK;
    }
    phy_info.port_list[port_no] = 1;
    phy_info.full = info->full;
    phy_info.clear = info->clear;
    phy_info.vml_format = info->vml_format;

    mepa_rc rc;
    vtss_dbg_ss = ss;
    rc = vtss_phy_debug_info_print(data->vtss_instance, vtss_dbg_ss_printf, &phy_info);
    vtss_dbg_ss = NULL;
    return rc;
}

// API for QSGMII synchronization
mepa_rc phy_1g_qsgmii_sync(struct mepa_device *dev)
{
    phy_data_t *data = (phy_data_t *)(dev->data);

    return vtss_phy_1g_qsgmii_sync(data->vtss_instance, data->port_no);
}

/*
Address is in this format
[15:0] -> Register address
[20:16] -> Device ID (MMD device id)
[22:21] -> Port ID
[23] -> Read/Write
*/
static mepa_rc phy_10g_clause45_read(struct mepa_device *dev,
                                     uint32_t address, uint16_t *const value)
{
    mepa_rc rc = MEPA_RC_OK;
    phy_data_t *data = (phy_data_t *)dev->data;
    uint16_t page_add = (address >> 16) & 0xffff;
    uint16_t mmd = (page_add & 0x1f);
    uint16_t addr = address & 0xffff;
    uint32_t data_val = 0;

    if (mmd) {
        rc = vtss_phy_10g_csr_read(data->vtss_instance, data->port_no, mmd, addr, &data_val);
        *value = (uint16_t)data_val;
    }

    return rc;

}
/*
Address is in this format
[15:0] -> Register address
[20:16] -> Device ID (MMD device id)
[22:21] -> Port ID
[23] -> Read/Write
*/
static mepa_rc phy_10g_clause45_write(struct mepa_device *dev,
                                      uint32_t address, uint16_t value)
{
    mepa_rc rc = MEPA_RC_OK;
    phy_data_t *data = (phy_data_t *)dev->data;
    uint16_t page_add = (address >> 16) & 0xffff;
    uint16_t mmd = (page_add & 0x1f);
    uint16_t addr = address & 0xffff;

    if (mmd) {
        rc = vtss_phy_10g_csr_write(data->vtss_instance, data->port_no, mmd, addr, value);
    }
    return rc;

}

static mepa_rc malibu_10g_event_enable_set(struct mepa_device *dev,
        mepa_event_t event, mesa_bool_t enable)
{
    mepa_rc rc = MEPA_RC_OK;
    phy_data_t *data = (phy_data_t *)dev->data;
    // No need to use VTSS_ENTER since vtss_phy_10g_event_enable_set() it self has the
    // lock
    rc = vtss_phy_10g_event_enable_set(data->vtss_instance, data->port_no, event, enable);
    return rc;
}

static mepa_rc malibu_10g_event_enable_get(struct mepa_device *dev,
        mepa_event_t *const event)
{
    mepa_rc rc = MEPA_RC_OK;
    phy_data_t *data = (phy_data_t *)dev->data;
    T_D(MEPA_TRACE_GRP_GEN, "Inside malibu_10g_event_enable_get");
    // No need to use VTSS_ENTER since vtss_phy_10g_event_enable_get() it self has the
    // lock
    rc = vtss_phy_10g_event_enable_get(data->vtss_instance, data->port_no, event);
    return rc;
}


static mepa_rc malibu_10g_gpio_write(struct mepa_device *dev,
                                     uint8_t gpio_no, mepa_bool_t value)
{
    mepa_rc rc = MEPA_RC_OK;
    phy_data_t *data = (phy_data_t *)dev->data;
    rc = vtss_phy_10g_gpio_write(data->vtss_instance, data->port_no, gpio_no, value);
    return rc;
}

static mepa_rc malibu_10g_gpio_read(struct mepa_device *dev,
                                    uint8_t gpio_no, mepa_bool_t *const value)
{
    mepa_rc rc = MEPA_RC_OK;
    phy_data_t *data = (phy_data_t *)dev->data;
    rc = vtss_phy_10g_gpio_read(data->vtss_instance, data->port_no, gpio_no, value);
    return rc;
}

static mepa_rc malibu_10g_power_set(struct mepa_device *dev,
                                    mepa_power_mode_t power)
{
    mepa_rc rc = MEPA_RC_OK;
    phy_data_t *data = (phy_data_t *)dev->data;
    vtss_phy_10g_power_t power_status;

    switch(power) {
    case MESA_PHY_POWER_NOMINAL:
        power_status=VTSS_PHY_10G_POWER_DISABLE;
        break;
    case MESA_PHY_POWER_ACTIPHY:
    case MESA_PHY_POWER_DYNAMIC:
    case MESA_PHY_POWER_ENABLED:
        power_status= VTSS_PHY_10G_POWER_ENABLE;
        break;
    }
    rc= vtss_phy_10g_power_set(data->vtss_instance, data->port_no, &power_status);
    return rc;
}

static mepa_rc malibu_10g_event_poll(struct mepa_device *dev, mepa_event_t *const ev_mask)
{
    mepa_rc rc=MEPA_RC_OK;
    phy_data_t *data =(phy_data_t*)dev->data;
    rc=vtss_phy_10g_event_poll(data->vtss_instance, data->port_no ,ev_mask);
    return rc;
}

static uint32_t malibu_10g_capability(struct mepa_device *dev , uint32_t capability)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    vtss_phy_10g_id_t phy_id = {};
    uint32_t c = 0;
    mesa_rc rc;

    switch(capability) {
    case MEPA_CAP_MACSEC_SECY_CNT:
        c = MEPA_MACSEC_10G_MAX_SA/2;
        break;
    case MEPA_CAP_MACSEC_MAX_SA:
        c = MEPA_MACSEC_10G_MAX_SA;
        break;
    case MEPA_CAP_MACSEC_MAX_SC:
        c = MEPA_MACSEC_10G_MAX_SA/2;
        break;
    case MEPA_CAP_SPEED_10G:
        c = 1;
        break;
    case MEPA_CAP_TS_GEN_2:
        rc = vtss_phy_10g_id_get(data->vtss_instance, data->port_no, &phy_id);
        if (rc != MESA_RC_OK) {
            goto out;
        }

        if (phy_id.family == VTSS_PHY_FAMILY_MALIBU) {
            c = 1;
        }
        break;
    default:
        c = 0;
        break;
    }

out:
    return c;
}

static uint32_t venice_10g_capability(struct mepa_device *dev , uint32_t capability)
{
    phy_data_t *data = (phy_data_t *)(dev->data);
    vtss_phy_10g_id_t phy_id = {};
    uint32_t c = 0;
    mesa_rc rc;

    rc = vtss_phy_10g_id_get(data->vtss_instance, data->port_no, &phy_id);
    if (rc != MESA_RC_OK) {
        goto out;
    }

    switch(capability) {
    case MEPA_CAP_SPEED_10G:
        c = 1;
        break;
    case MEPA_CAP_TS_GEN_1:
        if ((phy_id.part_number == 0x8488 || phy_id.part_number == 0x8487) &&
            phy_id.revision >= 4) {
            c = 1;
        }
        break;
    case MEPA_CAP_TS_GEN_2:
        if ((phy_id.part_number == 0x8489 && !(phy_id.device_feature_status & VTSS_PHY_10G_TIMESTAMP_DISABLED)) ||
            phy_id.part_number == 0x8490 || phy_id.part_number == 0x8491) {
            c = 1;
        }
        break;
    case MEPA_CAP_TS_NONE:
        if (!malibu_10g_capability(dev, MEPA_CAP_TS_GEN_1) &&
            !malibu_10g_capability(dev, MEPA_CAP_TS_GEN_2)) {
            c = 1;
        }
        break;
    default:
        c = 0;
        break;
    }
out:
    return c;
}

/* I2C master SCL prescaler. */
#define MALIBU_I2C_PRESCALE_DEFAULT 0x50

/* word_access is unused on Malibu (byte-oriented I2C master). */
static mepa_rc malibu_10g_i2c_read(struct mepa_device *dev,
                                   uint8_t             i2c_mux,
                                   uint8_t             i2c_reg_addr,
                                   uint8_t             i2c_dev_addr,
                                   mepa_bool_t         word_access,
                                   uint8_t             cnt,
                                   uint8_t *const      value)
{
    phy_data_t                   *data = (phy_data_t *)dev->data;
    vtss_phy_10g_i2c_slave_conf_t slave_cfg = {.slave_id = i2c_dev_addr,
                                               .prescale = MALIBU_I2C_PRESCALE_DEFAULT};
    uint8_t                       i;

    if (vtss_phy_10g_i2c_slave_conf_set(data->vtss_instance, data->port_no, &slave_cfg) !=
        VTSS_RC_OK) {
        return VTSS_RC_ERROR;
    }

    /* Slave 0x56 = de-facto-standard CuSFP I2C-MDIO bridge address. 
     * Observed behavior: the bridge serves a 16-bit MDIO register across two
     * single-byte I2C transactions targeting the SAME sub-addr - 1st read
     * returns the hi byte, 2nd read returns the lo byte.
     *
     * The default byte-stream loop (sub-addr N then N+1) does not work for
     * this slave: the second transaction is NACKed on the wire, and because
     * Malibu's I2C master has no NACK detection on reads, it ignores the
     * NACK, clocks 8 bits over an idle (pulled-up) SDA line, and reports
     * 0xFF as the data byte. Result: hi byte of reg N alternating with 0xFF
     * across consecutive transactions.
     *
     * For bridge access we read the SAME sub-addr `cnt` times to land hi+lo
     * of one MDIO register. EEPROM slaves (0x50/0x51) keep the normal
     * incrementing-sub-addr loop. */
    const uint8_t i2c_mdio_slave_addr = 0x56;
    mepa_bool_t   is_i2c_mdio_bridge = (i2c_dev_addr == i2c_mdio_slave_addr);

    for (i = 0; i < cnt; i++) {
        uint8_t sub_addr = is_i2c_mdio_bridge ? i2c_reg_addr : (uint8_t)(i2c_reg_addr + i);
        if (vtss_phy_10g_i2c_read(data->vtss_instance, data->port_no, sub_addr, &value[i]) !=
            VTSS_RC_OK) {
            return VTSS_RC_ERROR;
        }
    }
    return VTSS_RC_OK;
}

static mepa_rc malibu_10g_i2c_write(struct mepa_device  *dev,
                                    uint8_t              i2c_mux,
                                    uint8_t              i2c_reg_addr,
                                    uint8_t              i2c_dev_addr,
                                    mepa_bool_t          word_access,
                                    uint8_t              cnt,
                                    const uint8_t *const value)
{
    phy_data_t                   *data = (phy_data_t *)dev->data;
    vtss_phy_10g_i2c_slave_conf_t slave_cfg = {.slave_id = i2c_dev_addr,
                                               .prescale = MALIBU_I2C_PRESCALE_DEFAULT};
    uint8_t                       i;

    if (vtss_phy_10g_i2c_slave_conf_set(data->vtss_instance, data->port_no, &slave_cfg) !=
        VTSS_RC_OK) {
        return VTSS_RC_ERROR;
    }

    /* See malibu_10g_i2c_read for the rationale: bridge slave 0x56 needs both
     * I2C transactions at the same sub-addr to land a single 16-bit MDIO
     * write (1st = hi byte, 2nd = lo byte). EEPROM slaves use the normal
     * incrementing loop. */
    const uint8_t i2c_mdio_slave_addr = 0x56;
    mepa_bool_t   is_i2c_mdio_bridge = (i2c_dev_addr == i2c_mdio_slave_addr);

    for (i = 0; i < cnt; i++) {
        uint8_t sub_addr = is_i2c_mdio_bridge ? i2c_reg_addr : (uint8_t)(i2c_reg_addr + i);
        if (vtss_phy_10g_i2c_write(data->vtss_instance, data->port_no, sub_addr, &value[i]) !=
            VTSS_RC_OK) {
            return VTSS_RC_ERROR;
        }
    }
    return VTSS_RC_OK;
}

static mepa_rc phy_10g_warmrestart_conf_end(struct mepa_device *dev) {
    mepa_rc rc = MEPA_RC_OK;
    phy_data_t *data =(phy_data_t*)dev->data;
    data->vtss_instance->init_conf.restart_info_port = data->port_no;
    data->vtss_instance->init_conf.warm_start_enable = TRUE;
    data->vtss_instance->init_conf.restart_info_src = VTSS_RESTART_INFO_SRC_10G_PHY;
    if(data->vtss_instance->warm_start_cur) {
        data->vtss_instance->warm_start_cur = 0; /* To sync up the registers with the previous instance */

        /* Apply sync configurations */
        if((rc = vtss_phy_10g_sync(data->vtss_instance, data->port_no) != MEPA_RC_OK)) {
            T_D(MEPA_TRACE_GRP_GEN, "vtss_phy_10g_sync port(%d) return rc(0x%04X)", data->port_no, rc);
            return rc;
        }
#if defined(VTSS_FEATURE_WIS)
        if ((rc = vtss_phy_ewis_sync(data->vtss_instance, data->port_no)) != MEPA_RC_OK) {
            return rc;
        }
#endif /* VTSS_FEATURE_WIS */
#if defined (VTSS_FEATURE_PHY_TIMESTAMP)
        if((rc = vtss_phy_ts_sync(data->vtss_instance, data->port_no)) != MEPA_RC_OK) {
            T_D(MEPA_TRACE_GRP_GEN, "vtss_phy_ts_sync port(%d) return rc(0x%04X)", data->port_no, rc);
            return rc;
        }
#endif /* VTSS_FEATURE_PHY_TIMESTAMP */
#if defined (VTSS_FEATURE_MACSEC)
        if((rc = vtss_macsec_sync(data->vtss_instance, data->port_no)) != MEPA_RC_OK) {
            T_D(MEPA_TRACE_GRP_GEN, "vtss_macsec_sync port(%d) return rc(0x%04X)", data->port_no, rc);
            return rc;
        }
#endif /* VTSS_FEATURE_MACSEC */
    }
    if((rc = vtss_phy_10g_restart_conf_set(data->vtss_instance)) != MEPA_RC_OK ) {
        return rc;
    }

    return MEPA_RC_OK;
}

static mepa_rc phy_10g_warmrestart_conf_set(struct mepa_device *dev, const mepa_restart_t restart) {
    mepa_rc rc = MEPA_RC_OK;
    phy_data_t *data =(phy_data_t*)dev->data;
    data->vtss_instance->restart_cur = restart;
    data->vtss_instance->init_conf.restart_info_port = data->port_no;
    data->vtss_instance->init_conf.warm_start_enable = TRUE;
    data->vtss_instance->init_conf.restart_info_src = VTSS_RESTART_INFO_SRC_10G_PHY;
    if((rc = vtss_phy_10g_restart_conf_set(data->vtss_instance)) != MEPA_RC_OK) {
        return rc;
    }
    return MEPA_RC_OK;
}

static mepa_rc phy_10g_warmrestart_conf_get(struct mepa_device *dev, mepa_restart_t *const restart) {
    mepa_rc rc = MEPA_RC_OK;
    phy_data_t *data =(phy_data_t*)dev->data;
    *restart = data->vtss_instance->restart_cur;
    return rc;
}

static mepa_rc mepa_to_vtss_synce_conf(mepa_synce_clock_conf_t conf, vtss_phy_10g_lane_sync_conf_t *lane_sync)
{
    mepa_rc rc = MEPA_RC_OK;
    switch(conf.src) {
    case MEPA_SYNCE_CLOCK_SRC_LINE0:
    case MEPA_SYNCE_CLOCK_SRC_LINE1:
    case MEPA_SYNCE_CLOCK_SRC_LINE2:
    case MEPA_SYNCE_CLOCK_SRC_LINE3:
        lane_sync->rx_macro = VTSS_PHY_10G_RX_MACRO_LINE;
        lane_sync->tx_macro = VTSS_PHY_10G_TX_MACRO_LINE;
        lane_sync->rx_ch = lane_sync->tx_ch = (uint8_t)(conf.src - MEPA_SYNCE_CLOCK_SRC_LINE0);
        break;
    case MEPA_SYNCE_CLOCK_SRC_HOST0:
    case MEPA_SYNCE_CLOCK_SRC_HOST1:
    case MEPA_SYNCE_CLOCK_SRC_HOST2:
    case MEPA_SYNCE_CLOCK_SRC_HOST3:
        lane_sync->rx_macro = VTSS_PHY_10G_RX_MACRO_HOST;
        lane_sync->tx_macro = VTSS_PHY_10G_TX_MACRO_HOST;
        lane_sync->rx_ch = lane_sync->tx_ch = (uint8_t)(conf.src - MEPA_SYNCE_CLOCK_SRC_HOST0);
        break;
    case MEPA_SYNCE_CLOCK_SRC_SREFCLK:
        lane_sync->rx_macro = VTSS_PHY_10G_RX_MACRO_SREFCLK;
        lane_sync->tx_macro = VTSS_PHY_10G_TX_MACRO_LINE;
        lane_sync->rx_ch = lane_sync->tx_ch = 0;
        break;
    case MEPA_SYNCE_CLOCK_SRC_DISABLED:
        break;
    default:
        rc = MEPA_RC_ERROR;
        break;
    }
    return rc;
}

static mepa_rc mepa_to_vtss_sckout_conf(mepa_synce_clock_conf_t conf, vtss_phy_10g_sckout_conf_t *sckout)
{
    mepa_rc rc = MEPA_RC_OK;
    switch (conf.squelch.squelch_src) {
        case MEPA_SYNCE_NO_SQUELCH:           sckout->src = VTSS_CKOUT_NO_SQUELCH; break;
        // LINE link squelch
        case MEPA_SYNCE_SQUELCH_LINK_LINE0:   sckout->src = VTSS_CKOUT_SQUELCH_SRC_LINK_LINE0; break;
        case MEPA_SYNCE_SQUELCH_LINK_LINE1:   sckout->src = VTSS_CKOUT_SQUELCH_SRC_LINK_LINE1; break;
        case MEPA_SYNCE_SQUELCH_LINK_LINE2:   sckout->src = VTSS_CKOUT_SQUELCH_SRC_LINK_LINE2; break;
        case MEPA_SYNCE_SQUELCH_LINK_LINE3:   sckout->src = VTSS_CKOUT_SQUELCH_SRC_LINK_LINE3; break;
        // LINE LOS squelch
        case MEPA_SYNCE_SQUELCH_LOS_LINE0:    sckout->src = VTSS_CKOUT_SQUELCH_SRC_LOS_LINE0; break;
        case MEPA_SYNCE_SQUELCH_LOS_LINE1:    sckout->src = VTSS_CKOUT_SQUELCH_SRC_LOS_LINE1; break;
        case MEPA_SYNCE_SQUELCH_LOS_LINE2:    sckout->src = VTSS_CKOUT_SQUELCH_SRC_LOS_LINE2; break;
        case MEPA_SYNCE_SQUELCH_LOS_LINE3:    sckout->src = VTSS_CKOUT_SQUELCH_SRC_LOS_LINE3; break;
        // HOST link squelch
        case MEPA_SYNCE_SQUELCH_LINK_HOST0:   sckout->src = VTSS_CKOUT_SQUELCH_SRC_LINK_HOST0; break;
        case MEPA_SYNCE_SQUELCH_LINK_HOST1:   sckout->src = VTSS_CKOUT_SQUELCH_SRC_LINK_HOST1; break;
        case MEPA_SYNCE_SQUELCH_LINK_HOST2:   sckout->src = VTSS_CKOUT_SQUELCH_SRC_LINK_HOST2; break;
        case MEPA_SYNCE_SQUELCH_LINK_HOST3:   sckout->src = VTSS_CKOUT_SQUELCH_SRC_LINK_HOST3; break;
        // HOST LOS squelch
        case MEPA_SYNCE_SQUELCH_LOS_HOST0:    sckout->src = VTSS_CKOUT_SQUELCH_SRC_LOS_HOST0; break;
        case MEPA_SYNCE_SQUELCH_LOS_HOST1:    sckout->src = VTSS_CKOUT_SQUELCH_SRC_LOS_HOST1; break;
        case MEPA_SYNCE_SQUELCH_LOS_HOST2:    sckout->src = VTSS_CKOUT_SQUELCH_SRC_LOS_HOST2; break;
        case MEPA_SYNCE_SQUELCH_LOS_HOST3:    sckout->src = VTSS_CKOUT_SQUELCH_SRC_LOS_HOST3; break;
        default:                              rc = MEPA_RC_ERROR; break;
    }

    return rc;
}

static mepa_rc mepa_to_vtss_ckout_conf(const mepa_synce_clock_conf_t *conf, vtss_phy_10g_ckout_conf_t *ckout)
{
    // Map source
    switch (conf->src) {
        case MEPA_SYNCE_CLOCK_SRC_LINE0: ckout->mode = VTSS_CKOUT_LINE0_RECVRD_CLOCK; break;
        case MEPA_SYNCE_CLOCK_SRC_LINE1: ckout->mode = VTSS_CKOUT_LINE1_RECVRD_CLOCK; break;
        case MEPA_SYNCE_CLOCK_SRC_LINE2: ckout->mode = VTSS_CKOUT_LINE2_RECVRD_CLOCK; break;
        case MEPA_SYNCE_CLOCK_SRC_LINE3: ckout->mode = VTSS_CKOUT_LINE3_RECVRD_CLOCK; break;
        case MEPA_SYNCE_CLOCK_SRC_HOST0: ckout->mode = VTSS_CKOUT_HOST0_RECVRD_CLOCK; break;
        case MEPA_SYNCE_CLOCK_SRC_HOST1: ckout->mode = VTSS_CKOUT_HOST1_RECVRD_CLOCK; break;
        case MEPA_SYNCE_CLOCK_SRC_HOST2: ckout->mode = VTSS_CKOUT_HOST2_RECVRD_CLOCK; break;
        case MEPA_SYNCE_CLOCK_SRC_HOST3: ckout->mode = VTSS_CKOUT_HOST3_RECVRD_CLOCK; break;
        case MEPA_SYNCE_CLOCK_SRC_DISABLED: break;
        default:
            T_E(MEPA_TRACE_GRP_GEN, "Invalid CKOUT Source selected\n");
            return MEPA_RC_ERROR;
    }

    // Map squelch source
    switch (conf->squelch.squelch_src) {
        // LINE link squelch
        case MEPA_SYNCE_NO_SQUELCH:           ckout->src = VTSS_CKOUT_NO_SQUELCH; break;
        case MEPA_SYNCE_SQUELCH_LINK_LINE0:   ckout->src = VTSS_CKOUT_SQUELCH_SRC_LINK_LINE0; break;
        case MEPA_SYNCE_SQUELCH_LINK_LINE1:   ckout->src = VTSS_CKOUT_SQUELCH_SRC_LINK_LINE1; break;
        case MEPA_SYNCE_SQUELCH_LINK_LINE2:   ckout->src = VTSS_CKOUT_SQUELCH_SRC_LINK_LINE2; break;
        case MEPA_SYNCE_SQUELCH_LINK_LINE3:   ckout->src = VTSS_CKOUT_SQUELCH_SRC_LINK_LINE3; break;
        // LINE LOS squelch
        case MEPA_SYNCE_SQUELCH_LOS_LINE0:    ckout->src = VTSS_CKOUT_SQUELCH_SRC_LOS_LINE0; break;
        case MEPA_SYNCE_SQUELCH_LOS_LINE1:    ckout->src = VTSS_CKOUT_SQUELCH_SRC_LOS_LINE1; break;
        case MEPA_SYNCE_SQUELCH_LOS_LINE2:    ckout->src = VTSS_CKOUT_SQUELCH_SRC_LOS_LINE2; break;
        case MEPA_SYNCE_SQUELCH_LOS_LINE3:    ckout->src = VTSS_CKOUT_SQUELCH_SRC_LOS_LINE3; break;
        // HOST link squelch
        case MEPA_SYNCE_SQUELCH_LINK_HOST0:   ckout->src = VTSS_CKOUT_SQUELCH_SRC_LINK_HOST0; break;
        case MEPA_SYNCE_SQUELCH_LINK_HOST1:   ckout->src = VTSS_CKOUT_SQUELCH_SRC_LINK_HOST1; break;
        case MEPA_SYNCE_SQUELCH_LINK_HOST2:   ckout->src = VTSS_CKOUT_SQUELCH_SRC_LINK_HOST2; break;
        case MEPA_SYNCE_SQUELCH_LINK_HOST3:   ckout->src = VTSS_CKOUT_SQUELCH_SRC_LINK_HOST3; break;
        // HOST LOS squelch
        case MEPA_SYNCE_SQUELCH_LOS_HOST0:    ckout->src = VTSS_CKOUT_SQUELCH_SRC_LOS_HOST0; break;
        case MEPA_SYNCE_SQUELCH_LOS_HOST1:    ckout->src = VTSS_CKOUT_SQUELCH_SRC_LOS_HOST1; break;
        case MEPA_SYNCE_SQUELCH_LOS_HOST2:    ckout->src = VTSS_CKOUT_SQUELCH_SRC_LOS_HOST2; break;
        case MEPA_SYNCE_SQUELCH_LOS_HOST3:    ckout->src = VTSS_CKOUT_SQUELCH_SRC_LOS_HOST3; break;
        default:                              ckout->src = VTSS_CKOUT_NO_SQUELCH; break;
    }


    // Map frequency
    switch (conf->freq) {
        case MEPA_FREQ_125M:      ckout->freq = VTSS_PHY_10G_CLK_FULL_RATE;   break;   // 1G: 125MHz
        case MEPA_FREQ_62_5M:     ckout->freq = VTSS_PHY_10G_CLK_DIVIDE_BY_2; break;   // 1G: 62.5MHz
        case MEPA_FREQ_161_13M:   ckout->freq = VTSS_PHY_10G_CLK_DIVIDE_BY_2; break;   // 10G LAN: 161.13MHz
        case MEPA_FREQ_155_52M:   ckout->freq = VTSS_PHY_10G_CLK_DIVIDE_BY_2; break;   // 10G WAN: 155.52MHz
        case MEPA_FREQ_311_04M:   ckout->freq = VTSS_PHY_10G_CLK_FULL_RATE;   break;   // 10G WAN: 311.04MHz
        case MEPA_FREQ_322_27M:   ckout->freq = VTSS_PHY_10G_CLK_FULL_RATE;   break;   // 10G LAN: 322.27MHz
        default: ckout->freq = VTSS_PHY_10G_CLK_FULL_RATE; break;
    }

    ckout->squelch_inv = conf->squelch.squelch_inv;

    // CKOUT selector
    switch (conf->dst) {
        case MEPA_SYNCE_CLOCK_DST_1: ckout->ckout_sel = VTSS_CKOUT0; break;
        case MEPA_SYNCE_CLOCK_DST_2: ckout->ckout_sel = VTSS_CKOUT1; break;
        case MEPA_SYNCE_CLOCK_DST_3: ckout->ckout_sel = VTSS_CKOUT2; break;
        case MEPA_SYNCE_CLOCK_DST_4: ckout->ckout_sel = VTSS_CKOUT3; break;
        default: return MEPA_RC_ERROR;
    }

    return MEPA_RC_OK;
}

static mepa_rc phy_10g_synce_clk_conf_set(mepa_device_t *dev, const mepa_synce_clock_conf_t *conf)
{
    phy_data_t *data =(phy_data_t*)dev->data;

    T_D(MEPA_TRACE_GRP_GEN, "phy_10g_synce_clk_conf_set : conf->src : %d, conf->freq : %d, conf->dst : %d\n", conf->src, conf->freq, conf->dst);
    vtss_phy_10g_lane_sync_conf_t lane_sync = {0};
    lane_sync.enable = TRUE;

    if (mepa_to_vtss_synce_conf(*conf, &lane_sync) != VTSS_RC_OK) {
        T_E(MEPA_TRACE_GRP_GEN, "Invalid SyncE clock source on port : %d\n", data->port_no);
        return MEPA_RC_ERROR;
    }

    if (conf->src == MEPA_SYNCE_CLOCK_SRC_SREFCLK) {
        vtss_phy_10g_srefclk_mode_t sref_clk = {0};
        sref_clk.enable = TRUE;

        if (conf->freq == MEPA_FREQ_156_25M) {
            sref_clk.freq = VTSS_PHY_10G_SREFCLK_156_25;
        } else if (conf->freq == MEPA_FREQ_125M) {
            sref_clk.freq = VTSS_PHY_10G_SREFCLK_125_00;
        } else {
            T_E(MEPA_TRACE_GRP_GEN, "Invalid SREFCLK frequency selected on port : %d\n", data->port_no);
            return MEPA_RC_ERROR;
        }

        if(vtss_phy_10g_srefclk_conf_set(data->vtss_instance, data->port_no, &sref_clk) != VTSS_RC_OK) {
            T_E(MEPA_TRACE_GRP_GEN, "Error in configuring SREFCLK on port : %d\n", data->port_no);
            return MEPA_RC_ERROR;
        }
    }

    T_D(MEPA_TRACE_GRP_GEN, "lane_sync.tx_macro : %d, lane_sync.rx_macro : %d, lane_sync.rx_ch : %d, lane_sync.tx_ch : %d\n",
        lane_sync.tx_macro, lane_sync.rx_macro, lane_sync.rx_ch, lane_sync.tx_ch);

    if (conf->src != MEPA_SYNCE_CLOCK_SRC_DISABLED) {
        if (vtss_phy_10g_lane_sync_set(data->vtss_instance, data->port_no, &lane_sync) != VTSS_RC_OK) {
            T_E(MEPA_TRACE_GRP_GEN, "vtss_phy_10g_lane_sync_set failed on port : %d\n", data->port_no);
            return MEPA_RC_ERROR;
        }
    }

    // SCKOUT configuration
    if (conf->dst == MEPA_SYNCE_CLOCK_DST_SCKOUT) {

        T_D(MEPA_TRACE_GRP_GEN, "SCKOUT Configuration on port : %d\n", data->port_no);
        vtss_phy_10g_sckout_conf_t sckout = {0};

        sckout.enable = TRUE;

        // Map source
        if (conf->src >= MEPA_SYNCE_CLOCK_SRC_LINE0 && conf->src <= MEPA_SYNCE_CLOCK_SRC_LINE3) {
            sckout.mode = VTSS_PHY_10G_LINE0_RECVRD_CLOCK + (conf->src - MEPA_SYNCE_CLOCK_SRC_LINE0);
        } else if (conf->src >= MEPA_SYNCE_CLOCK_SRC_HOST0 && conf->src <= MEPA_SYNCE_CLOCK_SRC_HOST3) {
            sckout.mode = VTSS_PHY_10G_HOST0_RECVRD_CLOCK + (conf->src - MEPA_SYNCE_CLOCK_SRC_HOST0);
        } else if (conf->src == MEPA_SYNCE_CLOCK_SRC_SREFCLK) {
            sckout.mode = VTSS_PHY_10G_SREFCLK;
        } else if (conf->src == MEPA_SYNCE_CLOCK_SRC_DISABLED) {
            sckout.mode = VTSS_PHY_10G_SYNC_DISABLE;
            sckout.enable = FALSE;
        } else {
            /* Unsupported source for SCKOUT output */
            T_E(MEPA_TRACE_GRP_GEN, "Invalid SyncE clock source for SCKOUT on port : %d\n", data->port_no);
            return MEPA_RC_ERROR;
        }

        // Map frequency
        if (sckout.enable) {
            if (conf->freq == MEPA_FREQ_156_25M) {
                sckout.freq = VTSS_PHY_10G_SCKOUT_156_25;
            } else if (conf->freq == MEPA_FREQ_125M) {
                sckout.freq = VTSS_PHY_10G_SCKOUT_125_00;
            } else {
                T_E(MEPA_TRACE_GRP_GEN, "Invalid SyncE frequency for SCKOUT on port : %d\n", data->port_no);
                return MEPA_RC_ERROR;
            }
        } else {
            // No frequency required when disabled
            sckout.freq = 0;
        }

        // Squelch
        if (mepa_to_vtss_sckout_conf(*conf, &sckout) != VTSS_RC_OK) {
            T_E(MEPA_TRACE_GRP_GEN, "Invalid SyncE squelch source on port : %d\n", data->port_no);
            return MEPA_RC_ERROR;
        }

        sckout.squelch_inv = conf->squelch.squelch_inv;

        T_D(MEPA_TRACE_GRP_GEN, "SCKOUT Config : sckout.mode : %d, sckout.src : %d, sckout.freq : %d, sckout.squelch_inv : %d, sckout.enable : %d\n",
            sckout.mode, sckout.src, sckout.freq, sckout.squelch_inv, sckout.enable);

        // Configure SCKOUT
        if (vtss_phy_10g_sckout_conf_set(data->vtss_instance, data->port_no, &sckout) != VTSS_RC_OK) {
            T_E(MEPA_TRACE_GRP_GEN, "vtss_phy_10g_sckout_set failed  on port : %d\n", data->port_no);
            return MEPA_RC_ERROR;

        }

        if (sckout.enable == FALSE) {
            //Lane Sync is NOT required when disabling SCKOUT
            return MEPA_RC_OK;
        }

        // Lane Sync for SCKOUT
        vtss_phy_10g_lane_sync_conf_t lane_sync = {0};
        lane_sync.enable = TRUE;
        if (mepa_to_vtss_synce_conf(*conf, &lane_sync) != VTSS_RC_OK) {
            return MEPA_RC_ERROR;
        }

        lane_sync.tx_macro = VTSS_PHY_10G_TX_MACRO_SCKOUT;
        if (vtss_phy_10g_lane_sync_set(data->vtss_instance, data->port_no, &lane_sync) != VTSS_RC_OK) {
            T_E(MEPA_TRACE_GRP_GEN, "SCKOUT: vtss_phy_10g_lane_sync_set failed on port : %d\n", data->port_no);
            return MEPA_RC_ERROR;
        }
        return MEPA_RC_OK;
    }

    // CKOUT configuration
    if (conf->dst >= MEPA_SYNCE_CLOCK_DST_1 && conf->dst <= MEPA_SYNCE_CLOCK_DST_4) {
        vtss_phy_10g_ckout_conf_t ckout = {0};
        if (mepa_to_vtss_ckout_conf(conf, &ckout) != MEPA_RC_OK) {
            T_E(MEPA_TRACE_GRP_GEN, "Invalid CKOUT Parameter selected on port : %d\n", data->port_no);
            return MEPA_RC_ERROR;
        }
        if (conf->src == MEPA_SYNCE_CLOCK_SRC_DISABLED) {
            ckout.enable = FALSE;
        } else {
            ckout.enable = TRUE;
        }

        if (vtss_phy_10g_ckout_conf_set(data->vtss_instance, data->port_no, &ckout) != VTSS_RC_OK) {
           T_E(MEPA_TRACE_GRP_GEN, "vtss_phy_10g_ckout_conf_set failed  on port : %d\n", data->port_no);
            return MEPA_RC_ERROR;
        }
    }
    return MEPA_RC_OK;
}

mepa_drivers_t mepa_mscc_driver_init(void)
{
    static const int nr_mscc_phy = 5;
    static mepa_driver_t mscc_drivers[] = {
        {
            // VTSS Atom PHY
            .id = 0x000706e0,
            .mask = 0xffffffe0,
            .mepa_driver_delete = mscc_1g_delete,
            .mepa_driver_reset = mscc_1g_reset,
            .mepa_driver_poll = mscc_1g_poll,
            .mepa_driver_conf_set = mscc_1g_conf_set,
            .mepa_driver_conf_get = phy_1g_conf_get,
            .mepa_driver_if_set = mscc_if_set,
            .mepa_driver_if_get = mscc_1g_if_get,
            .mepa_driver_power_set = mscc_1g_power_set,
            .mepa_driver_cable_diag_start = mscc_1g_veriphy_start,
            .mepa_driver_cable_diag_get = mscc_1g_veriphy_get,
            .mepa_driver_media_set = mscc_1g_media_set,
            .mepa_driver_probe = mscc_1g_probe,
            .mepa_driver_aneg_status_get = mscc_1g_status_1g_get,
            .mepa_driver_clause22_read = phy_1g_read,
            .mepa_driver_clause22_write = phy_1g_write,
            .mepa_driver_event_enable_set = phy_1g_event_enable_set,
            .mepa_driver_event_enable_get = phy_1g_event_enable_get,
            .mepa_driver_event_poll = phy_1g_event_poll,
            .mepa_driver_loopback_set = phy_1g_loopback_set,
            .mepa_driver_loopback_get = phy_1g_loopback_get,
            .mepa_driver_gpio_mode_set = phy_1g_gpio_mode,
            .mepa_driver_gpio_out_set = phy_1g_gpio_set,
            .mepa_driver_gpio_in_get = phy_1g_gpio_get,
            .mepa_driver_synce_clock_conf_set = phy_1g_synce_clk_conf_set,
            .mepa_driver_link_base_port = phy_1g_link_base_port,
            .mepa_driver_capability = phy_1g_capability,
            .mepa_driver_phy_info_get = phy_1g_info_get,
            .mepa_driver_isolate_mode_conf = phy_isolate_mode_conf,
            .mepa_driver_debug_info_dump = phy_debug_info_dump,
        },
        {
            // Tesla
            .id = 0x000704a0,
            .mask = 0xfffffff0,
            .mepa_driver_delete = mscc_1g_delete,
            .mepa_driver_reset = mscc_1g_reset,
            .mepa_driver_poll = mscc_1g_poll,
            .mepa_driver_conf_set = mscc_1g_conf_set,
            .mepa_driver_conf_get = phy_1g_conf_get,
            .mepa_driver_if_set = mscc_if_set,
            .mepa_driver_if_get = mscc_1g_if_get,
            .mepa_driver_power_set = mscc_1g_power_set,
            .mepa_driver_cable_diag_start = mscc_1g_veriphy_start,
            .mepa_driver_cable_diag_get = mscc_1g_veriphy_get,
            .mepa_driver_media_set = mscc_1g_media_set,
            .mepa_driver_probe = mscc_1g_probe,
            .mepa_driver_aneg_status_get = mscc_1g_status_1g_get,
            .mepa_driver_clause22_read = phy_1g_read,
            .mepa_driver_clause22_write = phy_1g_write,
            .mepa_driver_event_enable_set = phy_1g_event_enable_set,
            .mepa_driver_event_enable_get = phy_1g_event_enable_get,
            .mepa_driver_event_poll = phy_1g_event_poll,
            .mepa_driver_loopback_set = phy_1g_loopback_set,
            .mepa_driver_loopback_get = phy_1g_loopback_get,
            .mepa_driver_gpio_mode_set = phy_1g_gpio_mode,
            .mepa_driver_gpio_out_set = phy_1g_gpio_set,
            .mepa_driver_gpio_in_get = phy_1g_gpio_get,
            .mepa_driver_synce_clock_conf_set = phy_1g_synce_clk_conf_set,
            .mepa_driver_link_base_port = phy_1g_link_base_port,
            .mepa_driver_capability = tesla_1g_capability,
            .mepa_driver_phy_info_get = phy_1g_info_get,
            .mepa_driver_isolate_mode_conf = phy_isolate_mode_conf,
            .mepa_driver_chip_temp_get = phy_1g_chip_temp_get,
            .mepa_driver_phy_i2c_read = phy_1g_i2c_read,
            .mepa_driver_phy_i2c_write = phy_1g_i2c_write,
            .mepa_driver_phy_i2c_clock_select = phy_1g_i2c_clock_select,
            .mepa_driver_warmrestart_conf_get = phy_1g_warmrestart_conf_get,
            .mepa_driver_warmrestart_conf_end = phy_1g_warmrestart_conf_end,
            .mepa_driver_warmrestart_conf_set = phy_1g_warmrestart_conf_set,
            .mepa_driver_eee_mode_conf_set = phy_eee_mode_conf_set,
            .mepa_driver_eee_mode_conf_get = phy_eee_mode_conf_get,
            .mepa_driver_eee_status_get = phy_eee_status_get,
            .mepa_driver_debug_info_dump = phy_debug_info_dump,
            .mepa_driver_phy_qsgmii_sync = phy_1g_qsgmii_sync,
            .mepa_ts = &vtss_ts_drivers,
        },
        {
            // Viper
            .id = 0x000707c0,
            .mask = 0xfffffff0,
            .mepa_driver_delete = mscc_1g_delete,
            .mepa_driver_reset = mscc_1g_reset,
            .mepa_driver_poll = mscc_1g_poll,
            .mepa_driver_conf_set = mscc_1g_conf_set,
            .mepa_driver_conf_get = phy_1g_conf_get,
            .mepa_driver_if_set = mscc_if_set,
            .mepa_driver_if_get = mscc_1g_if_get,
            .mepa_driver_power_set = mscc_1g_power_set,
            .mepa_driver_cable_diag_start = mscc_1g_veriphy_start,
            .mepa_driver_cable_diag_get = mscc_1g_veriphy_get,
            .mepa_driver_media_set = mscc_1g_media_set,
            .mepa_driver_probe = mscc_1g_probe,
            .mepa_driver_aneg_status_get = mscc_1g_status_1g_get,
            .mepa_driver_clause22_read = phy_1g_read,
            .mepa_driver_clause22_write = phy_1g_write,
            .mepa_driver_event_enable_set = phy_1g_event_enable_set,
            .mepa_driver_event_enable_get = phy_1g_event_enable_get,
            .mepa_driver_event_poll = phy_1g_event_poll,
            .mepa_driver_loopback_set = phy_1g_loopback_set,
            .mepa_driver_loopback_get = phy_1g_loopback_get,
            .mepa_driver_gpio_mode_set = phy_1g_gpio_mode,
            .mepa_driver_gpio_out_set = phy_1g_gpio_set,
            .mepa_driver_gpio_in_get = phy_1g_gpio_get,
            .mepa_driver_synce_clock_conf_set = phy_1g_synce_clk_conf_set,
            .mepa_driver_link_base_port = phy_1g_link_base_port,
            .mepa_driver_phy_info_get = phy_1g_info_get,
            .mepa_driver_isolate_mode_conf = phy_isolate_mode_conf,
            .mepa_driver_debug_info_dump = phy_debug_info_dump,
            .mepa_driver_phy_i2c_read = phy_1g_i2c_read,
            .mepa_driver_phy_i2c_write = phy_1g_i2c_write,
            .mepa_driver_phy_i2c_clock_select = phy_1g_i2c_clock_select,
            .mepa_driver_capability = viper_1g_capability,
            .mepa_driver_phy_fefi_set = phy_1g_fefi_set,
            .mepa_driver_phy_fefi_get = phy_1g_fefi_get,
            .mepa_driver_phy_fefi_detect = phy_1g_fefi_detect,
            .mepa_driver_chip_temp_get = phy_1g_chip_temp_get,
            .mepa_driver_warmrestart_conf_get = phy_1g_warmrestart_conf_get,
            .mepa_driver_warmrestart_conf_end = phy_1g_warmrestart_conf_end,
            .mepa_driver_warmrestart_conf_set = phy_1g_warmrestart_conf_set,
            .mepa_driver_eee_mode_conf_set = phy_eee_mode_conf_set,
            .mepa_driver_eee_mode_conf_get = phy_eee_mode_conf_get,
            .mepa_driver_eee_status_get = phy_eee_status_get,
            .mepa_ts = &vtss_ts_drivers,
            .mepa_macsec = &vtss_macsec_drivers,
        },
        {
            // VTSS (all other models)
            .id = 0x00070400,
            .mask = 0xfffffc00,
            .mepa_driver_delete = mscc_1g_delete,
            .mepa_driver_reset = mscc_1g_reset,
            .mepa_driver_poll = mscc_1g_poll,
            .mepa_driver_conf_set = mscc_1g_conf_set,
            .mepa_driver_conf_get = phy_1g_conf_get,
            .mepa_driver_if_set = mscc_if_set,
            .mepa_driver_if_get = mscc_1g_if_get,
            .mepa_driver_power_set = mscc_1g_power_set,
            .mepa_driver_cable_diag_start = mscc_1g_veriphy_start,
            .mepa_driver_cable_diag_get = mscc_1g_veriphy_get,
            .mepa_driver_media_set = mscc_1g_media_set,
            .mepa_driver_probe = mscc_1g_probe,
            .mepa_driver_aneg_status_get = mscc_1g_status_1g_get,
            .mepa_driver_clause22_read = phy_1g_read,
            .mepa_driver_clause22_write = phy_1g_write,
            .mepa_driver_event_enable_set = phy_1g_event_enable_set,
            .mepa_driver_event_enable_get = phy_1g_event_enable_get,
            .mepa_driver_event_poll = phy_1g_event_poll,
            .mepa_driver_loopback_set = phy_1g_loopback_set,
            .mepa_driver_loopback_get = phy_1g_loopback_get,
            .mepa_driver_gpio_mode_set = phy_1g_gpio_mode,
            .mepa_driver_gpio_out_set = phy_1g_gpio_set,
            .mepa_driver_gpio_in_get = phy_1g_gpio_get,
            .mepa_driver_synce_clock_conf_set = phy_1g_synce_clk_conf_set,
            .mepa_driver_link_base_port = phy_1g_link_base_port,
            .mepa_driver_capability = phy_1g_capability,
            .mepa_driver_phy_info_get = phy_1g_info_get,
            .mepa_driver_isolate_mode_conf = phy_isolate_mode_conf,
            .mepa_driver_debug_info_dump = phy_debug_info_dump,
            .mepa_driver_phy_qsgmii_sync = phy_1g_qsgmii_sync,
        },
        {
            // Cicada (all models)
            .id = 0x000FC400,
            .mask = 0xfffffc00,
            .mepa_driver_delete = mscc_1g_delete,
            .mepa_driver_reset = mscc_1g_reset,
            .mepa_driver_poll = mscc_1g_poll,
            .mepa_driver_conf_set = mscc_1g_conf_set,
            .mepa_driver_conf_get = phy_1g_conf_get,
            .mepa_driver_if_set = mscc_if_set,
            .mepa_driver_if_get = mscc_1g_if_get,
            .mepa_driver_power_set = mscc_1g_power_set,
            .mepa_driver_cable_diag_start = mscc_1g_veriphy_start,
            .mepa_driver_cable_diag_get = mscc_1g_veriphy_get,
            .mepa_driver_media_set = mscc_1g_media_set,
            .mepa_driver_probe = mscc_1g_probe,
            .mepa_driver_aneg_status_get = mscc_1g_status_1g_get,
            .mepa_driver_clause22_read = phy_1g_read,
            .mepa_driver_clause22_write = phy_1g_write,
            .mepa_driver_event_enable_set = phy_1g_event_enable_set,
            .mepa_driver_event_enable_get = phy_1g_event_enable_get,
            .mepa_driver_event_poll = phy_1g_event_poll,
            .mepa_driver_loopback_set = phy_1g_loopback_set,
            .mepa_driver_loopback_get = phy_1g_loopback_get,
            .mepa_driver_gpio_mode_set = phy_1g_gpio_mode,
            .mepa_driver_gpio_out_set = phy_1g_gpio_set,
            .mepa_driver_gpio_in_get = phy_1g_gpio_get,
            .mepa_driver_synce_clock_conf_set = phy_1g_synce_clk_conf_set,
            .mepa_driver_link_base_port = phy_1g_link_base_port,
            .mepa_driver_capability = phy_1g_capability,
            .mepa_driver_phy_info_get = phy_1g_info_get,
            .mepa_driver_isolate_mode_conf = phy_isolate_mode_conf,
            .mepa_driver_debug_info_dump = phy_debug_info_dump,
        }
    };

    mepa_drivers_t result;
    result.phy_drv = mscc_drivers;
    result.count = nr_mscc_phy;

    return result;
}

mepa_drivers_t mepa_malibu_driver_init(void)
{
    static const int nr_malibu_phy = 1;
    static mepa_driver_t malibu_drivers[] = {{
            .id = 0x8250,
            .mask = 0x0000FFF0,
            .mepa_driver_delete = phy_10g_delete,
            .mepa_driver_reset = malibu_10g_reset,
            .mepa_driver_capability = malibu_10g_capability,
            .mepa_driver_poll = phy_10g_poll,
            .mepa_driver_conf_set = phy_10g_conf_set,
            .mepa_driver_conf_get = phy_10g_conf_get,
            .mepa_driver_if_set = mscc_if_set,
            .mepa_driver_if_get = malibu_10g_if_get,
            .mepa_driver_power_set = malibu_10g_power_set,
            .mepa_driver_cable_diag_start = NULL,
            .mepa_driver_cable_diag_get = NULL,
            .mepa_driver_media_set = NULL,
            .mepa_driver_event_poll = malibu_10g_event_poll,
            .mepa_driver_probe = phy_10g_probe,
            .mepa_driver_aneg_status_get = NULL,
            .mepa_driver_gpio_out_set = malibu_10g_gpio_write,
            .mepa_driver_gpio_in_get = malibu_10g_gpio_read,
            .mepa_driver_phy_info_get = phy_10g_info_get,
            .mepa_driver_clause45_read = phy_10g_clause45_read,
            .mepa_driver_clause45_write = phy_10g_clause45_write,
            .mepa_driver_event_enable_set = malibu_10g_event_enable_set,
            .mepa_driver_event_enable_get = malibu_10g_event_enable_get,
            .mepa_driver_phy_i2c_read = malibu_10g_i2c_read,
            .mepa_driver_phy_i2c_write = malibu_10g_i2c_write,
            .mepa_driver_warmrestart_conf_get = phy_10g_warmrestart_conf_get,
            .mepa_driver_warmrestart_conf_end = phy_10g_warmrestart_conf_end,
            .mepa_driver_warmrestart_conf_set = phy_10g_warmrestart_conf_set,
            .mepa_driver_synce_clock_conf_set = phy_10g_synce_clk_conf_set,
            .mepa_driver_debug_info_dump = phy_debug_info_dump,
            .mepa_ts = &vtss_ts_drivers,
            .mepa_macsec = &vtss_macsec_drivers,
        }
    };

    mepa_drivers_t result;
    result.phy_drv = malibu_drivers;
    result.count = nr_malibu_phy;

    return result;
}

mepa_drivers_t mepa_venice_driver_init(void)
{
    static const int nr_venice_phy = 1;
    static mepa_driver_t venice_drivers[] = {{
            .id = 0x8400,
            .mask = 0x0000FF00,
            .mepa_driver_delete = phy_10g_delete,
            .mepa_driver_reset = venice_10g_reset,
            .mepa_driver_poll = phy_10g_poll,
            .mepa_driver_conf_set = phy_10g_conf_set,
            .mepa_driver_if_set = mscc_if_set,
            .mepa_driver_if_get = venice_10g_if_get,
            .mepa_driver_probe = phy_10g_probe,
            .mepa_driver_capability = venice_10g_capability,
            .mepa_driver_phy_info_get = phy_10g_info_get,
            .mepa_ts = &vtss_ts_drivers,
        }
    };

    mepa_drivers_t result;
    result.phy_drv = venice_drivers;
    result.count = nr_venice_phy;

    return result;
}

mepa_drivers_t mepa_default_phy_driver_init(void)
{
    static const int nr_default_drivers = 1;
    static mepa_driver_t default_drivers[] = {{
            .id = 0x0,
            .mask = 0x00,
            .mepa_driver_delete = mscc_1g_delete,
            .mepa_driver_reset = mscc_1g_reset,
            .mepa_driver_poll = mscc_1g_poll,
            .mepa_driver_conf_set = mscc_1g_conf_set,
            .mepa_driver_conf_get = phy_1g_conf_get,
            .mepa_driver_if_set = mscc_if_set,
            .mepa_driver_if_get = mscc_1g_if_get,
            .mepa_driver_power_set = mscc_1g_power_set,
            .mepa_driver_cable_diag_start = mscc_1g_veriphy_start,
            .mepa_driver_cable_diag_get = mscc_1g_veriphy_get,
            .mepa_driver_media_set = mscc_1g_media_set,
            .mepa_driver_probe = mscc_1g_probe,
            .mepa_driver_aneg_status_get = mscc_1g_status_1g_get,
            .mepa_driver_clause22_read = phy_1g_read,
            .mepa_driver_clause22_write = phy_1g_write,
            .mepa_driver_event_enable_set = phy_1g_event_enable_set,
            .mepa_driver_event_enable_get = phy_1g_event_enable_get,
            .mepa_driver_event_poll = phy_1g_event_poll,
            .mepa_driver_loopback_set = phy_1g_loopback_set,
            .mepa_driver_loopback_get = phy_1g_loopback_get,
            .mepa_driver_gpio_mode_set = phy_1g_gpio_mode,
            .mepa_driver_gpio_out_set = phy_1g_gpio_set,
            .mepa_driver_gpio_in_get = phy_1g_gpio_get,
            .mepa_driver_synce_clock_conf_set = phy_1g_synce_clk_conf_set,
            .mepa_driver_capability = phy_1g_capability,
            .mepa_driver_phy_info_get = phy_1g_info_get,
            .mepa_driver_isolate_mode_conf = phy_isolate_mode_conf,
            .mepa_driver_debug_info_dump = phy_debug_info_dump,
            .mepa_driver_phy_i2c_read = phy_1g_i2c_read,
            .mepa_driver_phy_i2c_write = phy_1g_i2c_write,
        }
    };

    mepa_drivers_t result;
    result.phy_drv = default_drivers;
    result.count = nr_default_drivers;

    return result;
}
