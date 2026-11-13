// context.h - what a workspace holds, and where things are on this machine: the tools, the libraries, the
// installation, Visual Studio, TI's compiler. Every question about the machine goes through the caller's
// FileSystem and Environment.
#ifndef GRIDE_COMPILE_CONTEXT_H
#define GRIDE_COMPILE_CONTEXT_H

#include "../gride_compile.h"
#include "definitions.h"
#include "project.h"

namespace gride { namespace compile {

struct WorkspaceData {
    Paths paths;
    FileSystem *fs;
    Environment *env;
    Definitions defs;
    BuildSettings settings;
    std::vector<Problem> problems;
    WorkspaceData() : fs(0), env(0) {}
};

struct VisualStudio {
    bool found;
    std::string root, msvcVersion, msvcBin, msvcLib, msvcInclude, llvmBin, kitRoot, kitVersion, kitBin;
    Words kitLibs, kitIncludes;
    VisualStudio() : found(false) {}
    Words binDirs() const;
    Words libDirs() const;
    Words includeDirs() const;
};

class Context {
public:
    explicit Context(const WorkspaceData &w);

    const Definitions &defs;
    const BuildSettings &settings;
    FileSystem &fs;
    Environment &env;
    const std::string programFolder;

    std::string expand(const std::string &text) const;            // %NAME% from the environment
    std::string installBin() const;                               // the installation's bin, or ""
    std::string installRoot() const;
    std::string installDir(const std::string &which, const std::string &named) const;
    Words searchDirs() const;                                     // beside GRIDE.exe, then the installation's bin
    const VisualStudio &visualStudio() const;
    std::string tiDir() const;                                    // TI's compiler directory, or ""

    // a tool as its lookup says; "" when it is nowhere, with every place looked in said in `looked`
    std::string findTool(const ToolDef &tool, const TargetDef *target, Words *looked) const;
    std::string findLibraryDir(const LibraryDef &lib) const;

    // the newest of the names under a folder that read as versions (14.44.35207, 10.0.26100.0)
    std::string newestVersionUnder(const std::string &folder, const std::string &prefix = std::string()) const;

private:
    mutable bool lookedBin_, lookedVs_, lookedTi_;
    mutable std::string bin_, ti_;
    mutable VisualStudio vs_;
    bool hasMarker(const std::string &folder) const;
    Context(const Context &);
    Context &operator=(const Context &);
};

Plan makePlan(const WorkspaceData &w, const Request &r);

}}

#endif
