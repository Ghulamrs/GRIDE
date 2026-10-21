#include "toolchain.h"

#include "compile.h"
#include "options.h"

#include "path.h"
#include "product.h"
#include "settings.h"

#include <cstdio>
#include <map>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#pragma comment(lib, "advapi32.lib")
const char kSep = '\\';
#else
#include <unistd.h>
const char kSep = '/';
#endif

namespace editor {

namespace {

std::string tempDir() {
#ifdef _WIN32
    const char* t = std::getenv("TEMP");
    if (!t) t = std::getenv("TMP");
    return t ? t : ".";
#else
    const char* t = std::getenv("TMPDIR");
    return t ? t : "/tmp";
#endif
}

std::string quote(const std::string& s) { return "\"" + s + "\""; }

std::string languageFlag(ToolchainKind kind, Language lang) {
    if (kind != ToolCxx) return std::string();
    return (lang == LangCpp) ? " -x c++" : " -x c";
}


std::string quoteDirectory(const std::string& s) {
    std::string path = s;
    if (!path.empty() && path[path.size() - 1] == kSep) path += kSep;
    return quote(path);
}

#ifdef _WIN32

// Where an installation keeps one folder per compiler version.
const char* const kMsvcTools = "VC\\Tools\\MSVC";

// The first line a program writes - started by itself, not through cmd.
std::string firstLineOf(const std::string& command) {
    std::string out;
    if (runCaptured(command, out) != 0) return std::string();
    std::string line = out.substr(0, out.find('\n'));
    while (!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == ' '))
        line.resize(line.size() - 1);
    return line;
}

// Whether version folder a is newer than b, number by number: 14.52.36725 is newer than 14.44.35207,
// and 10.0.26100.0 than 10.0.22621.0.
bool newerVersion(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        unsigned long x = 0, y = 0;
        while (i < a.size() && a[i] >= '0' && a[i] <= '9') x = x * 10 + (a[i++] - '0');
        while (j < b.size() && b[j] >= '0' && b[j] <= '9') y = y * 10 + (b[j++] - '0');
        if (x != y) return x > y;
        if (i < a.size()) ++i;
        if (j < b.size()) ++j;
    }
    return false;
}

// The newest version folder under dir that has inside below it, or "".
std::string newestHolding(const std::string& dir, const std::string& inside) {
    std::string best;
    std::vector<path::Entry> all = path::entries(dir);
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].directory && path::exists(path::join(path::join(dir, all[i].name), inside)) &&
            (best.empty() || newerVersion(all[i].name, best)))
            best = all[i].name;
    return best;
}

// **Which Visual Studio.** The installation whose vcvars64.bat settings.json names, when a person had
// to name one - the batch file is never run, its place says which installation - else the newest
// vswhere knows of (any version, any edition, Build Tools included), else the places the installer
// puts them. "" when there is none.
std::string findVisualStudio() {
    const std::string named = settings::vcvars();
    if (!named.empty()) {
        std::string root = named;   // <root>\VC\Auxiliary\Build\vcvars64.bat
        for (int i = 0; i < 4 && !root.empty(); ++i) root = path::parent(root);
        if (!root.empty() && path::isDirectory(path::join(root, kMsvcTools))) return root;
    }

    const char* programFiles = std::getenv("ProgramFiles(x86)");
    if (programFiles) {
        std::string vswhere = std::string(programFiles) +
                              "\\Microsoft Visual Studio\\Installer\\vswhere.exe";
        if (path::exists(vswhere)) {
            std::string where = firstLineOf(quote(vswhere) + " -latest -prerelease -products *"
                                            " -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
                                            " -property installationPath");
            if (!where.empty() && path::isDirectory(path::join(where, kMsvcTools))) return where;
        }
    }

    static const char* const roots[] = {
        "C:\\Program Files\\Microsoft Visual Studio\\18\\",
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\",
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\",
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2017\\"
    };
    static const char* const editions[] = {
        "Community", "Professional", "Enterprise", "BuildTools", "Preview"
    };
    for (size_t r = 0; r < sizeof roots / sizeof roots[0]; ++r)
        for (size_t e = 0; e < sizeof editions / sizeof editions[0]; ++e) {
            std::string root = std::string(roots[r]) + editions[e];
            if (path::isDirectory(path::join(root, kMsvcTools))) return root;
        }
    return std::string();
}

// The Windows SDK: where the registry says it is installed, else where its installer puts it.
std::string windowsKit() {
    char value[MAX_PATH];
    DWORD size = sizeof value;
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Microsoft\\Microsoft SDKs\\Windows\\v10.0",
                     "InstallationFolder", RRF_RT_REG_SZ, NULL, value, &size) == ERROR_SUCCESS) {
        std::string kit = value;
        while (kit.size() > 3 && kit[kit.size() - 1] == '\\') kit.resize(kit.size() - 1);
        if (path::isDirectory(path::join(kit, "Lib"))) return kit;
    }
    static const char* const homes[] = {"ProgramFiles(x86)", "ProgramFiles"};
    for (size_t i = 0; i < sizeof homes / sizeof homes[0]; ++i) {
        const char* home = std::getenv(homes[i]);
        if (!home) continue;
        std::string kit = path::join(home, "Windows Kits\\10");
        if (path::isDirectory(path::join(kit, "Lib"))) return kit;
    }
    return std::string();
}

// A variable's directories put ahead of what it held.
void putAhead(const char* name, const std::string& dirs) {
    const char* held = std::getenv(name);
    const std::string value = held && *held ? dirs + ";" + held : dirs;
    _putenv_s(name, value.c_str());
}

// **Visual Studio's environment without cmd** (GRIDE 1.0). GRIDE ran vcvars64.bat and took its `set`;
// a batch file runs only in cmd, and a policy may disable cmd - it does where this was written. So the
// directories vcvars64.bat would set are taken from the installation's layout, as Black and Grey take
// them (toolchain.json's visualStudio section): the newest compiler under VC\Tools\MSVC for cl, ml64
// and link.exe, clang under VC\Tools\Llvm, and the newest Windows SDK's headers, libraries and tools -
// PATH, INCLUDE and LIB, each ahead of what it held.
bool importMsvcEnvironment() {
    static int done = 0;
    if (done == 1) return true;
    // A failure is tried again once a person has named the batch file.
    if (done == -1 && settings::vcvars().empty()) return false;

    if (std::getenv("VSCMD_ARG_TGT_ARCH")) {
        done = 1;
        return true;
    }

    const std::string root = findVisualStudio();
    const std::string tools = root.empty() ? std::string() : path::join(root, kMsvcTools);
    const std::string version = tools.empty() ? std::string() : newestHolding(tools, "bin\\Hostx64\\x64\\cl.exe");
    if (version.empty()) {
        done = -1;
        return false;
    }
    const std::string msvc = path::join(tools, version);
    std::string bins = path::join(msvc, "bin\\Hostx64\\x64");
    std::string includes = path::join(msvc, "include");
    std::string libs = path::join(msvc, "lib\\x64");
    const std::string llvm = path::join(root, "VC\\Tools\\Llvm\\x64\\bin");
    if (path::isDirectory(llvm)) bins += ";" + llvm;

    const std::string kit = windowsKit();
    const std::string sdk = kit.empty() ? std::string() : newestHolding(path::join(kit, "Lib"), "um\\x64\\kernel32.lib");
    if (!sdk.empty()) {
        libs += ";" + path::join(kit, "Lib\\" + sdk + "\\ucrt\\x64") + ";" + path::join(kit, "Lib\\" + sdk + "\\um\\x64");
        static const char* const headers[] = {"ucrt", "um", "shared", "winrt", "cppwinrt"};
        for (size_t i = 0; i < sizeof headers / sizeof headers[0]; ++i) {
            const std::string dir = path::join(kit, "Include\\" + sdk + "\\" + headers[i]);
            if (path::isDirectory(dir)) includes += ";" + dir;
        }
        const std::string kitBin = path::join(kit, "bin\\" + sdk + "\\x64");
        if (path::isDirectory(kitBin)) bins += ";" + kitBin;
        _putenv_s("WindowsSdkDir", (kit + "\\").c_str());
        _putenv_s("WindowsSDKVersion", (sdk + "\\").c_str());
    }

    putAhead("PATH", bins);
    putAhead("INCLUDE", includes);
    putAhead("LIB", libs);
    _putenv_s("VSINSTALLDIR", (root + "\\").c_str());
    _putenv_s("VCToolsInstallDir", (msvc + "\\").c_str());
    _putenv_s("VSCMD_ARG_HOST_ARCH", "x64");
    _putenv_s("VSCMD_ARG_TGT_ARCH", "x64");
    done = 1;
    return true;
}
#endif

}

std::string visualStudioRoot() {
#ifdef _WIN32
    return findVisualStudio();
#else
    return std::string();
#endif
}

const char* hostCxxName() {
#if defined(_WIN32)
    return "cl";
#elif defined(__APPLE__)
    return "clang++";
#else
    return "g++";
#endif
}

ToolchainKind hostCppToolchain() {
#ifdef _WIN32
    return ToolMsvc;
#else
    return ToolCxx;
#endif
}

// C++ goes to cxx1 the way C goes to cc1: the compiler this editor is for,
// with the machine's own reachable by name. Until 3.0 C++ had no decision in
// it and went straight to the host's; now it has the same one C has.
ToolchainKind resolve(const Toolchain& tool, Language lang) {
    if (tool.kind != ToolAuto) return tool.kind;
    if (lang == LangShalimar) return ToolShc;

    return (lang == LangCpp) ? ToolCxx1 : ToolCc1;
}

ToolchainKind toolchainFrom(const std::string& word) {
    if (word == product::kCompilerC) return ToolCc1;
    if (word == "msvc" || word == "cl") return ToolMsvc;
    if (word == product::kCompilerShalimar) return ToolShc;
    if (word == product::kCompilerCpp) return ToolCxx1;

    if (word == "c++" || word == "cxx" || word == "g++" || word == "clang++")
        return hostCppToolchain();
    return ToolAuto;
}

const char* toolchainWord(ToolchainKind kind) {
    if (kind == ToolCc1) return product::kCompilerC;
    if (kind == ToolMsvc) return "msvc";
    if (kind == ToolShc) return product::kCompilerShalimar;
    if (kind == ToolCxx1) return product::kCompilerCpp;

    if (kind == ToolCxx) return "c++";
    return "auto";
}

const char* toolchainName(ToolchainKind kind) {
    switch (kind) {
        case ToolMsvc: return "cl";
        case ToolCc1:  return product::kCompilerC;
        case ToolShc:  return product::kCompilerShalimar;
        case ToolCxx:  return "c++";
        case ToolCxx1: return product::kCompilerCpp;
        default:       return "auto";
    }
}

std::string toolchainShown(const Toolchain& tool, ToolchainKind kind) {
    if (kind != ToolCxx) return toolchainName(kind);

    return path::filename(tool.cxx);
}

const char* programOf(const Toolchain& tool, ToolchainKind kind) {
    if (kind == ToolMsvc) return tool.cl.c_str();
    if (kind == ToolShc) return tool.shc.c_str();
    if (kind == ToolCxx) return tool.cxx.c_str();
    if (kind == ToolCxx1) return tool.cxx1.c_str();
    return tool.cc1.c_str();
}

bool usesArch(ToolchainKind kind) {
    return kind == ToolCc1 || kind == ToolShc || kind == ToolCxx1;
}

std::string includeFlags(const Toolchain& tool, ToolchainKind kind) {
    if (kind == ToolShc) return std::string();
    const char* flag = (kind == ToolMsvc) ? " /I" : " -I";

    std::string flags;
    for (size_t i = 0; i < tool.includes.size(); ++i) flags += flag + quote(tool.includes[i]);
    if (kind == ToolCc1 && !tool.lib.empty()) flags += flag + quote(tool.lib);
    if (kind == ToolCxx1 && !tool.include.empty()) flags += flag + quote(tool.include);
    return flags;
}

std::string libraryArguments(const Toolchain& tool) {
    std::string named;
    for (size_t i = 0; i < tool.libraries.size(); ++i) named += " " + quote(tool.libraries[i]);
    return named;
}

// The target as each compiler spells it: `-arch x` to cc1 and cxx1, `--target=x` to shc.
std::string archFlag(ToolchainKind kind, const std::string& arch) {
    return kind == ToolShc ? " --target=" + arch : " -arch " + arch;
}

bool isEmulated(const std::string& arch) { return arch == "tms6747"; }

std::string emulatorProgram() {
    const char* fromEnv = std::getenv("VM6747");
    if (fromEnv && *fromEnv) return fromEnv;
    std::string beside = path::besideProgram("vm6747.exe");
    if (beside.empty()) beside = path::besideProgram("vm6747");
    return beside.empty() ? std::string("vm6747") : beside;
}

std::string simulatorProgram() {
    const char* fromEnv = std::getenv("SIM6747");
    if (fromEnv && *fromEnv) return fromEnv;
    std::string beside = path::besideProgram("sim6747.exe");
    if (beside.empty()) beside = path::besideProgram("sim6747");
    return beside;
}

bool isEmulatedProgram(const std::string& program) {
    std::string leaf = path::filename(program);
    while (!leaf.empty() && (leaf.back() == '/' || leaf.back() == '\\')) leaf.pop_back();
    return (leaf.size() > 2 && leaf.compare(leaf.size() - 2, 2, ".s") == 0) ||
           (leaf.size() > 3 && leaf.compare(leaf.size() - 3, 3, ".vm") == 0);
}

std::string tiProgramOf(const std::string& program) {
    std::string stem = program;
    while (!stem.empty() && (stem.back() == '/' || stem.back() == '\\')) stem.pop_back();
    const char* ends[] = {".vm", ".exe", ".s", ".out"};
    for (const char* e : ends) {
        const size_t n = std::strlen(e);
        if (stem.size() > n && stem.compare(stem.size() - n, n, e) == 0) { stem.resize(stem.size() - n); break; }
    }
    return stem + ".out";
}

std::string simulateCommand(const std::string& out) {
    return quote(simulatorProgram()) + " --run -c " + quote(out);
}

std::string sessionCommand(const std::string& program) {
    if (!isEmulatedProgram(program)) return std::string();
    const std::string out = tiProgramOf(program), sim = simulatorProgram();
    if (sim.empty() || !path::exists(out)) return std::string();
    return quote(sim) + " --run " + quote(out);
}

std::string c6xAssembler() {
    const char* fromEnv = std::getenv("ASM6X");
    if (fromEnv && *fromEnv) return fromEnv;
    return path::besideProgram("asm6x.exe");
}

// Why no asm6x can run: none beside the editor, or $ASM6X naming one that is not there (D2).
std::string c6xAssemblerMissing() {
    const std::string as = c6xAssembler();
    if (as.empty())
        return std::string("no asm6x beside ") + product::kName + ": a tms6747 build is a TI program, and nothing here "
               "assembles one - Build > Emulate on vm6747 runs the .s without it, and \"emulateOnly\": true in the "
               "project's .pro makes that the project's build";
    if (!path::exists(as)) return "asm6x named by $ASM6X is not there: " + as;
    return std::string();
}

std::string emulatedProgram(const std::string& program) {
    std::string name = program;
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".exe") == 0) name.resize(name.size() - 4);
    return name + ".vm";
}

// The Shalimar runtime for the C6000: a directory of the .s cpp11 wrote from it, beside the editor in lib/ -
// shmrt-tms6747 at -O2, and for a Debug build shmrt-tms6747-debug at -O0 with SHM_DEBUG where it is there.
std::string shalimarRuntimeDir(Configuration config) {
    const char* fromEnv = std::getenv(config == ConfigDebug ? "SHMRT6747D" : "SHMRT6747");
    if (fromEnv && *fromEnv) return fromEnv;
    // A directory, which besideProgram does not answer for: it names files.
    std::string where = path::programDirectory();
    if (where.empty()) return std::string();
    std::string dir = path::join(path::join(where, "lib"), "shmrt-tms6747");
    if (config == ConfigDebug && path::isDirectory(dir + "-debug")) return dir + "-debug";
    return path::isDirectory(dir) ? dir : std::string();
}

// RTS6x for a TI program: the directory beside the editor whose rts6x.lib lnk6x links against
// when TI's own compiler directory is not named (5.1). Empty where it is not there.
std::string rts6xRuntimeDir() {
    const char* fromEnv = std::getenv("RTS6X");
    std::string dir;
    if (fromEnv && *fromEnv) dir = fromEnv;
    else if (!path::programDirectory().empty())
        dir = path::join(path::join(path::programDirectory(), "lib"), "rts6x-tms6747");
    return !dir.empty() && path::exists(path::join(dir, "rts6x.lib")) ? dir : std::string();
}

/*  **The project's run arguments go last, after everything the emulated
 *  target needs first.** For a native program that is simply the program and
 *  its arguments; for a .s or a .vm it is the emulator, the program, the
 *  Shalimar runtime if there is one, and only then the arguments - which is
 *  the order the emulator reads them in. Each is quoted on its own, so a path
 *  with a space survives. */
std::string launchCommand(const std::string& program, bool shalimar,
                          const std::vector<std::string>& args) {
    std::string leaf = path::filename(program);
    bool assembly = leaf.size() > 2 && leaf.compare(leaf.size() - 2, 2, ".s") == 0;
    bool directory = leaf.size() > 3 && leaf.compare(leaf.size() - 3, 3, ".vm") == 0;
    std::string command;
    if (assembly || directory) {
        command = quote(emulatorProgram()) + " " + quote(program);
        if (shalimar) {
            std::string runtime = shalimarRuntimeDir();
            if (!runtime.empty()) command += " " + quote(runtime);
        }
    } else {
        command = quote(program);
    }
    for (size_t i = 0; i < args.size(); ++i) command += " " + quote(args[i]);
    return command;
}

const char* configName(Configuration config) {
    return config == ConfigRelease ? "release" : "debug";
}

bool optimises(ToolchainKind kind) {
    return kind == ToolMsvc || kind == ToolCxx || kind == ToolCc1 || kind == ToolCxx1;
}

bool emitsDebugInfo(ToolchainKind kind, const std::string& arch) {

    if (kind == ToolMsvc) return true;

    if (kind == ToolCxx) return true;

    // shalimar since D1 to D5 (2026-10-13, Compiler-Si's CLAUDE.md "Debug information for the debuggers
    // GRIDE drives"): DWARF on the two GNU targets, CodeView on x86_64-windows through clang's assembler
    // and link.exe /DEBUG, as c90's and cpp11's, and cl6x's .dwpsn for the C6000 as cpp11 writes it -
    // which vm6747 --debug stops on, and sim6747 --run --debug through the .debug_line asm6x and lnk6x
    // make of it.
    if (kind == ToolShc)
        return arch == "x86_64-linux" || arch == "arm64-darwin" || arch == "x86_64-windows" ||
               isEmulated(arch);

    // cc1 and cxx1 alike: DWARF on the two GNU targets, CodeView on x86_64-windows (M10). For the
    // C6000, both write cl6x's .dwpsn source positions under -g (cpp11 since 1.8, c90 since 1.3).
    if (kind != ToolCc1 && kind != ToolCxx1) return false;
    if (isEmulated(arch)) return true;      /* cpp11 since 1.8, c90 since 2026-10-13: cl6x's .dwpsn under -g */
    return arch == "x86_64-linux" || arch == "arm64-darwin" || arch == "x86_64-windows";
}

bool debugsWithCodeView(ToolchainKind kind, const std::string& arch, Configuration config) {
    return config == ConfigDebug && arch == "x86_64-windows" &&
           (kind == ToolCc1 || kind == ToolCxx1 || kind == ToolShc) && emitsDebugInfo(kind, arch);
}

std::string configFlags(ToolchainKind kind, Configuration config,
                        const std::string& arch) {

    // **c90, cpp11 and shalimar take what Compiler Options set** (options.h), whose defaults are
    // the flags this function wrote before the dialog: -O2 -DNDEBUG=1 or -g -D_DEBUG=1, and --debug
    // for shalimar off the emulator, where the runtime beside it is the release one.
    if (kind == ToolShc || kind == ToolCc1 || kind == ToolCxx1)
        return options::flags(options::active(), kind, config, arch);

    // **cxx1's own -O2, not cl's.** This line passed -O2 while cxx1 had no -O flag at all, so every
    // Release C++ build failed, unseen because the editor defaults to Debug; cxx1 implements -O1
    // and -O2 now. Until 2026-09-26 -O2 went to the host's c++ alone; c90 and cpp11 take their own now.
    if (kind == ToolCxx)
        return config == ConfigRelease ? " -O2 -DNDEBUG=1" : " -g -D_DEBUG=1";

    // cl's own switches: /O2 is Microsoft's full optimisation, and it belongs
    // to this toolchain alone.
    if (kind == ToolMsvc)
        return config == ConfigRelease ? " /O2 /DNDEBUG" : " /Od /Zi /D_DEBUG";

    if (config == ConfigRelease) return " -O2 -DNDEBUG=1";
    return emitsDebugInfo(kind, arch) ? " -g -D_DEBUG=1" : " -D_DEBUG=1";
}

std::vector<std::string> debugNote(ToolchainKind kind, const std::string& arch) {
    std::vector<std::string> said;
    if (kind == ToolCxx) {
        said.push_back("This is the machine's own C++ compiler, so a debug build has real");
        said.push_back("DWARF in it and lldb or gdb reads it - there was never a question");
        said.push_back("about that one. What is below is the assembly the build produced,");
        said.push_back("read back out of itself; the editor stops at -S and assembles");
        said.push_back("nothing, so nothing has been linked or run.");
    } else if (emitsDebugInfo(kind, arch) && arch == "x86_64-windows") {
        said.push_back(std::string(toolchainName(kind)) + " writes CodeView for " + arch +
                       " - line tables, types and");
        said.push_back("objects - which link.exe /DEBUG makes a .pdb of and cdb reads. What");
        said.push_back("is below is the assembly the build produced, read back out of itself.");
    } else if (kind == ToolShc && isEmulated(arch)) {
        said.push_back("shalimar writes cl6x's .dwpsn before each statement for " + arch + " since D5");
        said.push_back("(2026-10-13) - the source position and nothing else, no types and no");
        said.push_back("variables, which is what the C6747 path carries - and vm6747 --debug stops");
        said.push_back("on them (F8), as does sim6747 --run --debug on the image, through the");
        said.push_back(".debug_line asm6x and lnk6x make of them. This is what the build produced,");
        said.push_back("read back out of its own assembly.");
    } else if (kind == ToolShc && emitsDebugInfo(kind, arch)) {
        said.push_back("shalimar writes DWARF for " + arch + " since D1 (2026-10-13) - the line");
        said.push_back("table, each function's frame, its parameters, locals and the globals by");
        said.push_back("name and type (D2), and the blocks a name hides inside (D3) - so lldb or");
        said.push_back("gdb stops a Shalimar program on a line, steps it, walks its stack and");
        said.push_back("reads or sets a variable; an array is the runtime's {count, element,");
        said.push_back("data}. shm_line is still there, and still names a runtime error's line.");
    } else if (emitsDebugInfo(kind, arch)) {
        said.push_back(std::string(toolchainName(kind)) + " writes DWARF for " + arch +
                       " - line tables, types, objects and");
        said.push_back("lexical blocks - so a debugger has something to read here. This");
        said.push_back("editor is not that debugger: it builds to assembly and stops, so");
        said.push_back("nothing has been assembled, linked or run. What the build did leave");
        said.push_back("behind is the assembly, and this is what is in it.");
    } else if (kind == ToolShc) {
        said.push_back("shalimar writes no debug information for " + arch + " yet - that is");
        said.push_back("rung D5 (tms6747) of Compiler-Si's debugger work, not landed. Here a");
        said.push_back("Shalimar program carries its own position instead -");
        said.push_back("shm_line before every statement, in every build - which is what names");
        said.push_back("the line of a runtime error and what F8 stops on. What it cannot do is");
        said.push_back("read a variable. This is the assembly the build produced, read back");
        said.push_back("out of itself.");
    } else if (kind == ToolCxx1 && isEmulated(arch)) {
        said.push_back("cpp11 writes cl6x's .dwpsn before each statement for " + arch + " - the");
        said.push_back("source position and nothing else, no types and no variables - and");
        said.push_back("vm6747 --debug stops on them (F8). This is what the build produced,");
        said.push_back("read back out of its own assembly.");
    } else if (kind == ToolCc1 && isEmulated(arch)) {
        said.push_back("c90 writes cl6x's .dwpsn before each statement for " + arch + " - the");
        said.push_back("source position and nothing else, no types and no variables - and");
        said.push_back("vm6747 --debug, or sim6747 on the linked image, stops on them (F8).");
        said.push_back("This is what the build produced, read back out of its own assembly.");
    } else if (kind == ToolCc1 || kind == ToolCxx1) {
        said.push_back(std::string(toolchainName(kind)) + " writes no debug information for " +
                       arch + ": it generates MASM");
        said.push_back("there, and MASM carries no line table. So there is nothing to step");
        said.push_back("through. This is what the build produced, read back out of its own");
        said.push_back("assembly.");
    } else {
        said.push_back("cl is not asked for /Zi here, so this build carries no debug");
        said.push_back("information either. This is what it produced, read back out of its");
        said.push_back("own assembly.");
    }
    return said;
}

bool canCompile(ToolchainKind kind, Language lang) {
    if (lang == LangShalimar) return kind == ToolShc;
    if (kind == ToolShc) return false;
    if (lang == LangCpp) return kind == ToolMsvc || kind == ToolCxx || kind == ToolCxx1;
    if (lang == LangC) return kind != ToolCxx1;
    return false;
}

std::string refusal(ToolchainKind kind, Language lang) {
    if (lang == LangShalimar && kind != ToolShc)
        return std::string(toolchainName(kind)) +
               " does not compile Shalimar - Ctrl-K for automatic, and it picks shalimar";
    if (kind == ToolShc && lang != LangShalimar)
        return std::string("shalimar compiles Shalimar, not ") + languageName(lang) +
               " - Ctrl-K for automatic";
    if (lang == LangCpp && kind == ToolCc1)
        return "c90 compiles C, not C++ - Ctrl-K for automatic, and it picks cpp11";
    if (lang == LangC && kind == ToolCxx1)
        return "cpp11 compiles C++, not C - Ctrl-K for automatic, and it picks c90";
    if (lang == LangPlain)
        return "nothing to compile: no extension names a language - .c, .cpp or .shl picks the compiler";
    if (lang != LangC && lang != LangCpp)
        return std::string("nothing to compile: this is ") + languageName(lang) +
               ", not C or C++";
    return "cannot compile this file";
}

const char* hostArch() {
#if defined(_WIN32)
    return "x86_64-windows";
#elif defined(__APPLE__)
    return "arm64-darwin";
#else
    return "x86_64-linux";
#endif
}

bool isHostArch(const std::string& arch) {
    return arch == "x86_64-linux" || arch == "arm64-darwin" || arch == "x86_64-windows";
}

bool runsHere(ToolchainKind kind, const std::string& arch) {

    if (kind == ToolMsvc || kind == ToolCxx) return true;
    // The C6000 runs anywhere the emulator is, for the three docked compilers.
    if (isEmulated(arch)) return kind == ToolCc1 || kind == ToolCxx1 || kind == ToolShc;
    return arch == hostArch();
}

std::string whyNotRun(ToolchainKind kind, const std::string& arch) {
    if (runsHere(kind, arch)) return std::string();
    if (isEmulated(arch))
        return std::string(toolchainName(kind)) + " has no " + arch + " target - it is c90's, cpp11's and shalimar's";
    return arch + " only reaches -S here - switch to " + hostArch() + " to run it";
}

namespace {

std::string mine(const std::string& what) {
    char id[32];
#ifdef _WIN32
    std::snprintf(id, sizeof id, "%lu", static_cast<unsigned long>(GetCurrentProcessId()));
#else
    std::snprintf(id, sizeof id, "%ld", static_cast<long>(getpid()));
#endif
    return tempDir() + kSep + what + "-" + id;
}

// "gride-run", "gride-objs": the temporary names a build leaves, from the product's name; mine() adds this process's id.
std::string productNamed(const char* what) { return std::string(product::kLower) + "-" + what; }

std::string programPath() {
    std::string path = mine(productNamed("run"));
#ifdef _WIN32
    path += ".exe";
#endif
    return path;
}

std::string objectFor(const std::string& dir, const std::string& source,
                      const char* suffix) {
    std::string leaf = path::filename(source);
    size_t dot = leaf.find_last_of('.');
    if (dot != std::string::npos) leaf.resize(dot);
    return dir.empty() ? leaf + suffix : path::join(dir, leaf + suffix);
}
}

Recipe targetRecipe(const Toolchain& tool, ToolchainKind kind,
                    const std::vector<std::string>& sources, Language lang,
                    const std::string& arch, Configuration config,
                    const std::string& program) {
    Recipe recipe;
    recipe.assemblyPath = program;

    std::string named;
    for (size_t i = 0; i < sources.size(); ++i) named += " " + quote(sources[i]);

    if (kind == ToolMsvc) {

        std::string objects = mine(productNamed("objs"));
        path::makeDirectories(objects);
        std::string forLanguage = (lang == LangCpp) ? " /TP /EHsc /std:c++14" : " /TC";
        std::string pdb = path::join(objects, productNamed("target") + ".pdb");
        // As the object recipe: _DEBUG makes the headers ask for libcpmtd, which only /MTd links.
        std::string crt = (config == ConfigDebug) ? " /MTd" : " /MT";

        recipe.command = quote(programOf(tool, kind)) + " /nologo /diagnostics:column" +
                         forLanguage + crt + configFlags(kind, config, arch) + includeFlags(tool, kind) +
                         (config == ConfigDebug ? " /Fd" + quote(pdb) : std::string()) +
                         " /Fe" + quote(program) +
                         " /Fo" + quoteDirectory(objects + kSep) + named + libraryArguments(tool) +
                         (config == ConfigDebug ? " /link /DEBUG" : std::string());

        for (size_t i = 0; i < sources.size(); ++i) {
            std::string leaf = path::filename(sources[i]);
            size_t dot = leaf.find_last_of('.');
            if (dot != std::string::npos) leaf.resize(dot);
            recipe.leftovers.push_back(path::join(objects, leaf + ".obj"));
        }
        if (config == ConfigDebug) recipe.leftovers.push_back(pdb);
        recipe.leftovers.push_back(objects);
        return recipe;
    }

    if (kind == ToolShc && !isEmulated(arch)) {

        // The project's and the installation's libraries, which shalimar links by --with= in the
        // order given - the ones its `uses` declarations need - where c90 and cpp11 leave them to the link.
        std::string with;
        for (size_t i = 0; i < tool.libraries.size(); ++i) with += " --with=" + quote(tool.libraries[i]);
        recipe.command = quote(programOf(tool, kind)) + named + " -o " + quote(program) +
                         configFlags(kind, config, arch) + with;
        return recipe;
    }

    if (isEmulated(arch)) {
        // Nothing links: the program is a directory of one .s per source,
        // which vm6747 assembles together. Each source is its own command,
        // since -S with several inputs writes beside them.
        std::string dir = emulatedProgram(program);
        path::removeTree(dir);
        path::makeDirectories(dir);
        recipe.assemblyPath = dir;
        if (kind == ToolShc) {
            // A Shalimar program is one compilation: the file with main() and the files it calls
            // into, which shc compiles as one - a library file alone has no main() and is refused.
            // One command, one .s, named after the program without Windows's .exe, as the .vm directory is.
            std::string stem = path::filename(emulatedProgram(program));
            stem.resize(stem.size() - 3);
            recipe.command = quote(programOf(tool, kind)) + named + " -S" + archFlag(kind, arch) + " -o " +
                             quote(path::join(dir, stem + ".s")) + configFlags(kind, config, arch);
            return recipe;
        }
        for (size_t i = 0; i < sources.size(); ++i) {
            recipe.commands.push_back(quote(programOf(tool, kind)) + " -S" + archFlag(kind, arch) +
                                      " " + quote(sources[i]) + " -o " +
                                      quote(path::join(dir, objectFor(std::string(), sources[i], ".s"))) +
                                      configFlags(kind, config, arch) + includeFlags(tool, kind));
            recipe.command += (i > 0 ? " && " : "") + recipe.commands.back();
        }
        return recipe;
    }

    recipe.command = quote(programOf(tool, kind)) + languageFlag(kind, lang) +
                     named + " -o " + quote(program) + configFlags(kind, config, arch) +
                     assemblerFlag(kind, arch, config) + includeFlags(tool, kind);
    return recipe;
}

namespace {

const char* hostDriver() {
    const char* named = std::getenv("C90_CC");
    return (named && *named) ? named : "cc";
}

const char* hostLinker() {
    const char* named = std::getenv("C90_LD");
    return (named && *named) ? named : "link.exe";
}

const char* hostCppDriver() {
    const char* named = std::getenv("CXX");
    return (named && *named) ? named : hostCxxName();
}

}

std::string linkerNameFor(bool windows, bool withCpp) {
    if (windows) {
        // The project's linker where one is named - see settings::linker.
        std::string named = settings::linker();
        return named.empty() ? std::string(hostLinker()) : named;
    }
    if (withCpp) return hostCppDriver();
    return hostDriver();
}

std::string linkerName(bool withCpp) {
#ifdef _WIN32
    return linkerNameFor(true, withCpp);
#else
    return linkerNameFor(false, withCpp);
#endif
}

Recipe objectRecipe(const Toolchain& tool, ToolchainKind kind,
                    const std::vector<std::string>& sources, Language lang,
                    const std::string& arch, Configuration config,
                    const std::string& objectDir, std::vector<std::string>& objects) {
    Recipe recipe;
    objects.clear();

    std::string named;
    for (size_t i = 0; i < sources.size(); ++i) named += " " + quote(sources[i]);

    if (kind == ToolMsvc) {

        std::string forLanguage = (lang == LangCpp) ? " /TP /EHsc /std:c++14" : " /TC";
        std::string crt = (config == ConfigDebug) ? " /MTd" : " /MT";
        std::string pdb = path::join(objectDir, productNamed("target") + ".pdb");

        recipe.command = quote(programOf(tool, kind)) + " /nologo /diagnostics:column /c" +
                         forLanguage + crt + configFlags(kind, config, arch) + includeFlags(tool, kind) +
                         (config == ConfigDebug ? " /Fd" + quote(pdb) : std::string()) +
                         " /Fo" + quoteDirectory(objectDir + kSep) + named;

        for (size_t i = 0; i < sources.size(); ++i)
            objects.push_back(objectFor(objectDir, sources[i], ".obj"));
        recipe.leftovers = objects;
        if (config == ConfigDebug) recipe.leftovers.push_back(pdb);
        return recipe;
    }

    if (isEmulated(arch)) {
        for (size_t i = 0; i < sources.size(); ++i) {
            std::string out = objectFor(objectDir, sources[i], ".s");
            recipe.commands.push_back(quote(programOf(tool, kind)) + " -S" + archFlag(kind, arch) +
                                      " " + quote(sources[i]) + " -o " + quote(out) +
                                      configFlags(kind, config, arch) + includeFlags(tool, kind));
            recipe.command += (i > 0 ? " && " : "") + recipe.commands.back();
            objects.push_back(out);
        }
        recipe.leftovers = objects;
        return recipe;
    }

    recipe.command = "cd " + quote(objectDir) + " && " +
                     quote(programOf(tool, kind)) + " -c" +
                     languageFlag(kind, lang) + named +
                     configFlags(kind, config, arch) + assemblerFlag(kind, arch, config) +
                     includeFlags(tool, kind);

    for (size_t i = 0; i < sources.size(); ++i)
        objects.push_back(objectFor(objectDir, sources[i], ".o"));
    recipe.leftovers = objects;
    return recipe;
}

Recipe linkRecipe(const Toolchain& tool, const std::vector<std::string>& objects,
                  bool withCpp, const std::string& arch, Configuration config,
                  const std::string& program) {
    (void)arch;
    Recipe recipe;
    recipe.assemblyPath = program;

    std::string named;
    for (size_t i = 0; i < objects.size(); ++i) named += " " + quote(objects[i]);
    named += libraryArguments(tool);

#ifdef _WIN32
    (void)withCpp;

    const char* crt = (config == ConfigDebug)
                          ? " libcmtd.lib libucrtd.lib libvcruntimed.lib"
                          : " libcmt.lib libucrt.lib libvcruntime.lib";
    // Debug links with link.exe /DEBUG: the project's LINK writes no PDB, and without one cdb sees no line.
    recipe.command = quote(config == ConfigDebug ? std::string(hostLinker()) : linkerNameFor(true, withCpp)) +
                     " /nologo /subsystem:console" +
                     (config == ConfigDebug ? std::string(" /DEBUG") : std::string()) +
                     " /out:" + quote(program) + named + crt +
                     " kernel32.lib legacy_stdio_definitions.lib";
#else

    recipe.command = quote(linkerNameFor(false, withCpp)) +
                     (config == ConfigDebug ? std::string(" -g") : std::string()) +
                     named + " -o " + quote(program) + " -lm";
#endif
    return recipe;
}

Recipe programRecipe(const Toolchain& tool, ToolchainKind kind,
                     const std::string& source, Language lang,
                     const std::string& arch, Configuration config) {
    Recipe recipe;
    std::string program = programOf(tool, kind);
    recipe.assemblyPath = programPath();

    if (isEmulated(arch) && usesArch(kind)) {
        // The program to run is the assembly: vm6747 runs it as it is - a
        // Shalimar one beside the runtime, which the launch adds.
        recipe.assemblyPath = mine(productNamed("run")) + ".s";
        recipe.command = quote(programOf(tool, kind)) + " -S" + archFlag(kind, arch) + " " +
                         quote(source) + " -o " + quote(recipe.assemblyPath) +
                         configFlags(kind, config, arch) + includeFlags(tool, kind);
        return recipe;
    }

    if (kind == ToolMsvc) {

        std::string obj = mine(productNamed("run")) + ".obj";
        std::string forLanguage = (lang == LangCpp) ? " /TP /EHsc /std:c++14" : " /TC";

        std::string pdb = mine(productNamed("run")) + ".pdb";
        recipe.command = quote(program) + " /nologo /diagnostics:column" + forLanguage +
                         configFlags(kind, config, arch) + includeFlags(tool, kind) +
                         (config == ConfigDebug ? " /Fd" + quote(pdb) : std::string()) +
                         " /Fe" + quote(recipe.assemblyPath) +
                         " /Fo" + quote(obj) + " " + quote(source) + libraryArguments(tool) +
                         (config == ConfigDebug ? " /link /DEBUG" : std::string());
        recipe.leftovers.push_back(obj);
        if (config == ConfigDebug) {
            recipe.leftovers.push_back(pdb);
            recipe.leftovers.push_back(mine(productNamed("run")) + ".ilk");
        }
        return recipe;
    }

    // cc1 and cxx1 take sources only, so a library rides on F4, where the host links the objects;
    // the machine's own C++ takes them here. assemblerFlag as on F4: without it cpp11 writes the
    // GNU spelling and hands masm.exe clang's command line - "usage: asm -t x64 ..." in the first 4.0 install.
    recipe.command = quote(program) + " " + quote(source) + " -o " +
                     quote(recipe.assemblyPath) + configFlags(kind, config, arch) +
                     assemblerFlag(kind, arch, config) + includeFlags(tool, kind) +
                     (kind == ToolCxx ? libraryArguments(tool) : std::string());
    return recipe;
}

std::string shownProgramCommand(const Toolchain& tool, ToolchainKind kind,
                                const std::string& source, Language lang,
                                const std::string& arch, Configuration config, bool emulate) {
    std::string program = programOf(tool, kind);
    if (isEmulated(arch) && usesArch(kind))
        return program + " -S" + archFlag(kind, arch) + " " + source + " -o " + productNamed("run") + ".s" +
               configFlags(kind, config, arch) + includeFlags(tool, kind) +
               (emulate ? " && vm6747 " + productNamed("run") + ".s" + (kind == ToolShc ? " lib/shmrt-tms6747" : "")
                        : " && asm6x && lnk6x -o " + productNamed("run") + ".out && sim6747 --run " +
                              productNamed("run") + ".out");
    if (kind == ToolMsvc)
        return program + " /diagnostics:column" +
               ((lang == LangCpp) ? " /TP /EHsc /std:c++14" : " /TC") +
               configFlags(kind, config, arch) + includeFlags(tool, kind) +
               " /Fe" + productNamed("run") + " " + source + libraryArguments(tool);
    return program + " " + source + " -o " + productNamed("run") + configFlags(kind, config, arch) +
           assemblerFlag(kind, arch, config) + includeFlags(tool, kind) +
           (kind == ToolCxx ? libraryArguments(tool) : std::string());
}

Recipe assemblyRecipe(const Toolchain& tool, ToolchainKind kind,
                      const std::string& source, Language lang,
                      const std::string& arch, Configuration config) {
    Recipe recipe;
    std::string stem = mine(productNamed("build"));
    std::string program = programOf(tool, kind);

    if (kind == ToolMsvc) {
        recipe.assemblyPath = stem + ".asm";
        std::string obj = stem + ".obj";

        std::string forLanguage = (lang == LangCpp) ? " /TP /EHsc /std:c++14" : " /TC";

        recipe.command = quote(program) + " /nologo /c /diagnostics:column /FAs" +
                         forLanguage + configFlags(kind, config, arch) + includeFlags(tool, kind) +
                         " /Fa" + quote(recipe.assemblyPath) +
                         " /Fo" + quote(obj) + " " + quote(source);
        recipe.leftovers.push_back(obj);
        return recipe;
    }

    if (kind == ToolShc) {
        recipe.assemblyPath = stem + (arch == "x86_64-windows" ? ".asm" : ".s");
        recipe.command = quote(program) + " -S " + quote(source) + " -o " +
                         quote(recipe.assemblyPath) + " --target=" + arch;
        return recipe;
    }

    recipe.assemblyPath = stem + ".s";
    // assemblerFlag too: a Debug build's -g is CodeView, which only the GNU spelling carries (M10).
    recipe.command = quote(programOf(tool, kind)) + " -S" + languageFlag(kind, lang) + " " +
                     quote(source) + " -o " + quote(recipe.assemblyPath) +
                     (usesArch(kind) ? " -arch " + arch : std::string()) +
                     configFlags(kind, config, arch) + assemblerFlag(kind, arch, config) +
                     includeFlags(tool, kind);
    return recipe;
}

std::string shownCommand(const Toolchain& tool, ToolchainKind kind,
                         const std::string& source, Language lang,
                         const std::string& arch, Configuration config) {
    std::string program = programOf(tool, kind);
    if (kind == ToolMsvc)
        return program + " /c /diagnostics:column /FAs" +
               ((lang == LangCpp) ? " /TP /EHsc /std:c++14" : " /TC") +
               configFlags(kind, config, arch) + includeFlags(tool, kind) + " " + source;
    if (kind == ToolShc)
        return program + " -S " + source + " --target=" + arch;
    return program + " -S " + source + " -arch " + arch +
           configFlags(kind, config, arch) + assemblerFlag(kind, arch, config) + includeFlags(tool, kind);
}

// The environment Compiler Options asks for (options.h): set where it has a value, removed where it
// is empty - cpp11 reads a variable that is set but empty as set.
static void optionEnvironment(Configuration config) {
    std::map<std::string, std::string> env = options::environment(options::active(), config);
    for (std::map<std::string, std::string>::const_iterator it = env.begin(); it != env.end(); ++it) {
#ifdef _WIN32
        _putenv_s(it->first.c_str(), it->second.c_str());
#else
        if (it->second.empty()) unsetenv(it->first.c_str());
        else setenv(it->first.c_str(), it->second.c_str(), 1);
#endif
    }
}

bool prepareFor(ToolchainKind kind, Configuration config, const std::string& arch) {
    optionEnvironment(config);
#ifdef _WIN32
    if (kind == ToolMsvc) return importMsvcEnvironment();

    importMsvcEnvironment();
    // The project's assembler, where one is named: all three compilers read
    // the variable, and cpp11 also needs -masm=masm - see assemblerFlag.
    // Not for a Debug build with CodeView: the compilers take *_AS even for the GNU spelling, and the
    // project's masm was handed clang's command line ("usage: asm -t x64") - so clang, by not naming one.
    std::string as = debugsWithCodeView(kind, arch, config) ? std::string() : settings::assembler();
    _putenv_s("C90_AS", as.c_str());
    _putenv_s("CPP11_AS", as.c_str());
    _putenv_s("SHALIMAR_AS", as.c_str());
    // And the linker for x86_64-windows the same way, where one is named: each compiler links its
    // own program through what *_LD says, else link.exe. An empty value unsets the variable,
    // which is what a yes to the native tools needs (settings::forceNative).
    // A Debug build that carries CodeView links with Microsoft's link.exe, the one linker that writes a PDB.
    std::string ld = debugsWithCodeView(kind, arch, config) ? std::string() : settings::linker();
    _putenv_s("C90_LD", ld.c_str());
    _putenv_s("CPP11_LD", ld.c_str());
    _putenv_s("SHALIMAR_LD", ld.c_str());
    return true;
#else
    (void)kind;
    (void)arch;
    return true;
#endif
}

bool nativeToolsAvailable(const std::string& arch) {
    if (isEmulated(arch)) {
        std::string ti = settings::ti();
        if (ti.empty()) return false;
        std::string lnk = path::join(path::join(ti, "bin"), "lnk6x.exe");
        return path::exists(lnk) || path::exists(path::join(path::join(ti, "bin"), "lnk6x"));
    }
#ifdef _WIN32
    if (arch == "x86_64-windows") return importMsvcEnvironment();
#endif
    return false;
}

std::string assemblerFlag(ToolchainKind kind, const std::string& arch, Configuration config) {
    // **A Debug build's CodeView goes through clang's assembler**, whatever assembler is named:
    // the project's masm carries no CodeView (M10, GRIDE docs/M10-ANALYSIS.md).
    // shalimar has no -masm: its -g alone chooses the GNU spelling and clang on this target (D4).
    if (debugsWithCodeView(kind, arch, config) && kind != ToolShc) return " -masm=gnu";
    if (kind == ToolCxx1 && arch == "x86_64-windows" && !settings::assembler().empty())
        return " -masm=masm";
    return std::string();
}

}
