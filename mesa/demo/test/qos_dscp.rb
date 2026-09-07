#!/usr/bin/env ruby

# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

require_relative 'libeasy/et'

$ts = get_test_setup("mesa_pc_b2b_2x")

################################################
# Capability & Configuration
################################################

$igr_port = 0
$egr_port = 1

# DSCP in -> DSCP out, one entry per DSCP sent by every test case
DSCP_UNCHANGED = { 0 => 0, 32 => 32, 63 => 63 }

# DSCP translation table
DSCP_TRANSLATE_MAP = { 0 => 1, 32 => 33, 63 => 0 }

# DSCP and DPL to DSCP remapping table, DPL zero
DSCP_DPL_MAP = { 0 => 12, 32 => 34, 63 => 56 }

# DSCP to queue and DPL mapping table
DSCP_QUEUE_DPL_MAP = [
    { dscp: 0,  trust: true,  prio: 1, dpl: 0 },
    { dscp: 32, trust: false, prio: 2, dpl: 1 },
    { dscp: 63, trust: true,  prio: 3, dpl: 1 },
]

################################################
# Test Tables
################################################

test_table =
[
    {
        txt: "DSCP translate off",
        cfg: { translate: false, rx: DSCP_UNCHANGED },
        fun: -> (t) { dscp_translate_test_func(t[:cfg]) }
    },
    {
        txt: "DSCP translate on, no egress remark",
        cfg: { translate: true, rx: DSCP_UNCHANGED },
        fun: -> (t) { dscp_translate_test_func(t[:cfg]) }
    },
    {
        txt: "DSCP translate on + egress remark",
        cfg: { translate: true, emode: "MESA_DSCP_EMODE_REMARK", rx: DSCP_TRANSLATE_MAP },
        fun: -> (t) { dscp_translate_test_func(t[:cfg]) }
    },
    {
        txt: "DSCP to queue and DPL mapping",
        cfg: { map: DSCP_QUEUE_DPL_MAP, rx: DSCP_UNCHANGED },
        fun: -> (t) { dscp_map_test_func(t[:cfg]) }
    },
    {
        txt: "Queue and DPL mapping to DSCP for all",
        cfg: { map: DSCP_QUEUE_DPL_MAP, mode: "MESA_DSCP_MODE_ALL",
               emode: "MESA_DSCP_EMODE_REMARK",
               rx: { 0 => 33, 32 => 11, 63 => 66 } },
        fun: -> (t) { dscp_map_test_func(t[:cfg]) }
    },
    {
        txt: "Queue and DPL mapping to DSCP for selected",
        cfg: { map: DSCP_QUEUE_DPL_MAP, mode: "MESA_DSCP_MODE_SEL",
               emode: "MESA_DSCP_EMODE_REMARK",
               remark: { 0 => true, 32 => true, 63 => false },
               rx: { 0 => 33, 32 => 11, 63 => 63 } },
        fun: -> (t) { dscp_map_test_func(t[:cfg]) }
    },
    {
        txt: "DSCP+DPL to DSCP remap",
        cfg: { rx: DSCP_DPL_MAP },
        fun: -> (t) { dscp_dpl_remap_test_func(t[:cfg]) }
    }
]

################################################
# General/Helper Functions
################################################

def qos_port_conf_get(port)
    $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[port])
end

def qos_port_conf_set(port, conf)
    $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[port], conf)
end

# Apply the given overrides to the port DSCP conf
def config_port_dscp(port, cfg)
    conf = qos_port_conf_get(port)
    cfg.each { |k, v| conf["dscp"][k.to_s] = v }
    qos_port_conf_set(port, conf)
end

# Get the global DSCP conf, hand it to the block and set it back
def config_qos_dscp
    conf = $ts.dut.call("mesa_qos_conf_get")
    yield conf["dscp"]
    $ts.dut.call("mesa_qos_conf_set", conf)
end

# Send one frame per DSCP in the map, expecting the mapped DSCP and tag on egress
def send_frames(rx_map, tags = {})
    eth = "eth dmac ff:ff:ff:ff:ff:ff smac 00:00:00:00:00:0a"

    rx_map.each do |tx, rx|
        tag = fld_get(tags, tx, {})
        pcp = fld_get(tag, :pcp, 0)
        dei = fld_get(tag, :dei, 0)
        t_i("Send DSCP #{tx}, expect DSCP #{rx} PCP #{pcp} DEI #{dei}")

        txframe = "#{eth} ipv4 dscp #{tx}"
        rxframe = "#{eth} ctag vid 1 pcp #{pcp} dei #{dei} ipv4 dscp #{rx}"
        $ts.pc.run("sudo ef tx #{$ts.pc.p[$igr_port]} #{txframe} data pattern cnt 40 rx #{$ts.pc.p[$egr_port]} #{rxframe} data pattern cnt 40")
    end
end

def check_queue_counters(queues, counters)
    t_i("Check QoS Counters: Queues = #{queues}, Counters = #{counters}")

    icounters = $ts.dut.call("mesa_port_counters_get", $ts.dut.p[$igr_port])
    ecounters = $ts.dut.call("mesa_port_counters_get", $ts.dut.p[$egr_port])

    queues.zip(counters).each do |queue, expected|
        check_counter("queue #{queue} rx", icounters["prio"][queue]["rx"], expected)
        check_counter("queue #{queue} tx", ecounters["prio"][queue]["tx"], expected)
    end
end

def configure_dscp_translate_table
    t_i("Configure DSCP translation table")
    config_qos_dscp() { |dscp| DSCP_TRANSLATE_MAP.each { |from, to| dscp[from]["dscp"] = to } }
end

def configure_dscp_queue_dpl_map(map, remark = nil)
    t_i("Configure DSCP to queue and DPL mapping table")
    config_qos_dscp() do |dscp|
        map.each do |m|
            e = dscp[m[:dscp]]
            m.each { |k, v| e[k.to_s] = v unless (k == :dscp) }
            e["remark"] = remark[m[:dscp]] if remark
        end
    end
end

def configure_dpl_dscp_map
    t_i("Configure queue and DPL to DSCP mapping table")
    map = { 0 => { 0 => 11, 1 => 33, 3 => 55 },  # [dpl][prio] -> DSCP
            1 => { 0 => 22, 1 => 44, 3 => 66 } }

    dpl_cnt = cap_get("QOS_DPL_CNT")
    conf = $ts.dut.call("mesa_qos_dpl_conf_get", dpl_cnt)
    map.each { |dpl, prios| prios.each { |prio, dscp| conf[dpl]["dscp"][prio] = dscp } }
    $ts.dut.call("mesa_qos_dpl_conf_set", dpl_cnt, conf)
end

def configure_dscp_dpl_map
    t_i("Configure DSCP and DPL to DSCP remapping table")
    dpl_cnt = cap_get("QOS_DPL_CNT")
    conf = $ts.dut.call("mesa_qos_dscp_dpl_conf_get", dpl_cnt)
    DSCP_DPL_MAP.each { |dscp, remap| conf[dscp][0]["dscp"] = remap }
    $ts.dut.call("mesa_qos_dscp_dpl_conf_set", dpl_cnt, conf)
end

def baseline_no_mapping_test
    t_i("Test: no translation - no queue mapping - no rewriter update")
    $ts.dut.run("mesa-cmd port statis clear")

    config_port_dscp($igr_port, translate: false, mode: "MESA_DSCP_MODE_NONE",
                                class_enable: false)
    config_port_dscp($egr_port, emode: "MESA_DSCP_EMODE_DISABLE")

    send_frames(DSCP_UNCHANGED)
    check_queue_counters([0], [3])
end

################################################
# Test Section
################################################

test "conf" do
    t_i("Configure egress port to C tag all")
    vconf = $ts.dut.call("mesa_vlan_port_conf_get", $ts.dut.p[$egr_port])
    vconf["port_type"] = "MESA_VLAN_PORT_TYPE_C"
    vconf["untagged_vid"] = 0 # MESA_VID_NULL
    $ts.dut.call("mesa_vlan_port_conf_set", $ts.dut.p[$egr_port], vconf)

    t_i("Configure egress prio and dpl mapping to 1:1")
    dpl_cnt = cap_get("QOS_DPL_CNT")
    dconf = $ts.dut.call("mesa_qos_port_dpl_conf_get", $ts.dut.p[$egr_port], dpl_cnt)
    dconf[0]["pcp"] = dconf[1]["pcp"] = (0..7).to_a
    dconf[0]["dei"] = Array.new(8, 0)
    dconf[1]["dei"] = Array.new(8, 1)
    $ts.dut.call("mesa_qos_port_dpl_conf_set", $ts.dut.p[$egr_port], dpl_cnt, dconf)

    t_i("Configure egress prio and dpl tagging to mapped.")
    qconf = qos_port_conf_get($egr_port)
    qconf["tag"]["remark_mode"] = "MESA_TAG_REMARK_MODE_MAPPED"
    qos_port_conf_set($egr_port, qconf)

end

def dscp_translate_test_func(cfg)
    t_i("Test DSCP translate with cfg = #{cfg}")
    configure_dscp_translate_table()

    config_port_dscp($igr_port, translate: cfg[:translate],
                                mode: fld_get(cfg, :mode, "MESA_DSCP_MODE_NONE"))
    config_port_dscp($egr_port, emode: fld_get(cfg, :emode, "MESA_DSCP_EMODE_DISABLE"))

    send_frames(cfg[:rx])
end

# DSCP to queue and DPL mapping, optionally remarked back to DSCP on egress
def dscp_map_test_func(cfg)
    # Egress tag expected per DSCP once queue and DPL mapping is enabled
    tags = { 0  => { pcp: 1, dei: 0 },
             32 => { pcp: 0, dei: 0 },
             63 => { pcp: 3, dei: 1 } }

    baseline_no_mapping_test()

    t_i("Test DSCP mapping with cfg = #{cfg}")
    $ts.dut.run("mesa-cmd port statis clear")

    config_port_dscp($igr_port, class_enable: true,
                                mode: fld_get(cfg, :mode, "MESA_DSCP_MODE_NONE"))

    configure_dscp_queue_dpl_map(cfg[:map], fld_get(cfg, :remark, nil))

    if (cfg[:emode] != nil)
        configure_dpl_dscp_map()
        config_port_dscp($egr_port, emode: cfg[:emode])
    end

    send_frames(cfg[:rx], tags)
    check_queue_counters([0,1,3], [1,1,1])
end

def dscp_dpl_remap_test_func(cfg)
    t_i("Test DSCP and DPL mapping to DSCP")
    $ts.dut.run("mesa-cmd port statis clear")

    config_port_dscp($igr_port, class_enable: false, mode: "MESA_DSCP_MODE_NONE")

    configure_dscp_dpl_map()

    config_port_dscp($egr_port, emode: "MESA_DSCP_EMODE_REMAP")

    send_frames(cfg[:rx])
    check_queue_counters([0], [3])
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
    #$ts.dut.run("mesa-cmd deb api ai qos")
end
