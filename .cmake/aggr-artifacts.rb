#!/usr/bin/env ruby

# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

require 'pp'
require 'open3'
require 'optparse'
require 'json'

$res = 0
$verbose = true
$do_upload = true

$opt = { }
global = OptionParser.new do |opts|
  opts.banner = "Usage: #{$0} [options] output-folder"

  opts.on("--no-upload", "Disable upload to artifactory") do
    $do_upload = false
  end

  opts.on("-o NAME", "output name") do |n|
    $out_name = n
  end
end.order!

def run cmd
  if $verbose
    STDOUT.print "RUN: #{cmd}"
    STDOUT.flush
  end

  a = Time.now
  o, e, s = Open3.capture3(cmd)
  b = Time.now

  if $verbose
    STDOUT.print " -> #{s} in %1.3fs\n" % [b - a]
    STDOUT.flush
  end

  if s.to_i != 0 or e.size > 0
    raise "CMD: #{cmd} status: #{s.to_i}, std-err: #{e}"
  end

  return o, e, s
end

def sys cmd
  if $verbose
    puts "RUN: #{cmd}"
  end

  a = Time.now
  system cmd
  b = Time.now

  if $verbose
    puts "RUN-Done: #{cmd} -> #{$?} in %1.3fs\n" % [b - a]
    STDOUT.flush
  end

  if $?.to_i != 0
    raise "CMD: #{cmd} status: #{$?.to_i}"
  end
end

def try cmd
  begin
    run cmd
  rescue
    puts "CMD: #{cmd} failed (but will continue)"
    $res = 1
  end
end

def cd path
  puts "cd #{path}  (was: #{Dir.pwd})" if $verbose
  Dir.chdir(path)
end

$top = File.dirname(File.dirname(File.expand_path(__FILE__)))
cd $top

git_sha = %x(git rev-parse --short HEAD).chop
git_sha_long = %x(git rev-parse HEAD).chop
begin
    git_id = %x(git describe --tags --long).chop
rescue
    git_id = git_sha
end
if ENV['BRANCH_NAME']
    git_branch = ENV['BRANCH_NAME']
else
    git_branch = %x(git symbolic-ref --short -q HEAD).chop
end

if $out_name.nil?
  $out_name = "mesa-#{git_id}@#{git_branch}"
end

$report_name = "static-analysis-report-#{git_id}-#{git_branch}"
if File.exist? "#{$report_name}"
    run "rm -rf #{$report_name}"
end
sys "mkdir #{$report_name}"
sys "cp -r static_analysis_reports/* #{$report_name}" if File.exist? "static_analysis_reports"

raise "No ws folder" if not File.exist? "./ws"

# Start from a clean packet directory (a previous interrupted run may have left
# one behind; in CI the name is unique per build, but local re-runs reuse it).
run "rm -rf #{$out_name}"
run "cp -r ws #{$out_name}"
run "mkdir #{$out_name}/bin"
try "tar -C #{$out_name}/bin -f arm.tar -x"
try "tar -C #{$out_name}/bin -f arm64.tar -x"
try "tar -C #{$out_name}/bin -f mipsel.tar -x"

# Generate the single release SBOM: it mirrors the assembled bin/ tree (produced
# libraries + bootable images), imports each arch's buildroot rootfs from the
# BSP's own SPDX, and includes the source components. The per-arch BSP SPDX is
# resolved from the pinned BSP version in .cmake/deps-bsp.json. stderr is merged
# into stdout so the logcmd wrapper (which fails on any stderr) is happy.
#
# The BSP SPDX is REQUIRED: a release SBOM that silently shipped an empty rootfs
# placeholder would misrepresent the buildroot components embedded in the bootable
# images. So a missing BSP SPDX is a hard build failure.
#
# The SPDX is sourced from the BSP tarball (not a separate artifactory fetch):
# create_cmake_project.rb copies the tarball's embedded <bsp>.spdx.json into
# bsp-spdx/ during each per-arch build stage, and the CI build stage stashes that
# dir so it reaches this aggregate stage. Prefer that carried copy; fall back to a
# locally-installed BSP under /opt/mchp (dev machines / build containers).
ext = []
bsp_ver = JSON.parse(File.read("#{$top}/.cmake/deps-bsp.json"))[0]["build-artifact-version-string"]
["arm", "arm64", "mipsel"].each do |a|
  name = "mchp-brsdk-#{a}-#{bsp_ver}"
  candidates = ["#{$top}/bsp-spdx/#{name}.spdx.json", "/opt/mchp/#{name}/#{name}.spdx.json"]
  spdx = candidates.find { |p| File.exist?(p) }
  unless spdx
    raise "BSP SPDX not found for #{a} (looked in: #{candidates.join(', ')}).\n" \
          "The release SBOM must import the buildroot rootfs component breakdown from the " \
          "BSP's SPDX (embedded in the BSP tarball). Ensure the per-arch build stage carried " \
          "bsp-spdx/#{name}.spdx.json (via create_cmake_project.rb), or install the pinned BSP " \
          "(#{bsp_ver}, per .cmake/deps-bsp.json) under /opt/mchp, then re-run."
  end
  ext << "--ext-spdx brsdk-#{a}:#{spdx}"
end
# Use sys (streams to the console) rather than run (captures and discards the
# child's output): sbom-spdx's progress and any validation/attribution errors
# must be visible in the build log, while a non-zero exit still fails the build.
sys ".cmake/sbom-spdx --release --bin-tree #{$out_name}/bin #{ext.join(' ')} " \
    "-o #{$out_name}/mesa-binary.spdx.json"

# Bake a standalone, browsable HTML viewer with the SBOM inlined, shipped next to
# the JSON so the release can be inspected without a server or file picker.
run ".cmake/spdx-outline-inline.rb #{$out_name}/mesa-binary.spdx.json " \
    "#{$out_name}/mesa-binary-outline.html 2>&1"

# Generate the source-only SBOM: covers only the MESA/MEPA/MEBA library source
# components (SPDXRef-group-source). This is the relevant SBOM for library
# integrators who do not use the demonstration firmware; it contains none of the
# BSP/rootfs packages and produces a focused, manageable CVE report.
sys ".cmake/sbom-spdx --source -o #{$out_name}/mesa-source.spdx.json"
run ".cmake/spdx-outline-inline.rb #{$out_name}/mesa-source.spdx.json " \
    "#{$out_name}/mesa-source-outline.html 2>&1"

run "tar -czvf #{$out_name}.tar.gz #{$out_name}"

if File.exist? "./images"
  run "cp #{$out_name}/bin/mipsel/mesa/demo/*.mfi images/."
  run "cp #{$out_name}/bin/arm/mesa/demo/*.itb images/."
  run "cp #{$out_name}/bin/arm64/mesa/demo/*.itb images/."
  run "cp #{$out_name}/bin/arm/mesa/demo/*.ext4.gz images/."
  run "cp #{$out_name}/bin/arm64/mesa/demo/*.ext4.gz images/."
  # The SBOMs, VEX files, and their browsable HTML outlines as loose artifacts,
  # so they can be picked up and viewed directly without unpacking the tarball.
  run "cp #{$out_name}/mesa-binary.spdx.json images/."
  run "cp #{$out_name}/mesa-binary-outline.html images/."
  run "cp #{$out_name}/mesa-source.spdx.json images/."
  run "cp #{$out_name}/mesa-source-outline.html images/."
  try "cp #{$out_name}/.vex/mesa-source.openvex.json images/."
end
run "rm -rf #{$out_name}"

days = 10
if git_branch == "master"
  days = 90
elsif git_branch == "master.nightly"
  days = 30
elsif git_branch =~ /^\d\d\d\d.\d\d-soak$/
  days = 180
end

if $do_upload
  cmd = [".cmake/artifactory-ci"]
  cmd << "-vvv"
  cmd << "--days #{days}"
  cmd << "--dep-file .cmake/deps-bsp.json"
  cmd << "--dep-file .cmake/deps-docker.json"
  cmd << "--dep-file .cmake/deps-toolchain.json"
  cmd << "--dep-file .cmake/deps-lmstax.json"
  cmd << "./images " if File.exist? "./images"
  cmd << "#{$out_name}.tar.gz "
  sys cmd.join(" ")
end

run "cp #{$report_name}/* images/." if File.exist? "./images"
run "cp #{$out_name}.tar.gz images/." if File.exist? "./images"

run "rm -rf #{$report_name}"

exit $res

