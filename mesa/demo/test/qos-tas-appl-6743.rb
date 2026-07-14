#!/usr/bin/env ruby

# Copyright (c) 2004-2026 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

# Regression test for APPL-6743:
# When disabling TAS (gate_enabled=false) while the scheduled base_time has not
# yet arrived, the hardware state machine needs up to base_time + stop-list
# startup time before it can accept the disable request.  The old application
# wrapper (tsn_tas_port_conf_set) only budgeted 2 s for ALL port_conf_set
# retries and also incorrectly recalculated base_time on every retry even for
# the disable path (where MESA ignores base_time).  Setting base_time 2 s in
# the future and disabling immediately therefore reliably caused the MESA API
# to return an error after exhausting the 2 s budget.
#
# The fix raises the disable timeout to 5 s and removes the pointless
# base_time recalculation on the disable path.

require_relative 'libeasy/et'

$ts = get_test_setup("mesa_pc_b2b_2x")

check_capabilities() do
    $tas_support = $ts.dut.call("mesa_capability", "MESA_CAP_QOS_TAS")
    assert(($tas_support == 1), "TAS not supported on this platform")
end

def appl_6743_cycle(eg, iteration)
    t_i ("Create a minimal GCL - two entries, all queues open")
    gcl = [{"gate_operation":"MESA_QOS_TAS_GCO_SET_GATE_STATES",
            "gate_open":[true,true,true,true,true,true,true,true],
            "time_interval":80000},
           {"gate_operation":"MESA_QOS_TAS_GCO_SET_GATE_STATES",
            "gate_open":[true,true,true,true,true,true,true,true],
            "time_interval":20000}]
    $ts.dut.call("mesa_qos_tas_port_gcl_conf_set", $ts.dut.p[eg], 2, gcl)

    t_i ("Enable TAS with base_time = current TOD")
    # base_time is read from the current TOD (which was reset to 0 before the
    # test loop).  The hardware will not start the active list until TOD
    # reaches base_time.  Attempting to disable before that point exercises the
    # slow-disable path that needs more than 2 s to complete.
    tod = $ts.dut.call("mesa_ts_timeofday_get")
    conf = $ts.dut.call("mesa_qos_tas_port_conf_get", $ts.dut.p[eg])
    conf["gate_enabled"]              = true
    conf["gate_open"].each_index {|i| conf["gate_open"][i] = true}
    conf["cycle_time"]                = 100000
    conf["cycle_time_ext"]            = 256
    conf["base_time"]["sec_msb"]      = 0
    conf["base_time"]["seconds"]      = tod[0]["seconds"]+2
    conf["base_time"]["nanoseconds"]  = tod[0]["nanoseconds"]
    conf["config_change"]             = true
    $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)

    t_i ("Verify config_pending is true - base_time has not arrived yet")
    status = $ts.dut.call("mesa_qos_tas_port_status_get", $ts.dut.p[eg])
    if (status["config_pending"] != true)
        t_e("Iteration #{iteration}: Expected config_pending=true immediately after enable, got #{status["config_pending"]}")
    end

    # This is the call that used to fail with the old 2 s timeout:
    # the hardware must wait for base_time (up to 2 s away) before it can
    # process the stop-list, so the disable needs up to ~2 s + stop-list
    # startup time - well beyond the old 2 s budget.
    t_i ("Disable TAS immediately while still pending")
    conf["gate_enabled"]  = false
    conf["config_change"] = false
    $ts.dut.call("mesa_qos_tas_port_conf_set", $ts.dut.p[eg], conf)

    t_i ("Poll for config_pending to clear (allow up to 10 s)")
    pending = true
    100.times do
        status = $ts.dut.call("mesa_qos_tas_port_status_get", $ts.dut.p[eg])
        if (status["config_pending"] == false)
            pending = false
            break
        end
        sleep 0.1
    end
    if (pending == true)
        t_e("Iteration #{iteration}: config_pending never cleared after disabling TAS")
    end

    t_i ("Verify all gates are open after TAS is disabled")
    open_cnt = 0
    status["gate_open"].each {|v| open_cnt += 1 if v == true}
    if (open_cnt != 8)
        t_e("Iteration #{iteration}: Expected 8 open gates after TAS disable, got #{open_cnt}")
    end
end

def appl_6743_test
    eg = 0

    test "TAS disable while config pending (APPL-6743) on port #{$ts.dut.p[eg]}" do
        t_i ("Reset TOD to 0 so base_time offsets are predictable")
        tod = $ts.dut.call("mesa_ts_timeofday_get")
        tod[0]["seconds"]     = 0
        tod[0]["nanoseconds"] = 0
        $ts.dut.call("mesa_ts_timeofday_set", tod[0])

        2.times do |i|
            t_i ("--- Iteration #{i + 1} of 2 ---")
            appl_6743_cycle(eg, i + 1)
        end
    end
end

test "test_run" do
    appl_6743_test()
end

test_summary()

test "dump" do
    #$ts.dut.run("mesa-cmd deb api ci qos action 7")
end
