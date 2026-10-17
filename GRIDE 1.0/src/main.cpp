#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "about.h"
#include "editor.h"
#include "path.h"
#include "product.h"
#include "settings.h"
#include "symbols.h"
#include "workspace.h"

static std::string calledIt(const char* argv0) {
    std::string name = (argv0 == 0 || *argv0 == 0) ? editor::product::kName : argv0;
    size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".exe") == 0)
        name.resize(name.size() - 4);
    return name;
}

int main(int argc, char** argv) {
    const std::string me = calledIt(argc > 0 ? argv[0] : 0);
    std::string file;
    std::string cc1;
    std::string project;
    std::string toolchain;
    std::string config;
    std::string cl;
    std::string shc;
    std::string cxx;
    std::string cxx1;
    std::string c2s;
    std::string arch;
    std::string assembler, linker;
    std::string ti, tilib, tilinker;
    std::string ccsRoot;
    bool ccs = false;
    bool build = false, runIt = false;
    int runner = 0;
    long width = 0;
    int plain = 0;
    int tabs = -1;
    int caseIndent = -1;

    // The installer's last step: the release record of the programs in a directory, for About to compare with.
    // Any file's CRC-32, as About computes it: the same answer on every system, which has no common command.
    if (argc >= 3 && std::strcmp(argv[1], "--crc32") == 0) {
        int status = 0;
        for (int k = 2; k < argc; ++k) {
            const std::string crc = editor::about::crc32Text(argv[k]);
            if (crc.empty()) { std::fprintf(stderr, "%s: cannot read %s\n", me.c_str(), argv[k]); status = 2; continue; }
            std::printf("%s  %s\n", crc.c_str(), argv[k]);
        }
        return status;
    }
    if (argc >= 3 && std::strcmp(argv[1], "--release-record") == 0) {
        std::vector<std::string> more;
        for (int k = 3; k < argc; ++k) more.push_back(argv[k]);
        const int wrote = editor::about::writeReleaseRecord(argv[2], more);
        if (wrote < 0) { std::fprintf(stderr, "%s: cannot write the release record in %s\n", me.c_str(), argv[2]); return 2; }
        std::printf("release record: %d programs in %s\n", wrote, argv[2]);
        return 0;
    }

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--c90") == 0 && i + 1 < argc) {
            cc1 = argv[++i];
        } else if (std::strcmp(argv[i], "--toolchain") == 0 && i + 1 < argc) {
            toolchain = argv[++i];
        } else if (std::strcmp(argv[i], "--cl") == 0 && i + 1 < argc) {
            cl = argv[++i];
        } else if (std::strcmp(argv[i], "--shalimar") == 0 && i + 1 < argc) {
            shc = argv[++i];
        } else if (std::strcmp(argv[i], "--cxx") == 0 && i + 1 < argc) {
            cxx = argv[++i];
        } else if (std::strcmp(argv[i], "--cpp11") == 0 && i + 1 < argc) {
            cxx1 = argv[++i];
        } else if (std::strcmp(argv[i], "--c2s") == 0 && i + 1 < argc) {
            c2s = argv[++i];
        } else if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config = argv[++i];
        } else if (std::strcmp(argv[i], "--project") == 0 && i + 1 < argc) {
            project = argv[++i];
        } else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            long w = std::atol(argv[++i]);
            if (w >= 1 && w <= 16) width = w;
        } else if (std::strcmp(argv[i], "--arch") == 0 && i + 1 < argc) {
            arch = argv[++i];
        } else if (std::strcmp(argv[i], "--assembler") == 0 && i + 1 < argc) {
            assembler = argv[++i];
        } else if (std::strcmp(argv[i], "--linker") == 0 && i + 1 < argc) {
            linker = argv[++i];
        } else if (std::strcmp(argv[i], "--tilinker") == 0 && i + 1 < argc) {
            tilinker = argv[++i];
        } else if (std::strcmp(argv[i], "--ti") == 0 && i + 1 < argc) {
            ti = argv[++i];
        } else if (std::strcmp(argv[i], "--tilib") == 0 && i + 1 < argc) {
            tilib = argv[++i];
        } else if (std::strcmp(argv[i], "--ccs") == 0) {
            ccs = true;
        } else if (std::strcmp(argv[i], "--ccs-root") == 0 && i + 1 < argc) {
            ccs = true;
            ccsRoot = argv[++i];
        } else if (std::strcmp(argv[i], "--build") == 0) {
            build = true;
        } else if (std::strcmp(argv[i], "--run") == 0) {
            build = true; runIt = true;
        } else if (std::strcmp(argv[i], "--simulate") == 0) {
            build = true; runIt = true; runner = 1;
        } else if (std::strcmp(argv[i], "--emulate") == 0) {
            build = true; runIt = true; runner = 3;
        } else if (std::strcmp(argv[i], "--verify") == 0) {
            build = true; runIt = true; runner = 2;
        } else if (std::strcmp(argv[i], "--plain") == 0) {
            plain = 1;
        } else if (std::strcmp(argv[i], "--tabs") == 0) {
            tabs = 1;
        } else if (std::strcmp(argv[i], "--case-indent") == 0) {
            caseIndent = 1;
        } else if (std::strcmp(argv[i], "-h") == 0 ||
                   std::strcmp(argv[i], "--help") == 0) {
            std::printf(
                "usage: %s [file] [--project dir] [--toolchain auto|c90|cpp11|msvc|shalimar|c++]\n"
                "           [--config debug|release] [--c90 path] [--cpp11 path] [--cl path]\n"
                "           [--shalimar path] [--cxx path] [--c2s path]\n"
                "           [--width n] [--tabs] [--case-indent] [--plain]\n"
                "       %s <project.pro or dir> [--arch a] [--assembler path] [--linker path]\n"
                "           [--ti dir [--tilib dir] [--tilinker path]] [--ccs [--ccs-root dir]]\n"
                "           --build | --run | --emulate | --verify\n"
                "       %s --crc32 file...      each file's CRC-32, as About and release.crc compute it\n"
                "  %s - the console half, which is %s.exe on Linux and\n"
                "  macOS and %sConsole.exe on Windows. %s.exe on Windows is the\n"
                "  same editor in a window, over the same core.\n"
                "\n"
                "  --toolchain    auto (the default) lets the file choose: C goes\n"
                "                 to c90, C++ to cpp11 and Shalimar to shalimar. C and C++\n"
                "                 each have a second answer - this machine's own\n"
                "                 compiler, cl on Windows and c++ elsewhere - and\n"
                "                 Shalimar goes to the only thing that reads it.\n"
                "                 Naming one uses it for everything, and it says so\n"
                "                 where it cannot take the file\n"
                "  --config       debug (the default) or release. For cl that is\n"
                "                 /Od /Zi /D_DEBUG or /O2 /DNDEBUG; for c90 and cpp11,\n"
                "                 -g and the define on the targets that carry a line\n"
                "                 table, and the define alone on the one that does not\n"
                "  --c90, --cpp11, the programs to run; $C90, $CPP11, $SHALIMAR and\n"
                "  --cl,          $CXX name them too, and without either a c90, cpp11\n"
                "  --shalimar,    or shalimar beside this editor is used, and failing\n"
                "  --cxx          that\n"
                "                 PATH is asked. cl is also found through Visual\n"
                "                 Studio 2022 itself, so no Developer Command Prompt\n"
                "                 is needed. --cxx is c++ by default, which is clang++\n"
                "                 on a Mac and g++ on Linux; a project file never\n"
                "                 names it, because which one it is, is a fact about\n"
                "                 a machine\n"
                "  --project      what the pane on the left shows; the file's own\n"
                "                 directory by default\n"
                "  --build, --run build the project's program the way F4 does - and\n"
                "                 run it, for --run - with no screen: the console is\n"
                "                 printed and the status is 0 when it built (and ran).\n"
                "                 --arch names the target, else the project's own;\n"
                "                 --assembler names the project's assembler for\n"
                "                 x86_64-windows for this run (Tools keeps one),\n"
                "                 --linker its linker there in place of link.exe;\n"
                "                 --ti names TI's C6000 compiler directory, whose\n"
                "                 lnk6x links a tms6747 build into a .out, and\n"
                "                 --tilib a directory with rts6740_elf_eh.lib,\n"
                "                 --tilinker the project's linker in place of lnk6x;\n"
                "                 --ccs opens a CCS 7.4 or 5.5 C6747 project folder as it\n"
                "                 is, which settings.json's \"ccs\" switch does for every run,\n"
                "                 and --ccs-root names the CCS install ${CG_TOOL_ROOT} is under\n"
                "                 For tms6747 --run builds the TI program - asm6x,\n"
                "                 lnk6x, RTS6x - and runs the .out on sim6747, the\n"
                "                 C6747 simulator (--simulate says the same)\n"
                "  --emulate,     for tms6747, the assembly run on vm6747, the\n"
                "  --verify       emulator, with no .out made - or, for --verify,\n"
                "                 both runs and the two outputs compared: 0 they\n"
                "                 agree, 3 they differ\n"
                "  --width n      columns per indent step (4)\n"
                "  --tabs         indent with tabs instead of spaces\n"
                "  --plain        frame the screen with - | + instead of the box\n"
                "                 characters, for a console that draws those from\n"
                "                 a second font and breaks the lines at every join\n"
                "  --case-indent  put case labels one step inside their switch\n"
                "                 rather than in its own column\n"
                "\n"
                "  F10 menu   Ctrl-B build this file   F5 run this file\n"
                "  F4 build the project's program   Ctrl-A lay out\n"
                "  F9 breakpoint   F8 debug   F7/F6 step over/into\n"
                "  F1 keys    Ctrl-Q quit\n",
                me.c_str(), me.c_str(), me.c_str(), editor::product::kName, editor::product::kName,
                editor::product::kName, editor::product::kName);
            return 0;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            std::fprintf(stderr, "%s: unknown option %s\n", me.c_str(), argv[i]);
            return 2;
        } else {
            file = argv[i];
        }
    }

    if (!toolchain.empty() && toolchain != "auto" && toolchain != editor::product::kCompilerC &&
        toolchain != "msvc" && toolchain != "cl" && toolchain != editor::product::kCompilerShalimar &&
        toolchain != editor::product::kCompilerCpp && toolchain != "c++" && toolchain != "cxx" &&
        toolchain != "g++" && toolchain != "clang++") {
        std::fprintf(stderr, "%s: unknown toolchain %s\n", me.c_str(), toolchain.c_str());
        return 2;
    }

    editor::installPlatformDemangler();

    editor::Editor ed;
    // The installation's indentation, before the command line's say.
    ed.setIndentWidth(editor::settings::indentWidth());
    if (editor::settings::indentTabs()) ed.setTabs(true);

    if (!file.empty() && project.empty() && editor::path::isDirectory(file)) {
        project = file;
        file.clear();
    }
    // A .pro named on the line is the project, for --build and --run.
    if (build && !file.empty() && project.empty() && file.size() > 4 &&
        file.compare(file.size() - 4, 4, ".pro") == 0) {
        project = file;
        file.clear();
    }
    if (build) ed.setBatch(true);
    if (!assembler.empty()) editor::settings::overrideAssembler(assembler);
    if (!linker.empty()) editor::settings::overrideLinker(linker);
    if (!ti.empty()) editor::settings::overrideTi(ti);
    if (!tilib.empty()) editor::settings::overrideTilib(tilib);
    if (!tilinker.empty()) editor::settings::overrideTilinker(tilinker);
    if (ccs) editor::settings::overrideCcs(true, ccsRoot);

    bool onItsOwn = false;
    if (project.empty() && !file.empty()) {
        size_t at = file.find_last_of("/\\");
        std::string beside = (at == std::string::npos) ? std::string(".") : file.substr(0, at);

        beside = editor::path::absolute(beside);

        std::string up = editor::path::parent(beside);
        if (!editor::Project::fileIn(beside).empty()) project = beside;
        else if (!up.empty() && !editor::Project::fileIn(up).empty()) project = up;
        else onItsOwn = true;
    }

    // The installation's settings.json, written once with its two directories.
    editor::settings::writeInstallFileIfAbsent();
    // Nothing named opens nothing: the last project is remembered under Project > Recent, never opened on its own.
    (void)onItsOwn;

    if (!project.empty()) ed.openProject(project);

    // The command line's compiler, else the installation's default from
    // settings.json; a project opened above may have set its own already.
    if (!toolchain.empty()) ed.setToolchain(editor::toolchainFrom(toolchain));
    else if (project.empty()) ed.setToolchain(editor::toolchainFrom(editor::settings::defaultCompiler()));

    if (config == "release") ed.setConfig(editor::ConfigRelease);
    else if (config == "debug") ed.setConfig(editor::ConfigDebug);
    else if (!config.empty()) {
        std::fprintf(stderr, "%s: unknown configuration %s\n", me.c_str(), config.c_str());
        return 2;
    }
    else if (editor::settings::configuration() == "release")
        ed.setConfig(editor::ConfigRelease);

    if (width > 0) ed.setIndentWidth(static_cast<size_t>(width));

    if (plain || editor::settings::plainFrame()) ed.setPlainFrame(true);
    if (tabs >= 0) ed.setTabs(true);
    if (caseIndent >= 0) ed.setCaseIndent(1);

    if (!cc1.empty()) ed.setCc1(cc1);
    if (!cl.empty()) ed.setCl(cl);
    if (!shc.empty()) ed.setShc(shc);
    if (!cxx1.empty()) ed.setCxx1(cxx1);
    if (!c2s.empty()) ed.setConverter(c2s);

    if (cxx.empty()) {
        const char* fromEnv = std::getenv("CXX");
        if (fromEnv && *fromEnv) cxx = fromEnv;
    }
    if (!cxx.empty()) ed.setCxx(cxx);

    if (build) {
        if (project.empty()) { std::fprintf(stderr, "%s: --build needs a project\n", me.c_str()); return 2; }
        if (!arch.empty() && !ed.setArchNamed(arch)) {
            std::fprintf(stderr, "%s: unknown target %s\n", me.c_str(), arch.c_str());
            return 2;
        }
        // A project that did not open - a CCS workspace, say, which is a folder of projects - is said, not built.
        if (!ed.projectLoaded()) {
            std::fprintf(stderr, "%s: %s\n", me.c_str(), ed.lastMessage().empty() ? "no project there" : ed.lastMessage().c_str());
            return 2;
        }
        ed.buildProjectBatch(runIt, runner);
        const std::vector<std::string>& lines = ed.consoleLines();
        for (size_t i = 0; i < lines.size(); ++i) std::printf("%s\n", lines[i].c_str());
        if (!ed.lastBuildOk()) return 1;
        return runIt ? (ed.lastRunStatus() == 0 ? 0 : 3) : 0;
    }

    if (!file.empty()) ed.open(file);
    else ed.openFirstFile();
    ed.run();
    return 0;
}
