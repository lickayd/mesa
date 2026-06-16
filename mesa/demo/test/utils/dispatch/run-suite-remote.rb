#!/usr/bin/env ruby

# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

require 'open3'
require 'optparse'
require 'pathname'

# Unbuffer stdout so the live log streams to the controller in real time.
$stdout.sync = true

# `timeout` exit codes that mean the suite hit its cap (GNU coreutils):
#   124 = killed by the TERM we sent
#   137 = ignored TERM, so `-k` escalated to SIGKILL (128 + 9)
TIMEOUT_EXIT_TERM = 124
TIMEOUT_EXIT_KILL = 137

# ---------------------------------------------------------------------------------------------------------------------
# Run command
# ---------------------------------------------------------------------------------------------------------------------

# Runs the command, streams its output, and returns its exit status.  Callers
# decide what a non-zero status means (most treat it as fatal; the suite run
# inspects it, since a timeout is an expected non-zero outcome).
def run_cmd(cmd, system)
    puts("running cmd: '#{cmd}'")
    Open3.popen2e(cmd) do |stdin, output, wait_thr|
        stdin.close
        output.each_line { |line| puts(line.chomp) }
        wait_thr.value.exitstatus
    end
rescue Errno::ENOENT => e
    raise "#{system}: command not found: #{e.message}"
end

# ---------------------------------------------------------------------------------------------------------------------
# Option parser
# ---------------------------------------------------------------------------------------------------------------------

$options = {
    :test_to_run => [],
    :out => ".",
    :timeout => 600
}

OptionParser.new do |opts|
    opts.banner = "Usage: run-suite-remote.rb [options]"
    opts.on("-h", "--help", "This message") do |v|
        puts(opts)
        exit
    end
    opts.on("-i", "--image image", "Image path; only the basename (without extension) is used as a label in output file names and JUnit properties") do |i|
        $options[:image_base] = File.basename(i, ".*")
    end
    opts.on("-s", "--system system", "System (DUT) name, used as a label in output file names and JUnit properties") do |s|
        $options[:system] = s
    end
    opts.on("-t", "--timeout seconds", "Per-suite wall-clock cap in seconds (enforced via `timeout`)") do |t|
        $options[:timeout] = t.to_i
    end
    opts.on("-T", "--test path", "Test suite file to run") do |t|
        $options[:test_to_run] = t
    end
    opts.on("-o", "--output folder", "Output folder for test results") do |f|
        $options[:out] = File.expand_path(f)
    end
end.parse!


# ---------------------------------------------------------------------------------------------------------------------
# Main sequence
# ---------------------------------------------------------------------------------------------------------------------

# Absolute out_dir so all output files land in the right place regardless of CWD
$repo_root = `git rev-parse --show-toplevel`.strip
out_dir_rel = Pathname.new($options[:out]).relative_path_from($repo_root).to_s

# Build output paths
suite_name       = File.basename($options[:test_to_run], ".rb")
out_dir          = $options[:out]
out_prefix       = [$options[:image_base], suite_name, $options[:system]].join("-")
out_et_xml       = "#{out_dir}/#{out_prefix}_ET.xml"
out_junit_xml    = "#{out_dir}/#{out_prefix}_JUNIT.xml"
junit_suite_name = [$options[:image_base], suite_name, $options[:system]].join(".")
junit_props      = "-p image=#{$options[:image_base]} -p suite=#{suite_name} -p system=#{$options[:system]}"

# Run suite
puts("Begin suite '#{suite_name}' on '#{$options[:system]}'")
puts("  suite_name       : #{suite_name}")
puts("  out_dir          : #{out_dir}")
puts("  out_et_xml       : #{out_et_xml}")
puts("  out_junit_xml    : #{out_junit_xml}")
puts("  junit_suite_name : #{junit_suite_name}")
puts("  junit_props      : #{junit_props}")

raise "#{$options[:system]}: mkdir -p #{out_dir} failed" unless run_cmd("mkdir -p #{out_dir}", $options[:system]).zero?
# Change to the test directory so the suite script can be invoked as ./suite.rb
# and require_relative paths inside the suite resolve correctly
Dir.chdir("#{$repo_root}/mesa/demo/test/")
# `exit ${PIPESTATUS[0]}` propagates the `timeout` exit code (not tee's, which
# is always 0) so we can tell here whether the suite hit its cap.
suite_cmd = "timeout -k 10 -s TERM #{$options[:timeout]}s ./#{$options[:test_to_run]}" \
            " --test-suite-name #{suite_name}" \
            " | tee #{out_et_xml}" \
            " | libeasy/xml2console.rb --brief -O #{out_dir} -o #{out_dir} -j #{out_junit_xml} -n #{junit_suite_name} #{junit_props}" \
            "; exit ${PIPESTATUS[0]}"

rc = run_cmd("bash -c '#{suite_cmd}'", $options[:system])

# The suite itself records the verdict (the framework reports a timed-out run as
# failed); this is just a clear human line in the log saying which way it went.
if [TIMEOUT_EXIT_TERM, TIMEOUT_EXIT_KILL].include?(rc)
    puts("Suite '#{suite_name}' TIMED OUT after #{$options[:timeout]}s (rc=#{rc})")
else
    puts("Suite '#{suite_name}' run finished (exit #{rc})")
end

# Run tar from repo root with relative path so archive entries are portable
Dir.chdir($repo_root)
raise "#{$options[:system]}: tar of #{out_dir_rel} failed" unless run_cmd("tar -czv #{out_dir_rel} -f out.tar", $options[:system]).zero?

