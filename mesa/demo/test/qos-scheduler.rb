#!/usr/bin/env ruby

# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

require_relative 'libeasy/et'

$ts = get_test_setup("mesa_pc_b2b_4x")

idx_list = $ts.pc.p.each_index.to_a
eg = idx_list[-1]
ig = (idx_list - [eg])

test_table =
[
    {
        txt: "strict-scheduling",
        cfg: {},
        chk: {erate: [0, 0, 1000000000], pcp: [0, 3, 7]},
        # Bound the leak by pcp7's own shortfall + slack, not by a fixed frame
        # count that host tx jitter trips on a fast bench.
        sp: {slack: 300},
        # On some platforms, some low priority frames are slipping through.
        # With :sp set only index 2 (pcp7, percent) is used.
        tol: {ca: [220, 305, 2],
              oc: [340, 380, 2],
              ma: [260, 500, 2],
              fa: [295, 535, 2],
              la: [10, 600, 2],
              df: [0, 700, 2]},
    },
    {
        txt: "weighted-scheduling-30-30-30",
        cfg: {dwrr: [30, 30, 30]},
        chk: {erate: [1000000000/3, 1000000000/3, 1000000000/3], pcp: [0, 1, 2]},
        tol: {ca: [1.2, 1.2, 1.2],
              oc: [1, 1, 1],
              df: [0.5, 0.5, 0.5]},
    },
    {
        txt: "weighted-scheduling-10-30-60",
        cfg: {dwrr: [10, 30, 60]},
        chk: {erate: [1000000000*1/10, 1000000000*3/10, 1000000000*6/10], pcp: [0, 1, 2]},
        tol: {ca: [4, 7.2, 5.3],
              oc: [1, 1, 1],
              df: [0.5, 0.5, 0.5]},
    },
    {
        txt: "weighted-scheduling-frame-10-30-60",
        cap: true,
        cfg: {dwrr: [10, 30, 60], frame_rate: true},
        chk: {size: [64, 512, 1024], pcp: [0, 1, 2]},
        tol: {j2: [2, 2, 2],
              df: [0.5, 0.5, 0.5]},
    },
    {
        txt: "weighted-scheduling-frame-2-3",
        cap: true,
        pop: true,
        cfg: {dwrr: [2, 3], frame_rate: true},
        chk: {size: [64, 1500], pcp: [0, 1]},
        tol: {j2: [5, 5, 5],
              df: [0.5, 0.5]},
    },
]

def run_test(t)
    ig = t[:ig].dup
    eg = t[:eg]
    if (t[:pop])
        ig.pop
    end
    t_i("ig: #{ig}, eg: #{eg}")

    # Egress port configuration
    cfg = t[:cfg]
    port = $ts.dut.p[eg]
    c = $ts.dut.call("mesa_qos_port_conf_get", port)
    dwrr = fld_get(cfg, :dwrr, [])
    cnt = dwrr.size
    c["dwrr_enable"] = (cnt > 0)
    frame_rate = fld_get(cfg, :frame_rate, false)
    c["dwrr_mode"] = ("MESA_DWWR_MODE_" + (frame_rate ? "FRAME" : "LINE"))
    c["dwrr_cnt"] = cnt
    cnt.times do |i|
        c["queue"][i]["pct"] = dwrr[i]
    end
    c = $ts.dut.call("mesa_qos_port_conf_set", port, c)

    # Tolerance per family, with a default value. Only add a family entry when it
    # needs to be looser than :df -- never tighter, or the bench jitter trips it.
    tol = t[:tol]
    etol = fld_get(tol, :df)
    case chip_id_to_family(cap_get("MISC_CHIP_FAMILY"))
    when "MESA_CHIP_FAMILY_CARACAL"
        etol = fld_get(tol, :ca, etol)
    when "MESA_CHIP_FAMILY_OCELOT"
        etol = fld_get(tol, :oc, etol)
    when "MESA_CHIP_FAMILY_LAN966X"
        etol = fld_get(tol, :ma, etol)
    when "MESA_CHIP_FAMILY_SPARX5"
        etol = fld_get(tol, :fa, etol)
    when "MESA_CHIP_FAMILY_LAN969X"
        etol = fld_get(tol, :la, etol)
    else
        # Jaguar-2/Serval-T
        etol = fld_get(tol, :j2, etol)
    end

    # Rate check
    chk = fld_get(t, :chk)
    c = {}
    c[:ig] = ig
    c[:eg] = eg
    c[:size] = 1000
    c[:with_pre_tx] = true
    c[:frame_rate] = frame_rate
    erate = chk[:erate]
    if (frame_rate)
        # Calculate the number of frames received in one second
        size = fld_get(chk, :size, [])
        sum = 0
        cnt.times do |i|
            sum += ((size[i] + 20) * dwrr[i])
        end
        erate = []
        cnt.times do |i|
            erate[i] = ((1000000000 * dwrr[i]) / (sum * 8))
        end
        c[:size_array] = size
    end
    c[:erate] = erate
    c[:etolerance] = etol
    c[:pcp] = chk[:pcp]
    sp = fld_get(t, :sp, nil)
    if (sp != nil)
        c[:strict_priority] = true
        c[:sp_slack] = fld_get(sp, :slack, 0)
    end
    check_rate(c)
end

# Initial configuration
test "config" do
    t_i ("Only forward on relevant ports #{$ts.dut.p}")
    port_list = port_idx_list_str(idx_list)
    $ts.dut.call("mesa_vlan_port_members_set", 1, port_list)

    t_i ("Configure ingress ports to C tag aware")
    ig.each do |i|
        port = $ts.dut.p[i]
        $ts.dut.run("mesa-cmd port flow control #{port + 1} disable")

        c = $ts.dut.call("mesa_vlan_port_conf_get", port)
        c["port_type"] = "MESA_VLAN_PORT_TYPE_C"
        $ts.dut.call("mesa_vlan_port_conf_set", port, c)

        c = $ts.dut.call("mesa_qos_port_conf_get", port)
        c["tag"]["class_enable"] = true
        c["tag"]["pcp_dei_map"][0][0]["prio"] = 0
        c["tag"]["pcp_dei_map"][0][0]["dpl"] = 0
        c["tag"]["pcp_dei_map"][0][1]["prio"] = 0
        c["tag"]["pcp_dei_map"][0][1]["dpl"] = 1
        c["tag"]["pcp_dei_map"][1][0]["prio"] = 1
        c["tag"]["pcp_dei_map"][1][0]["dpl"] = 0
        c["tag"]["pcp_dei_map"][1][1]["prio"] = 1
        c["tag"]["pcp_dei_map"][1][1]["dpl"] = 1
        c["default_prio"] = i
        c["default_dpl"] = 0
        $ts.dut.call("mesa_qos_port_conf_set", port, c)
    end
    sleep(5)
    dut_port_state_up(ig)

    t_i ("Configure egress port to C tag all")
    port = $ts.dut.p[eg]
    c = $ts.dut.call("mesa_vlan_port_conf_get", port)
    c["port_type"] = "MESA_VLAN_PORT_TYPE_C"
    c["untagged_vid"] = 0
    $ts.dut.call("mesa_vlan_port_conf_set", port, c)

    t_i("Configure egress prio and dpl mapping to 1:1")
    dpl_cnt = cap_get("QOS_DPL_CNT")
    c = $ts.dut.call("mesa_qos_port_dpl_conf_get", port, dpl_cnt)
    c[0]["pcp"] = [0,1,2,3,4,5,6,7]
    c[0]["dei"] = [0,0,0,0,0,0,0,0]
    c[1]["pcp"] = [0,1,2,3,4,5,6,7]
    c[1]["dei"] = [1,1,1,1,1,1,1,1]
    $ts.dut.call("mesa_qos_port_dpl_conf_set", port, dpl_cnt, c)

    t_i("Configure egress prio and dpl tagging to mapped")
    c = $ts.dut.call("mesa_qos_port_conf_get", port)
    c["tag"]["remark_mode"] = "MESA_TAG_REMARK_MODE_MAPPED"
    $ts.dut.call("mesa_qos_port_conf_set", port, c)

    if (cap_get("MISC_CHIP_FAMILY") == chip_family_to_id("MESA_CHIP_FAMILY_OCELOT"))
        # For some reason on Ocelot if flooding is not prevented tests will - by far - not pass.
        # A locked entry never ages out mid-run, the way a learned one does
        t_i("Add a static entry for the dmac check_rate transmits to")
        entry = {
            vid_mac: {vid: 1, mac: {addr: [0,0,0,0,1,1]}},
            destination: "#{$ts.dut.p[eg]}",
            copy_to_cpu: false,
            copy_to_cpu_smac: false,
            locked: true,
            index_table: false,
            aged: false,
            cpu_queue: 0,
        }
        $ts.dut.call("mesa_mac_table_add", entry)
    end
end

# Run all or selected test
sel = table_lookup(test_table, :sel)
test_table.each do |t|
    rep = fld_get(t, :rep, 1)
    rep.times do |i|
        txt = (rep == 1) ? t[:txt] : "#{t[:txt]} (#{i + 1}/#{rep})"
        test(txt) do
            if (t[:sel] != sel || (t[:cap] && cap_get("QOS_SCHEDULER_MODE_DWRR") == 0))
                test_skip
                next
            end
            t[:ig] = ig
            t[:eg] = eg
            run_test(t)
        end
    end
end

test_summary

test "dump" do
    #$ts.dut.run("mesa-cmd deb api ai qos")
end
