#!/usr/bin/env ruby

# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

require 'optparse'
require 'fileutils'
require 'net/http'
require 'yaml'
require 'json'
require 'pathname'
require_relative 'test_session'

# ---------------------------------------------------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------------------------------------------------

## Timeout
DEFAULT_TIMEOUT = 3600
EXTRACT_TAR_TIMEOUT_SECS = 600  # 10 minutes — generous; nightlies have seen
                                # rare extract_tar hangs that block forever
                                # without one. Bound the loss to one suite.
PER_SUITE_TIMEOUT_SECS   = 7200 # Default per-suite wall-clock cap (2h), used when
                                # a `-T suite:secs` entry gives no explicit secs.
RESERVE_TIMEOUT_SECS     = 3600 # 1h — how long to keep retrying `et reserve`.
                                # These machines are shared with other (non-nightly)
                                # runs, so a DUT can be legitimately busy for a
                                # while; wait up to an hour, but don't burn the
                                # whole test budget waiting.
UPLOAD_TIMEOUT_SECS      = 1800 # 30 minutes — `et upload` of a multi-MB
                                # firmware image. Bounds upload-image hangs
                                # so the reservation can release cleanly.
HTTP_BUSY_MAX_RETRIES    = 30   # 5 min total at 10s/retry — long enough for
                                # transient easytest server overload, short
                                # enough to bail on stuck queues.
RELEASE_MAX_ATTEMPTS     = 3    # `et release` itself can hit lab HTTP
                                # timeouts.Retry so one hiccup does not leak 
                                # the reservation.
RELEASE_TIMEOUT_SECS     = 120  # Per-attempt cap.  With 3 attempts +
                                # backoff: ~6.5 min worst case.

## POST request config
POST_REQ_REPO_URL   = "https://bitbucket.microchip.com/scm/unge/sw-mesa.git"
POST_REQ_OUT_FILE   = "out.tar"
POST_REQ_CACHE_NAME = File.basename(POST_REQ_REPO_URL, ".git")

## Log configuration (SECTION_HEADER_WIDTH and LOG_WIDTH defined in test_session.rb)
LOG_LABEL_LOCAL      = "[l]"
LOG_LABEL_REMOTE     = "[r]"

## HTTP status codes
HTTP_OK                = 200
HTTP_RUNNING           = 202
HTTP_TOO_MANY_REQUESTS = 429

# ---------------------------------------------------------------------------------------------------------------------
# Formatters
# ---------------------------------------------------------------------------------------------------------------------

def strip_html(body)
    body.gsub(/<[^>]+>/, '')
        .gsub('&amp;', '&').gsub('&lt;', '<').gsub('&gt;', '>').gsub('&quot;', '"')
        .gsub(/\n{3,}/, "\n\n")
        .each_line.map(&:rstrip).reject { |l| l.strip.empty? }.join("\n")
end

def format_html_error(res)
    title   = res.body[/<title[^>]*>(.*?)<\/title>/im,   1]&.strip
    msg     = res.body[/<\/h1>(.*?)<hr/im,               1]&.gsub(/<[^>]+>/, '')&.strip
    address = res.body[/<address[^>]*>(.*?)<\/address>/im, 1]&.gsub(/<[^>]+>/, '')&.split&.join(' ')&.strip

    fields = []
    fields << "\n Fields extracted from HTML:"
    fields << "  Code:    '#{res.code} #{res.message}'"
    fields << "  Title:   '#{title}'"   if title   && !title.empty?
    fields << "  Body:    '#{msg}'"     if msg     && !msg.empty?
    fields << "  Address: '#{address}'" if address && !address.empty?
    fields << "\n"

    fields.join("\n")
end

# ---------------------------------------------------------------------------------------------------------------------
# Loggers
# ---------------------------------------------------------------------------------------------------------------------

def log_write(label, msg)
    $stdout.write msg.to_s.each_line.flat_map { |l|
        l.chomp.scan(/.{1,#{LOG_WIDTH}}/).map { |chunk| "#{label} #{chunk}\n" }
    }.join
end

def log_remote(msg) = log_write(LOG_LABEL_REMOTE, msg)
def log_local(msg)  = log_write(LOG_LABEL_LOCAL,  msg)

def log_subsection_header(title)
    inner = " Subsection: #{title} "
    left  = (SECTION_HEADER_WIDTH - inner.length) / 2
    right = SECTION_HEADER_WIDTH - inner.length - left
    log_local("-" * left + inner + "-" * right)
end

# ---------------------------------------------------------------------------------------------------------------------
# Runners
# ---------------------------------------------------------------------------------------------------------------------

def post_suite(uri, img, out, system, suite, index, timeout, sha)

    suite_name = File.basename(suite, ".rb")
    name_dir = "#{out}/suites/#{[suite_name, system, index].join("-")}"

    req_data = {
        "sha"        => sha,
        "url"        => POST_REQ_REPO_URL,
        "cmd"        => "dr ./mesa/demo/test/utils/dispatch/run-suite-remote.rb -i #{img} -s #{system} -t #{timeout} -T #{suite} -o #{name_dir}",
        "out"        => POST_REQ_OUT_FILE,
        "cache_name" => POST_REQ_CACHE_NAME,
    }

    log_subsection_header("Post suite")
    busy_retries = 0
    loop do
        res = Net::HTTP.post(uri, req_data.to_json)
        msg = "POST #{uri}\n  Request:  #{JSON.pretty_generate(req_data)}\n  Response: #{res.code} #{res.message}"
        log_local(msg)
        if res.code.to_i == HTTP_TOO_MANY_REQUESTS
            busy_retries += 1
            if busy_retries > HTTP_BUSY_MAX_RETRIES
                raise "Server stayed busy (429) for #{HTTP_BUSY_MAX_RETRIES} retries — giving up on '#{suite}'"
            end
            log_local("Server busy (#{HTTP_TOO_MANY_REQUESTS}), retrying in 10s... (#{busy_retries}/#{HTTP_BUSY_MAX_RETRIES})")
            sleep(10)
            next
        end
        raise "#{msg}\n#{format_html_error(res)}" if res.code.to_i >= 400
        break
    end
end

def http_get(uri)
    http = Net::HTTP.new(uri.host, uri.port)
    http.read_timeout = 30
    http.open_timeout = 30
    http.get(uri.request_uri)
end

def extract_tar(body, out)
    log_local("Received #{body.bytesize} bytes, extracting into #{out}")
    in_r, in_w = IO.pipe
    pid = Process.spawn("tar -xzf -", :in => in_r, [:out, :err] => "/dev/null")

    # Write the body in a separate thread: it can exceed the ~64KB pipe buffer,
    # so writing inline could block and stall the timeout poll on a stuck tar.
    writer = Thread.new {
        begin
            in_w.write(body)
        rescue Errno::EPIPE
            # tar died early; ignore
        ensure
            in_w.close rescue nil
        end
    }

    # Poll with WNOHANG rather than Timeout.timeout, which can't interrupt a
    # thread blocked in waitpid (the exception only fires when the syscall
    # returns — never, if tar is hung).
    deadline = Time.now + EXTRACT_TAR_TIMEOUT_SECS
    timed_out = false
    loop do
        done_pid, _status = Process.waitpid2(pid, Process::WNOHANG)
        break if done_pid
        if Time.now >= deadline
            timed_out = true
            log_local("extract_tar timed out after #{EXTRACT_TAR_TIMEOUT_SECS}s (pid #{pid}), killing tar")
            Process.kill("KILL", pid) rescue nil
            Process.waitpid(pid) rescue nil
            break
        end
        sleep 1
    end

    writer.kill rescue nil
    writer.join rescue nil
    in_r.close rescue nil

    if timed_out
        raise "extract_tar timed out after #{EXTRACT_TAR_TIMEOUT_SECS}s on #{body.bytesize}-byte tar"
    end
    log_local("Extraction complete, output at: #{out}")
end

# One poll of the remote server while a suite runs
def poll_server(uri, out)
    res = http_get(uri)
    case res.code.to_i
    when HTTP_RUNNING
        # Suite is still executing on the remote server
        body = res.body.to_s
        log_remote(body) unless body.empty?
        false
    when HTTP_OK
        # Suite finished — response body is the result tar
        log_subsection_header("Success - Extracting tar")
        extract_tar(res.body, out)
        true
    else
        raise "Unexpected response:\n#{format_html_error(res)}"
    end
end

def run_suites(system, image, out, tests_to_run, total_timeout)
    topo = YAML.load_file(".mscc-libeasy-topology#{system}.yaml")
    uri  = URI("http://#{topo["easytest_server"]}/run")
    sha = $options[:sha] || %x{git rev-parse HEAD}.strip
    log_local("Test SHA: #{sha} (#{$options[:sha] ? 'explicit' : 'from HEAD'})")

    total_deadline = Time.now + total_timeout

    tests_to_run.each_with_index do |(suite, suite_secs), index|
        # Check the batch budget BEFORE posting, so we never start a suite on the
        # server only to bail in the first poll and leave it running (orphaned).
        raise "Total timeout (#{total_timeout}s) for all suites exceeded" if Time.now >= total_deadline

        per_suite_timeout = suite_secs || PER_SUITE_TIMEOUT_SECS
        if suite_secs.nil?
            log_local("No timeout given for '#{suite}', using default #{PER_SUITE_TIMEOUT_SECS}s")
        end
        post_suite(uri, image, out, system, suite, index, per_suite_timeout, sha)
        log_subsection_header("Streaming log from remote server")

        loop do
            raise "Total timeout (#{total_timeout}s) for all suites exceeded" if Time.now >= total_deadline
            break if poll_server(uri, out)  # true once the suite finished and its tar was extracted
            sleep(1)                        # still running — avoid hammering the server
        end
    end
end

# ---------------------------------------------------------------------------------------------------------------------
# Utils
# ---------------------------------------------------------------------------------------------------------------------


def reserve(system, timeout)
    t1    = Time.now
    stale = ".mscc-libeasy-topology#{system}.yaml"
    if File.exist?(stale)
        log_local("Removing stale topology file: #{stale}")
        FileUtils.rm(stale)
    end
    loop do
        begin
            run_cmd("et -l -n #{system} reserve #{system}", system)
            return true
        rescue => e
            log_local("Reserve attempt failed: #{e.message}")
        end

        if (Time.now - t1) > timeout
            log_local("Reserve time-out after #{timeout} seconds")
            return false
        end

        log_local("Reserve failed, will try again (#{Time.now - t1} < #{timeout})")
        sleep(30)
    end
end

def upload_image(system, image)
    raise "Image not found: #{image}" unless File.file?(image)
    run_cmd("et -l -n #{system} upload #{image}", system, timeout: UPLOAD_TIMEOUT_SECS)
end

# ---------------------------------------------------------------------------------------------------------------------
# Options parser
# ---------------------------------------------------------------------------------------------------------------------

$options = {
    :tests_to_run => [],
    :out          => ".",
    :timeout      => DEFAULT_TIMEOUT
}

OptionParser.new do |opts|
    opts.banner = "Usage: run-suites-on.rb [options]"
    opts.on("-h", "--help", "This message") { puts opts; exit }
    opts.on("-i", "--image image",   "Image path")        { |v| $options[:image]  = v }
    opts.on("-s", "--system system", "System to reserve") { |v| $options[:system] = v }

    opts.on("-t", "--timeout secs",
            "Total timeout (seconds) for the whole batch of suites " \
            "(default: #{DEFAULT_TIMEOUT})") do |v|
        $options[:timeout] = v.to_i
    end

    opts.on("-T", "--test path",
            "Suite to run as 'name', or 'name:secs' to cap that suite " \
            "(default per suite: #{PER_SUITE_TIMEOUT_SECS}s). Repeatable.") do |v|
        name, secs = v.split(":", 2)
        $options[:tests_to_run] << [name, (secs && !secs.empty? ? secs.to_i : nil)]
    end

    opts.on("-o", "--output folder",
            "Session output folder (created by caller; suites written to <out>/suites/)") do |v|
        $options[:out] = File.expand_path(v)
        FileUtils.mkdir_p($options[:out])
    end

    opts.on("-S", "--sha sha",
            "Exact commit SHA the remote server should check out " \
            "(default: working tree HEAD)") do |v|
        $options[:sha] = v
    end
end.parse!

# ---------------------------------------------------------------------------------------------------------------------
# Main sequence
# ---------------------------------------------------------------------------------------------------------------------

if $options[:system]

    log_section_header("Prerequisites")
    top = %x{git rev-parse --show-toplevel}.strip
    Dir.chdir(top)
    $options[:out] = Pathname.new($options[:out]).relative_path_from(Dir.pwd).to_s
    log_local("Output folder: #{$options[:out]}")
    log_local("Script: #{__FILE__}")
    log_local("Script SHA: #{%x{git log -1 --format=%H -- #{__FILE__} 2>/dev/null}.strip}")
    log_local("extract_tar at: #{method(:extract_tar).source_location.join(':')}")

    reserved = false
    begin
        log_section_header("Reserve")
        if !reserve($options[:system], RESERVE_TIMEOUT_SECS)
            log_local("Reserve timed out")
            exit 7
        end
        reserved = true
        log_local("Reservation successful")

        log_section_header("Upload image")
        upload_image($options[:system], $options[:image])

        log_section_header("Running test suites")
        log_local("Start time: #{Time.now}")
        run_suites($options[:system], $options[:image], $options[:out], $options[:tests_to_run], $options[:timeout])

    rescue => e
        log_section_header("Test fail summary")
        print_err(e)
        exit 1

    ensure
        if reserved
            log_section_header("Release")
            released = false
            RELEASE_MAX_ATTEMPTS.times do |attempt|
                begin
                    run_cmd("et -l -n #{$options[:system]} release",
                            $options[:system], timeout: RELEASE_TIMEOUT_SECS)
                    log_local("Completed Release")
                    released = true
                    break
                rescue => e
                    log_local("Release attempt #{attempt + 1}/#{RELEASE_MAX_ATTEMPTS} failed: #{e.message}")
                    sleep(10) unless attempt == RELEASE_MAX_ATTEMPTS - 1
                end
            end
            log_local("WARNING: failed to release #{$options[:system]} after #{RELEASE_MAX_ATTEMPTS} attempts — DUT may be stuck reserved") unless released
        end
        log_local("End time: #{Time.now}")
    end
end
