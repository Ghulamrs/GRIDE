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
// **A c90 or cpp11 Debug build for x86_64-windows (M10)**: -g's CodeView, the GNU spelling clang
// assembles, link.exe /DEBUG for the PDB, and cdb to read it - whatever masm and LINK settings name.
bool debugsWithCodeView(ToolchainKind kind, const std::string& arch, Configuration config);

std::vector<std::string> debugNote(ToolchainKind kind, const std::string& arch);

const char* hostCxxName();

ToolchainKind hostCppToolchain();

// **What lnk6x is handed for a tms6747 link besides the objects**, from a CCS project
// (ccs/ccsproject.h): its linker command files, sizes, search paths and libraries. Not given,
// and the link takes RIDE's own flat memory map (compile.cpp, tiLinkCmd).
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
// Visual Studio's vcvars64.bat: the one settings.json names, else the newest vswhere finds; "" when none.
std::string visualStudioVcvars();
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
    // A tms6747 group's per-source compiles, run at once on RIDE's pool (P3); command is them joined by &&.
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

// " -masm=masm" for cpp11 on x86_64-windows when settings name an assembler; " -masm=gnu" for a Debug build with CodeView.
std::string assemblerFlag(ToolchainKind kind, const std::string& arch, Configuration config);
Recipe targetRecipe(const Toolchain& tool, ToolchainKind kind,
                    const std::vector<std::string>& sources, Language lang,
                    const std::string& arch, Configuration config,
                    const std::string& program);

Recipe objectRecipe(const Toolchain& tool, ToolchainKind kind,
                    const std::vector<std::string>& sources, Language lang,
                    const std::string& arch, Configuration config,
                    const std::string& objectDir, std::vector<std::string>& objects);

Recipe linkRecipe(const Toolchain& tool, const std::vector<std::string>& objects,
                  bool withCpp, const std::string& arch, Configuration config,
                  const std::string& program);

std::string linkerName(bool withCpp);

bool prepareFor(ToolchainKind kind, Configuration config, const std::string& arch = std::string());
// Whether the vendor's tools for a target are here, found as a build finds them and never by PATH: Visual Studio through vswhere or "vcvars", TI's lnk6x under "ti". The native question is put only when this says yes.
bool nativeToolsAvailable(const std::string& arch);

}

#endif
