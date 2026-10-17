#include "compile.h"

#include "path.h"
#include "process.h"
#include "product.h"
#include "settings.h"

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace editor {

const char* const kArches[kArchCount] = {"x86_64-windows", "x86_64-linux", "arm64-darwin", "tms6747"};

namespace {

bool parseGnu(const std::string& line, Diagnostic& d) {
    const std::string marker = ": error: ";
    size_t at = line.find(marker);
    if (at == std::string::npos) return false;

    std::string where = line.substr(0, at);
    size_t colAt = where.rfind(':');
    if (colAt == std::string::npos || colAt == 0) return false;
    size_t lineAt = where.rfind(':', colAt - 1);
    if (lineAt == std::string::npos) return false;

    size_t lineNo = static_cast<size_t>(std::atol(where.c_str() + lineAt + 1));
    size_t colNo = static_cast<size_t>(std::atol(where.c_str() + colAt + 1));
    if (lineNo == 0 || colNo == 0) return false;

    d.file = where.substr(0, lineAt);
    d.line = lineNo;
    d.col = colNo;
    d.message = line.substr(at + marker.size());
    d.present = true;
    return true;
}

bool parseMsvc(const std::string& line, Diagnostic& d) {
    size_t at = line.find("): ");
    if (at == std::string::npos) return false;

    std::string rest = line.substr(at + 3);
    if (rest.compare(0, 6, "error ") != 0 && rest.compare(0, 12, "fatal error ") != 0)
        return false;

    size_t open = line.rfind('(', at);
    if (open == std::string::npos) return false;

    std::string inside = line.substr(open + 1, at - open - 1);
    size_t comma = inside.find(',');
    size_t lineNo = static_cast<size_t>(std::atol(inside.c_str()));
    if (lineNo == 0) return false;

    size_t colNo = 1;
    if (comma != std::string::npos)
        colNo = static_cast<size_t>(std::atol(inside.c_str() + comma + 1));
    if (colNo == 0) colNo = 1;

    d.file = line.substr(0, open);
    d.line = lineNo;
    d.col = colNo;

    d.message = rest.compare(0, 12, "fatal error ") == 0 ? rest.substr(12) : rest.substr(6);
    d.present = true;
    return true;
}

}

bool parseCc1Preprocessor(const std::string& first, const std::string& caretLine,
                          Diagnostic& d) {
    if (first.empty()) return false;

    size_t caret = caretLine.find('^');
    if (caret == std::string::npos) return false;
    for (size_t i = 0; i < caret; ++i)
        if (caretLine[i] != ' ' && caretLine[i] != '\t') return false;

    size_t after = first.rfind(": ");
    if (after == std::string::npos || after == 0) return false;
    std::string where = first.substr(0, after);
    size_t lineAt = where.rfind(':');
    if (lineAt == std::string::npos || lineAt == 0) return false;

    for (size_t i = lineAt + 1; i < where.size(); ++i)
        if (where[i] < '0' || where[i] > '9') return false;
    if (lineAt + 1 >= where.size()) return false;

    size_t lineNo = static_cast<size_t>(std::atol(where.c_str() + lineAt + 1));
    if (lineNo == 0) return false;

    size_t prefix = after + 2;
    size_t col = caret >= prefix ? caret - prefix + 1 : 1;

    std::string message = caretLine.substr(caret + 1);
    size_t begin = message.find_first_not_of(" \t");
    message = (begin == std::string::npos) ? std::string() : message.substr(begin);
    if (message.empty()) return false;

    d.file = where.substr(0, lineAt);
    d.line = lineNo;
    d.col = col;
    d.message = message;
    d.present = true;
    return true;
}

bool parseShalimar(const std::string& line, const std::string& source, Diagnostic& d) {
    const std::string marker = "Error: line ";
    if (line.compare(0, marker.size(), marker) != 0) return false;

    size_t at = line.find(':', marker.size());
    if (at == std::string::npos) return false;

    size_t lineNo = static_cast<size_t>(std::atol(line.c_str() + marker.size()));
    if (lineNo == 0) return false;

    size_t message = at + 1;
    while (message < line.size() && line[message] == ' ') ++message;

    d.file = source;
    d.line = lineNo;
    d.col = 1;
    d.message = line.substr(message);
    d.present = true;
    return true;
}

Diagnostic parseDiagnostic(const std::string& text, const std::string& source) {
    Diagnostic d;

    std::string previous;
    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find('\n', at);
        std::string line = text.substr(at, end == std::string::npos ? std::string::npos
                                                                    : end - at);
        if (!line.empty() && line[line.size() - 1] == '\r') line.resize(line.size() - 1);

        if (parseGnu(line, d) || parseMsvc(line, d)) return d;
        if (parseShalimar(line, source, d)) return d;
        if (parseCc1Preprocessor(previous, line, d)) return d;

        previous = line;
        if (end == std::string::npos) break;
        at = end + 1;
    }

    return d;
}

namespace {

std::string temporaryDirectory(const char* what) {
    char id[32];
#ifdef _WIN32
    std::snprintf(id, sizeof id, "%lu", static_cast<unsigned long>(GetCurrentProcessId()));
#else
    std::snprintf(id, sizeof id, "%ld", static_cast<long>(getpid()));
#endif
    return path::join(path::tempDir(), std::string(what) + "-" + id);
}

}

namespace {

// **Stop, for whatever a build or a run is running.** Each command runCaptured starts is on this
// list while it runs; cancelBuilds() kills every one and moves the epoch on, and a build that began
// before the move runs nothing more. A spin lock and a fixed table: nothing here has a destructor.
std::atomic<unsigned> cancelEpoch(0);
std::atomic_flag liveLock = ATOMIC_FLAG_INIT;
const int kLive = 64;
Process* live[kLive];

thread_local unsigned scopeEpoch = 0;
thread_local int scopeDepth = 0;

void lockLive() {
    while (liveLock.test_and_set(std::memory_order_acquire)) std::this_thread::yield();
}
void unlockLive() { liveLock.clear(std::memory_order_release); }

int enlist(Process* child) {
    lockLive();
    int slot = -1;
    for (int i = 0; i < kLive && slot < 0; ++i)
        if (live[i] == 0) { live[i] = child; slot = i; }
    unlockLive();
    return slot;
}

void unlist(int slot) {
    if (slot < 0) return;
    lockLive();
    live[slot] = 0;
    unlockLive();
}

unsigned epochNow() { return scopeDepth > 0 ? scopeEpoch : cancelEpoch.load(); }

int saidStopped(std::string& output, LineSink sink, void* context) {
    output += "[stopped]\n";
    if (sink) sink(context, "[stopped]");
    return kStoppedStatus;
}

// Every line the sink is handed, and on Windows the output without the carriage returns the pipe
// used to drop - popen read it in text mode, and everything that reads it expects "\n".
struct Lines {
    std::string& output;
    LineSink sink;
    void* context;
    std::string pending;
    bool carriage;

    Lines(std::string& o, LineSink s, void* c) : output(o), sink(s), context(c), carriage(false) {}

    void add(const std::string& bytes) {
        for (size_t i = 0; i < bytes.size(); ++i) {
            char c = bytes[i];
#ifdef _WIN32
            if (carriage && c != '\n') output += '\r';
            carriage = c == '\r';
            if (carriage) continue;
#endif
            output += c;
            if (!sink) continue;
            if (c == '\n') {
                sink(context, pending);
                pending.clear();
            } else if (c != '\r') {
                pending += c;
            }
        }
    }

    void end() {
        if (carriage) output += '\r';
        carriage = false;
        if (sink && !pending.empty()) sink(context, pending);
        pending.clear();
    }
};

}

BuildScope::BuildScope() {
    if (scopeDepth++ == 0) scopeEpoch = cancelEpoch.load();
}

BuildScope::~BuildScope() { --scopeDepth; }

bool buildCancelled() { return scopeDepth > 0 && scopeEpoch != cancelEpoch.load(); }

void cancelBuilds() {
    lockLive();
    cancelEpoch.fetch_add(1);
    for (int i = 0; i < kLive; ++i)
        if (live[i] != 0) live[i]->kill();
    unlockLive();
}

int runCaptured(const std::string& command, std::string& output,
                LineSink sink, void* context) {
    // Nothing run from here may read the editor's own input: a compiler that inherits it eats the
    // keystrokes not yet read, and on Windows the editor's next read saw end of file and quit. The
    // child's input is the null device, as the " < NUL" once on the command made it.
    unsigned epoch = epochNow();
    if (cancelEpoch.load() != epoch) return saidStopped(output, sink, context);

    Process child;
    if (!child.startCaptured(command)) return -1;
    int slot = enlist(&child);
    if (cancelEpoch.load() != epoch) child.kill();

    Lines lines(output, sink, context);
    std::string chunk;
    for (;;) {
        chunk.clear();
        int said = child.readAny(chunk, 0, 200);
        if (said < 0) break;
        if (said > 0) lines.add(chunk);
    }
    lines.end();

    unlist(slot);
    int status = child.finish();
    if (cancelEpoch.load() != epoch) return saidStopped(output, sink, context);
    return status;
}

namespace {

// A build whose program cannot be written where it would go - the project's own directory, as a
// rule - is refused before any compiler runs, and the refusal names the directory and what to do.
// A directory is writable if a file can be made in it; the read-only attribute _access answers with is not what UAC withholds.
std::string unwritable(const std::string& program) {
    if (program.empty()) return std::string();
    std::string dir = path::parent(program);
    if (dir.empty()) dir = ".";
    std::string probe = path::join(dir, std::string(".") + product::kLower + "-writes-here");
    if (std::FILE* f = std::fopen(probe.c_str(), "wb")) {
        std::fclose(f);
        std::remove(probe.c_str());
        return std::string();
    }
    return dir + " cannot be written to, and the program would go there - "
           "copy the project to a folder of your own, such as Documents, and build it there";
}

bool refusedUnwritable(const std::string& program, Built& result, LineSink sink, void* context) {
    std::string why = unwritable(program);
    if (why.empty()) return false;
    result.output = why + "\n";
    if (sink) sink(context, why);
    return true;
}

// The front end's way of asking, or none.
AskNative askNative = 0;
void* askNativeContext = 0;

bool looksLikeMissingProgram(const std::string& output) {
    bool shellSaidSo =
        output.find("command not found") != std::string::npos ||
        output.find("not recognized as an internal or external command") != std::string::npos ||
        output.find(": No such file or directory") != std::string::npos;
    if (!shellSaidSo) return false;

    const char* ranAfterAll[] = {"Undefined symbols", "symbol(s) not found", "ld: ",
                                 "LNK", "error:", "warning:"};
    for (size_t i = 0; i < sizeof ranAfterAll / sizeof *ranAfterAll; ++i)
        if (output.find(ranAfterAll[i]) != std::string::npos) return false;
    return true;
}

}

void setAskNative(AskNative ask, void* context) {
    askNative = ask;
    askNativeContext = context;
}

// Whether a failed build is one the native tools might make: it failed, the compilers found no
// fault in the source (a fault of the user's is not the tools'), one of the project's own tools
// was in play for this target, and the settings say to ask. The question names what was in play.
// Whether the output says one of our own tools failed, and not the program: cpp11 and c90 name a
// failed assembler or linker run, RIDE names lnk6x; an unresolved or duplicate symbol, or a library
// for another platform, is the program's fault and the vendor's tools would refuse it the same way.
bool ownToolFailed(const std::string& output, bool emulated) {
    if (emulated ? output.find("lnk6x did not link it") == std::string::npos
                 : output.find("the assembler or linker failed") == std::string::npos &&
                       output.find("the assembler failed") == std::string::npos)
        return false;
    const char* program[] = {"unresolved external", "undefined symbol", "no symbol index",
                             "LNK2001", "LNK2019", "LNK1120", "LNK2005", "already defined",
                             "multiply defined", "redefined"};
    for (size_t i = 0; i < sizeof program / sizeof *program; ++i)
        if (output.find(program[i]) != std::string::npos) return false;
    return true;
}

// The project's own tools in play for a target and the vendor's that would stand in, as the question names them.
bool nativeNames(const std::string& arch, std::string& ours, std::string& theirs) {
    if (isEmulated(arch)) {
        if (settings::tilinker().empty()) return false;
        ours = "lnk6x"; theirs = "TI's lnk6x";
        return true;
    }
    if (arch != "x86_64-windows") return false;
    bool as = !settings::assembler().empty(), ld = !settings::linker().empty();
    if (!as && !ld) return false;
    ours = as && ld ? "masm and link" : as ? "masm" : "link";
    theirs = as && ld ? "Visual Studio's ml64 and link.exe" : as ? "Visual Studio's ml64" : "Microsoft's link.exe";
    return true;
}

bool nativeFallbackWanted(bool ok, bool sourceFault, const std::string& arch,
                          const std::string& output, std::string& question) {
    question.clear();
    if (ok || sourceFault || settings::nativeForced() || !settings::askNative()) return false;
    if ((isEmulated(arch) || arch == "x86_64-windows") && !ownToolFailed(output, isEmulated(arch)))
        return false;
    std::string ours, theirs;
    if (!nativeNames(arch, ours, theirs)) return false;
    if (!nativeToolsAvailable(arch)) {
        question = "The project's own " + ours + " did not build it, and " + theirs +
                   (isEmulated(arch) ? " is not on this machine - Tools names TI's C6000 compiler directory"
                                     : " are not on this machine - no Visual Studio was found");
        return false;
    }
    question = "The project's own " + ours + " did not build it. Use " + theirs +
               " for this build instead?";
    return true;
}

namespace {

// The build again through the native tools, when the front end says yes to the question; the
// first build's answer stands otherwise. The recipes read the settings as they go, so forcing
// native for the retry is the whole switch: assembler(), linker() and tilinker() answer nothing.
template <class Again>
Built withNativeFallback(Built first, const std::string& arch, LineSink sink, void* context,
                         Again again) {
    std::string question;
    if (buildCancelled()) return first;
    if (!nativeFallbackWanted(first.ok, first.diag.present, arch, first.output, question)) {
        if (!question.empty()) {
            first.output += question + "\n";
            if (sink) sink(context, question);
        }
        return first;
    }
    if (!askNative) {
        // --build and --run have no one to ask: the build stands as failed,
        // and the line says what an interactive front end would have asked.
        std::string unasked = question + " (nothing here can ask - the build stands)";
        first.output += unasked + "\n";
        if (sink) sink(context, unasked);
        return first;
    }
    if (!askNative(askNativeContext, question)) return first;
    // The names the question used, so the line and the mark on the result cannot disagree with it (D5).
    std::string ours, theirs;
    nativeNames(arch, ours, theirs);
    std::string said = "building again with " + theirs + " in place of the project's " + ours + ", as asked";
    first.output += said + "\n";
    if (sink) sink(context, said);
    settings::forceNative(true);
    Built second = again();
    settings::forceNative(false);
    second.output = first.output + second.output;
    const std::string mark = second.ok ? "[built with " + theirs + ", not " + product::kName + "'s " + ours + "]"
                                       : "[not built: " + theirs + " failed as well]";
    second.output += mark + "\n";
    if (sink) sink(context, mark);
    return second;
}

}

Build build(const Toolchain& tool, ToolchainKind kind, const std::string& sourcePath,
            Language lang, const std::string& arch, Configuration config,
            LineSink sink, void* context) {
    Build result;

    if (!prepareFor(kind, config, arch)) {
        result.output = "no Visual Studio found - cl, ml64 and link cannot be run; name its vcvars64.bat under Tools\n";
        if (sink) sink(context, "no Visual Studio found - cl, ml64 and link cannot be run; name its vcvars64.bat under Tools");
        return result;
    }

    Recipe recipe = assemblyRecipe(tool, kind, sourcePath, lang, arch, config);
    int status = runCaptured(recipe.command, result.output, sink, context);
    if (status < 0) {
        result.output = std::string("could not run ") + programOf(tool, kind);
        return result;
    }

    result.ok = (status == 0);
    result.diag = parseDiagnostic(result.output, sourcePath);

    if (!result.ok && !result.diag.present && looksLikeMissingProgram(result.output)) {
        std::string hint = std::string(programOf(tool, kind)) +
                           " could not be run - name it with --c90, --cpp11 or --cl, or put it on PATH";
        result.output += hint + "\n";
        if (sink) sink(context, hint);
    }

    if (result.ok) {

        FILE* assembly = std::fopen(recipe.assemblyPath.c_str(), "rb");
        if (assembly) {
            std::string line;
            for (;;) {
                int c = std::fgetc(assembly);
                if (c == EOF) {
                    if (!line.empty()) result.asmLines.push_back(line);
                    break;
                }
                if (c == '\n') {
                    if (!line.empty() && line[line.size() - 1] == '\r')
                        line.resize(line.size() - 1);
                    result.asmLines.push_back(line);
                    line.clear();
                    continue;
                }
                line += static_cast<char>(c);
            }
            std::fclose(assembly);
        }
    }

    std::remove(recipe.assemblyPath.c_str());
    for (size_t i = 0; i < recipe.leftovers.size(); ++i)
        std::remove(recipe.leftovers[i].c_str());

    return result;
}

namespace {

void makeTiProgram(Built& result, const Toolchain& tool, const std::string& program, Configuration config,
                   LineSink sink, void* context);

// **A recipe's commands at once, on min(commands, cores) threads (P3).** Each one's output is captured
// whole and handed on in source order as it completes; the status is the first failure's, in that order.
int runRecipe(const Recipe& recipe, std::string& output, LineSink sink, void* context) {
    const size_t n = recipe.commands.size();
    if (n <= 1) return runCaptured(recipe.command, output, sink, context);
    const unsigned epoch = epochNow();
    std::vector<std::string> said(n);
    std::vector<int> status(n, 0);
    std::vector<char> done(n, 0);
    std::atomic<size_t> next(0);
    std::mutex held;
    std::condition_variable ended;
    auto work = [&]() {
        for (size_t i; (i = next.fetch_add(1)) < n;) {
            std::string out;
            int rc = cancelEpoch.load() != epoch ? kStoppedStatus : runCaptured(recipe.commands[i], out);
            if (rc == kStoppedStatus && out.empty()) out = "[stopped]\n";
            std::lock_guard<std::mutex> lock(held);
            said[i] = out; status[i] = rc; done[i] = 1;
            ended.notify_all();
        }
    };
    const unsigned cores = (std::max)(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> pool;
    for (size_t t = 0; t < (std::min<size_t>)(n, cores); ++t) pool.emplace_back(work);
    int first = 0;
    for (size_t i = 0; i < n; ++i) {
        std::string out;
        {
            std::unique_lock<std::mutex> lock(held);
            ended.wait(lock, [&] { return done[i] != 0; });
            out = said[i];
        }
        Lines lines(output, sink, context);
        lines.add(out);
        lines.end();
        if (first == 0 && status[i] != 0) first = status[i];
    }
    for (size_t t = 0; t < pool.size(); ++t) pool[t].join();
    return first;
}

Built buildProgramOnce(const Toolchain& tool, ToolchainKind kind, const std::string& sourcePath,
                       Language lang, const std::string& arch, Configuration config,
                       LineSink sink, void* context) {
    Built result;

    if (!prepareFor(kind, config, arch)) {
        result.output = "no Visual Studio found - cl, ml64 and link cannot be run; name its vcvars64.bat under Tools\n";
        if (sink) sink(context, "no Visual Studio found - cl, ml64 and link cannot be run; name its vcvars64.bat under Tools");
        return result;
    }

    Recipe recipe = programRecipe(tool, kind, sourcePath, lang, arch, config);
    result.program = recipe.assemblyPath;
    result.leftovers = recipe.leftovers;
    result.shalimar = kind == ToolShc;
    if (refusedUnwritable(result.program, result, sink, context)) return result;

    int made = runCaptured(recipe.command, result.output, sink, context);
    if (made < 0) {
        result.output = std::string("could not run ") + programOf(tool, kind);
        return result;
    }

    result.diag = parseDiagnostic(result.output, sourcePath);
    result.ok = (made == 0);

    if (!result.ok && !result.diag.present && looksLikeMissingProgram(result.output)) {
        std::string hint = std::string(programOf(tool, kind)) +
                           " could not be run - name it with --c90, --cpp11 or --cl, or put it on PATH";
        result.output += hint + "\n";
        if (sink) sink(context, hint);
    }
    // Run and Verify run the .out: a single file's build links one beside its .s (5.0).
    if (result.ok && tool.linkSingleFile && isEmulated(arch) && isEmulatedProgram(result.program))
        makeTiProgram(result, tool, result.program.substr(0, result.program.size() - 2), config, sink, context);
    return result;
}

struct ProgramAgain {
    const Toolchain* tool; ToolchainKind kind; const std::string* source; Language lang;
    const std::string* arch; Configuration config; LineSink sink; void* context;
    Built operator()() const {
        return buildProgramOnce(*tool, kind, *source, lang, *arch, config, sink, context);
    }
};

}

Built buildProgram(const Toolchain& tool, ToolchainKind kind, const std::string& sourcePath,
                   Language lang, const std::string& arch, Configuration config,
                   LineSink sink, void* context) {
    ProgramAgain again = {&tool, kind, &sourcePath, lang, &arch, config, sink, context};
    return withNativeFallback(again(), arch, sink, context, again);
}

namespace {

std::string q(const std::string& s) { return "\"" + s + "\""; }

bool copyText(const std::string& from, const std::string& to) {
    std::FILE* in = std::fopen(from.c_str(), "rb");
    if (!in) return false;
    std::FILE* out = std::fopen(to.c_str(), "wb");
    if (!out) { std::fclose(in); return false; }
    char buffer[4096];
    size_t got;
    bool ok = true;
    while ((got = std::fread(buffer, 1, sizeof buffer, in)) > 0)
        if (std::fwrite(buffer, 1, got, out) != got) { ok = false; break; }
    std::fclose(in);
    if (std::fclose(out) != 0) ok = false;
    return ok;
}

// The linker command file lnk6x needs: one flat memory for the C6747 with every section the
// compilers and TI's runtime write placed in it - the file VM6747/tests/ti.sh links with.
// A function and not a std::string global: see debugger.cpp - a native global with a destructor killed the mixed-mode window before main.
std::string tiLinkCmd() {
    return
    std::string("/* one flat memory for the C6747 and every section in it - written by ") + product::kName + " */\n" +
    "--rom_model\n--stack_size=0x4000\n--heap_size=0x100000\n"
    "MEMORY\n{\n    RAM : origin = 0xC0000000, length = 0x04000000\n}\n"
    "SECTIONS\n{\n"
    "    .text        > RAM\n    .const       > RAM\n    .data        > RAM\n    .bss         > RAM\n"
    "    .far         > RAM\n    .fardata     > RAM\n    .neardata    > RAM\n    .rodata      > RAM\n"
    "    .cinit       > RAM\n    .init_array  > RAM\n    .switch      > RAM\n    .cio         > RAM\n"
    "    .stack       > RAM\n    .sysmem      > RAM\n    .vm6747.eh   > RAM\n}\n";
}

std::vector<std::string> assemblyIn(const std::string& dir) {
    std::vector<std::string> found;
    std::vector<path::Entry> all = path::entries(dir);
    for (size_t i = 0; i < all.size(); ++i) {
        const std::string& n = all[i].name;
        if (!all[i].directory && n.size() > 2 && n.compare(n.size() - 2, 2, ".s") == 0 && n.find(' ') == std::string::npos)
            found.push_back(path::join(dir, n));
    }
    return found;
}

// **A tms6747 build is a TI program** (R5): asm6x makes each .s in <program>.vm a TI object and lnk6x links
// them against RTS6x, or TI's runtime named under Tools, into <program>.out, which Run hands to sim6747.
// A refusal, a failed link or no .out at all fails the build (D2), unless emulateOnly or an Emulate asks for none.
void makeTiProgram(Built& result, const Toolchain& tool, const std::string& program, Configuration config,
                   LineSink sink, void* context) {
    if (!result.ok) return;
    // A step said as it happens where there is a sink (the console), into the output where there is not (the window).
    auto tell = [&](const std::string& line) { if (sink) sink(context, line); else result.output += line + "\n"; };
    // **A tms6747 build without a .out is a failed build** (D2), unless the project or the run asks for none.
    auto fail = [&](const std::string& line) {
        result.ok = false; result.output += line + "\n"; if (sink) sink(context, line);
    };
    if (tool.emulateOnly || tool.emulating) {
        tell(tool.emulating ? "[no .out: Emulate on vm6747 runs the assembly]"
                            : "[no .out: the project says emulateOnly - Run is Emulate on vm6747]");
        return;
    }
    const std::string asMissing = c6xAssemblerMissing();
    if (!asMissing.empty()) { fail(asMissing); return; }
    std::string as = c6xAssembler();
    std::string dir = result.program;
    std::vector<std::string> sources;
    // A single file's build is one .s, not a .vm directory: it is linked as well, beside itself (5.0).
    if (!path::isDirectory(dir) && isEmulatedProgram(dir)) {
        sources.push_back(dir);
        dir = path::parent(dir);
    } else {
        sources = assemblyIn(dir);
    }
    if (sources.empty()) return;
    std::string say;
    // Shalimar's runtime packaged as a library beside RTS6x - shmrt6x.lib, or shmrt6xd.lib for a Debug build -
    // is linked with -l like it; with TI's runtime named, or none packaged, its assembly is copied in as before.
    const std::string shmLib = config == ConfigDebug ? "shmrt6xd.lib" : "shmrt6x.lib";
    const bool shmPacked = result.shalimar && settings::namedTi().empty() && !rts6xRuntimeDir().empty() &&
                           path::exists(path::join(rts6xRuntimeDir(), shmLib));
    if (result.shalimar && !shmPacked) {
        // the runtime's assembly, copied in and assembled here rather than
        // beside the editor, which is the installation's to keep
        std::string runtime = shalimarRuntimeDir(config);
        std::string into = path::join(dir, "shmrt");
        path::makeDirectories(into);
        std::vector<std::string> theirs = assemblyIn(runtime);
        for (size_t i = 0; i < theirs.size(); ++i) {
            std::string to = path::join(into, path::filename(theirs[i]));
            if (!copyText(theirs[i], to)) { say = "cannot copy the Shalimar runtime's " + path::filename(theirs[i]); break; }
            sources.push_back(to);
        }
        if (!say.empty()) { result.ok = false; result.output += say + "\n"; if (sink) sink(context, say); return; }
    }
    std::string command = q(as);
    for (size_t i = 0; i < sources.size(); ++i) command += " " + q(sources[i]);
    tell("$ asm6x " + std::to_string(sources.size()) + " sources");
    if (runCaptured(command, result.output, sink, context) != 0) {
        result.ok = false;
        std::string hint = "asm6x refused the assembly - the emulator would run it, but it is not a TI program";
        result.output += hint + "\n";
        if (sink) sink(context, hint);
        return;
    }
    std::vector<std::string> objects;
    for (size_t i = 0; i < sources.size(); ++i)
        objects.push_back(sources[i].substr(0, sources[i].size() - 2) + ".obj");

    // **RTS6x unless TI's own compiler directory is named under Tools** (5.1): RIDE's lnk6x against
    // rts6x.lib beside the editor, so nothing of TI's is on the link line; a CCS found, not named, does not count.
    const std::string named = settings::namedTi(), ours = rts6xRuntimeDir();
    const bool rts6x = named.empty() && !ours.empty();
    std::string ti = rts6x ? std::string() : settings::ti();
    const std::string made = "[" + std::to_string(objects.size()) + " TI objects made] no .out: ";
    if (!rts6x && ti.empty()) {
        fail(made + "a tms6747 build links against RTS6x in lib/rts6x-tms6747 beside " + product::kName +
             ", or TI's runtime when its compiler directory is named under Tools, and neither is here");
        return;
    }
    // A CCS found rather than named ships rts6740_elf.lib alone, and these objects want the
    // unwind personality only rts6740_elf_eh.lib has - so without one in reach, stop at objects.
    std::string lib = rts6x ? ours : path::join(ti, "lib"), extra = rts6x ? std::string() : settings::tilib();
    const bool eh = rts6x || path::exists(path::join(lib, "rts6740_elf_eh.lib")) ||
                    (!extra.empty() && path::exists(path::join(extra, "rts6740_elf_eh.lib")));
    if (!eh && named.empty()) {
        fail(made + ti + " has only rts6740_elf.lib - these objects need rts6740_elf_eh.lib, "
             "named under Tools (TI library directory)");
        return;
    }
    // The project's own C6000 linker where one is named, TI's otherwise; the runtime and the
    // command file are TI's either way. See settings::tilinker and tiLinker, which also says when
    // a linker that was named has gone, so that TI's standing in for it is never silent.
    LinkerChoice choice = rts6x ? ourLinker() : tiLinker(settings::tilinker(), settings::namedTilinker(), ti);
    if (choice.path.empty()) {
        result.ok = false;
        std::string hint = !choice.say.empty() ? choice.say
            : rts6x ? std::string("no lnk6x beside the editor - RTS6x's link needs RIDE's own C6000 linker")
            : "no lnk6x under " + ti + " - Tools names TI's C6000 compiler directory, the one with bin\\lnk6x";
        result.output += hint + "\n";
        if (sink) sink(context, hint);
        return;
    }
    if (!choice.say.empty()) {
        result.output += choice.say + "\n";
        if (sink) sink(context, choice.say);
    }
    std::string lnk = choice.path;
    // **A CCS project's link is the project's**: its .cmd files, sizes, search paths, libraries
    // and initialisation model, as .cproject says them (ccs/ccsproject.h); RIDE's own flat memory map otherwise.
    const TiLink& given = tool.tiLink;
    std::string cmdfile = path::join(dir, "ti-link.cmd");
    if (!given.given || given.cmdFiles.empty()) {
        if (std::FILE* f = std::fopen(cmdfile.c_str(), "wb")) { std::fputs(tiLinkCmd().c_str(), f); std::fclose(f); }
    }
    // the exception-handling build of TI's runtime where there is one (CCS
    // ships the other; the C++ programs need this one), else the shipped one
    // RTS6x comes in two: rts6x.lib built at -O2 for a Release build, rts6xd.lib at -O0 with _DEBUG for a Debug one.
    const std::string ours6x = config == ConfigDebug ? "rts6xd.lib" : "rts6x.lib";
    std::string rts = rts6x ? ours6x : eh ? "rts6740_elf_eh.lib" : "rts6740_elf.lib";
    std::string out = program;
    if (out.size() > 4 && out.compare(out.size() - 4, 4, ".exe") == 0) out.resize(out.size() - 4);
    out += ".out";
    std::string link = q(lnk) + " -mv6740 --abi=eabi -i " + q(lib) + (extra.empty() ? std::string() : " -i " + q(extra));
    for (size_t i = 0; i < given.searchPaths.size(); ++i) {
        if (path::isDirectory(given.searchPaths[i])) { link += " -i " + q(given.searchPaths[i]); continue; }
        std::string left = "[library search path not on this machine, left out: " + given.searchPaths[i] + "]";
        result.output += left + "\n";
        if (sink) sink(context, left);
    }
    if (given.given && !given.cmdFiles.empty()) {
        for (size_t i = 0; i < given.cmdFiles.size(); ++i) link += " " + q(given.cmdFiles[i]);
    } else {
        link += " " + q(cmdfile);
    }
    if (given.given && !given.heap.empty()) link += " --heap_size=" + given.heap;
    if (given.given && !given.stack.empty()) link += " --stack_size=" + given.stack;
    if (given.given && given.romModel == 1) link += " --rom_model";
    if (given.given && given.romModel == 0) link += " --ram_model";
    // TI's linker takes c_int00 from the library only under --rom_model, the model CCS defaults to.
    if (!rts6x && (!given.given || given.romModel < 0) && lnk.compare(0, ti.size(), ti) == 0)
        link += " --rom_model";
    for (size_t i = 0; i < objects.size(); ++i) link += " " + q(objects[i]);
    if (given.given && !given.libraries.empty()) {
        // With RTS6x a project's TI runtime - libc.a, read as rts6740_elf_eh.lib, or either rts6740 - is RTS6x's
        // for the configuration, rts6x.lib or rts6xd.lib; a project naming rts6x.lib itself gets the same choice.
        std::vector<std::string> libs = given.libraries;
        for (size_t i = 0; rts6x && i < libs.size(); ++i)
            if (libs[i] == "libc.a" || libs[i].compare(0, 7, "rts6740") == 0 || libs[i] == "rts6x.lib" || libs[i] == "rts6xd.lib")
                libs[i] = ours6x;
        rts = libs[0];
        if (shmPacked) link += " -l " + shmLib;
        for (size_t i = 0; i < libs.size(); ++i) link += " -l " + libs[i];
    } else {
        // Shalimar's runtime before the C runtime it calls into, so lnk6x finds what it needs after it.
        if (shmPacked) link += " -l " + shmLib;
        link += " -l " + rts;
    }
    link += " -o " + q(out);
    tell("$ lnk6x " + std::to_string(objects.size()) + " objects, " + (shmPacked ? shmLib + ", " : std::string()) + rts +
                    (given.given && !given.cmdFiles.empty() ? ", " + path::filename(given.cmdFiles[0]) : std::string()) +
                    " -o " + path::filename(out));
    if (runCaptured(link, result.output, sink, context) != 0) {
        result.ok = false;
        std::string hint = "lnk6x did not link it - see its messages above";
        result.output += hint + "\n";
        if (sink) sink(context, hint);
        return;
    }
    tell("[linked " + out + "]");
}

}

// RTS6x's link is RIDE's own lnk6x: the one settings.json names, else the one beside the editor.
LinkerChoice ourLinker() {
    LinkerChoice choice;
    std::string named = settings::tilinker();
    choice.path = !named.empty() && path::exists(named) ? named : path::besideProgram("lnk6x.exe");
    return choice;
}

// Nothing named: TI's own, .exe or not. Named and there: that one, and the
// console says which, because ours and TI's carry the same name and only the
// path tells them apart. Named and gone: TI's, and the console says so.
LinkerChoice tiLinker(const std::string& chosen, const std::string& named,
                      const std::string& tiDir) {
    LinkerChoice choice;
    // --tilinker skips the check settings::tilinker() makes, so a path typed wrong arrives here
    // whole. It is a choice stated for this run: say that it is not there, rather than falling
    // through to TI's and failing with TI's directory named, which is not what went wrong.
    if (!chosen.empty() && !path::exists(chosen)) {
        choice.say = "the linker named for tms6747 is not there: " + chosen;
        return choice;
    }
    if (!chosen.empty()) {
        choice.path = chosen;
        choice.say = "[linking with " + chosen + "]";
        return choice;
    }
    std::string theirs = path::join(path::join(tiDir, "bin"), "lnk6x.exe");
    if (!path::exists(theirs)) theirs = path::join(path::join(tiDir, "bin"), "lnk6x");
    if (!path::exists(theirs)) return choice;
    choice.path = theirs;
    if (!named.empty())
        choice.say = "[" + named + " is not there - linking with TI's " + theirs + "]";
    return choice;
}

namespace {

Built buildTargetOnce(const Toolchain& tool, ToolchainKind kind,
                      const std::vector<std::string>& sources, Language lang,
                      const std::string& arch, Configuration config,
                      const std::string& program, LineSink sink, void* context) {
    Built result;

    if (sources.empty()) {
        result.output = "nothing to build\n";
        return result;
    }
    if (refusedUnwritable(program, result, sink, context)) return result;

    if (!prepareFor(kind, config, arch)) {
        result.output = "no Visual Studio found - cl, ml64 and link cannot be run; name its vcvars64.bat under Tools\n";
        if (sink) sink(context, "no Visual Studio found - cl, ml64 and link cannot be run; name its vcvars64.bat under Tools");
        return result;
    }

    Recipe recipe = targetRecipe(tool, kind, sources, lang, arch, config, program);
    result.program = recipe.assemblyPath;
    result.shalimar = kind == ToolShc;
    result.leftovers = recipe.leftovers;

    int made = runRecipe(recipe, result.output, sink, context);
    if (made < 0) {
        result.output = std::string("could not run ") + programOf(tool, kind);
        return result;
    }

    result.diag = parseDiagnostic(result.output, sources.empty() ? std::string() : sources[0]);
    result.ok = (made == 0);

    if (!result.ok && !result.diag.present && looksLikeMissingProgram(result.output)) {
        std::string hint = std::string(programOf(tool, kind)) +
                           " could not be run - name it with --c90, --cpp11 or --cl, or put it on PATH";
        result.output += hint + "\n";
        if (sink) sink(context, hint);
    }

    for (size_t i = 0; i < result.leftovers.size(); ++i)
        std::remove(result.leftovers[i].c_str());
    result.leftovers.clear();
    if (isEmulated(arch)) makeTiProgram(result, tool, program, config, sink, context);
    return result;
}

struct TargetAgain {
    const Toolchain* tool; ToolchainKind kind; const std::vector<std::string>* sources; Language lang;
    const std::string* arch; Configuration config; const std::string* program; LineSink sink; void* context;
    Built operator()() const {
        return buildTargetOnce(*tool, kind, *sources, lang, *arch, config, *program, sink, context);
    }
};

}

Built buildTarget(const Toolchain& tool, ToolchainKind kind,
                  const std::vector<std::string>& sources, Language lang,
                  const std::string& arch, Configuration config,
                  const std::string& program, LineSink sink, void* context) {
    TargetAgain again = {&tool, kind, &sources, lang, &arch, config, &program, sink, context};
    return withNativeFallback(again(), arch, sink, context, again);
}

namespace {

Built buildPartsOnce(const Toolchain& tool, const std::vector<Part>& parts,
                     const std::string& arch, Configuration config,
                     const std::string& program, LineSink sink, void* context) {
    Built result;

    if (parts.empty()) {
        result.output = "nothing to build\n";
        return result;
    }
    if (refusedUnwritable(program, result, sink, context)) return result;

    // One part is its compiler's own link - unless the project names libraries, which cc1 and
    // cxx1 do not take: those go to the host's linker below, with the objects. The emulated
    // target links nothing and a Shalimar program links its own runtime, so those two stay.
    if (parts.size() == 1) {
        ToolchainKind only = toolchainOf(tool, parts[0]);
        if (tool.libraries.empty() || isEmulated(arch) || only == ToolShc)
            return buildTargetOnce(tool, only, parts[0].sources, parts[0].lang, arch, config,
                                   program, sink, context);
    }

    bool withCpp = false;
    for (size_t i = 0; i < parts.size(); ++i) {
        ToolchainKind kind = toolchainOf(tool, parts[i]);
        if (!prepareFor(kind, config, arch)) {
            result.output = "no Visual Studio found - cl, ml64 and link cannot be run; name its vcvars64.bat under Tools\n";
            if (sink) sink(context, "no Visual Studio found - cl, ml64 and link cannot be run; name its vcvars64.bat under Tools");
            return result;
        }
        if (parts[i].lang == LangCpp) withCpp = true;
    }

    // For the emulated target the parts' assembly is the program: each part
    // writes its .s files straight into <program>.vm, and there is no link.
    const bool emulated = isEmulated(arch);
    std::string objects = emulated ? emulatedProgram(program) : temporaryDirectory((std::string(product::kLower) + "-parts").c_str());
    if (emulated) path::removeTree(objects);
    path::makeDirectories(objects);

    std::vector<std::string> made;
    for (size_t i = 0; i < parts.size(); ++i) {
        ToolchainKind kind = toolchainOf(tool, parts[i]);

        if (sink)
            sink(context, "$ " + parts[i].group + " (" + toolchainShown(tool, kind) + ")");

        std::vector<std::string> theirs;
        Recipe recipe = objectRecipe(tool, kind, parts[i].sources, parts[i].lang,
                                     arch, config, objects, theirs);

        int rc = runRecipe(recipe, result.output, sink, context);
        if (rc != 0) {

            result.diag = parseDiagnostic(result.output, parts[i].sources[0]);
            if (rc < 0 || (!result.diag.present && looksLikeMissingProgram(result.output))) {
                std::string hint = std::string(programOf(tool, kind)) +
                                   " could not be run - name it with --c90, --cpp11 or --cl, or put "
                                   "it on PATH";
                result.output += hint + "\n";
                if (sink) sink(context, hint);
            }
            path::removeTree(objects);
            return result;
        }
        for (size_t o = 0; o < theirs.size(); ++o) made.push_back(theirs[o]);
    }

    if (emulated) {
        result.program = objects;
        result.ok = true;
        for (size_t i = 0; i < parts.size(); ++i)
            if (toolchainOf(tool, parts[i]) == ToolShc) result.shalimar = true;
        makeTiProgram(result, tool, program, config, sink, context);
        return result;
    }

    if (sink) sink(context, "$ linking with " + linkerName(withCpp));

    Recipe link = linkRecipe(tool, made, withCpp, arch, config, program);
    int linked = runCaptured(link.command, result.output, sink, context);

#ifdef __APPLE__
    // **The DWARF is in the objects, and the objects are about to go.** On a Mac the linker leaves
    // debug information in the objects and maps to them; only a driver that compiled the sources runs dsymutil, and a link of objects gets no bundle however many -g it is given.
    // So the bundle is asked for here, or a breakpoint in a project of two compilers stops nowhere.
    if (linked == 0 && config == ConfigDebug)
        runCaptured("dsymutil \"" + program + "\"", result.output, sink, context);
#endif

    path::removeTree(objects);

    if (linked != 0) {

        if (linked < 0) {
            std::string hint = linkerName(withCpp) +
                               " could not be run - it is the host's linker, and on Windows "
                               "it reaches PATH only after vcvars64.bat";
            result.output += hint + "\n";
            if (sink) sink(context, hint);
        }
        return result;
    }

    result.program = program;
    result.ok = true;
    return result;
}

struct PartsAgain {
    const Toolchain* tool; const std::vector<Part>* parts; const std::string* arch;
    Configuration config; const std::string* program; LineSink sink; void* context;
    Built operator()() const {
        return buildPartsOnce(*tool, *parts, *arch, config, *program, sink, context);
    }
};

}

Built buildParts(const Toolchain& tool, const std::vector<Part>& parts,
                 const std::string& arch, Configuration config,
                 const std::string& program, LineSink sink, void* context) {
    PartsAgain again = {&tool, &parts, &arch, config, &program, sink, context};
    return withNativeFallback(again(), arch, sink, context, again);
}

Ran runBuilt(const std::string& program, LineSink sink, void* context, bool shalimar,
             const std::vector<std::string>& args) {
    Ran result;
    if (program.empty()) return result;

    result.built = true;
    result.ran = true;
    result.status = runCaptured(launchCommand(program, shalimar, args), result.output, sink, context);
    return result;
}

void removeProgram(const Built& built) {
    // A program for the emulated target is a .s file or a .vm directory, and the .out linked beside it.
    if (!built.program.empty() && path::isDirectory(built.program)) {
        path::removeTree(built.program);
        std::remove(tiProgramOf(built.program).c_str());
    }
    else if (!built.program.empty()) {
        std::remove(built.program.c_str());
        // a single file's .s, and what its TI link left beside it: the object and the .out (5.0)
        if (isEmulatedProgram(built.program)) {
            std::remove(tiProgramOf(built.program).c_str());
            std::remove((built.program.substr(0, built.program.size() - 2) + ".obj").c_str());
        }
    }
    for (size_t i = 0; i < built.leftovers.size(); ++i)
        std::remove(built.leftovers[i].c_str());
#ifdef __APPLE__
    // The .dSYM a debug build leaves beside the program - the compiler's driver makes one for a
    // single file, buildParts for a project - is a directory, and std::remove does not take those.
    // Left behind, the temporary directory filled with ride-run-<pid>.dSYM bundles, one per F8.
    if (!built.program.empty()) path::removeTree(built.program + ".dSYM");
#endif
}

int runnerFor(int asked, const std::string& arch, bool emulateOnly) {
    if (asked != RunAsked || !isEmulated(arch)) return asked;
    return emulateOnly ? RunEmulator : RunSimulator;
}

std::string runnerLine(int runner, const std::string& program) {
    if (runner == RunSimulator) return "$ sim6747 --run " + path::filename(tiProgramOf(program));
    std::string leaf = program;
    while (!leaf.empty() && (leaf.back() == '/' || leaf.back() == '\\')) leaf.pop_back();
    return "$ vm6747 " + path::filename(leaf);
}

std::string simulationMissing(const std::string& program) {
    if (simulatorProgram().empty())
        return "no sim6747 beside " + std::string(product::kName) + " - the C6747 simulator ships with it from 5.0";
    const std::string out = tiProgramOf(program);
    if (!path::exists(out))
        return "no " + path::filename(out) + ": a tms6747 build links one only with asm6x and lnk6x beside " +
               product::kName + " and a runtime - RTS6x in lib/rts6x-tms6747 beside it, or TI's named under Tools";
    return std::string();
}

bool startSimulated(Process& process, const std::string& program) {
    if (!simulationMissing(program).empty()) return false;
    return process.startInteractive(simulateCommand(tiProgramOf(program)));
}

namespace {

std::vector<std::string> outputLines(const std::string& text, bool simulator) {
    std::vector<std::string> lines;
    size_t at = 0;
    while (at < text.size()) {
        size_t nl = text.find('\n', at);
        std::string line = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        at = nl == std::string::npos ? text.size() : nl + 1;
        if (simulator && line.compare(0, 7, "CYCLES ") == 0) continue;   // the count, not the program's
        lines.push_back(line);
    }
    return lines;
}

}

Ran verifyBuilt(const std::string& program, bool shalimar, const std::vector<std::string>& args,
                LineSink sink, void* context) {
    Ran result;
    if (program.empty()) return result;
    result.built = true;
    auto say = [&](const std::string& line) { result.output += line + "\n"; if (sink) sink(context, line); };
    const std::string missing = simulationMissing(program);
    if (!missing.empty()) { say("[verify] " + missing); result.status = 2; return result; }
    if (!args.empty()) say("[verify] the simulator takes no command line - both run without one");

    std::string emulated, simulated;
    const int emuStatus = runCaptured(launchCommand(program, shalimar), emulated);
    const int simStatus = runCaptured(simulateCommand(tiProgramOf(program)), simulated);
    result.ran = true;
    const std::vector<std::string> a = outputLines(emulated, false), b = outputLines(simulated, true);
    std::string cycles;
    for (const std::string& line : outputLines(simulated, false))
        if (line.compare(0, 7, "CYCLES ") == 0) cycles = line.substr(7);

    for (const std::string& line : a) say(line);
    say("[verify] emulator  (vm6747, the assembly):    " + std::to_string(a.size()) + " lines, exit " + std::to_string(emuStatus));
    say("[verify] simulator (sim6747, " + path::filename(tiProgramOf(program)) + "): " +
        std::to_string(b.size()) + " lines" + (cycles.empty() ? std::string() : ", " + cycles));
    size_t k = 0;
    while (k < a.size() && k < b.size() && a[k] == b[k]) ++k;
    if (k == a.size() && k == b.size()) {
        say("[verify] the two agree, line for line");
        result.status = 0;
    } else {
        say("[verify] they differ at line " + std::to_string(k + 1) + ":");
        say("  emulator : " + (k < a.size() ? a[k] : std::string("(no more output)")));
        say("  simulator: " + (k < b.size() ? b[k] : std::string("(no more output)")));
        result.status = 1;
    }
    (void)simStatus;
    return result;
}

Ran runProgram(const Toolchain& tool, ToolchainKind kind, const std::string& sourcePath,
               Language lang, const std::string& arch, Configuration config,
               LineSink sink, void* context) {
    Ran result;

    Built made = buildProgram(tool, kind, sourcePath, lang, arch, config, sink, context);
    result.output = made.output;
    result.diag = made.diag;
    result.built = made.ok;

    if (result.built) {
        result.ran = true;
        result.status = runCaptured(launchCommand(made.program, kind == ToolShc),
                                    result.output, sink, context);
    }

    removeProgram(made);
    return result;
}


std::vector<std::string> cleanBuilt(const std::string& program, bool itsFolder) {
    std::vector<std::string> removed;
    // A file only - a project named as one of its own folders must not lose the folder - and a
    // directory only where it is a build's own: a .dSYM, a .vm, RIDE's folder for a CCS project.
    auto gone = [&removed](const std::string& p, bool directory) {
        if (p.empty() || !path::exists(p) || path::isDirectory(p) != directory) return;
        if (directory) path::removeTree(p);
        else std::remove(p.c_str());
        if (!path::exists(p)) removed.push_back(p);
    };
    if (!program.empty()) {
        std::string stem = program;
        if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".exe") == 0) stem.resize(stem.size() - 4);
        gone(program, false);
        gone(program + ".dSYM", true);
        gone(stem + ".pdb", false);
        gone(stem + ".ilk", false);
        gone(stem + ".map", false);
        gone(stem + ".out", false);
        gone(emulatedProgram(program), true);
        // RIDE's own folder for a CCS project, and only when it is: named <product>-ccs-<project>.
        const std::string folder = path::parent(program);
        if (itsFolder && path::filename(folder).compare(0, std::string(product::kLower).size() + 5,
                                                        std::string(product::kLower) + "-ccs-") == 0)
            gone(folder, true);
    }
    // This process's own scratch: <product>-run-<id>, <product>-build-<id> and what hangs off them -
    // never another RIDE's, which may be building as this one cleans.
    char id[32];
#ifdef _WIN32
    std::snprintf(id, sizeof id, "-%lu", static_cast<unsigned long>(GetCurrentProcessId()));
#else
    std::snprintf(id, sizeof id, "-%ld", static_cast<long>(getpid()));
#endif
    const std::string ours = std::string(product::kLower) + "-";
    const std::string tag(id);
    std::vector<path::Entry> scratch = path::entries(path::tempDir());
    for (size_t i = 0; i < scratch.size(); ++i) {
        const std::string& name = scratch[i].name;
        if (name.compare(0, ours.size(), ours) != 0) continue;
        size_t at = name.find(tag);
        if (at == std::string::npos) continue;
        size_t after = at + tag.size();
        if (after < name.size() && name[after] != '.') continue;   // -123 is not -1234
        gone(path::join(path::tempDir(), name), scratch[i].directory);
    }
    return removed;
}

bool startProgram(Process& process, const std::string& program, bool shalimar,
                  const std::vector<std::string>& args) {
    if (program.empty()) return false;
    return process.startInteractive(launchCommand(program, shalimar, args));
}

// Until the program's output closes: handed over as it comes. A program that has ended but left a
// child holding its output open is given a moment for the last of it, not forever.
void pumpProgram(Process& process, ChunkSink sink, void* context) {
    std::string chunk;
    int quiet = 0;
    for (;;) {
        chunk.clear();
        bool isStderr = false;
        int said = process.readAny(chunk, &isStderr, 50);
        if (said < 0) break;
        if (said > 0) {
            quiet = 0;
            if (sink) sink(context, chunk.data(), chunk.size(), isStderr);
            continue;
        }
        if (process.ended(0) && ++quiet >= 6) break;
    }
}

}
