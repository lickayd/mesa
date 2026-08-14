#!/usr/bin/env ruby

# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

require_relative 'libeasy/et'
require_relative 'qos-lib.rb'

$ts = get_test_setup("mesa_pc_b2b_2x")

################################################
# Capability & Configuration
################################################

check_capabilities() do
    $dpl_cnt = $ts.dut.call("mesa_capability", "MESA_CAP_QOS_DPL_CNT")
    $gce_cnt = $ts.dut.call("mesa_capability", "MESA_CAP_QOS_TAS_GCE_CNT")
    $tas_support = $ts.dut.call("mesa_capability", "MESA_CAP_QOS_TAS")
    assert(($tas_support == 1), "TAS not supported on this platform")
    if (($ts.dut.looped_port_list != nil) && ($ts.dut.looped_port_list.length > 1))
        loop_pair_check()
        $loop_port0 = $ts.dut.looped_port_list[0]
        $loop_port1 = $ts.dut.looped_port_list[1]
    else
        assert(false, "Two front ports must be looped")
    end
    $cap_family = $ts.dut.call("mesa_capability", "MESA_CAP_MISC_CHIP_FAMILY")
end

MESA_VID_NULL = 0
# A TAS gate is only the rate limiting factor when the port shaper can supply a
# full gate open interval from banked burst credit. Tests that measure a single
# priority behind a gate therefore raise the burst level on the ports they measure.
TAS_SHAPER_BURST_LEVEL = 36000
# All families program the port shaper burst level in whole 4 kB units, capped at
# 63 units, so a shaper can bank slightly more credit than the level asked for.
SHAPER_BURST_UNIT = 4096
SHAPER_BURST_UNIT_CNT_MAX = 63
# Max allowed spread between the number of frames transmitted from each equally gated prio
TAS_EQUAL_PRIO_TX_PERCENT = 2
$eg = 0
$ig = [1, 2, 3]

t_i("Test call of TAS global configuration")
$conf = $ts.dut.call("mesa_qos_tas_conf_get")
$conf["always_guard_band"] = false
$conf = $ts.dut.call("mesa_qos_tas_conf_set", $conf)

################################################
# Test Tables
################################################

test_table =
[
    {
        txt: "TAS against PTP Time Jumps - Stop",
        fun: -> (t) {
            dut_four_ports() ? time_jump_test($eg, $ig, 1, true) : test_skip()
        }
    },
    {
        txt: "TAS against PTP Time Jumps - Restart",
        fun: -> (t) {
            dut_four_ports() ? time_jump_test($eg, $ig, 2, false) : test_skip()
        }
    },
    {
        txt: "Frames Per Cycle Accuracy",
        fun: -> (t) {
            frames_per_cycle_accuracy_test()
        }
    },
    {
        txt: "Disable Pending Instance",
        fun: -> (t) {
            disable_pending_instance()
        }
    },
    {
        txt: "Max SDU Size Bigger than Frame Size",
        fun: -> (t) {
            ($cap_family == chip_family_to_id("MESA_CHIP_FAMILY_LAN966X")) ? max_sdu_bigger_than_frame_size() : test_skip()
        }
    },
    {
        txt: "Change config after setting them to minimum cycle time",
        fun: -> (t) {
            change_config_after_minimum_cycle_time()
        }
    },
    {
        txt: "Frame Preemption impact on guard band",
        fun: -> (t) {
            (dut_looped_ports() && ($cap_family != chip_family_to_id("MESA_CHIP_FAMILY_SPARX5"))) ? frame_preemption_on_guard_band() : test_skip()
        }
    },
    {
        txt: "No queue block when disabling TAS with FP enabled",
        fun: -> (t) {
            dut_looped_ports() ? no_queue_block_test() : test_skip()
        }
    },
    {
        txt: "QOS TAS Equal Interval 3 Prio 1 Port Test",
        fun: -> (t) {
            dut_four_ports() ? qos_tas_equal_interval_3_prio_1_port_test($eg, $ig) : test_skip()
        }
    },
    {
        txt: "Equal Interval 1 Prio 3 Port Test",
        fun: -> (t) {
            dut_four_ports() ? equal_interval_1_prio_3_port_test() : test_skip()
        }
    },
    {
        txt: "Equal Interval GCL Reconfig Test",
        fun: -> (t) {
            dut_four_ports() ? equal_interval_gcl_reconfig_test() : test_skip()
        }
    },
    {
        txt: "TAS in Domain 1 Test",
        fun: -> (t) {
            dut_four_ports() ? tas_in_domain_1_test($eg, $ig) : test_skip()
        }
    },
]

################################################
# General/Helper Functions
################################################

def dut_four_ports
    return ($ts.dut.port_list.length == 4)
end

def dut_looped_ports
    return (($ts.dut.looped_port_list != nil) && ($ts.dut.looped_port_list.length > 1))
end

def set_tsn_domain(domain)
    t_i("Set the TAS TOD Domain to #{domain}")
    ts_conf = $ts.dut.call("mesa_ts_conf_get")
    ts_conf["tsn_domain"] = domain
    $ts.dut.call("mesa_ts_conf_set", ts_conf)
end

def tas_port_list
    port_list = $ts.dut.port_list
    port_list = port_list + $ts.dut.looped_port_list if (dut_looped_ports())
    return port_list
end

# Expected egress rate for a single priority gated open for interval of every
# cycle, limited by the shaper on port unless it can supply a full interval.
def gate_erate_get(port, interval, cycle)
    shaper = $ts.dut.call("mesa_qos_port_conf_get", port)["shaper"]
    burst_cnt = [(shaper["level"] + SHAPER_BURST_UNIT - 1) / SHAPER_BURST_UNIT, SHAPER_BURST_UNIT_CNT_MAX].min
    shaper_bits = (burst_cnt * SHAPER_BURST_UNIT * 8) + ((shaper["rate"] * 1000 * interval) / 1_000_000_000)
    line_bits = interval    # One bit takes one nano sec to transmit at 1G
    return (([shaper_bits, line_bits].min * 1_000_000_000) / cycle)
end

# The gate opens for the same interval per prio, so the DUT must transmit the same number of frames from each.
# Reads the DUT counters, an even split here with an uneven pcap means the capture lost frames
def check_equal_prio_tx(counters, eg, prio_list)
    return if (counters == nil)
    tx = prio_list.map {|prio| counters[:prio_tx][eg].fetch(prio, 0)}
    return if (tx.min <= 0)     # No prio counters on this family, nothing to compare

    spread = ((tx.max - tx.min) * 100.0) / tx.max
    t_i("Egress port #{eg} prio #{prio_list} transmitted #{tx}. Spread #{'%.2f' % spread} %")
    if (spread > TAS_EQUAL_PRIO_TX_PERCENT)
        t_e("Uneven prio tx from the gate. Spread #{'%.2f' % spread} % is above #{TAS_EQUAL_PRIO_TX_PERCENT} %  tx #{tx}")
    end
end

def tas_reset
    stopped = []
    tas_port_list().each do |port|
        conf = $ts.dut.call("mesa_qos_tas_port_conf_get", port)
        next if ((conf["gate_enabled"] == false) && (conf["config_change"] == false))
        t_i("TAS reset: stop gate on port #{port}")
        conf["gate_enabled"] = false
        conf["config_change"] = false
        $ts.dut.call("mesa_qos_tas_port_conf_set", port, conf)
        stopped << port
    end

    if (stopped.length > 0)
        sleep 2
        stopped.each do |port|
            status = $ts.dut.call("mesa_qos_tas_port_status_get", port)
            t_e("TAS reset: gate not stopped on port #{port}") if (status["config_pending"] == true)
        end
    end

    if (cap_get("QOS_FRAME_PREEMPTION") > 0)
        tas_port_list().each do |port|
            fp = $ts.dut.call("mesa_qos_fp_port_conf_get", port)
            next if ((fp["enable_tx"] == false) && (fp["admin_status"].include?(true) == false))
            t_i("TAS reset: disable frame preemption on port #{port}")
            fp["admin_status"].each_index {|i| fp["admin_status"][i] = false}
            fp["enable_tx"] = false
            $ts.dut.call("mesa_qos_fp_port_conf_set", port, fp)
        end
    end

    set_tsn_domain(0)
rescue => e
    t_e("TAS reset failed: #{e}")
end

def port_shaper_set(port_list, rate, level)
    t_i("Set port shaper on #{port_list} to rate #{rate} level #{level}")
    previous = {}
    port_list.each do |port|
        conf = $ts.dut.call("mesa_qos_port_conf_get", port)
        previous[port] = [conf["shaper"]["rate"], conf["shaper"]["level"]]
        conf["shaper"]["rate"] = rate
        conf["shaper"]["level"] = level
        $ts.dut.call("mesa_qos_port_conf_set", port, conf)
    end
    return previous
end

def port_shaper_restore(previous)
    previous.each do |port, shaper|
        conf = $ts.dut.call("mesa_qos_port_conf_get", port)
        conf["shaper"]["rate"] = shaper[0]
        conf["shaper"]["level"] = shaper[1]
        $ts.dut.call("mesa_qos_port_conf_set", port, conf)
    end
end

def start_gcl(conf, eg, ig, frame_size, cycle_time, domain)
    t_i("*************Start GCL")
    tod = $ts.dut.call("mesa_ts_domain_timeofday_get", domain)
    conf["base_time"]["nanoseconds"] = 0
    conf["base_time"]["seconds"] = tod[0]["seconds"] + 4
    conf["gate_enabled"] = true
    conf["config_change"] = true
    $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)

    base_time_seconds = conf["base_time"]["seconds"]

    t_i("Check GCL is pending")
    status = $ts.dut.call("mesa_qos_tas_port_status_get", $ts.dut.p[eg])
    t_e("GCL unexpected config_pending = #{status["config_pending"]}") if (status["config_pending"] != true)

    t_i("Wait for GCL to start")
    sleep 5

    t_i("Check GCL is started")
    status = $ts.dut.call("mesa_qos_tas_port_status_get", $ts.dut.p[eg])
    t_e("GCL unexpected config_pending = #{status["config_pending"]}") if (status["config_pending"] == true)

    $ts.dut.run("mesa-cmd mac flush")
    $ts.pc.run("sudo ef tx #{$ts.pc.p[eg]} eth dmac 00:00:00:00:01:02 smac 00:00:00:00:01:01 ipv4 dscp 0")
    erate = (DUT_TEST_RATE*1000)/3
    counters = check_rate({
        ig: ig, eg: eg, size: frame_size, sec: 2, erate: [erate,erate,erate], etolerance: [5,5,5],
        with_pre_tx: true, pcp: [0,3,7], cycle_time: [cycle_time,cycle_time,cycle_time]
    })
    check_equal_prio_tx(counters, eg, [0,3,7])

    return base_time_seconds
end

def stop_gcl(conf, eg)
    t_i("*************Stop GCL")
    conf["gate_enabled"] = false
    conf["config_change"] = false
    $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)

    t_i("Wait for GCL to stop")
    sleep 2

    t_i("Check GCL is stopped")
    status = $ts.dut.call("mesa_qos_tas_port_status_get", $ts.dut.p[eg])
    t_e("GCL unexpected config_pending = #{status["config_pending"]}") if (status["config_pending"] == true)
end

################################################
# Test Section
################################################

test "conf" do
    t_i("Testing correct config")
    if ($ts.dut.port_list.length == 4)
        t_i("Only forward on relevant ports ")
        port_list = "#{$ts.dut.port_list[0]},#{$ts.dut.port_list[1]},#{$ts.dut.port_list[2]},#{$ts.dut.port_list[3]}"
        $ts.dut.call("mesa_vlan_port_members_set", 1, port_list)
    end

    port_list = $ts.dut.port_list
    if (($ts.dut.looped_port_list != nil) && ($ts.dut.looped_port_list.length > 1))
        port_list = port_list + $ts.dut.looped_port_list
    end
    t_i("Configure all ports to C tag aware.  port_list #{port_list}")
    port_list.each do |i|
        t_i("Configure all ports to disable flow control")
        $ts.dut.run("mesa-cmd port flow control #{i+1} disable")
        $ts.dut.run("mesa-cmd port mode #{i+1} 1000fdx")

        t_i("Configure all ports to C tag aware")
        conf = $ts.dut.call("mesa_vlan_port_conf_get", i)
        conf["port_type"] = "MESA_VLAN_PORT_TYPE_C"
        conf["untagged_vid"] = MESA_VID_NULL
        $ts.dut.call("mesa_vlan_port_conf_set", i, conf)

        t_i("Configure ingress prio classification and prio and dpl mapping to 1:1")
        t_i("Configure egress prio and dpl tagging to mapped. Also enable port shaper to assure queues are never emptied")
        conf = $ts.dut.call("mesa_qos_port_conf_get", i)
        conf["tag"]["class_enable"] = true
        conf["tag"]["remark_mode"] = "MESA_TAG_REMARK_MODE_MAPPED"
        conf["tag"]["pcp_dei_map"][0][0]["prio"] = 0
        conf["tag"]["pcp_dei_map"][0][0]["dpl"] = 0
        conf["tag"]["pcp_dei_map"][0][1]["prio"] = 0
        conf["tag"]["pcp_dei_map"][0][1]["dpl"] = 1
        conf["tag"]["pcp_dei_map"][1][0]["prio"] = 1
        conf["tag"]["pcp_dei_map"][1][0]["dpl"] = 0
        conf["tag"]["pcp_dei_map"][1][1]["prio"] = 1
        conf["tag"]["pcp_dei_map"][1][1]["dpl"] = 1
        conf["default_prio"] = (i < 8) ? i : 7
        conf["default_dpl"] = 0
        conf["shaper"]["level"] = 10000    # Shaper must have "large" burst size level in order to shape correctly at "high" rates
        conf["shaper"]["rate"] = DUT_TEST_RATE
        conf["shaper"]["mode"] = "MESA_SHAPER_MODE_LINE"
        $ts.dut.call("mesa_qos_port_conf_set", i, conf)

        t_i("Configure egress prio and dpl mapping to 1:1")
        dconf = $ts.dut.call("mesa_qos_port_dpl_conf_get", i, $dpl_cnt)
        dconf[0]["pcp"] = [0,1,2,3,4,5,6,7]
        dconf[0]["dei"] = [0,0,0,0,0,0,0,0]
        dconf[1]["pcp"] = [0,1,2,3,4,5,6,7]
        dconf[1]["dei"] = [1,1,1,1,1,1,1,1]
        $ts.dut.call("mesa_qos_port_dpl_conf_set", i, $dpl_cnt, dconf)
    end
    sleep 5
    dut_port_state_up(port_list)
end

def time_jump_test(eg, ig, domain, stop_before_restart)
    # Based on JIRA MESA-977
    frame_size = 500
    frame_tx_time_nano = (frame_size+20)*8    # One bit takes one nano sec to transmit at 1G
    frame_tx_interval_count = 1000            # Number of frame transmitted in a GCL entry time interval
    time_interval = frame_tx_interval_count * frame_tx_time_nano
    cycle_time = 3*time_interval

    set_tsn_domain(domain)

    t_i("Create GCL")
    gcl = [{"gate_operation":"MESA_QOS_TAS_GCO_SET_GATE_STATES",
            "gate_open":[true,false,false,false,false,false,false,false],
            "time_interval":time_interval},
           {"gate_operation":"MESA_QOS_TAS_GCO_SET_GATE_STATES",
            "gate_open":[false,false,false,true,false,false,false,false],
            "time_interval":time_interval},
           {"gate_operation":"MESA_QOS_TAS_GCO_SET_GATE_STATES",
            "gate_open":[false,false,false,false,false,false,false,true],
            "time_interval":time_interval}]

    $ts.dut.call("mesa_qos_tas_port_gcl_conf_set", $ts.dut.p[eg], 3, gcl)

    t_i("Get TOD of domain #{domain}")
    tod = $ts.dut.call("mesa_ts_domain_timeofday_get", domain)
    tod[0]["seconds"] = 100_000
    tod[0]["nanoseconds"] = 555_555
    $ts.dut.call("mesa_ts_domain_timeofday_set", domain, tod[0])

    conf = $ts.dut.call("mesa_qos_tas_port_conf_get", $ts.dut.p[eg])
    conf["max_sdu"][0] = frame_size + (frame_size/2)
    conf["max_sdu"][3] = frame_size + (frame_size/2)
    conf["max_sdu"][7] = frame_size + (frame_size/2)
    conf["ot"] = false
    conf["gate_open"].each_index {|i| conf["gate_open"][i] = true}
    conf["cycle_time"] = cycle_time
    conf["cycle_time_ext"] = 256
    conf["base_time"]["sec_msb"] = 0

    base_time_seconds = start_gcl(conf, eg, ig, frame_size, cycle_time, domain)

    time_jumps =
    [
        {txt: "back in time before current base time",               pre_sleep: 0,  sec: -> (base, now) {base - 10}},
        {txt: "back in time after base time but before current TOD", pre_sleep: 10, sec: -> (base, now) {base + 5}},
        {txt: "after current TOD",                                   pre_sleep: 1,  sec: -> (base, now) {now + 10}},
    ]

    time_jumps.each do |jump|
        sleep jump[:pre_sleep]

        t_i("*************Set TOD #{jump[:txt]}")
        tod = $ts.dut.call("mesa_ts_domain_timeofday_get", domain)
        tod[0]["seconds"] = jump[:sec].call(base_time_seconds, tod[0]["seconds"])
        $ts.dut.call("mesa_ts_domain_timeofday_set", domain, tod[0])

        sleep 1

        stop_gcl(conf, eg) if (stop_before_restart)

        base_time_seconds = start_gcl(conf, eg, ig, frame_size, cycle_time, domain)
    end

    stop_gcl(conf, eg)
end

def frames_per_cycle_accuracy_test
    # Based on JIRA MESA-1013
    eg = 0
    ig = [1]
    frame_size = 1230
    max_sdu = 1250
    cycle_time = 1000000
    # In each cycle 10 frames are transmitted.
    # One cycle is 1 us, so in one second 10.000 frames is transmitted
    frame_rate = 10000

    set_tsn_domain(0)

    t_i("Create GCL")
    gcl = [{"gate_operation":"MESA_QOS_TAS_GCO_SET_GATE_STATES",
            "gate_open":[false,false,false,false,false,false,false,true],
            "time_interval":900000},
           {"gate_operation":"MESA_QOS_TAS_GCO_SET_GATE_STATES",
            "gate_open":[true,true,true,true,true,true,true,false],
            "time_interval":100000}]

    $ts.dut.call("mesa_qos_tas_port_gcl_conf_set", $ts.dut.p[eg], 2, gcl)

    t_i("Get TOD of domain 0")
    tod = $ts.dut.call("mesa_ts_timeofday_get")
    tod[0]["seconds"] = 0
    tod[0]["nanoseconds"] = 0
    $ts.dut.call("mesa_ts_timeofday_set", tod[0])

    t_i("Start GCL")
    conf = $ts.dut.call("mesa_qos_tas_port_conf_get", $ts.dut.p[eg])
    conf["max_sdu"].each_index {|i| conf["max_sdu"][i] = max_sdu}
    conf["gate_enabled"] = true
    conf["gate_open"].each_index {|i| conf["gate_open"][i] = true}
    conf["cycle_time"] = cycle_time
    conf["cycle_time_ext"] = 256
    conf["base_time"]["nanoseconds"] = 0
    conf["base_time"]["seconds"] = 4
    conf["base_time"]["sec_msb"] = 0
    conf["gate_enabled"] = true
    conf["config_change"] = true
    $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)

    t_i("Check GCL is pending")
    conf = $ts.dut.call("mesa_qos_tas_port_status_get", $ts.dut.p[eg])
    if (conf["config_pending"] != true)
        t_e("GCL unexpected config_pending = #{conf["config_pending"]}")
    end

    t_i("Wait for GCL to start")
    sleep 5

    t_i("Check GCL is started")
    conf = $ts.dut.call("mesa_qos_tas_port_status_get", $ts.dut.p[eg])
    if (conf["config_pending"] == true)
        t_e("GCL unexpected config_pending = #{conf["config_pending"]}")
    end

    $ts.dut.run("mesa-cmd mac flush")
    $ts.pc.run("sudo ef tx #{$ts.pc.p[eg]} eth dmac 00:00:00:00:01:02 smac 00:00:00:00:01:01 ctag vid 0 ipv4 dscp 0")

    check_rate({
        ig: ig, eg: eg, size: frame_size, sec: 2, frame_rate: true, erate: [frame_rate], etolerance: [5],
        with_pre_tx: true, pcp: [4]
    })

    t_i("Stop GCL")
    conf = $ts.dut.call("mesa_qos_tas_port_conf_get", $ts.dut.p[eg])
    conf["gate_enabled"] = false
    conf["config_change"] = false
    conf = $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)

    t_i("Wait for GCL to stop")
    sleep 2

    t_i("Check GCL is stopped")
    conf = $ts.dut.call("mesa_qos_tas_port_status_get", $ts.dut.p[eg])
    if (conf["config_pending"] == true)
        t_e("GCL unexpected config_pending = #{conf["config_pending"]}")
    end
end

def disable_pending_instance
    # Based on JIRA MESA-899
    eg = 0
    ig = [1]

    set_tsn_domain(0)

    t_i("Stop GCL even though no is active")
    conf = $ts.dut.call("mesa_qos_tas_port_conf_get", $ts.dut.p[eg])
    conf["gate_enabled"] = false
    conf["config_change"] = false
    $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)

    t_i("Wait for GCL to stop")
    sleep 2

    t_i("Create GCL gce_cnt #{$gce_cnt}")
    gcl = [{"gate_operation":"MESA_QOS_TAS_GCO_SET_GATE_STATES",
            "gate_open":[true,true,true,true,true,true,true,true],
            "time_interval":10000},
           {"gate_operation":"MESA_QOS_TAS_GCO_SET_GATE_STATES",
            "gate_open":[true,true,true,true,true,true,true,true],
            "time_interval":1000}]
    $ts.dut.call("mesa_qos_tas_port_gcl_conf_set", $ts.dut.p[eg], 2, gcl)

    t_i("Get TOD of domain 0")
    tod = $ts.dut.call("mesa_ts_timeofday_get")
    tod[0]["seconds"] = 0
    tod[0]["nanoseconds"] = 0
    $ts.dut.call("mesa_ts_timeofday_set", tod[0])

    t_i("Start GCL")
    conf = $ts.dut.call("mesa_qos_tas_port_conf_get", $ts.dut.p[eg])
    conf["gate_enabled"] = true
    conf["gate_open"].each_index {|i| conf["gate_open"][i] = true}
    conf["cycle_time"] = 20000
    conf["cycle_time_ext"] = 256
    conf["base_time"]["nanoseconds"] = 0
    conf["base_time"]["seconds"] = 3
    conf["base_time"]["sec_msb"] = 0
    conf["gate_enabled"] = true
    conf["config_change"] = true
    $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)

    pending = true
    for i in 1..60
        status = $ts.dut.call("mesa_qos_tas_port_status_get", $ts.dut.p[eg])
        if (status["config_pending"] == false)
            pending = false
            break;
        end
        sleep 0.1
    end
    if (pending == true)
        t_e("Pending never cleared after stop GCL")
    end
    tod = $ts.dut.call("mesa_ts_timeofday_get")
    if (tod[0]["seconds"] != 3)
        t_e ("TOD is not as expected #{tod[0]["seconds"]}")
    end

    t_i("Start new GCL to take over")
    conf["base_time"]["seconds"] = 5
    # This is the gap between the two lists but it is less than cycle_time_ext so no extension happens
    conf["base_time"]["nanoseconds"] = 150
    $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)

    pending = true
    for i in 1..60
        status = $ts.dut.call("mesa_qos_tas_port_status_get", $ts.dut.p[eg])
        if (status["config_pending"] == false)
            pending = false
            break;
        end
        sleep 0.1
    end
    if (pending == true)
        t_e("Pending never cleared after stop GCL")
    end
    tod = $ts.dut.call("mesa_ts_timeofday_get")
    if (tod[0]["seconds"] != 5)
        t_e ("TOD is not as expected #{tod[0]["seconds"]}")
    end

    t_i("Stop GCL")
    conf = $ts.dut.call("mesa_qos_tas_port_conf_get", $ts.dut.p[eg])
    conf["gate_enabled"] = false
    conf["config_change"] = false
    $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)

    pending = true
    for i in 1..60
        status = $ts.dut.call("mesa_qos_tas_port_status_get", $ts.dut.p[eg])
        if (status["config_pending"] == false)
            pending = false
            break;
        end
        sleep 0.1
    end
    if (pending == true)
        t_e("Pending never cleared after stop GCL")
    end

    $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)
end

def max_sdu_bigger_than_frame_size
    # Based on JIRA MESA-898
    eg = 0
    ig = [1]

    frame_size = 500
    frame_tx_time_nano = (frame_size+20)*8    # One bit takes one nano sec to transmit at 1G
    frame_tx_interval_count = 1000            # Number of frame transmitted in a GCL entry time interval
    time_interval = frame_tx_interval_count * frame_tx_time_nano
    cycle_time = 3*time_interval

    set_tsn_domain(0)

    t_i("Create GCL")
    gcl = [{"gate_operation":"MESA_QOS_TAS_GCO_SET_GATE_STATES",
            "gate_open":[true,false,false,false,false,false,false,false],
            "time_interval":time_interval},
           {"gate_operation":"MESA_QOS_TAS_GCO_SET_GATE_STATES",
            "gate_open":[false,false,false,true,false,false,false,false],
            "time_interval":time_interval},
           {"gate_operation":"MESA_QOS_TAS_GCO_SET_GATE_STATES",
            "gate_open":[false,false,false,false,false,false,false,true],
            "time_interval":time_interval}]
    $ts.dut.call("mesa_qos_tas_port_gcl_conf_set", $ts.dut.p[eg], 3, gcl)

    t_i("Get TOD of domain 0")
    tod = $ts.dut.call("mesa_ts_timeofday_get")
    tod[0]["seconds"] = 0
    tod[0]["nanoseconds"] = 0
    $ts.dut.call("mesa_ts_timeofday_set", tod[0])

    t_i("Start GCL with max SDU size smaller than the frame size")
    conf = $ts.dut.call("mesa_qos_tas_port_conf_get", $ts.dut.p[eg])
    conf["max_sdu"][0] = frame_size - 50
    conf["max_sdu"][3] = frame_size - 50
    conf["max_sdu"][7] = frame_size - 50
    conf["gate_enabled"] = true
    conf["gate_open"].each_index {|i| conf["gate_open"][i] = true}
    conf["cycle_time"] = cycle_time
    conf["cycle_time_ext"] = 256
    conf["base_time"]["nanoseconds"] = 0
    conf["base_time"]["seconds"] = 4
    conf["base_time"]["sec_msb"] = 0
    conf["config_change"] = true
    $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)

    t_i("Wait for GCL to start")
    sleep 5

    $ts.dut.run("mesa-cmd mac flush")
    $ts.pc.run("sudo ef tx #{$ts.pc.p[eg]} eth dmac 00:00:00:00:01:02 smac 00:00:00:00:01:01 ipv4 dscp 0")
    t_i "Measure that no frames are transmitted due to frame size too big"
    erate = 0
    check_rate({
        ig: ig, eg: eg, size: frame_size, sec: 2, erate: [erate], etolerance: [5], with_pre_tx: true,
        pcp: [3], cycle_time: [cycle_time]
    })

    t_i("Stop GCL")
    conf = $ts.dut.call("mesa_qos_tas_port_conf_get", $ts.dut.p[eg])
    conf["gate_enabled"] = false
    conf["config_change"] = false
    conf = $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)

    t_i("Wait for GCL to stop")
    sleep 2

    t_i("Get TOD of domain 0")
    tod = $ts.dut.call("mesa_ts_timeofday_get")

    t_i("Start GCL with max SDU size bigger than the frame size")
    conf = $ts.dut.call("mesa_qos_tas_port_conf_get", $ts.dut.p[eg])
    conf["max_sdu"][0] = frame_size + (frame_size/2)
    conf["max_sdu"][3] = frame_size + (frame_size/2)
    conf["max_sdu"][7] = frame_size + (frame_size/2)
    conf["gate_enabled"] = true
    conf["base_time"]["seconds"] = tod[0]["seconds"] + 4
    conf["config_change"] = true
    $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)

    t_i("Wait for GCL to start")
    sleep 5

    $ts.dut.run("mesa-cmd mac flush")
    $ts.pc.run("sudo ef tx #{$ts.pc.p[eg]} eth dmac 00:00:00:00:01:02 smac 00:00:00:00:01:01 ipv4 dscp 0")
    # pcp 3 only owns one of the three intervals, so the shaper is idle two thirds of the cycle
    erate = gate_erate_get($ts.dut.p[eg], time_interval, cycle_time)
    check_rate({
        ig: ig, eg: eg, size: frame_size, sec: 2, erate: [erate], etolerance: [5], with_pre_tx: true,
        pcp: [3], cycle_time: [cycle_time]
    })

    t_i("Stop GCL")
    conf = $ts.dut.call("mesa_qos_tas_port_conf_get", $ts.dut.p[eg])
    conf["gate_enabled"] = false
    conf["config_change"] = false
    conf = $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)
end

def change_config_after_minimum_cycle_time
    # Based on JIRA APPL-3396
    eg = 0

    set_tsn_domain(0)

    t_i("Create GCL")
    gcl = [{"gate_operation":"MESA_QOS_TAS_GCO_SET_GATE_STATES",
            "gate_open":[true,true,true,true,false,false,false,false],
            "time_interval":128},
           {"gate_operation":"MESA_QOS_TAS_GCO_SET_GATE_STATES",
            "gate_open":[false,false,false,false,true,true,true,true],
            "time_interval":128}]
    $ts.dut.call("mesa_qos_tas_port_gcl_conf_set", $ts.dut.p[eg], 2, gcl)

    t_i("Get TOD of domain 0")
    tod = $ts.dut.call("mesa_ts_timeofday_get")
    tod[0]["seconds"] = 0
    tod[0]["nanoseconds"] = 0
    $ts.dut.call("mesa_ts_timeofday_set", tod[0])

    t_i("Start GCL")
    conf = $ts.dut.call("mesa_qos_tas_port_conf_get", $ts.dut.p[eg])
    conf["max_sdu"][0] = 64
    conf["max_sdu"][3] = 64
    conf["max_sdu"][7] = 64
    conf["gate_enabled"] = true
    conf["gate_open"].each_index {|i| conf["gate_open"][i] = true}
    conf["cycle_time"] = 256
    conf["cycle_time_ext"] = 256
    conf["base_time"]["nanoseconds"] = 303697500
    conf["base_time"]["seconds"] = 4
    conf["base_time"]["sec_msb"] = 0
    conf["gate_enabled"] = true
    conf["config_change"] = true
    $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)

    t_i("Check GCL is pending")
    status = $ts.dut.call("mesa_qos_tas_port_status_get", $ts.dut.p[eg])
    if (status["config_pending"] != true)
        t_e("GCL unexpected config_pending = #{status["config_pending"]}")
    end

    t_i("Wait for GCL to start")
    sleep 5

    t_i("Check GCL is started")
    status = $ts.dut.call("mesa_qos_tas_port_status_get", $ts.dut.p[eg])
    if (status["config_pending"] == true)
        t_e("GCL unexpected config_pending = #{status["config_pending"]}")
    end

    t_i("Start GCL again")
    conf["base_time"]["seconds"] = 8
    conf["base_time"]["nanoseconds"] += 128    #Move the start time out less than cycle_time_ext
    $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)

    t_i("Check GCL is pending")
    status = $ts.dut.call("mesa_qos_tas_port_status_get", $ts.dut.p[eg])
    if (status["config_pending"] != true)
        t_e("GCL unexpected config_pending = #{status["config_pending"]}")
    end

    t_i("Wait for GCL to start")
    sleep 5

    t_i("Check GCL is started")
    status = $ts.dut.call("mesa_qos_tas_port_status_get", $ts.dut.p[eg])
    if (status["config_pending"] == true)
        t_e("GCL unexpected config_pending = #{status["config_pending"]}")
    end

    t_i("Stop GCL")
    conf["gate_enabled"] = false
    conf["config_change"] = false
    conf = $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)

    t_i("Wait for GCL to stop")
    sleep 2

    t_i("Check GCL is stopped")
    status = $ts.dut.call("mesa_qos_tas_port_status_get", $ts.dut.p[eg])
    if (status["config_pending"] == true)
        t_e("GCL unexpected config_pending = #{status["config_pending"]}")
    end
end

def frame_preemption_on_guard_band
    # Based on JIRA APPL-3433
    ig = 0
    eg = $loop_port0
    eg_measure = 1
    frame_size = 500
    frame_tx_time_nano = (frame_size+20)*8    # One bit takes one nano sec to transmit at 1G
    max_sdu = 10240
    guard_band_nano = max_sdu*8
    interval1 = 50*frame_tx_time_nano
    interval2 = 700_000
    cycle_time = (interval1 * 2_000_000) / DUT_TEST_RATE
    interval3 = cycle_time - interval1 - interval2
    preempt_erate = (DUT_TEST_RATE*1000)/2
    preempt_guard_band_erate = (preempt_erate * (interval1 - guard_band_nano)) / interval1

    set_tsn_domain(0)

    oper_up0 = $ts.dut.call("mesa_port_state_get", $loop_port0)
    oper_up1 = $ts.dut.call("mesa_port_state_get", $loop_port1)
    if ((oper_up0 == false) || (oper_up1 == false))
        t_e ("Loop ports are not up. oper_up0 #{oper_up0} oper_up1 #{oper_up1}")
    end

    # Port-to-port forwarding via loop ports
    $ts.dut.call("mesa_vlan_port_members_set", 1, "#{$ts.dut.port_list[1]},#{$ts.dut.port_list[0]},#{$loop_port0},#{$loop_port1}")
    pvlan = $ts.dut.call("mesa_pvlan_port_members_get", 0)
    $ts.dut.call("mesa_pvlan_port_members_set", 0, "#{$ts.dut.port_list[0]},#{$loop_port0}")
    $ts.dut.call("mesa_pvlan_port_members_set", 1, "#{$ts.dut.port_list[1]},#{$loop_port1}")

    shaper = port_shaper_set([$loop_port0, $ts.dut.port_list[eg_measure]], DUT_TEST_RATE, TAS_SHAPER_BURST_LEVEL)

    t_i("Measure initially")
    $ts.dut.run("mesa-cmd mac flush")
    check_rate({
        ig: [ig], eg: eg_measure, size: frame_size, sec: 2, erate: [(DUT_TEST_RATE*1000)],
        etolerance: [5], with_pre_tx: true, pcp: [2]
    })

    t_i("Enable Frame preemption")
    fp = $ts.dut.call("mesa_qos_fp_port_conf_get", $loop_port0)
    fp["admin_status"].each_index {|i| fp["admin_status"][i] = false}
    fp["admin_status"][2] = true
    fp["enable_tx"] = true
    fp["verify_disable_tx"] = false
    fp["add_frag_size"] = 1
    $ts.dut.call("mesa_qos_fp_port_conf_set", $loop_port0, fp)
    sleep 1
    port_status = $ts.dut.call("mesa_qos_fp_port_status_get", $loop_port0)

    t_i("Measure before creating TAS")
    check_rate({
        ig: [ig], eg: eg_measure, size: frame_size, sec: 2, erate: [(DUT_TEST_RATE*1000)],
        etolerance: [5], with_pre_tx: true, pcp: [2]
    })

    t_i("Create GCL")
    gcl = [{"gate_operation":"MESA_QOS_TAS_GCO_SET_AND_RELEASE_MAC",
            "gate_open":[false,false,true,false,false,false,false,false],
            "time_interval":interval1},
           {"gate_operation":"MESA_QOS_TAS_GCO_SET_AND_HOLD_MAC",
            "gate_open":[true,true,false,true,true,true,true,true],
            "time_interval":interval2},
           {"gate_operation":"MESA_QOS_TAS_GCO_SET_AND_HOLD_MAC",
            "gate_open":[false,false,false,false,false,false,false,false],
            "time_interval":interval3}]
    $ts.dut.call("mesa_qos_tas_port_gcl_conf_set", $loop_port0, 3, gcl)

    t_i("Get TOD of domain 0")
    tod = $ts.dut.call("mesa_ts_timeofday_get")
    tod[0]["seconds"] = 0
    tod[0]["nanoseconds"] = 0
    $ts.dut.call("mesa_ts_timeofday_set", tod[0])
    base_time = tod[0].dup

    t_i("Start GCL")
    conf = $ts.dut.call("mesa_qos_tas_port_conf_get", $loop_port0)
    conf["max_sdu"].each_index {|i| conf["max_sdu"][i] = max_sdu}     # Note that the MAXSDU size is very high
    conf["gate_enabled"] = true
    conf["gate_open"].each_index {|i| conf["gate_open"][i] = true}
    conf["cycle_time"] = cycle_time
    conf["cycle_time_ext"] = 256
    conf["base_time"]["nanoseconds"] = 303697500
    conf["base_time"]["seconds"] = 4
    conf["base_time"]["sec_msb"] = 0
    conf["gate_enabled"] = true
    conf["config_change"] = true
    $ts.dut.call("mesa_qos_tas_port_conf_set", $loop_port0, conf)

    t_i("Check GCL is pending")
    status = $ts.dut.call("mesa_qos_tas_port_status_get", $loop_port0)
    if (status["config_pending"] != true)
        t_e("GCL unexpected config_pending = #{status["config_pending"]}")
    end

    t_i("Wait for GCL to start")
    sleep 5

    t_i("Check GCL is started")
    status = $ts.dut.call("mesa_qos_tas_port_status_get", $loop_port0)
    if (status["config_pending"] == true)
        t_e("GCL unexpected config_pending = #{status["config_pending"]}")
    end

    t_i("Measure preemptable traffic after TAS created  pcb #{$ts.dut.pcb}")
    check_rate({
        ig: [ig], eg: eg_measure, size: frame_size, sec: 2, erate: [preempt_erate],
        etolerance: [5], with_pre_tx: true, pcp: [2]
    })

    t_i("Measure non-preemptable traffic after TAS created  pcb #{$ts.dut.pcb}")
    if ($ts.dut.pcb == 135)
        # Why is is a tolerance of 15% needed? Is the queue system able to hold 47000 frames?
        check_rate({
            ig: [ig], eg: eg_measure, size: frame_size, sec: 2, erate: [(DUT_TEST_RATE*1000)],
            etolerance: [15.0], with_pre_tx: true, pcp: [4]
        })
    else
        check_rate({
            ig: [ig], eg: eg_measure, size: frame_size, sec: 2, erate: [(DUT_TEST_RATE*1000)],
            etolerance: [5], with_pre_tx: true, pcp: [4]
        })
    end

    t_i("Disable Frame Preemption")
    fp["admin_status"][2] = false
    fp["enable_tx"] = false
    $ts.dut.call("mesa_qos_fp_port_conf_set", $loop_port0, fp)

    t_i("Measure preemptable traffic after Frame Preemption disabled")
    # Guard band is based on the "large" max SDU configuration and eats into interval1
    check_rate({
        ig: [ig], eg: eg_measure, size: frame_size, sec: 2, erate: [preempt_guard_band_erate],
        etolerance: [8], with_pre_tx: true, pcp: [2], cycle_time: [cycle_time]
    })

    # Throughput of non-preemptable traffic should not be affected by enabling/disabling preemption
    t_i("Measure non-preemptable traffic after Frame Preemption disabled pcb #{$ts.dut.pcb}")
    if ($ts.dut.pcb == 135)
        # Why is is a tolerance of 15% needed? Is the queue system able to hold 47000 frames?
        check_rate({
            ig: [ig], eg: eg_measure, size: frame_size, sec: 2, erate: [(DUT_TEST_RATE*1000)],
            etolerance: [15.0], with_pre_tx: true, pcp: [4]
        })
    else
        check_rate({
            ig: [ig], eg: eg_measure, size: frame_size, sec: 2, erate: [(DUT_TEST_RATE*1000)],
            etolerance: [5], with_pre_tx: true, pcp: [4]
        })
    end

    t_i("Enable Frame Preemption again")
    fp["enable_tx"] = true
    fp["admin_status"][2] = true
    $ts.dut.call("mesa_qos_fp_port_conf_set", $loop_port0, fp)

    t_i("Measure preemptable traffic after FP enabled")
    check_rate({
        ig: [ig], eg: eg_measure, size: frame_size, sec: 2, erate: [preempt_erate],
        etolerance: [5], with_pre_tx: true, pcp: [2]
    })

    t_i("Measure non-preemptable traffic after TAS created  pcb #{$ts.dut.pcb}")
    if ($ts.dut.pcb == 135)
        # Why is is a tolerance of 15% needed? Is the queue system able to hold 47000 frames?
        check_rate({
            ig: [ig], eg: eg_measure, size: frame_size, sec: 2, erate: [(DUT_TEST_RATE*1000)],
            etolerance: [15.0], with_pre_tx: true, pcp: [4]
        })
    else
        check_rate({
            ig: [ig], eg: eg_measure, size: frame_size, sec: 2, erate: [(DUT_TEST_RATE*1000)],
            etolerance: [5], with_pre_tx: true, pcp: [4]
        })
    end

    t_i("Stop GCL")
    conf["gate_enabled"] = false
    conf["config_change"] = false
    conf = $ts.dut.call("mesa_qos_tas_port_conf_set", $loop_port0, conf)

    t_i("Wait for GCL to stop")
    sleep 2

    #$ts.dut.run("mesa-cmd deb api cil qos act 7")
    #$ts.dut.run("symreg read SYS:PSTATE:FPORT_STATE[0-7].MAC_HOLD")

    t_i("Check GCL is stopped")
    status = $ts.dut.call("mesa_qos_tas_port_status_get", $loop_port0)
    if (status["config_pending"] == true)
        t_e("GCL unexpected config_pending = #{status["config_pending"]}")
    end
    open = 0
    status["gate_open"].each do |value|
        if (value == true)
            open += 1
        end
    end
    if (open != 8)
        t_e("GCL unexpected number of open gates #{open}")
    end

     t_i("Measure after stopping TAS")
     check_rate({
         ig: [ig], eg: eg_measure, size: frame_size, sec: 2, erate: [(DUT_TEST_RATE*1000)],
         etolerance: [5], with_pre_tx: true, pcp: [2]
     })

    t_i("Disable Frame Preemption")
    fp["enable_tx"] = false
    $ts.dut.call("mesa_qos_fp_port_conf_set", $loop_port0, fp)

    t_i("Measure after disable FP")
    check_rate({
        ig: [ig], eg: eg_measure, size: frame_size, sec: 2, erate: [(DUT_TEST_RATE*1000)],
        etolerance: [5], with_pre_tx: true, pcp: [2]
    })

    port_shaper_restore(shaper)

    $ts.dut.call("mesa_pvlan_port_members_set", 0, pvlan)
    $ts.dut.call("mesa_pvlan_port_members_set", 1, "")
    if ($ts.dut.port_list.length == 4)
        t_i("Only forward on relevant ports ")
        port_list = "#{$ts.dut.port_list[0]},#{$ts.dut.port_list[1]},#{$ts.dut.port_list[2]},#{$ts.dut.port_list[3]}"
        $ts.dut.call("mesa_vlan_port_members_set", 1, port_list)
    end
end

def no_queue_block_test
    # Based on JIRA MESA-1047
    ig = 0
    eg = $loop_port0
    eg_measure = 1
    frame_size = 1000
    frame_tx_time_nano = (frame_size+20)*8    # One bit takes one nano sec to transmit at 1G
    cycle_time = 100*frame_tx_time_nano
    interval1 = cycle_time * DUT_TEST_RATE / (2 * 1_000_000)
    interval2 = cycle_time - interval1

    set_tsn_domain(0)

    oper_up0 = $ts.dut.call("mesa_port_state_get", $loop_port0)
    oper_up1 = $ts.dut.call("mesa_port_state_get", $loop_port1)
    if ((oper_up0 == false) || (oper_up1 == false))
        t_e ("Loop ports are not up. oper_up0 #{oper_up0} oper_up1 #{oper_up1}")
    end

    # Port-to-port forwarding via loop ports
    $ts.dut.call("mesa_vlan_port_members_set", 1, "#{$ts.dut.port_list[1]},#{$ts.dut.port_list[0]},#{$loop_port0},#{$loop_port1}")
    pvlan = $ts.dut.call("mesa_pvlan_port_members_get", 0)
    $ts.dut.call("mesa_pvlan_port_members_set", 0, "#{$ts.dut.port_list[0]},#{$loop_port0}")
    $ts.dut.call("mesa_pvlan_port_members_set", 1, "#{$ts.dut.port_list[1]},#{$loop_port1}")

    shaper = port_shaper_set([$loop_port0, $ts.dut.port_list[eg_measure]], DUT_TEST_RATE, TAS_SHAPER_BURST_LEVEL)

    t_i("Measure initially")
    $ts.dut.run("mesa-cmd mac flush")
    check_rate({
        ig: [ig], eg: eg_measure, size: frame_size, sec: 2, erate: [(DUT_TEST_RATE*1000)],
        etolerance: [5], with_pre_tx: true, pcp: [2]
    })

    t_i("Enable Frame preemption")
    fp = $ts.dut.call("mesa_qos_fp_port_conf_get", $loop_port0)
    fp["admin_status"].each_index {|i| fp["admin_status"][i] = false}
    fp["admin_status"][0] = true
    fp["admin_status"][1] = true
    fp["admin_status"][2] = true
    fp["admin_status"][3] = true
    fp["enable_tx"] = true
    fp["verify_disable_tx"] = false
    fp["add_frag_size"] = 1
    $ts.dut.call("mesa_qos_fp_port_conf_set", $loop_port0, fp)
    port_status = nil
    verified = false
    50.times do
        sleep 0.1
        port_status = $ts.dut.call("mesa_qos_fp_port_status_get", $loop_port0)
        if (port_status["status_verify"] == "MESA_MM_STATUS_VERIFY_SUCCEEDED")
            verified = true
            break
        end
    end
    t_e("FP verify did not complete: status_verify = #{port_status["status_verify"]}") if (!verified)

    t_i("Create GCL")
    gcl = [{"gate_operation":"MESA_QOS_TAS_GCO_SET_AND_RELEASE_MAC",
            "gate_open":[true,true,true,true,false,false,false,false],
            "time_interval":interval1},
           {"gate_operation":"MESA_QOS_TAS_GCO_SET_AND_HOLD_MAC",
            "gate_open":[false,false,false,false,true,true,true,true],
            "time_interval":interval2}]
    $ts.dut.call("mesa_qos_tas_port_gcl_conf_set", $loop_port0, 2, gcl)

    t_i("Get TOD of domain 0")
    tod = $ts.dut.call("mesa_ts_timeofday_get")
    tod[0]["seconds"] = 0
    tod[0]["nanoseconds"] = 0
    $ts.dut.call("mesa_ts_timeofday_set", tod[0])
    base_time = tod[0].dup

    for iter in 1..10 do
                  
      t_i("Start GCL")
      tod = $ts.dut.call("mesa_ts_timeofday_get")
      conf = $ts.dut.call("mesa_qos_tas_port_conf_get", $loop_port0)
      conf["max_sdu"].each_index {|i| conf["max_sdu"][i] = 1536}     # Note that the MAXSDU size is very high
      conf["gate_enabled"] = true
      conf["gate_open"].each_index {|i| conf["gate_open"][i] = true}
      conf["cycle_time"] = cycle_time
      conf["cycle_time_ext"] = 256
      conf["base_time"]["nanoseconds"] = 303697500
      conf["base_time"]["seconds"] = tod[0]["seconds"] + 4
      conf["base_time"]["sec_msb"] = 0
      conf["gate_enabled"] = true
      conf["config_change"] = true
      $ts.dut.call("mesa_qos_tas_port_conf_set", $loop_port0, conf)

      t_i("Check GCL is pending")
      status = $ts.dut.call("mesa_qos_tas_port_status_get", $loop_port0)
      if (status["config_pending"] != true)
          t_e("GCL unexpected config_pending = #{status["config_pending"]}")
      end

      t_i("Wait for GCL to start")
      sleep 5

      t_i("Check GCL is started")
      status = $ts.dut.call("mesa_qos_tas_port_status_get", $loop_port0)
      if (status["config_pending"] == true)
          t_e("GCL unexpected config_pending = #{status["config_pending"]}")
      end

      t_i("Measure preemptable traffic after TAS created  pcb #{$ts.dut.pcb}")
      check_rate({
          ig: [ig], eg: eg_measure, size: frame_size, sec: 2, erate: [(DUT_TEST_RATE*1000)/2],
          etolerance: [5], with_pre_tx: true, pcp: [2]
      })


      t_i("Stop GCL")
      conf["gate_enabled"] = false
      conf["config_change"] = false
      conf = $ts.dut.call("mesa_qos_tas_port_conf_set", $loop_port0, conf)

      t_i("Wait for GCL to stop")
      sleep 2

      t_i("Check GCL is stopped")
      status = $ts.dut.call("mesa_qos_tas_port_status_get", $loop_port0)
      if (status["config_pending"] == true)
          t_e("GCL unexpected config_pending = #{status["config_pending"]}")
      end

      t_i("Measure after stopping TAS")
      check_rate({
          ig: [ig], eg: eg_measure, size: frame_size, sec: 2, erate: [(DUT_TEST_RATE*1000)],
          etolerance: [5], with_pre_tx: true, pcp: [2]
      })

    end
    t_i("Disable Frame Preemption")
    fp["enable_tx"] = false
    $ts.dut.call("mesa_qos_fp_port_conf_set", $loop_port0, fp)

    port_shaper_restore(shaper)

    $ts.dut.call("mesa_pvlan_port_members_set", 0, pvlan)
    $ts.dut.call("mesa_pvlan_port_members_set", 1, "")
    if ($ts.dut.port_list.length == 4)
        t_i("Only forward on relevant ports ")
        port_list = "#{$ts.dut.port_list[0]},#{$ts.dut.port_list[1]},#{$ts.dut.port_list[2]},#{$ts.dut.port_list[3]}"
        $ts.dut.call("mesa_vlan_port_members_set", 1, port_list)
    end
end

def equal_interval_1_prio_3_port_test
    ig = 3
    eg = [0,1,2]

    frame_size = 500
    frame_tx_time_nano = (frame_size+20)*8    # One bit takes one nano sec to transmit at 1G
    frame_tx_interval_count = 150             # Number of frame transmitted in a GCL entry time interval
    time_interval = frame_tx_interval_count * frame_tx_time_nano
    cycle_time = 3*time_interval
    # Only one priority is measured per egress port, so the port shaper is not shared
    # between priorities. At one open interval out of three the gate and a full rate
    # shaper limit within one percent of each other, leaving the measured rate decided
    # by burst credit. Halve the shaper to keep it the rate limiting factor.
    shaper_rate = DUT_TEST_RATE/2
    gated_erate = shaper_rate*1000

    set_tsn_domain(0)

    shaper = port_shaper_set(eg.map {|eg_idx| $ts.dut.port_list[eg_idx]}, shaper_rate, TAS_SHAPER_BURST_LEVEL)

    t_i("Initialize GCL configuration")
    gcl = [{"gate_operation":"MESA_QOS_TAS_GCO_SET_GATE_STATES",
            "gate_open":[false,false,false,false,false,false,false,false],
            "time_interval":time_interval},
           {"gate_operation":"MESA_QOS_TAS_GCO_SET_GATE_STATES",
            "gate_open":[false,false,false,false,false,false,false,false],
            "time_interval":time_interval},
           {"gate_operation":"MESA_QOS_TAS_GCO_SET_GATE_STATES",
            "gate_open":[false,false,false,false,false,false,false,false],
            "time_interval":time_interval}]

    t_i("Create GCL on all egress ports")
    eg.each do |eg_idx|
        gcl[1][:gate_open][eg_idx] = true  #Enable this prio
        $ts.dut.call("mesa_qos_tas_port_gcl_conf_set", $ts.dut.p[eg_idx], 3, gcl)
        gcl[1][:gate_open][eg_idx] = false     #This prio must be false in next loop
    end

    t_i("Get TOD of domain 0")
    tod = $ts.dut.call("mesa_ts_timeofday_get")
    tod[0]["seconds"] = 0
    tod[0]["nanoseconds"] = 0
    $ts.dut.call("mesa_ts_timeofday_set", tod[0])

    t_i("Start TAS on all egress ports")
    eg.each do |eg_idx|
        t_i("Start GCL")
        conf = $ts.dut.call("mesa_qos_tas_port_conf_get", $ts.dut.p[eg_idx])
        conf["max_sdu"][eg_idx] = frame_size + (frame_size/2)
        conf["gate_enabled"] = true
        conf["gate_open"].each_index {|i| conf["gate_open"][i] = true}
        conf["cycle_time"] = cycle_time
        conf["cycle_time_ext"] = 256
        conf["base_time"]["nanoseconds"] = 0
        conf["base_time"]["seconds"] = 4
        conf["base_time"]["sec_msb"] = 0
        conf["gate_enabled"] = true
        conf["config_change"] = true
        conf = $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg_idx], conf)
    end

    t_i("Wait for GCL to start")
    sleep 5

    t_i("Test TAS on all egress ports")
    eg.each do |eg_idx|
        $ts.dut.run("mesa-cmd mac flush")
        $ts.pc.run("sudo ef tx #{$ts.pc.p[eg_idx]} eth dmac 00:00:00:00:01:02 smac 00:00:00:00:01:01 ipv4 dscp 0")
        check_rate({
            ig: [ig], eg: eg_idx, size: frame_size, sec: 2, erate: [gated_erate],
            etolerance: [5], with_pre_tx: true, pcp: [eg_idx], cycle_time: [cycle_time]
        })
    end

    t_i("Stop TAS on all egress ports")
    eg.each do |eg_idx|
        t_i("Stop GCL")
        conf = $ts.dut.call("mesa_qos_tas_port_conf_get", $ts.dut.p[eg_idx])
        conf["gate_enabled"] = false
        conf["config_change"] = false
        conf = $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg_idx], conf)
    end

    t_i("Wait for GCL to stop")
    sleep 2

    t_i("Test without TAS on all egress ports")
    eg.each do |eg_idx|
        $ts.dut.run("mesa-cmd mac flush")
        $ts.pc.run("sudo ef tx #{$ts.pc.p[eg_idx]} eth dmac 00:00:00:00:01:02 smac 00:00:00:00:01:01 ipv4 dscp 0")
        check_rate({
            ig: [ig], eg: eg_idx, size: frame_size, erate: [gated_erate], etolerance: [5],
            with_pre_tx: true, pcp: [eg_idx]
        })
    end

    port_shaper_restore(shaper)
end

def equal_interval_gcl_reconfig_test
    eg = 0
    ig = [1,2,3]

    frame_size = 500
    frame_tx_time_nano = (frame_size+20)*8                       # One bit takes one nano sec to transmit at 1G
    frame_tx_interval_count = 1000                               # Number of frame transmitted in a GCL entry time interval
    time_interval = frame_tx_interval_count * frame_tx_time_nano # number of nano for each interval
    cycle_time = 3*time_interval                                 # GCL cycle time
    cycle_time_ext = time_interval / 2                           # GCL cycle time extension

    set_tsn_domain(0)

    t_i("Create GCL")
    gcl = [{"gate_operation":"MESA_QOS_TAS_GCO_SET_GATE_STATES",
            "gate_open":[true,false,false,false,false,false,false,false],
            "time_interval":time_interval},
           {"gate_operation":"MESA_QOS_TAS_GCO_SET_GATE_STATES",
            "gate_open":[false,false,false,true,false,false,false,false],
            "time_interval":time_interval},
           {"gate_operation":"MESA_QOS_TAS_GCO_SET_GATE_STATES",
            "gate_open":[false,false,false,false,false,false,false,true],
            "time_interval":time_interval}]
    $ts.dut.call("mesa_qos_tas_port_gcl_conf_set", $ts.dut.p[eg], 3, gcl)

    t_i("Get TOD of domain 0")
    tod = $ts.dut.call("mesa_ts_timeofday_get")
    tod[0]["seconds"] = 0
    tod[0]["nanoseconds"] = 0
    $ts.dut.call("mesa_ts_timeofday_set", tod[0])

    t_i("Start GCL")
    base_time = tod[0].dup
    base_time["seconds"] = 4
    conf = $ts.dut.call("mesa_qos_tas_port_conf_get", $ts.dut.p[eg])
    conf["max_sdu"][0] = frame_size + (frame_size/2)
    conf["max_sdu"][3] = frame_size + (frame_size/2)
    conf["max_sdu"][7] = frame_size + (frame_size/2)
    conf["gate_enabled"] = true
    conf["gate_open"].each_index {|i| conf["gate_open"][i] = true}
    conf["cycle_time"] = cycle_time
    conf["cycle_time_ext"] = cycle_time_ext
    conf["base_time"] = base_time
    conf["gate_enabled"] = true
    conf["config_change"] = true
    $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)

    t_i("Wait for GCL to start")
    sleep 5

    t_i("Check GCL is started")
    status = $ts.dut.call("mesa_qos_tas_port_status_get", $ts.dut.p[eg])
    if (status["config_pending"] == true)
        t_e("GCL unexpected config_pending = #{status["config_pending"]}")
    end

    t_i("Measure the frame rate and cycle time")
    $ts.dut.run("mesa-cmd mac flush")
    $ts.pc.run("sudo ef tx #{$ts.pc.p[eg]} eth dmac 00:00:00:00:01:02 smac 00:00:00:00:01:01 ipv4 dscp 0")
    erate = (DUT_TEST_RATE*1000)/3
    counters = check_rate({
        ig: ig, eg: eg, size: frame_size, sec: 2, erate: [erate,erate,erate], etolerance: [5,5,5],
        with_pre_tx: true, pcp: [0,3,7], cycle_time: [cycle_time,cycle_time,cycle_time]
    })
    check_equal_prio_tx(counters, eg, [0,3,7])

    t_i("-----------Create new GCL with new interval time for cycle extension----------------")
    frame_tx_interval_count = 700                                # Number of frame transmitted in a GCL entry time interval
    time_interval = frame_tx_interval_count * frame_tx_time_nano # number of nano for each interval
    cycle_time_1 = 3*time_interval                                 # GCL cycle time
    cycle_time_ext_1 = time_interval / 2                       # GCL cycle time extension
    gcl[0]["time_interval"] = time_interval
    gcl[1]["time_interval"] = time_interval
    gcl[2]["time_interval"] = time_interval
    $ts.dut.call("mesa_qos_tas_port_gcl_conf_set", $ts.dut.p[eg], 3, gcl)

    t_i("Re-configurate GCL with cycle extension")
    # Calculate a new base time two seconds away at cycle end time
    tod = $ts.dut.call("mesa_ts_timeofday_get")
    seconds = tod[0]["seconds"] + 2
    base_time_1 = base_time.dup
    while true
        base_time_1["nanoseconds"] += cycle_time
        if (base_time_1["nanoseconds"] >= 1_000_000_000)
            base_time_1["seconds"] += 1
            base_time_1["nanoseconds"] -= 1_000_000_000
        end

        if (base_time_1["seconds"] > seconds)
            break;
        end
    end
    # Add half a extension time to assure extension
    base_time_1["nanoseconds"] += (cycle_time_ext_1/2)
    if (base_time_1["nanoseconds"] >= 1_000_000_000)
        base_time_1["seconds"] += 1
        base_time_1["nanoseconds"] -= 1_000_000_000
    end
    # Start new GCL
    conf["cycle_time"] = cycle_time_1
    conf["cycle_time_ext"] = cycle_time_ext_1
    conf["base_time"] = base_time_1
    $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)

    t_i("Wait for GCL to start")
    sleep 4

    t_i("Check GCL is started")
    status = $ts.dut.call("mesa_qos_tas_port_status_get", $ts.dut.p[eg])
    if (status["config_pending"] == true)
        t_e("GCL unexpected config_pending = #{status["config_pending"]}")
    end

    t_i("Measure the frame rate and cycle time")
    $ts.dut.run("mesa-cmd mac flush")
    $ts.pc.run("sudo ef tx #{$ts.pc.p[eg]} eth dmac 00:00:00:00:01:02 smac 00:00:00:00:01:01 ipv4 dscp 0")
    erate = (DUT_TEST_RATE*1000)/3
    counters = check_rate({
        ig: ig, eg: eg, size: frame_size, sec: 2, erate: [erate,erate,erate], etolerance: [5,5,5],
        with_pre_tx: true, pcp: [0,3,7], cycle_time: [cycle_time_1,cycle_time_1,cycle_time_1]
    })
    check_equal_prio_tx(counters, eg, [0,3,7])

    t_i("------------Create new GCL with new interval time for cycle truncation----------")
    frame_tx_interval_count = 400                                # Number of frame transmitted in a GCL entry time interval
    time_interval = frame_tx_interval_count * frame_tx_time_nano # number of nano for each interval
    cycle_time_2 = 3*time_interval                                 # GCL cycle time
    cycle_time_ext_2 = time_interval / 2                           # GCL cycle time extension
    gcl[0]["time_interval"] = time_interval
    gcl[1]["time_interval"] = time_interval
    gcl[2]["time_interval"] = time_interval
    $ts.dut.call("mesa_qos_tas_port_gcl_conf_set", $ts.dut.p[eg], 3, gcl)

    t_i("Re-configurate GCL with cycle trunkation")
    # Calculate a new base time two seconds away at cycle end time
    tod = $ts.dut.call("mesa_ts_timeofday_get")
    seconds = tod[0]["seconds"] + 2
    base_time_2 = base_time_1.dup
    while true
        base_time_2["nanoseconds"] += cycle_time_1
        if (base_time_2["nanoseconds"] >= 1_000_000_000)
            base_time_2["seconds"] += 1
            base_time_2["nanoseconds"] -= 1_000_000_000
        end

        if (base_time_2["seconds"] > seconds)
            break;
        end
    end
    # Add two extension time to assure trunkation
    base_time_2["nanoseconds"] += (2*cycle_time_ext_2)
    if (base_time_2["nanoseconds"] >= 1_000_000_000)
        base_time_2["seconds"] += 1
        base_time_2["nanoseconds"] -= 1_000_000_000
    end
    # Start new GCL
    conf["cycle_time"] = cycle_time_2
    conf["cycle_time_ext"] = cycle_time_ext_2
    conf["base_time"] = base_time_2
    $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)

    t_i("Wait for GCL to start")
    sleep 4

    t_i("Check GCL is started")
    status = $ts.dut.call("mesa_qos_tas_port_status_get", $ts.dut.p[eg])
    if (status["config_pending"] == true)
        t_e("GCL unexpected config_pending = #{status["config_pending"]}")
    end

    t_i("Measure the frame rate and cycle time")
    $ts.dut.run("mesa-cmd mac flush")
    $ts.pc.run("sudo ef tx #{$ts.pc.p[eg]} eth dmac 00:00:00:00:01:02 smac 00:00:00:00:01:01 ipv4 dscp 0")
    erate = (DUT_TEST_RATE*1000)/3
    counters = check_rate({
        ig: ig, eg: eg, size: frame_size, sec: 2, erate: [erate,erate,erate], etolerance: [5,5,5],
        with_pre_tx: true, pcp: [0,3,7], cycle_time: [cycle_time_2,cycle_time_2,cycle_time_2]
    })
    check_equal_prio_tx(counters, eg, [0,3,7])

    t_i("Stop GCL")
    conf["gate_enabled"] = false
    conf["config_change"] = false
    conf = $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)

    t_i("Wait for GCL to stop")
    sleep 2

    t_i("Check GCL is stopped")
    status = $ts.dut.call("mesa_qos_tas_port_status_get", $ts.dut.p[eg])
    if (status["config_pending"] == true)
        t_e("GCL unexpected config_pending = #{status["config_pending"]}")
    end

    pcp0 = 770
    pcp3 = 850
    pcp7 = 5
    if ($cap_family == chip_family_to_id("MESA_CHIP_FAMILY_LAN966X"))
        pcp0 = 1100
        pcp3 = 1300
    end
    if ($cap_family == chip_family_to_id("MESA_CHIP_FAMILY_SPARX5"))
        pcp0 = 900
        pcp3 = 1600
    end
    if ($cap_family == chip_family_to_id("MESA_CHIP_FAMILY_LAN969X"))
        pcp0 = 10
        pcp3 = 5500
    end

    t_i("Strict scheduling test from #{$ts.dut.p[ig[0]]},#{$ts.dut.p[ig[1]]},#{$ts.dut.p[ig[2]]} to #{$ts.dut.p[eg]}")
    # Only expect frames in the highest priority queue when running strict scheduling
    $ts.dut.run("mesa-cmd mac flush")
    $ts.pc.run("sudo ef tx #{$ts.pc.p[eg]} eth dmac 00:00:00:00:01:02 smac 00:00:00:00:01:01 ipv4 dscp 0")
    check_rate({
        ig: ig, eg: eg, size: frame_size, erate: [0,0,(DUT_TEST_RATE*1000)], etolerance: [pcp0,pcp3,pcp7],
        with_pre_tx: true, pcp: [0,3,7]
    }) # On SparX-5 some lower priority frames are slipping through
end

def tas_in_domain_1_test(eg, ig)
    t_i("Time aware scheduling in domain 1 with equal time slots test from #{$ts.dut.p[ig[0]]},#{$ts.dut.p[ig[1]]},#{$ts.dut.p[ig[2]]} to #{$ts.dut.p[eg]}")
    set_tsn_domain(1)

    frame_size = 500
    frame_tx_time_nano = (frame_size+20)*8    # One bit takes one nano sec to transmit at 1G
    frame_tx_interval_count = 1000            # Number of frame transmitted in a GCL entry time interval
    time_interval = frame_tx_interval_count * frame_tx_time_nano
    cycle_time = 3*time_interval

    t_i("Create GCL")
    gcl = [{"gate_operation":"MESA_QOS_TAS_GCO_SET_GATE_STATES",
            "gate_open":[true,false,false,false,false,false,false,false],
            "time_interval":time_interval},
           {"gate_operation":"MESA_QOS_TAS_GCO_SET_GATE_STATES",
            "gate_open":[false,false,false,true,false,false,false,false],
            "time_interval":time_interval},
           {"gate_operation":"MESA_QOS_TAS_GCO_SET_GATE_STATES",
            "gate_open":[false,false,false,false,false,false,false,true],
            "time_interval":time_interval}]
    $ts.dut.call("mesa_qos_tas_port_gcl_conf_set", $ts.dut.p[eg], 3, gcl)

    t_i("Set TOD of domain 0")
    tod = $ts.dut.call("mesa_ts_timeofday_get")
    tod[0]["seconds"] = 0
    tod[0]["nanoseconds"] = 0
    $ts.dut.call("mesa_ts_timeofday_set", tod[0])

    t_i("Set TOD of domain 1")
    tod[0]["seconds"] = 5000
    $ts.dut.call("mesa_ts_domain_timeofday_set", 1, tod[0])

    t_i("Start GCL")
    conf = $ts.dut.call("mesa_qos_tas_port_conf_get", $ts.dut.p[eg])
    conf["max_sdu"][0] = frame_size + (frame_size/2)
    conf["max_sdu"][3] = frame_size + (frame_size/2)
    conf["max_sdu"][7] = frame_size + (frame_size/2)
    conf["gate_enabled"] = true
    conf["ot"] = false
    conf["gate_open"].each_index {|i| conf["gate_open"][i] = true}
    conf["cycle_time"] = cycle_time
    conf["cycle_time_ext"] = 256
    conf["base_time"]["nanoseconds"] = 0
    conf["base_time"]["seconds"] = 5004
    conf["base_time"]["sec_msb"] = 0
    conf["gate_enabled"] = true
    conf["config_change"] = true
    $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)

    t_i("Check GCL is pending")
    conf = $ts.dut.call("mesa_qos_tas_port_status_get", $ts.dut.p[eg])
    if (conf["config_pending"] != true)
        t_e("GCL unexpected config_pending = #{conf["config_pending"]}")
    end

    t_i("Wait for GCL to start")
    sleep 5

    t_i("Check GCL is started")
    conf = $ts.dut.call("mesa_qos_tas_port_status_get", $ts.dut.p[eg])
    if (conf["config_pending"] == true)
        t_e("GCL unexpected config_pending = #{conf["config_pending"]}")
    end
    time = conf["config_change_time"]
    if ((time["nanoseconds"] != 0) || (time["seconds"] != 5004) || (time["sec_msb"] != 0))
        t_e("GCL unexpected config_change_time = nanoseconds #{time["nanoseconds"]} seconds #{time["seconds"]} sec_msb #{time["sec_msb"]}")
    end
    open = 0
    conf["gate_open"].each do |value|
        if (value == true)
            open += 1
        end
    end
    if (open != 1)
        t_e("GCL unexpected number of open gates #{open}")
    end

    t_i("Stop GCL")
    conf = $ts.dut.call("mesa_qos_tas_port_conf_get", $ts.dut.p[eg])
    conf["gate_enabled"] = false
    conf["config_change"] = false
    conf = $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)
end

################################################
# Test Runners
################################################

def test_runner(t)
    cfg = fld_get(t, :cfg, {})
    chk = fld_get(t, :chk, {})
    fun = fld_get(t, :fun, nil)

    begin
        fun.call(t) if (fun != nil)
    ensure
        tas_reset()
    end
end

# Run all or selected test
sel = table_lookup(test_table, :sel)
test_table.each do |t|
    next if (t[:sel] != sel)
    test t[:txt] do
        test_runner(t)
    end
end

################################################
# Test Dump & Summary
################################################

test_summary()

test "dump" do
    #$ts.dut.run("mesa-cmd deb api ci qos action 7")
end