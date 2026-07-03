#!/usr/bin/env ruby

# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

require_relative 'libeasy/et'

$ts = get_test_setup("mesa_pc_b2b_2x")

test_table = 
[
    {
        txt: "Shaper disabled",
        cfg: {idx: 0, rate: 0xffffffff},
        chk: {etolerance: [2.8]}
    },
    {
        txt: "Shaper frame rate 100 kpps",
        cfg: {idx: 0, frame_rate: true, level: 5, rate: 100},
        chk: {size: 600, etolerance: [3], with_pre_tx: true}
    },
    {
        txt: "Shaper frame rate 1000 kpps (1 Mpps)",
        cfg: {idx: 1, frame_rate: true, level: 10, rate: 1000},
        chk: {size: 300, etolerance: [3], with_pre_tx: true}
    },
    {
        txt: "Shaper frame rate 300000 kpps (300 Mpps)",
        cfg: {idx: 0, frame_rate: true, level: 50, rate: 300000},
        chk: {size: 300, etolerance: [3], with_pre_tx: true}
    },
    {
        txt: "Shaper line rate 400 kbps",
        cfg: {idx: 0, level: 1, rate: 400},
        chk: {sec: 4, etolerance: [3], with_pre_tx: true}
    },
    {
        txt: "Shaper line rate 100000 kbps (100 Mbps)",
        cfg: {idx: 1, level: 1, rate: 100000},
        chk: {etolerance: [3]}
    },
    {
        txt: "Shaper line rate 1000000 kbps (1 Gbps)",
        # Shaper must have large burst size level to shape correct at high rate 
        cfg: {idx: 0, level: 25000, rate: 1000000},
        chk: {etolerance: [2.7]}
    },
    {
        txt: "Shaper data rate 400 kbps",
        cfg: {idx: 0, data_rate: true, level: 1, rate: 400},
        chk: {with_pre_tx: true}
    },
    {
        txt: "Shaper data rate 100000 kbps (100 Mbps)",
        cfg: {idx: 1, data_rate: true, level: 1, rate: 100000},
    },
    {
        txt: "Shaper data rate 1000000 kbps (1 Gbps)",
        # Shaper must have large burst size level to shape correct at high rate 
        cfg: {idx: 0, data_rate: true, level: 25000, rate: 1000000},
        chk: {etolerance: [2.6]}
    }
]

def test_runner(t)
    cfg = fld_get(t, :cfg)
    chk = fld_get(t, :chk, {})
    port = $ts.dut.p[fld_get(cfg, :idx)]
    eg = cfg[:idx] == 1 ? 0 : 1
    eg_port = $ts.dut.p[eg]

    setup_ingress_port(port)
    egress_ctag_all(eg_port)

    [0,3,7].each do |queue|
        next if (configure_queue_port(cfg, eg_port, queue) == false)
        setup_chk_params(cfg, chk, queue)
        check_rate(chk)
    end
end

$ig_port = []
def setup_ingress_port(port)
    if (!$ig_port.include?(port))
        $ig_port.push(port)
        flow_control_disable(port)
        ingress_ctag_aware(port)
        ingress_tag_pcp_mapping(port)
    end
end

def flow_control_disable(port)
    $ts.dut.run("mesa-cmd port flow control #{port + 1} disable")
    sleep(5)
    dut_port_state_up([port])
end

def ingress_ctag_aware(port)
    conf = $ts.dut.call("mesa_vlan_port_conf_get", port)
    conf["port type"] = "MESA_VLAN_PORT_TYPE_C"
    $ts.dut.call("mesa_vlan_port_conf_set", port, conf)
end

def ingress_tag_pcp_mapping(port)
    conf = $ts.dut.call("mesa_qos_port_conf_get", port)
    conf["tag"]["class_enable"] = true
    conf["default_prio"] = 0
    conf["default_dpl"] = 0
    $ts.dut.call("mesa_qos_port_conf_set", port, conf)
end

$eg_port = []
def egress_ctag_all(port)
    if (!$eg_port.include?(port))
        conf = $ts.dut.call("mesa_vlan_port_conf_get", port)
        conf["port type"] = "MESA_VLAN_PORT_TYPE_C"
        conf["untagged_vid"] = 0
        $ts.dut.call("mesa_vlan_port_conf_set", port, conf)
    end
end

def configure_queue_port(cfg, port, q)
    conf = $ts.dut.call("mesa_qos_port_conf_get", port)
    conf["queue"][q]["shaper"]["level"] = fld_get(cfg, :level)
    conf["queue"][q]["shaper"]["rate"] = cfg[:rate]
    isFrameRate = fld_get(cfg, :frame_rate, false)
    isDataRate = fld_get(cfg, :data_rate, false)

    if (cfg[:rate] == 0xffffffff)
        $ts.dut.call("mesa_qos_port_conf_set", port, conf)
        return true
    end
    
    if (isFrameRate && !isDataRate && cap_get("QOS_EGRESS_SHAPER_FRAME") == 1)
        conf["queue"][q]["shaper"]["mode"] = "MESA_SHAPER_MODE_FRAME"
        $ts.dut.call("mesa_qos_port_conf_set", port, conf)
        return true
    elsif (!isFrameRate && isDataRate)
        conf["queue"][q]["shaper"]["mode"] = "MESA_SHAPER_MODE_DATA"
        $ts.dut.call("mesa_qos_port_conf_set", port, conf)
        return true
    elsif (!isFrameRate && !isDataRate)
        conf["queue"][q]["shaper"]["mode"] = "MESA_SHAPER_MODE_LINE"
        $ts.dut.call("mesa_qos_port_conf_set", port, conf)
        return true
    end

    return false
end

def setup_chk_params(cfg, chk, q)
    chk[:ig] = [cfg[:idx]]
    chk[:eg] = cfg[:idx] == 0 ? 1 : 0
    chk[:size] = fld_get(chk, :size, 1000)
    rate_multiplier = cfg[:frame_rate] ? 1 : 1000
    chk[:erate] = [cfg[:rate] * rate_multiplier]
    chk[:frame_rate] = cfg[:frame_rate]
    chk[:data_rate] = fld_get(cfg, :data_rate, false)
    chk[:pcp] = [default_cos2pcp(q)]
end

# Run all or selected test
sel = table_lookup(test_table, :sel)
test_table.each do |t|
    next if (t[:sel] != sel)
    test t[:txt] do
        test_runner(t)
    end
end

test_summary

test "dump" do
    #$ts.dut.run("mesa-cmd deb api ai qos")
end
