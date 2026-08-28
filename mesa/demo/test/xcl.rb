#!/usr/bin/env ruby

# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

require_relative 'libeasy/et'

$ts = get_test_setup("mesa_pc_b2b_4x")

# Check if extended ACL lookup supported
cap_check_exit("ACL_EXT_LOOKUP")

test_table = [
    {
        # IPv4/IPv6 frames are matched with VCL gkey and mapped to ACL policy
        txt: "vcl-lookup-gkey",
        vcl: [{id: 1, key: {lookup: 0}, act: {gkey_mode: "ADD", gkey: 1}},
              {id: 2, key: {lookup: 1, type: "IPV4"}, act: {gkey_mode: "ADD", gkey: 1}},
              {id: 3, key: {lookup: 1, type: "IPV6"}, act: {gkey_mode: "REPLACE", gkey: 3}},
              {id: 4, key: {lookup: 2, gkey: 2}, act: {policy_no: 2}},
              {id: 5, key: {lookup: 2, gkey: 3}, act: {policy_no: 3}}],
        acl: [{id: 1, key: {policy: 2}, act: {port_action: "FILTER", idx: 2}},
              {id: 2, key: {policy: 3}, act: {port_action: "FILTER", idx: 3}}],
        frm: [{cmd: "ipv4", fwd: [{idx_tx: 0}, {idx_rx: 2}]},
              {cmd: "ipv6", fwd: [{idx_tx: 0}, {idx_rx: 3}]}],
    },
    {
        # Frames are copied to CPU/NPI port using VCL rule
        txt: "vcl-cpu-copy",
        npi: {idx: 1, queue: 7},
        vcl: [{act: {cpu: true, cpu_queue: 7}}],
        frm: [{drop: false, fwd: [{idx_tx: 0}, {idx_rx: 1, ifh_rx: 0}]}],
    },
    {
        # IPv4/IPv6 frames are filtered/redirected
        txt: "vcl-port-action",
        idx_dis: 2,
        cfg: [{idx: 1, uvid: 4096}],
        vcl: [{id: 1, key: {type: "IPV4"}, act: {port_action: "FILTER", idx: 1}},
              {id: 2, key: {type: "IPV6"}, act: {port_action: "REDIR", idx: 0}},
              {id: 3, key: {type: "ANY"}, act: {vid: 2, port_action: "ADD", idx: [1, 2]}}],
        frm: [{cmd: "ipv4", fwd: [{idx_tx: 0}, {idx_rx: 1}]},
              {cmd: "ipv6", fwd: [{idx_tx: 0}, {idx_rx: 0}]},
              {fwd: [{idx_tx: 0}, {idx_rx: 1}]}],
    },
    {
        # IPv4/IPv6 frames are matched by rules with different key size
        txt: "vcl-port-keys",
        vpc: [{idx: 0, tab: [{i: 0, key_type: "DOUBLE_TAG"},
                             {i: 1, key_type: "NORMAL"},
                             {i: 2, key_type: "IP_ADDR"},
                             {i: 3, key_type: "MAC_IP_ADDR"}]}],
        vcl: [{id: 1, key: {lookup: 0}, act: {gkey_mode: "ADD", gkey: 1}},
              {id: 2, key: {lookup: 1, type: "IPV4"}, act: {gkey_mode: "ADD", gkey: 1}},
              {id: 3, key: {lookup: 2, type: "IPV4"}, act: {gkey_mode: "ADD", gkey: 1}},
              {id: 4, key: {lookup: 3, type: "IPV4", gkey: 3}, act: {port_action: "FILTER", idx: 1}},
              {id: 5, key: {lookup: 1, type: "IPV6"}, act: {gkey_mode: "ADD", gkey: 2}},
              {id: 6, key: {lookup: 2, type: "IPV6"}, act: {gkey_mode: "ADD", gkey: 2}},
              {id: 7, key: {lookup: 3, type: "IPV6", gkey: 5}, act: {port_action: "FILTER", idx: 2}}],
        frm: [{cmd: "ipv4", fwd: [{idx_tx: 0}, {idx_rx: 1}]},
              {cmd: "ipv6", fwd: [{idx_tx: 0}, {idx_rx: 2}]}],
    },
    {
        # IPv4/IPv6 frames are matched by rules selecting the key encoding.
        # The port key generation is configured after the rules are added.
        txt: "vcl-rule-keys",
        vpc_last: true,
        vpc: [{idx: 0, tab: [{i: 0, key_type: "DOUBLE_TAG"},
                             {i: 1, key_type: "NORMAL", dmac_dip: true},
                             {i: 2, key_type: "IP_ADDR"},
                             {i: 3, key_type: "MAC_IP_ADDR"}]}],
        vcl: [{id: 1, key: {lookup: 0, key_type: "DOUBLE_TAG"}, act: {gkey_mode: "ADD", gkey: 1}},
              {id: 2, key: {lookup: 1, type: "IPV4", key_type: "NORMAL", dmac_dip: true}, act: {gkey_mode: "ADD", gkey: 1}},
              {id: 3, key: {lookup: 2, type: "IPV4", key_type: "IP_ADDR"}, act: {gkey_mode: "ADD", gkey: 1}},
              {id: 4, key: {lookup: 3, type: "IPV4", gkey: 3, key_type: "MAC_IP_ADDR"}, act: {port_action: "FILTER", idx: 1}},
              {id: 5, key: {lookup: 1, type: "IPV6", key_type: "NORMAL", dmac_dip: true}, act: {gkey_mode: "ADD", gkey: 2}},
              {id: 6, key: {lookup: 2, type: "IPV6", key_type: "IP_ADDR"}, act: {gkey_mode: "ADD", gkey: 2}},
              {id: 7, key: {lookup: 3, type: "IPV6", gkey: 5, key_type: "MAC_IP_ADDR"}, act: {port_action: "FILTER", idx: 2}}],
        frm: [{cmd: "ipv4", fwd: [{idx_tx: 0}, {idx_rx: 1}]},
              {cmd: "ipv6", fwd: [{idx_tx: 0}, {idx_rx: 2}]}],
    },
    {
        # Frames are matched in different lookups and actions are combined
        txt: "acl-lookup",
        npi: {idx: 3, queue: 5},
        acl: [{id: 1, key: {lookup: 0, idx: 0}, act: {cpu: true, cpu_queue: 5}},
              {id: 2, key: {lookup: 1, idx: 1}, act: {cpu: true, cpu_queue: 5}},
              {id: 3, key: {lookup: 2, idx: 0}, act: {port_action: "FILTER", idx: 2}},
              {id: 4, key: {lookup: 3, idx: 1}, act: {port_action: "FILTER", idx: 0}}],
        frm: [{fwd: [{idx_tx: 0}, {idx_rx: 2}, {idx_rx: 3, ifh_rx: 0}]},
              {fwd: [{idx_tx: 1}, {idx_rx: 0}, {idx_rx: 3, ifh_rx: 1}]}],
    },
    {
        # ETYPE frames are matched by rules with different key size
        txt: "acl-port-key-etype",
        apc: [{idx: 0, tab: [{i: 0, etype: "ETYPE"},
                             {i: 1, etype: "DEFAULT"},
                             {i: 2, etype: "EXT"}]}],
        acl: [{id: 1, key: {lookup: 0, type: "ETYPE"}, act: {port_action: "FILTER", idx: [1,2]}},
              {id: 2, key: {lookup: 1, type: "ETYPE"}, act: {port_action: "FILTER", idx: [2,3]}},
              {id: 3, key: {lookup: 2, type: "ETYPE", type_ext: true}, act: {port_action: "FILTER", idx: [1,3]}}],
        frm: [{cmd: "et 0xaaaa", fwd: [{idx_tx: 0}]}],
    },
    {
        # ARP frames are matched by rules with different key size
        txt: "acl-port-key-arp",
        apc: [{idx: 0, tab: [{i: 0, arp: "ETYPE"},
                             {i: 1, arp: "DEFAULT"},
                             {i: 2, arp: "EXT"}]}],
        acl: [{id: 1, key: {lookup: 0, type: "ETYPE"}, act: {port_action: "FILTER", idx: [1,2]}},
              {id: 2, key: {lookup: 1, type: "ARP"}, act: {port_action: "FILTER", idx: [2,3]}},
              {id: 3, key: {lookup: 2, type: "ARP", type_ext: true}, act: {port_action: "FILTER", idx: [1,3]}}],
        frm: [{cmd: "arp", fwd: [{idx_tx: 0}]}],
    },
    {
        # IPv4 frames are matched by rules with different key size
        txt: "acl-port-key-ipv4",
        apc: [{idx: 0, tab: [{i: 0, ipv4: "ETYPE"},
                             {i: 1, ipv4: "DEFAULT"},
                             {i: 2, ipv4: "EXT"}]}],
        acl: [{id: 1, key: {lookup: 0, type: "ETYPE"}, act: {port_action: "FILTER", idx: [1,2]}},
              {id: 2, key: {lookup: 1, type: "IPV4"}, act: {port_action: "FILTER", idx: [2,3]}},
              {id: 3, key: {lookup: 2, type: "IPV4", type_ext: true}, act: {port_action: "FILTER", idx: [1,3]}}],
        frm: [{cmd: "ipv4", fwd: [{idx_tx: 0}]}],
    },
    {
        # IPv6 frames are matched by rules with different key size
        txt: "acl-port-key-ipv6",
        apc: [{idx: 0, tab: [{i: 0, ipv6: "ETYPE"},
                             {i: 1, ipv6: "DEFAULT"},
                             {i: 2, ipv6: "EXT"}]}],
        acl: [{id: 1, key: {lookup: 0, type: "ETYPE"}, act: {port_action: "FILTER", idx: [1,2]}},
              {id: 2, key: {lookup: 1, type: "IPV6"}, act: {port_action: "FILTER", idx: [2,3]}},
              {id: 3, key: {lookup: 2, type: "IPV6", type_ext: true}, act: {port_action: "FILTER", idx: [1,3]}}],
        frm: [{cmd: "ipv6", fwd: [{idx_tx: 0}]}],
    },
]

def xcl_port_list(key, is_action = false)
    idx = fld_get(key, :idx, is_action ? [] : 0)
    if (idx.is_a?(Integer))
        idx = [idx]
    end
    return port_idx_list_str(idx)
end

def xcl_vcl_port_conf(t, vpc_old)
    fld_get(t, :vpc, []).each do |e|
        idx = fld_get(e, :idx)
        port = $ts.dut.p[idx]
        c = $ts.dut.call("mesa_vcl_port_conf_get", port)
        # Deep copy of configuration
        vpc_old.push({port: port, conf: Marshal.load(Marshal.dump(c))})
        e[:tab].each do |k|
            i = fld_get(k, :i)
            dmac_dip = fld_get(k, :dmac_dip, false)
            key_type = ("MESA_VCAP_KEY_TYPE_" + fld_get(k, :key_type, "NORMAL"))
            if (i == 0)
                c["dmac_dip"] = dmac_dip
                c["key_type"] = key_type
            else
                l = c["lookup"][i - 1]
                l["dmac_dip"] = dmac_dip
                l["key_type"] = key_type
            end
        end
        $ts.dut.call("mesa_vcl_port_conf_set", port, c)
    end
end

def xcl_test(t)
    # NPI port
    npi = fld_get(t, :npi, nil)
    if (npi != nil)
        c = $ts.dut.call("mesa_packet_rx_conf_get")
        q = fld_get(npi, :queue)
        c["queue"][q]["npi"]["enable"] = true
        $ts.dut.call("mesa_packet_rx_conf_set", c)

        # Enable NPI port
        c = $ts.dut.call("mesa_npi_conf_get")
        c["enable"] = true
        idx = fld_get(npi, :idx)
        c["port_no"] = $ts.dut.p[idx]
        $ts.dut.call("mesa_npi_conf_set", c)
    end

    # VLAN port configuration
    cfg_old = []
    cfg = fld_get(t, :cfg, [])
    cfg.each do |e|
        idx = fld_get(e, :idx)
        port = $ts.dut.p[idx]
        c = $ts.dut.call("mesa_vlan_port_conf_get", port)
        cfg_old.push({port: port, conf: c})
        c["untagged_vid"] = fld_get(e, :uvid)
        $ts.dut.call("mesa_vlan_port_conf_set", port, c)
    end

    # VCL port configuration, done before the rules unless 'vpc_last' is set
    vpc_old = []
    vpc_last = fld_get(t, :vpc_last, false)
    if (!vpc_last)
        xcl_vcl_port_conf(t, vpc_old)
    end

    # Add VCL rules
    vcl = fld_get(t, :vcl, [])
    vcl.each do |e|
        key = fld_get(e, :key, {})
        c = $ts.dut.call("mesa_vce_init", "MESA_VCE_TYPE_" + fld_get(key, :type, "ANY"))
        c["id"] = fld_get(e, :id, 1)

        # Key fields
        k = c["key"]
        k["port_list"] = xcl_port_list(key)
        k["lookup"] = fld_get(key, :lookup)
        gkey = fld_get(key, :gkey, nil)
        if (gkey != nil)
            k["gkey"]["value"][1] = gkey
            k["gkey"]["mask"][1] = 0xff
        end

        # If the key encoding is present, it is selected by the rule instead of
        # the ingress port key generation
        if (key.key?(:key_type) or key.key?(:dmac_dip))
            k["key_enable"] = true
            k["key_type"] = ("MESA_VCAP_KEY_TYPE_" + fld_get(key, :key_type, "NORMAL"))
            k["dmac_dip"] = fld_get(key, :dmac_dip, false)
        end

        # Action fields
        act = fld_get(e, :act, {})
        a = c["action"]
        a["vid"] = fld_get(act, :vid)
        a["policy_no"] = fld_get(act, :policy_no)
        a["cpu"] = fld_get(act, :cpu, false)
        a["cpu_queue"] = fld_get(act, :cpu_queue)
        a["port_action"] = ("MESA_VCL_PORT_ACTION_" + fld_get(act, :port_action, "NONE"))
        a["port_list"] = xcl_port_list(act, true)
        a["gkey_mode"] = ("MESA_VCL_GKEY_MODE_" + fld_get(act, :gkey_mode, "NONE"))
        a["gkey"] = fld_get(act, :gkey)
        $ts.dut.call("mesa_vce_add", 0, c)
    end

    # VCL port configuration after the rules have been added
    if (vpc_last)
        xcl_vcl_port_conf(t, vpc_old)
    end

    # ACL port configuration
    apc_old = []
    apc = fld_get(t, :apc, [])
    apc.each do |e|
        idx = fld_get(e, :idx)        
        port = $ts.dut.p[idx]
        c = $ts.dut.call("mesa_acl_port_conf_get", port)
        # Deep copy of configuration
        apc_old.push({port: port, conf: Marshal.load(Marshal.dump(c))})
        e[:tab].each do |k|
            i = fld_get(k, :i)
            key = (i == 0 ? c["key"] : c["lookup"][i - 1])
            pfx = "MESA_ACL_KEY_"
            key["etype"] = (pfx + fld_get(k, :etype, "DEFAULT"))
            key["arp"] = (pfx + fld_get(k, :arp, "DEFAULT"))
            key["ipv4"] = (pfx + fld_get(k, :ipv4, "DEFAULT"))
            key["ipv6"] = (pfx + fld_get(k, :ipv6, "DEFAULT"))
        end
        $ts.dut.call("mesa_acl_port_conf_set", port, c)
    end

    # Add ACL rules
    acl = fld_get(t, :acl, [])
    acl.each do |e|
        key = fld_get(e, :key, {})
        c = $ts.dut.call("mesa_ace_init", "MESA_ACE_TYPE_" + fld_get(key, :type, "ANY"))
        c["id"] = fld_get(e, :id, 1)

        # Key fields
        c["lookup"] = fld_get(key, :lookup)
        c["port_list"] = xcl_port_list(key)
        policy = fld_get(key, :policy, nil)
        if (policy != nil)
            c["policy"]["value"] = policy
            c["policy"]["mask"] = 0xff
        end
        if (cap_get("PACKET_IFH_EPID") != 11 || c["type"].include?("IP"))
            # FireAnt only supports extended rules for IPv4/IPv6
            c["type_ext"] = fld_get(key, :type_ext, false)
        end

        # Action fields
        act = fld_get(e, :act, {})
        a = c["action"]
        a["cpu"] = fld_get(act, :cpu, false)
        a["cpu_queue"] = fld_get(act, :cpu_queue)
        a["port_action"] = ("MESA_ACL_PORT_ACTION_" + fld_get(act, :port_action, "NONE"))
        a["port_list"] = xcl_port_list(act, true)
        $ts.dut.call("mesa_ace_add", 0, c)
    end

    # Disable port
    idx_dis = fld_get(t, :idx_dis, nil)
    if (idx_dis != nil)
        $ts.dut.call("mesa_stp_port_state_set", $ts.dut.p[idx_dis], "MESA_STP_STATE_DISCARDING")
    end

    # Frames
    frm_tab = fld_get(t, :frm, [])
    frm_tab.each do |f|
        cmd = "ef"
        cmd_add = ""
        frm = "eth"
        if (f[:cmd] != nil)
            frm += (" " + f[:cmd])
        end
        frm += " data pattern cnt 46"
        idx_list = []
        f[:fwd].each do |e|
            idx = e[:idx_tx]
            dir = "tx"
            if (idx == nil)
                idx = e[:idx_rx]
                dir = "rx"
            end
            idx_list.push(idx)
            cmd += " name f#{idx}"
            ifh_rx = fld_get(e, :ifh_rx, nil)
            if (ifh_rx != nil)
                cmd += (" " + cmd_rx_ifh_push({port_idx: ifh_rx}))
            end
            cmd += " #{frm}"
            cmd_add += " #{dir} #{$ts.pc.p[idx]} name f#{idx}"
        end
        drop = fld_get(f, :drop, true)
        $ts.pc.p.each_with_index do |name, idx|
            if (!idx_list.include?(idx))
                cmd_add += " rx #{name}"
                if (!drop)
                    cmd += (" name f#{idx} #{frm}")
                    cmd_add += " name f#{idx}"
                end
            end
        end
        $ts.pc.try(cmd + cmd_add)
    end

    # Return here when debugging a test
    return if (t[:sel] != nil)

    # Restore NPI configuration
    if (npi != nil)
        c = $ts.dut.call("mesa_npi_conf_get")
        c["enable"] = false
        $ts.dut.call("mesa_npi_conf_set", c)
    end

    # Restore VLAN port configuration
    cfg_old.each do |e|
        $ts.dut.call("mesa_vlan_port_conf_set", e[:port], e[:conf])
    end

    # Delete VCL rules
    vcl.each do |e|
        $ts.dut.call("mesa_vce_del", fld_get(e, :id, 1))
    end

    # Restore VCL port configuration
    vpc_old.each do |e|
        $ts.dut.call("mesa_vcl_port_conf_set", e[:port], e[:conf])
    end

    # Delete ACL rules
    acl.each do |e|
        $ts.dut.call("mesa_ace_del", fld_get(e, :id, 1))
    end

    # Restore ACL port configuration
    apc_old.each do |e|
        $ts.dut.call("mesa_acl_port_conf_set", e[:port], e[:conf])
    end

    # Enable port again
    if (idx_dis != nil)
        $ts.dut.call("mesa_stp_port_state_set", $ts.dut.p[idx_dis], "MESA_STP_STATE_FORWARDING")
    end
end

# Run all or selected test
sel = table_lookup(test_table, :sel)
test_table.each do |t|
    if (t[:sel] == sel)
        test t[:txt] do
            xcl_test(t)
        end
    end
end

test_summary

test "dump" do
    #$ts.dut.run("mesa-cmd deb api ci vx action 3")
    #$ts.dut.run("mesa-cmd deb api ci acl action 3")
end
