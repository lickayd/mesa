// Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#include <microchip/ethernet/phy/api.h>
#include <mepa_driver.h>
#include "lan80xx_private.h"
#include "lan80xx.h"

mepa_rc lan80xx_operating_mode_set(const mepa_device_t        *dev,
                                   const mepa_port_no_t       port_no,
                                   const phy25g_mode_conf_t   phy_mode)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    phy25g_phy_state_t *data = (phy25g_phy_state_t *) dev->data;

    if (phy_mode.oper_mode == MAC_RETIMER) {
        data->terminate_lfs_in_phy = phy_mode.terminate_lfs_in_phy;
        data->host_mac_tx_pad = phy_mode.host_mac_tx_pad;
        data->line_mac_tx_pad = phy_mode.line_mac_tx_pad;
    }
    rc = lan80xx_operating_mode_set_priv(dev, port_no, phy_mode.oper_mode);
    MEPA_EXIT(dev);
    return rc;
}

mepa_rc lan80xx_phy_lfs_set(const mepa_device_t  *dev,
                            const mepa_port_no_t port_no,
                            mepa_bool_t          terminate_in_phy)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_phy_lfs_set_priv(dev, port_no, terminate_in_phy);
    MEPA_EXIT(dev);
    return rc;
}

mepa_rc lan80xx_phy_lfs_get(const mepa_device_t  *dev,
                            const mepa_port_no_t port_no,
                            mepa_bool_t          *terminate_in_phy)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_phy_lfs_get_priv(dev, port_no, terminate_in_phy);
    MEPA_EXIT(dev);
    return rc;
}

mepa_rc lan80xx_rckout_conf_get(mepa_device_t     *dev,
                                const mepa_port_no_t    port_no,
                                phy_25g_rckout_conf_t *const rckout_conf_a,
                                phy_25g_rckout_conf_t *const rckout_conf_b)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_rckout_conf_get_priv(dev, port_no, rckout_conf_a, rckout_conf_b);
    MEPA_EXIT(dev);
    return rc;
}

mepa_rc lan80xx_status_get(const mepa_device_t     *dev,
                           const mepa_port_no_t    port_no,
                           phy25g_status_t         *const status)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_status_get_priv(dev, port_no, status);
    MEPA_EXIT(dev);
    return rc;
}

mepa_rc lan80xx_rx_eye_scan_conf_set (const mepa_device_t          *dev,
                                      const mepa_port_no_t         port_no,
                                      mepa_bool_t                  is_line,
                                      const phy25g_rx_eye_scan_t   conf)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_rx_eye_scan_conf_set_priv(dev, port_no, is_line, conf);
    MEPA_EXIT(dev);
    return rc;
}


mepa_rc lan80xx_rx_eye_scan_status_get (const mepa_device_t                *dev,
                                        const mepa_port_no_t               port_no,
                                        const phy25g_rx_eye_scan_t         scan,
                                        phy25g_rx_eye_scan_status_t        *const status)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_rx_eye_scan_status_get_priv(dev, port_no, scan, status);
    MEPA_EXIT(dev);
    return rc;
}

mepa_rc lan80xx_phy_tx_rx_equalization_status_get(const mepa_device_t        *dev,
                                                  const mepa_port_no_t       port_no,
                                                  mepa_bool_t                is_line,
                                                  phy25g_tx_rx_equ_status_t  *const status)

{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_phy_tx_rx_equalization_status_get_priv(dev, port_no, is_line, status);
    MEPA_EXIT(dev);
    return rc;
}



mepa_rc lan80xx_phy_tx_rx_equalization_set(const mepa_device_t              *dev,
                                           const mepa_port_no_t             port_no,
                                           mepa_bool_t                      is_line,
                                           const phy25g_tx_rx_equa_conf_t   *const conf)

{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_phy_tx_rx_equalization_set_priv(dev, port_no, is_line, conf);
    MEPA_EXIT(dev);
    return rc;
}

mepa_rc lan80xx_phy_i2c_init(mepa_device_t         *dev,
                             const mepa_port_no_t  port_no,
                             const u8              client_id,
                             const u16             prescalar)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_phy_i2c_init_priv(dev, port_no, client_id, prescalar);
    MEPA_EXIT(dev);
    return rc;
}

mepa_rc lan80xx_phy_loopback_conf_set(mepa_device_t            *dev,
                                      const mepa_port_no_t     port_no,
                                      const phy25g_lp_types_t  *loopback)

{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_phy_loopback_conf_set_priv(dev, port_no, loopback);
    MEPA_EXIT(dev);
    return rc;
}


mepa_rc lan80xx_phy_loopback_conf_get(mepa_device_t         *dev,
                                      const mepa_port_no_t  port_no,
                                      phy25g_lp_get_t       *const loopback)

{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_phy_loopback_conf_get_priv(dev, port_no, loopback);
    MEPA_EXIT(dev);
    return rc;
}



mepa_rc lan80xx_pkt_gen_conf(const mepa_device_t    *dev,
                             const mepa_port_no_t   port_no,
                             const phy25g_pkt_gen_conf_t  *const conf)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_pkt_gen_conf_priv(dev, port_no, conf);
    MEPA_EXIT(dev);
    return rc;
}

mepa_rc lan80xx_pkt_mon_conf(const mepa_device_t   *dev,
                             const mepa_port_no_t  port_no,
                             mepa_bool_t           mon_enable,
                             phy25g_pkt_mon_rst_t  reset_cnt)

{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_pkt_mon_conf_priv(dev, port_no, mon_enable, reset_cnt);
    MEPA_EXIT(dev);
    return rc;
}

mepa_rc lan80xx_pkt_mon_get(const mepa_device_t   *dev,
                            const mepa_port_no_t  port_no,
                            mepa_bool_t           *const mon_enable,
                            phy25g_pkt_mon_rst_t  *const reset_cnt)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_pkt_mon_get_priv(dev, port_no, mon_enable, reset_cnt);
    MEPA_EXIT(dev);
    return rc;
}

mepa_rc lan80xx_pkt_mon_counters_get(const mepa_device_t        *dev,
                                     const mepa_port_no_t       port_no,
                                     mepa_bool_t                ts_rd,
                                     phy25g_pkt_mon_counters_t  *const pkt_counters,
                                     phy25g_timestamp_val_t     *const ts_packet)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_pkt_mon_counters_get_priv(dev, port_no, ts_rd, pkt_counters, ts_packet);
    MEPA_EXIT(dev);
    return rc;
}


mepa_rc lan80xx_temp_sensor_get(mepa_device_t          *dev,
                                const mepa_port_no_t   port_no,
                                i16                    *const value)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_temp_sensor_get_priv(dev, port_no, value);
    MEPA_EXIT(dev);
    return rc;
}

mepa_rc lan80xx_event_conf_set(mepa_device_t          *dev,
                               const mepa_port_no_t   port_no,
                               const phy25g_event_conf_t  evt_conf)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_event_conf_set_priv(dev, port_no, evt_conf);
    MEPA_EXIT(dev);
    return rc;
}


mepa_rc lan80xx_event_conf_get(mepa_device_t          *dev,
                               const mepa_port_no_t   port_no,
                               phy25g_events_t   *const evt)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_event_conf_get_priv(dev, port_no, evt);
    MEPA_EXIT(dev);
    return rc;
}

mepa_rc lan80xx_event_poll(mepa_device_t          *dev,
                           const mepa_port_no_t   port_no,
                           phy25g_events_t    *const evt)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_event_poll_priv(dev, port_no, evt);
    MEPA_EXIT(dev);
    return rc;
}

mepa_rc lan80xx_warm_start_failed_get(mepa_device_t          *dev,
                                      const mepa_port_no_t   port_no)
{
    phy25g_phy_state_t *data;

    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    data = (phy25g_phy_state_t *)dev->data;
    mepa_rc rc = data->warm_start_reg_changed ? MEPA_RC_ERROR : MEPA_RC_OK;
    MEPA_EXIT(dev);
    return rc;
}


mepa_rc lan80xx_phy_csr_write(mepa_device_t               *dev,
                              const mepa_port_no_t        port_no,
                              const u32                   mmd,
                              const u32                   addr,
                              const u32                   value)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_phy_csr_write_priv(dev, port_no, mmd, addr, value);
    MEPA_EXIT(dev);
    return rc;
}


mepa_rc lan80xx_phy_csr_read(mepa_device_t               *dev,
                             const mepa_port_no_t        port_no,
                             const u16                   mmd,
                             const u32                   addr,
                             u32                         *const value)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_phy_csr_read_priv(dev, port_no, mmd, addr, value);
    MEPA_EXIT(dev);
    return rc;
}


mepa_rc lan80xx_pmac_counters_get(mepa_device_t                  *dev,
                                  const mepa_port_no_t           port_no,
                                  mepa_bool_t                    is_host,
                                  phy25g_pmac_counters_t         *const counters)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_pmac_counters_get_priv(dev, port_no, is_host, counters);
    MEPA_EXIT(dev);
    return rc;
}

mepa_rc lan80xx_pmac_counters_clear(mepa_device_t                  *dev,
                                    const mepa_port_no_t           port_no,
                                    mepa_bool_t                    is_host)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_pmac_counters_clear_priv(dev, port_no, is_host);
    MEPA_EXIT(dev);
    return rc;
}

mepa_rc lan80xx_ext_event_conf_set(mepa_device_t                 *dev,
                                   const mepa_port_no_t          port_no,
                                   const phy25g_ext_event_conf_t  ext_evt_conf)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_ext_event_conf_set_priv(dev, port_no, ext_evt_conf);
    MEPA_EXIT(dev);
    return rc;
}

mepa_rc lan80xx_ext_event_conf_get(mepa_device_t          *dev,
                                   const mepa_port_no_t   port_no,
                                   phy25g_ext_events_t    *const ext_evt)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_ext_event_conf_get_priv(dev, port_no, ext_evt);
    MEPA_EXIT(dev);
    return rc;
}

mepa_rc lan80xx_ext_event_poll(mepa_device_t          *dev,
                               const mepa_port_no_t   port_no,
                               phy25g_ext_events_t    *const ext_evt)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_ext_event_poll_priv(dev, port_no, ext_evt);
    MEPA_EXIT(dev);
    return rc;
}

mepa_rc lan80xx_flow_control_set(const mepa_device_t     *dev,
                                 mepa_port_no_t          port_no,
                                 mepa_bool_t             enable)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_flow_control_set_priv(dev, port_no, enable);
    MEPA_EXIT(dev);
    return rc;
}

mepa_rc lan80xx_xconnect_conf_get(mepa_device_t  *dev,
                                  const mepa_port_no_t   port_no,
                                  phy25g_xconnect_get_conf_t   *const get_conf)
{
    mepa_rc rc = MEPA_RC_ERROR;
    if (dev == NULL) {
        T_E(MEPA_TRACE_GRP_GEN, "\nPort instance not created in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    if (!lan80xx_driver_check(dev)) {
        T_E(MEPA_TRACE_GRP_GEN, "\nAPI not supported for PHY in port : %d", port_no);
        return MEPA_RC_ERROR;
    }
    MEPA_ENTER(dev);
    rc = lan80xx_xconnect_conf_get_priv(dev, port_no, get_conf);
    MEPA_EXIT(dev);
    return rc;
}
