#include "debugger.h"

#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#include "path.h"
#include "product.h"

namespace editor {

namespace {

// "<<ride-done>>": what each debugger is asked to say once a command has finished. cdb and lldb
// are asked in pieces, so that the command echoed back is not itself the marker.
// **Functions, not std::string globals**: in the mixed-mode window a native global with a destructor corrupted the heap before main - settings.cpp and the README say how.
std::string markerStart() { return std::string("<<") + product::kLower; }
std::string marker() { return markerStart() + "-done>>"; }

std::string markerCommand(DebuggerKind kind) {
    if (dbg_gdbLike(kind)) return "echo " + marker() + "\\n";
    if (kind == DebuggerCdb) {

        return ".printf \"" + markerStart() + "%cdone>>\\n\", 0x2d";
    }
    return "script print(\"" + markerStart() + "\" + \"-done>>\")";
}

void sayMarker(Process& child, DebuggerKind kind) {
    child.say(markerCommand(kind));
}

bool onPath(const std::string& name) {
#ifdef _WIN32
    (void)name;
    return false;
#else
    const char* where = std::getenv("PATH");
    if (!where) return false;
    std::string all = where;
    size_t from = 0;
    while (from <= all.size()) {
        size_t end = all.find(':', from);
        std::string dir = all.substr(from, end == std::string::npos ? std::string::npos : end - from);
        if (!dir.empty() && path::exists(path::join(dir, name))) return true;
        if (end == std::string::npos) break;
        from = end + 1;
    }
    return false;
#endif
}

std::vector<std::string> preamble(DebuggerKind kind) {
    std::vector<std::string> said;
    if (dbg_gdbLike(kind)) {
        said.push_back("set confirm off");

        said.push_back("set pagination off");

        said.push_back("set width unlimited");
        said.push_back("set breakpoint pending on");

        if (kind == DebuggerGdb && onPath("stdbuf")) said.push_back("set exec-wrapper stdbuf -o0 -e0");
    } else if (kind == DebuggerCdb) {

        said.push_back(".lines -e");
        said.push_back("l+t");
        // Source mode as well (M10): with l+t alone `t` stopped on a function's first byte, before its
        // parameters were stored - for cl's programs too - where l+s goes on to the first statement.
        said.push_back("l+s");
        said.push_back("n 10");
        // A step goes over Shalimar's runtime as it goes over the CRT: shc's code calls shm_ helpers for
        // its arithmetic, and `t` would stop in one where lldb and gdb, seeing no line table, step over it.
        said.push_back(".step_filter \"*!shm_*;*!shm::*\"");
    } else {

        said.push_back("script lldb.debugger.SetAsync(False)");
        said.push_back("settings set auto-confirm true");

        said.push_back("settings set target.input-path /dev/null");
    }
    return said;
}

std::string quoted(const std::string& text) { return "\"" + text + "\""; }

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::string line;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            if (!line.empty() && line[line.size() - 1] == '\r') line.resize(line.size() - 1);
            out.push_back(line);
            line.clear();
            continue;
        }
        line += text[i];
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

std::string withoutPrompt(const std::string& line) {
    std::string out = line;
    for (;;) {
        size_t at = 0;
        if (out.compare(0, 6, "(gdb) ") == 0) {
            at = 6;
        } else if (out.compare(0, 7, "(lldb) ") == 0) {
            at = 7;
        } else {

            size_t i = 0;
            while (i < out.size() && out[i] >= '0' && out[i] <= '9') ++i;
            if (i == 0 || i >= out.size() || out[i] != ':') break;
            size_t j = i + 1;
            while (j < out.size() && std::isxdigit(static_cast<unsigned char>(out[j]))) ++j;

            if (j == i + 1 || out.compare(j, 1, ">") != 0) break;
            at = j + 1;
            if (at < out.size() && out[at] == ' ') ++at;
        }
        out = out.substr(at);
    }
    return out;
}

std::string trimmed(const std::string& text) {
    size_t from = text.find_first_not_of(" \t");
    if (from == std::string::npos) return std::string();
    size_t to = text.find_last_not_of(" \t");
    return text.substr(from, to - from + 1);
}

bool digits(const std::string& text) {
    if (text.empty()) return false;
    for (size_t i = 0; i < text.size(); ++i)
        if (text[i] < '0' || text[i] > '9') return false;
    return true;
}

size_t number(const std::string& text) {
    return static_cast<size_t>(std::strtoul(text.c_str(), 0, 10));
}

bool startsWith(const std::string& text, const char* prefix) {
    const size_t n = std::strlen(prefix);
    return text.size() >= n && text.compare(0, n, prefix) == 0;
}

bool endsWith(const std::string& text, char c) {
    return !text.empty() && text[text.size() - 1] == c;
}

DebuggerKind promptOn(const std::string& line) {
    if (startsWith(line, "(gdb)")) return DebuggerGdb;
    if (startsWith(line, "(lldb)")) return DebuggerLldb;
    size_t i = 0;
    while (i < line.size() && line[i] >= '0' && line[i] <= '9') ++i;
    if (i == 0 || i >= line.size() || line[i] != ':') return DebuggerNone;
    size_t j = i + 1;
    while (j < line.size() && std::isxdigit(static_cast<unsigned char>(line[j]))) ++j;
    if (j == i + 1 || line.compare(j, 1, ">") != 0) return DebuggerNone;
    return DebuggerCdb;
}

bool sourceEcho(const std::string& line) {
    std::string rest = trimmed(line);
    if (startsWith(rest, "-> ")) rest = trimmed(rest.substr(3));
    size_t i = 0;
    while (i < rest.size() && rest[i] >= '0' && rest[i] <= '9') ++i;
    if (i == 0) return false;
    while (i < rest.size() && rest[i] == ' ') ++i;
    return i < rest.size() && rest[i] == '\t';
}

bool lldbOwn(const std::string& line) {
    const std::string bare = trimmed(line);
    if (startsWith(bare, "* thread #")) return true;
    if (startsWith(bare, "frame #")) return true;
    if (bare == "^") return true;
    if (startsWith(line, "Process ") || startsWith(line, "Target ")) return true;
    return false;
}

bool gdbOwn(const std::string& line) {
    if (startsWith(line, "Starting program:")) return true;
    if (startsWith(line, "Continuing.")) return true;
    if (startsWith(line, "Breakpoint ")) return true;
    if (startsWith(line, "[")) return true;
    if (startsWith(line, "Using host ")) return true;
    if (startsWith(line, "Reading symbols")) return true;

    if (startsWith(line, "Missing ") &&
        (line.find("debuginfo") != std::string::npos || line.find("try:") != std::string::npos))
        return true;
    return false;
}

bool ourCommand(const std::string& bare) {
    static const char* const said[] = {
        "g", "p", "t", "gu", "k", "q", "ln", "dv", "l+t", "l+s", "n 10",
        ".lines -e", ".lastevent", ".echo", ".printf"
    };
    for (size_t i = 0; i < sizeof said / sizeof said[0]; ++i)
        if (bare == said[i]) return true;
    return startsWith(bare, "bp ") || startsWith(bare, "bu ") ||
           startsWith(bare, "bc ") || startsWith(bare, ".printf ") || startsWith(bare, ".step_filter ");
}

bool cdbOwn(const std::string& line) {
    const std::string bare = trimmed(line);
    if (ourCommand(bare)) return true;
    if (startsWith(bare, "Breakpoint ")) return true;
    if (startsWith(bare, "Last event:")) return true;
    if (startsWith(bare, "debugger time:")) return true;
    if (startsWith(bare, "ModLoad:")) return true;

    if (bare.find('!') != std::string::npos && endsWith(bare, ':')) return true;

    if (startsWith(bare, "(") && bare.find('!') != std::string::npos) return true;

    if (bare.find('`') != std::string::npos &&
        std::isxdigit(static_cast<unsigned char>(bare[0]))) return true;

    if (endsWith(bare, ')') &&
        (bare.find(".c(") != std::string::npos || bare.find(".cpp(") != std::string::npos ||
         bare.find(".h(") != std::string::npos)) return true;
    return false;
}

}

bool dbg_stoppedWithNoSource(const std::string& said) {
    return said.find("stop reason") != std::string::npos ||
           said.find("frame #0") != std::string::npos ||
           said.find("#0  0x") != std::string::npos;
}

std::string dbg_programOutput(DebuggerKind kind, const std::string& said) {
    const std::vector<std::string> all = lines(said);
    std::string out;

    for (size_t i = 0; i < all.size(); ++i) {
        std::string line = all[i];

        const DebuggerKind prompt = promptOn(line);
        if (prompt == DebuggerLldb) continue;
        if (prompt != DebuggerNone) line = withoutPrompt(line);

        if (trimmed(line).empty()) continue;
        if (line.find(markerStart()) != std::string::npos) continue;
        if (sourceEcho(line)) continue;

        if (kind == DebuggerLldb && lldbOwn(line)) continue;
        if (dbg_gdbLike(kind) && gdbOwn(line)) continue;
        if (kind == DebuggerCdb && cdbOwn(line)) continue;

        out += line;
        out += "\n";
    }
    return out;
}

std::string dbg_withoutEscapes(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\x1b') { out += text[i]; continue; }
        if (i + 1 >= text.size()) break;
        const char kind = text[i + 1];
        if (kind == '[') {
            size_t at = i + 2;
            while (at < text.size() && !(text[at] >= '@' && text[at] <= '~')) ++at;
            i = at;
        } else if (kind == ']') {
            size_t at = i + 2;
            while (at < text.size() && text[at] != '\x07' &&
                   !(text[at] == '\x1b' && at + 1 < text.size() && text[at + 1] == '\\')) ++at;
            if (at < text.size() && text[at] == '\x1b') ++at;
            i = at;
        } else {
            ++i;
        }
    }
    return out;
}

std::string dbg_withoutEcho(const std::string& said, const std::string& asked,
                            const std::string& marker) {
    std::vector<std::string> all = lines(said);
    bool droppedAsked = false, droppedMarker = false;
    std::string out;
    for (size_t i = 0; i < all.size(); ++i) {
        const std::string bare = trimmed(all[i]);
        if (!droppedAsked && bare == trimmed(asked)) { droppedAsked = true; continue; }
        if (!droppedMarker && bare == trimmed(marker)) { droppedMarker = true; continue; }
        out += all[i];
        out += "\n";
    }
    return out;
}

DebuggerKind dbg_here() {
#if defined(_WIN32)
    return DebuggerNone;
#elif defined(__APPLE__)
    return DebuggerLldb;
#else
    return DebuggerGdb;
#endif
}

const char* dbg_name(DebuggerKind kind) {
    switch (kind) {
        case DebuggerLldb: return "lldb";
        case DebuggerGdb:  return "gdb";
        case DebuggerCdb:  return "cdb";
        case DebuggerC6x:  return "vm6747";
        default:           return "none";
    }
}

bool dbg_gdbLike(DebuggerKind kind) { return kind == DebuggerGdb || kind == DebuggerC6x; }

namespace {
unsigned long u32At(const std::string& d, size_t at) {
    if (at + 4 > d.size()) return 0;
    return (unsigned char)d[at] | ((unsigned char)d[at + 1] << 8) | ((unsigned char)d[at + 2] << 16) |
           ((unsigned long)(unsigned char)d[at + 3] << 24);
}
unsigned u16At(const std::string& d, size_t at) {
    if (at + 2 > d.size()) return 0;
    return (unsigned char)d[at] | ((unsigned char)d[at + 1] << 8);
}
bool envSet(const char* name) {
    const char* v = std::getenv(name);
    return v && *v && *v != '0';
}
bool modifiedAt(const std::string& path, long long* when) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    *when = (long long)st.st_mtime;
    return true;
}
}

// A 32-bit little-endian ELF read for two things: the executable sections' ranges and the .debug_line's
// units, each unit's program read to its first DW_LNE_set_address (what LNK6x and lnk6x open a sequence
// with). The runtime's own units, had the image any, would not place a row in the program's code.
bool dbg_imageHasLines(const std::string& out, std::string* why) {
    if (why) why->clear();
    std::FILE* f = std::fopen(out.c_str(), "rb");
    if (!f) { if (why) *why = "the image is not there"; return false; }
    std::string d;
    char buf[65536];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) d.append(buf, n);
    std::fclose(f);
    if (d.size() < 52 || d.compare(0, 4, "\x7f" "ELF") != 0 || (unsigned char)d[4] != 1) { if (why) *why = "the image is not a 32-bit ELF"; return false; }
    size_t shoff = u32At(d, 32); unsigned shentsize = u16At(d, 46), shnum = u16At(d, 48), shstrndx = u16At(d, 50);
    if (shentsize < 40 || shoff + (size_t)shnum * shentsize > d.size() || shstrndx >= shnum) { if (why) *why = "the image's section table is not readable"; return false; }
    size_t strOff = u32At(d, shoff + (size_t)shstrndx * shentsize + 16);
    std::vector<std::pair<unsigned long, unsigned long> > code;
    size_t lineOff = 0, lineSize = 0;
    for (unsigned i = 0; i < shnum; i++) {
        size_t h = shoff + (size_t)i * shentsize;
        size_t name = strOff + u32At(d, h);
        std::string n = name < d.size() ? std::string(d.c_str() + name) : std::string();
        unsigned long flags = u32At(d, h + 8), addr = u32At(d, h + 12), off = u32At(d, h + 16), size = u32At(d, h + 20);
        if (flags & 4) code.push_back(std::make_pair(addr, addr + size));
        if (n == ".debug_line" && size) { lineOff = off; lineSize = size; }
    }
    if (!lineSize) { if (why) *why = "the image carries no .debug_line"; return false; }
    if (lineOff + lineSize > d.size()) { if (why) *why = "the image's .debug_line runs past its end"; return false; }
    for (size_t at = lineOff; at + 10 <= lineOff + lineSize;) {
        unsigned long unitLen = u32At(d, at);
        if (unitLen == 0xffffffffUL || unitLen < 6) break;
        size_t unitEnd = at + 4 + unitLen;
        size_t prog = at + 10 + u32At(d, at + 6);
        // the program's first opcode: an extended set_address is 00 <len> 02 <address>
        if (prog + 7 <= unitEnd && prog + 7 <= d.size() && d[prog] == 0 && (unsigned char)d[prog + 2] == 2) {
            unsigned long a = u32At(d, prog + 3);
            for (size_t k = 0; k < code.size(); k++)
                if (a >= code[k].first && a < code[k].second) return true;
        }
        if (unitEnd <= at) break;
        at = unitEnd;
    }
    if (why) *why = "the image's .debug_line has no row in the program's code";
    return false;
}

std::string dbg_c6xBackend(const std::string& executable, std::string* why) {
    if (why) why->clear();
    const std::string out = tiProgramOf(executable), sim = simulatorProgram();
    if (envSet("RIDE_DEBUG_VM6747")) { if (why) *why = "RIDE_DEBUG_VM6747 asks for the emulator"; return "vm6747"; }
    if (envSet("RIDE_DEBUG_SIM6747")) {
        if (!sim.empty() && path::exists(out)) return "sim6747";
        if (why) *why = sim.empty() ? "RIDE_DEBUG_SIM6747 asks for sim6747, and none is beside " + std::string(product::kName)
                                    : "RIDE_DEBUG_SIM6747 asks for sim6747, and the build left no " + path::filename(out);
        return "vm6747";
    }
    if (sim.empty()) { if (why) *why = "no sim6747 beside " + std::string(product::kName); return "vm6747"; }
    if (!path::exists(out)) { if (why) *why = "the build left no " + path::filename(out) + " (asm6x, lnk6x and RTS6x make one)"; return "vm6747"; }
    long long builtOut = 0, builtAsm = 0;
    if (modifiedAt(out, &builtOut) && modifiedAt(executable, &builtAsm) && builtOut < builtAsm) {
        if (why) *why = path::filename(out) + " is older than the assembly: not this build's";
        return "vm6747";
    }
    std::string lines;
    if (!dbg_imageHasLines(out, &lines)) { if (why) *why = lines; return "vm6747"; }
    return "sim6747";
}

const char* dbg_program(DebuggerKind kind) {
    if (kind == DebuggerC6x) {
        // The emulator beside RIDE or named by $VM6747 (toolchain.h); sim6747 is chosen at start() for a .out.
        static std::string* emulator = 0;
        if (!emulator) emulator = new std::string(emulatorProgram());
        return emulator->c_str();
    }
    if (kind != DebuggerCdb) return dbg_name(kind);

    static std::string* found = 0;
    if (found) return found->c_str();

    // RIDE_CDB names a cdb.exe outside the Windows Kits, for a machine whose SDK left the debuggers out.
    const char* named = std::getenv("RIDE_CDB");
    if (named && *named && path::exists(named)) {
        found = new std::string(named);
        return found->c_str();
    }

    const char* under[2] = {"ProgramFiles(x86)", "ProgramFiles"};
    for (size_t i = 0; i < 2; ++i) {
        const char* root = std::getenv(under[i]);
        if (!root) continue;
        std::string where = std::string(root) + "\\Windows Kits\\10\\Debuggers\\x64\\cdb.exe";
        if (path::exists(where)) {
            found = new std::string(where);
            return found->c_str();
        }
    }
    found = new std::string("cdb");
    return found->c_str();
}

// **A Shalimar program on the emulator needs its runtime beside it** - the directory of C6000 text
// cpp11 wrote from runtime/, as a run on vm6747 is given it (toolchain.cpp). The assembly says what
// it is: shc opens it with `; <source>.shl` (or .shm), and nothing else does.
std::string dbg_shalimarRuntime(const std::string& assembly) {
    // <cstdio> and not <fstream>: the window compiles this file mixed-mode, and an iostream there
    // killed it before main with STATUS_HEAP_CORRUPTION (2026-10-13), as a native global does.
    std::FILE* in = std::fopen(assembly.c_str(), "rb");
    if (!in) return std::string();
    char head[512];
    const bool got = std::fgets(head, sizeof head, in) != 0;
    std::fclose(in);
    if (!got) return std::string();
    std::string first(head);
    while (!first.empty() && first[first.size() - 1] == '\n') first.erase(first.size() - 1);
    while (!first.empty() && (first[first.size() - 1] == '\r' || first[first.size() - 1] == ' ')) first.erase(first.size() - 1);
    if (first.size() < 6 || first.compare(0, 2, "; ") != 0) return std::string();
    const std::string ext = first.substr(first.size() - 4);
    if (ext != ".shl" && ext != ".shm") return std::string();
    const std::string runtime = shalimarRuntimeDir(ConfigDebug);
    return runtime.empty() ? std::string() : " " + quoted(runtime);
}

DebuggerKind dbg_for(ToolchainKind kind, const std::string& arch) {

    if (!emitsDebugInfo(kind, arch)) return DebuggerNone;

    // cl's PDB, and since M10 c90's and cpp11's on x86_64-windows: CodeView, which cdb reads.
    if (kind == ToolMsvc || arch == "x86_64-windows")
        return path::exists(dbg_program(DebuggerCdb)) ? DebuggerCdb : DebuggerNone;

    // tms6747: vm6747 --debug on the .s cpp11 -g wrote, with its .dwpsn positions (RIDE_DEBUG_SIM6747=1 asks
    // for sim6747 --run --debug on the .out beside it instead - which stops on a line only where the image
    // carries a .debug_line, which ASM6x and LNK6x do not write yet).
    if (isEmulated(arch)) {
        const std::string vm = dbg_program(DebuggerC6x);
        return path::exists(vm) || onPath(path::filename(vm)) ? DebuggerC6x : DebuggerNone;
    }

    return dbg_here();
}

bool dbg_stopsItself(ToolchainKind kind) { return kind == ToolShc; }

DebugPlan dbg_planFor(const Toolchain& tool, const std::vector<Part>& parts,
                      const std::string& arch) {
    DebugPlan plan;
    if (parts.empty()) return plan;

    plan.kind = toolchainOf(tool, parts[0]);
    for (size_t i = 0; i < parts.size(); ++i) {
        ToolchainKind each = toolchainOf(tool, parts[i]);
        DebuggerKind theirs = dbg_for(each, arch);
        if (theirs == DebuggerNone) {

            plan.blind.push_back(parts[i].group.empty() ? std::string(toolchainName(each))
                                                        : parts[i].group);
            continue;
        }
        if (plan.engine == DebuggerNone) { plan.engine = theirs; plan.kind = each; }
    }

    // **A Shalimar program stops itself only where no debugger can read it** - since D1 (2026-10-13)
    // lldb and gdb read the DWARF a Debug build of shalimar carries on the two GNU targets, and there
    // the program is driven as a c90 or cpp11 one is; the session remains for the other two targets.
    plan.stopsItself = dbg_stopsItself(toolchainOf(tool, parts[0])) && plan.engine == DebuggerNone;
    if (plan.stopsItself) {
        plan.kind = ToolShc;

        plan.blind.clear();
    }
    return plan;
}

std::string dbg_whyNot(ToolchainKind kind, const std::string& arch) {
    if (dbg_for(kind, arch) != DebuggerNone) return std::string();

    if (dbg_stopsItself(kind))
        return "a Shalimar program stops itself - it needs no debugger at all";
    if (isEmulated(arch) && emitsDebugInfo(kind, arch))
        return "vm6747 is not beside " + std::string(product::kName) + " and not on the PATH - the VM6747 emulator is the "
               "debugger for " + arch + " (vm6747 --debug); name one with $VM6747";
    if (isEmulated(arch))
        return std::string(toolchainName(kind)) + " writes no line table for " + arch + " - c90 -g and cpp11 -g write "
               "cl6x's .dwpsn, which vm6747 --debug and sim6747 stop on";
    if (kind == ToolMsvc || (emitsDebugInfo(kind, arch) && arch == "x86_64-windows"))
        return std::string(toolchainName(kind)) + " writes a .pdb and cdb reads one, but cdb is not installed - "
               "add Debugging Tools for Windows";
    if (!emitsDebugInfo(kind, arch))
        return std::string(toolchainName(kind)) + " generates MASM for " + arch +
               ", which carries no line table";
    return std::string("no ") + dbg_name(dbg_here()) + " on this machine";
}

namespace {

bool placeIn(const std::string& line, StackFrame* frame) {
    size_t at = line.rfind(" at ");
    if (at == std::string::npos) return false;

    std::string where = trimmed(line.substr(at + 4));
    size_t colon = where.find_last_of(':');
    if (colon == std::string::npos) return false;

    std::string tail = where.substr(colon + 1);
    std::string head = where.substr(0, colon);
    if (!digits(tail)) return false;

    size_t second = head.find_last_of(':');
    if (second != std::string::npos && digits(head.substr(second + 1))) {
        tail = head.substr(second + 1);
        head = head.substr(0, second);
    }

    frame->file = head;
    frame->line = number(tail);

    std::string front = line.substr(0, at);
    size_t tick = front.find_last_of('`');
    if (tick != std::string::npos) front = front.substr(tick + 1);

    size_t bracket = front.find('(');
    if (bracket != std::string::npos) front = front.substr(0, bracket);

    size_t comma = front.find_last_of(',');
    if (comma != std::string::npos) front = front.substr(comma + 1);

    size_t in = front.rfind(" in ");
    if (in != std::string::npos) front = front.substr(in + 4);

    frame->function = trimmed(front);
    return true;
}

}

Stop dbg_readCdbStop(const std::string& said) {
    Stop stop;
    stop.said = said;

    std::vector<std::string> all = lines(said);
    for (size_t i = 0; i < all.size(); ++i) {
        std::string line = trimmed(withoutPrompt(all[i]));

        size_t ended = line.find("Exit process");
        if (ended != std::string::npos) {
            stop.exited = true;
            stop.stopped = false;
            size_t code = line.find("code ", ended);
            if (code != std::string::npos)
                stop.status = static_cast<int>(std::strtol(line.c_str() + code + 5, 0, 16));
            continue;
        }
        if (line.find("No runnable debuggees") != std::string::npos) {
            stop.exited = true;
            continue;
        }
        if (stop.stopped || stop.exited) continue;

        size_t open = line.rfind('(');
        if (open == std::string::npos || open == 0) continue;
        size_t close = line.find(')', open);
        if (close == std::string::npos) continue;

        std::string number = line.substr(open + 1, close - open - 1);
        if (!digits(number)) continue;

        stop.file = line.substr(0, open);
        stop.line = editor::number(number);
        stop.stopped = true;

        for (size_t j = i + 1; j < all.size() && stop.function.empty(); ++j) {
            std::string under = withoutPrompt(all[j]);
            size_t bang = under.find('!');
            if (bang == std::string::npos) continue;
            std::string rest = under.substr(bang + 1);
            size_t end = rest.find_first_of("+ \t|(");
            stop.function = trimmed(end == std::string::npos ? rest : rest.substr(0, end));
        }
    }
    return stop;
}

Stop dbg_readStop(DebuggerKind kind, const std::string& said) {
    if (kind == DebuggerCdb) return dbg_readCdbStop(said);

    Stop stop;
    stop.said = said;

    std::vector<std::string> all = lines(said);
    for (size_t i = 0; i < all.size(); ++i) {
        std::string line = withoutPrompt(all[i]);

        if (line.find("exited with status") != std::string::npos ||
            line.find("exited with code") != std::string::npos ||
            line.find("exited normally") != std::string::npos) {
            stop.exited = true;
            stop.status = 0;

            size_t at = line.find("status = ");
            int base = 10;
            if (at != std::string::npos) {
                at += 9;
            } else {
                at = line.find("code ");
                if (at != std::string::npos) { at += 5; base = 8; }
            }
            if (at != std::string::npos)
                stop.status = static_cast<int>(std::strtol(line.c_str() + at, 0, base));
            continue;
        }

        if (line.compare(0, 18, "Run till exit from") == 0) continue;

        bool interesting = (kind == DebuggerLldb) ? line.find("frame #0:") != std::string::npos
                                                  : line.find(" at ") != std::string::npos;
        if (!interesting || stop.stopped) continue;

        StackFrame here;
        if (!placeIn(line, &here)) continue;

        stop.file = here.file;
        stop.line = here.line;
        stop.function = here.function;
        stop.stopped = true;
    }

    if (!stop.stopped && !stop.exited) {
        for (size_t i = 0; i < all.size(); ++i) {
            std::string line = withoutPrompt(all[i]);
            size_t tab = line.find('\t');
            if (tab == std::string::npos || tab == 0) continue;
            if (!digits(line.substr(0, tab))) continue;
            stop.line = number(line.substr(0, tab));
            stop.stopped = true;
            break;
        }
    }

    return stop;
}

namespace {

unsigned long long lldbFrameAddress(const std::string& said) {
    std::vector<std::string> all = lines(said);
    for (size_t i = 0; i < all.size(); ++i) {
        std::string line = withoutPrompt(all[i]);
        size_t frame = line.find("frame #0:");
        if (frame == std::string::npos) continue;
        size_t hex = line.find("0x", frame);
        if (hex == std::string::npos) continue;
        return std::strtoull(line.c_str() + hex, 0, 16);
    }
    return 0;
}

// Where cdb stands after a move: the line under "module!function+0x..:" in what `t` or `p` printed,
// "00007ff6`57691124 8b442420 mov ...", whose first word is the address with cdb's backtick in it.
unsigned long long cdbStopAddress(const std::string& said) {
    std::vector<std::string> all = lines(said);
    unsigned long long found = 0;
    for (size_t i = 0; i + 1 < all.size(); ++i) {
        const std::string head = trimmed(withoutPrompt(all[i]));
        if (head.find('!') == std::string::npos || head[head.size() - 1] != ':' || head.find(' ') != std::string::npos)
            continue;
        std::string word = trimmed(withoutPrompt(all[i + 1]));
        word = word.substr(0, word.find_first_of(" \t"));
        std::string hex;
        for (size_t k = 0; k < word.size(); ++k) if (word[k] != '`') hex += word[k];
        if (hex.empty() || !std::isxdigit(static_cast<unsigned char>(hex[0]))) continue;
        found = std::strtoull(hex.c_str(), 0, 16);
    }
    return found;
}

}

bool dbg_wentNowhere(DebuggerKind kind, const Stop& before, const Stop& after) {

    if (kind != DebuggerLldb && kind != DebuggerCdb) return false;

    if (!before.stopped || !after.stopped || after.exited) return false;
    if (after.file.empty() || after.function.empty() || after.line == 0) return false;

    if (after.file != before.file || after.line != before.line) return false;
    if (after.function != before.function) return false;

    if (after.said.find("stop reason = breakpoint") != std::string::npos) return false;

    // cdb too, since Shalimar's step filter (2026-10-13): `t` over a line that calls a shm_ helper
    // first steps over the helper and stops further on in the same line, and only then into the callee.
    if (kind == DebuggerCdb) {
        if (after.said.find("Breakpoint ") != std::string::npos && after.said.find(" hit") != std::string::npos)
            return false;
        unsigned long long was = cdbStopAddress(before.said);
        unsigned long long now = cdbStopAddress(after.said);
        return was != 0 && now > was;
    }

    unsigned long long was = lldbFrameAddress(before.said);
    unsigned long long now = lldbFrameAddress(after.said);
    return was != 0 && now > was;
}

std::vector<Variable> dbg_readVariables(DebuggerKind kind, const std::string& said) {
    std::vector<Variable> found;
    std::vector<std::string> all = lines(said);

    for (size_t i = 0; i < all.size(); ++i) {
        std::string line = trimmed(withoutPrompt(all[i]));
        if (line.empty() || line == marker()) continue;

        std::string type;
        std::string address;
        // lldb's -L puts "0x...: " in front of a variable that is in memory, and spaces before one that is not.
        if (kind == DebuggerLldb && line.compare(0, 2, "0x") == 0) {
            size_t colon = line.find(": ");
            if (colon == std::string::npos) continue;
            address = line.substr(0, colon);
            line = trimmed(line.substr(colon + 2));
        }
        if (kind == DebuggerLldb) {
            if (line.empty() || line[0] != '(') continue;
            size_t close = line.find(')');
            if (close == std::string::npos) continue;
            type = line.substr(1, close - 1);
            line = trimmed(line.substr(close + 1));
        }

        size_t equals = line.find(" = ");
        if (equals == std::string::npos) continue;

        Variable variable;
        variable.name = trimmed(line.substr(0, equals));
        variable.type = type;
        variable.value = trimmed(line.substr(equals + 3));
        variable.address = address;

        // cdb's dv /t /V: "<address> <where> <type> <name> = value", the type free to hold spaces.
        if (kind == DebuggerCdb && variable.name.find(' ') != std::string::npos) {
            std::string left = variable.name;
            size_t space = left.find_last_of(" \t");
            variable.name = left.substr(space + 1);
            std::string rest = trimmed(left.substr(0, space));
            size_t first = rest.find_first_of(" \t");
            std::string at = first == std::string::npos ? rest : rest.substr(0, first);
            rest = first == std::string::npos ? std::string() : trimmed(rest.substr(first));
            size_t second = rest.find_first_of(" \t");
            variable.type = second == std::string::npos ? std::string() : trimmed(rest.substr(second));
            variable.address = at.find('@') == std::string::npos ? dbg_addressIn("0x" + at) : std::string();
        }

        if (kind == DebuggerCdb && variable.value.compare(0, 2, "0n") == 0)
            variable.value = variable.value.substr(2);
        if (variable.name.empty() || variable.name.find(' ') != std::string::npos) continue;
        found.push_back(variable);
    }
    return found;
}

std::string afterFrameNumber(DebuggerKind kind, const std::string& line) {
    if (kind == DebuggerLldb) {
        size_t frame = line.find("frame #");
        if (frame == std::string::npos) return std::string();
        size_t colon = line.find(':', frame);
        if (colon == std::string::npos) return std::string();
        return trimmed(line.substr(colon + 1));
    }

    if (line.empty() || line[0] != '#') return std::string();
    size_t i = 1;
    while (i < line.size() && line[i] >= '0' && line[i] <= '9') ++i;
    if (i == 1) return std::string();
    return trimmed(line.substr(i));
}

bool cdbFrameIn(const std::string& line, StackFrame* frame) {
    size_t open = line.rfind('[');
    size_t close = line.rfind(']');
    if (open == std::string::npos || close == std::string::npos || close < open) return false;

    std::string where = line.substr(open + 1, close - open - 1);
    size_t sign = where.rfind(" @ ");
    if (sign == std::string::npos) return false;
    std::string tail = trimmed(where.substr(sign + 3));
    if (!digits(tail)) return false;

    frame->file = trimmed(where.substr(0, sign));
    frame->line = number(tail);

    std::string call = trimmed(line.substr(0, open));
    size_t space = call.find_last_of(" \t");
    if (space != std::string::npos) call = call.substr(space + 1);
    size_t bang = call.find('!');
    if (bang == std::string::npos) return false;
    call = call.substr(bang + 1);
    size_t end = call.find_first_of("+ \t(");
    frame->function = trimmed(end == std::string::npos ? call : call.substr(0, end));
    return true;
}

std::vector<StackFrame> dbg_readFrames(DebuggerKind kind, const std::string& said) {
    std::vector<StackFrame> found;
    std::vector<std::string> all = lines(said);

    for (size_t i = 0; i < all.size(); ++i) {
        std::string line = trimmed(withoutPrompt(all[i]));
        if (line.empty() || line == marker()) continue;

        StackFrame frame;
        if (kind == DebuggerCdb) {
            if (!cdbFrameIn(line, &frame)) continue;
        } else {
            std::string rest = afterFrameNumber(kind, line);
            if (rest.empty() || !placeIn(rest, &frame)) continue;
        }

        found.push_back(frame);

        if (frame.function == "main") break;
    }
    return found;
}

std::string dbg_frameLine(const StackFrame& frame, bool looking) {

    return std::string(looking ? "> " : "  ") + frame.function + "   " +
           path::filename(frame.file) + ":" + std::to_string(frame.line);
}

size_t dbg_frameOnLine(const std::vector<StackFrame>& stack, const std::string& line) {
    const std::string bare = trimmed(line);
    for (size_t i = 0; i < stack.size(); ++i) {

        if (trimmed(dbg_frameLine(stack[i], false)) == bare) return i;
        if (trimmed(dbg_frameLine(stack[i], true)) == bare) return i;
    }
    return stack.size();
}

std::string dbg_variableLine(const Variable& variable) {
    std::string said = "  " + variable.name + " = " + variable.value;
    if (!variable.type.empty()) said += "   [" + variable.type + "]";
    return said;
}

size_t dbg_variableOnLine(const std::vector<Variable>& locals, const std::string& line) {
    const std::string bare = trimmed(line);
    for (size_t i = 0; i < locals.size(); ++i)
        if (trimmed(dbg_variableLine(locals[i])) == bare) return i;
    return locals.size();
}

namespace {

std::string complaintIn(const std::string& answer) {
    const char* const words[] = {
        "error",
        "No symbol",
        "not an lvalue",
        "Couldn't",
        "cannot be",
        "Syntax error",
        "Type conflict",
        "Bad register error"
    };

    std::vector<std::string> all = lines(answer);
    for (size_t i = 0; i < all.size(); ++i) {
        std::string line = trimmed(withoutPrompt(all[i]));
        for (size_t w = 0; w < sizeof words / sizeof words[0]; ++w) {
            if (line.find(words[w]) == std::string::npos) continue;

            size_t at = line.find("error: ");
            if (at != std::string::npos) line = line.substr(at + 7);
            return line;
        }
    }
    return std::string();
}

}

std::string dbg_watchLine(const Watch& watch) {
    return "  " + watch.expression + " = " + (watch.ok ? watch.value : "[" + watch.value + "]");
}

size_t dbg_watchOnLine(const std::vector<Watch>& watches, const std::string& line) {
    const std::string bare = trimmed(line);
    for (size_t i = 0; i < watches.size(); ++i)
        if (trimmed(dbg_watchLine(watches[i])) == bare) return i;
    return watches.size();
}

namespace {
bool cdbRefused(const std::string& line) {
    return line.find("Couldn't resolve") != std::string::npos ||
           line.find("<<ride") != std::string::npos || line.find("rror") != std::string::npos;
}
}

std::string dbg_readValue(DebuggerKind kind, const std::string& said) {
    std::vector<std::string> all = lines(said);

    for (size_t i = 0; i < all.size(); ++i) {
        const std::string line = trimmed(withoutPrompt(all[i]));
        if (line.empty() || line == marker()) continue;

        if (kind == DebuggerCdb) {

            if (line[0] == '?') continue;
            // An expression cdb cannot resolve is an error line, which ends in the marker's ", 0x2d".
            if (cdbRefused(line)) return std::string();

            size_t space = line.find_last_of(' ');
            if (space == std::string::npos || space + 1 >= line.size()) continue;
            std::string value = line.substr(space + 1);
            if (value.compare(0, 2, "0n") == 0) value = value.substr(2);

            if (value.find_first_of("0123456789") == std::string::npos) continue;
            return value;
        }

        size_t dollar = line.find('$');
        if (dollar == std::string::npos) continue;
        size_t equals = line.find(" = ", dollar);
        if (equals == std::string::npos) continue;
        return trimmed(line.substr(equals + 3));
    }
    return std::string();
}

std::string dbg_stopLine(const std::string& file, size_t line,
                         const std::string& function) {
    return "stopped at " + path::filename(file) + ":" + std::to_string(line) +
           (function.empty() ? std::string() : " in " + function);
}

std::string dbg_lookingAt(const StackFrame& frame) {
    std::string where = path::filename(frame.file) + ":" + std::to_string(frame.line);
    if (frame.function.empty()) return "the variables are those of " + where;
    return "the variables are " + frame.function + "'s, at " + where;
}

Debugger::Debugger() : kind_(DebuggerNone), onConsole_(false) {}
Debugger::~Debugger() { stop(); }

bool Debugger::start(DebuggerKind kind, const std::string& executable,
                     const std::string& program) {
    stop();

    kind_ = kind;
    if (kind_ == DebuggerNone) return false;

    executable_ = executable;
    std::string run = program.empty() ? dbg_program(kind_) : program;

    std::string command = editor::quoted(run) + " " + editor::quoted(executable);
    backend_.clear(); backendWhy_.clear();
    if (kind_ == DebuggerC6x) {
        // The simulator on the linked image when it is this build's and carries the program's lines, else
        // the emulator on the assembly (dbg_c6xBackend). Either speaks gdb's words from here on.
        backend_ = dbg_c6xBackend(executable, &backendWhy_);
        if (backend_ == "sim6747") command = editor::quoted(simulatorProgram()) + " --run --debug " + editor::quoted(tiProgramOf(executable));
        else command = editor::quoted(run) + " --debug " + editor::quoted(executable) + dbg_shalimarRuntime(executable);
    } else backend_ = dbg_name(kind_);
    if (kind_ == DebuggerCdb) {

        command = editor::quoted(run) + " -y " + editor::quoted(path::parent(executable)) +
                  " " + editor::quoted(executable);
    }

    onConsole_ = false;
    if (kind_ == DebuggerCdb && child_.startOnConsole(command)) {
        onConsole_ = true;
    } else if (!child_.start(command)) {
        kind_ = DebuggerNone;
        return false;
    }

    std::vector<std::string> first = preamble(kind_);
    for (size_t i = 0; i < first.size(); ++i) child_.say(first[i]);

    bool found = false;
    sayMarker(child_, kind_);
    child_.readUntil(marker(), &found);
    if (!found) {
        child_.stop();
        kind_ = DebuggerNone;
        return false;
    }
    return true;
}

void Debugger::stop() {
    if (child_.running()) {
        child_.say("quit");
        child_.stop();
    }
    kind_ = DebuggerNone;
}

std::string Debugger::ask(const std::string& command) {
    if (!running()) return std::string();

    child_.say(command);
    sayMarker(child_, kind_);

    bool found = false;
    std::string said = child_.readUntil(marker(), &found);
    if (!found) child_.stop();

    if (onConsole_) said = dbg_withoutEcho(dbg_withoutEscapes(said), command, markerCommand(kind_));
    return said;
}

bool Debugger::breakAt(const std::string& file, size_t line) {
    if (!running()) return false;

    char digitsIn[32];
    std::snprintf(digitsIn, sizeof digitsIn, "%lu", static_cast<unsigned long>(line));

    std::string said;
    if (dbg_gdbLike(kind_)) {
        said = ask("break " + path::filename(file) + ":" + digitsIn);
    } else if (kind_ == DebuggerCdb) {

        said = ask("bp `" + path::filename(file) + ":" + digitsIn + "`");
        return said.find("Couldn't resolve") == std::string::npos &&
               said.find("Bp expression") == std::string::npos;
    } else {
        said = ask("breakpoint set --file " + quoted(path::filename(file)) + " --line " + digitsIn);
    }

    return said.find("Breakpoint") != std::string::npos ||
           said.find("breakpoint") != std::string::npos;
}

bool Debugger::clearBreakpoints() {
    if (!running()) return false;
    ask(dbg_gdbLike(kind_) ? "delete"
                           : (kind_ == DebuggerCdb ? "bc *" : "breakpoint delete --force"));
    return true;
}

Stop Debugger::afterMoving(const std::string& command) {
    std::string said = ask(command);

    if (kind_ == DebuggerCdb) {
        std::string event = ask(".lastevent");
        said += "\n" + event;
        if (event.find("Exit process") == std::string::npos) said += "\n" + ask("ln");
    }

    Stop stop = dbg_readStop(kind_, said);
    if (!running()) stop.stopped = false;

    if (stop.stopped) {
        if (stop.file.empty()) stop.file = last_.file;
        if (stop.function.empty()) stop.function = last_.function;
        last_ = stop;
    } else if (stop.exited) {
        last_ = Stop();
    }

    readWatches();
    return stop;
}

Stop Debugger::run() {

    if (kind_ == DebuggerCdb) return afterMoving("g");

    return afterMoving(dbg_gdbLike(kind_) ? "run < /dev/null" : "run");
}

Stop Debugger::afterStepping(const std::string& command) {
    Stop before = last_;
    Stop stop = afterMoving(command);
    std::string said = stop.said;

    for (int again = 0; again < 16 && dbg_wentNowhere(kind_, before, stop); ++again) {
        before = stop;
        stop = afterMoving(command);

        said += "\n" + stop.said;
    }

    stop.said = said;
    return stop;
}

Stop Debugger::resume() { return afterMoving(kind_ == DebuggerCdb ? "g" : "continue"); }
Stop Debugger::stepOver() { return afterStepping(kind_ == DebuggerCdb ? "p" : "next"); }
// **cdb's `t` stops on a callee's first instruction**, before its prologue has stored the arguments -
// cl's programs as much as c90's and cpp11's (M10) - so one more step over goes to its first line, where
// gdb and lldb stop. A first instruction is the one cdb names "module!function:" with no offset.
static bool atFunctionEntry(const std::string& said) {
    const std::vector<std::string> all = lines(said);
    for (size_t i = all.size(); i-- > 0;) {
        const std::string bare = trimmed(all[i]);
        if (bare.find('!') == std::string::npos || !endsWith(bare, ':')) continue;
        return bare.find("+0x") == std::string::npos && bare.find(' ') == std::string::npos;
    }
    return false;
}

Stop Debugger::stepInto() {
    Stop stop = afterStepping(kind_ == DebuggerCdb ? "t" : "step");
    if (kind_ != DebuggerCdb || !atFunctionEntry(stop.said)) return stop;
    Stop first = afterStepping("p");
    first.said = stop.said + '\n' + first.said;
    return first;
}

Stop Debugger::stepOut() { return afterMoving(kind_ == DebuggerCdb ? "gu" : "finish"); }

std::vector<Variable> Debugger::locals() {
    if (!running()) return std::vector<Variable>();

    if (kind_ == DebuggerCdb) return dbg_readVariables(kind_, ask("dv /t /V"));
    if (!dbg_gdbLike(kind_)) return dbg_readVariables(kind_, ask("frame variable -L"));

    // gdb lists no addresses, so each variable is asked where it is, one "print &name" apiece.
    std::vector<Variable> found = dbg_readVariables(kind_, ask("info args"));
    std::vector<Variable> locals = dbg_readVariables(kind_, ask("info locals"));
    for (size_t i = 0; i < locals.size(); ++i) found.push_back(locals[i]);
    for (size_t i = 0; i < found.size(); ++i) found[i].address = addressOf(found[i].name);
    return found;
}

std::string Debugger::evaluate(const std::string& expression, bool* ok) {
    if (ok) *ok = false;
    if (!running() || expression.empty()) return std::string();

    std::string answer;
    if (kind_ == DebuggerCdb) {
        answer = ask("?? " + expression);
    } else if (dbg_gdbLike(kind_)) {
        answer = ask("print " + expression);
    } else {
        answer = ask("expression " + expression);
    }

    const std::string value = dbg_readValue(kind_, answer);
    if (!value.empty()) {
        if (ok) *ok = true;
        return value;
    }

    const std::string why = complaintIn(answer);
    return why.empty() ? "no answer" : why;
}

std::string Debugger::addressOf(const std::string& expression) {
    bool ok = false;
    std::string answer = evaluate("&(" + expression + ")", &ok);
    return ok ? dbg_addressIn(answer) : std::string();
}

std::string Debugger::typeOf(const std::string& expression) {
    if (!running() || expression.empty()) return std::string();
    if (kind_ == DebuggerCdb) return dbg_readType(kind_, ask("?? " + expression));
    if (dbg_gdbLike(kind_)) return dbg_readType(kind_, ask("whatis " + expression));
    return dbg_readType(kind_, ask("expression " + expression));
}

std::string dbg_readType(DebuggerKind kind, const std::string& said) {
    std::vector<std::string> all = lines(said);
    for (size_t i = 0; i < all.size(); ++i) {
        const std::string line = trimmed(withoutPrompt(all[i]));
        if (line.empty() || line == marker()) continue;
        if (kind == DebuggerCdb) {
            if (line[0] == '?') continue;
            if (cdbRefused(line)) return std::string();
            size_t space = line.find_last_of(' ');
            if (space == std::string::npos) continue;
            std::string value = line.substr(space + 1);
            if (value.find_first_of("0123456789") == std::string::npos) continue;
            return trimmed(line.substr(0, space));
        }
        if (dbg_gdbLike(kind)) {
            if (line.compare(0, 7, "type = ") == 0) return trimmed(line.substr(7));
            continue;
        }
        if (line[0] == '(') {
            size_t close = line.find(')');
            if (close != std::string::npos && line.find(" = ", close) != std::string::npos)
                return line.substr(1, close - 1);
        }
    }
    return std::string();
}

std::string dbg_addressIn(const std::string& said) {
    size_t at = said.find("0x");
    if (at == std::string::npos) return std::string();
    std::string found = "0x";
    for (size_t i = at + 2; i < said.size(); ++i) {
        char c = said[i];
        if (c == '`') continue;
        if (!std::isxdigit(static_cast<unsigned char>(c))) break;
        found += c;
    }
    return found.size() > 2 ? found : std::string();
}

void Debugger::addWatch(const std::string& expression) {
    if (expression.empty()) return;
    Watch watch;
    watch.expression = expression;
    watches_.push_back(watch);
    if (running()) {
        Watch& added = watches_[watches_.size() - 1];
        added.value = evaluate(expression, &added.ok);
        added.address = added.ok ? addressOf(expression) : std::string();
        added.type = added.ok ? typeOf(expression) : std::string();
    }
}

void Debugger::setWatch(size_t which, const std::string& expression) {
    if (which >= watches_.size()) return;
    if (expression.empty()) { removeWatch(which); return; }
    watches_[which].expression = expression;
    watches_[which].value.clear();
    watches_[which].address.clear();
    watches_[which].ok = false;
    if (running()) watches_[which].value = evaluate(expression, &watches_[which].ok);
    watches_[which].type.clear();
    if (watches_[which].ok) watches_[which].address = addressOf(expression);
    if (watches_[which].ok) watches_[which].type = typeOf(expression);
}

void Debugger::removeWatch(size_t which) {
    if (which >= watches_.size()) return;
    watches_.erase(watches_.begin() + static_cast<long>(which));
}

void Debugger::readWatches() {
    for (size_t i = 0; i < watches_.size(); ++i) {
        if (!running()) {
            watches_[i].value = "not running";
            watches_[i].address.clear();
            watches_[i].type.clear();
            watches_[i].ok = false;
            continue;
        }
        watches_[i].value = evaluate(watches_[i].expression, &watches_[i].ok);
        watches_[i].address = watches_[i].ok ? addressOf(watches_[i].expression) : std::string();
        watches_[i].type = watches_[i].ok ? typeOf(watches_[i].expression) : std::string();
    }
}

bool Debugger::setVariable(const std::string& name, const std::string& value,
                           std::string* said) {
    if (said) said->clear();
    if (!running() || name.empty() || value.empty()) return false;

    std::string answer;
    if (kind_ == DebuggerCdb) {
        answer = ask("?? " + name + " = " + value);
    } else if (dbg_gdbLike(kind_)) {
        answer = ask("set variable " + name + " = " + value);
    } else {
        answer = ask("expression " + name + " = " + value);
    }

    const std::string why = complaintIn(answer);
    if (!why.empty()) {
        if (said) *said = why;
        return false;
    }

    readWatches();
    return true;
}

bool Debugger::selectFrame(size_t which) {
    if (!running()) return false;

    const std::string number = std::to_string(which);
    std::string said;
    if (kind_ == DebuggerCdb) {

        said = ask(".frame " + number);
    } else if (dbg_gdbLike(kind_)) {
        said = ask("frame " + number);
    } else {
        said = ask("frame select " + number);
    }

    if (said.find("error:") != std::string::npos) return false;
    if (said.find("No frame at level") != std::string::npos) return false;
    if (said.find("Invalid frame") != std::string::npos) return false;

    readWatches();
    return true;
}

std::vector<StackFrame> Debugger::frames() {
    if (!running()) return std::vector<StackFrame>();

    if (kind_ == DebuggerCdb) return dbg_readFrames(kind_, ask("k"));
    if (dbg_gdbLike(kind_)) return dbg_readFrames(kind_, ask("backtrace"));
    return dbg_readFrames(kind_, ask("thread backtrace"));
}

}
