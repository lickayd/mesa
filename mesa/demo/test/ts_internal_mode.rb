#!/usr/bin/env ruby

# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

require_relative 'libeasy/et'
require_relative 'ts_lib'

################################################
# Capability & Configuration
################################################

$ts = get_test_setup("mesa_pc_b2b_2x", {}, "", "loop")

cfg = { cap_array: ["PACKET_TX_IFH_SIZE"] }
cap_check_ts(cfg)

loop_pair_check()
$loop_port0 = $ts.dut.looped_port_list[0]
$loop_port1 = $ts.dut.looped_port_list[1]

$port0 = 0
$npi_port = 1
$port1 = 2
$cpu_queue = 7
$vlan = 100

# Laguna is longer as it is an FPGA with longer forwarding time
$max_diff = (cap_get("MISC_CHIP_FAMILY") == chip_family_to_id("MESA_CHIP_FAMILY_LAN969X")) ? 7650 : 4000

################################################
# Test Tables
################################################

test_table =
[
    {
        txt: "PCH Operation Mode - Default",
        cfg: nil,
        chk: { port_id: 0, tx_mode: 0, rx_mode: 0, err_mode: 3 },
        fun: -> (t) { pch_operation_mode_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "PCH Operation Mode - Encrypt Bit / 28-4",
        cfg: { tx_pch_mode: "MESA_TS_PCH_TX_MODE_ENCRYPT_BIT", rx_pch_mode: "MESA_TS_PCH_RX_MODE_28_4", pch_port_id: 0x05 },
        chk: { port_id: 0x05, tx_mode: 2, rx_mode: 2, err_mode: 3 },
        fun: -> (t) { pch_operation_mode_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "PCH Operation Mode - Encrypt Bit Invert SMAC / 16-16",
        cfg: { tx_pch_mode: "MESA_TS_PCH_TX_MODE_ENCRYPT_BIT_INVERT_SMAC", rx_pch_mode: "MESA_TS_PCH_RX_MODE_16_16", pch_port_id: 0x0A },
        chk: { port_id: 0x0A, tx_mode: 3, rx_mode: 4, err_mode: 3 },
        fun: -> (t) { pch_operation_mode_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "PCH Operation Mode - Disabled",
        cfg: { tx_pch_mode: "MESA_TS_PCH_TX_MODE_NONE", rx_pch_mode: "MESA_TS_PCH_RX_MODE_NONE", pch_port_id: 0 },
        chk: { port_id: 0, tx_mode: 0, rx_mode: 0, err_mode: 3 },
        fun: -> (t) { pch_operation_mode_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "Internal Mode - Ingress Node",
        cfg: { port_internal: $ts.dut.port_list[$port0], port_none: $loop_port1, header_rsv: 0 },
        chk: -> (data, tod_nano_tx, header_rsv) { check_reserved2_field(data, tod_nano_tx) },
        fun: -> (t) { tod_internal_mode_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "Internal Mode - Egress Node",
        cfg: { port_internal: $loop_port1, port_none: $ts.dut.port_list[$port0], header_rsv: 35_000_000 },
        chk: -> (data, tod_nano_tx, header_rsv) { check_correction_field(data, tod_nano_tx, header_rsv) },
        fun: -> (t) { tod_internal_mode_test(t[:cfg], t[:chk]) }
    },
]

################################################
# General/Helper Functions
################################################

def ts_operation_mode_set(port, mode)
    conf = $ts.dut.call("mesa_ts_operation_mode_get", port)
    conf["domain"] = 0
    conf["mode"] = mode
    $ts.dut.call("mesa_ts_operation_mode_set", port, conf)
end

def ts_timestamp_age_out()
    4.times { $ts.dut.call("mesa_timestamp_age") }
end

def pch_reg_val(cmd, idx, base = 10)
    out = $ts.dut.run(cmd)
    out[:out].split(" ")[idx].to_i(base)
end

def pch_field(regval, mask, shift)
    (regval & mask) >> shift
end

def pch_read(port)
    chip_port = $ts.port_map[port]["chip_port"]
    t_i("port #{port} chip_port #{chip_port}")
    ret = {}

    if (cap_get("MISC_CHIP_FAMILY") == chip_family_to_id("MESA_CHIP_FAMILY_LAN966X"))
        regval = pch_reg_val("symreg_lan966x SYS:PTPPORT[#{chip_port}]:PCH_CFG", 2, 16)
        ret[:port_id] = pch_field(regval, 0x780, 7)
        ret[:tx_mode] = pch_field(regval, 0x60, 5)
        ret[:rx_mode] = pch_field(regval, 0x1C, 2)
        ret[:err_mode] = pch_field(regval, 0x03, 0)
    end

    if ((cap_get("MISC_CHIP_FAMILY") == chip_family_to_id("MESA_CHIP_FAMILY_LAN969X")) ||
        (cap_get("MISC_CHIP_FAMILY") == chip_family_to_id("MESA_CHIP_FAMILY_SPARX5")))
        regval = pch_reg_val("mesa-cmd deb sym read DEV2G5[#{chip_port}]:PTP_CFG_STATUS:PTP_CFG", 13)
        ret[:port_id] = pch_field(regval, 0xF00, 8)
        ret[:tx_mode] = pch_field(regval, 0x18, 3)
        ret[:rx_mode] = pch_field(regval, 0x07, 0)
        ret[:err_mode] = pch_field(regval, 0x6000, 13)
        if (cap_get("MISC_CHIP_FAMILY") == chip_family_to_id("MESA_CHIP_FAMILY_SPARX5"))
            ret[:err_mode] = 3
        end
    end

    return ret
end

def check_pch_cfg(pch, port_id_chk, tx_mode_chk, rx_mode_chk, err_mode_chk)
    chk = { port_id: port_id_chk, tx_mode: tx_mode_chk, rx_mode: rx_mode_chk, err_mode: err_mode_chk }
    t_i("check_pch_cfg")
    chk.each do |key, expect|
        t_e("Unexpected #{key} #{pch[key]}  #{key}_chk #{expect}") if (pch[key] != expect)
    end
end

def ptp_field_val(data, hdr_offset, len)
    return data[14 + hdr_offset, len].inject(0) { |val, byte| (val << 8) | byte }
end

def check_ts_diff(diff, err_msg)
    t_e(err_msg) if ((diff < 0) || (diff > $max_diff))
end

def transmit_sync_frame(ts_id, header_rsv)
    frame_hdr = frame_create("00:02:03:04:05:06", "00:08:09:0a:0b:0c")
    frame = tx_ifh_create($loop_port0, "MESA_PACKET_PTP_ACTION_TWO_STEP", ts_id << 16) + frame_hdr.dup + sync_pdu_create(header_rsv)

    # Reset TOD to zero right before transmitting, after the frame is already built, so none of
    # the frame-build RPC round trips land in the reset-to-TX window and inflate/skew it.
    tod_ts = $ts.dut.call("mesa_ts_timeofday_get")
    tod_ts[0]["seconds"] = 0
    tod_ts[0]["nanoseconds"] = 0
    $ts.dut.call("mesa_ts_timeofday_set", tod_ts[0])
    tod_ret = $ts.dut.call("mesa_ts_timeofday_get")

    frame_cfg = { frame: frame, port: $npi_port, capture_size: 60, port0: $port0, port1: $port1, npi_port: $npi_port }
    frame_tx(frame_cfg)
    return tod_ret
end

def capture_frame_bytes(port)
    pkts = $ts.pc.get_pcap("#{$ts.links[port][:pc]}.pcap")
    return pkts[0][:data].each_byte.map { |byte| byte.to_i }
end

def check_reserved2_field(data, tod_nano_tx)
    frame_ts = ptp_field_val(data, 16, 4)

    diff = frame_ts - (tod_nano_tx >> 16)
    t_i("Difference between frame and TX-FIFO #{diff}  frame_ts #{frame_ts}  tod_nano_tx #{tod_nano_tx >> 16}")
    check_ts_diff(diff, "SYNC PDU reserved field not as expected.")
end

def check_correction_field(data, tod_nano_tx, header_rsv)
    nano_correction = ptp_field_val(data, 8, 8)

    smallest_corr_value = (tod_nano_tx >> 16) - header_rsv
    diff = (nano_correction >> 16) - smallest_corr_value
    t_i("Difference between frame and TX-FIFO #{diff}  nano_correction #{nano_correction >> 16}  tod_nano_tx #{tod_nano_tx >> 16}  smallest_corr_value #{smallest_corr_value}")
    check_ts_diff(diff, "SYNC PDU correction field not as expected.")
end

################################################
# Test Section
################################################

test "conf" do
    $ts.dut.run("mesa-cmd port mode #{$loop_port0+1} 1000fdx")
    $ts.dut.run("mesa-cmd port mode #{$loop_port1+1} 1000fdx")

    # disable VLAN 1 to avoid looping
    $ts.dut.call("mesa_vlan_port_members_set", 1, "")

    # Flush MAC table
    $ts.dut.call("mesa_mac_table_flush")

    # CPU queue configuration
    $packet_rx_conf_restore = $ts.dut.call("mesa_packet_rx_conf_get")
    conf = $packet_rx_conf_restore.dup
    conf["queue"][$cpu_queue]["npi"]["enable"] = true
    $ts.dut.call("mesa_packet_rx_conf_set", conf)

    # NPI port configuration save
    $npi_conf_restore = $ts.dut.call("mesa_npi_conf_get")
    conf = $npi_conf_restore.dup
    conf["enable"] = true
    conf["port_no"] = $ts.dut.port_list[$npi_port]
    $ts.dut.call("mesa_npi_conf_set", conf)

    # Set VLAN port configuration
    conf = $ts.dut.call("mesa_vlan_port_conf_get", $ts.dut.port_list[$port0])
    $vlan_port_conf_restore0 = conf.dup
    conf["port_type"] = "MESA_VLAN_PORT_TYPE_UNAWARE"
    conf["pvid"] = $vlan
    conf["untagged_vid"] = $vlan
    conf["frame_type"] = "MESA_VLAN_FRAME_ALL"
    $ts.dut.call("mesa_vlan_port_conf_set", $ts.dut.port_list[$port0], conf)

    conf = $ts.dut.call("mesa_vlan_port_conf_get", $loop_port1)
    $vlan_port_conf_restore1 = conf.dup
    conf["port_type"] = "MESA_VLAN_PORT_TYPE_UNAWARE"
    conf["pvid"] = $vlan
    conf["untagged_vid"] = $vlan
    conf["frame_type"] = "MESA_VLAN_FRAME_ALL"
    $ts.dut.call("mesa_vlan_port_conf_set", $loop_port1, conf)

    # Set VLAN memberships
    port_list = "#{$ts.dut.port_list[$port0]},#{$loop_port1}"
    $ts.dut.call("mesa_vlan_port_members_set", $vlan, port_list)

    # Create IS2 to ONE-STEP SYNC frame
    conf = $ts.dut.call("mesa_ace_init", "MESA_ACE_TYPE_ETYPE")
    conf["id"] = 1 # ACL ID
    conf["port_list"] = "#{$loop_port1}"
    action = conf["action"]
    action["ptp_action"] = "MESA_ACL_PTP_ACTION_ONE_STEP"
    $ts.dut.call("mesa_ace_add", 0, conf)

    # Port PTP domain configuration save
    $operation_mode_0_restore =  $ts.dut.call("mesa_ts_operation_mode_get", $ts.dut.port_list[$port0])
    $operation_mode_l1_restore = $ts.dut.call("mesa_ts_operation_mode_get", $loop_port1)

    # Internal mode configuration save
    $internal_mode = $ts.dut.call("mesa_ts_internal_mode_get")
end

def pch_operation_mode_test(cfg, chk)
    t_i("Test: PCH Operation Mode")
    if (cap_get("TS_PCH") == 0)
        test_skip()
        return
    end
    port = $ts.dut.port_list[$port0]

    if (cfg != nil)
        conf = $ts.dut.call("mesa_ts_operation_mode_get", port)
        conf["domain"] = 0
        conf["mode"] = "MESA_TS_MODE_NONE"
        conf["tx_pch_mode"] = cfg[:tx_pch_mode]
        conf["rx_pch_mode"] = cfg[:rx_pch_mode]
        conf["pch_port_id"] = cfg[:pch_port_id]
        $ts.dut.call("mesa_ts_operation_mode_set", port, conf)
    end

    pch = pch_read(port)
    check_pch_cfg(pch, chk[:port_id], chk[:tx_mode], chk[:rx_mode], chk[:err_mode])
end

def tod_internal_mode_test(cfg, chk)
    t_i("Test: Internal Mode")

    ts_operation_mode_set(cfg[:port_none], "MESA_TS_MODE_NONE")

    # Configure internal mode format. This must be called before 'ts_operation_mode'
    conf = $ts.dut.call("mesa_ts_internal_mode_get")
    conf["int_fmt"] = "MESA_TS_INTERNAL_FMT_RESERVED_LEN_30BIT"
    $ts.dut.call("mesa_ts_internal_mode_set", conf)

    ts_operation_mode_set(cfg[:port_internal], "MESA_TS_MODE_INTERNAL")

    # age out any allocated timestamps id's
    ts_timestamp_age_out()

    # Allocate a timestamp id
    conf = {port_mask: 1<<$loop_port0, context: 0, cb: 0}
    idx0 = $ts.dut.call("mesa_tx_timestamp_idx_alloc", conf)    # Just to make sure that the test is working with idx ather than 0
    idx = $ts.dut.call("mesa_tx_timestamp_idx_alloc", conf)

    t_i("Transmit a Two-Step SYNC frame into NPI port with the allocated timestamp id")
    tod_ret = transmit_sync_frame(idx["ts_id"], cfg[:header_rsv])

    t_i("Update the TX FIFO in AIL. This will cause callback to Json with the TX timestamp")
    $ts.dut.call("mesa_tx_timestamp_update")

    # Get the TX time stamp. This is not a MESA API function, only a Json implementation to get the TX timestamp delivered through callback
    ts_tx = $ts.dut.call("mesa_tx_timestamp_get")

    t_i("Calculate the TX TOD nanoseconds based on TX time stamp")
    if (cap_get("MISC_CHIP_FAMILY") == chip_family_to_id("MESA_CHIP_FAMILY_JAGUAR2"))
        tod_nano_tx = tc_to_tod_nano(ts_tx["ts"], tod_ret)
    else
        tod_nano_tx = ts_tx["ts"]
    end

    if ((ts_tx["id"] != idx["ts_id"]) || (ts_tx["ts_valid"] != true))
        t_e("Not the expected TX timestamp. ts_tx[id] = #{ts_tx["id"]}  idx[ts_id] = #{idx["ts_id"]}  ts_tx[ts_valid] = #{ts_tx["ts_valid"]}")
    end

    data = capture_frame_bytes($port0)
    chk.call(data, tod_nano_tx, cfg[:header_rsv])

    # age out the allocated timestamps id's
    ts_timestamp_age_out()
end

################################################
# Test Runners
################################################

def test_runner(t)
    fun = fld_get(t, :fun, nil)

    fun.call(t) if (fun != nil)
end

# Run all or selected test
sel = table_lookup(test_table, :sel)
test_table.each do |t|
    next if (t[:sel] != sel)
    rep = fld_get(t, :rep, 1)
    rep.times do |i|
        txt = (rep == 1) ? t[:txt] : "#{t[:txt]} (#{i + 1}/#{rep})"
        test(txt) do
            test_runner(t)
        end
    end
end

################################################
# Test Dump & Summary
################################################

test_summary()

test("dump") do
    #$ts.dut.run("mesa-cmd deb api ci ts")
end
