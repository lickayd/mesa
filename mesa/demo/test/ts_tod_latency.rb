#!/usr/bin/env ruby

# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

require_relative 'libeasy/et'
require_relative 'ts_lib'

################################################
# Capability & Configuration
################################################

$ts = get_test_setup("mesa_pc_b2b_2x", {}, "", "loop")
$meba_cap = 0

# To change PCB to run 25G Do the following linux commands:
# ps
#               ps-id root     er -b -l /tmp/t_i-er -- mesa-demo -f
# kill ps-id
# fw_setenv pcb_var 9
# mesa-demo
# The NPI port cable must be moved to the responding 25G port
#conf = $ts.dut.call("mesa_port_conf_get", 2)
#if (chip_family_to_id("MESA_CHIP_FAMILY_SPARX5") && conf["speed"] == "MESA_SPEED_25G")
#    $ts.dut.looped_port_list.clear
#    $ts.dut.looped_port_list << 2
#    $ts.dut.looped_port_list << 3
#end

cfg = { cap_array: ["PACKET_TX_IFH_SIZE"], skip_on_fpga: true }
cap_check_ts(cfg)

check_capabilities do
    $loop_ports = []
    if (($ts.dut.looped_port_list != nil) && (($ts.dut.looped_port_list.length % 2) == 0))
        assert(dut_port_state_up($ts.dut.looped_port_list), "Loop ports must be up")
        $loop_ports += $ts.dut.looped_port_list
    end
    if (($ts.dut.looped_port_list_10g != nil) && (($ts.dut.looped_port_list_10g.length % 2) == 0))
        assert(dut_port_state_up($ts.dut.looped_port_list_10g), "Loop ports must be up")
        $loop_ports += $ts.dut.looped_port_list_10g
    end
    t_i("*********$loop_ports #{$loop_ports}  #{$loop_ports.length}*********")
    assert((($loop_ports != nil) && (($loop_ports.length % 2) == 0)),
           "Number of looped front ports must be multiples of two")
    if (cap_get("MISC_CHIP_FAMILY") == chip_family_to_id("MESA_CHIP_FAMILY_JAGUAR2"))
        assert(($ts.dut.looped_port_list_10g != nil) && ($ts.dut.looped_port_list_10g.length > 1),
            "On Jaguar2 two 10G front ports must be looped")
        $loop_port0_10g = $ts.dut.looped_port_list_10g[0]
        $loop_port1_10g = $ts.dut.looped_port_list_10g[1]
    end
end

$npi_port = 1
$cpu_queue = 7

$port_map = $ts.dut.call("mesa_port_map_get", cap_get("PORT_CNT"))
$misc_conf = $ts.dut.call("mesa_misc_get")
t_i("Core Clock Frequency #{$misc_conf["core_clock_freq"]}")
t_i("----------------------------------------------------")


PTP_LATENCY_MAX = 200

################################################
# Test Tables
################################################

test_table =
[
    {
        txt: "10G_FDX - NO FEC",
        fun: -> (t) {
            each_sfp_pair("10G_FDX") do |port0, port1|
                next unless port_mode_setup(port0, port1, "10g", "MESA_SPEED_10G")
                tod_latency_test(port0, port1, t[:txt])
            end
        }
    },
    {
        txt: "10G_FDX - KR aneg overrides forced 10G port mode",
        fun: -> (t) {
            each_sfp_pair("10G_FDX") do |port0, port1|
                next unless port_mode_setup(port0, port1, "10g", "MESA_SPEED_10G")
                kr_aneg_set(port0, port1, "all")
                kr_aneg_wait(port0, port1)
                kr_status_check(port0, port1)
                tod_latency_test(port0, port1, t[:txt])
                kr_teardown(port0, port1, "10g")
            end
        }
    },
    {
        txt: "10G_FDX - KR R-FEC",
        fun: -> (t) {
            each_sfp_pair("10G_FDX") do |port0, port1|
                next unless port_mode_setup(port0, port1, "10g", "MESA_SPEED_10G")
                kr_aneg_r_fec(port0, port1, "adv-10g")
                tod_latency_test(port0, port1, t[:txt])
                kr_teardown(port0, port1, "10g")
            end
        }
    },
    {
        txt: "5G_FDX",
        fun: -> (t) {
            each_sfp_pair(" 5G_FDX") do |port0, port1|
                port_mode_setup(port0, port1, "5g")
                tod_latency_test(port0, port1, t[:txt])
            end
        }
    },
    {
        txt: "2_5G_FDX",
        fun: -> (t) {
            each_sfp_pair("2_5G_FDX") do |port0, port1|
                port_mode_setup(port0, port1, "2500")
                tod_latency_test(port0, port1, t[:txt])
            end
        }
    },
    {
        txt: "1G_FDX",
        fun: -> (t) {
            each_sfp_pair("1G_FDX") do |port0, port1|
                port_mode_setup(port0, port1, "1000fdx")
                tod_latency_test(port0, port1, t[:txt])
            end
        }
    },
    {
        txt: "25G_FDX - NO FEC",
        fun: -> (t) {
            each_sfp_pair("25G_FDX") do |port0, port1|
                next unless port_mode_setup(port0, port1, "25g", "MESA_SPEED_25G")
                tod_latency_test(port0, port1, t[:txt])
            end
        }
    },
    {
        txt: "25G_FDX - KR RS-FEC",
        fun: -> (t) {
            each_sfp_pair("25G_FDX") do |port0, port1|
                next unless port_mode_setup(port0, port1, "25g", "MESA_SPEED_25G")
                kr_aneg_rs_fec(port0, port1, "adv-25g")
                tod_latency_test(port0, port1, t[:txt])
                kr_teardown(port0, port1, "25g")
            end
        }
    },
    {
        txt: "25G_FDX - KR R-FEC",
        fun: -> (t) {
            each_sfp_pair("25G_FDX") do |port0, port1|
                next unless port_mode_setup(port0, port1, "25g", "MESA_SPEED_25G")
                kr_aneg_r_fec(port0, port1, "adv-25g")
                tod_latency_test(port0, port1, t[:txt])
                kr_teardown(port0, port1, "25g")
            end
        }
    },
    {
        txt: "10G_FDX - Jaguar2 10G loop ports",
        fun: -> (t) {
            if (cap_get("MISC_CHIP_FAMILY") != chip_family_to_id("MESA_CHIP_FAMILY_JAGUAR2"))
                test_skip()
                return
            end

            port0 = $loop_port0_10g
            port1 = $loop_port1_10g
            $meba_cap = $ts.dut.run "mesa-cmd deb port cap #{port0+1}"
            unless $meba_cap[:out].include?("10G_FDX")
                test_skip()
                return
            end

            port_mode_setup(port0, port1, "10g")
            tod_latency_test(port0, port1, t[:txt])
        }
    },
]

################################################
# General/Helper Functions
################################################

# Yields every looped SFP port pair that supports 'cap_txt'. The test is skipped
# if the DUT has no such pair. Sets $meba_cap, which tod_latency_test reads.
def each_sfp_pair(cap_txt)
    found = false

    $loop_ports.each_slice(2) do |port0, port1|
        next if ((port0 >= cap_get("PORT_CNT")) || (port1 >= cap_get("PORT_CNT")))

        $meba_cap = $ts.dut.run "mesa-cmd deb port cap #{port0+1}"
        unless $meba_cap[:out].include?("SFP_ONLY")
            t_i("Port #{port0+1} is not a SFP port")
            next
        end
        next unless $meba_cap[:out].include?(cap_txt)

        t_i("*****Testing on loop ports #{port0+1} and #{port1+1}*****")
        found = true
        yield port0, port1
    end

    test_skip() unless found
end

# Wait for the link on both ports. This is the raw link, unlike
# dut_port_state_up() which also requires KR aneg to have completed.
def port_link_wait(ports, timeout = 20)
    ts = Time.now.to_i
    ports.each do |port|
        until $ts.dut.call("mesa_port_status_get", port)["link"]
            return false if ((Time.now.to_i - ts) > timeout)
            sleep 1
        end
    end
    true
end

# Switch both ports to 'mode'. When 'speed' is given, verify the switch took
# effect and return false if it did not.
def port_mode_set(port0, port1, mode, speed = nil)
    $ts.dut.run("mesa-cmd port mode #{port0+1} #{mode}")
    $ts.dut.run("mesa-cmd port mode #{port1+1} #{mode}")
    return true if speed.nil?

    conf = $ts.dut.call("mesa_port_conf_get", port0)
    return true if (conf["speed"] == speed)

    t_i("Port did not switch to #{mode}  port0 #{port0}  port1 #{port1}")
    false
end

# Bring the pair to 'mode' with the link up. Returns false if the port does not
# support 'mode'. KR is deliberately not touched here: disabling KR on a port that
# never ran it also clears the FEC config, which skews the TS latency defaults.
def port_mode_setup(port0, port1, mode, speed = nil)
    return false unless port_mode_set(port0, port1, mode, speed)

    t_e("Link did not come up in #{mode}  port0 #{port0}  port1 #{port1}") unless port_link_wait([port0, port1])
    true
end

# Teardown for a KR test: KR off, then back to a plain 'mode' link.
def kr_teardown(port0, port1, mode)
    kr_aneg_disable(port0, port1)
    port_mode_setup(port0, port1, mode)
end

def kr_aneg_wait(port0, port1, stage = "train")
    t_e("Link did not come up after KR aneg #{stage}  port0 #{port0}  port1 #{port1}") unless dut_port_state_up([port0, port1])
end

# Wait for KR aneg and training to complete on both ports, and log what was
# negotiated. Two consecutive reads must agree: the state machine can still be
# settling just after aneg completes, while a link that keeps retraining never
# gives two good reads in a row.
def kr_status_check(port0, port1, timeout = 20)
    [port0, port1].each do |port|
        ts = Time.now.to_i
        good = 0
        sts = $ts.dut.call("mesa_port_kr_status_get", port)
        until (good == 2)
            good = (sts["aneg"]["complete"] && sts["train"]["complete"]) ? (good + 1) : 0
            break if (good == 2)
            if ((Time.now.to_i - ts) > timeout)
                t_e("KR aneg/training did not complete  port #{port}  aneg #{sts["aneg"]["complete"]}  train #{sts["train"]["complete"]}")
                break
            end
            sleep 1
            sts = $ts.dut.call("mesa_port_kr_status_get", port)
        end
        conf = $ts.dut.call("mesa_port_conf_get", port)
        t_i("KR port #{port}  speed #{conf["speed"]}  r_fec #{sts["fec"]["r_fec_enable"]}  rs_fec #{sts["fec"]["rs_fec_enable"]}")
    end
end

# Both ends are configured in one command. Two commands leave a window where one
# end anegs against a partner that is not anegging yet.
def kr_aneg_set(port0, port1, args)
    $ts.dut.run("mesa-cmd Port KR aneg #{port0+1},#{port1+1} #{args}")
end

# RS-FEC (Clause 108) is 25G only, so 'adv' must advertise 25G.
def kr_aneg_rs_fec(port0, port1, adv)
    kr_aneg_set(port0, port1, "#{adv} rsfec train")
    kr_aneg_wait(port0, port1)
    kr_status_check(port0, port1)
end

# R-FEC is requested on a link that is already anegging, as the KR state machine
# does not train when it is enabled straight into a narrow advertisement.
def kr_aneg_r_fec(port0, port1, adv)
    kr_aneg_set(port0, port1, "all")
    kr_aneg_wait(port0, port1, "warm-up")
    kr_aneg_set(port0, port1, "#{adv} rfec train")
    kr_aneg_wait(port0, port1)
    kr_status_check(port0, port1)
end

def kr_aneg_disable(port0, port1)
    kr_aneg_set(port0, port1, "disable")
end

def nano_delay_measure(port0, port1)
    $nano_delay

    t_i("running nano_delay_measure")

    # Initialize TOD to zero
    tod_ts  = $ts.dut.call("mesa_ts_timeofday_get")
    tod_ts[0]["seconds"] = 0
    tod_ts[0]["nanoseconds"] = 0

    # age out any allocated timestamps id's
    4.times {$ts.dut.call("mesa_timestamp_age")}

    # Allocate a timestamp id
    conf = {port_mask: 1<<port0, context: 0, cb: 0}
    idx0 = $ts.dut.call("mesa_tx_timestamp_idx_alloc", conf)    # Just to make sure that the test is working with idx rather than 0
    idx = $ts.dut.call("mesa_tx_timestamp_idx_alloc", conf)

    t_i("transmit SYNC frame on NPI against loop port and receive again on NPI port")
    $ts.dut.run "mesa-cmd port statis clear"
    frameHdrTx = frame_create("00:02:03:04:05:06", "00:08:09:0a:0b:0c")
    frametx = tx_ifh_create(port0, "MESA_PACKET_PTP_ACTION_TWO_STEP", idx["ts_id"]<<16) + frameHdrTx.dup + sync_pdu_create()
    framerx = rx_ifh_create(port1) + frameHdrTx.dup + sync_pdu_rx_create()
    frame_cfg = { frame: frametx, port: $npi_port, framenpi: framerx, capture_size: 60, port0: nil, port1: nil, npi_port: $npi_port }
    frame_tx(frame_cfg)
    pkts = $ts.pc.get_pcap "#{$ts.links[$npi_port][:pc]}.pcap"

    if (pkts[1] == nil)
        t_e("get_pcap did not return any received frame")
        $ts.dut.run "mesa-cmd port statis #{port1+1}"
        return nil
    end

    t_i("Calculate the IFH and decode it")
    ifh = rx_ifh_extract(pkts[1])   # both transmitted and received frame is in 'pkts'
    meta = { no_wait: false, chip_no: 0, xtr_qu: 0, etype: 0, fcs: 0, sw_tstamp: { hw_cnt: 0 }, length: 0}
    frame_info = $ts.dut.call("mesa_packet_rx_hdr_decode", meta, ifh)

    t_i("Update the TX FIFO in AIL. This will cause callback to json with the TX timestamp")
    $ts.dut.call("mesa_tx_timestamp_update")

    t_i("Get the TX timestamp. This is not a MESA API function, only a json implementation to get the TX timestamp delivered through callback")
    ts_tx = $ts.dut.call("mesa_tx_timestamp_get")
    if ((ts_tx["id"] != idx["ts_id"]) || (ts_tx["ts_valid"] != true))
        t_e("Not the expected TX timestamp. ts_tx[id] = #{ts_tx["id"]}  idx[ts_id] = #{idx["ts_id"]}  ts_tx[ts_valid] = #{ts_tx["ts_valid"]}")
    end
    tod_nano_tx = ts_tx["ts"] / 65536.0

    #Calculate the RX TOD nanoseconds based on IFH RX tc
    tod_nano_rx = frame_info["hw_tstamp"] / 65536.0

    # Calculate the delay as the difference between RX and TX TOD nanoseconds
    $nano_delay = tod_nano_rx - tod_nano_tx
    t_i("nano_delay = #{$nano_delay}  tod_nano_tx = #{tod_nano_tx}  tod_nano_rx = #{tod_nano_rx}")

    return $nano_delay
end

def tx_two_step_sync(port0, port1)
    # This function is not in use, it was called like this:
    #8.times {
    #    tx_two_step_sync(port0, port1)
    #}
    #tx_fifo_print(port0)
    #exit 0
    $nano_delay

    t_i("tx_two_step_sync")

    # Allocate a timestamp id
    conf = {port_mask: 1<<port0, context: 0, cb: 0}
    idx = $ts.dut.call("mesa_tx_timestamp_idx_alloc", conf)

    t_i("transmit SYNC frame on NPI against loop port and receive again on NPI port")
    frameHdrTx = frame_create("00:02:03:04:05:06", "00:08:09:0a:0b:0c")
    frametx = tx_ifh_create(port0, "MESA_PACKET_PTP_ACTION_TWO_STEP", idx["ts_id"]<<16) + frameHdrTx.dup + sync_pdu_create()
    framerx = rx_ifh_create(port1) + frameHdrTx.dup + sync_pdu_rx_create()
    frame_cfg = { frame: frametx, port: $npi_port, port0: nil, port1: nil, npi_port: $npi_port }
    frame_tx(frame_cfg)
end

def tx_fifo_print(port0)
    # This function is not in use
    $ts.dut.run ("mesa-cmd deb sym write HSCH:SYSTEM:PORT_MODE[#{port0}] 0x00")
    sleep 1

    t_i("Update the TX FIFO in AIL. This will cause callback to json with the TX timestamp")
    $ts.dut.call("mesa_tx_timestamp_update")

    t_i("Get the TX timestamps. This is not a MESA API function, only a json implementation to get the TX timestamp delivered through callback")
    tod_nano_tx = []
    loop do
        ts_tx = $ts.dut.call("mesa_tx_timestamp_get")
        if (ts_tx["ts_valid"] != true)
            t_i("Not valid")
            break;
        end
        tod_nano_tx << (ts_tx["ts"] >> 16)
    end
    t_i("tod_nano_tx #{tod_nano_tx}")

    tod_nano_diff = []
    value = 0
    for i in tod_nano_tx do
        if value != 0
            tod_nano_diff << (i - value)
        end
        value = i
    end
    t_i("tod_nano_diff #{tod_nano_diff}")
end

def multiple_measure(port0, port1)
    # This function is not in use
    delays = []
    10.times {
        nano_delay = nano_delay_measure(port0, port1)

        delays << nano_delay
        $ts.dut.run ("mesa-cmd port state #{port0} disable")
        sleep 1
        $ts.dut.run ("mesa-cmd port state #{port0} enable")
        sleep 2
        $ts.dut.call("mesa_ts_status_change", port0)
        $ts.dut.call("mesa_ts_status_change", port1)
    }
    t_i("delays = #{delays}")

    delays_diff = []
    value = 0
    for i in delays do
        if value != 0
            delays_diff << (i - value)
        end
        value = i
    end
    t_i("delays_diff #{delays_diff}")
end

################################################
# Test Section
################################################

test "conf" do
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
end

def tod_latency_test(port0, port1, text)
    # Set Port PTP domain to 0 on loop ports
    conf = $ts.dut.call("mesa_ts_operation_mode_get", port0)
    conf["domain"] = 0
    $ts.dut.call("mesa_ts_operation_mode_set", port0, conf)
    conf = $ts.dut.call("mesa_ts_operation_mode_get", port1)
    conf["domain"] = 0
    $ts.dut.call("mesa_ts_operation_mode_set", port1, conf)

    # Set external clock mode to disable
    conf = $ts.dut.call("mesa_ts_external_clock_mode_get")
    conf["one_pps_mode"] = "MESA_TS_EXT_CLOCK_MODE_ONE_PPS_DISABLE"
    conf["enable"] = false
    conf["freq"] = 0
    $ts.dut.call("mesa_ts_external_clock_mode_set", conf)

    # Set Port egress latency to zero on loop TX port
    latency = $ts.dut.call("mesa_ts_egress_latency_get", port0)
    latency = 0
    $ts.dut.call("mesa_ts_egress_latency_set", port0, latency)

    # Set Port ingress latency to zero on loop RX port
    latency = $ts.dut.call("mesa_ts_ingress_latency_get", port1)
    latency = 0
    $ts.dut.call("mesa_ts_ingress_latency_set", port1, latency)

    # Update default ingress and egress latency in the API. This is based on register values potentially different after link up
    # Delay after call to mesa_ts_status_change should be smaller as the default delay (that is added) is calculated internally in the API
    # This is only the case the first run of the test after boot as this default delay is remembered in the API
    $ts.dut.call("mesa_ts_status_change", port0)
    $ts.dut.call("mesa_ts_status_change", port1)

    t_i "Measure nanosecond delay with egress latency 0 and ingress latency 0"
    nano_delay_0 = nano_delay_measure(port0, port1)
    # The loop cable is a 1 meter DAC that should give delay close to 4 nanoseconds.
    min = -2.1
    max = 18  #Value 17 is seen on Fireant Jenkins test
    if $meba_cap[:out].include?("COPPER")
        min = -11
    end
    if (cap_get("MISC_CHIP_FAMILY") == chip_family_to_id("MESA_CHIP_FAMILY_LAN966X"))
        min = -11
        max = 11
        if ($ts.dut.pcb == "8281-SVB")
            max = 520  #Copper SFP
        end
        if ($ts.dut.pcb == "8290")
            max = 90   #External PHY
        end
        if ($ts.dut.pcb == "8309")
            max = 15  #DAC cable
        end
    end

    if (!nano_delay_0.nil? && ((nano_delay_0 < min) || (nano_delay_0 > max)))
        t_e("#{text}  port0 #{port0}  port1 #{port1}")
        t_e("Unexpected delay with egress latency 0 and ingress latency 0.  Delay = #{nano_delay_0}  min #{min}  max #{max}")
    end

    # Set Port egress latency to max
    latency = PTP_LATENCY_MAX << 16
    $ts.dut.call("mesa_ts_egress_latency_set", port0, latency)

    t_i "Measure nanosecond delay with egress latency #{PTP_LATENCY_MAX} and ingress latency 0"
    nano_delay_1 = nano_delay_measure(port0, port1)
    diff = (nano_delay_0.nil? || nano_delay_1.nil?) ? nil : (nano_delay_0 - nano_delay_1)
    t_i("delay difference #{diff}")
    diff_tolerance = 9
    if (($ts.dut.pcb == "8281-SVB") || ($ts.dut.pcb == "8290"))
        diff_tolerance = 20
    end
    if (!diff.nil? && ((diff > (PTP_LATENCY_MAX + diff_tolerance)) || (diff < (PTP_LATENCY_MAX - diff_tolerance))))
        t_e("#{text}  port0 #{port0}  port1 #{port1}")
        t_e("Unexpected delay with egress latency #{PTP_LATENCY_MAX} and ingress latency 0.  Delay = #{nano_delay_1}  tolerance #{diff_tolerance}")
    end

    # Set Port ingress latency to max
    latency = PTP_LATENCY_MAX << 16
    $ts.dut.call("mesa_ts_ingress_latency_set", port1, latency)

    t_i "Measure nanosecond delay with egress latency #{PTP_LATENCY_MAX} and ingress latency #{PTP_LATENCY_MAX}"
    nano_delay_2 = nano_delay_measure(port0, port1)
    diff = (nano_delay_0.nil? || nano_delay_2.nil?) ? nil : (nano_delay_0 - nano_delay_2)
    t_i("delay difference #{diff}")
    if (!diff.nil? && ((diff > (PTP_LATENCY_MAX*2 + diff_tolerance)) || (diff < (PTP_LATENCY_MAX*2 - diff_tolerance))))
        t_e("#{text}  port0 #{port0}  port1 #{port1}")
        t_e("Unexpected delay with egress latency #{PTP_LATENCY_MAX} and ingress latency #{PTP_LATENCY_MAX}.  Delay = #{nano_delay_2}  tolerance #{diff_tolerance}")
    end

    t_i("#{text}  port0 #{port0}  port1 #{port1}")
    t_i("nano_delay_0 = #{nano_delay_0}  nano_delay_1 = #{nano_delay_1}  nano_delay_2 = #{nano_delay_2}  ")
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
