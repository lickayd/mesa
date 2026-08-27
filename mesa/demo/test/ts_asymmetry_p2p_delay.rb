#!/usr/bin/env ruby

# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

require_relative 'libeasy/et'
require_relative 'ts_lib'

################################################
# Capability & Configuration
################################################

$ts = get_test_setup("mesa_pc_b2b_2x")
cap_check_ts()

$port0 = 0
$port1 = 1
$vlan = 100
$pcb = $ts.dut.pcb
$exp_corr = 0

COPPER_PHY = 135

CLOCK_TO_CORR = {
    "MESA_CORE_CLOCK_250MHZ" => 2,
    "MESA_CORE_CLOCK_328MHZ" => 3,
}

FAMILY_TO_CORR = {
    chip_family_to_id("MESA_CHIP_FAMILY_JAGUAR2") => 2,
    chip_family_to_id("MESA_CHIP_FAMILY_LAN966X") => 2,
}

################################################
# Test Tables
################################################

test_table =
[
    {
        txt: "No asymmetry delay check of correction field",
        fun: -> (t) {
            no_asym_delay_check_cf()
        }
    },
    {
        txt: "Substract asymmetry delay from CF on egress",
        fun: -> (t) {
            sub_asym_delay_from_cf_egress()
        }
    },
    {
        txt: "Add asymmetry delay to CF on ingress",
        fun: -> (t) {
            add_asym_delay_to_cf_ingress()
        }
    },
    {
        txt: "Add asymmetry and p2p delay to CF on ingress",
        fun: -> (t) {
            add_asym_p2p_delay_to_cf_ingress()
        }
    }
]

################################################
# General/Helper Functions
################################################

def config_ptp_action(ptp_action, conf = nil)
    t_i("Create IS2 to #{ptp_action} frame")
    if conf.nil?
        conf = $ts.dut.call("mesa_ace_init", "MESA_ACE_TYPE_ETYPE")
        conf["id"] = 1 # ACL ID
        conf["port_list"] = "#{$ts.dut.port_list[$port0]}"
    end
    conf["action"]["ptp_action"] = ptp_action
    $ts.dut.call("mesa_ace_add", 0, conf)

    return conf
end

# Configure the IS2 rule for the ONE-STEP action (no delay) and measure the
# resulting baseline lowest correction field over the port0/port1 loop.
def corr_none_measure
    config_ptp_action("MESA_ACL_PTP_ACTION_ONE_STEP")
    return nano_corr_lowest_measure(port0: $port0, port1: $port1)
end

# Configure a delay asymmetry on both ports, as large as possible but
# smaller than 'lowest_corr_none', and return the value used.
def asymmetry_delay_configure(lowest_corr_none)
    t_i("Configure asymmetry delay. It is selected to be as large as possible but smaller than the lowest measured correction")
    asymmetry = (lowest_corr_none - 200)
    $ts.dut.call("mesa_ts_delay_asymmetry_set", $ts.dut.port_list[$port0], asymmetry<<16)
    $ts.dut.call("mesa_ts_delay_asymmetry_set", $ts.dut.port_list[$port1], asymmetry<<16)

    return asymmetry
end

# Grow the running $range with a new measured range and return the diff_max tolerance.
def corr_diff_max(range1, margin = 20)
    $range = ($range > range1) ? $range : range1
    $range += ($range / 6)
    return ($range / 2) + margin
end

# Report a correction-field check result.
def cf_check(bad_cf, msg = "Unexpected correction field including egress delay.")
    return bad_cf ? t_e(msg) : t_i("CF ok")
end

################################################
# Test Section
################################################

test "conf" do
    # disable VLAN 1 to avoid looping
    $ts.dut.call("mesa_vlan_port_members_set", 1, "")

    # Flush MAC table
    $ts.dut.call("mesa_mac_table_flush")

    # Set VLAN port configuration
    $vlan_port_conf_restore0 = $ts.dut.call("mesa_vlan_port_conf_get", $ts.dut.port_list[$port0])
    conf = $vlan_port_conf_restore0.dup
    conf["port_type"] = "MESA_VLAN_PORT_TYPE_UNAWARE"
    conf["pvid"] = $vlan
    conf["untagged_vid"] = $vlan
    conf["frame_type"] = "MESA_VLAN_FRAME_ALL"
    $ts.dut.call("mesa_vlan_port_conf_set", $ts.dut.port_list[$port0], conf)

    $vlan_port_conf_restore1 = $ts.dut.call("mesa_vlan_port_conf_get", $ts.dut.port_list[$port1])
    conf = $vlan_port_conf_restore1.dup
    conf["port_type"] = "MESA_VLAN_PORT_TYPE_UNAWARE"
    conf["pvid"] = $vlan
    conf["untagged_vid"] = $vlan
    conf["frame_type"] = "MESA_VLAN_FRAME_ALL"
    $ts.dut.call("mesa_vlan_port_conf_set", $ts.dut.port_list[$port1], conf)

    # Set VLAN memberships
    port_list = "#{$ts.dut.port_list[$port0]},#{$ts.dut.port_list[$port1]}"
    $ts.dut.call("mesa_vlan_port_members_set", $vlan, port_list)

    $asymmetry_conf_restore0 = $ts.dut.call("mesa_ts_delay_asymmetry_get", $ts.dut.port_list[$port0])
    $asymmetry_conf_restore1 = $ts.dut.call("mesa_ts_delay_asymmetry_get", $ts.dut.port_list[$port1])

    $p2p_conf_restore = $ts.dut.call("mesa_ts_p2p_delay_get", $ts.dut.port_list[$port0])
    misc = $ts.dut.call("mesa_misc_get")

    if (cap_get("INIT_CORE_CLOCK") != 0)
        $exp_corr = CLOCK_TO_CORR.fetch(misc["core_clock_freq"], 1)
    else
        $exp_corr = FAMILY_TO_CORR.fetch(cap_get("MISC_CHIP_FAMILY"), 1)
    end

    # Measure the baseline correction once and configure the asymmetry delay once, reused by all tests below
    $lowest_corr_none, $range = corr_none_measure()
    $asymmetry = asymmetry_delay_configure($lowest_corr_none)
end

def no_asym_delay_check_cf
    t_i("Test: No asymmetry delay check of correction field")
    if $pcb == COPPER_PHY
        bad_cf = ($lowest_corr_none > 3000) || ($lowest_corr_none < 1830)
    elsif ($pcb == "8281-SVB") || ($pcb == "6849-Sunrise")
        bad_cf = ($lowest_corr_none > 7010) || ($lowest_corr_none < 0)
    else
        bad_cf = ($lowest_corr_none < 0) || (($lowest_corr_none / 1000) > $exp_corr)
    end

    cf_check(bad_cf, "Unexpected CF including egress delay. lowest_corr_none = #{$lowest_corr_none}  $exp_corr = #{$exp_corr}")
end

def sub_asym_delay_from_cf_egress
    config_ptp_action("MESA_ACL_PTP_ACTION_ONE_STEP_ADD_DELAY")

    t_i("Measure lowest with asymmetry deducted")
    lowest_corr_eg, range_eg = nano_corr_lowest_measure(port0: $port0, port1: $port1)
    diff = (lowest_corr_eg - ($lowest_corr_none - $asymmetry))
    diff_max = corr_diff_max(range_eg)

    t_i("Test: The asymmetry delay is subtracted from correction on egress")
    t_i("lowest_corr_none = #{$lowest_corr_none}  lowest_corr_eg = #{lowest_corr_eg}  diff #{diff}  diff_max #{diff_max}")
    cf_check( ($lowest_corr_none < lowest_corr_eg) || (diff < -diff_max) || (diff > diff_max) )
end

def add_asym_delay_to_cf_ingress
    config_ptp_action("MESA_ACL_PTP_ACTION_ONE_STEP_SUB_DELAY_1")
    lowest_corr_ig1, range_ig1 = nano_corr_lowest_measure(port0: $port0, port1: $port1)
    diff = (lowest_corr_ig1 - ($lowest_corr_none + $asymmetry))
    diff_max = corr_diff_max(range_ig1)

    t_i("Test: The asymmetry delay is added to correction on ingress")
    t_i("lowest_corr_none = #{$lowest_corr_none}  lowest_corr_ig1 = #{lowest_corr_ig1}  diff #{diff}  diff_max #{diff_max}")
    cf_check( (lowest_corr_ig1 < $lowest_corr_none) || (diff < -diff_max) || (diff > diff_max) )
end

def add_asym_p2p_delay_to_cf_ingress
    config_ptp_action("MESA_ACL_PTP_ACTION_ONE_STEP_SUB_DELAY_1")
    lowest_corr_ig1, range_ig1 = nano_corr_lowest_measure(port0: $port0, port1: $port1)

    config_ptp_action("MESA_ACL_PTP_ACTION_ONE_STEP_SUB_DELAY_2")
    lowest_corr_ig2, range_ig2 = nano_corr_lowest_measure(port0: $port0, port1: $port1)
    diff2 = (lowest_corr_ig2 - ($lowest_corr_none + $asymmetry))
    diff_max = corr_diff_max(range_ig2, 15)

    t_i("Test: The asymmetry + p2p delay is added to correction on ingress. The p2p delay is zero at this point")
    t_i("lowest_corr_none = #{$lowest_corr_none}  lowest_corr_ig2 = #{lowest_corr_ig2}  diff #{diff2}  diff_max #{diff_max}")
    cf_check( (lowest_corr_ig2 < $lowest_corr_none) || (diff2 < -diff_max) || (diff2 > diff_max) )

    # Configure p2p delay. It is selected to be as large as possible but smaller than the lowest measured correction
    $ts.dut.call("mesa_ts_p2p_delay_set", $ts.dut.port_list[$port0], $asymmetry<<16)

    lowest_corr_ig2, range_ig2 = nano_corr_lowest_measure(port0: $port0, port1: $port1)
    diff3 = (lowest_corr_ig2 - ($lowest_corr_none + 2*$asymmetry))
    diff_max = corr_diff_max(range_ig2, 15)

    t_i("Test: The asymmetry + p2p delay is added to correction on ingress")
    cf_check( (lowest_corr_ig2 < lowest_corr_ig1) || (diff3 < -diff_max) || (diff3 > diff_max) )

    t_i("lowest_corr_none #{$lowest_corr_none}  lowest_corr_ig1 #{lowest_corr_ig1}  lowest_corr_ig2 #{lowest_corr_ig2}")
    t_i("diff_max #{diff_max}  diff2 #{diff2}  diff3 #{diff3}")
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

