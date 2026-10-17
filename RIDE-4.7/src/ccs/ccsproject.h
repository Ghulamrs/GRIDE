#ifndef EDITOR_CCS_PROJECT_H
#define EDITOR_CCS_PROJECT_H

#include <map>
#include <string>
#include <vector>

#include "../toolchain.h"

namespace editor {
namespace ccs {

// **A CCS 7.4 or 5.5 project for the TMS320C6747, read as it is.** Nothing is converted and
// nothing is written: .ccsproject, .cproject and .project are read afresh on every open and every
// build, so an edit made in CCS is seen at once. What of it RIDE's own toolchain can honour is
// mapped (docs/ccs-reference/FORMAT.md, "Proposed mapping"); the rest is named, never dropped
// in silence. The switch is settings.json's "ccs": { "enabled": true, "root": "C:/ti/ccsv7" }.
struct Config {
    std::string name;                    // CCS's: Debug, Release
    Configuration which;                 // RIDE's
    // The compiler, mapped: what Compiler Options will show for c90 and cpp11.
    std::string opt;                     // -O0, -O1 or -O2
    bool debug;                          // -g (DWARF); on tms6747 no line table is written anyway
    bool debugSaid;                      // whether CCS stored a debug model at all
    std::vector<std::string> defines;    // NAME or NAME=value, CCS's outer quotes taken off
    std::vector<std::string> undefines;
    std::vector<std::string> includes;   // absolute, macros resolved, ${CG_TOOL_ROOT}/include left out
    bool noCompress;
    // The link, mapped: what lnk6x is handed beside the objects.
    TiLink link;
    // The sources this configuration builds: absolute, exclusions applied, .cmd files apart.
    std::vector<std::string> sources;
    std::vector<std::string> excluded;   // project-relative, as .cproject spells them
    // What RIDE cannot honour and what it decides for itself, each as cl6x would have been given it.
    std::vector<std::string> unsupported;
    std::vector<std::string> ownInstead;
    // One line per mapped option, "cl6x -O3 -> cpp11 -O2", for the read-only dialog.
    std::vector<std::string> mapped;
    std::string prebuild, postbuild;     // as written, macros resolved; not run

    Config() : which(ConfigDebug), debug(false), debugSaid(false), noCompress(false) {}
};

struct Reading {
    std::string dir;          // the project folder, absolute, slashes one way
    std::string name;         // .project's name
    std::string device;       // TMS320C67XX.TMS320C6747
    std::string family;       // C6000
    std::string ccsVersion;   // 7.4.0, or "" for 5.5
    std::string cgtVersion;   // 8.2.2, 7.4.4
    std::string cgToolRoot;   // ${CG_TOOL_ROOT} as resolved here, or ""
    bool elf;
    std::vector<Config> configs;          // Debug first where there is one, then Release
    std::vector<std::string> linked;      // linked resources' absolute paths, files only
    std::vector<std::string> notBuilt;    // sources of a kind RIDE does not build: .asm, .sa, .lib, .obj
    std::vector<std::string> headers;     // the folder's own headers, relative: shown and opened, never built
    std::vector<std::string> notes;       // anything else worth a line: a macro left unresolved, definitions missing
    std::map<std::string, std::string> macros;   // build variables, per project (the first configuration's), then the workspace's
    // The workspace the project was opened from (ccsworkspace.h), or "" for a folder opened alone,
    // where WORKSPACE_LOC is taken to be the folder's parent as before.
    std::string workspace;
    std::map<std::string, std::string> pathVariables;   // the workspace's, for a linked resource's locationURI
    std::map<std::string, std::string> projects;        // the workspace's projects, name to folder, for ${workspace_loc:/P}
    std::vector<std::string> references;                // .project's referenced projects, which RIDE does not build

    Reading() : elf(true) {}
    const Config* config(Configuration which) const;
};

// Whether a directory is a CCS project: .project and .ccsproject both there.
bool isProject(const std::string& dir);
// Whether a file is one of the three CCS files, so opening one opens its folder.
bool isProjectFile(const std::string& file);

// Reads the three files. False with the reason - a device that is not a C674x, a COFF project,
// big endian, a file that will not parse - and the reader's word is final: no half-read project.
// With a workspace, the project is read as a member of it: its locations and macros resolve there.
struct Workspace;
bool read(const std::string& dir, Reading& out, std::string& error, const Workspace* workspace = 0);

// The Messages line for one configuration: "CCS project K6747c (Release): 2 options not
// supported, using RIDE's defaults: --opt_for_speed=5, -ms3; RIDE's own instead of: ...".
std::string report(const Reading& reading, Configuration which);
// The Messages line about the sources: linked files not on this machine, kinds RIDE does not build.
std::string sourceReport(const Reading& reading);

// The dialog's text for a configuration: every mapped option, then the two lists, then "edit in CCS".
std::string mappingText(const Reading& reading, Configuration which);

// ${ProjName}, ${PROJECT_ROOT}, ${PROJECT_LOC}, ${CG_TOOL_ROOT}, ${workspace_loc:/P/x}, a build
// variable - resolved against the reading; a macro nobody knows is left as written.
std::string resolveMacros(const std::string& text, const Reading& reading);

// Where ${CG_TOOL_ROOT} is: under the CCS root settings.json names, the compiler directory of
// that version; else the TI compiler directory RIDE's tms6747 builds already link against.
std::string cgToolRootFor(const std::string& ccsRoot, const std::string& cgtVersion);

}
}

#endif
