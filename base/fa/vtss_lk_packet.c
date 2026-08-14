// Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#define VTSS_TRACE_GROUP VTSS_TRACE_GROUP_PACKET
#include "vtss_fa_cil.h"

#if defined(VTSS_ARCH_LAIKA)
#include "vtss_lk_packet.h"

#include "vtss_lk_rda_regs.h"

#define PIE_BD_PTR_SZ 24
#define PIE_BD_MASK   0x00FFFFFF
#define MIN_FLEN      64

#define PIE_REG(reg_name, c) VTSS_PIE_CHN_##reg_name(lk_pie_tgt(c->cpu_port), c->chnl_id)

#define PIE_DEFAULT_RX_DESCRIPTORS 4
#define PIE_DEFAULT_TX_DESCRIPTORS 4
#define MTU                        (LMU_FRAME_MAX_SIZE + VTSS_FA_RX_IFH_SIZE) // 1518 + 36
#define P64H_PIE_BUFF_ALIGN        32
#define CEIL_ALIGN(x, m)           (((((x) - 1) / m) + 1) * m)

struct lk_pie_rx_desc_t {
    u64 memory_addr : 50;
    u64 block_len   : 14;

    u64 valid       : 1;
    u64 owner       : 1;
    u64 rsvd1       : 3;
    u64 sop         : 1;
    u64 eop         : 1;
    u64 ifh_present : 1;

    u64 rofh_present     : 1;
    u64 checksum_checked : 1;
    u64 debug_id         : 5;
    u64 rsvd2            : 1;

    u64 ofh_present       : 1;
    u64 priority          : 3;
    u64 pkt_len           : 14;
    u64 checksum          : 16;
    u64 error_present     : 1;
    u64 err_invalid_isdx  : 1;
    u64 err_checksum      : 1;
    u64 err_missing_sof   : 1;
    u64 err_missing_eof   : 1;
    u64 err_1clock_pkt    : 1;
    u64 err_wrong_unused  : 1;
    u64 err_fifo_overflow : 1;
    u64 err_pkt_oversize  : 1;
    u64 rsvd3             : 5;
};
struct lk_pie_tx_desc_t {
    u64 memory_addr           : 50;
    u64 block_len             : 14;
    u64 valid                 : 1;
    u64 sop                   : 1;
    u64 eop                   : 1;
    u64 sge                   : 6;
    u64 vlan_en               : 1;
    u64 vlan_tpid             : 3;
    u64 vlan_id               : 12;
    u64 vlan_dei              : 1;
    u64 checksum_en           : 1;
    u64 checksum_is_ipv6      : 1;
    u64 start_byte_offset     : 7;
    u64 start_checksum_insert : 7;
    u64 tofh_insert_en        : 1;
    u64 tofh_insert_isdx      : 12;
    u64 insert_prio           : 3;
    u64 remain_bits           : 6;
};

static inline u32 lk_pie_tgt(int i) { return (i == 0) ? VTSS_TO_PIE_CHN_0 : VTSS_TO_PIE_CHN_1; }

static inline vtss_lk_pie_chnl_t *lk_get_chnl(vtss_state_t *vtss_state)
{
    return &vtss_state->packet.lk.chnls[0];
}

static inline u64 lk_pie_bd_ptr2addr(uint32_t base, uint32_t ptr, bool base_msb)
{
    uintptr_t addr;
    base += base_msb ? 0 : 1; // If the BD ring crosses a 16MB frontier (end msb != start msb), then
                              // end's msb must be start's msb + 1
    addr = base;
    addr <<= PIE_BD_PTR_SZ;
    addr |= ptr;
    return addr;
}

static inline uint32_t lk_pie_bd_msb(u64 addr) { return addr >> PIE_BD_PTR_SZ; }

static inline bool lk_pie_bd_msb_cmp(u64 addr, uint32_t msb) { return lk_pie_bd_msb(addr) == msb; }

static inline void lk_u64_to_u32(u32 *low, u32 *high, u64 val)
{
    *low = (uintptr_t)val;
    *high = val >> 32;
}

static inline vtss_rc lk_pie_rd(vtss_state_t *vtss_state, u64 *addr, u32 reg, bool is_rx)
{
    u32  ptr;
    u32  ring_msb;
    bool base_msb;
    *addr = 0;
    vtss_fa_rd(vtss_state, reg, &ptr);
    base_msb = !VTSS_EXTRACT_BITFIELD(ptr, PIE_BD_PTR_SZ, 1);
    vtss_lk_pie_chnl_t *c = lk_get_chnl(vtss_state);
    ring_msb = is_rx ? c->pc_rx_ring_msb : c->pc_tx_ring_msb;
    *addr = lk_pie_bd_ptr2addr(ring_msb, ptr, base_msb);
    return VTSS_RC_OK;
}

static inline vtss_rc lk_pie_update(vtss_state_t *vtss_state, u64 addr, u32 reg, bool is_rx)
{
    u32                 ring_msb;
    bool                is_end;
    vtss_lk_pie_chnl_t *c = lk_get_chnl(vtss_state);
    ring_msb = is_rx ? c->pc_rx_ring_msb : c->pc_tx_ring_msb;
    is_end = !lk_pie_bd_msb_cmp(addr, ring_msb);
    vtss_fa_wr(vtss_state, reg,
               (addr & PIE_BD_MASK) | VTSS_ENCODE_BITFIELD(is_end, PIE_BD_PTR_SZ, 1));
    return VTSS_RC_OK;
}

// Reads
static inline vtss_rc lk_edesc_write_ptr_read(vtss_state_t       *vtss_state,
                                              vtss_lk_pie_chnl_t *c,
                                              u64                *addr)
{
    return lk_pie_rd(vtss_state, addr, REG_ADDR(PIE_REG(PE_CB_EDESC_STS0, c)), TRUE);
}
static inline vtss_rc lk_edesc_read_ptr_read(vtss_state_t       *vtss_state,
                                             vtss_lk_pie_chnl_t *c,
                                             u64                *addr)
{
    return lk_pie_rd(vtss_state, addr, REG_ADDR(PIE_REG(PE_CB_EDESC_CFG6, c)), TRUE);
}
static inline vtss_rc lk_rdesc_read_ptr_read(vtss_state_t       *vtss_state,
                                             vtss_lk_pie_chnl_t *c,
                                             u64                *addr)
{
    return lk_pie_rd(vtss_state, addr, REG_ADDR(PIE_REG(PE_CB_RDESC_STS0, c)), TRUE);
}
static inline vtss_rc lk_rdesc_write_ptr_read(vtss_state_t       *vtss_state,
                                              vtss_lk_pie_chnl_t *c,
                                              u64                *addr)
{
    return lk_pie_rd(vtss_state, addr, REG_ADDR(PIE_REG(PE_CB_RDESC_CFG6, c)), TRUE);
}
static inline vtss_rc lk_idesc_read_ptr_read(vtss_state_t       *vtss_state,
                                             vtss_lk_pie_chnl_t *c,
                                             u64                *addr)
{
    return lk_pie_rd(vtss_state, addr, REG_ADDR(PIE_REG(PI_CB_DESC_STS0, c)), FALSE);
}
static inline vtss_rc lk_idesc_write_ptr_read(vtss_state_t       *vtss_state,
                                              vtss_lk_pie_chnl_t *c,
                                              u64                *addr)
{
    return lk_pie_rd(vtss_state, addr, REG_ADDR(PIE_REG(PI_CB_DESC_CFG6, c)), FALSE);
}
// Updates
static inline vtss_rc lk_edesc_read_ptr_update(vtss_state_t       *vtss_state,
                                               vtss_lk_pie_chnl_t *c,
                                               u64                 addr)
{
    return lk_pie_update(vtss_state, addr, REG_ADDR(PIE_REG(PE_CB_EDESC_CFG6, c)), TRUE);
}
static inline vtss_rc lk_rdesc_write_ptr_update(vtss_state_t       *vtss_state,
                                                vtss_lk_pie_chnl_t *c,
                                                u64                 addr)
{
    return lk_pie_update(vtss_state, addr, REG_ADDR(PIE_REG(PE_CB_RDESC_CFG6, c)), TRUE);
}
static inline vtss_rc lk_idesc_write_ptr_update(vtss_state_t       *vtss_state,
                                                vtss_lk_pie_chnl_t *c,
                                                u64                 addr)
{
    return lk_pie_update(vtss_state, addr, REG_ADDR(PIE_REG(PI_CB_DESC_CFG6, c)), FALSE);
}

static inline vtss_rc lk_pi_desc_cfg(vtss_state_t       *vtss_state,
                                     vtss_lk_pie_chnl_t *c,
                                     u64                 start,
                                     u64                 end)
{
    u32 low, high;
    lk_u64_to_u32(&low, &high, start);
    REG_WR(PIE_REG(PI_CB_DESC_CFG0, c), low);
    REG_WR(PIE_REG(PI_CB_DESC_CFG1, c), high);
    lk_u64_to_u32(&low, &high, end);
    REG_WR(PIE_REG(PI_CB_DESC_CFG2, c), low);
    REG_WR(PIE_REG(PI_CB_DESC_CFG3, c), high);
    return VTSS_RC_OK;
}

static inline vtss_rc lk_pe_desc_cfg(vtss_state_t       *vtss_state,
                                     vtss_lk_pie_chnl_t *c,
                                     u64                 start,
                                     u64                 end)
{
    u32 low, high;
    lk_u64_to_u32(&low, &high, start);
    REG_WR(PIE_REG(PE_CB_RDESC_CFG0, c), low);
    REG_WR(PIE_REG(PE_CB_RDESC_CFG1, c), high);
    REG_WR(PIE_REG(PE_CB_EDESC_CFG0, c), low);
    REG_WR(PIE_REG(PE_CB_EDESC_CFG1, c), high);
    lk_u64_to_u32(&low, &high, end);
    REG_WR(PIE_REG(PE_CB_RDESC_CFG2, c), low);
    REG_WR(PIE_REG(PE_CB_RDESC_CFG3, c), high);
    REG_WR(PIE_REG(PE_CB_EDESC_CFG2, c), low);
    REG_WR(PIE_REG(PE_CB_EDESC_CFG3, c), high);
    return VTSS_RC_OK;
}

vtss_rc lk_debug_pkt(vtss_state_t *vtss_state, lmu_ss_t *ss, const vtss_debug_info_t *const info)
{
    u64                 wptr, rptr;
    vtss_lk_pie_chnl_t *c = lk_get_chnl(vtss_state);

    pr("PIE CHANNEL %u-%u\n", c->cpu_port, c->chnl_id);
    lk_idesc_write_ptr_read(vtss_state, c, &wptr);
    lk_idesc_read_ptr_read(vtss_state, c, &rptr);
    pr("%-33s SW_WR=0x%llx HW_RD=0x%llx\n", "PI DESC", wptr, rptr);
    lk_rdesc_write_ptr_read(vtss_state, c, &wptr);
    lk_rdesc_read_ptr_read(vtss_state, c, &rptr);
    pr("%-33s SW_WR=0x%llx HW_RD=0x%llx\n", "PE RDESC", wptr, rptr);
    lk_edesc_write_ptr_read(vtss_state, c, &wptr);
    lk_edesc_read_ptr_read(vtss_state, c, &rptr);
    pr("%-33s HW_WR=0x%llx SW_RD=0x%llx\n", "PE EDESC", wptr, rptr);

    vtss_fa_debug_reg(vtss_state, ss, REG_ADDR(PIE_REG(SRX_PIE_CTRL, c)), "SRX_PIE_CTRL");
    vtss_fa_debug_reg(vtss_state, ss, REG_ADDR(PIE_REG(STX_PIE_CTRL, c)), "STX_PIE_CTRL");
    vtss_fa_debug_reg(vtss_state, ss, REG_ADDR(PIE_REG(STX_AXIS_ERR_CTRL_PIE, c)),
                      "STX_AXIS_ERR_CTRL_PIE");
    vtss_fa_debug_reg(vtss_state, ss, REG_ADDR(PIE_REG(PIE_0_INT, c)), "PIE_0_INT");
    vtss_fa_debug_reg(vtss_state, ss, REG_ADDR(PIE_REG(PIE_1_INT, c)), "PIE_1_INT");
    vtss_fa_debug_reg(vtss_state, ss, REG_ADDR(PIE_REG(PIE_2_INT, c)), "PIE_2_INT");
    pr("\n");

    return VTSS_RC_OK;
}

static vtss_rc lk_chn_traffic_enable(vtss_state_t *vtss_state)
{
    vtss_lk_pie_chnl_t *c = lk_get_chnl(vtss_state);

    REG_WRM(PIE_REG(SRX_PIE_CTRL, c), VTSS_F_PIE_CHN_SRX_PIE_CTRL_SRX_PIE_CH_EN(1),
            VTSS_M_PIE_CHN_SRX_PIE_CTRL_SRX_PIE_CH_EN);
    REG_WRM(PIE_REG(STX_PIE_CTRL, c), VTSS_F_PIE_CHN_STX_PIE_CTRL_STX_PIE_CH_EN(1),
            VTSS_M_PIE_CHN_STX_PIE_CTRL_STX_PIE_CH_EN);
    REG_WRM(PIE_REG(PIE_GEN_CFG, c),
            (VTSS_F_PIE_CHN_PIE_GEN_CFG_CHN_PI_EN(1) | VTSS_F_PIE_CHN_PIE_GEN_CFG_CHN_PE_EN(1)),
            (VTSS_M_PIE_CHN_PIE_GEN_CFG_CHN_PI_EN | VTSS_M_PIE_CHN_PIE_GEN_CFG_CHN_PE_EN));
    return VTSS_RC_OK;
}

static vtss_rc lk_chn_traffic_disable(vtss_state_t *vtss_state)
{
    vtss_lk_pie_chnl_t *c = lk_get_chnl(vtss_state);

    REG_WRM(PIE_REG(PIE_GEN_CFG, c), 0,
            (VTSS_M_PIE_CHN_PIE_GEN_CFG_CHN_PI_EN | VTSS_M_PIE_CHN_PIE_GEN_CFG_CHN_PE_EN));
    REG_WRM(PIE_REG(STX_PIE_CTRL, c), 0, VTSS_M_PIE_CHN_STX_PIE_CTRL_STX_PIE_CH_EN);
    REG_WRM(PIE_REG(SRX_PIE_CTRL, c), 0, VTSS_M_PIE_CHN_SRX_PIE_CTRL_SRX_PIE_CH_EN);
    return VTSS_RC_OK;
}

static vtss_rc lk_setup_rx_cfg(vtss_state_t *vtss_state)
{
    vtss_lk_pie_chnl_t *c = lk_get_chnl(vtss_state);
    u32                 val, mask;

    val = VTSS_F_PIE_CHN_SRX_TAXI_ERROR_CH_CTRL_PIE_SRX_PIE_ABORT_MISS_EOF(1) |
          VTSS_F_PIE_CHN_SRX_TAXI_ERROR_CH_CTRL_PIE_SRX_PIE_ABORT_WRONG_UB(1);
    mask = VTSS_M_PIE_CHN_SRX_TAXI_ERROR_CH_CTRL_PIE_SRX_PIE_ABORT_MISS_EOF |
           VTSS_M_PIE_CHN_SRX_TAXI_ERROR_CH_CTRL_PIE_SRX_PIE_ABORT_WRONG_UB;
    REG_WRM(PIE_REG(SRX_TAXI_ERROR_CH_CTRL_PIE, c), val, mask);

    val = VTSS_F_PIE_CHN_SRX_PIE_CTRL_SRX_PIE_REMOVE_FCS(1);
    mask = VTSS_M_PIE_CHN_SRX_PIE_CTRL_SRX_PIE_REMOVE_ROFH |
           VTSS_M_PIE_CHN_SRX_PIE_CTRL_SRX_PIE_REMOVE_FCS |
           VTSS_M_PIE_CHN_SRX_PIE_CTRL_SRX_PIE_PREPEND_ROFH;
    REG_WRM(PIE_REG(SRX_PIE_CTRL, c), val, mask);

    REG_WR(PIE_REG(PE_CB_EDESC_CFG4, c), 0); // CHN_PE_CB_EDESC_HI_THLD
    REG_WR(PIE_REG(PE_CB_EDESC_CFG5, c), 1); // CHN_PE_CB_EDESC_LO_THLD
    REG_WR(PIE_REG(PE_CB_EDESC_CFG8, c), 1); // CHN_PE_CB_EDESC_TIMER

    return VTSS_RC_OK;
}

static vtss_rc lk_setup_tx_cfg(vtss_state_t *vtss_state)
{
    vtss_lk_pie_chnl_t *c = lk_get_chnl(vtss_state);
    u32                 priority_mask = 0;
    u32                 dest_mask = 0;
    u32                 val, mask;

    val = VTSS_F_PIE_CHN_STX_AXIS_ERR_CTRL_PIE_STX_PIE_SIZE_CTRL_ADJT_FH(1) |
          VTSS_F_PIE_CHN_STX_AXIS_ERR_CTRL_PIE_STX_PIE_SIZE_CTRL_ADJT_REM_FH(1);
    mask = VTSS_M_PIE_CHN_STX_AXIS_ERR_CTRL_PIE_STX_PIE_ABORT_MISS_TLAST |
           VTSS_M_PIE_CHN_STX_AXIS_ERR_CTRL_PIE_STX_PIE_ABORT_1CC_PKT_ERR |
           VTSS_M_PIE_CHN_STX_AXIS_ERR_CTRL_PIE_STX_PIE_ABORT_WRONG_TKEEP |
           VTSS_M_PIE_CHN_STX_AXIS_ERR_CTRL_PIE_STX_PIE_SIZE_CTRL_ADJT_FH |
           VTSS_M_PIE_CHN_STX_AXIS_ERR_CTRL_PIE_STX_PIE_SIZE_CTRL_ADJT_REM_FH;

    REG_WRM(PIE_REG(STX_AXIS_ERR_CTRL_PIE, c), val, mask);

    val = VTSS_F_PIE_CHN_STX_PIE_CTRL_STX_PIE_INS_FCS(1);
    mask = VTSS_M_PIE_CHN_STX_PIE_CTRL_STX_PIE_INS_FCS |
           VTSS_M_PIE_CHN_STX_PIE_CTRL_STX_PIE_INS_TOFH |
           VTSS_M_PIE_CHN_STX_PIE_CTRL_STX_PIE_SRC_TOFH |
           VTSS_M_PIE_CHN_STX_PIE_CTRL_STX_PIE_INS_VLAN |
           VTSS_M_PIE_CHN_STX_PIE_CTRL_STX_PIE_SRC_VLAN |
           VTSS_M_PIE_CHN_STX_PIE_CTRL_STX_PIE_SRC_PRIO |
           VTSS_M_PIE_CHN_STX_PIE_CTRL_STX_PIE_SIZE_CTRL_OSIZE_CH_EN |
           VTSS_M_PIE_CHN_STX_PIE_CTRL_STX_PIE_SIZE_CTRL_USIZE_CH_EN;

    REG_WRM(PIE_REG(STX_PIE_CTRL, c), val, mask);

    REG_WR(PIE_REG(GEN_CFG0, c), dest_mask);
    REG_WR(PIE_REG(GEN_CFG1, c), priority_mask);
    return VTSS_RC_OK;
}

/********************* CIRCULAR BUFFER ********************/
static inline void lk_crc_init(vtss_circ_ring_ctl_t *r, vtss_circ_ring_idx_t size)
{
    r->crc_size = size;
    r->crc_rd_next = r->crc_wr_next = 0;
}

static inline vtss_circ_ring_idx_t lk_crc_size(const vtss_circ_ring_ctl_t *r)
{
    return r->crc_size;
}
static inline vtss_circ_ring_idx_t lk_crc_get_wr_idx(const vtss_circ_ring_ctl_t *r)
{
    return r->crc_wr_next;
}
static inline vtss_circ_ring_idx_t lk_crc_get_rd_idx(const vtss_circ_ring_ctl_t *r)
{
    return r->crc_rd_next;
}
static inline vtss_circ_ring_idx_t lk_crc_get_next(const vtss_circ_ring_ctl_t *r,
                                                   vtss_circ_ring_idx_t        idx)
{
    idx++;
    return idx >= r->crc_size ? 0 : idx;
}

static inline vtss_circ_ring_idx_t lk_crc_wr_next(vtss_circ_ring_ctl_t *r)
{
    r->crc_wr_next = lk_crc_get_next(r, r->crc_wr_next);
    return r->crc_wr_next;
}
static inline vtss_circ_ring_idx_t lk_crc_rd_next(vtss_circ_ring_ctl_t *r)
{
    r->crc_rd_next = lk_crc_get_next(r, r->crc_rd_next);
    return r->crc_rd_next;
}

static inline BOOL lk_crc_is_full(const vtss_circ_ring_ctl_t *r)
{
    return r->crc_rd_next == lk_crc_get_next(r, r->crc_wr_next);
}

static inline uint16_t lk_crc_nb_free(const vtss_circ_ring_ctl_t *r)
{
    vtss_circ_ring_idx_t dt = r->crc_wr_next - r->crc_rd_next;
    dt %= r->crc_size;
    return r->crc_size - dt - 1;
}

/****************************************************************/

static inline uint16_t lk_get_rx_ring_sz(const vtss_lk_pie_chnl_t *c)
{
    return lk_crc_size(&c->pc_rx_ring_ctl);
}
static inline uint16_t lk_get_tx_ring_sz(const vtss_lk_pie_chnl_t *c)
{
    return lk_crc_size(&c->pc_tx_ring_ctl);
}

/****************************************************************/

static vtss_rc lk_alloc_rx_bmem(vtss_state_t *vtss_state)
{
    vtss_lk_pie_chnl_t *c = lk_get_chnl(vtss_state);
    c->pc_rx_bmem = VTSS_OS_MALLOC((c->pc_buff_sz * lk_get_rx_ring_sz(c)), VTSS_MEM_FLAGS_DMA);
    if (!c->pc_rx_bmem) {
        VTSS_E("Failed memory Allocation RX");
        return VTSS_RC_ERROR;
    }
    c->pc_rx_bmem_dma = VTSS_OS_CPU_TO_DMA_ADDR(c->pc_rx_bmem);
    return VTSS_RC_OK;
}

static vtss_rc lk_alloc_tx_bmem(vtss_state_t *vtss_state)
{
    vtss_lk_pie_chnl_t *c = lk_get_chnl(vtss_state);

    c->pc_tx_bmem = VTSS_OS_MALLOC((c->pc_buff_sz * lk_get_tx_ring_sz(c)), VTSS_MEM_FLAGS_DMA);
    if (!c->pc_tx_bmem) {
        VTSS_E("Failed memory Allocation TX ");
        return VTSS_RC_ERROR;
    }
    c->pc_tx_bmem_dma = VTSS_OS_CPU_TO_DMA_ADDR(c->pc_tx_bmem);
    return VTSS_RC_OK;
}

static vtss_rc lk_xmit_update(vtss_state_t *vtss_state)
{
    vtss_circ_ring_idx_t begin, end;
    u64                  current;
    vtss_lk_pie_chnl_t  *c = lk_get_chnl(vtss_state);

    lk_idesc_read_ptr_read(vtss_state, c, &current);
    end = (current - c->pc_tx_ring_mem_dma) / sizeof(lk_pie_tx_desc_t);
    begin = lk_crc_get_rd_idx(&c->pc_tx_ring_ctl);
    while (begin != end) {
        VTSS_I("Pie finished sending frame at index %i", begin);
        begin = lk_crc_rd_next(&c->pc_tx_ring_ctl);
    }
    return VTSS_RC_OK;
}

static vtss_rc lk_tx_init(vtss_state_t *vtss_state)
{
    vtss_lk_pie_chnl_t *c = lk_get_chnl(vtss_state);
    size_t              size = sizeof(struct lk_pie_tx_desc_t) * PIE_DEFAULT_TX_DESCRIPTORS;

    lk_crc_init(&c->pc_tx_ring_ctl, PIE_DEFAULT_TX_DESCRIPTORS);
    c->pc_tx_ring_mem = VTSS_OS_MALLOC(size, VTSS_MEM_FLAGS_DMA);
    if (!c->pc_tx_ring_mem) {
        VTSS_E("Failed memory Allocation TX Init");
        return VTSS_RC_ERROR;
    }
    c->pc_tx_ring_mem_dma = VTSS_OS_CPU_TO_DMA_ADDR(c->pc_tx_ring_mem);
    c->pc_tx_ring_msb = lk_pie_bd_msb(c->pc_tx_ring_mem_dma);
    VTSS_MEMSET(c->pc_tx_ring_mem, 0, size);
    u64 start = c->pc_tx_ring_mem_dma; // init tx desc ring
    u64 end = c->pc_tx_ring_mem_dma +
              sizeof(struct lk_pie_tx_desc_t) * (lk_crc_size(&c->pc_tx_ring_ctl) - 1);
    lk_pi_desc_cfg(vtss_state, c, start, end);
    lk_idesc_write_ptr_update(vtss_state, c, start);
    return VTSS_RC_OK;
}

static vtss_rc pie_rx_fill_bp(vtss_state_t *vtss_state)
{
    vtss_lk_pie_chnl_t  *c = lk_get_chnl(vtss_state);
    lk_pie_rx_desc_t     desc = {};
    lk_pie_rx_desc_t    *pd;
    u64                  bmem_dma;
    bool                 update_hw = FALSE;
    vtss_circ_ring_idx_t id;

    VTSS_MEMSET(&desc, 0, sizeof(desc));
    desc.valid = 1;
    desc.owner = 0;
    desc.block_len = c->pc_buff_sz;

    id = lk_crc_get_wr_idx(&c->pc_rx_ring_ctl);
    pd = &c->pc_rx_ring_mem[id];
    while (!lk_crc_is_full(&c->pc_rx_ring_ctl)) {
        bmem_dma = c->pc_rx_bmem_dma + (id * c->pc_buff_sz);
        desc.memory_addr = bmem_dma;
        desc.valid = 1;
        desc.owner = 0;
        desc.block_len = c->pc_buff_sz;
        VTSS_I("Allocate RX packet at id=%i dma_addr=0x%lx ", id, bmem_dma);
        *pd = desc;
        update_hw = TRUE;
        lk_crc_wr_next(&c->pc_rx_ring_ctl);
        id = lk_crc_get_wr_idx(&c->pc_rx_ring_ctl);
        pd = &c->pc_rx_ring_mem[id];
    }
    if (update_hw) {
        lk_rdesc_write_ptr_update(vtss_state, c,
                                  c->pc_rx_ring_mem_dma + (sizeof(lk_pie_rx_desc_t) * id));
    }
    return VTSS_RC_OK;
}

static vtss_rc lk_rx_init(vtss_state_t *vtss_state)
{
    vtss_lk_pie_chnl_t *c = lk_get_chnl(vtss_state);
    size_t              size = sizeof(struct lk_pie_rx_desc_t) * PIE_DEFAULT_RX_DESCRIPTORS;

    lk_crc_init(&c->pc_rx_ring_ctl, PIE_DEFAULT_RX_DESCRIPTORS);
    c->pc_rx_ring_mem = VTSS_OS_MALLOC(size, VTSS_MEM_FLAGS_DMA);
    if (!c->pc_rx_ring_mem) {
        VTSS_E("Failed memory Allocation RX Init");
        return VTSS_RC_ERROR;
    }
    c->pc_rx_ring_mem_dma = VTSS_OS_CPU_TO_DMA_ADDR(c->pc_rx_ring_mem);
    c->pc_rx_ring_msb = lk_pie_bd_msb(c->pc_rx_ring_mem_dma);
    VTSS_MEMSET(c->pc_rx_ring_mem, 0, size);
    u64 start = c->pc_rx_ring_mem_dma;
    u64 end = c->pc_rx_ring_mem_dma +
              sizeof(struct lk_pie_rx_desc_t) * (lk_crc_size(&c->pc_rx_ring_ctl) - 1);
    lk_edesc_read_ptr_update(vtss_state, c, start);
    lk_rdesc_write_ptr_update(vtss_state, c, start);
    lk_pe_desc_cfg(vtss_state, c, start, end);
    return VTSS_RC_OK;
}

vtss_rc lk_init(vtss_state_t *vtss_state, u32 cpu_port)
{
    vtss_rc             rc = VTSS_RC_INCOMPLETE;
    vtss_lk_pie_chnl_t *c = lk_get_chnl(vtss_state);
    VTSS_MEMSET(c, 0, sizeof(*c));

    c->cpu_port = (cpu_port == RT_CHIP_PORT_CPU_0) ? 0 : 1;
    c->chnl_id = 0;
    c->pc_buff_sz = CEIL_ALIGN(MTU, P64H_PIE_BUFF_ALIGN);

    rc = lk_chn_traffic_disable(vtss_state);
    rc = lk_tx_init(vtss_state);
    rc = lk_rx_init(vtss_state);
    rc = lk_setup_tx_cfg(vtss_state);
    rc = lk_setup_rx_cfg(vtss_state);
    rc = lk_chn_traffic_enable(vtss_state);
    rc = lk_alloc_rx_bmem(vtss_state);
    rc = lk_alloc_tx_bmem(vtss_state);
    rc = pie_rx_fill_bp(vtss_state);
    return rc;
}

static void lk_pie_chnl_rx_desc_printout(vtss_state_t     *vtss_state,
                                         const char       *reason,
                                         lk_pie_rx_desc_t *d,
                                         int               i)
{
    u64 addr = d->memory_addr;
    u32 block_len = d->block_len;
    u32 packet_len = d->pkt_len;

    VTSS_E("Rx desc: %s, id=%i addr=0x%lx blk_len=%i pkt_len=%i %s%s%s%s%s%s", reason, i, addr,
           block_len, packet_len, (d->valid ? "V" : ""), (d->owner ? "O" : ""), (d->sop ? "S" : ""),
           (d->eop ? "E" : ""), (d->ifh_present ? " IFH" : ""), (d->rofh_present ? " ROFH" : ""));
    if (d->error_present)
        VTSS_E("Err=%s%s%s%s%s%s%s%s", (d->err_invalid_isdx ? " ISDX" : ""),
               (d->err_checksum ? " CKSUM" : ""), (d->err_missing_sof ? " MSOF" : ""),
               (d->err_missing_eof ? " MEOF" : ""), (d->err_1clock_pkt ? " 1CLK" : ""),
               (d->err_wrong_unused ? " WRNG" : ""), (d->err_fifo_overflow ? " OVFLW" : ""),
               (d->err_pkt_oversize ? " OVSZ" : ""));
}

vtss_rc lk_pie_chnl_rx(vtss_state_t *vtss_state, void *data, const u32 buflen, u8 *ifh, u32 *pktlen)
{
    vtss_circ_ring_idx_t rxid, endid;
    lk_pie_rx_desc_t    *d;
    u8                  *pkt;
    u64                  end;
    vtss_rc              ret;
    vtss_lk_pie_chnl_t  *c = lk_get_chnl(vtss_state);

    lk_edesc_write_ptr_read(vtss_state, c, &end);
    endid = (end - c->pc_rx_ring_mem_dma) / sizeof(lk_pie_rx_desc_t);
    rxid = lk_crc_get_rd_idx(&c->pc_rx_ring_ctl);
    if (rxid == endid) {
        return VTSS_RC_INCOMPLETE;
    }
    d = &c->pc_rx_ring_mem[rxid];

    ret = VTSS_RC_ERROR;
    if (!d->valid) {
        lk_pie_chnl_rx_desc_printout(vtss_state, "no valid bit", d, rxid);
    } else if (!d->owner) {
        lk_pie_chnl_rx_desc_printout(vtss_state, "invalid owner bit", d, rxid);
    } else if (d->rofh_present) {
        lk_pie_chnl_rx_desc_printout(vtss_state, "rofh bit", d, rxid);
    } else if (!d->sop || !d->eop) {
        lk_pie_chnl_rx_desc_printout(vtss_state, "no SOP or EOP bit", d, rxid);
    } else if (d->error_present || d->err_invalid_isdx || d->err_checksum || d->err_missing_sof ||
               d->err_missing_eof || d->err_1clock_pkt || d->err_wrong_unused ||
               d->err_fifo_overflow || d->err_pkt_oversize) {
        lk_pie_chnl_rx_desc_printout(vtss_state, "error bit set", d, rxid);
    } else {
        ret = VTSS_RC_OK;
        pkt = c->pc_rx_bmem + (rxid * c->pc_buff_sz);
        *pktlen = d->pkt_len;
        VTSS_I("Received packet, pkt_len=%i, block_len=%i", (uint32_t)d->pkt_len,
               (uint32_t)d->block_len);
        VTSS_I_HEX(pkt, d->pkt_len);
        VTSS_MEMCPY(ifh, pkt, VTSS_FA_RX_IFH_SIZE);
        if (buflen < d->pkt_len) {
            VTSS_E("laika rx buffer too small %d", buflen);
        }
        VTSS_MEMCPY(data, (pkt + VTSS_FA_RX_IFH_SIZE),
                    MIN(buflen, d->pkt_len - VTSS_FA_RX_IFH_SIZE));
    }

    // Move on to the next descriptor and refill rx descs even if there was an error in the last
    // packet.
    rxid = lk_crc_rd_next(&c->pc_rx_ring_ctl);
    end = c->pc_rx_ring_mem_dma + rxid * sizeof(lk_pie_rx_desc_t);
    lk_edesc_read_ptr_update(vtss_state, c, end);
    pie_rx_fill_bp(vtss_state);
    return ret;
}

vtss_rc lk_pie_chnl_tx(vtss_state_t                     *vtss_state,
                       const void *const                 frame,
                       u16                               flen,
                       const vtss_packet_tx_ifh_t *const ifh)
{
    vtss_circ_ring_idx_t txid;
    lk_pie_tx_desc_t     tx_desc;
    uint8_t             *bp;
    u64                  end;
    vtss_lk_pie_chnl_t  *c = lk_get_chnl(vtss_state);

    if (ifh->length != VTSS_FA_RX_IFH_SIZE) {
        VTSS_E("IFH length mismatch");
        return VTSS_RC_ERROR;
    }

    if ((ifh->length + flen) > MTU) {
        VTSS_E("IFH length greater than MTU");
        return VTSS_RC_ERROR;
    }

    lk_xmit_update(vtss_state);
    if (!lk_crc_nb_free(&c->pc_tx_ring_ctl)) {
        VTSS_E("No free buffer to allocate for TX Segment");
        return VTSS_RC_ERROR;
    }

    txid = lk_crc_get_wr_idx(&c->pc_tx_ring_ctl);
    bp = c->pc_tx_bmem + (c->pc_buff_sz * txid);
    VTSS_MEMCPY(bp, ifh->ifh, ifh->length);
    if (flen < MIN_FLEN) {
        VTSS_MEMSET(bp + ifh->length, 0, MIN_FLEN); // if flen < 64 must pad frame with all zeros
        flen = MIN_FLEN;
    }
    VTSS_MEMCPY(bp + ifh->length, frame, flen);
    VTSS_MEMSET(&tx_desc, 0, sizeof(tx_desc));
    tx_desc.valid = 1;
    tx_desc.sop = 1;
    tx_desc.eop = 1;
    tx_desc.block_len = flen + ifh->length;
    tx_desc.memory_addr = c->pc_tx_bmem_dma + (c->pc_buff_sz * txid);
    VTSS_I_HEX(bp, tx_desc.block_len);
    VTSS_I("Queue packet id=%i dma_addr=0x%lx block_len=%i flen=%i ifh->length=%i", txid,
           (uint64_t)tx_desc.memory_addr, (uint32_t)tx_desc.block_len, flen, ifh->length);
    c->pc_tx_ring_mem[txid] = tx_desc;
    txid = lk_crc_wr_next(&c->pc_tx_ring_ctl);
    end = c->pc_tx_ring_mem_dma + (sizeof(lk_pie_tx_desc_t) * txid);
    lk_idesc_write_ptr_update(vtss_state, c, end);
    return VTSS_RC_OK;
}

#endif /* VTSS_ARCH_LAIKA */
