// definitions.h - toolchain.json as the block reads it: the shipped file with the user's file merged over it.
// Everything a build needs to know about a tool is in that file; nothing here names a tool, a flag or a folder.
#ifndef GRIDE_COMPILE_DEFINITIONS_H
#define GRIDE_COMPILE_DEFINITIONS_H

#include <map>
#include <string>
#include <vector>

#include "json.h"
#include "text.h"

namespace gride { namespace compile {

typedef std::map<std::string, std::string> Named;

struct OperationDef {
    std::string id, args, output;
};

struct ToolDef {
    std::string id, name, role, exe;
    Words languages, targets, aliases, words, installIncludeDirs, options;
    std::string lookupEnv, lookupInstallDir;
    Words lookupDirs;
    bool lookupPath, lookupVcvars;
    std::string outputEncoding, diagnostics, archFlag, includeFlag, defineFlag, responseFile;
    Named languageFlags, configFlags, asmSyntaxFlags, linkFlags;
    std::map<std::string, Named> configFlagsByTarget;
    Words environmentNames;                       // the variables the tool is documented to read
    std::vector<OperationDef> operations;
    ToolDef() : lookupPath(false), lookupVcvars(false) {}

    const OperationDef *operation(const std::string &id) const;
    bool takes(const std::string &language) const { return contains(languages, language); }
    bool serves(const std::string &arch) const { return targets.empty() || contains(targets, arch); }
    Words names() const;                          // exe and aliases, each also with .exe
};

struct TargetDef {
    std::string arch, ideTarget, folder, debugInfo;
    bool host, emulated;
    std::string assemblyExtension, objectExtension, programExtension, libraryDirFlag, libraryFlag;
    std::string refuseLibraryFlag;                // "/NODEFAULTLIB:" - how a link is told not to take a library
    Named roles;
    std::string runtime, linkerCommandFile;
    TargetDef() : host(false), emulated(false) {}
    std::string role(const std::string &name) const;
};

struct LibraryDef {
    std::string id, name, kind, crt, target, directory, directoryEnv, marker, when;
    Named files;                                  // "Debug" / "Release" -> the file names, blanks between them
    Words aliases, refuses;                       // refuses: libraries a link with this one must not take
    bool alias(const std::string &name) const;    // an alias may end in *
    Words filesFor(const std::string &configuration) const;
};

struct OptionDef {
    std::string id, control, flag, flagOn, flagOff, flagEach, silent, needs, environment;
    Named defaults;                               // "Debug" / "Release"; "" is both
    std::string defaultFor(const std::string &configuration) const;
};

struct LanguageDef {
    std::string name, role;                       // "C", "cCompiler"
    Words extensions;                             // ".c"
};

struct LinkerCommandFileDef {
    std::string id, name, text;
};

struct VisualStudioDef {
    std::string msvcVersion, vcvars, msvcTools, hostBin, lib, include, llvmBin;
    Words defaultRoots, editions;
    std::string kitRegistryKey, kitRegistryValue, kitVersionsUnder, kitBinDir;
    Words kitRoots, kitLibDirs, kitIncludeDirs;
};

struct Definitions {
    // the installation (an earlier RIDE, or GRIDE installed with a bin folder)
    Words installRoots, installMarkers;
    std::string installBin, installInclude, installLib;

    std::vector<TargetDef> targets;
    std::vector<ToolDef> tools;
    std::vector<LibraryDef> libraries;
    std::vector<OptionDef> options;
    std::vector<LanguageDef> languages;
    std::vector<LinkerCommandFileDef> linkerCommandFiles;
    Words headerExtensions;
    std::string outputFolder, objectFolder;       // the patterns of "folders"
    VisualStudioDef visualStudio;
    // TI's own compiler, for the fallback link on the C6747
    Words tiRoots, tiUnder;
    std::string tiBin, tiLib, tiRuntime, tiRuntimeFallback, tiRuntimeIndex;
    Named tiCompilerDirs;

    bool userChanges;                             // the user's toolchain.json changed something

    Definitions() : userChanges(false) {}

    const TargetDef *target(const std::string &archOrIdeName) const;
    const ToolDef *tool(const std::string &id) const;
    const ToolDef *toolFor(const TargetDef &target, const std::string &role) const;
    const ToolDef *toolOfWord(const std::string &word) const;      // a .pro's "toolchain" word
    const LibraryDef *library(const std::string &id) const;
    const LibraryDef *libraryOfKind(const std::string &arch, const std::string &kind, const std::string &crt = std::string()) const;
    const OptionDef *option(const std::string &id) const;
    const LanguageDef *languageOf(const std::string &path) const;  // by extension; null for a header or the unknown
    const LanguageDef *language(const std::string &name) const;
    bool header(const std::string &path) const;
    // a language whose compiler takes every source of a build in one run (its compile names $(Sources)): the
    // first file is then the program's own, and the order of the files is part of the project
    bool compiledAsOne(const LanguageDef &language) const;
    bool assembly(const LanguageDef &language) const { return language.role == "assembler"; }
    // the library a build takes because it has a source of that language ("when": "shalimar")
    const LibraryDef *libraryWhen(const std::string &arch, const std::string &language, const std::string &crt = std::string()) const;
    const LinkerCommandFileDef *linkerCommandFile(const std::string &id) const;
};

// objects merge field by field, anything else replaces the shipped value
Json merged(const Json &shipped, const Json &user);

// reads the definitions; what is wrong with the file is said in `problems`
Definitions readDefinitions(const Json &root, Words &problems);

}}

#endif
