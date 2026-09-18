#!/usr/bin/env ruby

# Copyright (c) 2021-2022 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

# SBOM component model for the sbom-spdx generator.
#
# It loads the sdlc-objects.yaml files (see support/dg/sdlc_doc_schema.json),
# reads the cmake target databases of the requested build folders, and builds
# the component containment graph.
#
# Each file is a single YAML object with two containers:
#   - cmake-targets: one record per build target. A record links to an SPDX
#     component via an inline 'spdx' block or a 'spdx-id' reference; a record
#     with neither is a transparent pass-through (its children roll up to the
#     nearest ancestor that does map to a component).
#   - spdx-elements: standalone SPDX components referenced by id from
#     'spdx-id' and from 'sbom_add_kids'.
#
# A component whose cmake target is a cmake root becomes a "SW packet" (root);
# everything else reachable is an embedded component.

require 'json'
require 'yaml'
require 'json_schemer'
require_relative "cmake-targets.rb"

# Shared SDLC document schema (defines the cmake-targets and spdx-elements
# containers). Resolved relative to the repo top (the generator chdir's there).
SDLC_SCHEMA_PATH = ".cmake/sdlc_doc_schema.json"

class SbomElement
  attr_reader :id, :name, :directory, :files, :license, :license_file,
              :download_location, :source_info, :version, :use_packet_version,
              :copyright, :no_src_aggr, :sbom_add_kids, :description, :yaml_dir, :path,
              :group, :external_spdx, :external_refs
  attr_reader :kids, :parents

  # d: the spdx-element mapping; path: the yaml file it was loaded from.
  def initialize(path, d)
    @path = path
    @yaml_dir = File.dirname(path)
    @id = d["id"]
    @name = d["name"] || d["id"]
    @directory = d["directory"]
    @files = d["files"]
    @license = d["license"]
    @license_file = d["license_file"]
    @license_text_inline = d["license_text"]
    @download_location = d["download_location"]
    @source_info = d["source_info"]
    @version = d["version"]
    @use_packet_version = d["use_packet_version"] ? true : false
    @copyright = d["copyright"]
    @no_src_aggr = d["no_src_aggr"] ? true : false
    @group = d["group"] || "product"
    @external_spdx = d["external-spdx"]   # nil, or { "provider" =>, "import" => }
    @sbom_add_kids = d["sbom_add_kids"] || []
    @external_refs = d["external_refs"] || []
    @description = d["description"]
    @kids = []
    @parents = []
  end

  # SPDXID for the element. The id is authored as the SPDXID verbatim
  # (SPDXRef-...), so it is greppable 1:1 between the SPDX output and the source.
  def spdxref
    @id
  end

  def kid_add(c)
    return if c.equal?(self)
    return if @kids.include?(c)
    @kids << c
    c.parents << self unless c.parents.include?(self)
  end

  def license_ref?
    !@license.nil? && @license.start_with?("LicenseRef-")
  end

  # Full license text, from inline license_text or license_file (relative to the
  # element's yaml). nil if neither is available.
  def license_text
    return @license_text_inline if @license_text_inline
    if @license_file
      p = File.join(@yaml_dir, @license_file)
      return File.read(p) if File.exist?(p)
    end
    nil
  end

  def recursive_kids
    r = []
    @kids.each do |k|
      r << k
      r += k.recursive_kids
    end
    r.uniq
  end
end

class SbomModel
  attr_reader :by_id, :by_target, :roots, :errors

  # The sdlc-objects.yaml files to read. By default only git-tracked files, so a
  # primary checkout is not polluted by copies living under worktrees or other
  # hidden directories (e.g. .claude/worktrees) or build outputs. all=true falls
  # back to a filesystem glob, useful when hacking on the scripts outside a git
  # checkout. build-* and the given excludes are always filtered out.
  def self.sdlc_files(excludes, all = false)
    excludes = excludes.map { |d| File.expand_path(d) }
    list = if all
             Dir.glob("**/sdlc-objects.yaml", File::FNM_DOTMATCH)
           else
             `git ls-files -z -- '*sdlc-objects.yaml'`.split("\0")
           end
    list.reject { |f| f =~ %r{(^|/)build-} }
        .reject { |f| excludes.any? { |d| File.expand_path(f).start_with?(d + "/") } }
        .sort
  end

  # Validate every sdlc-objects.yaml against the shared SDLC document schema and
  # enforce the cross-record invariants a single-document schema cannot express:
  #   - cmake target 'name' and 'id' unique across all files
  #   - spdx-element 'id' unique (inline and standalone) across all files
  #   - every 'spdx-id' reference and 'sbom_add_kids' id resolves to an element
  #   - directory/files anchors exist on disk
  #   - derived SPDXIDs are unique
  # When build_dirs is given, also enforce coverage: every cmake target the build
  # system reports (in each build's cmake_target_db.txt) must have a cmake-target
  # record, so nothing the build produces is silently absent from the metadata.
  # Returns [errors, stats]. The generator runs this before building the model
  # (and aborts on any error) so a generated SBOM is always valid. cwd must be
  # the repo top; excludes ignores sdlc-objects.yaml under the given dirs.
  def self.validate(excludes = [], build_dirs = [], all: false)
    schemer = JSONSchemer.schema(JSON.parse(File.read(SDLC_SCHEMA_PATH)))

    errors = []
    target_names = {}   # cmake target name => file
    target_ids = {}     # cmake-target id  => file
    elem_ids = {}       # spdx-element id  => file
    spdxrefs = {}       # derived SPDXID   => elem id
    spdx_id_refs = []   # [target_name, spdx_id, file]
    add_kid_refs = []   # [elem_id, kid_id, file]

    # Cross-record + on-disk checks for a single spdx-element. The id is authored
    # as the SPDXID verbatim (SPDXRef-...).
    check_element = lambda do |el, dir, file|
      eid = el["id"]
      if eid
        if elem_ids[eid]
          errors << "#{file}: duplicate spdx-element id '#{eid}' (also in #{elem_ids[eid]})"
        else
          elem_ids[eid] = file
          if spdxrefs[eid]
            errors << "#{file}: spdx-element id '#{eid}' yields SPDXID '#{eid}' which collides with id '#{spdxrefs[eid]}'"
          else
            spdxrefs[eid] = eid
          end
        end
      end
      if el["directory"]
        p = File.join(dir, el["directory"])
        errors << "#{file}: spdx-element '#{eid}' directory '#{el["directory"]}' does not exist (#{p})" unless File.directory?(p)
      end
      (el["files"] || []).each do |rel|
        p = File.join(dir, rel)
        errors << "#{file}: spdx-element '#{eid}' file '#{rel}' does not exist (#{p})" unless File.exist?(p)
      end
      if el["license_file"]
        p = File.join(dir, el["license_file"])
        errors << "#{file}: spdx-element '#{eid}' license_file '#{el["license_file"]}' does not exist (#{p})" unless File.exist?(p)
      end
    end

    files = sdlc_files(excludes, all)

    files.each do |f|
      dir = File.dirname(f)
      doc = YAML.load_file(f)

      schemer.validate(doc).each do |e|
        ptr = e["data_pointer"].empty? ? "(root)" : e["data_pointer"]
        errors << "#{f} #{ptr}: #{e["error"]}"
      end
      next unless doc.is_a?(Hash)

      (doc["cmake-targets"] || []).each do |t|
        tid, tname = t["id"], t["name"]
        if tid
          errors << "#{f}: duplicate cmake-target id '#{tid}' (also in #{target_ids[tid]})" if target_ids[tid]
          target_ids[tid] ||= f
        end
        if tname
          errors << "#{f}: duplicate cmake target name '#{tname}' (also in #{target_names[tname]})" if target_names[tname]
          target_names[tname] ||= f
        end
        if t["spdx"]
          check_element.call(t["spdx"], dir, f)
          (t["spdx"]["sbom_add_kids"] || []).each { |k| add_kid_refs << [t["spdx"]["id"], k, f] }
        elsif t["spdx-id"]
          spdx_id_refs << [tname, t["spdx-id"], f]
        elsif t["spdx-ids"]
          t["spdx-ids"].each { |sid| spdx_id_refs << [tname, sid, f] }
        end
      end

      (doc["spdx-elements"] || []).each do |el|
        check_element.call(el, dir, f)
        (el["sbom_add_kids"] || []).each { |k| add_kid_refs << [el["id"], k, f] }
      end
    end

    spdx_id_refs.each do |tname, sid, f|
      errors << "#{f}: cmake target '#{tname}' spdx-id '#{sid}' does not resolve to any spdx-element" unless elem_ids[sid]
    end
    add_kid_refs.each do |eid, kid, f|
      errors << "#{f}: sbom_add_kids of '#{eid}' references unknown spdx-element id '#{kid}'" unless elem_ids[kid]
    end

    # Coverage: every cmake target the build system reports must be registered.
    build_dirs.each do |b|
      next unless File.exist?("#{b}/cmake_target_db.txt")
      CmakeTargets.new(b).targets.keys.sort.each do |tname|
        errors << "cmake target '#{tname}' (in #{b}) has no record in any sdlc-objects.yaml" unless target_names[tname]
      end
    end

    [errors, { files: files.size, targets: target_names.size, elements: elem_ids.size }]
  end

  # Produced libraries for the release SBOM: parse cmake_target_db_full.txt in
  # each build dir (the full target-graph export) and return one record per
  # distinct produced library, deduplicated by output name across arches:
  #   { output:, name:, source_dir:, type: }
  # Only real STATIC/SHARED libraries whose source lives under the repo top are
  # returned, so the buildroot sysroot .so files (which are not cmake targets)
  # are never included. 'name' is the cmake target name, used to look up any
  # third-party mapping via @by_target.
  def self.produced_libraries(build_dirs, top)
    top = File.expand_path(top)
    libs = {}
    build_dirs.each do |b|
      f = "#{b}/cmake_target_db_full.txt"
      next unless File.exist?(f)
      props = Hash.new { |h, k| h[k] = {} }
      File.read(f).each_line do |l|
        props[$1][$2] = $3.strip if l =~ /\A(\S+) (\w+) = (.*)\z/m
      end
      props.each do |name, p|
        next unless ["STATIC_LIBRARY", "SHARED_LIBRARY"].include?(p["TYPE"])
        sd = p["SOURCE_DIR"]
        next unless sd && File.expand_path(sd).start_with?(top)
        out = (p["OUTPUT_NAME"] && !p["OUTPUT_NAME"].empty?) ? p["OUTPUT_NAME"] : name
        # A single output (e.g. "vsc7546") is often produced by two targets: a
        # SHARED "<x>" and a STATIC "<x>_static". Collect *all* their names so the
        # caller can resolve the third-party mapping from whichever target carries
        # it (the sdlc-objects.yaml record lives on the _static target).
        libs[out] ||= { output: out, name: name, source_dir: sd, type: p["TYPE"], names: [] }
        libs[out][:names] << name
      end
    end
    libs.values.sort_by { |x| x[:output] }
  end

  # The static-archive merge composition, for the release SBOM's CONTAINS graph.
  # MEPA collections (libmepa_<x>.a) and MEBA libs (libmeba_<x>.a) are produced by
  # merging other static archives (the mepa_merge_static_libs / merge_static_libs
  # macros), recorded as a '<base>_target' UTILITY whose MANUALLY_ADDED_DEPENDENCIES
  # are the merged-in libraries. So libmepa_sparx5.a truly *contains* the driver
  # archives (Intel, Aquantia, ...), and libmeba_sparx5.a contains the mepa
  # collection. Returns { owner_lib_stem => [merged_lib_stem, ...] }, keyed by the
  # library file stem (e.g. "libmepa_sparx5") so both the .a and .so match.
  def self.merge_composition(build_dirs)
    strip = lambda { |fn| File.basename(fn).sub(/\.(a|so)(\..*)?\z/, "") }
    comp = Hash.new { |h, k| h[k] = [] }

    # Derived from the cmake target graph: a merged archive is built by a
    # '<base>_target' UTILITY whose MANUALLY_ADDED_DEPENDENCIES are the merged-in
    # libraries (mepa_merge_static_libs / meba merge_static_libs / LMStax's
    # lm_link_static_library all record this). Map targets to their output file
    # stem via TARGET_FILE (resolved by file(GENERATE)), falling back to the
    # lib<name> convention for IMPORTED collections that have no TARGET_FILE.
    build_dirs.each do |b|
      dbf = "#{b}/cmake_target_db_full.txt"
      next unless File.exist?(dbf)
      file_of = {}
      util = {}
      props = Hash.new { |h, k| h[k] = {} }
      File.read(dbf).each_line { |l| props[$1][$2] = $3.strip if l =~ /\A(\S+) (\w+) = (.*)\z/m }
      props.each do |name, p|
        file_of[name] = File.basename(p["TARGET_FILE"]) if p["TARGET_FILE"] && !p["TARGET_FILE"].empty?
        util[$1] = p["MANUALLY_ADDED_DEPENDENCIES"].split(";") if name =~ /\A(.+)_target\z/ && p["TYPE"] == "UTILITY" && p["MANUALLY_ADDED_DEPENDENCIES"]
      end
      # Map a target to its produced library-file stem. Prefer the authoritative
      # TARGET_FILE; otherwise reconstruct from the lib<name>.a convention. The
      # meba merge macro names its wrapper 'meba_<x>_static' but produces
      # 'libmeba_<x>.a' (merge_static_libs: TARGET meba_<x>_static, FILENAME
      # libmeba_<x>.a), so a trailing '_static' must be dropped -- the shipped
      # filename never carries it. Without this, merge targets that are absent
      # from the target db (the EXCLUDE_FROM_ALL macsec/bringup meba variants have
      # no TARGET_FILE recorded) key their CONTAINS edges under 'libmeba_<x>_static',
      # which never matches the produced 'libmeba_<x>.a' and so the mepa collection
      # they merge in is silently dropped from the containment graph.
      tstem = lambda { |t| strip.call(file_of[t] || "lib#{t.sub(/_static\z/, "")}.a") }
      util.each { |base, deps| comp[tstem.call(base)].concat(deps.map { |d| tstem.call(d) }) }
    end

    comp.each_value(&:uniq!)
    comp
  end

  # build_dirs: list of build folders to read cmake databases from.
  # excludes:   directories whose sdlc-objects.yaml files are ignored (e.g.
  #             a packet output folder that contains copies of source metadata).
  # all:        read every sdlc-objects.yaml on disk rather than only git-tracked.
  def initialize(build_dirs, excludes = [], all: false)
    @excludes = excludes.map { |d| File.expand_path(d) }
    @all = all
    @by_id = {}        # spdx-element id  => SbomElement
    @by_target = {}    # cmake target name => SbomElement
    @roots = []
    @errors = []
    load_files
    build_tree(build_dirs)
    apply_add_kids
  end

  # Components reachable from a root (roots + transitive kids). Elements that are
  # neither mapped to a built target nor pulled in via sbom_add_kids are loaded
  # and schema-checked but not part of the binary SBOM.
  def emitted
    reach = {}
    @roots.each { |r| mark_reach(r, reach) }
    @by_id.values.select { |c| reach[c] }.uniq
  end

  def embedded
    emitted - @roots
  end

  # Every component that has actual source in the tree, for the source-release
  # SBOM. This is element-driven (not the cmake link graph): it includes inline,
  # referenced and standalone spdx-elements alike, so test-only and doc-bundle
  # third-party that never links into a binary is still described. Source-less
  # binary aggregates (no_src_aggr, e.g. the img_*/VDSP_BIN_* elements) have no
  # source and are excluded.
  def source_elements
    @by_id.values.reject(&:no_src_aggr).uniq.sort_by(&:id)
  end

 private
  def register_element(el)
    if @by_id[el.id]
      raise "Duplicate spdx-element id '#{el.id}' in #{el.path} and #{@by_id[el.id].path}"
    end
    @by_id[el.id] = el
  end

  def load_files
    # pass 1: create all elements (inline + standalone) and map targets that
    # carry an inline spdx. Defer spdx-id references to pass 2.
    deferred = []   # [target_name, spdx_id, path]
    self.class.sdlc_files(@excludes, @all).each do |x|
      begin
        doc = YAML.load_file(x)
        next unless doc.is_a?(Hash)

        (doc["spdx-elements"] || []).each { |e| register_element(SbomElement.new(x, e)) }

        (doc["cmake-targets"] || []).each do |t|
          tname = t["name"]
          if t["spdx"]
            el = SbomElement.new(x, t["spdx"])
            register_element(el)
            map_target(tname, [el], x)
          elsif t["spdx-id"]
            deferred << [tname, [t["spdx-id"]], x]
          elsif t["spdx-ids"]
            deferred << [tname, t["spdx-ids"], x]
          end
          # else: transparent pass-through (no SBOM component)
        end
      rescue => e
        STDERR.puts "Error processing: #{x}"
        raise e
      end
    end

    # pass 2: resolve spdx-id / spdx-ids references now that all elements exist.
    deferred.each do |tname, sids, path|
      els = sids.map do |sid|
        el = @by_id[sid]
        raise "cmake target '#{tname}' in #{path} references unknown spdx-id '#{sid}'" unless el
        el
      end
      map_target(tname, els, path)
    end
  end

  # els: array of SbomElement the target maps to (usually one; more than one for a
  # target that bundles several components via spdx-ids).
  def map_target(tname, els, path)
    if @by_target[tname]
      raise "cmake target '#{tname}' mapped more than once (#{path})"
    end
    @by_target[tname] = els
  end

  def build_tree(build_dirs)
    build_dirs.each do |tf|
      unless File.exist?(tf)
        @errors << "Skipping #{tf} as path does not exist"
        STDERR.puts "Skipping #{tf} as path does not exist"
        next
      end
      ct = CmakeTargets.new(tf)
      ct.root_targets.each do |rt|
        ct.visit(rt) do |target_name, target, indent, stack|
          cs = @by_target[target_name]
          next unless cs && !cs.empty?
          # Nearest ancestor target that maps to a component; its first element is
          # the enclosing container these components roll up into.
          enclosing = nil
          stack[0..-2].reverse_each do |a|
            ac = @by_target[a]
            if ac && !ac.empty? && ac != cs
              enclosing = ac.first
              break
            end
          end
          cs.each do |c|
            if enclosing && !enclosing.equal?(c)
              enclosing.kid_add(c)
            elsif enclosing.nil?
              @roots << c unless @roots.include?(c)
            end
          end
        end
      end
    end
  end

  def apply_add_kids
    @by_id.values.each do |c|
      c.sbom_add_kids.each do |kid_id|
        kid = @by_id[kid_id]
        unless kid
          raise "sbom_add_kids: '#{c.id}' references unknown spdx-element id '#{kid_id}'"
        end
        c.kid_add(kid)
      end
    end
  end

  def mark_reach(c, seen)
    return if seen[c]
    seen[c] = true
    c.kids.each { |k| mark_reach(k, seen) }
  end
end
