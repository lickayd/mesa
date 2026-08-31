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

check_capabilities() do
    $loop_ports = []
    [$ts.dut.looped_port_list, $ts.dut.looped_port_list_10g].each do |list|
        next if (list == nil) || ((list.length % 2) != 0)
        assert(dut_port_state_up(list), "Loop ports must be up")
        $loop_ports += list
    end
    t_i("*********$loop_ports #{$loop_ports}  #{$loop_ports.length}*********")
    assert((($loop_ports != nil) && (($loop_ports.length % 2) == 0)), "Number of looped front ports must be multiples of two")
end

$pcb = $ts.dut.pcb

$port0 = 0
$npi_port = 1
$cpu_queue = 7

$port_map = $ts.dut.call("mesa_port_map_get", cap_get("PORT_CNT"))

################################################
# Test Tables
################################################

test_table =
[
    {
        txt: "TOD Domain Shift (Config and Increment TOD)",
        fun: -> (t) {
            tod_domain_shift_test()
        }
    },
    {
        txt: "TOD Domain 0 configuration, PPS and SYNC timestamp check (seconds = 10)",
        fun: -> (t) {
            tod_domain_test(0, 10)
        }
    },
    {
        txt: "TOD Domain 1 configuration, PPS and SYNC timestamp check (seconds = 100)",
        fun: -> (t) {
            tod_domain_test(1, 100)
        }
    },
    {
        txt: "TOD Domain 2 configuration, PPS and SYNC timestamp check (seconds = 1000)",
        fun: -> (t) {
            tod_domain_test(2, 1000)
        }
    },
    {
        txt: "TOD default domain (bypass domain API) configuration, PPS and SYNC timestamp check (seconds = 0)",
        fun: -> (t) {
            tod_domain_test(3, 0)
        }
    },
]

################################################
# General/Helper Functions
################################################

def tod_domain_config_and_pps_check(domain, seconds, domain_def)
    # Port PTP domain configuration
    update_ts_operation_mode($ts.dut.port_list[$port0], { domain: domain })

    #Check that TOD in a domain can be configured and is incremented
    # Set TOD to 'second'
    $tod_ts = domain_def ? get_timeofday() : get_tod_domain(domain: domain)
    domain_def ? set_timeofday(sec: seconds, tod_ts: $tod_ts) : set_tod_domain(domain: domain, sec: seconds, tod_ts: $tod_ts)

    # Get TOD to check configuration
    $tod_ts = domain_def ? get_timeofday() : get_tod_domain(domain: domain)
    t_e("TOD in domain #{domain} was not configured as expected.  seconds = #{seconds}  tod_ts[seconds] = #{$tod_ts[0]["seconds"]}") if ($tod_ts[0]["seconds"] != seconds)

    # Get previous PPS TOD. Only default domain call possible - there is no domain interface for this !!!
    if (domain_def)
        set_timeofday(sec: seconds, nanosec: 100)
        prev_ts = get_timeofday_prev_pps()
        if (prev_ts["seconds"] != seconds) || (prev_ts["nanoseconds"] != 0)
            t_e("Previous TOD in domain #{domain} was not configured as expected.  seconds = #{seconds}  seconds = #{prev_ts["seconds"]}  nanoseconds = #{prev_ts["nanoseconds"]}")
        end
    end

    # Get next PPS TOD.
    domain_def ? set_timeofday(sec: seconds, tod_ts: $tod_ts) : set_tod_domain(domain: domain, sec: seconds, tod_ts: $tod_ts)
    next_ts = get_timeofday_next_pps(domain: (domain_def ? nil : domain))
    if (next_ts["seconds"] > (seconds + 2))   # It has been seen that two seconds has passed since mesa_ts_timeofday_set()
        t_e("next TOD in domain #{domain} was not configured as expected.  seconds = #{seconds}  next_ts[seconds] = #{next_ts["seconds"]}")
    end

    t_i("Sleep one second and check that TOD seconds is incremented - do a new TOD set first")
    domain_def ? set_timeofday(sec: seconds, tod_ts: $tod_ts) : set_tod_domain(domain: domain, sec: seconds, tod_ts: $tod_ts)
    sleep(1)


    $tod_ts = domain_def ? get_timeofday() : get_tod_domain(domain: domain)
    if ($tod_ts[0]["seconds"] != (seconds + 1))
        t_e("TOD in domain #{domain} is not incrementing as expected.  expected seconds = #{seconds+1}  tod_ts[seconds] = #{$tod_ts[0]["seconds"]}")
    end

    $tod_ts[0]["seconds"] = seconds
    $tod_ts[0]["nanoseconds"] = 0
end

def sync_frame_check(domain, action, rx_seconds)
    frameHdrTx = frame_create("00:02:03:04:05:06", "00:08:09:0a:0b:0c")
    frametx = tx_ifh_create($ts.dut.port_list[$port0], action, 0xFEFEFEFE0000, domain) + frameHdrTx.dup + sync_pdu_create()
    framerx = frameHdrTx.dup + sync_pdu_rx_create(IGNORE, rx_seconds)

    set_tod_domain_now(domain, $tod_ts)
    frame_cfg = { frame: frametx, port: $npi_port, frame0: framerx , port0: $port0, port1: nil, npi_port: $npi_port }
    frame_tx(frame_cfg)
end

def loop_two_step_sync_check(domain, domain_def)
    t_i("Test: Inject SYNC into NPI port to be transmitted on loop0 port and receive SYNC frame from NPI port")
    update_ts_operation_mode($loop_port0, { domain: domain })
    update_ts_operation_mode($loop_port1, { domain: domain })

    # Update default ingress and egress latency in the API. This is based on register values potentially different after link up
    # Delay after call to mesa_ts_status_change should be smaller as the default delay (that is added) is calculated internally in the API
    # This is only the case the first run of the test after boot as this default delay is remembered in the API
    $ts.dut.call("mesa_ts_status_change", $loop_port0)
    $ts.dut.call("mesa_ts_status_change", $loop_port1)

    # age out any allocated timestamps id's
    4.times {$ts.dut.call("mesa_timestamp_age")}

    # Allocate a timestamp id
    conf = {port_mask: 1<<$loop_port0, context: 0, cb: 0}
    idx0 = $ts.dut.call("mesa_tx_timestamp_idx_alloc", conf)    # Just to make sure that the test is working with idx ather than 0
    idx = $ts.dut.call("mesa_tx_timestamp_idx_alloc", conf)

    t_i("Transmit a Two-Step SYNC frame into NPI port with the allocated timestamp id on NPI against loop port and receive again on NPI port")
    frameHdrTx = frame_create("00:02:03:04:05:06", "00:08:09:0a:0b:0c")
    frametx = tx_ifh_create($loop_port0, "MESA_PACKET_PTP_ACTION_TWO_STEP", idx["ts_id"]<<16, domain) + frameHdrTx.dup + sync_pdu_create()
    framerx = rx_ifh_create($loop_port1) + frameHdrTx.dup + sync_pdu_rx_create()
    $tod_ts  = domain_def ? get_timeofday() : $ts.dut.call("mesa_ts_domain_timeofday_get", domain)
    frame_cfg = { frame: frametx, port: $npi_port, framenpi: framerx, capture_size: 60, port0: $port0, port1: nil, npi_port: $npi_port }
    frame_tx(frame_cfg)

    t_i("Calculate the IFH and decode it")
    pkts = $ts.pc.get_pcap "#{$ts.links[$npi_port][:pc]}.pcap"
    ifh = rx_ifh_extract(pkts[1])   # both transmitted and received frame is in 'pkts'
    meta = { no_wait: false, chip_no: 0, xtr_qu: 0, etype: 0, fcs: 0, sw_tstamp: { hw_cnt: 0 }, length: 0}
    $frame_info = $ts.dut.call("mesa_packet_rx_hdr_decode", meta, ifh)
    $hw_tstamp = $frame_info["hw_tstamp"] >> 16

    if ((domain == 0) && (cap_get("PHY_TS") != 0))    #Port with timestamping PHY is assumed by API to be in domain 0 always
        t_i("Get the frame RX tc based on a 32 bit ns counter from frame content inserted by timestamping PHY")
        tx_props = { ts_feature_is_PTS: true, phy_ts_mode: "MESA_PACKET_INTERNAL_TC_MODE_32BIT", backplane_port: false, delay_comp: {delay_cnt: 100<<16, asymmetry_cnt: 100<<16} }
        phy_ts = ($tod_ts[0]["seconds"] * 1_000_000_000) + $tod_ts[0]["nanoseconds"] # The TS inserted in frame by PHY is a 32 bit wrapping TOD nanoseconds. Current TOD TS is used as PHY TS
        timestamp = [((phy_ts & 0xFF000000) >> 24), ((phy_ts & 0xFF0000) >> 16), ((phy_ts & 0xFF00) >> 8), (phy_ts & 0xFF)]
        frame_ts = $ts.dut.call("mesa_ptp_get_timestamp", timestamp, $frame_info, "MESA_PACKET_PTP_MESSAGE_TYPE_SYNC", tx_props)
        rx_tc = frame_ts[0]

        if (rx_tc != $tod_ts[1])
            t_e("RX TC is not as expected.  rx_tc: #{rx_tc}  tod_ts[1]: #{$tod_ts[1]}")
        end
    end

    t_i("Update the TX FIFO in AIL. This will cause callback to Jason with the TX timestamp")
    $ts.dut.call("mesa_tx_timestamp_update")

    t_i("Get the TX timestamp. This is not a MESA API function, only a Jason implementation to get the TX timestamp delivered through callback")
    ts_tx = $ts.dut.call("mesa_tx_timestamp_get")
    if ((ts_tx["id"] != idx["ts_id"]) || (ts_tx["ts_valid"] != true))
        t_e("Not the expected TX timestamp. ts_tx[id] = #{ts_tx["id"]}  idx[ts_id] = #{idx["ts_id"]}  ts_tx[ts_valid] = #{ts_tx["ts_valid"]}")
    end
    $tx_tc = ts_tx["ts"] >> 16
end

def loop_calculated_rx_tc_check
    t_i("Test: Check the calculated received frame tc value")
    t_i("Get the delay and asymmetry compensated frame RX tc from frame_info")
    tx_props = { ts_feature_is_PTS: false, phy_ts_mode: "MESA_PACKET_INTERNAL_TC_MODE_32BIT", backplane_port: false, delay_comp: {delay_cnt: 100<<16, asymmetry_cnt: 100<<16} }
    rx_ts = $ts.dut.call("mesa_ptp_get_timestamp", [0,1,2,3], $frame_info, "MESA_PACKET_PTP_MESSAGE_TYPE_SYNC", tx_props)
    rx_tc = rx_ts[0] >> 16
    diff = rx_tc - $tx_tc
    t_i("tx_tc: #{$tx_tc}  hw_tstamp: #{$hw_tstamp}  rx_tc: #{rx_tc}  difference: #{diff}")

    # The loop cable is a 1 meter DAC that should give delay close to 4 nanoseconds.
    min = -200-2
    max = -200+18  #200ns is from delay_comp: {delay_cnt: 100<<16, asymmetry_cnt: 100<<16}
                   #Latency 17 is seen on Fireant Jenkins test
    if (cap_get("MISC_CHIP_FAMILY") == chip_family_to_id("MESA_CHIP_FAMILY_LAN966X"))
        if ($ts.dut.pcb == "8281-SVB")
            min = -200+480  #Copper SFP
            max = -200+520  #Copper SFP
        end
        if ($ts.dut.pcb == "8290")
            min = -200      #External PHY
            max = -200+75   #External PHY
        end
    end
    if (diff > max)
        t_e("Difference between TX TC and RX TC is unexpected high.  max: #{max}")
    end
    if (diff < min)
        t_e("Difference between TX TC and RX TC is unexpected low.  min: #{min}")
    end

    if (($hw_tstamp - (tx_props[:delay_comp][:delay_cnt]>>16) - (tx_props[:delay_comp][:asymmetry_cnt]>>16)) != rx_tc)  # Check that the calculated frame tc is correctly compensated
        t_e("Frame TC is not as expected. rx_tc: #{rx_tc}  hw_tstamp: #{$hw_tstamp}  expected rx_tc #{$hw_tstamp - (tx_props[:delay_comp][:delay_cnt]>>16) - (tx_props[:delay_comp][:asymmetry_cnt]>>16)}")
    end
end

def tod_domain_test(domain, seconds)
    t_i("Test: tod_domain_test  domain = #{domain}  seconds = #{seconds}")
    $data = ""
    $tod_ts = 0
    $tx_tc = 0
    $frame_info = ""

    #domain == 3 indicates use of default domain API
    domain_def = (domain == 3) ? true : false
    domain = 0 if domain_def    # The default domain is 0

    tod_domain_config_and_pps_check(domain, seconds, domain_def)
    t_i("Test: Inject SYNC frame with PTP action NONE AFI into NPI port and receive SYNC frame from front port and check the origin timestamp")
    sync_frame_check(domain, "MESA_PACKET_PTP_ACTION_AFI_NONE", 0)  # Frame should not be updated
    t_i("Test: Inject SYNC frame into NPI port and receive SYNC frame from front port and check the origin timestamp")
    sync_frame_check(domain, "MESA_PACKET_PTP_ACTION_ORIGIN_TIMESTAMP_SEQ", seconds)

    return if ($loop_port0 == nil)

    loop_two_step_sync_check(domain, domain_def)
    loop_calculated_rx_tc_check()
end

def get_timeofday
    $ts.dut.call("mesa_ts_timeofday_get")
end

def set_timeofday_now(tod_ts)
    $ts.dut.call("mesa_ts_timeofday_set", tod_ts[0])
    return tod_ts
end

def set_timeofday(sec: 0, nanosec: 0, tod_ts: nil)
    tod_ts = tod_ts || get_timeofday()
    tod_ts[0]["seconds"] = sec
    tod_ts[0]["nanoseconds"] = nanosec
    set_timeofday_now(tod_ts)
end

def update_ts_operation_mode(port, updates)
    conf = $ts.dut.call("mesa_ts_operation_mode_get", port)
    conf.merge!(updates)
    $ts.dut.call("mesa_ts_operation_mode_set", port, conf)
end

def get_timeofday_prev_pps(domain: nil)
    domain.nil? ? $ts.dut.call("mesa_ts_timeofday_prev_pps_get") : $ts.dut.call("mesa_ts_domain_timeofday_prev_pps_get", domain)
end

def get_timeofday_next_pps(domain: nil)
    domain.nil? ? $ts.dut.call("mesa_ts_timeofday_next_pps_get") : $ts.dut.call("mesa_ts_domain_timeofday_next_pps_get", domain)
end

def get_tod_domain(domain: 0)
    return $ts.dut.call("mesa_ts_domain_timeofday_get", domain)
end

def set_tod_domain_now(domain, tod_ts)
    $ts.dut.call("mesa_ts_domain_timeofday_set", domain, tod_ts[0])
    return tod_ts
end

def set_tod_domain(domain: 0, sec: 0, nanosec: 0, tod_ts: nil)
    tod_ts = tod_ts || get_tod_domain(domain: domain)
    tod_ts[0]["seconds"] = sec
    tod_ts[0]["nanoseconds"] = nanosec
    set_tod_domain_now(domain, tod_ts)
end

################################################
# Test Section
################################################

test "conf" do
    # disable VLAN 1 to avoid looping
    $ts.dut.call("mesa_vlan_port_members_set", 1, "")

    # CPU queue configuration
    conf = $ts.dut.call("mesa_packet_rx_conf_get")
    conf["queue"][$cpu_queue]["npi"]["enable"] = true
    $ts.dut.call("mesa_packet_rx_conf_set", conf)

    # NPI port configuration save
    conf = $ts.dut.call("mesa_npi_conf_get")
    conf["enable"] = true
    conf["port_no"] = $ts.dut.port_list[$npi_port]
    $ts.dut.call("mesa_npi_conf_set", conf)

    conf = $ts.dut.call("mesa_learn_port_mode_get", $ts.dut.port_list[$npi_port])
    conf["automatic"] = false
    $ts.dut.call("mesa_learn_port_mode_set", $ts.dut.port_list[$npi_port], conf)

    # CreateMAC address entry to copy frame to CPU
    entry = {
        vid_mac: { vid: 1, mac: { addr: [0x00,0x02,0x03,0x04,0x05,0x06] } },
        destination: "#{$loop_port1}",
        copy_to_cpu: true,
        copy_to_cpu_smac: false,
        locked: true,
        index_table: false,
        aged: false,
        cpu_queue: $cpu_queue,
    }
    $ts.dut.call("mesa_mac_table_add", entry)

    $ts.dut.run "mesa-cmd Debug Port Polling disable"

    i = 0
    while (i < $loop_ports.length) do
        if ((i % 2) != 0)
            port0 = $loop_ports[i-1]
            port1 = $loop_ports[i]
            i = i + 1
        else
            i = i + 1
            next
        end

        if ((port0 >= cap_get("PORT_CNT")) || (port1 >= cap_get("PORT_CNT")))
            next
        end

        cap = $ts.dut.run "mesa-cmd deb port cap #{port0+1}"
        if cap[:out].include?("SFP_ONLY")
            t_i("Prefer SFP port  port0 #{port0} port1 #{port1}")
            $loop_port0 = port0
            $loop_port1 = port1
            break;
        end
    end
end

def tod_domain_shift_test
    t_i("Test: tod_domain_shift_test")

    #Check that TOD in a domain can be configured and is incremented
    # Set TOD to 'second'
    set_tod_domain(domain: 0, sec: 1)
    set_tod_domain(domain: 1, sec: 10)
    set_tod_domain(domain: 2, sec: 100)

    tod_0 = $ts.dut.call("mesa_ts_domain_timeofday_get", 0)
    tod_1 = $ts.dut.call("mesa_ts_domain_timeofday_get", 1)
    tod_2 = $ts.dut.call("mesa_ts_domain_timeofday_get", 2)

    if ((tod_0[0]["seconds"] != 1) && (tod_0[0]["seconds"] != 2))
        t_e("TOD in domain 0 was not configured as expected.")
    end
    if ((tod_1[0]["seconds"] != 10) && (tod_1[0]["seconds"] != 11))
        t_e("TOD in domain 1 was not configured as expected.")
    end
    if ((tod_2[0]["seconds"] != 100) && (tod_2[0]["seconds"] != 101))
        t_e("TOD in domain 2 was not configured as expected.")
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

test "dump" do
    #$ts.dut.run("mesa-cmd deb api ci ts")
end
