#ifndef EDITOR_SETTINGS_H
#define EDITOR_SETTINGS_H

#include <string>
#include <vector>

#include "json.h"

namespace editor {

namespace settings {

std::string fileName();

std::string lastProject();

bool rememberProject(const std::string& directory);
// The last three projects opened, most recent first, as the Project menu lists them; lastProject() is the first.
std::vector<std::string> recentProjects();
// The last three files opened on their own, most recent first, for the
// File menu the same way.
std::vector<std::string> recentFiles();
bool rememberFile(const std::string& path);

bool plainFrame();

std::string configuration();
bool rememberConfiguration(const std::string& which);

// The window's font, "Consolas 11" style, and the indentation a file is
// laid out with when its project says nothing - both in the installation's
// settings.json since 2026-09-17, the Tools menu writing the font.
std::string codeFont();
bool rememberCodeFont(const std::string& described);
size_t indentWidth();
bool indentTabs();
bool rememberIndent(size_t width, bool tabs);

std::string setAside();
bool rememberPlainFrame(bool plain);

// **The installation's settings.json**, one directory above the editor's bin/ and read for every
// compile, project or not. It says where the shipped headers are (include/ is cxx1's, lib/ is
// cc1's, relative to the file when not absolute) and, when named, Visual Studio's batch file. Empty answers mean the directory of that name beside the file, else the compilers look for their own.
std::string installFile();
std::string includeDir();
std::string libDir();
std::string vcvars();
// **The assembler for x86_64-windows**, the project's own (Ghulamrs/MASM's asm) named by its
// path: c90 and cpp11 then assemble through it instead of ml64 and clang - C90_AS and CPP11_AS in
// their environment, and cpp11 told -masm=masm. Empty, and the compilers choose as they always did.
std::string assembler();
void overrideAssembler(const std::string& path);   // --assembler, for this run only
// **The linker for x86_64-windows**, the project's own (Ghulamrs/LINK's
// link) named by its path: a Windows build then links through it instead of
// Microsoft's link.exe. Empty, and link.exe (or C90_LD) as always.
std::string linker();
void overrideLinker(const std::string& path);      // --linker, for this run only
// **TI's C6000 compiler directory**, for tms6747: CCS's ti-cgt-c6000 root, whose bin\lnk6x links
// what asm6x made into a real .out against the runtime in its lib\ ("tilib" is a second directory,
// for the exception-handling build CCS does not ship). Empty, and a tms6747 build stops at the objects - at the assembly, without asm6x.
std::string ti();
std::string namedTi();     // ti() is this, named in settings.json or by --ti,
std::string detectedTi();  // else this: CCS's newest ti-cgt-c6000 with a bin/lnk6x
std::string tilib();
void overrideTi(const std::string& dir);           // --ti, for this run only
void overrideTilib(const std::string& dir);        // --tilib, for this run only
// **The linker for tms6747**, the project's own (Ghulamrs/LNK6X's lnk6x) named by its path: a
// tms6747 build then links its .out through it instead of TI's bin\lnk6x - still against TI's
// runtime, which "ti" and "tilib" name. Empty, and TI's lnk6x as always.
std::string tilinker();
void overrideTilinker(const std::string& path);    // --tilinker, for this run only
// **What was named for tms6747, there or not.** tilinker() answers nothing for a path that has gone; this answers what was asked for either way, so the build can say TI's stood in. Empty when nothing was named.
std::string namedTilinker();
// **Whether a build the project's own tools failed asks to go native.** "askNative", true by
// default: when masm, link or lnk6x did not build something and the compilers found no fault, the
// front end asks and a yes builds again through the vendor's; false never asks. The user's design, 2026-09-20: ours by default, the vendor's by consent.
bool askNative();
bool rememberAskNative(bool ask);
// The Compiler Options the installation sets, used where no project is open (options.h).
Json compilerOptions();
bool rememberCompilerOptions(const Json& options);
// For the build a yes was given to: the four above answer as if nothing of
// the project's own were named, so every recipe reaches for the vendor's.
void forceNative(bool on);
bool nativeForced();
// The compiler chosen at start when nothing says otherwise: auto, cc1, cxx1, shc, msvc or c++ - "compiler" in the installation's settings.json, which the menu writes.
std::string defaultCompiler();
// Header directories every compile searches after a project's own, and
// libraries every host link takes - "includes" and "libraries" in the
// installation's settings.json, each relative to the file unless absolute.
std::vector<std::string> includes();
std::vector<std::string> libraries();
bool rememberIncludes(const std::vector<std::string>& dirs);
bool rememberLibraries(const std::vector<std::string>& files);
bool rememberDefaultCompiler(const std::string& word);
bool rememberHeaderDirs(const std::string& include, const std::string& lib);
bool rememberVcvars(const std::string& file);
bool rememberAssembler(const std::string& file);
bool rememberLinker(const std::string& file);
bool rememberTi(const std::string& dir, const std::string& lib);
bool rememberTilinker(const std::string& file);
// **CCS projects opened as they are** (ccs/ccsproject.h): "ccs": { "enabled": true, "root": "C:/ti/ccsv7" }
// in the installation's settings.json. The root resolves ${CG_TOOL_ROOT} and may be empty - a Mac
// has no CCS - in which case the TI compiler directory a tms6747 build already links against stands in.
bool ccsEnabled();
std::string ccsRoot();
bool rememberCcs(bool enabled, const std::string& root);
void overrideCcs(bool enabled, const std::string& root);   // --ccs [--ccs-root dir], for this run only
// **RIDE's own state for a CCS project goes here, never into CCS's files**: the open file and the
// configuration, under "ccs": { "projects": { "<absolute dir>": { "open": ..., "config": ... } } }.
Json ccsProjectState(const std::string& dir);
bool rememberCcsProjectState(const std::string& dir, const Json& state);
// Writes the file with the two directories when there is none yet, so a person opening the installation sees what is in force.
bool writeInstallFileIfAbsent();
// For the suite: take this directory for the installation's, in place of the one above the running binary. Empty puts it back.
void pretendInstalledAt(const std::string& directory);

}
}

#endif
