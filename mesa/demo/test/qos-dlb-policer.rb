#!/usr/bin/env ruby

# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

require_relative 'libeasy/et'
require_relative 'libeasy/utils'

$ts = get_test_setup("mesa_pc_b2b_2x")

################################################
# Capability & Configuration
################################################

check_capabilities() do
    assert((cap_get("L2_XDLB") != 0) && (cap_get("L2_XFLOW") != 0), "Dual leaky Bucket it not supported")
end

$ig = 0
$eg = 1
$class_cnt, $cosid = (cap_get("QOS_COSID_CLASSIFICATION") == 0) ? [1, 0] : [3, 1]

t_i("$class_cnt = #{$class_cnt}, $cosid = #{$cosid}")

################################################
# Test Tables
################################################

test_table = 
[
    {
        txt: "DLB Policer Disabled",
        cfg: {  rate: 1_000_000, etolerance: [3.2] },
        fun: -> (t) {
            dlb_policer_test(t[:cfg])
        }
    },
    {
        txt: "DLB Policer Enabled: Line Rate 0",
        cfg: {  rate: 0, etolerance: [200] },
        dlb: {  enable: true },
        fun: -> (t) {
            config_dlb_policer(t[:dlb])
            dlb_policer_test(t[:cfg])
        }
    },
    {
        txt: "DLB Policer Single Bucket: Line Rate 30 Mbps (CIR: 30, EIR: 0)",
        cfg: {  rate: 30_000, with_pre_tx: true },
        dlb: {  enable: true, line_rate: true, type: "MESA_POLICER_TYPE_SINGLE",
                cir: 30_000, cbs: 2048 },
        fun: -> (t) {
            config_dlb_policer(t[:dlb])
            dlb_policer_test(t[:cfg])
        }
    },
    {
        txt: "DLB Policer Dual Bucket: Line Rate 40 Mbps (CIR: 30, EIR: 10)",
        cfg: {  rate: 40_000, with_pre_tx: true },
        dlb: {  enable: true, line_rate: true, type: "MESA_POLICER_TYPE_MEF",
                cir: 30_000, cbs: 2048, eir: 10_000, ebs: 2048 },
        fun: -> (t) {
            config_dlb_policer(t[:dlb])
            $ts.dut.call("mesa_egress_cnt_clear", $estat, $cosid)
            dlb_policer_test(t[:cfg])
            cnt = $ts.dut.call("mesa_egress_cnt_get", $estat, $cosid)
            check_green_yellow_ratio(cnt)
        }
    },
    {
        txt: "DLB Policer Single Bucket: Burst size too small, Line Rate 30 Mbps (CIR: 30, CBS: 1000 -> 2048)",
        cfg: {  rate: 30_000, with_pre_tx: true },
        dlb: {  enable: true, line_rate: true, type: "MESA_POLICER_TYPE_SINGLE",
                cir: 30_000, cbs: 1000 },
        fun: -> (t) {
            return test_skip() if (cap_get("MISC_CHIP_FAMILY") != chip_family_to_id("MESA_CHIP_FAMILY_SPARX5"))

            config_dlb_policer(t[:dlb])
            validate_dlb_cir_cbs(t[:dlb][:cir], cbs: 2048)
            dlb_policer_test(t[:cfg])
        }
    },
    {
        txt: "DLB Policer Single Bucket: Burst size too large, Line Rate 10 Mbps (CIR: 10, CBS: 3M -> unchanged)",
        cfg: {  rate: 10_000, with_pre_tx: true },
        dlb: {  enable: true, line_rate: true, type: "MESA_POLICER_TYPE_SINGLE",
                cir: 10_000, cbs: 3_000_000 },
        fun: -> (t) {
            return test_skip() if (cap_get("MISC_CHIP_FAMILY") != chip_family_to_id("MESA_CHIP_FAMILY_SPARX5"))

            config_dlb_policer(t[:dlb])
            validate_dlb_cir_cbs(t[:dlb][:cir], cbs: t[:dlb][:cbs])
            dlb_policer_test(t[:cfg])
        }
    },
    {
        txt: "DLB Policer Single Bucket: Burst size too large, Line Rate 300 kbps (CIR: 0.3, CBS: 5M -> 4.19M)",
        cfg: {  rate: 300, etolerance: [25], with_pre_tx: true, sec: 2 },
        dlb: {  enable: true, line_rate: true, type: "MESA_POLICER_TYPE_SINGLE",
                cir: 300, cbs: 5_000_000 },
        fun: -> (t) {
            return test_skip() if (cap_get("MISC_CHIP_FAMILY") != chip_family_to_id("MESA_CHIP_FAMILY_SPARX5"))

            config_dlb_policer(t[:dlb])
            validate_dlb_cir_cbs(t[:dlb][:cir], cbs: 4_190_208)
            dlb_policer_test(t[:cfg])
        }
    },
]

################################################
# General/Helper Functions
################################################

def config_dlb_policer(cfg = {})
    t_i("Get the DLB policer conf, apply the given overrides and set it back. Returns the conf")
    conf = $ts.dut.call("mesa_dlb_policer_conf_get", $pol, $cosid)
    cfg.each { |k, v| conf[k.to_s] = v }
    $ts.dut.call("mesa_dlb_policer_conf_set", $pol, $cosid, conf)
    return conf
end

def check_green_yellow_ratio(cnt, ratio = 3.0, tolerance = 0.05)
    t_i("Check that the green/yellow frame count ratio matches the expected ratio within tolerance")
    green = cnt["tx_green"]["frames"]
    yellow = cnt["tx_yellow"]["frames"]

    if (yellow == 0)
        t_e("No yellow frame transmitted")
        return
    end

    factor = green.to_f / yellow.to_f
    if ((factor > ratio + tolerance) || (factor < ratio - tolerance))
        t_e("Unexpected number of yellow frame transmitted, Green = #{green}, Yellow = #{yellow}")
    end

    t_i("Sucess: Green/Yellow ratio")
end

def validate_dlb_cir_cbs(cir_expect, cbs: nil)
    t_i("Check that the resulting CIR/CBS match expected values")
    conf = $ts.dut.call("mesa_dlb_policer_conf_get", $pol, $cosid)
    if (conf["cir"] != cir_expect) || (conf["cbs"] != cbs)
        t_e("Unexpected CIR #{conf["cir"]} or CBS #{conf["cbs"]}")
    end
end

################################################
# Test Section
################################################

test "conf" do
    t_i("Only forward on relevant ports #{$ts.dut.p}")
    port_list = port_idx_list_str([$ig, $eg])
    $ts.dut.call("mesa_vlan_port_members_set", 1, port_list)

    t_i("Enable ingress tag pcp mapping")
    conf = $ts.dut.call("mesa_qos_port_conf_get", $ts.dut.p[$ig])
    conf["tag"]["class_enable"] = true
    conf["default_prio"] = $cosid
    conf["cosid"] = $cosid
    conf["default_dpl"] = 0
    $ts.dut.call("mesa_qos_port_conf_set", $ts.dut.p[$ig], conf)

    t_i("Allocate resources")
    iflow = $ts.dut.call("mesa_iflow_alloc")
    $pol = $ts.dut.call("mesa_dlb_policer_alloc", $class_cnt)
    eflow = $ts.dut.call("mesa_eflow_alloc")
    $estat = $ts.dut.call("mesa_egress_cnt_alloc", $class_cnt)

    t_i("Configure IFLOW to point to policer")
    conf = $ts.dut.call("mesa_iflow_conf_get", iflow)
    conf["dlb_enable"] = true
    conf["dlb_id"] = $pol
    $ts.dut.call("mesa_iflow_conf_set", iflow, conf)

    t_i("Configure EFLOW to point to counter set")
    conf = $ts.dut.call("mesa_eflow_conf_get", eflow)
    conf["cnt_enable"] = true
    conf["cnt_id"] = $estat
    $ts.dut.call("mesa_eflow_conf_set", eflow, conf)

    t_i("Ingress configuration VCE pointing untagged frames to policer")
    vce = $ts.dut.call("mesa_vce_init", "MESA_VCE_TYPE_ANY")
    vce["id"] = 1
    key = vce["key"]
    key["port_list"] = "#{$ts.dut.port_list[$ig]}"
    tag = key["tag"]
    tag["tagged"] = "MESA_VCAP_BIT_0"
    tag["s_tag"] = "MESA_VCAP_BIT_ANY"
    tag["vid"]["value"] = 0
    tag["vid"]["mask"] = 0
    tag["dei"] = "MESA_VCAP_BIT_ANY"
    tag["pcp"]["mask"] = 0
    action = vce["action"]
    action["flow_id"] = iflow
    $ts.dut.call("mesa_vce_add", 0, vce)

    t_i("Egress configuration TCE pointing to counter set")
    tce = $ts.dut.call("mesa_tce_init")
    tce["id"] = 1;
    tce["key"]["port_list"] = "#{$ts.dut.port_list[$eg]}"
    tce["action"]["flow_id"] = eflow
    $ts.dut.call("mesa_tce_add", 0, tce)
end

def dlb_policer_test(cfg)
    t_i("Test: DLB Policer with cfg = #{cfg}")

    erate = [cfg[:rate] * 1000]
    etol = fld_get(cfg, :etolerance, [1])
    size = fld_get(cfg, :size, 1000)
    sec = fld_get(cfg, :sec, 1)
    with_pre_tx = fld_get(cfg, :with_pre_tx, false)

    check_rate({ ig: [$ig], eg: $eg, size: size, sec: sec, erate: erate, etolerance: etol, with_pre_tx: with_pre_tx })
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
    #$ts.dut.run("mesa-cmd deb api ai vx")
end
