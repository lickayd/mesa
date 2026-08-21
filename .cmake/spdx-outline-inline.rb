#!/usr/bin/env ruby

# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

# Bake an SPDX JSON document into a standalone copy of spdx-outline.html.
#
# The generic viewer (spdx-outline.html) opens with a drag/drop picker. This
# tool produces a self-contained HTML file with one SPDX document inlined, so it
# renders that SBOM immediately with no server and no file picker - handy for
# shipping a browsable SBOM alongside a release. Drag/drop still works in the
# shipped copy if you want to load a different document.
#
# Usage:
#   spdx-outline-inline.rb <spdx.json> [output.html]
#
# If output.html is omitted, writes <spdx-basename>-outline.html next to the
# input.

require 'json'

HERE        = File.dirname(File.expand_path(__FILE__))
TEMPLATE    = File.join(HERE, "spdx-outline.html")
PLACEHOLDER = '<script id="spdx-data" type="application/json"></script>'

if ARGV.empty? || ["-h", "--help"].include?(ARGV[0])
  STDERR.puts "Usage: #{$0} <spdx.json> [output.html]"
  exit(ARGV.empty? ? 2 : 0)
end

src = ARGV[0]
out = ARGV[1] || File.join(File.dirname(src),
                           "#{File.basename(src, '.*').sub(/\.spdx$/, '')}-outline.html")

template = File.read(TEMPLATE, encoding: "UTF-8")
unless template.include?(PLACEHOLDER)
  raise "placeholder not found in template #{TEMPLATE} (is it an up-to-date spdx-outline.html?)"
end

# Validate it is real JSON, then re-serialize compactly.
doc  = JSON.parse(File.read(src, encoding: "UTF-8"))
data = JSON.generate(doc)

# Neutralize any "</..." that would prematurely close the <script> tag.
# "<\/" is still valid JSON (\/ is an escaped '/') and parses back identically.
data.gsub!("</", "<\\/")

filled = %(<script id="spdx-data" type="application/json">#{data}</script>)
# Block form: avoids String#sub interpreting backslash escapes in the replacement.
html   = template.sub(PLACEHOLDER) { filled }

File.write(out, html)
puts "wrote #{out} (#{html.bytesize} bytes, from #{src})"
