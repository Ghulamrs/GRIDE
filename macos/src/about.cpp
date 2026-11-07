#include "about.h"
#include "path.h"
#include "product.h"
#include "settings.h"
#include "toolchain.h"

#include <cstdio>
#include <algorithm>
#include <ctime>
#include <utility>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32)
#define POPEN  _popen
#define PCLOSE _pclose
#else
#define POPEN  popen
#define PCLOSE pclose
#endif

namespace editor {
namespace about {

const char* name() { return product::kName; }

const char* version() { return "5.17"; }

namespace {

// **The compilers are asked rather than listed.** Their numbers written here would be a second
// copy, and the stale one: the editor does not build them. What the box answers is "what is it
// driving now", and absence is an ordinary state, so a missing compiler is a row that says so.
std::string askVersion(const std::string& program) {
    const std::string found = path::besideProgram(program);
    if (found.empty()) return std::string();

    // Quoted, because a path may hold a space. `--version` is the flag all four answer: cc1, shc
    // and c2s write one line and stop, cxx1 writes its banner first and its version second, so
    // the line kept is the last one naming a version - and the first when none does.
    const std::string command = "\"" + found + "\" --version 2>&1";
    FILE* pipe = POPEN(command.c_str(), "r");
    if (!pipe) return std::string();

    // The first line is the banner, which is what About shows: every one of the four opens with
    // one, and cxx1's version line beneath is detail. Its own copyright is taken off, since the box
    // ends with it once - so a row reads "cpp11 - ISO C++ 11" beside "c90 - ISO C 90".
    char buffer[256];
    std::string first;
    while (std::fgets(buffer, sizeof buffer, pipe)) {
        std::string line = buffer;
        while (!line.empty() && (line[line.size() - 1] == '\n' || line[line.size() - 1] == '\r'))
            line.resize(line.size() - 1);
        if (first.empty() && !line.empty()) first = line;
    }
    PCLOSE(pipe);

    const std::string owner = "\xC2\xA9""2026 G. R. Akhtar - ";
    if (first.compare(0, owner.size(), owner) == 0) first.erase(0, owner.size());
    return first;
}

// Named by the row when the answer does not name itself: cxx1's version line
// is "Version 1.2, sealed ..." with no cxx1 in it, and a row that could be
// any of the four is a row that says nothing.
std::string cell(const std::string& program) {
    const std::string answer = askVersion(program);
    std::string stem = program;
    if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".exe") == 0) stem.resize(stem.size() - 4);
    if (answer.empty()) return stem + " - not beside this program";
    return stem + " - " + answer;
}

// **One per line, since 3.0.** Three answers shared a row while the box was seven lines and a
// fourth row would have left the panel; Editor::fitPanelTo grows the panel to what About puts in
// it now. cxx1's --version banner is longer than a column, and a row that wraps reads worse.
void tool(std::vector<std::string>& said, const std::string& program) {
    said.push_back("  " + cell(program));
}

}

namespace {

// One row: what, the file it resolved to, and where that answer came from.
void row(std::vector<std::string>& said, const std::string& what, const std::string& file, const std::string& from) {
    std::string left = "  " + what;
    if (left.size() < 22) left.resize(22, ' ');
    said.push_back(left + (file.empty() ? std::string("-") : file) + (from.empty() ? std::string() : "   (" + from + ")"));
}

// A tool beside RIDE, the way the build finds it: beside this program first, then PATH.
void besideOrPath(std::vector<std::string>& said, const std::string& what, const std::string& program) {
    std::string found = path::besideProgram(program);
    if (!found.empty()) { row(said, what, found, "beside RIDE"); return; }
    found = path::onPath(program);
    row(said, what, found, found.empty() ? "not found" : "on PATH");
}

// A tool settings.json may name: named, else what the build uses in its place.
void named(std::vector<std::string>& said, const std::string& what, const std::string& file,
           const std::string& otherwise) {
    if (!file.empty()) row(said, what, file, "settings.json");
    else row(said, what, otherwise, "settings.json names none");
}

}

std::vector<std::string> environment() {
    std::vector<std::string> said;
    said.push_back(std::string(name()) + " " + version() + " - the environment in force");
    const std::string install = settings::installFile();
    row(said, "settings.json", install, path::exists(install) ? "read" : "absent - every answer below is a default");
    said.push_back("");
    said.push_back("Compilers and tools");
    besideOrPath(said, "c90", "c90.exe");
    besideOrPath(said, "cpp11", "cpp11.exe");
    besideOrPath(said, "shalimar", "shalimar.exe");
    besideOrPath(said, "c2s", "c2s.exe");
    besideOrPath(said, "vm6747", "vm6747.exe");
    besideOrPath(said, "asm6x", "asm6x.exe");
    said.push_back("");
    said.push_back("x86_64-windows");
    named(said, "assembler", settings::assembler(), "cpp11's own choice: masm.exe beside it, else clang");
    named(said, "linker", settings::linker(), "Microsoft's link.exe");
#ifdef _WIN32
    const std::string vs = settings::vcvars();
    if (!vs.empty()) row(said, "Visual Studio", vs, "settings.json");
    else {
        const std::string found = visualStudioVcvars();
        row(said, "Visual Studio", found, found.empty() ? "none found - cl, ml64, link.exe and the Windows libraries need it"
                                                        : "found by vswhere");
    }
#else
    row(said, "Visual Studio", "", "not on this machine - Windows programs link on Windows");
#endif
    said.push_back("");
    said.push_back("tms6747");
    const std::string namedTi = settings::namedTi(), rts6x = rts6xRuntimeDir();
    // RTS6x links every .out while no TI compiler is named; naming one under Tools puts TI's runtime back.
    if (namedTi.empty() && !rts6x.empty())
        row(said, "runtime", rts6x, "RTS6x, " + std::string(product::kName) + "'s own - nothing of TI's on the link line");
    if (!namedTi.empty()) row(said, "TI compiler", namedTi, "settings.json - its runtime links in place of RTS6x");
    else {
        const std::string found = settings::detectedTi();
        row(said, "TI compiler", found, !rts6x.empty() ? (found.empty() ? "none found - not needed, RTS6x links the .out"
                                                                         : "detected, not used - Tools names it to link with TI's runtime")
                                        : found.empty() ? "none found - a .out needs CCS's ti-cgt-c6000; Tools names it"
                                                        : "detected - CCS's newest");
    }
    const std::string ti = namedTi.empty() && !rts6x.empty() ? std::string() : settings::ti();
    named(said, "C6000 linker", settings::tilinker(), ti.empty() ? std::string() : path::join(path::join(ti, "bin"), "lnk6x"));
    const std::string tilib = settings::tilib();
    if (ti.empty() && !rts6x.empty()) {
        if (!tilib.empty()) row(said, "tilib", tilib, "settings.json - read only with a TI compiler named");
    } else {
        const bool eh = (!ti.empty() && path::exists(path::join(path::join(ti, "lib"), "rts6740_elf_eh.lib"))) ||
                        (!tilib.empty() && path::exists(path::join(tilib, "rts6740_elf_eh.lib")));
        row(said, "runtime", ti.empty() ? std::string() : path::join(ti, "lib"),
            eh ? "with rts6740_elf_eh.lib, which C++ programs need" : "without rts6740_elf_eh.lib - tilib names a directory holding it");
        if (!tilib.empty()) row(said, "tilib", tilib, "settings.json");
    }
    said.push_back("");
    said.push_back("Headers and libraries");
    row(said, "cpp11 headers", settings::includeDir(), "settings.json \"include\"");
    row(said, "c90 headers", settings::libDir(), "settings.json \"lib\"");
    const std::vector<std::string> inc = settings::includes();
    for (size_t i = 0; i < inc.size(); ++i) row(said, "extra headers", inc[i], "settings.json \"includes\"");
    const std::vector<std::string> libs = settings::libraries();
    for (size_t i = 0; i < libs.size(); ++i) row(said, "extra library", libs[i], "settings.json \"libraries\"");
    if (inc.empty() && libs.empty()) row(said, "extra", "", "none - a project adds its own in its .pro");
    return said;
}

namespace {

// CRC-32 as zip and PNG take it (reflected, polynomial EDB88320), over the file's bytes as they are on disk.
bool crc32Of(const std::string& file, unsigned long& crc) {
    FILE* f = std::fopen(file.c_str(), "rb");
    if (!f) return false;
    unsigned long table[256];
    for (unsigned long n = 0; n < 256; ++n) {
        unsigned long c = n;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
        table[n] = c;
    }
    unsigned long c = 0xFFFFFFFFUL;
    unsigned char buffer[65536];
    size_t got;
    while ((got = std::fread(buffer, 1, sizeof buffer, f)) > 0)
        for (size_t i = 0; i < got; ++i) c = table[(c ^ buffer[i]) & 0xFF] ^ (c >> 8);
    std::fclose(f);
    crc = (c ^ 0xFFFFFFFFUL) & 0xFFFFFFFFUL;
    return true;
}

// The box's last line: this program and its version, when its file was last written in Pakistan time (UTC+5, no
// daylight saving, so it is UTC plus five hours whatever the machine's own zone), and its CRC-32.
std::string recordedFor(const std::string& leaf, unsigned long& crc, long long& size);

std::string selfLine() {
    const std::string file = path::programFile();
    if (file.empty()) return std::string();
#ifdef _WIN32
    struct _stat64 st;
    if (_stat64(file.c_str(), &st) != 0) return std::string(name()) + " " + version();
#else
    struct stat st;
    if (stat(file.c_str(), &st) != 0) return std::string(name()) + " " + version();
#endif
    std::time_t pkt = static_cast<std::time_t>(st.st_mtime) + 5 * 3600;
    std::tm when;
#ifdef _WIN32
    gmtime_s(&when, &pkt);
#else
    gmtime_r(&pkt, &when);
#endif
    char stamp[64];
    std::strftime(stamp, sizeof stamp, "%d-%m-%Y %H:%M:%S PKT", &when);
    // Named by the release, "RIDE 5.17", the same in every window and in the console.
    std::string said = std::string(name()) + " " + version() + "  " + stamp;
    unsigned long crc = 0;
    if (crc32Of(file, crc)) {
        char hex[16];
        std::snprintf(hex, sizeof hex, "%08lX", crc);
        // On a line of its own: the release and the time, then the CRC and what the record says of it.
        said += "\nCRC32 " + std::string(hex);
        // Against the record the installer wrote beside it: the same bytes, or changed since.
        unsigned long was = 0;
        long long wasSize = 0;
        std::string leaf = file;
        const size_t slash = leaf.find_last_of('/');
        if (slash != std::string::npos) leaf.erase(0, slash + 1);
        const std::string found = recordedFor(leaf, was, wasSize);
        if (found.empty()) said += "  - no release record";
        else if (was == crc && wasSize == static_cast<long long>(st.st_size)) said += "  - matches the release";
        else {
            char old[16];
            std::snprintf(old, sizeof old, "%08lX", was);
            said += "  - CHANGED, released " + std::string(old);
        }
    }
    return said;
}

const char* kRecord = "release.crc";

// The entry for `leaf` in release.crc beside this program - or, for the macOS window, whose bundle a file
// added after signing would break, in the console's bin - its CRC and size, and the line, or "".
std::string recordedFor(const std::string& leaf, unsigned long& crc, long long& size) {
    FILE* f = std::fopen(path::join(path::programDirectory(), kRecord).c_str(), "r");
#ifdef __APPLE__
    if (!f) f = std::fopen((std::string("/usr/local/ride-") + version() + "/bin/" + kRecord).c_str(), "r");
#endif
    if (!f) return std::string();
    char line[1024];
    std::string found;
    while (std::fgets(line, sizeof line, f)) {
        unsigned long c = 0;
        long long n = 0;
        char name[768];
        if (std::sscanf(line, "%lx %lld %767[^\r\n]", &c, &n, name) == 3 && leaf == name) {
            crc = c;
            size = n;
            found = line;
            break;
        }
    }
    std::fclose(f);
    return found;
}

}

int writeReleaseRecord(const std::string& directory, const std::vector<std::string>& more) {
    bool ok = false;
    std::vector<path::Entry> listed = path::entries(directory, &ok);
    if (!ok) return -1;
    // The directory's own programs first, then any further directory's under names not yet listed, in name order.
    std::vector<std::pair<std::string, std::string> > all;
    for (size_t i = 0; i < listed.size(); ++i)
        if (!listed[i].directory && listed[i].name != kRecord) all.push_back(std::make_pair(listed[i].name, directory));
    for (size_t d = 0; d < more.size(); ++d) {
        std::vector<path::Entry> extra = path::entries(more[d], &ok);
        for (size_t i = 0; ok && i < extra.size(); ++i) {
            bool seen = extra[i].directory || extra[i].name == kRecord;
            for (size_t k = 0; !seen && k < all.size(); ++k) seen = all[k].first == extra[i].name;
            if (!seen) all.push_back(std::make_pair(extra[i].name, more[d]));
        }
    }
    std::sort(all.begin(), all.end());
    const std::string target = path::join(directory, kRecord);
    FILE* out = std::fopen(target.c_str(), "w");
    if (!out) return -1;
    std::fprintf(out, "# %s %s - the programs as released: CRC-32, size, name. About compares its own with this.\n",
                 name(), version());
    int wrote = 0;
    for (size_t i = 0; i < all.size(); ++i) {
        const std::string file = path::join(all[i].second, all[i].first);
        unsigned long crc = 0;
        if (!crc32Of(file, crc)) continue;
#ifdef _WIN32
        struct _stat64 st;
        if (_stat64(file.c_str(), &st) != 0) continue;
#else
        struct stat st;
        if (stat(file.c_str(), &st) != 0) continue;
#endif
        if ((st.st_mode & S_IFMT) != S_IFREG) continue;
        std::fprintf(out, "%08lX %lld %s\n", crc, static_cast<long long>(st.st_size), all[i].first.c_str());
        ++wrote;
    }
    std::fclose(out);
    return wrote;
}

std::string crc32Text(const std::string& file) {
    unsigned long crc = 0;
    if (!crc32Of(file, crc)) return std::string();
    char hex[16];
    std::snprintf(hex, sizeof hex, "%08lX", crc);
    return hex;
}

std::vector<std::string> stampLines() {
    std::vector<std::string> said;
    const std::string self = selfLine();
    if (self.empty()) return said;
    const size_t cut = self.find('\n');
    said.push_back(self.substr(0, cut));
    if (cut != std::string::npos) said.push_back(self.substr(cut + 1));
    return said;
}

std::vector<std::string> lines() {
    std::vector<std::string> said;
    said.push_back(std::string(name()) + " " + version());
    // Asked one at a time; the heading is the user's wording, and the list says which compilers are actually here.
    said.push_back("Compiler's version list as follows:");
    // The VM6747 line since 3.5 - what the editor actually looks for, so
    // that a copy standing beside the sealed originals says so. Shalimar
    // first, the converter fourth: the user's order.
    tool(said, "shalimar.exe");
    tool(said, "c90.exe");
    tool(said, "cpp11.exe");
    tool(said, "c2s.exe");
    said.push_back("");
    // The sign docked to the year, the word left out: the user's wording.
    said.push_back("\xC2\xA9""2026 G. R. Akhtar");
    said.push_back("Islamabad, Pakistan");
    const std::vector<std::string> stamp = stampLines();
    if (!stamp.empty()) said.push_back("");
    said.insert(said.end(), stamp.begin(), stamp.end());
    return said;
}

}
}
