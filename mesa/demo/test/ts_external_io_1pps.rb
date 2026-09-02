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

case $pcb
    when "6849-Sunrise"
        $external_io_in, $external_io_out, $diff_high, $diff_low = 2, 1, 8, 5
    when "8291-EndNode"
        $external_io_in, $external_io_out, $diff_high, $diff_low = 4, 1, 2, 3
    when 134
        $external_io_in, $external_io_out, $diff_high, $diff_low = 2, 1, 2, 3
    when "8290"
        $external_io_in, $external_io_out, $diff_high, $diff_low = 0, 3, 2, 3
    when 111
        $external_io_in, $external_io_out, $diff_high, $diff_low = 2, 0, 16, 25
    when "6813-Adaro"
        $external_io_in, $external_io_out, $diff_high, $diff_low = 0, 1, 10, 10
    when "8398"
        $external_io_in, $external_io_out, $diff_high, $diff_low = 5, 4, 2, 2
    else
        $external_io_in, $external_io_out, $diff_high, $diff_low = 2, 1, 2, 2
end

t_i("$external_io_out #{$external_io_out} $external_io_in #{$external_io_in}")


################################################
# Test Tables
################################################

test_table =
[
    {
        txt: "1PPS External I/O: Domain = 0",
        fun: -> (t) { tod_external_io_1pps_test(0) }
    },
    {
        txt: "1PPS External I/O: Domain = 1",
        fun: -> (t) { tod_external_io_1pps_test(1) }
    },
    {
        txt: "1PPS External I/O: Domain = 2",
        fun: -> (t) { tod_external_io_1pps_test(2) }
    },
    {
        txt: "1PPS External I/O: TOD Offset",
        fun: -> (t) { tod_external_io_1pps_tod_offset_test() }
    }
]

################################################
# General/Helper Functions
################################################

# Merge overrides into an already-fetched io-mode conf, then apply it.
def io_mode_apply(io, pin_conf, **overrides)
    overrides.each { |k, v| pin_conf[k.to_s] = v }
    $ts.dut.call("mesa_ts_external_io_mode_set", io, pin_conf)
end

# Check that a nanosecond difference is within expect +/- $diff_high/$diff_low.
def diff_check(diff, expect)
    t_i("Difference #{diff} in TOD nanoseconds must be approx #{expect}")
    if ((diff > (expect + $diff_high)) || (diff < (expect - $diff_low)))
        t_e("Difference is not as expected")
    end
end

# Apply overrides to an already-fetched TOD struct, then set it.
def ts_domain_timeofday_set(domain, ts, **overrides)
    overrides.each { |k, v| ts[k.to_s] = v }
    $ts.dut.call("mesa_ts_domain_timeofday_set", domain, ts)
end

################################################
# Test Section
################################################

test("conf") do
    # disable VLAN 1 to avoid looping
    $ts.dut.call("mesa_vlan_port_members_set", 1, "")

    # External io mode save
    $extern_io_mode_restore_in = $ts.dut.call("mesa_ts_external_io_mode_get", $external_io_in)

    $ts.dut.run("mesa-cmd Debug Port Polling disable")

    # TOD template and 1PPS output pin conf shared/mutated across the per-domain tests below
    $tod_ts = $ts.dut.call("mesa_ts_domain_timeofday_get", 0)[0]
    $pin_conf = $ts.dut.call("mesa_ts_external_io_mode_get", $external_io_out)
    io_mode_apply($external_io_out, $pin_conf, pin: "MESA_TS_EXT_IO_MODE_ONE_PPS_DISABLE", freq: 0)
end

def tod_external_io_1pps_test(domain)
    t_i("Test: tod_external_io_1pps  domain = #{domain}")

    t_i("Set TOD to zero")
    ts_domain_timeofday_set(domain, $tod_ts, seconds: 0, nanoseconds: 0)

    t_i("Configure 1PPS input pin to this domain")
    io_mode_apply($external_io_in, $pin_conf, domain: domain, pin: "MESA_TS_EXT_IO_MODE_ONE_PPS_SAVE")

    t_i("Get TOD on 1PPS input pin")
    pin = $ts.dut.call("mesa_ts_saved_timeofday_get", $external_io_in)
    pin_ts1 = pin[0]

    sleep(1.6)

    if (!cap_get("MISC_FPGA")) # On the Laguna FPGA it seems that the GPIO output is unstable when 1PPS is disabled
        t_i("Get TOD on 1PPS input pin again to check not incremented")
        pin = $ts.dut.call("mesa_ts_saved_timeofday_get", $external_io_in)
        pin_ts2 = pin[0]
        if (pin_ts1["seconds"] != pin_ts2["seconds"])
            t_e("Case 1PPS is not enabled. TOD in domain #{domain} was not as expected.  pin_ts1[seconds] = #{pin_ts1["seconds"]}  pin_ts2[seconds] = #{pin_ts2["seconds"]}")
        end
    end

    t_i("Configure 1PPS output pin to this domain")
    io_mode_apply($external_io_out, $pin_conf, pin: "MESA_TS_EXT_IO_MODE_ONE_PPS_OUTPUT")

    t_i("Get TOD on 1PPS input pin")
    5.times do
        pin = $ts.dut.call("mesa_ts_saved_timeofday_get", $external_io_in)
        break if (pin[0]["seconds"] > pin_ts1["seconds"])
        sleep(0.8)
    end
    pin_ts1 = pin[0]

    sleep(0.8)

    t_i("Get TOD on 1PPS input pin to check incremented")
    pin = $ts.dut.call("mesa_ts_saved_timeofday_get", $external_io_in)
    if (pin[0]["seconds"] == pin_ts1["seconds"])
        sleep(0.2)
        pin = $ts.dut.call("mesa_ts_saved_timeofday_get", $external_io_in)
    end

    pin_ts2 = pin[0]
    if ((pin_ts2["seconds"] > (pin_ts1["seconds"] + 2)) || (pin_ts2["seconds"] <= pin_ts1["seconds"]))
        t_e("Case 1PPS is enabled. TOD in domain #{domain} was not as expected.  pin_ts1[seconds] = #{pin_ts1["seconds"]}  pin_ts2[seconds] = #{pin_ts2["seconds"]}")
    end

    t_i("Configure 1PPS output pin to no 1PPS output")
    io_mode_apply($external_io_out, $pin_conf, pin: "MESA_TS_EXT_IO_MODE_ONE_PPS_DISABLE")
end

def tod_external_io_1pps_tod_offset_test
    t_i("Test: tod_external_io_1pps_tod_offset")

    domain_out = 0
    domain_in = 1
    offset = 10_000_000

    t_i("Init Set TOD")
    tod = $ts.dut.call("mesa_ts_domain_timeofday_get", domain_out)
    ts = tod[0]

    t_i("Configure 1PPS input pin")
    pin_conf = $ts.dut.call("mesa_ts_external_io_mode_get", $external_io_in)
    io_mode_apply($external_io_in, pin_conf, domain: domain_in, pin: "MESA_TS_EXT_IO_MODE_ONE_PPS_SAVE")

    t_i("Set TOD to zero for both domains")
    ts_domain_timeofday_set(domain_in, ts, seconds: 1, nanoseconds: 0)
    ts_domain_timeofday_set(domain_out, ts, seconds: 1, nanoseconds: 0)

    t_i("Configure 1PPS output pin")
    pin_conf = $ts.dut.call("mesa_ts_external_io_mode_get", $external_io_out)
    io_mode_apply($external_io_out, pin_conf, domain: domain_out, pin: "MESA_TS_EXT_IO_MODE_ONE_PPS_OUTPUT", freq: 0)

    sleep(0.5)
    tod = $ts.dut.call("mesa_ts_saved_timeofday_get", $external_io_in)

    sleep(0.7)

    # Get base line TOD on 1PPS input pin
    tod = $ts.dut.call("mesa_ts_saved_timeofday_get", $external_io_in)
    ts1 = tod[0]

    t_i("Set TOD offset 0.1 seconds - positive. Its weird - offset parameter is signed but always subtracted in the API, so in order to add an offset it has to be negative")
    $ts.dut.call("mesa_ts_domain_timeofday_offset_set", domain_out, -offset)

    sleep(0.5)

    tod = $ts.dut.call("mesa_ts_saved_timeofday_get", $external_io_in)
    sleep(0.7)

    # Get TOD on 1PPS input pin
    tod = $ts.dut.call("mesa_ts_saved_timeofday_get", $external_io_in)
    ts2 = tod[0]

    diff_check(ts1["nanoseconds"] - ts2["nanoseconds"], offset)

    t_i("Set TOD offset 0.1 seconds - negative. Its weird - offset parameter is signed but always subtracted in the API, so in order to subtract an offset it has to be positive")
    $ts.dut.call("mesa_ts_domain_timeofday_offset_set", domain_out, offset)

    sleep(0.5)

    tod = $ts.dut.call("mesa_ts_saved_timeofday_get", $external_io_in)
    sleep(0.7)

    # Get TOD on 1PPS input pin
    tod = $ts.dut.call("mesa_ts_saved_timeofday_get", $external_io_in)
    ts2 = tod[0]

    diff_check(ts2["nanoseconds"] - ts1["nanoseconds"], 0)
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
