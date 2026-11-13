// project.h - a .pro as the block reads it, the build settings of settings.json, and a project's files by
// pattern (the design's 3.2 to 3.4).
#ifndef GRIDE_COMPILE_PROJECT_H
#define GRIDE_COMPILE_PROJECT_H

#include <map>
#include <string>
#include <vector>

#include "../gride_compile.h"
#include "definitions.h"

namespace gride { namespace compile {

struct Group {
    std::string name, toolchain;      // the group's own compiler word, or ""
    Words entries;                    // names and patterns, as written
};

// one "Debug|Windows" entry of "configurations"; `stored` says the .pro has it - nothing is invented for one
// that it has not
struct Configuration {
    bool stored;
    std::string outDir, objDir, linkerCmd, compileFlags, linkFlags, outputName;
    Words includeDirs, libDirs, libs, defines;
    Configuration() : stored(false) {}
};

struct Project {
    std::string file, folder, name, toolchain, arch, target, language, kind;
    Words targets;                    // Black's "targets": the ide names a project lists; with arch, what it builds for
    std::vector<Group> groups;
    Words buildGroups;                // "build.groups"; none: every group
    bool namesBuildGroups;
    Words arguments, include, libraries, exclude, excludeFromBuild, dependsOn;
    Named paths;
    bool emulateOnly;
    Json options;                     // { "debug": { id: value }, "release": {...} }
    Json configurations;              // Black's: an object keyed "Debug|Windows"
    Json buildConfigurations;         // Grey's: a list of { name, defines, flags, linkFlags, outputDir }
    Project() : namesBuildGroups(false), emulateOnly(false) {}

    std::string programName() const { return target.empty() ? name : target; }
    const Group *group(const std::string &name) const;
    bool builds(const std::string &group) const;
    Configuration configuration(const std::string &config, const TargetDef &target) const;
    std::string option(const Definitions &d, const std::string &config, const std::string &id) const;
};

// reads a .pro; false with `problem` said when it cannot be read
bool readProject(const std::string &file, FileSystem &fs, Project &into, std::string &problem);

// ---- the files of a project (3.4) ----

struct ProjectFile {
    std::string path, relative, group, entry;     // absolute; relative with '/'; its group; the entry that gave it
    bool missing;                                 // a named file that is not there
    ProjectFile() : missing(false) {}
};

struct ProjectFiles {
    std::vector<ProjectFile> files;               // every group's, in order: a file is in one group only
    std::vector<Listed> listed;                   // entry by entry, for the log and the explorers
};

// `never`: folders no pattern takes from (the output and object folders)
ProjectFiles resolveFiles(const Project &p, FileSystem &fs, const Words &never);

// whether a list of names and patterns (an "exclude", an "excludeFromBuild") takes a project-relative path
bool excluded(const Words &entries, const std::string &relativePath);

// the groups with every name list written as a pattern where nothing is lost by it; as written where not
std::vector<Group> compacted(const Project &p, FileSystem &fs, const Words &never, const Definitions &d);

// ---- the build settings of settings.json (3.2) ----

struct BuildSettings {
    std::string root, include, lib, compiler, windowsRuntime, vcvars, ti, tilib;
    Words includes, libraries;
    bool askNative;
    Named paths;
    BuildSettings() : compiler("auto"), windowsRuntime("auto"), askNative(true) {}
};

// "build.<key>" where it is there; else the old key of the outlook named, then of the other two
BuildSettings readBuildSettings(const Json &settings, const std::string &outlook);

}}

#endif
