#ifndef EDITOR_TOOLCHAIN_H
#define EDITOR_TOOLCHAIN_H

#include <string>
#include <vector>

#include "syntax.h"

namespace editor {

enum ToolchainKind {
    ToolAuto = 0,
    ToolCc1,
    ToolMsvc,
    ToolShc,
    ToolCxx,
    ToolCxx1,
    ToolCount
};

enum Configuration {
    ConfigDebug = 0,
    ConfigRelease,
    ConfigCount
};

const char* configName(Configuration config);

std::string configFlags(ToolchainKind kind, Configuration config,
                        const std::string& arch);

bool optimises(ToolchainKind kind);

bool emitsDebugInfo(ToolchainKind kind, const std::string& arch);
// **A Debug build for x86_64-windows that writes its own CodeView** (GRIDEx3): c90, cpp11 and shalimar under
// -g -masm=masm write it in the MASM spelling, GRIDE's masm carries it and GRIDE's link /DEBUG makes the PDB -
// no clang, no Microsoft link.exe.
bool ownCodeView(ToolchainKind kind, const std::string& arch, Configuration config);
// **A Debug build whose CodeView clang must carry (M10)**: a compiler with none of its own - -g's GNU spelling,
// clang's assembler, link.exe /DEBUG for the PDB. None of the three since GRIDEx3; the road is kept for one.
bool debugsWithCodeView(ToolchainKind kind, const std::string& arch, Configuration config);
// **Whether clang is here to assemble that CodeView** - Visual Studio's "C++ Clang tools", an LLVM
// installation, or PATH, where cpp11 looks. Without it such a build is made the Release way.
bool clangAvailable();
// The line such a Debug build says first when clang is not here, "" otherwise.
std::string lineTableNote(ToolchainKind kind, const std::string& arch, Configuration config);

// **The C runtime of a Windows link** (GRIDEx3): GRIDE's own - grtstart, grtcompat, grtcxx, grtc and grtwin.lib and
// GRIDE's kernel32.lib, each by its full path, with Microsoft's libraries refused - or Microsoft's, libcmt and the
// UCRT by name through LIB. settings::windowsRuntime() chooses; automatic is Microsoft's only where Visual Studio is
// found and cl's objects are in the link. error is set where GRIDE's is needed and not there, and nothing links.
struct WindowsCrt {
    std::string crt;          // "grt" or "msvc"
    std::string libraries;    // what the link line names after the objects and the project's libraries
    std::string said;         // what a build says once about it: a line, or two split by "\n"
    std::string warning;      // GRIDE's runtime chosen with cl's objects in the link
    std::string error;
};
WindowsCrt windowsCrt(Configuration config, bool clObjects);
// Where GRIDE's runtime is: $GRT, else lib beside the editor.
std::string grtDirectory();
// **Whether the program is linked here rather than by its compiler**: c90's, cpp11's and shalimar's for
// x86_64-windows, whose own link names Microsoft's runtime by name - and GRIDE's is named by its paths.
bool linkedHere(ToolchainKind kind, const std::string& arch);

std::vector<std::string> debugNote(ToolchainKind kind, const std::string& arch);

const char* hostCxxName();

ToolchainKind hostCppToolchain();

// **What lnk6x is handed for a tms6747 link besides the objects**, from a CCS project
// (ccs/ccsproject.h): its linker command files, sizes, search paths and libraries. Not given,
// and the link takes GRIDE's own flat memory map (compile.cpp, tiLinkCmd).
struct TiLink {
    bool given;
    std::vector<std::string> cmdFiles;      // absolute, in the order CCS lists them
    std::string heap, stack;                // --heap_size=, --stack_size=; "" leaves the command file's
    std::vector<std::string> searchPaths;   // -i, absolute; the compiler's own two are left out
    std::vector<std::string> libraries;     // -l, libc.a already mapped to rts6740_elf_eh.lib
    int romModel;                           // 1 --rom_model, 0 --ram_model, -1 unsaid

    TiLink() : given(false), romModel(-1) {}
};

// c90, cpp11 and shalimar since 3.5: the VM6747 line, the first two carrying tms6747. The kinds
// keep their names, cc1, cxx1 and shc, being the same compilers one target on.
struct Toolchain {
    ToolchainKind kind;
    std::string cc1;
    std::string cl;
    std::string shc;
    std::string cxx;
    std::string cxx1;

    // Where the shipped headers are, from the settings: include/ is cxx1's and lib/ is cc1's.
    // Empty leaves each compiler to find its own.
    std::string include;
    std::string lib;

    // The project's own: header directories every compiler searches first,
    // absolute, in the order the project lists them, and libraries linked
    // after the objects.
    std::vector<std::string> includes;
    std::vector<std::string> libraries;
    // A CCS project's link, for tms6747; given nowhere else.
    TiLink tiLink;
    // A single file's tms6747 build links its .out as well, for a Run or a Verify (R5); a project's always does.
    bool linkSingleFile = false;
    // No .out, and the build still succeeds: the project's "emulateOnly", or an Emulate on vm6747 (D2, R5).
    bool emulateOnly = false;
    bool emulating = false;

    Toolchain()
        : kind(ToolAuto), cc1("c90.exe"), cl("cl"), shc("shalimar.exe"),
          cxx(hostCxxName()), cxx1("cpp11.exe") {}
};

// The header directories a compiler is given, as flags: the project's first, then the shipped ones it reads - cc1 lib/, cxx1 include/; shc gets none.
std::string includeFlags(const Toolchain& tool, ToolchainKind kind);
// The project's libraries, spelled for the link.
std::string libraryArguments(const Toolchain& tool);
// The Shalimar runtime built for a Windows C runtime (WindowsCrt::crt), by its full path: shmrt-x86_64-windows-grt.lib
// over GRIDE's (grtDirectory), shmrt-x86_64-windows.lib over Microsoft's (lib beside shalimar); -debug for the session's.
std::string windowsShalimarRuntime(const Toolchain& tool, const std::string& crt, bool debugRuntime);

ToolchainKind resolve(const Toolchain& tool, Language lang);

const char* toolchainName(ToolchainKind kind);
// The word a project file or settings.json spells a compiler with, and back.
ToolchainKind toolchainFrom(const std::string& word);
const char* toolchainWord(ToolchainKind kind);
const char* programOf(const Toolchain& tool, ToolchainKind kind);

// **The fourth target runs on an emulator.** tms6747 is the TI TMS320C6747; c90 and cpp11 - the
// VM6747 line, docked since 3.5 - know it beside the three host targets, and vm6747 runs what they
// emit. Nothing is assembled or linked: the program is the .s file, or a directory of them.
bool isEmulated(const std::string& arch);
std::string emulatorProgram();
// Visual Studio's installation folder: the one whose vcvars64.bat settings.json names, else the newest
// vswhere finds; "" when none. Its tools and libraries are found below it - no batch file is run.
std::string visualStudioRoot();
// The C6000 assembler beside the editor (ASM6x's asm6x.exe), or empty when it is not there; $ASM6X names one elsewhere.
std::string c6xAssembler();
// Why no asm6x can run - none beside the editor, or $ASM6X naming one that is gone; empty when one can (D2).
std::string c6xAssemblerMissing();
// The command that runs a built program: the program itself, or the emulator
// with it.
std::string launchCommand(const std::string& program, bool shalimar = false,
                          const std::vector<std::string>& args = std::vector<std::string>());
// **The C6747 simulator, beside the emulator (5.0).** sim6747 runs the TI program a tms6747 build
// links, <program>.out - TI's boot and runtime, instruction by instruction - where vm6747 runs the
// assembly. $SIM6747 names one elsewhere; empty when there is none.
std::string simulatorProgram();
// Whether a built program is the emulated target's: a .s file or a <program>.vm directory.
bool isEmulatedProgram(const std::string& program);
// The .out a tms6747 build linked beside <program>.vm, or the name it would have: <program>.out.
std::string tiProgramOf(const std::string& program);
// The command that runs it: the simulator, --run, and -c for the cycle count on stderr.
std::string simulateCommand(const std::string& out);
// The line a Shalimar debug session runs: empty for a host program, which is run as it is; for a
// C6000 build, sim6747 running its .out, where the program's session talks over CIO's stdin and stderr.
std::string sessionCommand(const std::string& program);
// The directory of runtime assembly a Shalimar program needs on the emulator.
std::string shalimarRuntimeDir(Configuration config = ConfigRelease);
// RTS6x, the project's own C6747 runtime: lib/rts6x-tms6747 beside the editor, holding rts6x.lib (or $RTS6X).
std::string rts6xRuntimeDir();
// Where a project's program goes for the emulated target: <program>.vm, a directory of assembly, the Windows .exe dropped.
std::string emulatedProgram(const std::string& program);

std::string toolchainShown(const Toolchain& tool, ToolchainKind kind);

bool usesArch(ToolchainKind kind);

bool canCompile(ToolchainKind kind, Language lang);
std::string refusal(ToolchainKind kind, Language lang);

const char* hostArch();
// One of the three machines' own targets, as against the emulated C6000.
bool isHostArch(const std::string& arch);

bool runsHere(ToolchainKind kind, const std::string& arch);

std::string whyNotRun(ToolchainKind kind, const std::string& arch);

struct Recipe {
    std::string command;
    // A tms6747 group's per-source compiles, run at once on GRIDE's pool (P3); command is them joined by &&.
    std::vector<std::string> commands;
    std::string assemblyPath;
    std::vector<std::string> leftovers;
};

Recipe assemblyRecipe(const Toolchain& tool, ToolchainKind kind,
                      const std::string& source, Language lang,
                      const std::string& arch, Configuration config);

std::string shownCommand(const Toolchain& tool, ToolchainKind kind,
                         const std::string& source, Language lang,
                         const std::string& arch, Configuration config);

Recipe programRecipe(const Toolchain& tool, ToolchainKind kind,
                     const std::string& source, Language lang,
                     const std::string& arch, Configuration config);

// For tms6747 a Run's line ends in sim6747 on the .out (R5); `emulate` ends it in vm6747 on the .s.
std::string shownProgramCommand(const Toolchain& tool, ToolchainKind kind,
                                const std::string& source, Language lang,
                                const std::string& arch, Configuration config, bool emulate = false);

// " -masm=masm" for a Debug build with its own CodeView, and for cpp11 on x86_64-windows when settings name an
// assembler; " -masm=gnu" for a Debug build whose CodeView clang carries.
std::string assemblerFlag(ToolchainKind kind, const std::string& arch, Configuration config);
Recipe targetRecipe(const Toolchain& tool, ToolchainKind kind,
                    const std::vector<std::string>& sources, Language lang,
                    const std::string& arch, Configuration config,
                    const std::string& program);

Recipe objectRecipe(const Toolchain& tool, ToolchainKind kind,
                    const std::vector<std::string>& sources, Language lang,
                    const std::string& arch, Configuration config,
                    const std::string& objectDir, std::vector<std::string>& objects);

// On Windows crt is the C runtime's part of the line (WindowsCrt::libraries), Microsoft's by name when empty;
// microsoftDebug says a Debug link is Microsoft's link.exe's - cl's objects, or clang's CodeView, are in it.
Recipe linkRecipe(const Toolchain& tool, const std::vector<std::string>& objects,
                  bool withCpp, const std::string& arch, Configuration config,
                  const std::string& program, const std::string& crt = std::string(),
                  bool microsoftDebug = false);

std::string linkerName(bool withCpp);

bool prepareFor(ToolchainKind kind, Configuration config, const std::string& arch = std::string());
// Whether the vendor's tools for a target are here, found as a build finds them and never by PATH: Visual Studio through vswhere or "vcvars", TI's lnk6x under "ti". The native question is put only when this says yes.
bool nativeToolsAvailable(const std::string& arch);

}

#endif
