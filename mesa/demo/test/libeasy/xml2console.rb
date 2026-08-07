#!/usr/bin/env ruby

# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

require 'ox'
require 'pp'
require 'optparse'
require 'rexml' # => true
require 'open3'

# Unbuffer stdout. When this script's stdout is a pipe (as in the dispatch
# chain: suite.rb | tee | xml2console.rb | ...), libc fully-buffers it, so the
# brief RUN/OK lines batch in ~4-8KB chunks instead of flushing per test.  If a
# test hangs, its "RUN <test>" line stays trapped in the unflushed buffer and
# never reaches the controller — making the suite look idle with no indication
# of which test hung. Syncing makes every line flush immediately.
$stdout.sync = true
$stderr.sync = true

class String
    def black;          "\e[30m#{self}\e[0m" end
    def red;            "\e[31m#{self}\e[0m" end
    def green;          "\e[32m#{self}\e[0m" end
    def brown;          "\e[33m#{self}\e[0m" end
    def blue;           "\e[34m#{self}\e[0m" end
    def magenta;        "\e[35m#{self}\e[0m" end
    def cyan;           "\e[36m#{self}\e[0m" end
    def gray;           "\e[37m#{self}\e[0m" end

    def bg_black;       "\e[40m#{self}\e[0m" end
    def bg_red;         "\e[41m#{self}\e[0m" end
    def bg_green;       "\e[42m#{self}\e[0m" end
    def bg_brown;       "\e[43m#{self}\e[0m" end
    def bg_blue;        "\e[44m#{self}\e[0m" end
    def bg_magenta;     "\e[45m#{self}\e[0m" end
    def bg_cyan;        "\e[46m#{self}\e[0m" end
    def bg_gray;        "\e[47m#{self}\e[0m" end

    def bold;           "\e[1m#{self}\e[22m" end
    def italic;         "\e[3m#{self}\e[23m" end
    def underline;      "\e[4m#{self}\e[24m" end
    def blink;          "\e[5m#{self}\e[25m" end
    def reverse_color;  "\e[7m#{self}\e[27m" end
end

def map_char_to_safe_ascii x
  if x < 32 or x > 126
    return 46
  else
    return x
  end
end

class EtXML < ::Ox::Sax
    attr_accessor :junit_file, :junit_suite, :tc_html_folder, :tc_log_folder, :brief_stdout, :short_stdout
    def initialize
        @indent_level = 0
        @time_start = nil
        @time_now = nil
        @time_now_rel_s = nil
        @stack = []
        @stack.push({ :name => :document, :attr => {} })
        @test_stack = []

        @junit_file = nil
        @tc_html_folder = nil
        @tc_log_folder = nil
        @brief_stdout = false
        @short_stdout = false
        @junit_prop = {}
        @junit_suite = ""
        @junit_tests = []
    end

    def update_ts ts
        if ts.nil?
            @time_now_rel_s = nil
            @time_now = nil
            return
        end

        if /(\d+).(\d{9})/ =~ ts
            @time_now = (($1.to_i + Rational($2.to_i, 1000000000)) * 1000000000).to_i
        else
            raise "Invalid TS: #{ts}"
        end

        if @time_start.nil?
            @time_start = @time_now
        end

        diff = @time_now - @time_start

         h, diff = diff.divmod(60 * 60 * 1000 * 1000 * 1000)
         m, diff = diff.divmod(     60 * 1000 * 1000 * 1000)
         s, diff = diff.divmod(          1000 * 1000 * 1000)
        ms, diff = diff.divmod(                 1000 * 1000)
        us,   ns = diff.divmod(                        1000)

        # TODO, rounding...

        @time_now_rel_s = "%02d:%02d:%02d.%03d.%03d" % [h, m, s, ms, us]
        @time_now_rel_s = "%02d:%02d:%02d.%03d" % [h, m, s, ms]
    end

    def pr msg, pre_indent = " "
        indent = ""
        cnt = @indent_level
        if (cnt > 0 and @short_stdout)
            cnt = (cnt - 1)
        end
        cnt.times do |i|
            indent += "    "
        end

        l = "#{@time_now_rel_s} #{pre_indent} #{indent}#{msg.chomp}\n"
        if @short_stdout
            l = "#{indent}#{msg.chomp}\n"
        end
        if not @brief_stdout
            STDOUT.write(l)
        end
        junit_stdout(l)
    end

    def stack_include(name)
        @stack.filter{|x| x[:name] == name}.size > 0
    end

    def start_element(name)
        #pp name
        @stack.push({ :name => name, :attr => {} })
    end

    def end_element(name)
        e = @stack.pop
        a = e[:attr]
        update_ts a[:ts]

        case e[:name]
        when :trace
            case a[:level]
            when "noice"
                pr "#{e[:data]}", "T_N".gray
            when "debug"
                pr "#{e[:data]}", "T_D".green
            when "info"
                pr "#{e[:data]}", "T_I".bg_green
            when "error"
                pr "#{e[:data]}", "T_E".bg_red
                junit_failure()
            when "fatal"
                pr "#{e[:data]}", "T_F".bg_red.bold.blink
                junit_failure()
            else
                pr "#{e[:data]}"
            end

        when :poll
            if a[:val] == "ok"
                #pr "#{e[:name].to_s} OK", "POL".gray
            else
                pr "#{e[:data].to_s} NOT-OK", "POL".gray.bold
                junit_failure()
            end

        when :assert, :assert_poll
            if a[:val] == "ok"
                pr "#{e[:name].to_s} OK: #{a[:msg]}", "AST".bg_green
            else
                pr "#{e[:data].to_s} NOT-OK: #{a[:msg]}", "AST".bg_red

                if stack_include(:check_capability)
                  junit_skipped(a[:msg])
                else
                  junit_failure()
                end
            end

        when :check, :check_poll
            if a[:val] == "ok"
                pr "#{e[:name].to_s} OK: #{a[:msg]}", "CHK".bg_green
            else
                pr "#{e[:data].to_s} NOT-OK: #{a[:msg]}", "CHK".bg_red
                junit_failure()
            end

        when :backtrace
            pr "Test script crash in:\n#{e[:data]}", "BUG".bg_red.bold
            junit_failure()

        when :run, :try, :try_ignore, :try_err, :run_err, :mup
            @indent_level -= 1

        when :run_stdin, :try_stdin, :try_ignore_stdin, :try_err_stdin, :run_err_stdin
            pr "#{e[:data]}", "IN ".gray

        when :run_stdout, :try_stdout, :try_ignore_stdout, :try_err_stdout, :run_err_stdout, :mup_out
            pr "#{e[:data]}", "OUT".green

        when :run_stderr, :try_stderr, :try_err_stderr, :run_err_stderr
            # TODO, ignore the stderr lines on remote PC
            if a[:error_is_info] or a[:on] == "pc-remote"
                pr "#{e[:data]}", "ERR".green
            else
                pr "#{e[:data]}", "ERR".red
                junit_failure()
            end

        when :try_ignore_stderr
            pr "#{e[:data]}", "ERR".green

        when :run_end, :try_end, :mup_end
            msg = "Exit #{a[:exitstatus].to_i}, took #{a[:ts_rel]} secs"
            if a[:exitstatus].to_i == 0
                pr msg, "RES".green.bold
            else
                pr msg, "RES".bg_red
                junit_failure()
            end

        when :run_err_end, :try_err_end
            msg = "Exit #{a[:exitstatus].to_i}, took #{a[:ts_rel]} secs"
            if a[:exitstatus].to_i != 0
                pr msg, "RES".green.bold  # non-zero is expected
            else
                pr msg, "RES".bg_red      # exit 0 is unexpected for run_err
                junit_failure()
            end

        when :try_ignore_end
            msg = "Exit #{a[:exitstatus].to_i}, took #{a[:ts_rel]} secs"
            pr msg, "RES".green.bold

        when :console
            pr "#{e[:data]}", "CON".cyan

        when :bg
            pr "#{a[:name]}: #{a[:cmd]}", "BG ".magenta

        when :bg_stdout
            pr "#{a[:name]}-OUT: #{e[:data]}", "BG ".magenta

        when :bg_stderr
            pr "#{a[:name]}-ERR: #{e[:data]}", "BG ".bg_red

        when :test_end
            if a[:status] != "ok"
                pr "Test failed: #{@test_stack[-1]}", "TST".bg_red
                junit_failure()
            end

        when :test_run_end
            if a[:status] == "ok"
                junit_ok()
            else
                junit_failure()
            end

        when :test_run
            @indent_level -= 1
            if junit_tc_is_ok
                pr "OK: #{a[:file]}", "TC".green.bold
            else
                pr "Not-OK: #{a[:file]}", "TC".bg_red
            end
            junit_tc_end()

        when :test
            @indent_level -= 1
            @test_stack.pop

        when :check_capability_end
            if a[:status] == "ok"
                pr "Capability OK", "CAP".bg_green
            else
                pr "capability NOT OK", "CAP".bg_red
            end

        when :check_capability
            @indent_level -= 1

        when :expect_failure
            @indent_level -= 1

        end
    end

    def attr(name, value)
        @stack[-1][:attr][name] = value
    end

    def attrs_done()
        e = @stack[-1]
        a = e[:attr]
        update_ts a[:ts]

        case e[:name]
        when :run, :try, :try_ignore, :try_err, :run_err, :mup
            pr "#{e[:name].upcase}-#{a[:on]}: #{a[:cmd]}", "#{e[:name].upcase}".bg_cyan
            @indent_level += 1

        when :test_run
            pr "#{a[:file]}", "TC".bg_blue
            @indent_level += 1
            junit_tc_start(File.basename(a[:file]))

        when :test
            pr "#{a[:name]}", "TST".bg_blue
            @test_stack << a[:name]
            @indent_level += 1

        when :check_capability
            pr "Check Capabilities", "CAP".bg_magenta
            @indent_level += 1

        when :expect_failure
            pr "Expect failure", "EXP".bg_magenta
            @indent_level += 1
        end
    end

    def instruct(target)
    end

    def end_instruct(target)
    end

    def doctype(str)
    end

    def comment(str)
    end

    def cdata(str)
    end

    def value(str)
        @stack[-1][:data] = str.as_s
    end

    def error(message, line, column)
        pr "ERR: Msg: #{message}, line: #{line}, col: #{column}", "SAX".bg_red
        junit_failure()
    end

    def abort(name)
        pr "Abort: #{name}", "SAX".bg_red
        junit_failure()
    end

    def junit_stdout line
        return if @junit.nil?
        @junit[:stdout] += line
    end

    def junit_ok
        return if @junit.nil?
        @junit[:test_status_ok] = true
    end

    def junit_failure
        return if @junit.nil?
        return if stack_include(:expect_failure)
        puts "Marking test as failing"
        @junit[:failed] = true
    end

    def junit_skipped reason = nil
        return if @junit.nil?
        @junit[:skipped] = true
        # Keep the first reason seen -- the capability assert that triggered the
        # skip fires before the script's clean exit, so it wins over any later
        # generic marker.
        @junit[:skip_reason] ||= reason if reason
    end

    def pr_brief lable, msg
        if @brief_stdout
            puts "#{Time.now} #{lable} #{msg}"
        end
    end

    def junit_tc_is_ok tc = nil
        tc = @junit if tc.nil?
        return false if tc.nil?

        if tc[:failed] == false and tc[:test_status_ok] == true
            return true
        else
            return false
        end
    end

    def junit_tc_skipped tc = nil
        tc = @junit if tc.nil?
        return false if tc.nil?

        if tc[:skipped]
            return true
        else
            return false
        end
    end

    def junit_tc_end
        return if @junit.nil?

        filename_log = ""
        filename_html = ""
        if not junit_tc_is_ok
            pr_brief "ERR".bg_red, "#{@junit[:name]}"
            filename_log = "#{@tc_log_folder}/err-#{@junit[:name]}.log"
            filename_html = "#{@tc_html_folder}/err-#{@junit[:name]}.html"
        else
            pr_brief "OK ".green.bold, "#{@junit[:name]}"
            filename_log = "#{@tc_log_folder}/ok-#{@junit[:name]}.log"
            filename_html = "#{@tc_html_folder}/ok-#{@junit[:name]}.html"
        end

        if @tc_log_folder
            File.write(filename_log, @junit[:stdout])
            pr_brief "LOG".bg_blue, filename_log
        end

        if @tc_html_folder
            Open3.pipeline_w("ansi2html -s dracula -l", :out => filename_html) {|i, ts|
                i.write @junit[:stdout]
            }
            pr_brief "LOG".bg_blue, filename_html
        end

        @junit_tests << @junit
        @junit = nil
    end

    def junit_tc_start name
        pr_brief "RUN".bg_blue.bold, "#{name}"

        @junit = {
            # Used fail the test despite the tests own status (such as
            # stack-trace or error messages seen).
            :failed => false,
            :skipped => false,
            :skip_reason => nil,

            # Track if the test has explicitly reported ok
            :test_status_ok => false,
            :stdout => "",
            :name => name
        }
    end

    def junit_prop_add key, val
        @junit_prop[key] = val
    end

    def junit_write
        return if @junit_file.nil?

        failure = 0
        @junit_tests.each do |tc|
            if not junit_tc_is_ok(tc)
                failure += 1
            end
        end

        File.open(@junit_file, "w") do |j|
            j.puts "<?xml version=\"1.0\"?>"
            j.puts "<testsuite name=\"#{@junit_suite}\" tests=\"#{@junit_tests.size}\" failures=\"#{failure}\">"
            if @junit_prop.size > 0
                j.puts "  <properties>"
                @junit_prop.each do |k, v|
                    j.puts "    <property name=#{k.encode(:xml => :attr)} value=#{v.encode(:xml => :attr)}/>"
                end
                j.puts "  </properties>"
            end
            @junit_tests.each do |tc|
                j.puts "  <testcase name=\"#{tc[:name]}\">"

                if junit_tc_skipped tc
                    reason = tc[:skip_reason] || "test skipped"
                    j.puts "    <skipped message=#{reason.encode(:xml => :attr)} />"
                elsif not junit_tc_is_ok tc
                    j.puts "    <failure message=\"test failed\" />"
                end


                # TODO: find an XML library which are handling \x1b correctly in XML v1.1
                # Tried the following which did not work: ox, rexml, nokogiri
                # Maybe builder or libxml can?
                # For now just replace all non-printable chars with a '.'
                j.puts "    <system-out>"
                j.puts "      See the html log file instead"
                #j.puts tc[:stdout].encode(:xml => :text).unpack("C*").map{|x| map_char_to_safe_ascii(x) }[0, 128].pack("C*")
                j.puts "    </system-out>"
                j.puts "  </testcase>"
            end
            j.puts "</testsuite>"
        end
    end
end

$handler = EtXML.new()

$options = {}
OptionParser.new do |opts|
    opts.banner = "Usage: [options]"

    opts.on("-h", "--help", "This message") do |v|
        puts opts
        exit
    end

    opts.on("-j", "--junit-out FILE", "Capture test output in JUNIT format") do |f|
        $handler.junit_file = f
    end

    opts.on("-n", "--junit-suite-name NAME", "Capture test output in JUNIT format") do |n|
        $handler.junit_suite = n
    end

    opts.on("-o", "--testcase-log-out FOLDER", "Write the console output log file, per test case in the suite in the provided folder") do |f|
        %x{mkdir -p #{f}}
        $handler.tc_log_folder = f
    end

    opts.on("-O", "--testcase-html-out FOLDER", "Write the console output html file, per test case in the suite in the provided folder") do |f|
        %x{mkdir -p #{f}}
        $handler.tc_html_folder = f
    end

    opts.on("-p", "--property KEY=VAL", "Set a JUnit property in the test suite") do |p|
        if p =~ /(\S+)=(\S+)/
            $handler.junit_prop_add($1, $2)
        end
    end

    opts.on("--brief", "") do |v|
        $handler.brief_stdout = true
    end

    opts.on("--short", "") do |v|
        $handler.short_stdout = true
    end
end.parse!

begin
    Ox.sax_parse($handler, STDIN, :skip => :skip_none)
ensure
    $handler.junit_write()
end

