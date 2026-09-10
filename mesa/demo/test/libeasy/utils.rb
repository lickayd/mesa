# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

require 'ipaddr'

def hexstr s
    ss = ""
    s.each_byte{|b| ss += ("%02x" % [b])}
    return ss
end

# Convert IPv6 address string to byte array
def ipv6_str2arr(ipv6_str)
    ipaddr = IPAddr.new ipv6_str
    ipaddr = ipaddr.hton.split(//)
    adr = []
    ipaddr.each do |x|
        adr << x.unpack("H*").first.hex
    end
    adr
end

# Convert IPv4 address string to integer
def ipv4_str2int(ipv4)
    IPAddr.new(ipv4, Socket::AF_INET).to_i
end

# Convert MAC address array to string
def mac_to_txt(mac)
    mac.map {|s| s.to_s(16).rjust(2,'0')}.join ":"
end

# Convert IPv4 integer to string
def ipv4_to_txt(ipv4)
    IPAddr.new(ipv4, Socket::AF_INET).to_s
end

# Convert IPv6 array to string
def ipv6_to_txt(ipv6)
    txt = ""
    for i in 0..15
        txt = (txt + (i == 0 || (i & 1) == 1 ? "" : ":") + "%02x" %ipv6[i])
    end
    txt
end

def vcap_vm_set(k, name, v, fld)
    if (v != nil and v.key?(fld))
        k[name]["value"] = v[fld][:v]
        k[name]["mask"] = v[fld][:m]
    end
end

def vcap_range_set(k, name, v, fld)
    if (v != nil and v.key?(fld))
        v = v[fld]
        k = k[name]
        if (v.key?:l)
            type = "RANGE_INCLUSIVE"
            r = {}
            r["low"] = v[:l]
            r["high"] = v[:h]
            k["vr"]["r"] = r
        else
            type = "VALUE_MASK"
            k["vr"]["v"]["value"] = v[:v]
            k["vr"]["v"]["mask"] = v[:m]
        end
        k["type"] = "MESA_VCAP_VR_TYPE_#{type}"
    end
end

def vcap_bit_set(k, name, v, fld)
    if (v != nil and v.key?(fld))
        k[name] = "MESA_VCAP_BIT_#{v[fld]}"
    end
end

def ace_bit_set(k, name, v, fld)
    if (v != nil and v.key?(fld))
        k[name] = "MESA_ACE_BIT_#{v[fld]}"
    end
end

def ace_range_set(k, name, v, fld)
    if (v != nil and v.key?(fld))
        v = v[fld]
        k = k[name]
        k["in_range"] = true
        k["low"] = v[:l]
        k["high"] = v[:h]
    end
end

def fld_get(v, fld, val = 0)
    if (v != nil and v.key?(fld))
        val = v[fld]
    end
    val
end

def table_lookup(table, fld)
    f = nil
    table.each do |e|
        v = e[fld]
        if (v != nil)
            f = v
        end
    end
    f
end

def port_idx_list_str(idx_list)
    str = ""
    idx_list.each do |idx|
        str += "#{$ts.dut.port_list[idx]}"
        str += "," unless idx == idx_list.last
    end
    str
end

def port_idx_shuffle(ts)
    port_list = []
    ts.pc.p.each_index do |idx|
        port_list << idx
    end
    port_list.shuffle
end

$cap_cache = {}
def cap_get(cap)
    if ($cap_cache[cap] == nil)
        $cap_cache[cap] = $ts.dut.call "mesa_capability", "MESA_CAP_" + cap
    end
    return $cap_cache[cap]
end

def cap_check_exit(cap)
    check_capabilities do
        assert(cap_get(cap) > 0, "Capability '#{cap}' must be present")
    end
end

def dut_cap_check_exit(cap)
    check_capabilities do
        c = $ts.dut.cap
        assert((c != nil and c.include?cap), "DUT capability '#{cap}' must be present")
    end
end

def loop_pair_check(loop_10g = false)
    port_list = $ts.dut.looped_port_list
    if (port_list == nil and loop_10g)
        port_list = $ts.dut.looped_port_list_10g
    end

    check_capabilities do
        assert(port_list != nil && port_list.length > 1, "Two front ports must be looped")
    end

    check_capabilities do
        assert(dut_port_state_up(port_list.take(2)), "Loop ports must be up")
    end
    return port_list
end

def check_val(name, val, exp, fmt)
    v = (fmt % val);
    e = (fmt % exp);
    msg = "#{name}: #{v}, expected: #{e}"
    if (val == exp)
        t_i(msg)
    else
        t_e(msg)
    end
end

def check_counter(name, val, exp)
    check_val(name, val, exp, "%u")
end

def check_val_hex_u8(name, val, exp)
    check_val(name, val, exp, "0x%02x")
end

def check_val_hex_u16(name, val, exp)
    check_val(name, val, exp, "0x%04x")
end

def check_val_hex_u32(name, val, exp)
    check_val(name, val, exp, "0x%08x")
end

def check_val_str(name, val, exp)
    check_val(name, val, exp, "%s")
end

def cmd_tag_push(tag)
    cmd = ""
    if (tag.key?:tpid and tag[:tpid] != 0)
        tpid = tag[:tpid]
        vid = ((tag.key?:vid) ? tag[:vid] : 0)
        pcp = ((tag.key?:pcp) ? tag[:pcp] : 0)
        dei = ((tag.key?:dei) ? tag[:dei] : 0)
        cmd = " et 0x#{tpid.to_s(16)} ctag vid #{vid} pcp #{pcp} dei #{dei}"
    end
    cmd
end

def cmd_rx_ifh_push(ifh={}, pfx = true)
    # Short prefix by default (Jaguar-2 format used)
    epid = cap_get("PACKET_IFH_EPID")
    cmd = ""
    if (pfx)
        cmd = "sp-jr2 ign et 0x8880 id #{epid} "
    end

    # Default field names
    port_name = "src-port"
    isdx_name = "isdx"
    vid_name = "vid"
    mid_name = "de-cl-rslt"

    case epid
    when 5, 10
        # Serval-1/Ocelot
        cmd += "efh-oc1 ign"
    when 7, 9
        # Jaguar-2/Serval-T
        cmd += "ifh-jr2 ign"
        port_name = "f-src-port"
        isdx_name = "vm1-isdx"
        vid_name = "vt-cl-vid"
    when 11
        # FireAnt
        cmd += "ifh-fa ign"
        port_name = "f-src-port"
        isdx_name = "vm1-isdx"
        vid_name = "vt-cl-vid"
    when 13
        # Maserati
        cmd += "ifh-mas ign"
    when 14
        # Laguna
        cmd += "ifh-la ign"
        port_name = "f-src-port"
        isdx_name = "vm1-isdx"
        vid_name = "vt-cl-vid"
        mid_name = "de-match-id"
    else
        # Luton26, no prefix
        cmd = "efh-crcl ign"
    end

    port = fld_get(ifh, :port, nil)
    if (ifh.key?:port_idx)
        # Match chip port with ifh port_idx
        pmap = $ts.port_map
        port = $ts.dut.port_list[ifh[:port_idx]]
    end

    if (port != nil)
        # Match chip port with port
        pmap = $ts.port_map
        cmd += " #{port_name} #{pmap[port]["chip_port"]}"
    end

    if (ifh.key?:vid)
        cmd += " #{vid_name} #{ifh[:vid]}"
    end

    if (ifh.key?:isdx)
        cmd += " #{isdx_name} #{ifh[:isdx]}"
    end

    if (ifh.key?:match_id)
        cmd += " #{mid_name} #{ifh[:match_id]}"
    end

    cmd += " "

    cmd
end

def cmd_tx_ifh_push(info={}, pfx = true)
    # Short prefix by default (Jaguar-2 format used)
    epid = cap_get("PACKET_IFH_EPID")
    cmd = "sp-jr2 dmac ff:ff:ff:ff:ff:ff smac fe:ff:ff:ff:ff:ff id #{epid} "
    if (epid == 0 || !pfx)
        # Luton26 or no prefix
        cmd = ""
    end

    # Build IFH based on Tx information
    tx_info = $ts.dut.call("mesa_packet_tx_info_init")
    info.each do |key, val|
        tx_info[key.to_s] = val
    end
    # Support one egress port only via 'dst_port' field
    tx_info["dst_port_mask"] = (1 << tx_info["dst_port"])
    ifh = $ts.dut.call("mesa_packet_tx_hdr_encode", tx_info, cap_get("PACKET_TX_IFH_SIZE"))
    cmd += "data hex #{ifh[0].take(ifh[1]).pack("c*").unpack("H*").first} "
end

def ethtool_stat ts, diff = nil, if_list = [], ns = nil, all = false
    tot = {}

    ns_ = ""
    if ns
        ns_ = "ip netns exec #{ns}"
    end

    if (if_list.length == 0)
        if_list = ts.pc.p
    end

    if_list.each do |e|
        begin
            a = (all ? "" : " | grep -v queue | grep -v os2bmc | grep -v hwt")
            cmd = "sh -c 'ethtool -S #{e}#{a}'"
            ts.pc.run(cmd)[:out].each_line do |l|
                tot[e] = { "rx" => {}, "tx" => {}} if tot[e].nil?
                if /(rx|tx)_(\w+):\s+(\d+)/ =~ l
                    tot[e][$1][$2] = $3.to_i
                end
            end
        rescue
        end
    end

    return tot if diff.nil?

    res = Marshal.load(Marshal.dump(tot))
    tot.each do |ifn, rxtx|
        rxtx["rx"].each do |n, val|
            begin
                res[ifn]["rx"][n] -= diff[ifn]["rx"][n]
            rescue
            end
        end

        rxtx["tx"].each do |n, val|
            begin
                res[ifn]["tx"][n] -= diff[ifn]["tx"][n]
            rescue
            end
        end
    end

    return tot, res
end

def eval_stats actual_stat, if_list, expect_stat, tolerance = 0
    act = {}
    exp = {}
    tol = {}

    t = 0
    if expect_stat["tolerance"]
        t = expect_stat["tolerance"]
    elsif tolerance.is_a? Integer
        t = tolerance
    end

    if_list.each do |n|
        act["#{n}"] = { "rx" => 0, "tx" => 0 }
        exp["#{n}"] = { "rx" => 0, "tx" => 0 }
        tol["#{n}"] = { "rx" => t, "tx" => t }
        ["rx", "tx"].each do |x|
            if actual_stat[n][x]["packets"]
                # as returned by ethtool -S on host
                act[n][x] = actual_stat[n][x]["packets"]
            else
                raise "Could not parse actual_stat"
            end
        end
    end

    err = 0

    expect_stat.each do |k, v|
        begin
            if /([^-]+)-(rx|tx)/ =~ k
                exp[$1][$2] = v
            end
        rescue
            puts "Problem processing expect_stat #{k}"
            err += 1
        end
    end

    if tolerance.is_a? Hash
        tolerance.each do |k, v|
            begin
                if /([^-]+)-(rx|tx)/ =~ k
                    tol[$1][$2] = v
                end
            rescue
                puts "Problem processing tolerance #{k}"
                err += 1
            end
        end
    end

    txt = []
    if_list.each do |n|
        rx_act = act[n]["rx"]
        rx_exp = exp[n]["rx"]
        rx_diff = rx_act - rx_exp
        tx_act = act[n]["tx"]
        tx_exp = exp[n]["tx"]
        tx_diff = tx_act - tx_exp
        txt << "%20s | %8d %8d | %8d %8d | %8d %8d" %
          [n, rx_act, tx_act, rx_exp, tx_exp, rx_diff, tx_diff]
        err += 1 if (rx_diff).abs > tol[n]["rx"]
        err += 1 if (tx_diff).abs > tol[n]["tx"]
    end

    t_i("NIC Name               Actual rx/tx        Expected rx/tx      Difference rx/tx")
    t_i("-------------------- | -------- -------- | -------- -------- | -------- --------")
    txt.each {|l| t_i(l)}

    if err > 0
        t_e("Counter mismatch")
    end
end

# Show Rx/Tx counters in two columns
def ethtool_show(if_list, cnt)
    if_list.each do |name|
        t_i("")
        t_i("Counters for #{name}:")
        table = [
            ["packets", ""],
            ["bytes", ""],
            ["broadcast", ""],
            ["multicast", ""],
            ["flow_control_xon", ""],
            ["flow_control_xoff", ""],
            ["errors", ""],
            ["fifo_errors", ""],
            ["smbus", ""],
            ["crc_errors", "carrier_errors"],
            ["align_errors", "aborted_errors"],
            ["no_buffer_count", "dropped"],
            ["length_errors", "abort_late_coll"],
            ["short_length_errors", "deferred_ok"],
            ["long_length_errors", "single_coll_ok"],
            ["over_errors", "multi_coll_ok"],
            ["frame_errors", "window_errors"],
            ["missed_errors", "heartbeat_errors"],
        ]
        table.each do |e|
            n = ""
            str = ""
            e.each_with_index do |c, i|
                n = c if (c != "")
                dir = (i == 0 ? "rx" : "tx")
                str += sprintf("%-22s: %10u   ", "#{dir}_#{n}", cnt[name][dir][n])
            end
            t_i(str)
        end
    end
end

def conf_func(warm)
    t_e("conf_func not implemented")
end

def frame_func
    t_e("frame_func not implemented")
end

def warm_start(ts)
    go = true
    thr1 = Thread.new do
        ts.dut.run("mesa-cmd warm start")
        conf_func(true)
        ts.dut.call("mesa_restart_conf_end")
        go = false
    end
    thr2 = Thread.new do
        while (go)
            frame_func
        end
    end
    thr1.join
    thr2.join
end

CHIP_FAMILY_MAP = {
  "MESA_CHIP_FAMILY_UNKNOWN" => 0,
  # Unknown = 1
  "MESA_CHIP_FAMILY_CARACAL" => 2,
  # Unknown = 3
  "MESA_CHIP_FAMILY_SERVAL"  => 4,
  # Unknown = 5
  "MESA_CHIP_FAMILY_SERVALT" => 6,
  "MESA_CHIP_FAMILY_JAGUAR2" => 7,
  "MESA_CHIP_FAMILY_OCELOT"  => 8,
  "MESA_CHIP_FAMILY_SPARX5"  => 9,
  "MESA_CHIP_FAMILY_LAN966X" => 10,
  "MESA_CHIP_FAMILY_LAN969X" => 11
}
CHIP_ID_TO_FAMILY = CHIP_FAMILY_MAP.invert

def chip_family_to_id(txt)
    CHIP_FAMILY_MAP[txt] || t_e("mesa_chip_family '#{txt}' not known")
end

def chip_id_to_family(id)
    CHIP_ID_TO_FAMILY[id] || t_e("mesa_chip_family id '#{id}' not known")
end

def dut_port_state_up(ports)
    ts = Time.now.to_i
    ports.each do |port|
        loop do
            oper_up = $ts.dut.call "mesa_port_state_get", port
            break if oper_up
            if ((Time.now.to_i - ts) > 20)
                return false
            end
            sleep 1
        end
    end
    return true
end

def default_cos2pcp(cos)
  case cos
  when 0
    return 1
  when 1
    return 0
  else
    return cos
  end
end

def counter_get(direction, port)
    grep_str = (direction == "RX") ? "rx_packets" : "tx_packets"
    array = $ts.pc.run("sh -c 'ethtool -S #{port} | grep #{grep_str}'")[:out].split(' ')
    t_i("counter_get #{port} #{direction} #{array[0]} #{array[1]}")
    return array[1]
end

MEASURE_PCP_NONE = 0xFFFF

# Frame counters for the ports in a measurement. Returns nil if they cannot be read, a diagnostic must never break the measurement
def measure_counters_get(ig, eg)
    counters = {:ts => {}, :rx => {}, :tx => {}, :prio_tx => {}}
    (ig + [eg]).uniq.each do |port|
        ts = Time.now
        cnt = $ts.dut.call("mesa_port_counters_get", $ts.dut.p[port])
        # One timestamp per port, so a rate never depends on the read batches taking equal time
        counters[:ts][port] = ts + ((Time.now - ts) / 2)
        counters[:rx][port] = cnt["rmon"]["rx_etherStatsPkts"].to_i
        counters[:tx][port] = cnt["rmon"]["tx_etherStatsPkts"].to_i
        counters[:prio_tx][port] = (cnt["prio"] || []).map {|prio| prio["tx"].to_i}
    end
    return counters
rescue => e
    t_i("Could not read port counters: #{e}")
    return nil
end

# Counter deltas over the capture. Returns nil when the counters could not be read
def measure_counters_delta(before, after)
    return nil if ((before == nil) || (after == nil))

    delta = {:secs => {}, :rx => {}, :tx => {}, :prio_tx => {}}
    after[:rx].each_key do |port|
        secs = after[:ts][port] - before[:ts][port]
        return nil if (secs <= 0)
        delta[:secs][port] = secs
        delta[:rx][port] = after[:rx][port] - before[:rx][port]
        delta[:tx][port] = after[:tx][port] - before[:tx][port]
        prio_before = before[:prio_tx][port]
        delta[:prio_tx][port] = after[:prio_tx][port].each_with_index.map {|cnt, prio| cnt - prio_before.fetch(prio, cnt)}
    end
    return delta
end

# No verdict here, judging a rate needs the expected egress rate that only the caller knows
def measure_counters_report(delta, ig, eg, line_rate_fps)
    return if (delta == nil)

    ig.each_with_index do |port, ig_idx|
        rx = delta[:rx][port]
        secs = delta[:secs][port]
        fps = (rx / secs).to_i
        exp_fps = line_rate_fps[ig_idx].to_i
        percent = (exp_fps > 0) ? ((fps * 100) / exp_fps) : 0
        t_i("Ingress port #{port} received #{rx} frames in #{'%.3f' % secs} sec: #{fps} fps, #{percent} % of the #{exp_fps} fps line rate")

        # An ingress port that also transmits means the traffic is flooded, which loads the shared egress resources
        ig_tx = delta[:tx][port]
        if ((port != eg) && (ig_tx > secs))
            t_i("WARNING: ingress port #{port} transmitted #{ig_tx} frames in #{'%.3f' % secs} sec. The traffic is flooded, the measurement is not only about the egress port")
        end
    end

    tx = delta[:tx][eg]
    secs = delta[:secs][eg]
    t_i("Egress port #{eg} transmitted #{tx} frames in #{'%.3f' % secs} sec: #{(tx / secs).to_i} fps. Per prio #{delta[:prio_tx][eg]}")
end

# Drops in the NIC or its driver, before tcpdump. Not part of the 'dropped by kernel' statistic that tcpdump reports itself
def pc_drop_counters_get(port)
    counters = {}
    res = $ts.pc.try_ignore("sh -c 'ethtool -S #{port} | grep -E \"drop|discard|missed|no_buffer|fifo\"'")
    res[:out].to_s.each_line do |line|
        name, value = line.split(":")       # ethtool prints one "  name: value" per line
        counters[name.strip] = value.to_i if (value != nil)
    end
    return counters
end

def pc_drop_counters_report(before, after, port)
    after.each do |name, value|
        next if (value <= before.fetch(name, value))
        t_i("WARNING: #{port} #{name} increased by #{value - before[name]} during the capture. The capture is incomplete")
    end
end

# Expected number of egress frames per second for one measurement index. 'size' is the requested frame size inclusive checksum
def measure_expected_fps(erate, size, frame_rate, data_rate)
    sec_count_in = 1000000000/8/(20+size)   # Frames per second offered at line speed. The ef tx function can only run at line speed
    return erate if (frame_rate)            # A frame rate is already the number of frames per second
    sec_count_out = 1000000000/8/((data_rate ? 0 : 20)+size)   # Theoretical full rate number of outgoing frames per sec
    sec_count = (sec_count_out*erate)/1000000000               # Outgoing frames per sec as a fraction of the full rate count
    # Number of outgoing frames cannot be larger than the number of incoming
    return (sec_count < sec_count_in) ? sec_count : sec_count_in
end

# A frame rate shaper is only exercised while the offered load exceeds it. Line and data rate expectations are capped by the offered load, so only a frame rate can ask for the impossible
def measure_config_check(erate, sizes, frame_rate)
    return true if (!frame_rate)
    ok = true
    erate.each_with_index do |rate, idx|
        next if ((rate == nil) || (rate == 0) || (sizes[idx] == nil))
        line_rate_fps = 1000000000/8/(20+sizes[idx])
        next if (rate <= line_rate_fps)
        t_e("Test configuration error: #{rate} fps cannot be offered with frame size #{sizes[idx]}, line speed is #{line_rate_fps} fps. Use a frame size of #{(1000000000/8/rate)-20} bytes or less, or a lower rate")
        ok = false
    end
    return ok
end

# The DUT is only measurable while the generator offers more than the expected egress rate. A shortfall beyond the tolerance makes the capture say nothing about the DUT
def measure_offered_load_check(delta, ig, expected_fps, etolerance)
    return true if (delta == nil)
    ok = true
    ig.each_with_index do |port, idx|
        exp = expected_fps[idx].to_i
        next if (exp == 0)
        min = (exp - ((exp * (etolerance[idx] || 0).to_f) / 100)).to_i
        fps = (delta[:rx][port] / delta[:secs][port]).to_i
        next if (fps >= min)
        t_e("Measurement invalid: ingress port #{port} was offered #{fps} fps, the #{exp} fps expected on egress needs at least #{min} fps. The traffic generator fell short, this says nothing about the DUT")
        ok = false
    end
    return ok
end

# Confirms a pcap_analyze deficit against the DUT's own egress counters over the same window. Unlike
# measure_offered_load_check this never fails the test itself, it only tells the caller whether the DUT's own view agrees
def measure_egress_load_check(delta, eg, expected_fps, etolerance, pcp)
    return true if (delta == nil)
    ok = true
    secs = delta[:secs][eg]
    if (pcp == [])
        exp = expected_fps[0].to_i
        return true if (exp == 0)
        min = (exp - ((exp * (etolerance[0] || 0).to_f) / 100)).to_i
        fps = (delta[:tx][eg] / secs).to_i
        if (fps < min)
            t_i("Confirmation: egress port #{eg} delivered #{fps} fps over this window, the #{exp} fps expected needs at least #{min} fps. The DUT's own counters show this deficit too")
            ok = false
        end
    else
        pcp.each_with_index do |pcp_value, idx|
            exp = expected_fps[idx].to_i
            next if (exp == 0)
            min = (exp - ((exp * (etolerance[idx] || 0).to_f) / 100)).to_i
            fps = (delta[:prio_tx][eg][pcp_value.to_i] / secs).to_i
            next if (fps >= min)
            t_i("Confirmation: egress port #{eg} pcp #{pcp_value} delivered #{fps} fps over this window, the #{exp} fps expected needs at least #{min} fps. The DUT's own counters show this deficit too")
            ok = false
        end
    end
    return ok
end

# Serializes check_rate() across DUTs that share one PC's traffic generator. Held on the PC itself,
# since that's the one thing every caller can reach, even from different hosts.
RATE_LOCK_FILE = "/tmp/mesa-rate-test.lock"
RATE_LOCK_HOLD_TTL = 120          # Self-expiry backstop for a crashed/orphaned holder; its marker is left behind but harmless
RATE_LOCK_MAX_STACKED_ORPHANS = 5 # Tolerate this many orphans self-expiring in sequence (e.g. repeated cancellations) before giving up
RATE_LOCK_ACQUIRE_TIMEOUT = RATE_LOCK_HOLD_TTL * RATE_LOCK_MAX_STACKED_ORPHANS
RATE_LOCK_POLL_INTERVAL = 0.5

# Blocks (bounded) until this is the only DUT running check_rate() against this PC. Returns a holder
# handle for rate_lock_release, or nil (having already reported t_e) if the wait timed out
def rate_lock_acquire
    token = "#{Process.pid}.#{rand(1_000_000)}"
    marker = "#{RATE_LOCK_FILE}.acquired.#{token}"
    # Blocks until free, then touches a marker unique to this attempt - a race-free acquire signal.
    # 'exec sleep' keeps it one killable process, not a shell holding its own copy of the lock
    holder_pid = $ts.pc.bg("rate_lock", "flock #{RATE_LOCK_FILE} -c 'touch #{marker} && exec sleep #{RATE_LOCK_HOLD_TTL}'")

    deadline = Time.now + RATE_LOCK_ACQUIRE_TIMEOUT
    polls = 0
    loop do
        break if ($ts.pc.try_ignore("test -f #{marker}")[:res] == 0)
        if (Time.now > deadline)
            t_e("Timed out after #{RATE_LOCK_ACQUIRE_TIMEOUT}s waiting for the shared rate-test lock on the PC - it may be stuck held by another run")
            rate_lock_kill(holder_pid)
            $ts.pc.try_ignore("rm -f #{marker}")
            return nil
        end
        sleep(RATE_LOCK_POLL_INTERVAL)
        polls += 1
        # Progress line every ~10s, so a long queue doesn't look like a hang
        t_i("Still waiting for the rate-test lock (#{(polls * RATE_LOCK_POLL_INTERVAL).round}s)...") if ((polls % (10.0 / RATE_LOCK_POLL_INTERVAL)) == 0)
    end
    return {:pid => holder_pid, :marker => marker}
end

def rate_lock_release(holder)
    return if (holder == nil)
    rate_lock_kill(holder[:pid])
    $ts.pc.try_ignore("rm -f #{holder[:marker]}")     # kill -9 skips the holder's own cleanup, so remove the marker here
end

# A forked child inherits its own copy of the locked fd, so kill it before the flock process itself
def rate_lock_kill(pid)
    $ts.pc.try_ignore("pkill -KILL -P #{pid}")
    $ts.pc.try_ignore("kill  -KILL    #{pid}")
end

# Wrapper function for measure() utilility
# This takes a hash input and does not create a test block
# A pcap-verdicted deficit the DUT's own counters can't confirm on either end is retried once, then treated as a real failure
MEASURE_MAX_ATTEMPTS = 2

def check_rate(cfg)
    # Extract input parameters
    ig = fld_get(cfg, :ig)
    eg = fld_get(cfg, :eg)
    size = fld_get(cfg, :size, 64)
    sec = fld_get(cfg, :sec, 1)
    frame_rate = fld_get(cfg, :frame_rate, false)
    data_rate = fld_get(cfg, :data_rate, false)
    erate = fld_get(cfg, :erate, [1000000000])
    etolerance = fld_get(cfg, :etolerance, [1])
    with_pre_tx = fld_get(cfg, :with_pre_tx, false)
    pcp = fld_get(cfg, :pcp, [])
    cycle_time = fld_get(cfg, :cycle_time, [])
    size_array = fld_get(cfg, :size_array, [])
    dmac = fld_get(cfg, :dmac, "00:00:00:00:01:01")
    streams = fld_get(cfg, :streams, 1)   # Parallel/Multiple Easyframe transmitter per ingress port
    strict_priority = fld_get(cfg, :strict_priority, false)
    sp_slack = fld_get(cfg, :sp_slack, 0)

    # Frame size and expected egress rate per measurement index, before the transmitter loop overwrites 'size'
    sizes = erate.each_index.map {|idx| size_array[idx] || size}
    expected_fps = erate.each_index.map {|idx| measure_expected_fps(erate[idx], sizes[idx], frame_rate, data_rate)}
    return nil if (!measure_config_check(erate, sizes, frame_rate))

    # Only one DUT at a time may drive the PC's traffic generator - acquire before touching it, release however this returns
    rate_lock = rate_lock_acquire
    return nil if (rate_lock == nil)
    begin

    pre_tx = with_pre_tx ? 1 : 0    # Calculate the possible pre tx time in seconds
    # The transmitters must outlive the counter window, which extends past the capture by the serial counter reads. They are always killed below
    time = (pre_tx+sec+100)
    max_cnt = 50

    attempt = 0
    counters = nil

    # Loop Start
    loop do
    attempt += 1
    pid_ef = []
    line_rate_fps = []
    ig.each_with_index do |ig_value, ig_idx|
        if (size_array != [])
            size = size_array[ig_idx]
        end

        sec_count_in = 1000000000/8/(20+size)    # Calculate frames per second at line speed. The ef tx function can only run at line speed. The 'size' parameter is the requested frame size inclusive checksum
        rep = time*sec_count_in     # Convert the required transmission seconds to number of frames, as this is the parameter to ef tx function
        line_rate_fps << sec_count_in

#        t_i("Calculated frames per sec at line speed: #{sec_count_in}")
        t_i("Start #{streams} Easy Frame transmitter(s) of #{sec*sec_count_in} frames of size #{size} with #{pre_tx} sec of pre TX and 2 sec of post TX. Speed is 1 Gbps.")
        streams.times do |s_idx|
            smac = "00:00:00:00:0#{s_idx + 1}:1#{ig_idx}"
            if (pcp != [])
                pid_ef << $ts.pc.bg("ef tx #{pcp[ig_idx]} s#{s_idx}", "sudo ef tx #{$ts.pc.p[ig_value]} rep #{rep} eth dmac #{dmac} smac #{smac} ctag vid 0 pcp #{pcp[ig_idx]} data pattern cnt #{size - (6+6+4+2+4)}") # 'size' is requested frame size inclusive checksum
            else
                pid_ef << $ts.pc.bg("ef tx s#{s_idx}",                "sudo ef tx #{$ts.pc.p[ig_value]} rep #{rep} eth dmac #{dmac} smac #{smac} data pattern cnt #{size - (6+6+2+4)}") # 'size' is requested frame size inclusive checksum
            end
        end
        max = 0
        begin   # Check that transmitter is started
            rx_cnt = counter_get("TX", $ts.pc.p[ig_value])
            max = max + 1
        end while (rx_cnt == counter_get("TX", $ts.pc.p[ig_value])) && (max < max_cnt)
        if (max == max_cnt)
            t_e("Easy Frame transmitting never started")
        end
    end

    fname = "/tmp/#{$ts.pc.p[eg]}.pcap"
    $ts.pc.run("rm -f #{fname}")
    counters_before = measure_counters_get(ig, eg)
    pc_drops_before = pc_drop_counters_get($ts.pc.p[eg])
    t_i("Start tcpdump logging on egress port: #{$ts.pc.p[eg]}")
    # -B sets to 32 MiB of buffer, a smaller one silently drops frames, which looks like missing traffic
    pid_tcp = $ts.pc.bg("tcpdump", "tcpdump -i #{$ts.pc.p[eg]} -j adapter_unsynced -B 32768 -s22 -w #{fname}")

#    t_i("Wait for necessary amount of frames to be transmitted")
#    time1 = Time.now
#    low_count = 0
#    fail = false
#    begin
#        sleep(1)
#        time2 = Time.now
#        sec_diff = time2 - time1
#        ecounters = $ts.dut.call("mesa_port_counters_get", $ts.dut.p[eg])
#        if (ecounters["prio"][3]["tx"] != low_count)
#            ecounters1 = $ts.dut.call("mesa_port_counters_get", $ts.dut.p[eg])
#            t_i ("At sec_diff #{sec_diff} Low Priority tx detected.  count0 #{ecounters["prio"][3]["tx"]}  count1 #{ecounters1["prio"][3]["tx"]}")
#            t_i ("Port tx count0 #{ecounters["rmon"]["tx_etherStatsPkts"]}  tx count1 #{ecounters1["rmon"]["tx_etherStatsPkts"]}")
#            low_count = ecounters1["prio"][3]["tx"]
#            fail = true
#        end
#    end until (sec_diff) >= (pre_tx+sec+2)
#    if fail
#        t_e("Failed as low priority tx is counted")
#    end

    counters_window_before = nil
    counters_window_after = nil
    begin
        t_i("Wait for necessary amount of frames to be transmitted")
        # Bracket exactly the window pcap_analyze will verdict (it skips the pre_tx ramp-up and the trailing drain),
        # so the offered-load check below judges the same window as the pass/fail result it is meant to explain
        sleep(pre_tx)
        counters_window_before = measure_counters_get(ig, eg)
        sleep(sec)
        counters_window_after = measure_counters_get(ig, eg)
        sleep(2)
    ensure
        # Always stop tcpdump, even if the test is interrupted or a check
        # raises during the capture window. tcpdump runs forever until killed;
        # if orphaned it keeps the pcap open on the PC, and the next run's
        # `rm` turns it into a deleted-but-open file that leaks disk.
        # try_ignore so cleanup never masks the real error.
        t_i("Kill the tcpdump process")
        $ts.pc.try_ignore("kill -s SIGHUP #{pid_tcp}")

        # Read the counters here, so the window matches the capture. Waiting for tcpdump to flush includes the ef transmitters stopping one by one
        counters_after = measure_counters_get(ig, eg)
        pc_drops_after = pc_drop_counters_get($ts.pc.p[eg])

        # Wait (bounded) for tcpdump to actually exit, polling from Ruby with a
        # single `kill -0` per iteration. 
        t_i("Wait for tcpdump process to terminate")
        50.times do
            break unless $ts.pc.try_ignore("kill -0 #{pid_tcp}")[:res] == 0
            sleep(0.2)
        end
    end

    t_i("Kill Easy Frame transmitters")
    pid_ef.each do |pid|
        $ts.pc.try_ignore("sudo pkill -KILL -P #{pid}")
        $ts.pc.try_ignore("sudo kill  -KILL    #{pid}")
    end
    # The kill above only reaches the wrapper and its direct children. An ef orphaned below that keeps transmitting for the rest of its repeat count, so sweep per port as well
    ig.each do |ig_value|
        pc_port = $ts.pc.p[ig_value]
        # Spaces as '.' keep the pattern one argv token, the bracketed first letter keeps it from matching the sweep itself
        $ts.pc.try_ignore("sudo pkill -KILL -f ef.tx.[#{pc_port[0]}]#{pc_port[1..-1]}.")
    end

    t_i("Wait for Easy Frame transmitters to stop")
    max = 0
    begin   # Check that all transmitters are stopping
        rx_cnt = counter_get("RX", $ts.pc.p[eg])
        max = max + 1
    end while (rx_cnt != counter_get("RX", $ts.pc.p[eg])) && (max < max_cnt)
    if (max == max_cnt)
        t_e("Easy Frame transmitting never stopped")
    end

    # tcpdump reports its capture statistics when killed. Dropped frames leave holes in the capture
    dropped = $ts.pc.bg_stderr(pid_tcp)[/(\d+) packets dropped by kernel/, 1]
    t_i("tcpdump dropped #{dropped} frames. The capture is incomplete") if (dropped.to_i > 0)

    counters = measure_counters_delta(counters_before, counters_after)
    measure_counters_report(counters, ig, eg, line_rate_fps)
    pc_drop_counters_report(pc_drops_before, pc_drops_after, $ts.pc.p[eg])

    # Judge the offered load over the same window pcap_analyze verdicts, not the whole capture. A generator that only
    # dips below the shaper rate during that window can still average out fine over the full pre_tx+sec+2 capture
    counters_window = measure_counters_delta(counters_window_before, counters_window_after)

    # Analyzing a capture the generator could not fill only reports the shortfall as a rate deviation
    if (!measure_offered_load_check(counters_window, ig, expected_fps, etolerance))
        t_i("Analyze skipped. The capture is kept in #{fname} on the PC")
        return counters
    end
    # A deficit the DUT's own egress counters confirm over this exact window is real, not worth retrying
    egress_ok = measure_egress_load_check(counters_window, eg, expected_fps, etolerance, pcp)

    t_i("Analyze pcap file")
    expected_count = ""
    expected_tolerance = ""
    expected_pcp = ""
    expected_open_ratio = ""
    expected_cycle = (cycle_time == []) ? "" : "--exp-cycle #{cycle_time[0] / 1000}"
    # Gate-open percentage of the cycle per PCP, for pcap_analyze's hole detection
    open_ratio_pct = erate.each_index.map do |idx|
        (line_rate_fps[idx].to_i > 0) ? [[((expected_fps[idx].to_f / line_rate_fps[idx]) * 100).round, 0].max, 99].min : nil
    end

    if (pcp != [])
        pcp.each_with_index do |pcp_value, pcp_idx|
            expected_pcp << "#{pcp_value},"
            count = sec*expected_fps[pcp_idx]
            expected_count << "#{count},"
            if (count != 0) # If count is expected the tolerance is a percentage of expected count
                tolerance = ((count * etolerance[pcp_idx]) / 100) + ((((count * etolerance[pcp_idx]) % 100) != 0) ? 1 : 0)
            else            # If count is not expected then the tolerance is a number of frames
                tolerance = etolerance[pcp_idx]
            end
            expected_tolerance << "#{tolerance},"
            expected_open_ratio << "#{open_ratio_pct[pcp_idx] || 50},"
        end
        sp_opt = strict_priority ? " --strict-priority --sp-slack #{sp_slack}" : ""
        cmd = "pcap_analyze.rb --frame-count pcp --pre-tx-sec #{pre_tx} --count-sec #{sec} --pcp_values #{expected_pcp} --exp-count #{expected_count} --exp-tolerance #{expected_tolerance} #{expected_cycle} --exp-open-ratio #{expected_open_ratio}#{sp_opt} #{fname}"
    else
        expected_count = sec*expected_fps[0]
        expected_tolerance = ((expected_count * etolerance[0]) / 100) + ((((expected_count * etolerance[0]) % 100) != 0) ? 1 : 0)
        cmd = "pcap_analyze.rb --frame-count all --pre-tx-sec #{pre_tx} --count-sec #{sec} --exp-count #{expected_count} --exp-tolerance #{expected_tolerance} #{fname}"
    end

    # A deficit neither DUT counter saw is likely generator-host jitter, not a real DUT problem, so retry once.
    # try_ignore keeps a retried attempt from marking the test failed before we know it needs to be
    last_attempt = (attempt >= MEASURE_MAX_ATTEMPTS) || !egress_ok
    res = last_attempt ? $ts.pc.try(cmd) : $ts.pc.try_ignore(cmd)

    # pcap_analyze.rb saved a copy when it failed. Keep the analyzed file too, the next measurement removes it again
    if (res[:res] == 0)
        $ts.pc.run("rm -f #{fname}")
        break
    elsif (last_attempt)
        t_i("Analyze failed. The capture is kept in #{fname} on the PC")
        break
    else
        t_i("Analyze failed on attempt #{attempt}/#{MEASURE_MAX_ATTEMPTS}, but the DUT's own ingress and egress counters over this exact window show no deficit, retrying, likely the shared generator host's scheduler jitter rather than a DUT issue")
    end
    end
    # Loop End

    return counters
    ensure
        rate_lock_release(rate_lock)
    end
end

def measure(ig, eg, size, sec=1, frame_rate=false, data_rate=false, erate=[1000000000], etolerance=[1], with_pre_tx=false, pcp=[], cycle_time=[], size_array=[], strict_priority: false, sp_slack: 0)
    test "measure  ig: #{ig}  eg: #{eg}  size: #{size}  sec: #{sec}  frame_rate #{frame_rate}  data_rate #{data_rate}  erate #{erate}  etolerance #{etolerance}  with_pre_tx: #{with_pre_tx}  pcp #{pcp}  cycle_time #{cycle_time}#{strict_priority ? "  strict_priority sp_slack: #{sp_slack}" : ""}" do
        cfg = {}
        cfg[:ig] = ig
        cfg[:eg] = eg
        cfg[:size] = size
        cfg[:sec] = sec
        cfg[:frame_rate] = frame_rate
        cfg[:data_rate] = data_rate
        cfg[:erate] = erate
        cfg[:etolerance] = etolerance
        cfg[:with_pre_tx] = with_pre_tx
        cfg[:pcp] = pcp
        cfg[:cycle_time] = cycle_time
        cfg[:size_array] = size_array
        cfg[:strict_priority] = strict_priority
        cfg[:sp_slack] = sp_slack
        check_rate(cfg)
    end # test
end

# Calculate bit rate (in bits per second) from number of bytes during duration uS
def bit_rate bytes, duration
    bytes.to_f * 8 * 1000000 / duration
end

# Calculate frame rate (in frames per second) from number of frames during duration uS
def frame_rate frames, duration
    frames.to_f * 1000000 / duration
end

# tx_capture() transmits and capture frames
#
# ts: test setup
# tx: Transmit on this interface on the pc
# capture: Capture on this interface on the pc. Set to nil ti disable.
# no_rx: (optional) Check that no frames are received on these interfaces on the pc. Set to nil disable.
# frame: Test frame specified as an EasyFrame FRAME
# frame_size: Size of transmitted frame. Padded with enough zeroes.
# num_frames: Number of frames to transmit.
# pause: (optional) Count number of pauses greater than this value. Set to nil to disable.
#
# Returns hash of values calculated from capture or nil if no capture
def tx_capture ts, tx, capture, no_rx, frame, frame_size, num_frames, pause = nil
    if capture
        cap = "-c #{capture},20,adapter_unsynced"
    end

    if no_rx
        if no_rx.is_a?(Array)
            rx = no_rx.map { |x| "rx #{x}" }.join(" ") # Array
        else
            rx = "rx #{no_rx}" # Single value
        end
    end

    if num_frames > 1
        rep = "rep #{num_frames}"
    end

    # Calculate current frame size
    res = ts.pc.run "ef hex #{frame} data repeat 64 0" # Add 64 bytes to eliminate padding
    fsz = (res[:out].chomp.length / 2) - 64 # Subtract 64 bytes to find length excluding padding
    payload = frame_size - fsz

    ts.pc.run_with_stderr_as_info "ef #{cap} #{rx} tx #{tx} #{rep} #{frame} data repeat #{payload} 0"

    return nil if capture.nil?

    pkts = ts.pc.get_pcap "#{capture}.pcap"
    bytes = 0
    duration = 0.0
    pauses = 0.0

    pkts.each do |p|
        #t_i "TS: %4d, TSD: %4d, len: %4d #{hexstr(p[:data])}" % [p[:us_rel], p[:us_delta], p[:len_on_wire]]
        bytes += p[:len_on_wire]
        duration = p[:us_rel] # Just save the last one
        pauses += 1 if pause && (p[:us_delta] >= (pause / 1000))
    end

    if duration == 0 # Probably only captured a single frame
        bps = 0
        fps = 0
        pps = 0
    else
        bps = bit_rate(bytes, duration)
        fps = frame_rate(pkts.size, duration)
        pps = pauses * 1_000_000 / duration
    end

    #t_i "#{capture}: frames: #{pkts.size}, bytes: #{bytes}, duration in usec: #{duration}, bps: #{bps}, fps: #{fps}, pps: #{pps}"
    {:frames => pkts.size, :bytes => bytes, :duration => duration, :bps => bps, :fps => fps, :pps => pps}
end


# Wait for network interfaces to come up or down
# ts: test setup
# device: :dut or :pc
# iface: An interface or an array of interfaces. "eth1" or ["eth1","eth2","eth3"]
# timeout: time (in seconds) to wait in total
# sleep_time: time (in seconds) to wait between polls
# Add :down in flags in order to wait for the interface(s) to come down.
def wait_iface_state ts, device, iface, timeout = 10, sleep_time = 1, flags = []
    case device
    when :dut
        raise "Not yet supported on the MESA API!" if ts.dut.api == :mesa
    when :pc
    else
        raise "Invalid device!"
    end

    ni = []
    if iface.is_a? Array
        ni = iface
    else
        ni << iface
    end

    b = Time.now.to_f
    ok = []

    first = true

    while (Time.now.to_f - b) < timeout
        if first
            first = false
        else
            sleep sleep_time
        end

        ni.each do |i|
            next if ok.include? i

            res = ts.pc.run "ip link show dev #{i}" if device == :pc
            res = ts.dut.run "ip link show dev #{i}" if device == :dut
            if /<([^>]+)>/ =~ res[:out].lines.first
                f = $1.split ","

                if flags.include? :down
                    if not f.include? "LOWER_UP"
                        ok << i
                    end
                else
                    if f.include? "LOWER_UP"
                        ok << i
                    end
                end
            end
        end

        break if ok.size == ni.size
    end

    raise "Timeout! The following interfaces are still pending: #{ni - ok}" if ok.size != ni.size
end

# Input is a string containing pairs of data such as the output from ethtool -S <device>:
# # ethtool -S eth8
# NIC statistics:
#      rx_octets: 20682
#      rx_unicast: 200
#      rx_multicast: 85
#      rx_broadcast: 0
#      .
#      .
# Each pair is here separated by "\n" and the name and counter is separated by ": "
# These are the default separators.
#
# Names and counters are converted to keys and values in a hash
#
# All incomplete pairs, e.g "NIC statistics:" which has no value, are left out by default.
# Add :keep_nil in flags in order to keep keys without values.
#
# It is possible to return all keys as symbols ("rx_octets" -> :rx_octets).
# Add :to_sym in flags in order convert all keys.
#
# All values that can be translated to an integer is done so by default.
# Add :no_int in flags in order to keep all values as strings.
#
# The string above generates a hash like this:
# {"rx_octets"=>20682, "rx_unicast"=>200, "rx_multicast"=>85, "rx_broadcast"=>0, ..}
#
def string_pairs_to_hash str, arr_sep = "\n", key_sep = ": ", flags = []
    hash = {}
    array = str.split(arr_sep)
    array.each do |a|
        kv = a.split(key_sep).map { |s| s.strip } # Split and remove whitespace
        if flags.include? :to_sym
            k = kv[0].to_sym # Convert to symbol
        else
            k = kv[0] # Keep it as string
        end
        if flags.include? :no_int
            v = kv[1] # Keep it as string
        else
            v = Integer(kv[1]) rescue kv[1] # Convert to integer if possible
        end
        if flags.include? :keep_nil
            hash[k] = v # Add everything including nil values
        else
            hash[k] = v if not v.nil? # Add everything but nil values
        end
    end
    return hash
end

# Get interface statistics and return them in a hash
# ts: test setup
# device: :dut or :pc
# iface: An interface or an array of interfaces. "eth1" or ["eth1","eth2","eth3"]
# prev: Previous statistics for making diffs (optional)
# If present, prev is expected to be generated with the same interfaces(s)
# Output is a hash like this
# {"eth1"=>{"rx_octets"=>200, "rx_unicast"=>2, "rx_multicast"=>1, "rx_broadcast"=>0, ..},
#  "eth2"=>{"rx_octets"=>4000, "rx_unicast"=>20, "rx_multicast"=>5, "rx_broadcast"=>1, ..}}
def get_iface_statistics ts, device, iface, prev = nil
    case device
    when :dut
        raise "Not yet supported on the MESA API!" if ts.dut.api == :mesa
    when :pc
    else
        raise "Invalid device!"
    end

    ni = []
    if iface.is_a? Array
        ni = iface
    else
        ni << iface
    end

    stat = {}
    diff = {}
    ni.each do |i|
        s = ts.pc.run "ethtool -S #{i}" if device == :pc
        s = ts.dut.run "ethtool -S #{i}" if device == :dut
        h = string_pairs_to_hash s[:out]
        stat[i] = h
        if not prev.nil?
            diff[i] = prev[i].merge(h) {|k, o, n| n - o}
        end
    end
    if prev.nil?
        return stat
    else
        return stat, diff
    end
end

# percent deviation between actual and expected
def percent_deviation act, exp
    if exp.to_i == 0
        pct = (act.to_f - exp.to_f) * 100 / Float::MIN # Avoid divide by zero
    else
        pct = (act.to_f - exp.to_f) * 100 / exp.to_f
    end
    return pct.abs
end

# returns true if deviation is within limit
def percent_deviation_check act, exp, dev
    return (percent_deviation(act, exp) <= dev.to_f)
end

# absolute deviation between actual and expected
# returns true if deviation is within limit
def absolute_deviation act, exp
    diff = act.to_f - exp.to_f
    return diff.abs
end

# returns true if deviation is within limit
def absolute_deviation_check act, exp, dev
    return (absolute_deviation(act, exp) <= dev.to_f)
end

# Evaluate statistics counters
# Use the output from get_iface_statistics
# Counters not mentioned in expect is expected to be zero.
# Add :ignore in flags in order to ignore counters not mentioned.
#
# The following formats are supported in the expected values:
#  A literal integer:     2000
#  String interpolation: "#{NUMBER * 2}"
#  Absolute deviation:   "2000 +- 30"
#  Deviation in percent: "2000 +- 10 %"
#  A logical expression: "2000 <= %a and %a <= 3000 and %a != 2345"
#                        where %a is a placeholder for the actual value:
# Expect format example:
# {"eth1"=>{"rx_octets"=>200, "rx_unicast"=>"#{NUM}", "rx_multicast"=>"2000 +- 30"},
#  "eth2"=>{"rx_octets"=>"2000 <= %a and %a <= 3000 and %a != 2345", "rx_unicast"=>"2000 +- 10 %"}}
#
def eval_statistics actual, expect, flags = []
    err = 0
    remaining = {}

    actual.each do |dev, stat|
        stat.each do |k, v|
            if expect[dev].nil? || expect[dev][k].nil?
                remaining[dev] = {} if remaining[dev].nil? # Create first level hash
                remaining[dev][k] = v if not flags.include? :ignore
            else
                e = expect[dev][k]
                if e.is_a? String and e.match('\A\s*(\d+)\s*\Z')
                    # This String is a single integer - convert it to an Integer
                    e = Integer(e)
                end

                if e.is_a? String
                    s = e.gsub("%a", "#{v}") # Replace placeholder with actual for use with eval
                    if e.match('\A\s*(\d+)\s*\+-\s*(\d+)\s*\Z') # Match on e.g. '2000 +- 30'
#                        t_i "Match #{e} #{$1} #{$2} abs"
                        t_e "ERROR: #{dev} #{k}: Actual = #{v} is not #{e}!" if not absolute_deviation_check(v, $1, $2)
                        err += 1
                    elsif e.match('\A\s*(\d+)\s*\+-\s*(\d+)\s*%\s*\Z') # Match on e.g. '2000 +- 10 %'
#                        t_i "Match #{e} #{$1} #{$2} pct"
                        t_e "ERROR: #{dev} #{k}: Actual = #{v} is not #{e}!" if not percent_deviation_check(v, $1, $2)
                        err += 1
                    elsif eval("(#{s})") == false # E.g. eval (100 < %a and %a < 300) returned false
                        t_e "ERROR: #{dev} #{k}: Actual = #{v}. Expected (#{s}) is false!"
                        err += 1
                    else # E.g. eval (100 < %a and %a < 300) returned true
#                        t_i "Match #{e} (#{s})"
                    end
                else # Integer
                    if v != e
                        t_e "ERROR: #{dev} #{k}: Actual = #{v}. Expected #{e}!"
                        err += 1
                    end
                end
            end
        end
    end

    remaining.each do |dev, stat|
        stat.each do |k, v|
            if v > 0
                t_e "ERROR: #{dev} #{k}: Actual = #{v}. Expected 0!"
                err += 1
            end
        end
    end
    return err
end

def dl_file uri, out
    FileUtils.mkdir_p File.dirname(out)
    puts "Downloading #{uri} to #{out}"
    IO.write(out, Net::HTTP.get(URI(uri)))
end

def run_(cmd, verify=true)
    puts "Executing #{cmd}"
    res = system cmd
    if verify
        raise "Running '#{cmd}' failed" if $? != 0
    end
    res
end

def int2hex(val)
    val = ("0x%08x" % val)
end

def qspi_rw(reg, fld = "", val = "")
    if (fld == "")
        # Read
        i = (reg.length - 1)
        if (reg[i] == ".")
            reg = reg[0..(i - 1)]
            fld = "."
        end
    elsif (val == "")
        # Write register
        val = int2hex(fld)
        fld = ""
    else
        # Write field
        fld = ".#{fld}"
        val = int2hex(val)
    end
    $ts.dut.run("symreg qspi_qspi_#{reg}[1]#{fld} #{val}")
end

$io_fpga_dev = nil

# Intialize QSPI
def qspi_init
    ol = "/sys/kernel/config/device-tree/overlays/tsys01"
    $ts.dut.run("mount -t configfs none /sys/kernel/config")
    $ts.dut.run("mkdir -p #{ol}")
    $ts.dut.run("sh -c 'cat /overlays/lan966x_pcb8309_qspi_rte.dtbo > #{ol}/dtbo'")
end

def io_read_val(txt)
    i = txt.index("value: ")
    if (i == nil)
        txt = "0xdeaddead"
    else
        txt = txt[(i + 7)..(i + 16)]
    end
    txt
end

def io_fpga_rw(cmd)
    if ($io_fpga_dev == nil)
        # Detect device (device 0 currently fails, so we count down)
        vcore_rw(0x40000100, 0x11223344)
        for i in (5).downto(0) do
            dev = "/dev/hidraw#{i}"
            txt = $ts.pc.run("mera-iofpga-rw #{dev} read 0x100")[:out]
            if (txt.include? "0x44332211")
                $io_fpga_dev = dev
                break
            end
        end
        vcore_rw(0x40000100, 0)
        if ($io_fpga_dev == nil)
            t_e("IO-FPGA device not detected")
            return
        else
            txt = $ts.pc.run("mera-iofpga-rw #{$io_fpga_dev} read 0x218")[:out]
            val = io_read_val(txt).to_i(16)
            t_i("IO-FPGA device: #{$io_fpga_dev}")
            exp = 47 # Expect at least this version
            txt = "IO-FPGA version: #{val}, expected >= #{exp}"
            if (val < exp)
                t_e(txt)
            else
                t_i(txt)
            end
        end
    end
    txt = $ts.pc.run("mera-iofpga-rw #{$io_fpga_dev} #{cmd}")[:out]
    if (cmd.include? "read")
        txt = io_read_val(txt)
    end
    txt
end

def io_sram_rw(cmd)
    txt = $ts.dut.run("mera-sram-rw #{cmd}")[:out]
    if (cmd.include? "read")
        txt = io_read_val(txt)
    end
    txt
end

def vcore_rw(addr, val = "")
    symreg = true
    value = val
    read = (val == "" ? true : false)
    if (symreg)
        addr = int2hex(addr)
        if (read)
            $ts.dut.run("symreg -r gcb_va_addr #{addr}")
            $ts.dut.run("symreg -r gcb_va_data")
            $ts.dut.run("symreg -r gcb_va_ctrl")
            txt = $ts.dut.run("symreg -r gcb_va_data_inert")[:out]
            value = txt[2..11].to_i(16)
        else
            val = int2hex(val)
            $ts.dut.run("symreg -r gcb_va_addr #{addr}")
            $ts.dut.run("symreg -r gcb_va_ctrl")
            $ts.dut.run("symreg -r gcb_va_data #{val}")
        end
    else
        if (read)
            value = $ts.dut.call("mesa_reg_read", 0, addr)
        else
            $ts.dut.call("mesa_reg_write", 0, addr, val)
        end
        addr = int2hex(addr)
    end
    val = int2hex(value)
    t_i("vcore_#{read ? "read " : "write"}: #{addr}:#{val}")
    value
end

def io_rw(addr, io, val = "")
    txt = "0xdeaddead"
    swap = (io == "VCORE")
    if (swap and val != "")
        # Little-endian swapping before writing
        v0 = ((val >> 24) & 0xff)
        v1 = ((val >> 16) & 0xff)
        v2 = ((val >>  8) & 0xff)
        v3 = ((val >>  0) & 0xff)
        val = ((v3 << 24) + (v2 << 16) + (v1 << 8) + v0)
    end
    if (io == "QSPI")
        if (val == "")
            txt = io_fpga_rw("read #{addr}")
        else
            io_fpga_rw("write #{addr} #{val}")
        end
    elsif (io == "SRAM")
        if (val == "")
            txt = io_sram_rw("read #{addr}")
        else
            io_sram_rw("write #{addr} #{val}")
        end
    elsif (io == "VCORE")
        # SRAM via VCore
        value = vcore_rw(addr + 0x00100000, val)
        if (val == "")
            txt = int2hex(value)
        end
    else
        t_e("unknown I/O: #{io}")
    end
    if (swap)
        # Little-endian swapping after reading
        txt = ("0x" + txt[8..9] + txt[6..7] + txt[4..5] + txt[2..3])
    end
    txt
end

def io_rd(addr, io)
    io_rw(addr, io)
end

def io_wr(addr, val, io)
    io_rw(addr, io, val)
end

def io_str_rd(addr, str, io = "QSPI")
    len = str.length
    res = ""
    if (len.odd?)
        t_e("odd length: #{len}")
        return res
    end
    n = (addr & 3)
    addr = (addr - n)
    while (len > 0)
        txt = io_rd(addr, io)
        # Format '0x12345678'
        i = (2 + n * 2)
        j = (i + len - 1)
        if (j > 9)
            j = 9
        end
        res = (res + txt[i..j])
        n = 0
        len = (len + i - j - 1)
        addr = (addr + 4)
    end
    res
end

def io_str_wr(addr, str, io = "QSPI")
    len = str.length
    if (len.odd?)
        t_e("odd length: #{len}")
        return
    end
    n = (addr & 3)
    addr = (addr - n)
    i = 0
    while (i < len)
        # Format '0x12345678'
        txt = "0xffffffff"
        cnt = (len - i)
        if (n > 0 or cnt < 8)
            txt = io_rd(addr, io)
        end
        j = (2 + n * 2)
            if ((j + cnt) > 10)
                cnt = (10 - j)
            end
            n = 0
            txt[j..(j + cnt - 1)] = str[i ..(i + cnt - 1)]
            io_wr(addr, txt.to_i(16), io)
            addr = (addr + 4)
            i = (i + cnt)
    end
end

def test_sleep(n)
    t_i("sleeping #{n} seconds")
    sleep(n)
end
