#!/usr/bin/env ruby

# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

require_relative 'libeasy/et'
require_relative 'qos-lib.rb'

$ts = get_test_setup("mesa_pc_b2b_4x")

################################################
# Capability & Configuration
################################################

$dpl_cnt = $ts.dut.call("mesa_capability", "MESA_CAP_QOS_DPL_CNT")
$chip_family = $ts.dut.call("mesa_capability", "MESA_CAP_MISC_CHIP_FAMILY")

check_capabilities() do
    $cap_family = $ts.dut.call("mesa_capability", "MESA_CAP_MISC_CHIP_FAMILY")
    $dpl_cnt = $ts.dut.call("mesa_capability", "MESA_CAP_QOS_DPL_CNT")
    $ot_it = $ts.dut.call("mesa_capability", "MESA_CAP_QOS_OT")
    assert(($ot_it == 1), "OT/IT must be supported")
end

idx_list = [0,1,2,3]
$eg = idx_list[0]
$ig = (idx_list - [$eg])
$ig_it = idx_list[1]
$ig_ot = idx_list[2]

$it_vid = 100
$ot_vid = 200

MESA_VID_NULL = 0

# Queue type enum. Each member is the matching mesa_qos_port_conf key
module QueueType
    IT_QUEUE = "queue"
    OT_QUEUE = "ot_queue"
end

################################################
# Test Tables
################################################

test_table = 
[
    {
        txt: "Port Shaper",
        fun: -> (t) {
            port_shaper_test()
        }
    },
    {
        txt: "Port Strict Scheduling",
        fun: -> (t) {
            port_scheduling_strict_test()
        }
    },
    {
        txt: "IT/OT Scheduling Weighted 30/70 pct",
        fun: -> (t) {
            it_ot_scheduling_with_weighted_30_and_70_percent_test()
        }
    },
    {
        txt: "IT Shaper",
        fun: -> (t) {
            it_shaper_test()
        }
    },
    {
        txt: "OT Shaper",
        fun: -> (t) {
            ot_shaper_test()
        }
    },
    {
        txt: "IT Queue Shaper",
        fun: -> (t) {
            it_queue_shaper_tests()
        }
    },
    {
        txt: "OT Queue Shaper",
        fun: -> (t) {
            ot_queue_shaper_tests()
        }
    },
    {
        txt: "IT Scheduling",
        fun: -> (t) {
            it_scheduling()
        }
    },
    {
        txt: "OT Scheduling",
        fun: -> (t) {
            ot_scheduling()
        }
    },
]

################################################
# General/Helper Functions
################################################

def config_port_shaper_queues_never_emptied
    t_i("Configure port shaper to assure queues are never emptied")
    qconf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[$eg])
    qconf["shaper"]["level"] = 25_000    # Shaper must have "large" burst size level in order to shape correctly at "high" rates
    qconf["shaper"]["rate"] = 990_000
    $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[$eg], qconf)
end

def set_ot_it_strict_scheduling
    t_i("Configure IT/OT strict scheduling")
    conf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[$eg])
    conf["ot_it_dwrr_enable"] = false
    conf["ot_pct"] = 0
    conf = $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[$eg], conf)
end

def disable_it_shaper
    t_i("Configure IT shaper disabled")
    qconf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[$eg])
    qconf["it_shaper"]["level"] = 0
    qconf["it_shaper"]["rate"] = 0xFFFFFFFF
    $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[$eg], qconf)
end

def disable_ot_shaper
    t_i("Configure OT shaper disabled")
    qconf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[$eg])
    qconf["ot_shaper"]["level"] = 0
    qconf["ot_shaper"]["rate"] = 0xFFFFFFFF
    $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[$eg], qconf)
end

def disable_queue_shaping(queue_type: nil, cos_list: nil)
    if queue_type != QueueType::IT_QUEUE && queue_type != QueueType::OT_QUEUE
        t_e("disable_queue_shaping() requires queue_type to be IT or OT")
        return
    end

    return t_e("disable_queue_shaping() requires a cos_list argument") if cos_list.nil?

    t_i("Configure #{queue_type} shaping disabled")
    conf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[$eg])
    cos_list.each do |cos|
        conf[queue_type][cos]["shaper"]["level"] = 0
        conf[queue_type][cos]["shaper"]["rate"] = 0xFFFFFFFF
    end
    conf = $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[$eg], conf)
end

def ot_scheduling_with_weighted_10_30_and_60_percent_test
    t_i("OT scheduling with Weighted 10, 30 and 60 percent test")
    # Expect distribution of frames in queue 0..2 based on weights (10%, 30%, 60%)
    conf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[$eg])
    conf["ot_dwrr_enable"] = true
    conf["ot_dwrr_cnt"] = 3
    conf["ot_queue"][0]["pct"] = 10
    conf["ot_queue"][1]["pct"] = 30
    conf["ot_queue"][2]["pct"] = 60
    conf = $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[$eg], conf)

    erate0 = 990_000_000 * 1 / 10
    erate1 = 990_000_000 * 3 / 10
    erate2 = 990_000_000 * 6 / 10

    check_rate({
        ig: $ig, eg: $eg, size: 600, sec: 1, erate: [erate0,erate1,erate2], etolerance: [4,6.5,5.1],
        with_pre_tx: true, pcp: [0,1,2]
    })

    t_i("Config OT strict scheduling")
    conf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[$eg])
    conf["ot_dwrr_enable"] = false
    conf["ot_dwrr_cnt"] = 0
    conf = $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[$eg], conf)
end

def it_scheduling_with_weighted_10_30_and_60_percent_test
    t_i("IT scheduling with Weighted 10, 30 and 60 percent test")
    # Expect distribution of frames in queue 0..2 based on weights (10%, 30%, 60%)
    conf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[$eg])
    conf["dwrr_enable"] = true
    conf["dwrr_cnt"] = 3
    conf["queue"][0]["pct"] = 10
    conf["queue"][1]["pct"] = 30
    conf["queue"][2]["pct"] = 60
    conf = $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[$eg], conf)

    erate0 = 990_000_000 * 1 / 10
    erate1 = 990_000_000 * 3 / 10
    erate2 = 990_000_000 * 6 / 10

    check_rate({
        ig: $ig, eg: $eg, size: 600, sec: 1, erate: [erate0,erate1,erate2], etolerance: [4,6.5,5],
        with_pre_tx: true, pcp: [0,1,2]
    })

    t_i("Configure IT strict scheduling")
    conf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[$eg])
    conf["dwrr_enable"] = false
    conf["dwrr_cnt"] = 0
    conf = $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[$eg], conf)
end

################################################
# Test Section
################################################
test "conf" do
    t_i("Configure all ports to C tag aware")
    idx_list.each do |i|
        $ts.dut.run("mesa-cmd port flow control #{$ts.dut.p[i]+1} disable")

        conf = $ts.dut.call("mesa_vlan_port_conf_get", $ts.dut.p[i])
        conf["port_type"] = "MESA_VLAN_PORT_TYPE_C"
        $ts.dut.call("mesa_vlan_port_conf_set", $ts.dut.p[i], conf)

        conf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[i])
        conf["tag"]["class_enable"] = true
        conf["tag"]["pcp_dei_map"][0][0]["prio"] = 0
        conf["tag"]["pcp_dei_map"][0][0]["dpl"] = 0
        conf["tag"]["pcp_dei_map"][0][1]["prio"] = 0
        conf["tag"]["pcp_dei_map"][0][1]["dpl"] = 1
        conf["tag"]["pcp_dei_map"][1][0]["prio"] = 1
        conf["tag"]["pcp_dei_map"][1][0]["dpl"] = 0
        conf["tag"]["pcp_dei_map"][1][1]["prio"] = 1
        conf["tag"]["pcp_dei_map"][1][1]["dpl"] = 1
        conf["default_prio"] = i
        conf["default_dpl"] = 0
        $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[i], conf)
    end

    t_i("Configure OT/IT VLAN classification as default")
    conf = $ts.dut.call("mesa_vlan_port_conf_get", $ts.dut.p[$ig_it])
    conf["pvid"] = $it_vid
    $ts.dut.call("mesa_vlan_port_conf_set", $ts.dut.p[$ig_it], conf)
    conf = $ts.dut.call("mesa_vlan_port_conf_get", $ts.dut.p[$ig_ot])
    conf["pvid"] = $ot_vid
    $ts.dut.call("mesa_vlan_port_conf_set", $ts.dut.p[$ig_ot], conf)

    port_list = port_idx_list_str(idx_list)
    t_i("Only forward on relevant ports #{port_list}")
    $ts.dut.call("mesa_vlan_port_members_set", $ot_vid, port_list)
    $ts.dut.call("mesa_vlan_port_members_set", $it_vid, port_list)

    t_i("Configure VID to be OT")
    conf = $ts.dut.call("mesa_vlan_vid_conf_get", $ot_vid)
    conf["ot"] = true
    conf = $ts.dut.call("mesa_vlan_vid_conf_set", $ot_vid, conf)

    sleep(5)
    dut_port_state_up([$ts.dut.p[$ig_it], $ts.dut.p[$ig_ot], $ts.dut.p[$eg]])

    t_i("Configure egress port to C tag all")
    vconf = $ts.dut.call("mesa_vlan_port_conf_get", $ts.dut.p[$eg])
    vconf["port_type"] = "MESA_VLAN_PORT_TYPE_C"
    vconf["untagged_vid"] = MESA_VID_NULL
    $ts.dut.call("mesa_vlan_port_conf_set", $ts.dut.p[$eg], vconf)

    t_i("Configure egress prio and dpl mapping to 1:1")
    dconf = $ts.dut.call("mesa_qos_port_dpl_conf_get", $ts.dut.p[$eg], $dpl_cnt)
    dconf[0]["pcp"] = [0,1,2,3,4,5,6,7]
    dconf[0]["dei"] = [0,0,0,0,0,0,0,0]
    dconf[1]["pcp"] = [0,1,2,3,4,5,6,7]
    dconf[1]["dei"] = [1,1,1,1,1,1,1,1]
    $ts.dut.call("mesa_qos_port_dpl_conf_set", $ts.dut.p[$eg], $dpl_cnt, dconf)

    t_i("Configure egress prio and dpl tagging to mapped.")
    qconf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[$eg])
    qconf["tag"]["remark_mode"] = "MESA_TAG_REMARK_MODE_MAPPED"
    $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[$eg], qconf)

    t_i("Learn the destination MAC on the egress port")
    $ts.pc.run("sudo ef tx #{$ts.pc.p[$eg]} eth dmac 00:00:00:00:01:02 smac 00:00:00:00:01:01 ctag vid #{$it_vid} ipv4 dscp 0")
    $ts.pc.run("sudo ef tx #{$ts.pc.p[$eg]} eth dmac 00:00:00:00:01:02 smac 00:00:00:00:01:01 ctag vid #{$ot_vid} ipv4 dscp 0")
end

def port_shaper_test
    t_i("Port shaper test")
    qconf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[$eg])
    qconf["shaper"]["level"] = 25_000
    qconf["shaper"]["rate"] = 500_000
    qconf["shaper"]["mode"] = "MESA_SHAPER_MODE_LINE"
    $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[$eg], qconf)

    check_rate({
        ig: [$ig_it], eg: $eg, size: 1000, sec: 1, erate: [500_000_000], etolerance: [3], with_pre_tx: true
    })
    check_rate({
        ig: [$ig_ot], eg: $eg, size: 1000, sec: 1, erate: [500_000_000], etolerance: [3], with_pre_tx: true
    })
end

def port_scheduling_strict_test
    config_port_shaper_queues_never_emptied()

    t_i("Port scheduling strict test")
    # Only expect frames classified to OT when running strict port scheduling. Independent of classified prio
    check_rate({
        ig: [$ig_it, $ig_ot], eg: $eg, size: 600, sec: 1, erate: [0,990_000_000], etolerance: [0,2],
        with_pre_tx: true, pcp: [7,0]
    })
end

def it_ot_scheduling_with_weighted_30_and_70_percent_test
    config_port_shaper_queues_never_emptied()

    t_i("IT/OT scheduling with Weighted 30 and 70 percent test")
    # Expect distribution of IT - OT frames based on weights (30%, 70%). Independent of classified prio
    conf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[$eg])
    conf["ot_it_dwrr_enable"] = true
    conf["ot_pct"] = 70
    conf = $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[$eg], conf)

    erate0 = 990_000_000 * 3 / 10
    erate1 = 990_000_000 * 7 / 10

    check_rate({
        ig: [$ig_it, $ig_ot], eg: $eg, size: 1000, sec: 1, erate: [erate0,erate1], etolerance: [1.7,1.7],
        with_pre_tx: true, pcp: [7,0]
    })

    set_ot_it_strict_scheduling()
end

def it_shaper_test
    config_port_shaper_queues_never_emptied()

    t_i("IT shaper test")
    qconf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[$eg])
    qconf["it_shaper"]["level"] = 25_000
    qconf["it_shaper"]["rate"] = 500_000
    qconf["it_shaper"]["mode"] = "MESA_SHAPER_MODE_LINE"
    $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[$eg], qconf)

    check_rate({
        ig: [$ig_it], eg: $eg, size: 1000, sec: 1, erate: [500_000_000], etolerance: [2], with_pre_tx: true
    })
    check_rate({
        ig: [$ig_ot], eg: $eg, size: 1000, sec: 1, erate: [990_000_000], etolerance: [2], with_pre_tx: true
    })

    disable_it_shaper()
end

def ot_shaper_test
    config_port_shaper_queues_never_emptied()

    t_i("OT shaper test")
    qconf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[$eg])
    qconf["ot_shaper"]["level"] = 25_000
    qconf["ot_shaper"]["rate"] = 500_000
    qconf["ot_shaper"]["mode"] = "MESA_SHAPER_MODE_LINE"
    $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[$eg], qconf)

    check_rate({
        ig: [$ig_ot], eg: $eg, size: 1000, sec: 1, erate: [500_000_000], etolerance: [2], with_pre_tx: true
    })
    check_rate({
        ig: [$ig_it], eg: $eg, size: 1000, sec: 1, erate: [990_000_000], etolerance: [2], with_pre_tx: true
    })

    disable_ot_shaper()
end

def it_queue_shaper_tests
    config_port_shaper_queues_never_emptied()

    t_i("IT queue shaper tests")
    cos_list = [0,3,7]
    cos_list.each do |cos|
        t_i("Queue #{cos} shaper line rate 100000 kbps")
        conf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[$eg])
        conf["queue"][cos]["shaper"]["level"] = 1
        conf["queue"][cos]["shaper"]["rate"] = 100_000
        conf["queue"][cos]["shaper"]["mode"] = "MESA_SHAPER_MODE_LINE"
        conf = $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[$eg], conf)

        check_rate({
            ig: [$ig_it], eg: $eg, size: 1000, sec: 1, erate: [100_000_000], etolerance: [0.5], pcp: [cos]
        })

        t_i("Queue #{cos} shaper data rate 100000 kbps")
        conf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[$eg])
        conf["queue"][cos]["shaper"]["level"] = 1
        conf["queue"][cos]["shaper"]["rate"] = 100_000
        conf["queue"][cos]["shaper"]["mode"] = "MESA_SHAPER_MODE_DATA"
        conf = $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[$eg], conf)

        check_rate({
            ig: [$ig_it], eg: $eg, size: 1000, sec: 1, data_rate: true, erate: [100_000_000],
            etolerance: [0.5], pcp: [cos]
        })
    end

    disable_queue_shaping(queue_type: QueueType::IT_QUEUE, cos_list: cos_list)
end

def ot_queue_shaper_tests
    config_port_shaper_queues_never_emptied()

    t_i("OT queue shaper tests")
    cos_list = [0,3,7]
    cos_list.each do |cos|
        t_i("Queue #{cos} shaper line rate 100000 kbps")
        conf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[$eg])
        conf["ot_queue"][cos]["shaper"]["level"] = 1
        conf["ot_queue"][cos]["shaper"]["rate"] = 100_000
        conf["ot_queue"][cos]["shaper"]["mode"] = "MESA_SHAPER_MODE_LINE"
        conf = $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[$eg], conf)

        check_rate({
            ig: [$ig_ot], eg: $eg, size: 1000, sec: 1, erate: [100_000_000], etolerance: [0.7], pcp: [cos]
        })

        t_i("Queue #{cos} shaper data rate 100000 kbps")
        conf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[$eg])
        conf["ot_queue"][cos]["shaper"]["level"] = 1
        conf["ot_queue"][cos]["shaper"]["rate"] = 100_000
        conf["ot_queue"][cos]["shaper"]["mode"] = "MESA_SHAPER_MODE_DATA"
        conf = $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[$eg], conf)

        check_rate({
            ig: [$ig_ot], eg: $eg, size: 1000, sec: 1, data_rate: true, erate: [100_000_000],
            etolerance: [0.7], pcp: [cos]
        })
    end

    disable_queue_shaping(queue_type: QueueType::OT_QUEUE, cos_list: cos_list)
end

def ot_scheduling
    config_port_shaper_queues_never_emptied()

    t_i("Configure OT VLAN classification on all ingress as default")
    $ig.each do |index|
        conf = $ts.dut.call("mesa_vlan_port_conf_get", $ts.dut.p[index])
        conf["pvid"] = $ot_vid
        $ts.dut.call("mesa_vlan_port_conf_set", $ts.dut.p[index], conf)
    end

    $ts.pc.run("sudo ef tx #{$ts.pc.p[$eg]} eth dmac 00:00:00:00:01:02 smac 00:00:00:00:01:01 ctag vid #{$it_vid} ipv4 dscp 0")
    $ts.pc.run("sudo ef tx #{$ts.pc.p[$eg]} eth dmac 00:00:00:00:01:02 smac 00:00:00:00:01:01 ctag vid #{$ot_vid} ipv4 dscp 0")

    t_i("Running a measurement test for OT Strict Scheduling")
    check_rate({
        ig: $ig, eg: $eg, size: 600, sec: 1, erate: [0,0,990_000_000], etolerance: [150,900,2],
        with_pre_tx: true, pcp: [0,3,7]
    })

    ot_scheduling_with_weighted_10_30_and_60_percent_test()
    qos_tas_equal_interval_3_prio_1_port_test($eg, $ig, $it_vid, $ot_vid, true, custom_rate: 990_000)
end

def it_scheduling
    config_port_shaper_queues_never_emptied()
    t_i("Configure IT VLAN classification on all ingress as default")
    $ig.each do |index|
        conf = $ts.dut.call("mesa_vlan_port_conf_get", $ts.dut.p[index])
        conf["pvid"] = $it_vid
        $ts.dut.call("mesa_vlan_port_conf_set", $ts.dut.p[index], conf)
    end

    $ts.pc.run("sudo ef tx #{$ts.pc.p[$eg]} eth dmac 00:00:00:00:01:02 smac 00:00:00:00:01:01 ctag vid #{$it_vid} ipv4 dscp 0")
    $ts.pc.run("sudo ef tx #{$ts.pc.p[$eg]} eth dmac 00:00:00:00:01:02 smac 00:00:00:00:01:01 ctag vid #{$ot_vid} ipv4 dscp 0")

    t_i("Running a measurement test for IT Strict Scheduling")
    check_rate({
        ig: $ig, eg: $eg, size: 600, sec: 1, erate: [0,0,990_000_000], etolerance: [10,2100,1],
        with_pre_tx: true, pcp: [0,3,7]
    })
    it_scheduling_with_weighted_10_30_and_60_percent_test()
    qos_tas_equal_interval_3_prio_1_port_test($eg, $ig, $it_vid, $ot_vid, false, custom_rate: 990_000)
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
    test t[:txt] do
        test_runner(t)
    end
end

################################################
# Test Dump & Summary
################################################

test_summary()

test "dump" do
    #$ts.dut.run("mesa-cmd deb api ai qos")
end
