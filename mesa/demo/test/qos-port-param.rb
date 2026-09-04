#!/usr/bin/env ruby

# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

require_relative 'libeasy/et'

$ts = get_test_setup("mesa_pc_b2b_2x")

################################################
# Capability & Configuration
################################################

$dpl_cnt = cap_get("QOS_DPL_CNT")
$cap_cnt_evc = (cap_get("PORT_CNT_EVC") != 0) ? true : false
t_i("DPL_CNT = #{$dpl_cnt}, MESA_CAP_PORT_CNT_ENV = #{$cap_cnt_evc}")

$ig = 0
$eg = 1

################################################
# Test Tables
################################################

test_table =
[
    {
        txt: "Untagged frame maps to default cos 3, dpl 0 (green)",
        cfg: { default_prio: 3, default_dpl: 0, rx_pcp: 3, rx_dei: 0, cos: 3, color: :green },
    },
    {
        txt: "Untagged frame maps to default cos 5, dpl 1 (yellow)",
        cfg: { default_prio: 5, default_dpl: 1, rx_pcp: 5, rx_dei: 1, cos: 5, color: :yellow },
    },
    {
        txt: "Tag classification disabled, tagged frame falls back to cos 0, dpl 0 (green)",
        cfg: { class_enable: false, default_prio: 0, default_dpl: 0, tx_tag: { pcp: 3, dei: 1 },
               rx_pcp: 0, rx_dei: 0, cos: 0, color: :green },
    },
    {
        txt: "Tag pcp 5, dei 0 classifies to cos 5, dpl 0 (green)",
        cfg: { class_enable: true, default_prio: 0, default_dpl: 0, tx_tag: { pcp: 5, dei: 0 },
               rx_pcp: 5, rx_dei: 0, cos: 5, color: :green },
    },
    {
        txt: "Tag pcp 0, dei 1 classifies to cos 1, dpl 1 (yellow)",
        cfg: { class_enable: true, default_prio: 0, default_dpl: 0, tx_tag: { pcp: 0, dei: 1 },
               rx_pcp: 1, rx_dei: 1, cos: 1, color: :yellow },
    },
]

################################################
# Test Section
################################################

test "conf" do
    t_i("Only forward on relevant ports #{$ts.dut.p}")
    port_list = port_idx_list_str([$ig, $eg])
    $ts.dut.call("mesa_vlan_port_members_set", 1, port_list)

    t_i("Learn mac address on egress port")
    $ts.pc.run("sudo ef tx #{$ts.pc.p[$eg]} eth dmac 00:01:00:00:00:02 smac 00:01:00:00:00:01 ipv4 dscp 0")

    t_i("Configure ingress port to C tag aware")
    vconf = $ts.dut.call("mesa_vlan_port_conf_get", $ts.dut.p[$ig])
    vconf["port_type"] = "MESA_VLAN_PORT_TYPE_C"
    $ts.dut.call("mesa_vlan_port_conf_set", $ts.dut.p[$ig], vconf)

    t_i("Configure egress port to C tag all")
    vconf = $ts.dut.call("mesa_vlan_port_conf_get", $ts.dut.p[$eg])
    vconf["port_type"] = "MESA_VLAN_PORT_TYPE_C"
    vconf["untagged_vid"] = 0 # MESA_VID_NULL
    $ts.dut.call("mesa_vlan_port_conf_set", $ts.dut.p[$eg], vconf)

    t_i("Configure egress prio and dpl mapping to 1:1")
    dconf = $ts.dut.call("mesa_qos_port_dpl_conf_get", $ts.dut.p[$eg], $dpl_cnt)
    dconf[0]["pcp"] = [0,1,2,3,4,5,6,7]
    dconf[0]["dei"] = [0,0,0,0,0,0,0,0]
    dconf[1]["pcp"] = [0,1,2,3,4,5,6,7]
    dconf[1]["dei"] = [1,1,1,1,1,1,1,1]
    $ts.dut.call("mesa_qos_port_dpl_conf_set", $ts.dut.p[$eg], $dpl_cnt, dconf)

    t_i("Configure egress prio and dpl tagging to mapped")
    qconf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[$eg])
    qconf["tag"]["remark_mode"] = "MESA_TAG_REMARK_MODE_MAPPED"
    $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[$eg], qconf)
end

def qos_port_param_test(cfg)
    t_i("Configure ingress default prio and dpl")
    qconf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[$ig])
    qconf["tag"]["class_enable"] = cfg[:class_enable] if cfg.key?(:class_enable)
    qconf["default_prio"] = cfg[:default_prio]
    qconf["default_dpl"] = cfg[:default_dpl]
    $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[$ig], qconf)

    t_i("clear port counters")
    $ts.dut.call("mesa_port_counters_clear", $ts.dut.p[$ig])
    $ts.dut.call("mesa_port_counters_clear", $ts.dut.p[$eg])

    t_i("Send IPV4 frame and expect reception of tag with pcp and dei")
    tag = cfg[:tx_tag]
    tx_tag_str = tag.nil? ? "" : "ctag vid 1 pcp #{tag[:pcp]} dei #{tag[:dei]} "
    txframe = "eth dmac 00:01:00:00:00:02 smac 00:01:00:00:00:01 #{tx_tag_str}ipv4 dscp 0 data pattern cnt 40"
    rxframe = "eth dmac 00:01:00:00:00:02 smac 00:01:00:00:00:01 ctag vid 1 pcp #{cfg[:rx_pcp]} dei #{cfg[:rx_dei]} ipv4 dscp 0 data pattern cnt 40"
    $ts.pc.run("sudo ef tx #{$ts.pc.p[$ig]} #{txframe} rx #{$ts.pc.p[$eg]} #{rxframe}")

    t_i("Check QoS counters")
    cos = cfg[:cos]
    icounters = $ts.dut.call("mesa_port_counters_get", $ts.dut.p[$ig])
    ecounters = $ts.dut.call("mesa_port_counters_get", $ts.dut.p[$eg])
    if ((icounters["prio"][cos]["rx"] != 1) || (ecounters["prio"][cos]["tx"] != 1))
        t_e("ingress/egress counters not as expected. rx #{icounters["prio"][cos]["rx"]}   tx #{ecounters["prio"][cos]["tx"]}")
    end

    if ($cap_cnt_evc && !cfg[:color].nil?)
        t_i("Check colour counter")
        rx_col = "rx_#{cfg[:color]}"
        tx_col = "tx_#{cfg[:color]}"
        if ((icounters["prio"][cos][rx_col] != 1) || (ecounters["prio"][cos][tx_col] != 1))
            t_e("ingress/egress counters not as expected. rx #{cfg[:color]} #{icounters["prio"][cos][rx_col]}   tx #{cfg[:color]} #{ecounters["prio"][cos][tx_col]}")
        end
    end
end

################################################
# Test Runners
################################################

# Run all or selected test
sel = table_lookup(test_table, :sel)
test_table.each do |t|
    next if (t[:sel] != sel)
    rep = fld_get(t, :rep, 1)
    rep.times do |i|
        txt = (rep == 1) ? t[:txt] : "#{t[:txt]} (#{i + 1}/#{rep})"
        test(txt) do
            qos_port_param_test(t[:cfg])
        end
    end
end

################################################
# Test Dump & Summary
################################################

test_summary()

test "dump" do
    #$ts.dut.run("mesa-cmd deb api ai qos")
end

