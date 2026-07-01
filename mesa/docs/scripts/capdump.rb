#!/usr/bin/env ruby

# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

require 'pp'
require 'yaml'
require 'open3'
require 'optparse'

options = {}
OptionParser.new do |opts|
    opts.banner = "Usage: capdump.rb [options] libraries..."

    opts.on("-c", "--capability-header [file]", "Capability header file to parse") do |h|
        options[:hdr] = h
    end

    opts.on("-C", "--capdumper-exec [file]", "Capability dumper executable") do |h|
        options[:exe] = h
    end

    opts.on("-o", "--output [file]", "Where to write the database") do |o|
        options[:out] = o
    end

    opts.on("-h", "--help", "This message") do |v|
        puts opts
        exit
    end

end.parse!

if options[:hdr].nil?
    puts "No header specified"
    exit
end

if options[:exe].nil?
    puts "No capability dumper executable"
    exit
end

if ARGV.size == 0
    puts "No libraries specified"
    exit
end


CAPS = []
enum_val = 0
# Match actual enumerators only: the MESA_CAP_ token must start the line (after
# indentation) and be followed by an optional "= <value>" and a comma. This
# avoids miscounting MESA_CAP_ tokens that appear inside wrapped doc comments
# (e.g. "... MESA_CAP_TS_DOMAIN_CNT number of domains."), which would otherwise
# shift the enum value of every capability that follows and mislabel the DB.
File.readlines(options[:hdr]).each do |l|
    case l
    when /^\s*(MESA_CAP_\w+)\s*=\s*(\d+)\s*,/
        enum_val = $2.to_i
        CAPS << [$1, enum_val]
    when /^\s*(MESA_CAP_\w+)\s*,/
        enum_val += 1
        CAPS << [$1, enum_val]
    end
end

CAP_DB = {}

# Instance-dependent capabilities (e.g. MESA_CAP_L2_REDBOX_CNT and the other
# mesa_feature()-gated ones) are answered from a chip instance's state, so the
# dumper must be told which mesa_target_type_t to instantiate. For the vscNNNN
# libraries the chip number is the hexadecimal target value, and the TSN/MSEC
# industrial variants add 0x40000. The generic Laguna (lan969x) and LAN966x
# libraries, which support several target variants, are represented by their
# largest / RED variant. A target of 0 selects the default (NULL) instance only,
# which is enough for the purely compile-time capabilities.
LIB_TARGET_OVERRIDE = {
    "lan966x"    => 0x9668, # LAN9668
    "lan966x_lm" => 0x9668,
    "lan969x"    => 0x9698, # Laguna-100
    "lan969x_lm" => 0x9698,
    "lan9698RED" => 0x969C, # Laguna-100-RED (PRP/HSR)
    "p64h"       => 0x6500, # P64H
    "p64h_lm"    => 0x6500,
}

def lib_target(base_lib)
    name = base_lib.sub(/\Alib/, "").sub(/\.so.*\z/, "")
    return LIB_TARGET_OVERRIDE[name] if LIB_TARGET_OVERRIDE.key?(name)

    case name
    when /\Avsc(\h+)(?:TSN|MSEC)\z/ then (0x40000 | $1.to_i(16))
    when /\Avsc(\h+)\z/             then $1.to_i(16)
    else 0
    end
end

# Libraries that should not appear as separate columns in the capability DB:
#  - *MSEC: a MACsec packaging build of the corresponding *TSN chip (identical
#    switch API and capabilities; MACsec lives in the PHY/board layer).
#  - *_lm:  the "light" build variants, which are the same chip as the
#    full-featured library.
def skip_lib?(base_lib)
    name = base_lib.sub(/\Alib/, "").sub(/\.so.*\z/, "")
    name =~ /MSEC\z/ || name =~ /_lm\z/
end

ARGV.each do |lib|
    base_lib = File.basename(lib)
    if skip_lib?(base_lib)
        puts "Skipping #{base_lib}"
        next
    end
    CAP_DB[base_lib] = []
    target = lib_target(base_lib)

    caps = CAPS.clone
    caps_size_old = 0

    while caps.size > 0 and caps_size_old != caps.size
        caps_size_old = caps.size
        c = "#{options[:exe]} #{lib} #{target} #{caps.collect{|x| x[1]}.join(" ")}"

        stdout_str, stderr_str, status = Open3.capture3(c)

        stdout_str.split("\n").each do |l|
            o_cap = l.split.map{|x| x.to_i}

            if caps.size > 0 and caps[0][1] == o_cap[0]
                cap = caps.shift
                puts "%-40s %-60s = %d" % [base_lib, "#{cap[0]}(#{cap[1]})", o_cap[1]]
                CAP_DB[base_lib] << { :str => cap[0], :int => cap[1], :val => o_cap[1], :chip => base_lib }
            end
        end

        if status.to_i != 0
            cap = caps.shift
            puts "%-40s %-60s = <unknown>" % [base_lib, "#{cap[0]}(#{cap[1]})"]
            CAP_DB[base_lib] << { :str => cap[0], :int => cap[1], :val => nil, :chip => base_lib }
        end

    end
end

if options[:out]
    File.write(options[:out], YAML.dump(CAP_DB))
end

