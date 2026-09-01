#!/usr/bin/env ruby

# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

require_relative 'libeasy/et'
require_relative 'ts_lib'

################################################
# Capability & Configuration
################################################

$ts = get_test_setup("mesa_pc_b2b_2x")

cfg = {ext_clk_loop: true}
cap_check_ts(cfg)

$pcb = $ts.dut.pcb
DEFAULT_DOMAIN = 0
DEFAULT_DOMAIN_API = 3

case $pcb
    when "6813-Adaro"
        $external_io_in, $external_io_out = 0, 1
    when 111
        $external_io_in, $external_io_out = 2, 0
    when "8291-EndNode"
        $external_io_in, $external_io_out = 4, 1
    when "8290"
        $external_io_in, $external_io_out = 0, 3
    when "8398"
        $external_io_in, $external_io_out = 5, 4
    else
        $external_io_in, $external_io_out = 2, 1
end
t_i("external_io_in #{$external_io_in}  external_io_out #{$external_io_out}")

if (cap_get("INIT_CORE_CLOCK") != 0)
    # Get the core clock and set the maximum frequency adjustment
    misc = $ts.dut.call("mesa_misc_get")
    case misc["core_clock_freq"]
        when "MESA_CORE_CLOCK_625MHZ"
            $adj_max, $diff_high, $diff_low = 11874999, 1190000, 1185000
        when "MESA_CORE_CLOCK_500MHZ"
            $adj_max, $diff_high, $diff_low = 9499999, 951500, 948000
        when "MESA_CORE_CLOCK_328MHZ"
            $adj_max, $diff_high, $diff_low = 6231998, 624184, 621888
        when "MESA_CORE_CLOCK_250MHZ"
            $adj_max, $diff_high, $diff_low = 4749999, 476000, 474000
    end
else
    $adj_max, $diff_high, $diff_low = 11874999, 1190000, 1185000
end
$diff_no_adj = 2
case $pcb
    when "6813-Adaro"
        $diff_no_adj, $adj_max, $diff_high, $diff_low = 8, 1200000, 120114, 119885
    when "8290", "8291"
        $diff_no_adj, $adj_max, $diff_high, $diff_low = 8, 2400000, 240114, 239885
    when "6849-Sunrise"
        $diff_no_adj, $adj_max, $diff_high, $diff_low = 12, 1300000, 130100, 129900
end
t_i("diff_high #{$diff_high}  diff_low #{$diff_low}  adj_max #{$adj_max}")


################################################
# Test Tables
################################################

tod_adj_timer_domains =
[ # [domain_out, domain_in]
    [0, 1],
    [1, 2],
    [2, 0],
    [3, 1],
]

test_table = tod_adj_timer_domains.map do |domain_out, domain_in|
    {
        cfg: {domain_out: domain_out, domain_in: domain_in},
        txt: "domain_out = #{domain_out}, domain_in = #{domain_in}",
        fun: -> (t) { tod_adj_timer_test(t[:cfg][:domain_out], t[:cfg][:domain_in]) }
    }
end

################################################
# General/Helper Functions
################################################

def check_saved_ts_diff(diff_high, diff_low = 0)
    t_i("Sub-test: check_saved_ts_diff")

    $tod0 = $ts.dut.call("mesa_ts_saved_timeofday_get", $external_io_in)
    i = 0
    (0..20).each do |i|
        $tod1 = $ts.dut.call("mesa_ts_saved_timeofday_get", $external_io_in)
        if ($tod1[0]["seconds"] == ($tod0[0]["seconds"] + 1))
            break;
        end
        if (($tod1[0]["seconds"] > ($tod0[0]["seconds"] + 1)) ||
            ($tod1[0]["seconds"] < ($tod0[0]["seconds"])))
            # When seconds has incremented more than once we take a new sample
            $tod0 = $tod1.dup
        end
        if (i == 10)
            t_e("TOD seconds not incrementing correctly")
        end
    end
    diff = $tod1[0]["nanoseconds"] - $tod0[0]["nanoseconds"]
    t_i("Difference #{diff}")
    t_e("Difference is not as expected") if ((diff.abs > diff_high) || (diff.abs < diff_low))
end

# Get the current io mode, apply the given overrides, then set it back.
def ts_external_io_mode_set(io, **cfg)
    pin_conf = $ts.dut.call("mesa_ts_external_io_mode_get", io)
    cfg.each { |k, v| pin_conf[k.to_s] = v }
    $ts.dut.call("mesa_ts_external_io_mode_set", io, pin_conf)
end

# Apply the given overrides to an already-fetched TOD struct, then set it.
def ts_domain_timeofday_set(domain, ts, **cfg)
    cfg.each { |k, v| ts[k.to_s] = v }
    $ts.dut.call("mesa_ts_domain_timeofday_set", domain, ts)
end

# Get the frequency adjustment, using the default-domain API if domain_def is set.
def ts_adjtimer_get(domain, domain_def: false)
    domain_def ? $ts.dut.call("mesa_ts_adjtimer_get") : $ts.dut.call("mesa_ts_domain_adjtimer_get", domain)
end

# Set the frequency adjustment, using the default-domain API if domain_def is set.
def ts_adjtimer_set(domain, adj, domain_def: false)
    domain_def ? $ts.dut.call("mesa_ts_adjtimer_set", adj) : $ts.dut.call("mesa_ts_domain_adjtimer_set", domain, adj)
end


################################################
# Test Section
################################################

test("conf") do
    # disable VLAN 1 to avoid looping
    $ts.dut.call("mesa_vlan_port_members_set", 1, "")

    # Frequency adjustment save
    $frequence_adjustment_restore0 = $ts.dut.call("mesa_ts_domain_adjtimer_get", 0)
    $frequence_adjustment_restore1 = $ts.dut.call("mesa_ts_domain_adjtimer_get", 1)
    $frequence_adjustment_restore2 = $ts.dut.call("mesa_ts_domain_adjtimer_get", 2)
end

def tod_adj_timer_test(domain_out, domain_in)
    t_i("Test: tod_adj_timer_test  domain = #{domain_out}")

    domain_def = (domain_out == DEFAULT_DOMAIN_API) ? true : false
    domain_out = DEFAULT_DOMAIN if (domain_def)

    t_i("Get and save current frequency adjustment")
    adjtimer = ts_adjtimer_get(domain_out, domain_def: domain_def)

    tod = $ts.dut.call("mesa_ts_domain_timeofday_get", domain_out)
    ts = tod[0]

    t_i("Configure 1PPS input pin")
    ts_external_io_mode_set($external_io_in, domain: domain_in, pin: "MESA_TS_EXT_IO_MODE_ONE_PPS_SAVE")

    t_i("Set TOD to zero for both domains")
    ts_domain_timeofday_set(domain_in, ts, seconds: 1, nanoseconds: 0)
    ts_domain_timeofday_set(domain_out, ts, seconds: 1, nanoseconds: 0)

    t_i("Configure 1PPS output pin")
    ts_external_io_mode_set($external_io_out, domain: domain_out, pin: "MESA_TS_EXT_IO_MODE_ONE_PPS_OUTPUT", freq: 0)
    sleep(1.5)

    check_saved_ts_diff($diff_no_adj)

    t_i("Set frequency adjustment to maximum positive")
    ts_adjtimer_set(domain_out, $adj_max, domain_def: domain_def)
    sleep(1.5)

    check_saved_ts_diff($diff_high, $diff_low)

    t_i("Set frequency adjustment to maximum negative")
    ts_adjtimer_set(domain_out, -$adj_max, domain_def: domain_def)
    sleep(1.5)

    check_saved_ts_diff($diff_high, $diff_low)

    t_i("Set frequency adjustment to saved")
    ts_adjtimer_set(domain_out, adjtimer, domain_def: domain_def)
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

test("dump") do
    #$ts.dut.run("mesa-cmd deb api ci ts")
end
