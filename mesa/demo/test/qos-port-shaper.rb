#!/usr/bin/env ruby

# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

require_relative 'libeasy/et'

$ts = get_test_setup("mesa_pc_b2b_2x")

test_table = 
[
    {
        txt: "Shaper port disabled",
        cfg: {idx: 0, rate: 0xffffffff},
        chk: {etolerance: [2], with_pre_tx: false}
    },
    {
        txt: "Shaper port frame rate 100 kpps",
        cfg: {idx: 0, frame_rate: true, rate: 100},
        chk: {size: 600, etolerance: [3]}
    },
    {
        txt: "Shaper port frame rate 1000 kpps (1 Mpps)",
        cfg: {idx: 1, frame_rate: true, rate: 1000},
        chk: {size: 600, etolerance: [3]}
    },
    {
        txt: "Shaper port frame rate 50000 kpps (50 Mpps)",
        cfg: {idx: 0, frame_rate: true, rate: 50_000},
        chk: {etolerance: [3]}
    },
    {
        txt: "Shaper port line rate 400 kbps",
        cfg: {idx: 0, level: 1, rate: 400},
        chk: {sec: 2, etolerance: [4]}
    },
    {
        txt: "Shaper port line rate 1200 kbps (1.2 Mbps)",
        cfg: {idx: 0, level: 1, rate: 1200},
        chk: {etolerance: [3]}
    },
    {
        txt: "Shaper port line rate 10000 kbps (10 Mbps)",
        cfg: {idx: 1, level: 1, rate: 10000},
        chk: {etolerance: [3]}
    },
    {
        txt: "Shaper port line rate 100000 kbps (100 Mbps)",
        cfg: {idx: 1, level: 1, rate: 100000},
        chk: {etolerance: [3]}
    },
    {
        txt: "Shaper port line rate 1000000 kbps (1 Gbps)",
        cfg: {idx: 0, level: 25000, rate: 1000000},
        chk: {etolerance: [2]}
    },
    {
        txt: "Shaper port data rate 400 kbps",
        cfg: {idx: 0, data_rate: true, level: 1, rate: 400},
    },
    {
        txt: "Shaper port data rate 1200 kbps (1.2 Mbps)",
        cfg: {idx: 1, data_rate: true, level: 1, rate: 1200},
    },
    {
        txt: "Shaper port data rate 10000 kbps (10 Mbps)",
        cfg: {idx: 0, data_rate: true, level: 1, rate: 10000},
        chk: {etolerance: [2.2]}
    },
    {
        txt: "Shaper port data rate 100000 kbps (100 Mbps)",
        cfg: {idx: 0, data_rate: true, level: 1, rate: 100000},
    },
    {
        txt: "Shaper port data rate 1000000 kbps (1 Gbps)",
        cfg: {idx: 1, data_rate: true, level: 25000, rate: 1000000},
        chk: {etolerance: [3]}
    }
]

def test_runner(t)
    cfg = fld_get(t, :cfg)
    chk = fld_get(t, :chk, {})
    port = $ts.dut.p[fld_get(cfg, :idx)]
    eg = cfg[:idx] == 1 ? 0 : 1
    eg_port = $ts.dut.p[eg]

    flow_control_disable(port)

    if (configure_port_shaper(cfg, eg_port) == false)
        test_skip
        return
    end

    setup_chk_params(cfg, chk)
    check_rate(chk)
end

$ig_port = []
def flow_control_disable(port)
    if (!$ig_port.include?(port))
        $ts.dut.run("mesa-cmd port flow control #{port + 1} disable")
        sleep(5)
        dut_port_state_up([port])
    end
end

def configure_port_shaper(cfg, port)
    conf = $ts.dut.call("mesa_qos_port_conf_get", port)
    conf["shaper"]["level"] = fld_get(cfg, :level)
    conf["shaper"]["rate"] = cfg[:rate]
    isFrameRate = fld_get(cfg, :frame_rate, false)
    isDataRate = fld_get(cfg, :data_rate, false)

    if (isFrameRate)
        return false unless cap_get("QOS_EGRESS_SHAPER_FRAME") == 1
        conf["shaper"]["mode"] = "MESA_SHAPER_MODE_FRAME"
    elsif (isDataRate)
        conf["shaper"]["mode"] = "MESA_SHAPER_MODE_DATA"
    elsif (cfg[:rate] != 0xffffffff)
        conf["shaper"]["mode"] = "MESA_SHAPER_MODE_LINE"
    end

    $ts.dut.call("mesa_qos_port_conf_set", port, conf)
    return true
end

def setup_chk_params(cfg, chk)
    chk[:ig] = [cfg[:idx]]
    chk[:eg] = cfg[:idx] == 0 ? 1 : 0
    chk[:size] = fld_get(chk, :size, 1000)
    rate_multiplier = cfg[:frame_rate] ? 1 : 1000
    chk[:erate] = [cfg[:rate] * rate_multiplier]
    chk[:frame_rate] = cfg[:frame_rate]
    chk[:with_pre_tx] = fld_get(chk, :with_pre_tx, true)
    chk[:data_rate] = fld_get(cfg, :data_rate, false)
end

# Run all or selected test
sel = table_lookup(test_table, :sel)
test_table.each do |t|
    next if (t[:sel] != sel)
    test t[:txt] do
        test_runner(t)
    end
end

test_summary()

test "dump" do
    #$ts.dut.run("mesa-cmd deb api ai qos")
end
