// Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#ifndef _VTSS_MRP_API_H_
#define _VTSS_MRP_API_H_

#include <vtss/api/types.h>

#if defined(VTSS_FEATURE_MRP)
#ifdef __cplusplus
extern "C" {
#endif

#define VTSS_MRP_CNT   (VTSS_PORTS/2) + 1  // The maximum number of MRP instances. One instance requires at least two ports

// MRP ring role.
typedef enum {
    VTSS_MRP_RING_ROLE_DISABLED,
    VTSS_MRP_RING_ROLE_CLIENT,
    VTSS_MRP_RING_ROLE_MANAGER,
} vtss_mrp_ring_role_t;

// MRP instance create configuration.
typedef struct {
    vtss_mrp_ring_role_t  ring_role;    // MRP ring role
    vtss_mrp_ring_role_t  in_ring_role; // MRP Interconnect ring role
    BOOL                  mra;          // MRP is MRA. The actual roles is given by 'role' and vtss_mrp_ring_role_set()
    uint32_t              mra_priority; // MRA priority
    vtss_port_no_t        p_port;       // Port with Primary port role
    vtss_port_no_t        s_port;       // Port with Secondary port role
    vtss_port_no_t        i_port;       // Port with Interconnect port role
    vtss_mac_t            p_mac;        // Primary port MRP endpoint MAC address */
    vtss_mac_t            s_mac;        // Secondary port MRP endpoint MAC address */
    vtss_mac_t            i_mac;        // Interconnect port MRP endpoint MAC address */
} vtss_mrp_conf_t;

// Add a MRP instance with configuration.
// Resources are allocated.
// The ring state is VTSS_MRP_RING_STATE_OPEN.
// inst     [IN] Target instance reference.
// mrp_idx  [IN] Index of the configured MRP instance.
// conf     [IN] Configuration parameters for MRP.
vtss_rc vtss_mrp_add(const vtss_inst_t      inst,
                     const vtss_mrp_idx_t   mrp_idx,
                     const vtss_mrp_conf_t  *const conf);

vtss_rc vtss_mrp_get(const vtss_inst_t      inst,
                     const vtss_mrp_idx_t   mrp_idx,
                     vtss_mrp_conf_t        *const conf);

// Delete a MRP instance.
// inst     [IN] Target instance reference.
// mrp_idx  [IN] Index of the deleted MRP instance.
vtss_rc vtss_mrp_del(const vtss_inst_t      inst,
                     const vtss_mrp_idx_t   mrp_idx);

// Set a MRP instance ring role.
// inst     [IN] Target instance reference.
// mrp_idx  [IN] Index of the configured MRP instance.
// role     [IN] The MRP ring role.
vtss_rc vtss_mrp_ring_role_set(const vtss_inst_t           inst,
                               const vtss_mrp_idx_t        mrp_idx,
                               const vtss_mrp_ring_role_t  role);

vtss_rc vtss_mrp_ring_role_get(const vtss_inst_t     inst,
                               const vtss_mrp_idx_t  mrp_idx,
                               vtss_mrp_ring_role_t  *const role);

// Set a MRP instance Interconnet ring role.
// inst     [IN] Target instance reference.
// mrp_idx  [IN] Index of the configured MRP instance.
// role     [IN] The MRP ring role.
vtss_rc vtss_mrp_in_ring_role_set(const vtss_inst_t           inst,
                                  const vtss_mrp_idx_t        mrp_idx,
                                  const vtss_mrp_ring_role_t  role);

vtss_rc vtss_mrp_in_ring_role_get(const vtss_inst_t     inst,
                                  const vtss_mrp_idx_t  mrp_idx,
                                  vtss_mrp_ring_role_t  *const role);

// MRP ports
typedef struct {
    vtss_port_no_t   p_port;       // Port with Primary port role
    vtss_port_no_t   s_port;       // Port with Secondary port role
} vtss_mrp_ports_t;

// Set a MRP instance ring port numbers.
// The Primary and Secondary ring port numbers can only be swapped.
// After swapping ports vtss_mrp_port_state_set() must be called with updated port state.
// inst     [IN] Target instance reference.
// mrp_idx  [IN] Index of the configured MRP instance.
// ports    [IN] The ports.
vtss_rc vtss_mrp_ports_set(const vtss_inst_t       inst,
                           const vtss_mrp_idx_t    mrp_idx,
                           const vtss_mrp_ports_t  *const ports);

vtss_rc vtss_mrp_ports_get(const vtss_inst_t       inst,
                           const vtss_mrp_idx_t    mrp_idx,
                           vtss_mrp_ports_t        *const ports);

// MRP ring state.
typedef enum {
    VTSS_MRP_RING_STATE_CLOSED,
    VTSS_MRP_RING_STATE_OPEN
} vtss_mrp_ring_state_t;

// Set a MRP instance ring state.
// inst     [IN] Target instance reference.
// mrp_idx  [IN] Index of the configured MRP instance.
// state    [IN] The ring state.
vtss_rc vtss_mrp_ring_state_set(const vtss_inst_t            inst,
                                const vtss_mrp_idx_t         mrp_idx,
                                const vtss_mrp_ring_state_t  state);

vtss_rc vtss_mrp_ring_state_get(const vtss_inst_t      inst,
                                const vtss_mrp_idx_t   mrp_idx,
                                vtss_mrp_ring_state_t  *const state);

// Set a MRP instance Interconnect ring state.
// inst     [IN] Target instance reference.
// mrp_idx  [IN] Index of the configured MRP instance.
// state    [IN] The ring state.
vtss_rc vtss_mrp_in_ring_state_set(const vtss_inst_t            inst,
                                   const vtss_mrp_idx_t         mrp_idx,
                                   const vtss_mrp_ring_state_t  state);

vtss_rc vtss_mrp_in_ring_state_get(const vtss_inst_t      inst,
                                   const vtss_mrp_idx_t   mrp_idx,
                                   vtss_mrp_ring_state_t  *const state);

// MRP port state.
typedef enum {
    VTSS_MRP_PORT_STATE_DISABLED,
    VTSS_MRP_PORT_STATE_BLOCKED,
    VTSS_MRP_PORT_STATE_FORWARDING
} vtss_mrp_port_state_t;

// Set a MRP instance ring port state.
// inst     [IN] Target instance reference.
// mrp_idx  [IN] Index of the configured MRP instance.
// port     [IN] The port.
// state    [IN] The port state.
vtss_rc vtss_mrp_port_state_set(const vtss_inst_t            inst,
                                const vtss_mrp_idx_t         mrp_idx,
                                const vtss_port_no_t         port,
                                const vtss_mrp_port_state_t  state);

vtss_rc vtss_mrp_port_state_get(const vtss_inst_t      inst,
                                const vtss_mrp_idx_t   mrp_idx,
                                const vtss_port_no_t   port,
                                vtss_mrp_port_state_t  *const state);

// MRP instance best information.
typedef struct {
    vtss_mac_t  mac;       // Best MAC address */
} vtss_mrp_best_t;

// Set a MRP instance best received priority and MAC.
// This information can be received by MRP_TestMgrNAck or MRP_TestPropagate.
// It is used to check for TST recetion from currentlu best MRM.
//
// inst     [IN] Target instance reference.
// mrp_idx  [IN] Index of the configured MRP instance.
// best     [IN] Best parameters for MRP.
vtss_rc vtss_mrp_best_set(const vtss_inst_t      inst,
                          const vtss_mrp_idx_t   mrp_idx,
                          const vtss_mrp_best_t  *const best);

vtss_rc vtss_mrp_best_get(const vtss_inst_t      inst,
                          const vtss_mrp_idx_t   mrp_idx,
                          vtss_mrp_best_t        *const best);

// MRP TST LOC configuration.
typedef struct {
    u32  tst_interval;    // The expected TST frame reception interval in microseconds
    u32  tst_mon_count;   // The TST monitoring count of intervals without TST generating TST LOC

    u32  itst_interval;   // The expected ITST frame reception interval in microseconds
    u32  itst_mon_count;  // The ITST monitoring count of intervals without ITST generating ITST LOC
} vtss_mrp_tst_loc_t;

// Set a MRP instance TST LOC configuration.
// inst     [IN] Target instance reference.
// mrp_idx  [IN] Index of the configured MRP instance.
// conf     [IN] The TST LOC configuration.
vtss_rc vtss_mrp_tst_loc_set(const vtss_inst_t         inst,
                             const vtss_mrp_idx_t      mrp_idx,
                             const vtss_mrp_tst_loc_t  *const conf);

vtss_rc vtss_mrp_tst_loc_get(const vtss_inst_t     inst,
                             const vtss_mrp_idx_t  mrp_idx,
                             vtss_mrp_tst_loc_t    *const conf);

// MRP TST copy configuration.
typedef struct {
    BOOL  tst_low_prio;    // Copy TST with lower MRA priority to CPU
    BOOL  tst_clear_loc;   // Copy next TST that clear TST LOC to CPU

    BOOL  itst_clear_loc;  // Copy next ITST that clear ITST LOC to CPU
} vtss_mrp_copy_tst_t;

// Set a MRP instance TST copy to CPU configuration.
// inst     [IN] Target instance reference.
// mrp_idx  [IN] Index of the configured MRP instance.
// copy     [IN] The TST copy configuration.
vtss_rc vtss_mrp_copy_tst_set(const vtss_inst_t          inst,
                              const vtss_mrp_idx_t       mrp_idx,
                              const vtss_mrp_copy_tst_t  *const copy);

vtss_rc vtss_mrp_copy_tst_get(const vtss_inst_t     inst,
                              const vtss_mrp_idx_t  mrp_idx,
                              vtss_mrp_copy_tst_t   *const copy);

// MRP status.
typedef struct {
    BOOL  tst_loc;        // The TST LOC state.
    BOOL  itst_loc;       // The ITST LOC state.

    BOOL  mrp_seen;       // MRP frame received
    BOOL  mrp_proc_seen;  // MRP frame received and processed
    BOOL  dmac_err_seen;  // MRP frame received with DMAC error
    BOOL  vers_err_seen;  // MRP frame received with version error
    BOOL  seq_err_seen;   // MRP frame received with sequence error
} vtss_mrp_port_status_t;

typedef struct {
    vtss_mrp_port_status_t p_status;
    vtss_mrp_port_status_t s_status;
    vtss_mrp_port_status_t i_status;
} vtss_mrp_status_t;

// Get a MRP instance status.
// inst     [IN] Target instance reference.
// mrp_idx  [IN] Index of the configured MRP instance.
// status   [IN] The MRP status.
vtss_rc vtss_mrp_status_get(const vtss_inst_t     inst,
                            const vtss_mrp_idx_t  mrp_idx,
                            vtss_mrp_status_t     *const status);

// MRP counters.
typedef struct {
    u64   tst_rx_count;   // Number of received valid TST frames subject to MRP_Test processing.
    u64   itst_rx_count;  // Number of received valid ITST frames subject to MRP_Test processing.
} vtss_mrp_port_counters_t;

typedef struct {
    vtss_mrp_port_counters_t p_counters;
    vtss_mrp_port_counters_t s_counters;
    vtss_mrp_port_counters_t i_counters;
} vtss_mrp_counters_t;

// Get a MRP instance counters.
// inst     [IN] Target instance reference.
// mrp_idx  [IN] Index of the configured MRP instance.
// counters [IN] The MRP counters.
vtss_rc vtss_mrp_counters_get(const vtss_inst_t     inst,
                              const vtss_mrp_idx_t  mrp_idx,
                              vtss_mrp_counters_t   *const counters);

// Clear the MRP instance status counters.
// inst     [IN] Target instance reference.
// mrp_idx  [IN] Index of the configured MRP instance.
vtss_rc vtss_mrp_counters_clear(const vtss_inst_t     inst,
                                const vtss_mrp_idx_t  mrp_idx);

// MRP Event indication flags.
// Different events can be enabled/disabled and polled.
// The events are represented by a bit in an event mask.
// VTSS_CAP(VTSS_CAP_MRP_EVENT_SUPPORTED) will return an event mask with the
// events supported on a given platform.
#define VTSS_MRP_EVENT_MASK_NONE               0x00
// Change in ITST frame Loss of Continuity (LOC) state
#define VTSS_MRP_EVENT_MASK_ITST_LOC           0x01
// Change in TST frame Loss of Continuity (LOC) state
#define VTSS_MRP_EVENT_MASK_TST_LOC            0x02
#define VTSS_MRP_EVENT_MASK_ALL                0x0F

// MRP event generation enable/disable.
// inst     [IN] Target instance reference.
// mrp_idx  [IN] Index of the configured MRPE instance.
// mask     [IN] Mask of events to either enable or disable.
// enable   [IN] Whether to enable or disable the events specified in the mask.
vtss_rc vtss_mrp_event_mask_set(const vtss_inst_t     inst,
                                const vtss_mrp_idx_t  mrp_idx,
                                const u32             mask,
                                const BOOL            enable);

vtss_rc vtss_mrp_event_mask_get(const vtss_inst_t     inst,
                                const vtss_mrp_idx_t  mrp_idx,
                                u32                   *const mask);

// MRP event polling.
// Mask is returned indicating pending events. Pending event are cleared after get.
// inst      [IN]  Target instance reference.
// mrp_idx   [IN]  Index of polled MRPE instance.
// events    [OUT] Masks of detected pending events during get.
typedef struct {
    u32 p_mask;
    u32 s_mask;
    u32 i_mask;
} vtss_mrp_event_t;

vtss_rc vtss_mrp_event_get(const vtss_inst_t     inst,
                           const vtss_mrp_idx_t  mrp_idx,
                           vtss_mrp_event_t      *const events);

#ifdef __cplusplus
}
#endif
#endif /* VTSS_FEATURE_MRP */
#endif /* _VTSS_MRP_API_H_ */
