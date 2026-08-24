#!/usr/bin/env ruby

# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

require_relative 'libeasy/et'
require_relative 'ts_lib'

################################################
# Capability & Configuration
################################################

$ts = get_test_setup("mesa_pc_b2b_4x", {}, "")

cfg = { cap_array: ["PACKET_INJ_ENCAP", "PACKET_TX_IFH_SIZE"] }
cap_check_ts(cfg)
loop_pair_check()

$pcb = $ts.dut.pcb

$vlan = 100
$port0 = 0
$port1 = 1
$npi_port = 2
$cpu_queue = 7
$acl_id = 1

$port_map = $ts.dut.call("mesa_port_map_get", cap_get("PORT_CNT"))
$cap_family = $ts.dut.call("mesa_capability", "MESA_CAP_MISC_CHIP_FAMILY")

################################################
# Test Table
################################################

test_table =
[
    {
        txt: "ORIGIN-TIMESTAMP SYNC frame check (ipv4)",
        cfg: -> () { {ip: "ipv4"} },
        fun: -> (t) { c = t[:cfg].call; ptp_sync_test(c[:ip]) }
    },
    {
        txt: "ORIGIN-TIMESTAMP REQUEST correction field check (ipv4)",
        cfg: -> () { {ip: "ipv4"} },
        fun: -> (t) { c = t[:cfg].call; ptp_origin_request_test(c[:ip]) }
    },
    {
        txt: "ONE-STEP REQUEST correction field check (ipv4)",
        cfg: -> () { {ip: "ipv4"} },
        fun: -> (t) { c = t[:cfg].call; ptp_one_step_request_test(c[:ip]) }
    },
    {
        txt: "ONE-STEP SYNC through switch correction field check (ipv4)",
        cfg: -> () { {ip: "ipv4"} },
        chk: {max_corr: {default: 3160, chip_family_to_id("MESA_CHIP_FAMILY_LAN966X") => 3400}},
        fun: -> (t) { c = t[:cfg].call; ptp_switch_one_step_test(c[:ip], t[:chk]) }
    },
    {
        txt: "ORIGIN-TIMESTAMP SYNC frame check (ipv6)",
        cfg: -> () { {ip: "ipv6"} },
        fun: -> (t) { c = t[:cfg].call; ptp_sync_test(c[:ip]) }
    },
    {
        txt: "ORIGIN-TIMESTAMP REQUEST correction field check (ipv6)",
        cfg: -> () { {ip: "ipv6"} },
        fun: -> (t) { c = t[:cfg].call; ptp_origin_request_test(c[:ip]) }
    },
    {
        txt: "ONE-STEP REQUEST correction field check (ipv6)",
        cfg: -> () { {ip: "ipv6"} },
        fun: -> (t) { c = t[:cfg].call; ptp_one_step_request_test(c[:ip]) }
    },
    {
        txt: "ONE-STEP SYNC through switch correction field check (ipv6)",
        cfg: -> () { {ip: "ipv6"} },
        chk: {max_corr: {default: 3160, chip_family_to_id("MESA_CHIP_FAMILY_LAN966X") => 3400}},
        fun: -> (t) { c = t[:cfg].call; ptp_switch_one_step_test(c[:ip], t[:chk]) }
    },
]

################################################
# General/Helper Functions
################################################

def ptp_ip_offsets(ip)
    size = 44+28+17
    off = 14+28
    if (ip == "ipv6")
        size += 20
        off += 20
    end
    { size: size, off: off }
end

def ptp_domain_tod_set(seconds, domain)
    conf = $ts.dut.call("mesa_ts_operation_mode_get", $ts.dut.port_list[$port0])
    conf["domain"] = domain
    $ts.dut.call("mesa_ts_operation_mode_set", $ts.dut.port_list[$port0], conf)

    tod_ts = $ts.dut.call("mesa_ts_domain_timeofday_get", domain)
    tod_ts[0]["seconds"] = seconds
    tod_ts[0]["nanoseconds"] = 0
    $ts.dut.call("mesa_ts_domain_timeofday_set", domain, tod_ts[0])
end

################################################
# Test Section
################################################

test "test_conf" do
    # disable VLAN 1 to avoid looping
    $ts.dut.call("mesa_vlan_port_members_set", 1, "")

    # CPU queue configuration
    $packet_rx_conf_restore = $ts.dut.call("mesa_packet_rx_conf_get")
    conf = $packet_rx_conf_restore.dup
    conf["queue"][$cpu_queue]["npi"]["enable"] = true
    $ts.dut.call("mesa_packet_rx_conf_set", conf)

    # NPI port configuration save
    conf = $ts.dut.call("mesa_npi_conf_get")
    conf["enable"] = true
    conf["port_no"] = $ts.dut.port_list[$npi_port]
    $ts.dut.call("mesa_npi_conf_set", conf)

    # Set VLAN port configuration
    conf = $ts.dut.call("mesa_vlan_port_conf_get", $ts.dut.port_list[$port0])
    conf["port_type"] = "MESA_VLAN_PORT_TYPE_UNAWARE"
    conf["pvid"] = $vlan
    conf["untagged_vid"] = $vlan
    conf["frame_type"] = "MESA_VLAN_FRAME_ALL"
    $ts.dut.call("mesa_vlan_port_conf_set", $ts.dut.port_list[$port0], conf)

    conf = $ts.dut.call("mesa_vlan_port_conf_get", $ts.dut.port_list[$port1])
    conf["port_type"] = "MESA_VLAN_PORT_TYPE_UNAWARE"
    conf["pvid"] = $vlan
    conf["untagged_vid"] = $vlan
    conf["frame_type"] = "MESA_VLAN_FRAME_ALL"
    $ts.dut.call("mesa_vlan_port_conf_set", $ts.dut.port_list[$port1], conf)

    # Set VLAN memberships
    port_list = "#{$ts.dut.port_list[$port0]},#{$ts.dut.port_list[$port1]}"
    $ts.dut.call("mesa_vlan_port_members_set", $vlan, port_list)
end

def ptp_sync_test(ip)
    seconds = 10
    domain = 0
    p = ptp_ip_offsets(ip)
    ptp_domain_tod_set(seconds, domain)

    frameHdrTx = frame_create("00:02:03:04:05:06", "00:08:09:0a:0b:0c", "#{ip} udp")
    frametx = tx_ifh_create($ts.dut.port_list[$port0], "MESA_PACKET_PTP_ACTION_ORIGIN_TIMESTAMP_SEQ", 0xFEFEFEFE0000, 0, 0, ip) + frameHdrTx.dup + sync_pdu_create()

    frame_cfg = { frame: frametx, port: $npi_port, capture_size: p[:size], port0: $port0, port1: $port1, npi_port: $npi_port }
    frame_tx(frame_cfg)
    pkts = $ts.pc.get_pcap "#{$ts.links[$port0][:pc]}.pcap"
    data = pkts[0][:data].each_byte.map{|c| c.to_i}
    t_i("data #{data}")

    off = p[:off]
    origin_sec = ((data[off+34]<<40) + (data[off+35]<<32) + (data[off+36]<<24) + (data[off+37]<<16) + (data[off+38]<<8) + (data[off+39]))
    t_i("origin_sec #{origin_sec}")
    if (origin_sec != seconds+3)
        t_e("Origin not as expected")
    end
end

def ptp_origin_request_test(ip)
    seconds = 10
    domain = 0
    requestClockId = 0xAABBCCDDEEFFAABB
    requestPortNumber = 0xABCD
    p = ptp_ip_offsets(ip)
    ptp_domain_tod_set(seconds, domain)
    time_a = Time.now()

    frameHdrTx = frame_create("00:02:03:04:05:06", "00:08:09:0a:0b:0c", "#{ip} udp")
    frametx = tx_ifh_create($ts.dut.port_list[$port0], "MESA_PACKET_PTP_ACTION_ORIGIN_TIMESTAMP", 0xFEFEFEFE0000, 0, 0, ip) + frameHdrTx.dup + request_pdu_create(requestClockId, requestPortNumber)

    frame_cfg = { frame: frametx, port: $npi_port, capture_size: p[:size], port0: $port0, port1: $port1, npi_port: $npi_port }
    frame_tx(frame_cfg)
    time_b = Time.now()
    pkts = $ts.pc.get_pcap "#{$ts.links[$port0][:pc]}.pcap"
    data = pkts[0][:data].each_byte.map{|c| c.to_i}
    t_i("data #{data}")

    off = p[:off]
    nano_correction = ((data[off+8]<<40) + (data[off+9]<<32) + (data[off+10]<<24) + (data[off+11]<<16) + (data[off+12]<<8) + (data[off+13]))
    origin_sec = ((data[off+34]<<40) + (data[off+35]<<32) + (data[off+36]<<24) + (data[off+37]<<16) + (data[off+38]<<8) + (data[off+39]))
    origin_nsec = ((data[off+40]<<24) + (data[off+41]<<16) + (data[off+42]<<8) + (data[off+43]))
    origin_f = origin_sec.to_f + origin_nsec/1_000_000_000.0

    # Origin is the live TOD written by HW, so its upper bound must track the
    # measured console round trip, not a fixed window.
    execution = time_b - time_a
    tolerance_s = 0.5

    t_i("nano_correction #{nano_correction}")
    t_i("origin_f #{origin_f}")
    t_i("execution #{execution}  tolerance_s #{tolerance_s}")
    if (nano_correction > 1000) || (nano_correction < 300)
        t_e("Origin not as expected")
    end
    if ((origin_f > (seconds + execution + tolerance_s)) || (origin_f < seconds))
        t_e("Origin not as expected.  origin_sec #{origin_sec}  origin_nsec #{origin_nsec}")
    end
end

def ptp_one_step_request_test(ip)
    seconds = 10
    domain = 0
    requestClockId = 0xAABBCCDDEEFFAABB
    requestPortNumber = 0xABCD
    p = ptp_ip_offsets(ip)
    ptp_domain_tod_set(seconds, domain)
    time_a = Time.now()

    frameHdrTx = frame_create("00:02:03:04:05:06", "00:08:09:0a:0b:0c", "#{ip} udp")
    frametx = tx_ifh_create($ts.dut.port_list[$port0], "MESA_PACKET_PTP_ACTION_ONE_STEP", (seconds * 1_000_000_000) << 16, 0, 0, ip) + frameHdrTx.dup + request_pdu_create(requestClockId, requestPortNumber)

    frame_cfg = { frame: frametx, port: $npi_port, capture_size: p[:size], port0: $port0, port1: $port1, npi_port: $npi_port }
    frame_tx(frame_cfg)
    time_b = Time.now()
    pkts = $ts.pc.get_pcap "#{$ts.links[$port0][:pc]}.pcap"
    data = pkts[0][:data].each_byte.map{|c| c.to_i}
    t_i("data #{data}")

    off = p[:off]
    nano_correction = ((data[off+8]<<40) + (data[off+9]<<32) + (data[off+10]<<24) + (data[off+11]<<16) + (data[off+12]<<8) + (data[off+13]))
    origin_sec = ((data[off+34]<<40) + (data[off+35]<<32) + (data[off+36]<<24) + (data[off+37]<<16) + (data[off+38]<<8) + (data[off+39]))
    origin_nsec = ((data[off+40]<<24) + (data[off+41]<<16) + (data[off+42]<<8) + (data[off+43]))
    origin_f = origin_sec.to_f + origin_nsec/1_000_000_000.0

    # Correction is the live TOD minus the fixed IFH timestamp, so its expected
    # value must track the measured console round trip, not a fixed window.
    execution = time_b - time_a
    expected_ns = execution * 1_000_000_000
    tolerance_ns = 500_000_000
    floor_ns = 300

    t_i("nano_correction #{nano_correction}")
    t_i("origin_f #{origin_f}")
    t_i("execution #{execution}  expected_ns #{expected_ns}  tolerance_ns #{tolerance_ns}")
    if (nano_correction > (expected_ns + tolerance_ns)) || (nano_correction < floor_ns)
        t_e("Origin not as expected")
    end
    if (origin_f != 0)
        t_e("Origin not as expected.  origin_sec #{origin_sec}  origin_nsec #{origin_nsec}")
    end
end

def ptp_switch_one_step_test(ip, chk)
    max_corr_map = fld_get(chk, :max_corr, {default: 3160})
    max_corr = fld_get(max_corr_map, $cap_family, fld_get(max_corr_map, :default, 3160))

    conf = $ts.dut.call("mesa_ace_init", (ip == "ipv4") ? "MESA_ACE_TYPE_IPV4" : "MESA_ACE_TYPE_IPV6")
    conf["id"] = $acl_id
    conf["port_list"] = "#{$ts.dut.port_list[$port0]}"
    action = conf["action"]
    action["ptp_action"] = "MESA_ACL_PTP_ACTION_ONE_STEP"
    $ts.dut.call("mesa_ace_add", 0, conf)

    lowest_corr_none,range = nano_corr_lowest_measure(ip: ip, port0: $port0, port1: $port1)

    if ((lowest_corr_none < 0) || (lowest_corr_none > max_corr))
        t_e("Unexpected correction field including egress delay. lowest_corr_none = #{lowest_corr_none}")
    end
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
    txt = t[:txt]
    txt = (txt.respond_to?(:call) ? txt.call(t) : txt)
    rep = fld_get(t, :rep, 1)
    rep.times do |i|
        test (rep == 1 ? txt : "#{txt} (#{i + 1}/#{rep})") do
            test_runner(t)
        end
    end
end

################################################
# Test Dump & Summary
################################################

test_summary()

test "dump" do
    #$ts.dut.run("mesa-cmd deb api ci ts")
end
