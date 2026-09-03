#!/usr/bin/env ruby

# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

require_relative 'libeasy/et'
require_relative 'libeasy/utils'

$ts = get_test_setup("mesa_pc_b2b_4x")

################################################
# Capability & Configuration
################################################

SUPPORTED_FAMILIES = [chip_family_to_id("MESA_CHIP_FAMILY_SPARX5"),
                      chip_family_to_id("MESA_CHIP_FAMILY_LAN969X")]

check_capabilities() do
    assert(cap_get("QOS_COSID_CLASSIFICATION") == 1, "COSID classification not supported")
    assert((cap_get("L2_XDLB") != 0) && (cap_get("L2_XFLOW") != 0), "Dual leaky Bucket it not supported")
    assert(SUPPORTED_FAMILIES.include?(cap_get("MISC_CHIP_FAMILY")), "Must be a Fireant or Laguna")
end

$cos2_cir = 30_000
$cos2_eir = 10_000
$cos1_cir = 50_000
$cos1_eir = 10_000
$cos0_cir = 70_000
$cos0_eir = 10_000

$class_cnt = 3

$igCos0 = 0
$igCos1 = 1
$igCos2 = 2
$eg     = 3

# CIR sharing, 50% inherit: what policers 0,1,2 may pull from the ones below them in the chain
$pol_inherit_cir = [(($cos1_cir + $cos2_cir) / 100) * 50, ($cos2_cir / 100) * 50, 0]

# CIR sharing, MAX inherit: each policer may pull everything below it in the chain
$pol_inherit_cir_max = [$cos1_cir + $cos2_cir, $cos2_cir, 0]

# EIR sharing, 50% inherit: the same chain applied to the EIR buckets
$pol_inherit_eir = [(($cos1_eir + $cos2_eir) / 100) * 50, ($cos2_eir / 100) * 50, 0]

# Coupled variant: policer 2 pulls 50% of all three CIRs into its EIR
$pol_inherit_eir_all_cir = [0, 0, (($cos0_cir + $cos1_cir + $cos2_cir) * 50) / 100]

################################################
# Test Tables
################################################

test_table =
[
    {
        txt: "DLB no sharing - COS 0 Traffic",
        cfg: { cosid: 0, cir: $cos0_cir, eir: $cos0_eir },
        fun: -> (t) { dlb_no_sharing_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "DLB no sharing - COS 1 Traffic",
        cfg: { cosid: 1, cir: $cos1_cir, eir: $cos1_eir },
        fun: -> (t) { dlb_no_sharing_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "DLB no sharing - COS 2 Traffic",
        cfg: { cosid: 2, cir: $cos2_cir, eir: $cos2_eir },
        fun: -> (t) { dlb_no_sharing_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "DLB CIR sharing 50% - COS 0 Traffic",
        cfg: { pol_inherit_cir: $pol_inherit_cir, cosid: [0], cir: [$cos0_cir], eir: [$cos0_eir], inherit: [$pol_inherit_cir[0]] },
        fun: -> (t) { dlb_cir_sharing_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "DLB CIR sharing 50% - COS 1 Traffic",
        cfg: { pol_inherit_cir: $pol_inherit_cir, cosid: [1], cir: [$cos1_cir], eir: [$cos1_eir], inherit: [$pol_inherit_cir[1]] },
        fun: -> (t) { dlb_cir_sharing_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "DLB CIR sharing 50% - COS 2 Traffic",
        cfg: { pol_inherit_cir: $pol_inherit_cir, cosid: [2], cir: [$cos2_cir], eir: [$cos2_eir], inherit: [$pol_inherit_cir[2]] },
        fun: -> (t) { dlb_cir_sharing_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "DLB CIR sharing 50% - COS 0,2 Traffic",
        cfg: { pol_inherit_cir: $pol_inherit_cir, cosid: [0, 2], cir: [$cos0_cir, $cos2_cir], eir: [$cos0_eir, $cos2_eir],
                inherit: [($cos1_cir > $pol_inherit_cir[0]) ? $pol_inherit_cir[0] : $cos1_cir, $pol_inherit_cir[2]] },
        chk: { etolerance: [1, 1], pcp: [0, 2] },
        fun: -> (t) { dlb_cir_sharing_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "DLB CIR sharing 50% - COS 0,1 Traffic",
        cfg: { pol_inherit_cir: $pol_inherit_cir, cosid: [0, 1], cir: [$cos0_cir, $cos1_cir], eir: [$cos0_eir, $cos1_eir],
                inherit: [$cos2_cir - $pol_inherit_cir[1], $pol_inherit_cir[1]] },
        chk: { etolerance: [1, 1], pcp: [0, 1] },
        fun: -> (t) { dlb_cir_sharing_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "DLB CIR sharing 50% - COS 0,1,2 Traffic",
        cfg: { pol_inherit_cir: $pol_inherit_cir, cosid: [0, 1, 2], cir: [$cos0_cir, $cos1_cir, $cos2_cir],
                eir: [$cos0_eir, $cos1_eir, $cos2_eir], inherit: [0, 0, $pol_inherit_cir[2]] },
        chk: { etolerance: [1, 1, 1], pcp: [0, 1, 2] },
        fun: -> (t) { dlb_cir_sharing_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "DLB CIR sharing 50% EIR share - COS 2 Traffic",
        cfg: { pol_inherit_cir: $pol_inherit_cir, pol_inherit_eir: $pol_inherit_eir_all_cir, dpl: 1,
                cosid: [2], cir: [0], eir: [$cos2_eir], inherit: [$pol_inherit_eir_all_cir[2]] },
        fun: -> (t) { dlb_cir_sharing_50pct_eir_share_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "DLB CIR sharing 50% CF on COS 2 - COS 0 Traffic",
        cfg: { pol_inherit_cir: $pol_inherit_cir, pol_cf: [false, false, true],cosid: [0], cir: [$cos0_cir],
                eir: [$cos0_eir], inherit: [($cos1_cir > $pol_inherit_cir[0]) ? $pol_inherit_cir[0] : $cos1_cir] },
        fun: -> (t) { dlb_cir_sharing_cf_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "DLB CIR sharing 50% CF on COS 1,2 - COS 0 Traffic",
        cfg: { pol_inherit_cir: $pol_inherit_cir, pol_cf: [false, true, true],
               cosid: [0], cir: [$cos0_cir], eir: [$cos0_eir], inherit: [0] },
        fun: -> (t) { dlb_cir_sharing_cf_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "DLB CIR sharing MAX - COS 0 Traffic",
        cfg: { pol_inherit_cir: $pol_inherit_cir_max, cosid: [0], cir: [$cos0_cir], eir: [$cos0_eir],
                inherit: [$pol_inherit_cir_max[0]] },
        chk: { etolerance: [10] },
        fun: -> (t) { dlb_cir_sharing_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "DLB CIR sharing MAX - COS 1 Traffic",
        cfg: { pol_inherit_cir: $pol_inherit_cir_max, cosid: [1], cir: [$cos1_cir], eir: [$cos1_eir],
                inherit: [$pol_inherit_cir_max[1]] },
        fun: -> (t) { dlb_cir_sharing_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "DLB CIR sharing MAX - COS 2 Traffic",
        cfg: { pol_inherit_cir: $pol_inherit_cir_max, cosid: [2], cir: [$cos2_cir], eir: [$cos2_eir],
                inherit: [$pol_inherit_cir_max[2]] },
        fun: -> (t) { dlb_cir_sharing_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "DLB CIR sharing MAX - COS 0,2 Traffic",
        cfg: { pol_inherit_cir: $pol_inherit_cir_max, cosid: [0, 2], cir: [$cos0_cir, $cos2_cir], eir: [$cos0_eir, $cos2_eir],
                inherit: [$cos1_cir, $pol_inherit_cir_max[2]] },
        chk: { etolerance: [1, 1], pcp: [0, 2] },
        fun: -> (t) { dlb_cir_sharing_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "DLB CIR sharing MAX - COS 0,1 Traffic",
        cfg: { pol_inherit_cir: $pol_inherit_cir_max, cosid: [0, 1], cir: [$cos0_cir, $cos1_cir], eir: [$cos0_eir, $cos1_eir],
                inherit: [0, $pol_inherit_cir_max[1]] },
        chk: { etolerance: [1, 1], pcp: [0, 1] },
        fun: -> (t) { dlb_cir_sharing_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "DLB CIR sharing MAX - COS 0,1,2 Traffic",
        cfg: { pol_inherit_cir: $pol_inherit_cir_max, cosid: [0, 1, 2], cir: [$cos0_cir, $cos1_cir, $cos2_cir],
                eir: [$cos0_eir, $cos1_eir, $cos2_eir], inherit: [0, 0, $pol_inherit_cir_max[2]] },
        chk: { etolerance: [1, 1, 1], pcp: [0, 1, 2] },
        fun: -> (t) { dlb_cir_sharing_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "DLB EIR sharing 50% - COS 0 Traffic",
        cfg: { pol_inherit_eir: $pol_inherit_eir, cosid: [0], cir: [$cos0_cir], eir: [$cos0_eir], inherit: [$pol_inherit_eir[0]] },
        fun: -> (t) { dlb_eir_sharing_50pct_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "DLB EIR sharing 50% - COS 1 Traffic",
        cfg: { pol_inherit_eir: $pol_inherit_eir, cosid: [1], cir: [$cos1_cir], eir: [$cos1_eir], inherit: [$pol_inherit_eir[1]] },
        fun: -> (t) { dlb_eir_sharing_50pct_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "DLB EIR sharing 50% - COS 2 Traffic",
        cfg: { pol_inherit_eir: $pol_inherit_eir, cosid: [2], cir: [$cos2_cir], eir: [$cos2_eir], inherit: [$pol_inherit_eir[2]] },
        fun: -> (t) { dlb_eir_sharing_50pct_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "DLB EIR sharing 50% - COS 0,2 Traffic",
        cfg: { pol_inherit_eir: $pol_inherit_eir, cosid: [0, 2], cir: [$cos0_cir, $cos2_cir], eir: [$cos0_eir, $cos2_eir],
                inherit: [($cos1_eir > $pol_inherit_eir[0]) ? $pol_inherit_eir[0] : $cos1_eir, $pol_inherit_eir[2]] },
        chk: { etolerance: [1, 1], pcp: [0, 2] },
        fun: -> (t) { dlb_eir_sharing_50pct_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "DLB EIR sharing 50% - COS 0,1 Traffic",
        cfg: { pol_inherit_eir: $pol_inherit_eir, cosid: [0, 1], cir: [$cos0_cir, $cos1_cir], eir: [$cos0_eir, $cos1_eir],
                inherit: [$cos2_eir - $pol_inherit_eir[1], $pol_inherit_eir[1]] },
        chk: { etolerance: [1, 1], pcp: [0, 1] },
        fun: -> (t) { dlb_eir_sharing_50pct_test(t[:cfg], t[:chk]) }
    },
    {
        txt: "DLB EIR sharing 50% - COS 0,1,2 Traffic",
        cfg: { pol_inherit_eir: $pol_inherit_eir, cosid: [0, 1, 2], cir: [$cos0_cir, $cos1_cir, $cos2_cir],
                eir: [$cos0_eir, $cos1_eir, $cos2_eir], inherit: [0, 0, $pol_inherit_eir[2]] },
        chk: { etolerance: [1, 1, 1], pcp: [0, 1, 2] },
        fun: -> (t) { dlb_eir_sharing_50pct_test(t[:cfg], t[:chk]) }
    },
]

################################################
# General/Helper Functions
################################################

def qos_port_cosid_set(port_idx, cosid)
    conf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[port_idx])
    conf["cosid"] = cosid
    conf["default_prio"] = cosid
    conf["default_dpl"] = 0
    $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[port_idx], conf)
end

def qos_port_dpl_set(port_idx, dpl)
    conf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[port_idx])
    conf["default_dpl"] = dpl
    $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[port_idx], conf)
end

def dlb_policer_get(cosid)
    $ts.dut.call("mesa_dlb_policer_conf_get", $pol, cosid)
end

def dlb_policer_set(cosid, fields)
    conf = dlb_policer_get(cosid)
    fields.each { |k, v| conf[k.to_s] = v }
    $ts.dut.call("mesa_dlb_policer_conf_set", $pol, cosid, conf)
end

# Full, known-good baseline for one policer - every test row starts from this so rows
# never depend on state left behind by another row.
def dlb_policer_baseline(cosid, cir, eir)
    dlb_policer_set( cosid, enable: true, line_rate: true, type: "MESA_POLICER_TYPE_MEF",
        cir: cir, cbs: 2048, eir: eir, ebs: 2048, share_cir: false, share_eir: false,
        inherit_cir: 0, inherit_eir: 0, cf: false, cm: false )
end

def dlb_baseline_all
    dlb_policer_baseline(0, $cos0_cir, $cos0_eir)
    dlb_policer_baseline(1, $cos1_cir, $cos1_eir)
    dlb_policer_baseline(2, $cos2_cir, $cos2_eir)
end

# Shared setup for the rows built on "CIR sharing, 50% inherit"
def dlb_cir_sharing_setup(inherit_cir)
    dlb_baseline_all()
    dlb_policer_set(0, share_cir: false, inherit_cir: inherit_cir[0])
    dlb_policer_set(1, share_cir: true, inherit_cir: inherit_cir[1])
    dlb_policer_set(2, share_cir: true, inherit_cir: inherit_cir[2])
end

# Shared setup for the rows built on "EIR sharing, 50% inherit"
def dlb_eir_sharing_50pct_setup(inherit_eir)
    dlb_baseline_all()
    dlb_policer_set(0, share_eir: false, inherit_eir: inherit_eir[0])
    dlb_policer_set(1, share_eir: true, inherit_eir: inherit_eir[1])
    dlb_policer_set(2, share_eir: true, inherit_eir: inherit_eir[2])
end

################################################
# Test Section
################################################
test "conf" do
    t_i("Only forward on relevant ports #{$ts.dut.p}")
    port_list = port_idx_list_str([$igCos0, $igCos1, $igCos2, $eg])
    $ts.dut.call("mesa_vlan_port_members_set", 1, port_list)

    t_i("Configure default COSID on ingress ports")
    qos_port_cosid_set($igCos0, 0)
    qos_port_cosid_set($igCos1, 1)
    qos_port_cosid_set($igCos2, 2)

    t_i("Allocate resources")
    $iflow = $ts.dut.call("mesa_iflow_alloc")
    $pol = $ts.dut.call("mesa_dlb_policer_alloc", $class_cnt)
    $eflow = $ts.dut.call("mesa_eflow_alloc")
    $estat = $ts.dut.call("mesa_egress_cnt_alloc", $class_cnt)
    $istat = $ts.dut.call("mesa_ingress_cnt_alloc", $class_cnt)

    t_i("Configure IFLOW to point to policer")
    conf = $ts.dut.call("mesa_iflow_conf_get", $iflow)
    conf["dlb_enable"] = true
    conf["dlb_id"] = $pol
    conf["cnt_enable"] = true
    conf["cnt_id"] = $istat
    $ts.dut.call("mesa_iflow_conf_set", $iflow, conf)

    t_i("Configure EFLOW to point to counter set")
    conf = $ts.dut.call("mesa_eflow_conf_get", $eflow)
    conf["cnt_enable"] = true
    conf["cnt_id"] = $estat
    $ts.dut.call("mesa_eflow_conf_set", $eflow, conf)

    t_i("Ingress configuration VCE pointing untagged frames to policer")
    vce = $ts.dut.call("mesa_vce_init", "MESA_VCE_TYPE_ANY")
    vce["id"] = 1
    key = vce["key"]
    key["port_list"] = "#{$ts.dut.port_list[$igCos0]},#{$ts.dut.port_list[$igCos1]},#{$ts.dut.port_list[$igCos2]}"
    tag = key["tag"]
    tag["tagged"] = "MESA_VCAP_BIT_ANY"
    tag["s_tag"] = "MESA_VCAP_BIT_ANY"
    tag["vid"]["value"] = 0
    tag["vid"]["mask"] = 0
    tag["dei"] = "MESA_VCAP_BIT_ANY"
    tag["pcp"]["mask"] = 0
    action = vce["action"]
    action["flow_id"] = $iflow
    $ts.dut.call("mesa_vce_add", 0, vce)

    t_i("Egress configuration TCE pointing to counter set")
    tce = $ts.dut.call("mesa_tce_init")
    tce["id"] = 1
    tce["key"]["port_list"] = "#{$ts.dut.port_list[$eg]}"
    tce["action"]["flow_id"] = $eflow
    tce["action"]["tag"]["tpid"] = "MESA_TPID_SEL_NONE"
    $ts.dut.call("mesa_tce_add", 0, tce)
end

def dlb_no_sharing_test(cfg, chk)
    dlb_baseline_all()

    ig = [$igCos0, $igCos1, $igCos2][cfg[:cosid]]
    erate = (cfg[:cir] + cfg[:eir]) * 1000
    etolerance = fld_get(chk, :etolerance, [1])
    pcp = fld_get(chk, :pcp, [])

    check_rate(ig: [ig], eg: $eg, size: 1000, sec: 1, erate: [erate],
        etolerance: etolerance, with_pre_tx: true, pcp: pcp)
end

def dlb_cir_sharing_test(cfg, chk)
    dlb_cir_sharing_setup(cfg[:pol_inherit_cir])

    ig = cfg[:cosid].map { |cosid| [$igCos0, $igCos1, $igCos2][cosid] }
    erate = cfg[:cosid].each_index.map { |i| (cfg[:cir][i] + cfg[:eir][i] + cfg[:inherit][i]) * 1000 }
    etolerance = fld_get(chk, :etolerance, [1])
    pcp = fld_get(chk, :pcp, [])

    check_rate(ig: ig, eg: $eg, size: 1000, sec: 1, erate: erate,
        etolerance: etolerance, with_pre_tx: true, pcp: pcp)
end

def dlb_cir_sharing_50pct_eir_share_test(cfg, chk)
    dlb_cir_sharing_setup(cfg[:pol_inherit_cir])

    t_i("COS 0 shares its CIR down and COS 2 inherits EIR from all three CIRs")
    dlb_policer_set(0, share_cir: true)
    dlb_policer_set(2, inherit_eir: cfg[:pol_inherit_eir][2], cm: true)

    ig = cfg[:cosid].map { |cosid| [$igCos0, $igCos1, $igCos2][cosid] }
    erate = cfg[:cosid].each_index.map { |i| (cfg[:cir][i] + cfg[:eir][i] + cfg[:inherit][i]) * 1000 }
    etolerance = fld_get(chk, :etolerance, [1])
    pcp = fld_get(chk, :pcp, [])

    t_i("Configure traffic to DPL #{cfg[:dpl]}")
    ig.each { |port| qos_port_dpl_set(port, cfg[:dpl]) }

    check_rate(ig: ig, eg: $eg, size: 1000, sec: 1, erate: erate,
        etolerance: etolerance, with_pre_tx: true, pcp: pcp)

    t_i("Configure traffic back to all green")
    ig.each { |port| qos_port_dpl_set(port, 0) }
end

def dlb_cir_sharing_cf_test(cfg, chk)
    dlb_cir_sharing_setup(cfg[:pol_inherit_cir])

    t_i("Configure the Coupling Flag per policer: #{cfg[:pol_cf]}")
    cfg[:pol_cf].each_with_index { |cf, cosid| dlb_policer_set(cosid, cf: cf) }

    ig = cfg[:cosid].map { |cosid| [$igCos0, $igCos1, $igCos2][cosid] }
    erate = cfg[:cosid].each_index.map { |i| (cfg[:cir][i] + cfg[:eir][i] + cfg[:inherit][i]) * 1000 }
    etolerance = fld_get(chk, :etolerance, [1])
    pcp = fld_get(chk, :pcp, [])

    check_rate(ig: ig, eg: $eg, size: 1000, sec: 1, erate: erate,
        etolerance: etolerance, with_pre_tx: true, pcp: pcp)
end

def dlb_eir_sharing_50pct_test(cfg, chk)
    dlb_eir_sharing_50pct_setup(cfg[:pol_inherit_eir])

    ig = cfg[:cosid].map { |cosid| [$igCos0, $igCos1, $igCos2][cosid] }
    erate = cfg[:cosid].each_index.map { |i| (cfg[:cir][i] + cfg[:eir][i] + cfg[:inherit][i]) * 1000 }
    etolerance = fld_get(chk, :etolerance, [1])
    pcp = fld_get(chk, :pcp, [])

    check_rate(ig: ig, eg: $eg, size: 1000, sec: 1, erate: erate,
        etolerance: etolerance, with_pre_tx: true, pcp: pcp)
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
    #$ts.dut.run("mesa-cmd deb api cil qos full act 17")
    #$ts.dut.run("mesa-cmd deb api cil qos full act 16")
end
