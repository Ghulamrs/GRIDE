
#include "bridge.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>

#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

#include <richedit.h>
#include <richole.h>
#include <tom.h>

#pragma comment(lib, "user32.lib")
#endif

#include "about.h"
#include "compile.h"
#include "convert.h"
#include "debugger.h"
#include "shalimar/session.h"
#include "find.h"
#include "indent.h"
#include "product.h"
#include "path.h"
#include "project.h"
#include "symbols.h"
#include "syntax.h"
#include "settings.h"
#include "options.h"
#include "workspace.h"
#include "ccs/ccsworkspace.h"

namespace {

static_assert(GRIDE_KIND_KEYWORD == static_cast<int>(editor::KindKeyword), "kind numbering has drifted");
static_assert(GRIDE_KIND_LABEL == static_cast<int>(editor::KindLabel), "kind numbering has drifted");
static_assert(GRIDE_LANG_CPP == static_cast<int>(editor::LangCpp), "language numbering has drifted");
static_assert(GRIDE_LANG_SHALIMAR == static_cast<int>(editor::LangShalimar), "language numbering has drifted");
static_assert(GRIDE_LANG_ASM == static_cast<int>(editor::LangAsm), "language numbering has drifted");
static_assert(GRIDE_LANG_JSON == static_cast<int>(editor::LangJson), "language numbering has drifted");
static_assert(GRIDE_TOOL_MSVC == static_cast<int>(editor::ToolMsvc), "toolchain numbering has drifted");
static_assert(GRIDE_TOOL_SHC == static_cast<int>(editor::ToolShc), "toolchain numbering has drifted");
static_assert(GRIDE_TOOL_CXX == static_cast<int>(editor::ToolCxx), "toolchain numbering has drifted");
static_assert(GRIDE_TOOL_CXX1 == static_cast<int>(editor::ToolCxx1), "toolchain numbering has drifted");
static_assert(GRIDE_CONFIG_RELEASE == static_cast<int>(editor::ConfigRelease), "config numbering has drifted");

char* give(const std::string& text) {
    char* out = static_cast<char*>(std::malloc(text.size() + 1));
    if (!out) return 0;
    std::memcpy(out, text.c_str(), text.size() + 1);
    return out;
}

// A message into the caller's buffer, cut where it must be at the start of a UTF-8 character, so
// the last one is never half a character that the window shows as a replacement mark (L14).
void copyOut(char* into, int size, const std::string& text) {
    size_t room = static_cast<size_t>(size) - 1;
    size_t n = text.size() < room ? text.size() : room;
    if (n < text.size())
        while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80) --n;
    std::memcpy(into, text.data(), n);
    into[n] = '\0';
}

std::vector<std::string> split(const char* text) {
    std::vector<std::string> lines;
    std::string line;
    for (const char* p = text ? text : ""; *p; ++p) {
        if (*p == '\n') {
            lines.push_back(line);
            line.clear();
        } else if (*p != '\r') {
            line += *p;
        }
    }
    lines.push_back(line);
    return lines;
}

std::string join(const std::vector<std::string>& lines) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) out += "\n";
        out += lines[i];
    }
    return out;
}

editor::IndentStyle styleOf(int width, int tabs, int caseIndent, int dialect) {
    editor::IndentStyle style;
    if (width >= 1 && width <= 16) style.width = static_cast<size_t>(width);
    style.tabs = tabs != 0;
    style.caseIndent = caseIndent ? 1 : 0;
    style.dialect = dialect == GRIDE_DIALECT_SHALIMAR ? editor::DialectShalimar
                                                    : editor::DialectC;
    return style;
}

// **Made once and never destroyed, and every string this file hands back goes through it.** A
// function-local static with a destructor registers an atexit handler when first reached, and in
// this mixed image that corrupts the heap - Json::get did, and gride_group_for_file again on 2026-09-11. tests/test.cpp scans the window's sources for the shape.
std::string& scratch() {
    static std::string* kept = new std::string();
    return *kept;
}

#ifdef _WIN32

char faultLog[MAX_PATH] = "";   // set before main, by EarlyWatch below

void write(FILE* f, const char* text) { std::fputs(text, f); }

// The stack, symbolised, in a frame of its own: dbghelp loads, locks and allocates, which is no
// place to be on a stack that has just overflowed, so onFault never calls this for one.
__declspec(noinline) void writeStack(FILE* f) {
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, NULL, TRUE);

    void* frames[62];
    USHORT got = RtlCaptureStackBackTrace(0, 62, frames, NULL);

    char room[sizeof(SYMBOL_INFO) + 512];
    SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(room);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 500;

    for (USHORT i = 0; i < got; ++i) {
        DWORD64 address = reinterpret_cast<DWORD64>(frames[i]);
        DWORD64 offset = 0;

        if (SymFromAddr(process, address, &offset, symbol)) {
            IMAGEHLP_LINE64 line;
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            DWORD column = 0;
            if (SymGetLineFromAddr64(process, address, &column, &line))
                std::fprintf(f, "  %2u  %s  (%s:%lu)\n", i, symbol->Name, line.FileName,
                             static_cast<unsigned long>(line.LineNumber));
            else
                std::fprintf(f, "  %2u  %s + 0x%llX\n", i, symbol->Name,
                             static_cast<unsigned long long>(offset));
        } else {
            std::fprintf(f, "  %2u  %p\n", i, frames[i]);
        }
    }

    SymCleanup(process);
}

// Native faults only. A managed exception is not logged here: they are routine, handled ones
// arrive first-chance, and the window's own ThreadException handler logs the one that escapes.
LONG CALLBACK onFault(EXCEPTION_POINTERS* info) {
    static bool inside = false;
    if (inside) return EXCEPTION_CONTINUE_SEARCH;

    DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != STATUS_HEAP_CORRUPTION &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_ILLEGAL_INSTRUCTION)
        return EXCEPTION_CONTINUE_SEARCH;
    inside = true;

    // Kept to a megabyte: past that it is begun again rather than grown for ever.
    const char* where = faultLog[0] ? faultLog : "fault.log";
    WIN32_FILE_ATTRIBUTE_DATA facts;
    bool full = GetFileAttributesExA(where, GetFileExInfoStandard, &facts) &&
                (facts.nFileSizeHigh != 0 || facts.nFileSizeLow > 1024 * 1024);
    FILE* f = std::fopen(where, full ? "w" : "a");
    if (!f) {
        inside = false;
        return EXCEPTION_CONTINUE_SEARCH;
    }

    std::fprintf(f, "\nexception 0x%08lX at %p\n", static_cast<unsigned long>(code),
                 info->ExceptionRecord->ExceptionAddress);
    if (code == EXCEPTION_ACCESS_VIOLATION &&
        info->ExceptionRecord->NumberParameters >= 2) {
        std::fprintf(f, "  %s address %p\n",
                     info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
                     reinterpret_cast<void*>(info->ExceptionRecord->ExceptionInformation[1]));
    }

    if (code == EXCEPTION_STACK_OVERFLOW) std::fputs("  stack overflow - no stack is walked on a full one\n", f);
    else writeStack(f);

    std::fclose(f);
    inside = false;
    return EXCEPTION_CONTINUE_SEARCH;
}

#endif

std::string joinedList(const std::vector<std::string>& parts) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) out += (i ? ";" : "") + parts[i];
    return out;
}

std::vector<std::string> splitList(const char* line) {
    std::vector<std::string> out;
    std::string text = line ? line : "";
    size_t at = 0;
    while (at <= text.size()) {
        size_t cut = text.find(';', at);
        if (cut == std::string::npos) cut = text.size();
        std::string part = text.substr(at, cut - at);
        size_t a = part.find_first_not_of(" \t"), b = part.find_last_not_of(" \t");
        if (a != std::string::npos) out.push_back(part.substr(a, b - a + 1));
        at = cut + 1;
    }
    return out;
}




}

struct GRIDEProject {
    editor::Project project;
    editor::options::Store draft;       // what the Compiler Options dialog is editing
    std::string answer;
    editor::Outcome last;

    std::vector<std::string> sources;

    std::vector<editor::Part> parts;
    std::string program;
    std::string why;
    std::string detail;
    int language;

    editor::DebugPlan plan;
    std::string whyNot;

    // gride_project_target_ready, called on the window's thread just before a build, read the project
    // (a CCS one afresh): the build thread then builds what was read rather than reading it again
    // while the window looks at the same strings and vectors.
    bool prepared = false;
};

namespace {

// A bare tool name made a full path - beside the editor, then on PATH - so the shell a build runs
// through never looks in the current directory for it (M14); a name found nowhere stays bare.
std::string resolved(const std::string& name) {
    if (name.empty() || name.find_first_of("/\\") != std::string::npos) return name;
    std::string found = editor::path::besideProgram(name);
    if (found.empty()) found = editor::path::onPath(name);
    return found.empty() ? name : found;
}

// The toolchain every build is given: the compilers as named, the
// installation's header directories, and the project's own paths when a
// project is open. The window has one project and passes it, loaded or not.
editor::Toolchain toolFrom(GRIDEProject* project, const char* cc1, const char* cl,
                           const char* shc, const char* cxx1, int config = -1) {
    editor::Toolchain tool;
    if (cc1 && *cc1) tool.cc1 = cc1;
    if (cl && *cl) tool.cl = cl;
    if (shc && *shc) tool.shc = shc;
    if (cxx1 && *cxx1) tool.cxx1 = cxx1;
    tool.cc1 = resolved(tool.cc1);
    tool.cl = resolved(tool.cl);
    tool.shc = resolved(tool.shc);
    tool.cxx1 = resolved(tool.cxx1);
    tool.cxx = resolved(tool.cxx);
    tool.include = editor::settings::includeDir();
    tool.lib = editor::settings::libDir();
    if (project && project->project.loaded()) {
        tool.includes = project->project.absoluteIncludes();
        tool.libraries = project->project.absoluteLibraries();
        // A CCS project's link, for the configuration being built (ccs/ccsproject.h).
        if (config >= 0) tool.tiLink = project->project.tiLink(static_cast<editor::Configuration>(config));
        tool.emulateOnly = project->project.emulateOnly();
    }
    std::vector<std::string> shared = editor::settings::includes();
    tool.includes.insert(tool.includes.end(), shared.begin(), shared.end());
    shared = editor::settings::libraries();
    tool.libraries.insert(tool.libraries.end(), shared.begin(), shared.end());
    return tool;
}

// What the next gride_*_start runs (bridge.h): taken by it and reset, so a Run after it is a plain run.
std::atomic<int> nextRunner{GRIDE_RUN_PROGRAM};
// Whether the last project build made no .out on purpose, its .pro saying emulateOnly: its Run then emulates (R5).
std::atomic<bool> builtEmulateOnly{false};

}

struct GRIDEBuild {
    editor::Build built;
    std::string assembly;
    bool stopped = false;
    // What the build made, which is what runs: <program>.vm for the emulated target, not the target's name.
    std::string program;
    bool shalimar = false;
};

struct GRIDERan {
    editor::Ran ran;
    bool stopped = false;
};

struct GRIDEProgram {
    editor::Built built;
};

struct GRIDEDebugger {
    editor::Debugger debugger;

    shalimar::Session shm;
    editor::Stop stop;
    std::vector<editor::Variable> locals;
    std::vector<editor::StackFrame> stack;
    size_t looking;
    std::string frameLine;
    std::string variableLine;
    std::string watchLine;
    std::string lookingLine;
    std::string complaint;
    std::string answer;
    std::string output;
    std::string refusal;

    GRIDEDebugger() : looking(0) {}
};

extern "C" {

void gride_watch_for_faults(const char* logPath) {
#ifdef _WIN32
    static bool watching = false;
    if (logPath && *logPath) {
        std::strncpy(faultLog, logPath, sizeof faultLog - 1);
        faultLog[sizeof faultLog - 1] = '\0';
    }
    if (!watching) AddVectoredExceptionHandler(1, onFault);
    watching = true;
#else
    (void)logPath;
#endif
}

#ifdef _WIN32
// Watching from the first native initialiser, before main - a window that
// dies on the way to main leaves nothing otherwise, and one did.
namespace {
struct EarlyWatch {
    EarlyWatch() {
        char temp[MAX_PATH];
        DWORD n = GetEnvironmentVariableA("TEMP", temp, MAX_PATH);
        const std::string leaf = std::string(editor::product::kName) + "-fault.log";
        std::string where = (n > 0 && n < MAX_PATH) ? std::string(temp) + "\\" + leaf : leaf;
        gride_watch_for_faults(where.c_str());
    }
} earlyWatch;
}
#endif

#ifdef _WIN32

static void undoRecording(void* windowHandle, long how) {
    HWND window = reinterpret_cast<HWND>(windowHandle);
    if (!window) return;

    IRichEditOle* ole = 0;
    SendMessage(window, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&ole));
    if (!ole) return;

    ITextDocument* document = 0;
    if (SUCCEEDED(ole->QueryInterface(__uuidof(ITextDocument),
                                      reinterpret_cast<void**>(&document))) &&
        document) {
        document->Undo(how, 0);
        document->Release();
    }
    ole->Release();
}
#endif

const char* gride_settings_set_aside(void) {
    scratch() = editor::settings::setAside();
    return scratch().c_str();
}

const char* gride_code_font(void) {
    scratch() = editor::settings::codeFont();
    return scratch().c_str();
}

int gride_remember_code_font(const char* described) {
    return editor::settings::rememberCodeFont(described ? described : "") ? 1 : 0;
}

void gride_undo_suspend(void* windowHandle) {
#ifdef _WIN32
    undoRecording(windowHandle, tomSuspend);
#else
    (void)windowHandle;
#endif
}

void gride_undo_resume(void* windowHandle) {
#ifdef _WIN32
    undoRecording(windowHandle, tomResume);
#else
    (void)windowHandle;
#endif
}

char* gride_reindent(const char* text, int width, int tabs, int caseIndent, int dialect) {
    return give(join(editor::reindent(split(text),
                                      styleOf(width, tabs, caseIndent, dialect))));
}

char* gride_indent_after_newline(const char* text, int row, int col,
                               int width, int tabs, int caseIndent, int dialect) {
    if (row < 0) row = 0;
    if (col < 0) col = 0;
    return give(editor::indentAfterNewline(split(text), static_cast<size_t>(row),
                                           static_cast<size_t>(col),
                                           styleOf(width, tabs, caseIndent, dialect)));
}

char* gride_indent_for(const char* text, int row, int width, int tabs, int caseIndent,
                     int dialect) {
    if (row < 0) row = 0;
    std::vector<std::string> lines = split(text);
    return give(editor::indentFor(lines, static_cast<size_t>(row),
                                  styleOf(width, tabs, caseIndent, dialect)));
}

void gride_free(char* what) { std::free(what); }

char* gride_about(void) { return give(join(editor::about::lines())); }

// Find in Files may run on a thread of its own; gride_find_stop ends the walk at its next file.
static std::atomic<bool> findStop(false);

void gride_find_stop(void) { findStop = true; }

char* gride_find_in_files(const char* text, const char* folder, const char* patterns,
                         int matchCase, int wholeWord, int subfolders, int namesOnly) {
    findStop = false;
    editor::FindInFiles q;
    q.stop = &findStop;
    q.text = text ? text : "";
    q.folder = folder ? folder : "";
    q.patterns = patterns ? patterns : "";
    q.matchCase = matchCase != 0;
    q.wholeWord = wholeWord != 0;
    q.subfolders = subfolders != 0;
    q.namesOnly = namesOnly != 0;
    size_t files = 0;
    bool cut = false;
    std::vector<editor::FileHit> hits = editor::findInFiles(q, 2000, files, cut);
    // Plain strings and not <sstream>: the iostream library's globals corrupt this window's heap before main.
    const size_t n = hits.size();
    std::string out = std::string(q.namesOnly ? "Find files: '" : "Find in files: '") + q.text + "' in " + q.folder +
                      (q.patterns.empty() ? std::string() : " (" + q.patterns + ")") + " - " + std::to_string(n) +
                      (q.namesOnly ? (n == 1 ? " file" : " files") : (n == 1 ? " match" : " matches"));
    if (!q.namesOnly) out += " in " + std::to_string(files) + (files == 1 ? " file searched" : " files searched");
    if (findStop) out += ", stopped";
    else if (cut) out += ", the first " + std::to_string(n) + " shown";
    out += "\n";
    for (size_t i = 0; i < n; ++i) {
        if (q.namesOnly) out += hits[i].file + "\n";
        else out += hits[i].file + ":" + std::to_string(hits[i].line) + ":" + std::to_string(hits[i].col) + ": " +
                    hits[i].text + "\n";
    }
    return give(out);
}

char* gride_about_credits(void) {
    std::vector<std::string> all = editor::about::lines();
    const size_t stamp = editor::about::stampLines().size();
    if (stamp > 0 && all.size() > stamp) all.resize(all.size() - stamp);
    while (!all.empty() && all.back().empty()) all.pop_back();
    return give(join(all));
}

char* gride_about_stamp(void) { return give(join(editor::about::stampLines())); }

char* gride_environment(void) { return give(join(editor::about::environment())); }

char* gride_describe_build(const char* assembly) {
    return give(join(editor::describe(editor::symbolsIn(split(assembly)))));
}

char* gride_debug_note(int kind, const char* arch) {
    return give(join(editor::debugNote(static_cast<editor::ToolchainKind>(kind),
                                       arch ? arch : "")));
}

int gride_find_next(const char* text, const char* needle, int row, int col,
                  int* foundRow, int* foundCol) {
    if (row < 0) row = 0;
    if (col < 0) col = 0;
    editor::Match match = editor::findNext(split(text), needle ? needle : "",
                                           static_cast<size_t>(row),
                                           static_cast<size_t>(col));
    if (!match.found) return 0;
    if (foundRow) *foundRow = static_cast<int>(match.row);
    if (foundCol) *foundCol = static_cast<int>(match.col);
    return 1;
}

int gride_find_previous(const char* text, const char* needle, int row, int col,
                      int* foundRow, int* foundCol) {
    if (row < 0) row = 0;
    if (col < 0) col = 0;
    editor::Match match = editor::findPrevious(split(text), needle ? needle : "",
                                               static_cast<size_t>(row),
                                               static_cast<size_t>(col));
    if (!match.found) return 0;
    if (foundRow) *foundRow = static_cast<int>(match.row);
    if (foundCol) *foundCol = static_cast<int>(match.col);
    return 1;
}

char* gride_replace_all(const char* text, const char* needle, const char* with,
                      int* howMany) {
    std::vector<std::string> lines = split(text);
    size_t count = editor::replaceAll(lines, needle ? needle : "", with ? with : "");
    if (howMany) *howMany = static_cast<int>(count);
    return give(join(lines));
}

int gride_language_for(const char* path) {
    return static_cast<int>(editor::languageFor(path ? path : ""));
}

int gride_dialect_for(int language) {
    return language == GRIDE_LANG_SHALIMAR ? GRIDE_DIALECT_SHALIMAR : GRIDE_DIALECT_C;
}

int gride_highlight(const char* line, int language, int* state,
                  unsigned char* kinds, int kindsSize) {
    editor::SyntaxState carried;
    if (state) {
        carried.comment = (*state & 1) != 0;
        carried.string = (*state & 2) != 0;
    }

    std::vector<unsigned char> worked =
        editor::highlight(line ? line : "", static_cast<editor::Language>(language), carried);

    if (state) *state = (carried.comment ? 1 : 0) | (carried.string ? 2 : 0);

    int n = static_cast<int>(worked.size());
    if (n > kindsSize) n = kindsSize;
    for (int i = 0; i < n; ++i) kinds[i] = worked[static_cast<size_t>(i)];
    return n;
}

GRIDEProject* gride_project_new(void) { return new GRIDEProject(); }
void gride_project_free(GRIDEProject* project) { delete project; }

int gride_project_load(GRIDEProject* project, const char* directory,
                     char* error, int errorSize) {
    if (!project) return 0;
    std::string why;
    bool loaded = project->project.load(directory ? directory : ".", why);
    if (error && errorSize > 0) {
        copyOut(error, errorSize, why);
    }
    return loaded ? 1 : 0;
}

const char* gride_project_name(GRIDEProject* project) {
    if (!project) return "";
    project->answer = project->project.name();
    return project->answer.c_str();
}

int gride_project_groups(GRIDEProject* project) {
    if (!project) return 0;
    return static_cast<int>(project->project.groups().size());
}

const char* gride_project_group_name(GRIDEProject* project, int group) {
    if (!project) return "";
    if (group < 0 || group >= gride_project_groups(project)) return "";
    project->answer = project->project.groups()[static_cast<size_t>(group)].name;
    return project->answer.c_str();
}

int gride_project_files(GRIDEProject* project, int group) {
    if (!project) return 0;
    if (group < 0 || group >= gride_project_groups(project)) return 0;
    return static_cast<int>(
        project->project.groups()[static_cast<size_t>(group)].files.size());
}

const char* gride_project_file(GRIDEProject* project, int group, int file) {
    if (!project) return "";
    if (file < 0 || file >= gride_project_files(project, group)) return "";
    project->answer =
        project->project.groups()[static_cast<size_t>(group)].files[static_cast<size_t>(file)];
    return project->answer.c_str();
}

const char* gride_project_absolute(GRIDEProject* project, const char* relative) {
    if (!project) return "";
    project->answer = project->project.absolute(relative ? relative : "");
    return project->answer.c_str();
}

int gride_project_indent_width(GRIDEProject* project) {
    if (!project) return 0;
    return static_cast<int>(project->project.indent().width);
}
int gride_project_indent_tabs(GRIDEProject* project) {
    if (!project) return 0;
    return project->project.indent().tabs ? 1 : 0;
}
int gride_project_case_indent(GRIDEProject* project) {
    if (!project) return 0;
    return static_cast<int>(project->project.indent().caseIndent);
}
int gride_project_toolchain(GRIDEProject* project) {
    if (!project) return 0;
    return static_cast<int>(project->project.toolchain());
}
int gride_configuration(void) {
    return editor::settings::configuration() == "release" ? GRIDE_CONFIG_RELEASE : 0;
}

void gride_remember_configuration(int config) {
    editor::settings::rememberConfiguration(config == GRIDE_CONFIG_RELEASE ? "release" : "debug");
}
const char* gride_project_arch(GRIDEProject* project) {
    if (!project) return "";
    project->answer = project->project.arch();
    return project->answer.c_str();
}

int gride_ccs_is_workspace(const char* directory) {
    return directory && editor::ccs::isWorkspace(directory) ? 1 : 0;
}

const char* gride_ccs_workspace_projects(const char* directory) {
    scratch().clear();
    editor::ccs::Workspace ws;
    std::string why;
    if (directory && editor::ccs::readWorkspace(directory, ws, why))
        for (size_t i = 0; i < ws.projects.size(); ++i) scratch() += (i ? "\n" : "") + ws.projects[i].name;
    return scratch().c_str();
}

int gride_ccs_workspace_pro(const char* workspace, const char* project,
                           char* file, int fileSize, char* error, int errorSize) {
    std::string pro, why;
    bool made = workspace && project && editor::ccs::writePro(workspace, project, pro, why);
    if (file && fileSize > 0) copyOut(file, fileSize, made ? pro : std::string());
    if (error && errorSize > 0) copyOut(error, errorSize, made ? std::string() : (why.empty() ? std::string("no workspace or project named") : why));
    return made ? 1 : 0;
}

int gride_project_is_ccs(GRIDEProject* project) {
    return project && project->project.loaded() && project->project.isCcs() ? 1 : 0;
}

const char* gride_project_ccs_report(GRIDEProject* project, int config) {
    if (!project || !project->project.isCcs()) return "";
    project->answer.clear();
    std::vector<std::string> lines = project->project.ccsReport(static_cast<editor::Configuration>(config == GRIDE_CONFIG_RELEASE ? 1 : 0));
    for (size_t i = 0; i < lines.size(); ++i) project->answer += (i ? "\n" : "") + lines[i];
    return project->answer.c_str();
}

const char* gride_project_ccs_mapping(GRIDEProject* project, int config) {
    if (!project) return "";
    project->answer = project->project.ccsMapping(static_cast<editor::Configuration>(config == GRIDE_CONFIG_RELEASE ? 1 : 0));
    return project->answer.c_str();
}

int gride_project_ccs_configuration(GRIDEProject* project) {
    return project ? project->project.ccsConfiguration() : -1;
}

void gride_project_remember_configuration(GRIDEProject* project, int config) {
    if (project) project->project.rememberConfiguration(config == GRIDE_CONFIG_RELEASE ? editor::ConfigRelease : editor::ConfigDebug);
}

int gride_project_runs_as_project(GRIDEProject* project, const char* source) {
    if (!project || !source) return 0;
    return static_cast<int>(project->project.runsAsProject(source));
}

int gride_project_set_arch(GRIDEProject* project, const char* arch) {
    if (!project || !project->project.loaded() || !arch) return 0;
    project->project.setArch(arch);
    project->last = editor::saveProject(project->project);
    return project->last.ok ? 1 : 0;
}

int gride_project_set_toolchain(GRIDEProject* project, int kind) {
    if (!project || !project->project.loaded()) return 0;
    project->project.setToolchain(static_cast<editor::ToolchainKind>(kind));
    project->last = editor::saveProject(project->project);
    return project->last.ok ? 1 : 0;
}

int gride_project_allows(const char* relative, char* why, int whySize) {
    std::string reason;
    bool fine = editor::Project::allows(relative ? relative : "", reason);
    if (why && whySize > 0) {
        copyOut(why, whySize, reason);
    }
    return fine ? 1 : 0;
}

int gride_project_loaded(GRIDEProject* project) { return project && project->project.loaded() ? 1 : 0; }

const char* gride_project_root(GRIDEProject* project) {
    if (!project) return "";
    project->answer = project->project.root();
    return project->answer.c_str();
}

void gride_project_set_root(GRIDEProject* project, const char* path) {
    if (!project) return;
    project->project.setRoot(path ? path : ".");
}

void gride_project_close(GRIDEProject* project) { if (project) project->project.close(); }

const char* gride_project_suffix(void) { return editor::Project::suffix(); }

const char* gride_product_name(void) { return editor::product::kName; }
const char* gride_version(void) { return editor::about::version(); }

int gride_project_save_as(GRIDEProject* project, const char* file,
                            char* why, int whySize) {
    if (!project) return 0;
    std::string error;
    bool ok = project->project.saveAs(file ? file : "", error);
    if (!ok && why && whySize > 0) {
        std::string said = error.empty() ? std::string("could not save the project") : error;
        copyOut(why, whySize, said);
    }
    return ok ? 1 : 0;
}

const char* gride_group_for_file(const char* name) {
    scratch() = editor::groupForFile(name ? name : "");
    return scratch().c_str();
}

const char* gride_project_relative(GRIDEProject* project, const char* path) {
    if (!project) return "";
    project->answer = project->project.relative(path ? path : "");
    return project->answer.c_str();
}


const char* gride_outcome_message(GRIDEProject* project) {
    if (!project) return "";
    return project->last.message.c_str();
}

const char* gride_outcome_path(GRIDEProject* project) { return project ? project->last.path.c_str() : ""; }

int gride_create_file(GRIDEProject* project, const char* relative, const char* group, int kind) {
    if (!project) return 0;
    project->last = editor::createFile(project->project, relative ? relative : "",
                                       group ? group : "", static_cast<editor::ToolchainKind>(kind));
    return project->last.ok ? 1 : 0;
}

int gride_rename_file(GRIDEProject* project, const char* fromAbsolute, const char* toRelative) {
    if (!project) return 0;
    project->last = editor::renameFile(project->project, fromAbsolute ? fromAbsolute : "",
                                       toRelative ? toRelative : "");
    return project->last.ok ? 1 : 0;
}

int gride_delete_file(GRIDEProject* project, const char* absolute) {
    if (!project) return 0;
    project->last = editor::deleteFile(project->project, absolute ? absolute : "");
    return project->last.ok ? 1 : 0;
}

int gride_move_to_group(GRIDEProject* project, const char* absolute, const char* group) {
    if (!project) return 0;
    project->last = editor::moveToGroup(project->project, absolute ? absolute : "",
                                        group ? group : "");
    return project->last.ok ? 1 : 0;
}

int gride_add_existing(GRIDEProject* project, const char* absolute, const char* group) {
    if (!project) return 0;
    project->last = editor::addExisting(project->project, absolute ? absolute : "",
                                        group ? group : "");
    return project->last.ok ? 1 : 0;
}

int gride_adopt_saved(GRIDEProject* project, const char* absolute) {
    if (!project) return 0;
    editor::Outcome joined = editor::adoptSaved(project->project, absolute ? absolute : "");
    if (joined.ok) project->last = joined;
    return joined.ok ? 1 : 0;
}

int gride_project_holds(GRIDEProject* project, const char* absolute) {
    if (!project || !absolute || !*absolute || !project->project.loaded()) return 0;
    std::string relative = project->project.relative(absolute);
    return project->project.groupOf(relative) < project->project.groups().size() ? 1 : 0;
}

const char* gride_project_file_to_open(GRIDEProject* project) {
    scratch() = project ? project->project.fileToOpen() : std::string();
    return scratch().c_str();
}

int gride_remember_open(GRIDEProject* project, const char* absolute) {
    if (!project) return 0;
    return editor::rememberOpen(project->project, absolute ? absolute : "").ok ? 1 : 0;
}

int gride_remove_from_project(GRIDEProject* project, const char* absolute) {
    if (!project) return 0;
    project->last = editor::removeExisting(project->project, absolute ? absolute : "");
    return project->last.ok ? 1 : 0;
}

int gride_begin_project(GRIDEProject* project, const char* directory, const char* name,
                      const char* firstFile) {
    if (!project) return 0;
    project->last = editor::beginProject(project->project, directory ? directory : ".",
                                         name ? name : "Project",
                                         firstFile ? firstFile : "");
    return project->last.ok ? 1 : 0;
}


const char* gride_project_includes(GRIDEProject* project) {
    scratch() = project ? joinedList(project->project.includes()) : std::string();
    return scratch().c_str();
}

const char* gride_project_clean(GRIDEProject* project) {
    std::string program;
    bool ccs = false;
    if (project && project->project.loaded()) {
        program = project->project.targetProgram();
        ccs = project->project.isCcs();
    }
    std::vector<std::string> removed = editor::cleanBuilt(program, ccs);
    std::string said;
    for (size_t i = 0; i < removed.size(); ++i) said += removed[i] + "\n";
    scratch() = said;
    return scratch().c_str();
}

const char* gride_header_named(const char* text) {
    std::string s = text ? text : "";
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    s = a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    std::string name;
    if (!s.empty() && s[0] == '#') {
        // #include "Quat.h" or #include <cstdio>, spaces anywhere the preprocessor allows them
        size_t at = s.find_first_not_of(" \t", 1);
        if (at != std::string::npos && s.compare(at, 7, "include") == 0) {
            at = s.find_first_not_of(" \t", at + 7);
            if (at != std::string::npos && (s[at] == '"' || s[at] == '<')) {
                size_t end = s.find(s[at] == '"' ? '"' : '>', at + 1);
                if (end != std::string::npos) name = s.substr(at + 1, end - at - 1);
            }
        }
    } else {
        // A selection that is itself a file name: Quat.h, "Quat.h" or <cstdio>
        if (s.size() >= 2 && ((s[0] == '"' && s[s.size() - 1] == '"') || (s[0] == '<' && s[s.size() - 1] == '>')))
            s = s.substr(1, s.size() - 2);
        bool plain = !s.empty() && s.size() < 260 && s.find_first_of(" \t\r\n\"<>;(){}") == std::string::npos;
        if (plain) name = s;
    }
    scratch() = name;
    return scratch().c_str();
}

const char* gride_find_header(GRIDEProject* project, const char* fromFile, const char* name) {
    std::string want = name ? name : "";
    scratch().clear();
    if (want.empty()) return scratch().c_str();
    std::vector<std::string> places;
    std::string from = fromFile ? fromFile : "";
    if (!from.empty()) places.push_back(editor::path::parent(from));
    if (project && project->project.loaded()) {
        places.push_back(project->project.root());
        std::vector<std::string> own = project->project.absoluteIncludes();
        places.insert(places.end(), own.begin(), own.end());
    }
    std::vector<std::string> shared = editor::settings::includes();
    places.insert(places.end(), shared.begin(), shared.end());
    places.push_back(editor::settings::includeDir());
    places.push_back(editor::settings::libDir());
    for (size_t i = 0; i < places.size(); ++i) {
        if (places[i].empty()) continue;
        std::string there = editor::path::join(places[i], want);
        if (editor::path::exists(there) && !editor::path::isDirectory(there)) {
            scratch() = editor::path::absolute(there);
            break;
        }
    }
    return scratch().c_str();
}

const char* gride_project_libraries(GRIDEProject* project) {
    scratch() = project ? joinedList(project->project.libraries()) : std::string();
    return scratch().c_str();
}

int gride_project_set_includes(GRIDEProject* project, const char* line) {
    if (!project) return 0;
    project->project.setIncludes(splitList(line));
    project->last = editor::saveProject(project->project);
    return project->last.ok ? 1 : 0;
}

int gride_project_set_libraries(GRIDEProject* project, const char* line) {
    if (!project) return 0;
    project->project.setLibraries(splitList(line));
    project->last = editor::saveProject(project->project);
    return project->last.ok ? 1 : 0;
}

const char* gride_includes(void) {
    scratch() = joinedList(editor::settings::includes());
    return scratch().c_str();
}

const char* gride_libraries(void) {
    scratch() = joinedList(editor::settings::libraries());
    return scratch().c_str();
}

namespace {
int (*askNativeInWindow)(const char*) = 0;
bool askNativeThroughWindow(void*, const std::string& question) {
    return askNativeInWindow && askNativeInWindow(question.c_str()) != 0;
}
}
void gride_ask_native(int (*ask)(const char* question)) {
    askNativeInWindow = ask;
    editor::setAskNative(ask ? askNativeThroughWindow : 0, 0);
}

int gride_set_includes(const char* line) { return editor::settings::rememberIncludes(splitList(line)) ? 1 : 0; }
int gride_set_libraries(const char* line) { return editor::settings::rememberLibraries(splitList(line)) ? 1 : 0; }

const char* gride_install_file(void) {
    scratch() = editor::settings::installFile();
    return scratch().c_str();
}

int gride_write_install_file_if_absent(void) {
    return editor::settings::writeInstallFileIfAbsent() ? 1 : 0;
}

const char* gride_include_dir(void) {
    scratch() = editor::settings::includeDir();
    return scratch().c_str();
}

const char* gride_lib_dir(void) {
    scratch() = editor::settings::libDir();
    return scratch().c_str();
}

int gride_remember_header_dirs(const char* include, const char* lib) {
    return editor::settings::rememberHeaderDirs(include ? include : "", lib ? lib : "") ? 1 : 0;
}

int gride_default_compiler(void) {
    return static_cast<int>(editor::toolchainFrom(editor::settings::defaultCompiler()));
}

int gride_default_indent_width(void) { return static_cast<int>(editor::settings::indentWidth()); }
int gride_default_indent_tabs(void) { return editor::settings::indentTabs() ? 1 : 0; }

int gride_remember_default_compiler(int kind) {
    return editor::settings::rememberDefaultCompiler(
               editor::toolchainWord(static_cast<editor::ToolchainKind>(kind))) ? 1 : 0;
}

const char* gride_vcvars(void) {
    scratch() = editor::settings::vcvars();
    return scratch().c_str();
}

int gride_remember_vcvars(const char* file) {
    return editor::settings::rememberVcvars(file ? file : "") ? 1 : 0;
}

const char* gride_assembler(void) {
    scratch() = editor::settings::assembler();
    return scratch().c_str();
}

int gride_remember_assembler(const char* file) {
    return editor::settings::rememberAssembler(file ? file : "") ? 1 : 0;
}

const char* gride_linker(void) {
    scratch() = editor::settings::linker();
    return scratch().c_str();
}

int gride_remember_linker(const char* file) {
    return editor::settings::rememberLinker(file ? file : "") ? 1 : 0;
}

const char* gride_tilinker(void) {
    scratch() = editor::settings::tilinker();
    return scratch().c_str();
}

int gride_remember_tilinker(const char* file) {
    return editor::settings::rememberTilinker(file ? file : "") ? 1 : 0;
}

const char* gride_ti(void) {
    scratch() = editor::settings::ti();
    return scratch().c_str();
}

const char* gride_tilib(void) {
    scratch() = editor::settings::tilib();
    return scratch().c_str();
}

int gride_remember_ti(const char* dir, const char* lib) {
    return editor::settings::rememberTi(dir ? dir : "", lib ? lib : "") ? 1 : 0;
}

int gride_save_project(GRIDEProject* project) {
    if (!project) return 0;
    project->last = editor::saveProject(project->project);
    return project->last.ok ? 1 : 0;
}

int gride_parse_diagnostic(const char* text, const char* source, int* line, int* column,
                          const char** file, const char** message) {
    static std::string* keptFile = new std::string();
    static std::string* keptMessage = new std::string();
    editor::Diagnostic d = editor::parseDiagnostic(text ? text : "", source ? source : "");
    *keptFile = d.present ? d.file : std::string();
    *keptMessage = d.present ? d.message : std::string();
    if (line) *line = d.present ? static_cast<int>(d.line) : 0;
    if (column) *column = d.present ? static_cast<int>(d.col) : 0;
    if (file) *file = keptFile->c_str();
    if (message) *message = keptMessage->c_str();
    return d.present ? 1 : 0;
}

const char* gride_compiler_name(int which) {
    if (which == GRIDE_COMPILER_CPP) return editor::product::kCompilerCpp;
    if (which == GRIDE_COMPILER_SHALIMAR) return editor::product::kCompilerShalimar;
    return editor::product::kCompilerC;
}

const char* gride_arch(int index) {
    if (index < 0 || index >= static_cast<int>(editor::kArchCount)) index = 0;
    return editor::kArches[index];
}

int gride_arch_count(void) { return static_cast<int>(editor::kArchCount); }

const char* gride_toolchain_name(int kind) {
    return editor::toolchainName(static_cast<editor::ToolchainKind>(kind));
}

const char* gride_language_name(int language) {
    return editor::languageName(static_cast<editor::Language>(language));
}

const char* gride_config_name(int config) {
    return editor::configName(static_cast<editor::Configuration>(config));
}

int gride_resolve(int toolchainKind, int language) {
    editor::Toolchain tool;
    tool.kind = static_cast<editor::ToolchainKind>(toolchainKind);
    return static_cast<int>(editor::resolve(tool, static_cast<editor::Language>(language)));
}

int gride_can_compile(int kind, int language) {
    return editor::canCompile(static_cast<editor::ToolchainKind>(kind),
                              static_cast<editor::Language>(language))
               ? 1 : 0;
}

const char* gride_refusal(int kind, int language) {
    scratch() = editor::refusal(static_cast<editor::ToolchainKind>(kind),
                                static_cast<editor::Language>(language));
    return scratch().c_str();
}

int gride_uses_arch(int kind) {
    return editor::usesArch(static_cast<editor::ToolchainKind>(kind)) ? 1 : 0;
}

const char* gride_shown_command(GRIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1, int kind,
                              const char* source, int language, const char* arch,
                              int config) {
    editor::Toolchain tool = toolFrom(project, cc1, cl, shc, cxx1);

    scratch() = editor::shownCommand(tool, static_cast<editor::ToolchainKind>(kind),
                                     source ? source : "",
                                     static_cast<editor::Language>(language),
                                     arch ? arch : "",
                                     static_cast<editor::Configuration>(config));
    return scratch().c_str();
}

int gride_runs_here(int kind, const char* arch) {
    return editor::runsHere(static_cast<editor::ToolchainKind>(kind), arch ? arch : "") ? 1 : 0;
}

const char* gride_why_not_run(int kind, const char* arch) {
    scratch() = editor::whyNotRun(static_cast<editor::ToolchainKind>(kind), arch ? arch : "");
    return scratch().c_str();
}

const char* gride_host_arch(void) { return editor::hostArch(); }

const char* gride_shown_run_command(GRIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1, int kind,
                                  const char* source, int language, const char* arch,
                                  int config) {
    editor::Toolchain tool = toolFrom(project, cc1, cl, shc, cxx1);

    scratch() = editor::shownProgramCommand(tool, static_cast<editor::ToolchainKind>(kind),
                                            source ? source : "",
                                            static_cast<editor::Language>(language),
                                            arch ? arch : "",
                                            static_cast<editor::Configuration>(config),
                                            nextRunner.load() == GRIDE_RUN_EMULATOR);
    return scratch().c_str();
}

GRIDERan* gride_run(GRIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1, int kind, const char* source,
                int language, const char* arch, int config) {
    editor::Toolchain tool = toolFrom(project, cc1, cl, shc, cxx1);

    GRIDERan* out = new GRIDERan();
    editor::BuildScope scope;
    out->ran = editor::runProgram(tool, static_cast<editor::ToolchainKind>(kind),
                                  source ? source : "",
                                  static_cast<editor::Language>(language),
                                  arch ? arch : "",
                                  static_cast<editor::Configuration>(config));
    out->stopped = editor::buildCancelled();
    return out;
}

void gride_run_free(GRIDERan* ran) { delete ran; }

int gride_ran_built(GRIDERan* ran) { return ran && ran->ran.built ? 1 : 0; }
int gride_ran_ran(GRIDERan* ran) { return ran && ran->ran.ran ? 1 : 0; }
int gride_ran_status(GRIDERan* ran) { return ran ? ran->ran.status : 0; }
const char* gride_ran_output(GRIDERan* ran) { return ran ? ran->ran.output.c_str() : ""; }
int gride_ran_has_error(GRIDERan* ran) { return ran && ran->ran.diag.present ? 1 : 0; }
int gride_ran_error_line(GRIDERan* ran) { return ran ? static_cast<int>(ran->ran.diag.line) : 0; }
int gride_ran_error_column(GRIDERan* ran) { return ran ? static_cast<int>(ran->ran.diag.col) : 0; }
const char* gride_ran_error_message(GRIDERan* ran) { return ran ? ran->ran.diag.message.c_str() : ""; }
const char* gride_ran_error_file(GRIDERan* ran) { return ran ? ran->ran.diag.file.c_str() : ""; }

GRIDEProgram* gride_build_program(GRIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1, int kind, const char* source,
                              int language, const char* arch, int config) {
    editor::Toolchain tool = toolFrom(project, cc1, cl, shc, cxx1);

    GRIDEProgram* out = new GRIDEProgram();
    editor::BuildScope scope;
    out->built = editor::buildProgram(tool, static_cast<editor::ToolchainKind>(kind),
                                      source ? source : "",
                                      static_cast<editor::Language>(language),
                                      arch ? arch : "",
                                      static_cast<editor::Configuration>(config));
    return out;
}

void gride_program_free(GRIDEProgram* built) {
    if (!built) return;
    editor::removeProgram(built->built);
    delete built;
}

int gride_program_ok(GRIDEProgram* built) { return built && built->built.ok ? 1 : 0; }
const char* gride_program_path(GRIDEProgram* built) { return built ? built->built.program.c_str() : ""; }
const char* gride_program_output(GRIDEProgram* built) { return built ? built->built.output.c_str() : ""; }
int gride_program_has_error(GRIDEProgram* built) { return built && built->built.diag.present ? 1 : 0; }
int gride_program_error_line(GRIDEProgram* built) {
    if (!built) return 0;
    return static_cast<int>(built->built.diag.line);
}
int gride_program_error_column(GRIDEProgram* built) {
    if (!built) return 0;
    return static_cast<int>(built->built.diag.col);
}
const char* gride_program_error_message(GRIDEProgram* built) {
    if (!built) return "";
    return built->built.diag.message.c_str();
}
const char* gride_program_error_file(GRIDEProgram* built) {
    return built ? built->built.diag.file.c_str() : "";
}

int gride_debugger_for(int kind, const char* arch) {
    return static_cast<int>(editor::dbg_for(static_cast<editor::ToolchainKind>(kind),
                                                arch ? arch : ""));
}

const char* gride_debugger_name(int kind) {
    return editor::dbg_name(static_cast<editor::DebuggerKind>(kind));
}

const char* gride_no_debugger_because(int kind, const char* arch) {
    scratch() = editor::dbg_whyNot(static_cast<editor::ToolchainKind>(kind),
                                          arch ? arch : "");
    return scratch().c_str();
}

int gride_debugger_stops_itself(int kind) {
    return editor::dbg_stopsItself(static_cast<editor::ToolchainKind>(kind)) ? 1 : 0;
}

const char* gride_release_cannot_stop(int kind) {
    return editor::dbg_stopsItself(static_cast<editor::ToolchainKind>(kind))
               ? shalimar::releaseHasNoSession()
               : "release is built without -g";
}

const char* gride_why_it_did_not_start(int kind, const char* arch) {
    if (editor::dbg_stopsItself(static_cast<editor::ToolchainKind>(kind)) &&
        editor::dbg_for(static_cast<editor::ToolchainKind>(kind), arch ? arch : "") == editor::DebuggerNone) {

        scratch() = shalimar::didNotArm();
        return scratch().c_str();
    }
    scratch() = std::string(editor::dbg_name(
                    editor::dbg_for(static_cast<editor::ToolchainKind>(kind),
                                    arch ? arch : ""))) +
                " could not be started - is it installed?";
    return scratch().c_str();
}

GRIDEDebugger* gride_debugger_new(void) { return new GRIDEDebugger(); }
void gride_debugger_free(GRIDEDebugger* debugger) { delete debugger; }

int gride_debugger_start(GRIDEDebugger* debugger, int kind, const char* arch,
                       const char* program) {
    if (!debugger) return 0;
    debugger->stop = editor::Stop();
    debugger->locals.clear();
    debugger->stack.clear();
    debugger->looking = 0;

    const editor::ToolchainKind tool = static_cast<editor::ToolchainKind>(kind);

    // The session only where no debugger reads the program (D1: lldb and gdb read shalimar's DWARF).
    if (editor::dbg_stopsItself(tool) && editor::dbg_for(tool, arch ? arch : "") == editor::DebuggerNone)
        return debugger->shm.start(program ? program : "", editor::sessionCommand(program ? program : "")) ? 1 : 0;

    return debugger->debugger.start(editor::dbg_for(tool, arch ? arch : ""),
                                    program ? program : "") ? 1 : 0;
}

int gride_debugger_running(GRIDEDebugger* debugger) {
    if (!debugger) return 0;
    return (debugger->debugger.running() || debugger->shm.running()) ? 1 : 0;
}

int gride_debugging_shalimar(GRIDEDebugger* debugger) {
    if (!debugger) return 0;
    return debugger->shm.running() ? 1 : 0;
}

void gride_debugger_stop(GRIDEDebugger* debugger) {
    if (!debugger) return;
    debugger->debugger.stop();
    debugger->shm.stop();
    debugger->stop = editor::Stop();
    debugger->locals.clear();
    debugger->stack.clear();
    debugger->looking = 0;
}

int gride_debugger_break(GRIDEDebugger* debugger, const char* file, int line) {
    if (!debugger) return 0;
    if (line < 1) return 0;
    if (debugger->shm.running())
        return debugger->shm.breakAt(file ? file : "", static_cast<size_t>(line)) ? 1 : 0;
    return debugger->debugger.breakAt(file ? file : "", static_cast<size_t>(line)) ? 1 : 0;
}

int gride_debugger_clear(GRIDEDebugger* debugger) {
    if (!debugger) return 0;
    if (debugger->shm.running()) return debugger->shm.clearBreakpoints() ? 1 : 0;
    return debugger->debugger.clearBreakpoints() ? 1 : 0;
}

namespace {

void afterMoving(GRIDEDebugger* debugger, const editor::Stop& stop, bool itself) {
    debugger->stop = stop;
    debugger->locals.clear();
    debugger->stack.clear();
    debugger->looking = 0;
    if (!stop.stopped) return;

    if (itself) {
        debugger->stack = debugger->shm.frames();
        return;
    }

    debugger->locals = debugger->debugger.locals();
    debugger->stack = debugger->debugger.frames();
}
}

void gride_debugger_run(GRIDEDebugger* debugger) {
    if (!debugger) return;
    const bool itself = debugger->shm.running();
    afterMoving(debugger, itself ? debugger->shm.run() : debugger->debugger.run(), itself);
}
void gride_debugger_resume(GRIDEDebugger* debugger) {
    if (!debugger) return;
    const bool itself = debugger->shm.running();
    afterMoving(debugger, itself ? debugger->shm.resume() : debugger->debugger.resume(), itself);
}
void gride_debugger_step_over(GRIDEDebugger* debugger) {
    if (!debugger) return;
    const bool itself = debugger->shm.running();
    afterMoving(debugger, itself ? debugger->shm.stepOver() : debugger->debugger.stepOver(), itself);
}
void gride_debugger_step_into(GRIDEDebugger* debugger) {
    if (!debugger) return;
    const bool itself = debugger->shm.running();
    afterMoving(debugger, itself ? debugger->shm.stepInto() : debugger->debugger.stepInto(), itself);
}
void gride_debugger_step_out(GRIDEDebugger* debugger) {
    if (!debugger) return;
    const bool itself = debugger->shm.running();
    afterMoving(debugger, itself ? debugger->shm.stepOut() : debugger->debugger.stepOut(), itself);
}

int gride_stop_stopped(GRIDEDebugger* debugger) { return debugger && debugger->stop.stopped ? 1 : 0; }
int gride_stop_exited(GRIDEDebugger* debugger) { return debugger && debugger->stop.exited ? 1 : 0; }
int gride_stop_status(GRIDEDebugger* debugger) { return debugger ? debugger->stop.status : 0; }
const char* gride_stop_file(GRIDEDebugger* debugger) { return debugger ? debugger->stop.file.c_str() : ""; }
int gride_stop_line(GRIDEDebugger* debugger) { return debugger ? static_cast<int>(debugger->stop.line) : 0; }
const char* gride_stop_function(GRIDEDebugger* debugger) { return debugger ? debugger->stop.function.c_str() : ""; }
const char* gride_stop_said(GRIDEDebugger* debugger) { return debugger ? debugger->stop.said.c_str() : ""; }

int gride_stop_no_source(GRIDEDebugger* debugger) {
    if (!debugger) return 0;
    return editor::dbg_stoppedWithNoSource(debugger->stop.said) ? 1 : 0;
}

const char* gride_stop_output(GRIDEDebugger* debugger) {
    if (!debugger) return "";

    if (debugger->shm.ownsTheStop()) {
        debugger->output = debugger->stop.said;
        return debugger->output.c_str();
    }

    debugger->output = editor::dbg_programOutput(debugger->debugger.kind(), debugger->stop.said);
    return debugger->output.c_str();
}

int gride_locals_count(GRIDEDebugger* debugger) {
    if (!debugger) return 0;
    return static_cast<int>(debugger->locals.size());
}

namespace {
bool holds(GRIDEDebugger* debugger, int index) {
    return index >= 0 && static_cast<size_t>(index) < debugger->locals.size();
}
}

const char* gride_local_name(GRIDEDebugger* debugger, int index) {
    if (!debugger) return "";
    return holds(debugger, index) ? debugger->locals[index].name.c_str() : "";
}
const char* gride_local_type(GRIDEDebugger* debugger, int index) {
    if (!debugger) return "";
    return holds(debugger, index) ? debugger->locals[index].type.c_str() : "";
}
const char* gride_local_value(GRIDEDebugger* debugger, int index) {
    if (!debugger) return "";
    return holds(debugger, index) ? debugger->locals[index].value.c_str() : "";
}

const char* gride_local_address(GRIDEDebugger* debugger, int index) {
    if (!debugger) return "";
    return holds(debugger, index) ? debugger->locals[index].address.c_str() : "";
}

int gride_stack_count(GRIDEDebugger* debugger) {
    if (!debugger) return 0;
    return static_cast<int>(debugger->stack.size());
}

namespace {
bool reaches(GRIDEDebugger* debugger, int index) {
    return index >= 0 && static_cast<size_t>(index) < debugger->stack.size();
}
}

const char* gride_stack_function(GRIDEDebugger* debugger, int index) {
    if (!debugger) return "";
    return reaches(debugger, index) ? debugger->stack[index].function.c_str() : "";
}
const char* gride_stack_file(GRIDEDebugger* debugger, int index) {
    if (!debugger) return "";
    return reaches(debugger, index) ? debugger->stack[index].file.c_str() : "";
}
int gride_stack_line(GRIDEDebugger* debugger, int index) {
    if (!debugger) return 0;
    return reaches(debugger, index) ? static_cast<int>(debugger->stack[index].line) : 0;
}

const char* gride_stack_text(GRIDEDebugger* debugger, int index) {
    if (!debugger) return "";

    debugger->frameLine =
        reaches(debugger, index)
            ? editor::dbg_frameLine(debugger->stack[index],
                                    static_cast<size_t>(index) == debugger->looking)
            : std::string();
    return debugger->frameLine.c_str();
}

const char* gride_local_text(GRIDEDebugger* debugger, int index) {
    if (!debugger) return "";
    debugger->variableLine = holds(debugger, index)
                                 ? editor::dbg_variableLine(debugger->locals[index])
                                 : std::string();
    return debugger->variableLine.c_str();
}

int gride_locals_on_line(GRIDEDebugger* debugger, const char* line) {
    if (!debugger) return 0;
    size_t which = editor::dbg_variableOnLine(debugger->locals, line ? line : "");
    return which < debugger->locals.size() ? static_cast<int>(which) : -1;
}

int gride_set_variable(GRIDEDebugger* debugger, const char* name, const char* value) {
    if (!debugger) return 0;
    debugger->complaint.clear();
    if (!debugger->debugger.setVariable(name ? name : "", value ? value : "",
                                        &debugger->complaint))
        return 0;

    debugger->locals = debugger->debugger.locals();
    return 1;
}

const char* gride_set_complaint(GRIDEDebugger* debugger) { return debugger ? debugger->complaint.c_str() : ""; }

void gride_watch_add(GRIDEDebugger* debugger, const char* expression) {
    if (!debugger) return;
    debugger->debugger.addWatch(expression ? expression : "");
}

int gride_watch_count(GRIDEDebugger* debugger) {
    if (!debugger) return 0;
    return static_cast<int>(debugger->debugger.watches().size());
}

namespace {
bool watched(GRIDEDebugger* debugger, int index) {
    return index >= 0 && static_cast<size_t>(index) < debugger->debugger.watches().size();
}
}

const char* gride_watch_text(GRIDEDebugger* debugger, int index) {
    if (!debugger) return "";
    debugger->watchLine = watched(debugger, index)
                              ? editor::dbg_watchLine(debugger->debugger.watches()[index])
                              : std::string();
    return debugger->watchLine.c_str();
}

const char* gride_watch_expression(GRIDEDebugger* debugger, int index) {
    if (!debugger) return "";
    return watched(debugger, index)
               ? debugger->debugger.watches()[index].expression.c_str()
               : "";
}

const char* gride_watch_value(GRIDEDebugger* debugger, int index) {
    if (!debugger) return "";
    return watched(debugger, index) ? debugger->debugger.watches()[index].value.c_str() : "";
}

int gride_watch_ok(GRIDEDebugger* debugger, int index) {
    if (!debugger) return 0;
    return watched(debugger, index) && debugger->debugger.watches()[index].ok ? 1 : 0;
}

const char* gride_watch_address(GRIDEDebugger* debugger, int index) {
    if (!debugger) return "";
    return watched(debugger, index) ? debugger->debugger.watches()[index].address.c_str() : "";
}

const char* gride_watch_type(GRIDEDebugger* debugger, int index) {
    if (!debugger) return "";
    return watched(debugger, index) ? debugger->debugger.watches()[index].type.c_str() : "";
}

int gride_watch_on_line(GRIDEDebugger* debugger, const char* line) {
    if (!debugger) return 0;
    size_t which = editor::dbg_watchOnLine(debugger->debugger.watches(), line ? line : "");
    return which < debugger->debugger.watches().size() ? static_cast<int>(which) : -1;
}

void gride_watch_set(GRIDEDebugger* debugger, int index, const char* expression) {
    if (!debugger) return;
    if (!watched(debugger, index)) return;
    debugger->debugger.setWatch(static_cast<size_t>(index), expression ? expression : "");
}

int gride_debugger_look_at(GRIDEDebugger* debugger, int which) {
    if (!debugger) return 0;
    if (!reaches(debugger, which)) return 0;

    if (debugger->shm.running()) {
        debugger->looking = 0;
        return which == 0 ? 1 : 0;
    }

    if (!debugger->debugger.selectFrame(static_cast<size_t>(which))) return 0;

    debugger->looking = static_cast<size_t>(which);
    debugger->locals = debugger->debugger.locals();
    return 1;
}

const char* gride_locals_none_because(GRIDEDebugger* debugger) {
    if (!debugger) return "";
    debugger->refusal = debugger->shm.running()
                            ? "  (" + std::string(shalimar::saysWhereOnly()) + ")"
                            : std::string("  (nothing in scope here)");
    return debugger->refusal.c_str();
}

const char* gride_cannot_watch(GRIDEDebugger* debugger) {
    if (!debugger) return "";

    debugger->refusal = debugger->shm.running()
                            ? std::string(shalimar::saysWhereOnly()) +
                                  " - nothing to watch with"
                            : std::string();
    return debugger->refusal.c_str();
}

const char* gride_cannot_walk_stack(GRIDEDebugger* debugger) {
    if (!debugger) return "";

    debugger->refusal = debugger->shm.running() ? std::string(shalimar::saysHowDeepOnly())
                                                : std::string();
    return debugger->refusal.c_str();
}

const char* gride_stop_line_text(const char* file, int line, const char* function) {

    scratch() = editor::dbg_stopLine(file ? file : "",
                                     static_cast<size_t>(line < 0 ? 0 : line),
                                     function ? function : "");
    return scratch().c_str();
}

int gride_looking_at(GRIDEDebugger* debugger) {
    if (!debugger) return 0;
    return static_cast<int>(debugger->looking);
}

const char* gride_looking_text(GRIDEDebugger* debugger) {
    if (!debugger) return "";
    debugger->lookingLine =
        (debugger->looking > 0 && debugger->looking < debugger->stack.size())
            ? editor::dbg_lookingAt(debugger->stack[debugger->looking])
            : std::string();
    return debugger->lookingLine.c_str();
}

int gride_stack_on_line(GRIDEDebugger* debugger, const char* line) {
    if (!debugger) return 0;
    size_t which = editor::dbg_frameOnLine(debugger->stack, line ? line : "");
    return which < debugger->stack.size() ? static_cast<int>(which) : -1;
}

int gride_begin_from_what_is_there(GRIDEProject* project, const char* directory) {
    if (!project) return 0;
    project->last = editor::beginFromWhatIsThere(project->project, directory ? directory : "");
    project->answer = project->last.message;
    return project->last.ok ? 1 : 0;
}

const char* gride_last_project(void) {

    scratch() = editor::settings::lastProject();
    return scratch().c_str();
}

const char* gride_recent_project(int index) {
    std::vector<std::string> recent = editor::settings::recentProjects();
    scratch() = (index >= 0 && index < static_cast<int>(recent.size())) ? recent[static_cast<size_t>(index)]
                                                                         : std::string();
    return scratch().c_str();
}

const char* gride_recent_file(int index) {
    std::vector<std::string> recent = editor::settings::recentFiles();
    scratch() = (index >= 0 && index < static_cast<int>(recent.size())) ? recent[static_cast<size_t>(index)]
                                                                         : std::string();
    return scratch().c_str();
}

int gride_remember_file(const char* path) {
    return editor::settings::rememberFile(path ? path : "") ? 1 : 0;
}

int gride_remember_project(const char* directory) {
    return editor::settings::rememberProject(directory ? directory : "") ? 1 : 0;
}

const char* gride_demo_directory(void) {
    scratch() = editor::demoDirectory();
    return scratch().c_str();
}

int gride_project_builds(GRIDEProject* project) {
    return project && project->project.builds() ? 1 : 0;
}

int gride_project_target_ready(GRIDEProject* project) {
    if (!project) return 0;
    project->prepared = false;

    // A CCS project is read again for every build, so an edit made in CCS is what is built.
    if (project->project.isCcs() && !project->project.reloadIfCcs(project->why)) {
        project->detail.clear();
        project->sources.clear();
        project->parts.clear();
        return 0;
    }
    project->sources.clear();
    bool ok = project->project.targetParts(project->parts, project->why, &project->detail);
    if (ok)
        for (size_t i = 0; i < project->parts.size(); ++i)
            for (size_t f = 0; f < project->parts[i].sources.size(); ++f)
                project->sources.push_back(project->parts[i].sources[f]);

    project->language = ok && !project->parts.empty()
                            ? static_cast<int>(project->parts[0].lang)
                            : static_cast<int>(editor::LangPlain);
    project->program = ok ? project->project.targetProgram() : std::string();
    project->prepared = ok;
    return ok ? 1 : 0;
}

int gride_project_target_parts(GRIDEProject* project) {
    return project ? static_cast<int>(project->parts.size()) : 0;
}

const char* gride_project_part_group(GRIDEProject* project, int index) {
    if (!project || index < 0 || index >= static_cast<int>(project->parts.size())) return "";
    return project->parts[static_cast<size_t>(index)].group.c_str();
}

int gride_project_part_language(GRIDEProject* project, int index) {
    if (!project || index < 0 || index >= static_cast<int>(project->parts.size())) return 0;
    return static_cast<int>(project->parts[static_cast<size_t>(index)].lang);
}

int gride_project_part_toolchain(GRIDEProject* project, int index, const char* cc1,
                               const char* cl, const char* shc, const char* cxx1, int kind) {
    if (!project || index < 0 || index >= static_cast<int>(project->parts.size()))
        return static_cast<int>(editor::ToolAuto);
    editor::Toolchain tool;
    tool.kind = static_cast<editor::ToolchainKind>(kind);
    if (cc1 && *cc1) tool.cc1 = cc1;
    if (cl && *cl) tool.cl = cl;
    if (shc && *shc) tool.shc = shc;
    if (cxx1 && *cxx1) tool.cxx1 = cxx1;
    return static_cast<int>(
        editor::toolchainOf(tool, project->parts[static_cast<size_t>(index)]));
}

const char* gride_project_target_why(GRIDEProject* project) {
    return project ? project->why.c_str() : "";
}

const char* gride_project_target_detail(GRIDEProject* project) {
    return project ? project->detail.c_str() : "";
}

int gride_project_target_language(GRIDEProject* project) {
    return project ? project->language : 0;
}

int gride_project_target_sources(GRIDEProject* project) {
    return project ? static_cast<int>(project->sources.size()) : 0;
}

const char* gride_project_target_source(GRIDEProject* project, int index) {
    if (!project || index < 0 || index >= static_cast<int>(project->sources.size())) return "";
    return project->sources[static_cast<size_t>(index)].c_str();
}

const char* gride_project_target_program(GRIDEProject* project) {
    return project ? project->program.c_str() : "";
}

int gride_project_debug_plan(GRIDEProject* project, const char* cc1, const char* cl,
                           const char* shc, const char* cxx1, int kind, const char* arch) {
    if (!project) return 0;
    project->plan = editor::DebugPlan();
    project->whyNot.clear();
    if (!gride_project_target_ready(project)) {
        project->whyNot = project->why;
        return 0;
    }

    editor::Toolchain tool = toolFrom(project, cc1, cl, shc, cxx1);
    tool.kind = static_cast<editor::ToolchainKind>(kind);

    project->plan = editor::dbg_planFor(tool, project->parts, arch ? arch : "");
    if (project->plan.possible()) return 1;

    project->whyNot = editor::dbg_whyNot(project->plan.kind, arch ? arch : "");
    return 0;
}

int gride_project_debug_kind(GRIDEProject* project) {
    return project ? static_cast<int>(project->plan.kind) : static_cast<int>(editor::ToolAuto);
}

const char* gride_project_why_not_debug(GRIDEProject* project) {
    return project ? project->whyNot.c_str() : "";
}

int gride_project_blind_groups(GRIDEProject* project) {
    return project ? static_cast<int>(project->plan.blind.size()) : 0;
}

const char* gride_project_blind_group(GRIDEProject* project, int index) {
    if (!project || index < 0 || index >= static_cast<int>(project->plan.blind.size()))
        return "";
    return project->plan.blind[static_cast<size_t>(index)].c_str();
}

GRIDEBuild* gride_build_target(GRIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1,
                           int kind, const char* arch, int config) {
    if (!project) return 0;
    // What the window prepared, never a second reading on this thread; prepared here only when it was not.
    if (!project->prepared && !gride_project_target_ready(project)) return 0;
    project->prepared = false;

    editor::Toolchain tool = toolFrom(project, cc1, cl, shc, cxx1, config);

    tool.kind = static_cast<editor::ToolchainKind>(kind);
    // An Emulate asked for before this build builds no .out; the .pro's emulateOnly the same (R5, D2).
    tool.emulating = nextRunner.load() == GRIDE_RUN_EMULATOR;
    builtEmulateOnly = tool.emulateOnly;

    GRIDEBuild* out = new GRIDEBuild();
    editor::BuildScope scope;
    editor::Built made = editor::buildParts(
        tool, project->parts, arch ? arch : "",
        static_cast<editor::Configuration>(config), project->program);
    out->stopped = editor::buildCancelled();

    out->built.ok = made.ok;
    out->built.diag = made.diag;
    out->built.output = made.output;
    out->program = made.program;
    out->shalimar = made.shalimar;
    return out;
}

GRIDERan* gride_run_built(const char* program) {
    GRIDERan* out = new GRIDERan();
    editor::BuildScope scope;
    out->ran = editor::runBuilt(program ? program : "");
    out->stopped = editor::buildCancelled();
    return out;
}

struct GRIDEConversion {
    editor::Conversion made;
    bool stopped = false;
};

int gride_converts_from(int language, int* toShalimar) {
    bool wanted = false;
    if (!editor::convertsFrom(static_cast<editor::Language>(language), &wanted)) {
        return 0;
    }
    if (toShalimar) *toShalimar = wanted ? 1 : 0;
    return 1;
}

char* gride_find_converter(void) {
    return give(editor::findConverter());
}

char* gride_converted_name(const char* source, int toShalimar) {
    return give(editor::convertedName(source ? source : "", toShalimar != 0));
}

GRIDEConversion* gride_convert(const char* converter, const char* source,
                                   const char* output, int toShalimar) {
    GRIDEConversion* out = new GRIDEConversion();
    editor::BuildScope scope;
    out->made = editor::convert(converter ? converter : "", source ? source : "",
                                output ? output : "", toShalimar != 0);
    out->stopped = editor::buildCancelled();
    return out;
}

void gride_conversion_free(GRIDEConversion* made) { delete made; }
int gride_conversion_ran(GRIDEConversion* made) { return made && made->made.ran ? 1 : 0; }
int gride_conversion_ok(GRIDEConversion* made) { return made && made->made.ok ? 1 : 0; }
const char* gride_conversion_produced(GRIDEConversion* made) {
    if (!made) return "";
    return made->made.produced.c_str();
}
const char* gride_conversion_output(GRIDEConversion* made) {
    if (!made) return "";
    return made->made.output.c_str();
}

GRIDEBuild* gride_build(GRIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1, int kind, const char* source,
                    int language, const char* arch, int config) {
    editor::Toolchain tool = toolFrom(project, cc1, cl, shc, cxx1);

    GRIDEBuild* out = new GRIDEBuild();
    editor::BuildScope scope;
    out->built = editor::build(tool, static_cast<editor::ToolchainKind>(kind),
                               source ? source : "",
                               static_cast<editor::Language>(language),
                               arch ? arch : "",
                               static_cast<editor::Configuration>(config));
    out->assembly = join(out->built.asmLines);
    out->stopped = editor::buildCancelled();
    return out;
}

void gride_build_free(GRIDEBuild* built) { delete built; }

int gride_build_ok(GRIDEBuild* built) { return built && built->built.ok ? 1 : 0; }
const char* gride_build_made(GRIDEBuild* built) { return built ? built->program.c_str() : ""; }
int gride_build_made_shalimar(GRIDEBuild* built) { return built && built->shalimar ? 1 : 0; }
const char* gride_build_output(GRIDEBuild* built) { return built ? built->built.output.c_str() : ""; }
const char* gride_build_assembly(GRIDEBuild* built) { return built ? built->assembly.c_str() : ""; }
int gride_build_assembly_lines(GRIDEBuild* built) {
    if (!built) return 0;
    return static_cast<int>(built->built.asmLines.size());
}
int gride_build_has_error(GRIDEBuild* built) { return built && built->built.diag.present ? 1 : 0; }
const char* gride_build_error_file(GRIDEBuild* built) {
    return built ? built->built.diag.file.c_str() : "";
}

int gride_build_error_line(GRIDEBuild* built) {
    if (!built) return 0;
    return static_cast<int>(built->built.diag.line);
}
int gride_build_error_column(GRIDEBuild* built) {
    if (!built) return 0;
    return static_cast<int>(built->built.diag.col);
}
const char* gride_build_error_message(GRIDEBuild* built) {
    if (!built) return "";
    return built->built.diag.message.c_str();
}

// ---- a program that runs while the window watches (bridge.h) --------------------------------

// Two locks: `state` for what the run has come to, which Stop takes and never waits in; `input`
// for the input's handle, which a send may hold while the program is slow to read.
struct GRIDERunning {
    GRIDEOutput onOutput;
    void* user;

    bool fromSource;
    editor::Toolchain tool;
    editor::ToolchainKind kind;
    std::string source;
    editor::Language language;
    std::string arch;
    editor::Configuration config;
    std::string program;
    bool shalimar = false;      // a built Shalimar program, which the emulator runs beside its runtime
    std::vector<std::string> args;  // its command line, Build > Command-line arguments
    int runner = GRIDE_RUN_PROGRAM;  // the program, the simulator on its .out, or both compared (5.0)

    editor::Process process;
    std::thread worker;
    std::mutex state;
    std::mutex input;
    std::condition_variable over;

    std::string waiting;         // sent before the program started
    bool closeWanted = false;
    bool stopWanted = false;
    bool started = false;
    bool finished = false;
    std::atomic<int> done{0};

    bool built = false;
    bool ran = false;
    int status = 0;
    bool stopped = false;
    bool verified = false;      // Verify ran both itself; status is its answer
    editor::Diagnostic diag;
    std::string buildOutput;
};

namespace {

void runningLine(void* context, const std::string& line) {
    GRIDERunning* running = static_cast<GRIDERunning*>(context);
    std::string text = line + "\n";
    if (running->onOutput)
        running->onOutput(running->user, text.data(), static_cast<int>(text.size()), GRIDE_STREAM_BUILD);
}

void runningChunk(void* context, const char* bytes, size_t size, bool isStderr) {
    GRIDERunning* running = static_cast<GRIDERunning*>(context);
    if (running->onOutput)
        running->onOutput(running->user, bytes, static_cast<int>(size),
                          isStderr ? GRIDE_STREAM_ERR : GRIDE_STREAM_OUT);
}


void runningSay(void* context, const std::string& line) {
    GRIDERunning* running = static_cast<GRIDERunning*>(context);
    std::string text = line + "\n";
    if (running->onOutput)
        running->onOutput(running->user, text.data(), static_cast<int>(text.size()), GRIDE_STREAM_OUT);
}

void runTheProgram(GRIDERunning* running) {
    editor::BuildScope scope;
    bool stopFirst = false;
    {
        std::lock_guard<std::mutex> held(running->state);
        stopFirst = running->stopWanted;
    }
    editor::Built made;
    bool shalimar = running->shalimar;
    std::string program = running->program;
    if (running->fromSource && !stopFirst) {
        // A single file has no .pro: on tms6747 its Run is the simulator's, and links the .out (R5).
        running->runner = editor::runnerFor(running->runner, running->arch, false);
        running->tool.emulateOnly = false;
        running->tool.emulating = running->runner == GRIDE_RUN_EMULATOR;
        running->tool.linkSingleFile = running->runner == GRIDE_RUN_SIMULATOR || running->runner == GRIDE_RUN_VERIFY;
        made = editor::buildProgram(running->tool, running->kind, running->source, running->language,
                                    running->arch, running->config, runningLine, running);
        running->buildOutput = made.output;
        running->diag = made.diag;
        program = made.ok ? made.program : std::string();
        shalimar = made.shalimar;
    }

    {
        std::lock_guard<std::mutex> held(running->state);
        std::lock_guard<std::mutex> in(running->input);
        running->built = !program.empty() && !stopFirst;
        // A project's program: a tms6747 Run is the .out on sim6747, or vm6747's when the .pro says emulateOnly.
        if (running->runner == GRIDE_RUN_PROGRAM && editor::isEmulatedProgram(program))
            running->runner = builtEmulateOnly ? GRIDE_RUN_EMULATOR : GRIDE_RUN_SIMULATOR;
        if (running->built && running->runner == GRIDE_RUN_EMULATOR && !editor::isEmulatedProgram(program)) {
            runningSay(running, "Emulate on vm6747 is for a tms6747 build - choose tms6747 under Target");
            running->status = 2;
            running->built = false;
        } else if (running->built && (running->runner == GRIDE_RUN_SIMULATOR || running->runner == GRIDE_RUN_VERIFY)) {
            // The simulator, or Verify: neither is for a program the emulator does not run.
            std::string why = editor::isEmulatedProgram(program) ? editor::simulationMissing(program)
                : "Verify is for a tms6747 build - choose tms6747 under Target";
            if (!why.empty()) { runningSay(running, why); running->status = 2; }
        }
        if (running->built && running->runner == GRIDE_RUN_VERIFY && !running->stopWanted && !editor::buildCancelled()
            && editor::isEmulatedProgram(program) && editor::simulationMissing(program).empty()) {
            editor::Ran both = editor::verifyBuilt(program, shalimar, running->args, runningSay, running);
            running->verified = true;
            running->status = both.status;
        } else if (running->built && running->runner == GRIDE_RUN_SIMULATOR) {
            if (!running->stopWanted && !editor::buildCancelled() && editor::isEmulatedProgram(program)
                && editor::simulationMissing(program).empty()) {
                if (!running->args.empty())
                    runningSay(running, "[run] the simulator takes no command line - Emulate on vm6747 hands them over");
                if (running->fromSource) runningSay(running, editor::runnerLine(editor::RunSimulator, program));
                running->ran = editor::startSimulated(running->process, program);
            }
        } else if (running->built && (running->runner == GRIDE_RUN_PROGRAM || running->runner == GRIDE_RUN_EMULATOR)
                   && !running->stopWanted && !editor::buildCancelled()) {
            if (running->runner == GRIDE_RUN_EMULATOR && running->fromSource)
                runningSay(running, editor::runnerLine(editor::RunEmulator, program));
            running->ran = editor::startProgram(running->process, program, shalimar, running->args);
            if (running->ran) {
                if (!running->waiting.empty())
                    running->process.send(running->waiting.data(), running->waiting.size());
                if (running->closeWanted) running->process.closeInput();
            }
        }
        running->waiting.clear();
        running->started = true;
    }

    // Read until the output closes, then wait - with no lock held, so Stop can still end a program
    // that closed its output and went on - and only then let the process go, under both locks.
    if (running->ran) {
        editor::pumpProgram(running->process, runningChunk, running);
        while (!running->process.ended(0)) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (running->fromSource) editor::removeProgram(made);

    {
        std::lock_guard<std::mutex> held(running->state);
        std::lock_guard<std::mutex> in(running->input);
        if (!running->verified) running->status = running->ran ? running->process.finish() : running->status;
        running->stopped = running->stopWanted;
        running->finished = true;
        running->done = 1;
    }
    running->over.notify_all();
    if (running->onOutput) running->onOutput(running->user, NULL, 0, GRIDE_STREAM_OUT);
}

GRIDERunning* startRunning(GRIDERunning* running) {
    running->runner = nextRunner.exchange(GRIDE_RUN_PROGRAM);
    try {
        running->worker = std::thread(runTheProgram, running);
    } catch (...) {
        delete running;
        return 0;
    }
    return running;
}

}

void gride_run_next(int runner) {
    nextRunner = runner == GRIDE_RUN_SIMULATOR || runner == GRIDE_RUN_VERIFY || runner == GRIDE_RUN_EMULATOR
                     ? runner : GRIDE_RUN_PROGRAM;
}

int gride_simulator_here(void) { return editor::simulatorProgram().empty() ? 0 : 1; }

GRIDERunning* gride_run_start(GRIDEProject* project, const char* cc1, const char* cl, const char* shc,
                            const char* cxx1, int kind, const char* source, int language,
                            const char* arch, int config, GRIDEOutput onOutput, void* user) {
    GRIDERunning* running = new GRIDERunning();
    running->onOutput = onOutput;
    running->user = user;
    running->fromSource = true;
    running->tool = toolFrom(project, cc1, cl, shc, cxx1, config);
    running->kind = static_cast<editor::ToolchainKind>(kind);
    running->source = source ? source : "";
    running->language = static_cast<editor::Language>(language);
    running->arch = arch ? arch : "";
    running->config = static_cast<editor::Configuration>(config);
    return startRunning(running);
}

GRIDERunning* gride_run_built_start(const char* program, GRIDEOutput onOutput, void* user) {
    return gride_run_made_start(program, 0, onOutput, user);
}

GRIDERunning* gride_run_made_start(const char* program, int shalimar, GRIDEOutput onOutput, void* user) {
    GRIDERunning* running = new GRIDERunning();
    running->shalimar = shalimar != 0;
    running->onOutput = onOutput;
    running->user = user;
    running->fromSource = false;
    running->kind = editor::ToolAuto;
    running->language = editor::LangPlain;
    running->config = editor::ConfigDebug;
    running->program = program ? program : "";
    return startRunning(running);
}

namespace {

void argumentsFrom(const char* line, const char* base, std::vector<std::string>& out) {
    out = editor::Project::splitArguments(line ? line : "");
    const std::string root = base ? base : "";
    for (size_t i = 0; i < out.size() && !root.empty(); ++i) {
        const std::string full = editor::path::absolute(editor::path::join(root, out[i]));
        if (!out[i].empty() && out[i][0] != '-' && editor::path::exists(full)) out[i] = full;
    }
}

}

GRIDERunning* gride_run_made_start_line(const char* program, int shalimar, const char* line, const char* base,
                                      GRIDEOutput onOutput, void* user) {
    GRIDERunning* running = new GRIDERunning();
    running->shalimar = shalimar != 0;
    running->onOutput = onOutput;
    running->user = user;
    running->fromSource = false;
    running->kind = editor::ToolAuto;
    running->language = editor::LangPlain;
    running->config = editor::ConfigDebug;
    running->program = program ? program : "";
    argumentsFrom(line, base, running->args);
    return startRunning(running);
}

GRIDERunning* gride_run_start_line(GRIDEProject* project, const char* cc1, const char* cl, const char* shc,
                                 const char* cxx1, int kind, const char* source, int language,
                                 const char* arch, int config, const char* line, const char* base,
                                 GRIDEOutput onOutput, void* user) {
    GRIDERunning* running = new GRIDERunning();
    running->onOutput = onOutput;
    running->user = user;
    running->fromSource = true;
    running->tool = toolFrom(project, cc1, cl, shc, cxx1, config);
    running->kind = static_cast<editor::ToolchainKind>(kind);
    running->source = source ? source : "";
    running->language = static_cast<editor::Language>(language);
    running->arch = arch ? arch : "";
    running->config = static_cast<editor::Configuration>(config);
    argumentsFrom(line, base, running->args);
    return startRunning(running);
}

char* gride_project_arguments(GRIDEProject* project) {
    return give(project && project->project.loaded() ? project->project.argumentsText() : std::string());
}

int gride_project_set_arguments(GRIDEProject* project, const char* line) {
    if (!project || !project->project.loaded()) return 0;
    std::string error;
    return project->project.rememberArguments(line ? line : "", error) ? 1 : 0;
}

char* gride_run_line(GRIDEProject* project, const char* program, const char* line) {
    std::string shown = program ? program : "";
    // A tms6747 program is run by sim6747 or vm6747, and the line names which (R5).
    if (editor::isEmulatedProgram(shown)) {
        int runner = nextRunner.load();
        if (runner == GRIDE_RUN_PROGRAM) runner = builtEmulateOnly ? GRIDE_RUN_EMULATOR : GRIDE_RUN_SIMULATOR;
        if (runner == GRIDE_RUN_VERIFY)
            return give(editor::runnerLine(editor::RunEmulator, shown) + ", then " +
                        editor::runnerLine(editor::RunSimulator, shown).substr(2) + ", compared");
        std::string said = editor::runnerLine(runner, shown);
        if (runner == GRIDE_RUN_EMULATOR && line && *line) said += std::string(" ") + line;
        return give(said);
    }
    if (project && project->project.loaded()) {
        // Under the project's folder, by its name there; elsewhere - the emulator's temporary build - in full.
        const std::string rel = project->project.relative(shown);
        if (!rel.empty() && rel.compare(0, 2, "..") != 0 && rel.find(':') == std::string::npos && rel[0] != '/') shown = rel;
    }
    const std::string args = line ? line : "";
    if (!args.empty()) shown += " " + args;
    return give("$ " + shown);
}

char* gride_project_run_line(GRIDEProject* project, const char* program) {
    std::string shown = program ? program : "";
    if (project && project->project.loaded()) {
        shown = project->project.relative(shown);
        const std::string args = project->project.argumentsText();
        if (!args.empty()) shown += " " + args;
    }
    return give("$ " + shown);
}

int gride_running_send(GRIDERunning* running, const char* bytes, int size) {
    if (!running || !bytes || size < 0) return 0;
    std::lock_guard<std::mutex> in(running->input);
    if (running->finished || running->closeWanted) return 0;
    if (!running->started) {
        running->waiting.append(bytes, static_cast<size_t>(size));
        return 1;
    }
    if (!running->ran) return 0;
    return running->process.send(bytes, static_cast<size_t>(size)) ? 1 : 0;
}

void gride_running_close_input(GRIDERunning* running) {
    if (!running) return;
    std::lock_guard<std::mutex> in(running->input);
    if (running->finished || running->closeWanted) return;
    running->closeWanted = true;
    if (running->started && running->ran) running->process.closeInput();
}

void gride_running_stop(GRIDERunning* running) {
    if (!running) return;
    std::lock_guard<std::mutex> held(running->state);
    if (running->finished) return;
    running->stopWanted = true;
    if (!running->started) editor::cancelBuilds();
    else if (running->ran) running->process.kill();
}

int gride_running_wait(GRIDERunning* running, int timeoutMs) {
    if (!running) return 1;
    std::unique_lock<std::mutex> held(running->state);
    if (timeoutMs < 0) {
        running->over.wait(held, [running] { return running->done.load() != 0; });
        return 1;
    }
    return running->over.wait_for(held, std::chrono::milliseconds(timeoutMs),
                                  [running] { return running->done.load() != 0; }) ? 1 : 0;
}

int gride_running_done(GRIDERunning* running) { return running && running->done.load() ? 1 : 0; }

namespace {
bool isOver(GRIDERunning* running) { return running && running->done.load() != 0; }
}

int gride_running_built(GRIDERunning* running) { return isOver(running) && running->built ? 1 : 0; }
int gride_running_ran(GRIDERunning* running) { return isOver(running) && running->ran ? 1 : 0; }
int gride_running_status(GRIDERunning* running) { return isOver(running) ? running->status : 0; }
int gride_running_stopped(GRIDERunning* running) { return isOver(running) && running->stopped ? 1 : 0; }
const char* gride_running_build_output(GRIDERunning* running) {
    return isOver(running) ? running->buildOutput.c_str() : "";
}
int gride_running_has_error(GRIDERunning* running) {
    return isOver(running) && running->diag.present ? 1 : 0;
}
const char* gride_running_error_file(GRIDERunning* running) {
    return isOver(running) ? running->diag.file.c_str() : "";
}
int gride_running_error_line(GRIDERunning* running) {
    return isOver(running) ? static_cast<int>(running->diag.line) : 0;
}
int gride_running_error_column(GRIDERunning* running) {
    return isOver(running) ? static_cast<int>(running->diag.col) : 0;
}
const char* gride_running_error_message(GRIDERunning* running) {
    return isOver(running) ? running->diag.message.c_str() : "";
}

void gride_running_free(GRIDERunning* running) {
    if (!running) return;
    gride_running_stop(running);
    if (running->worker.joinable()) running->worker.join();
    delete running;
}

void gride_cancel_builds(void) { editor::cancelBuilds(); }
int gride_build_stopped(GRIDEBuild* built) { return built && built->stopped ? 1 : 0; }
int gride_ran_stopped(GRIDERan* ran) { return ran && ran->stopped ? 1 : 0; }
int gride_conversion_stopped(GRIDEConversion* made) { return made && made->stopped ? 1 : 0; }

}


// ---- Compiler Options (options.h) ----------------------------------------------------------------

int gride_option_count(void) { return static_cast<int>(editor::options::count()); }
static const editor::options::Def* optionAt(int index) {
    return index >= 0 && static_cast<size_t>(index) < editor::options::count() ? &editor::options::at(index) : 0;
}
const char* gride_option_id(int index) { const editor::options::Def* d = optionAt(index); return d ? d->id : ""; }
const char* gride_option_tab(int index) { const editor::options::Def* d = optionAt(index); return d ? d->tab : ""; }
const char* gride_option_label(int index) { const editor::options::Def* d = optionAt(index); return d ? d->label : ""; }
int gride_option_control(int index) { const editor::options::Def* d = optionAt(index); return d ? static_cast<int>(d->control) : 0; }
const char* gride_option_choices(int index) { const editor::options::Def* d = optionAt(index); return d ? d->choices : ""; }
const char* gride_option_hint(int index) { const editor::options::Def* d = optionAt(index); return d ? d->hint : ""; }
int gride_option_tab_count(void) { return static_cast<int>(editor::options::tabCount()); }
const char* gride_option_tab_name(int tab) { return tab >= 0 ? editor::options::tabName(static_cast<size_t>(tab)) : ""; }

static editor::Configuration configOf(int config) {
    return config == GRIDE_CONFIG_RELEASE ? editor::ConfigRelease : editor::ConfigDebug;
}

// The draft a window with no GRIDEProject at all edits: the installation's. A pointer, like every
// native global the window links (settings.cpp).
static editor::options::Store& draftOf(GRIDEProject* project) {
    static editor::options::Store* alone = new editor::options::Store();
    return project ? project->draft : *alone;
}

void gride_options_begin(GRIDEProject* project) {
    draftOf(project) = project && project->project.loaded() ? project->project.compilerOptions()
                                                            : editor::options::installation();
}

const char* gride_options_value(GRIDEProject* project, int config, const char* id) {
    scratch() = id ? draftOf(project).value(configOf(config), id) : std::string();
    return scratch().c_str();
}

void gride_options_set(GRIDEProject* project, int config, const char* id, const char* value) {
    if (id) draftOf(project).set(configOf(config), id, value ? value : "");
}

void gride_options_reset(GRIDEProject* project, int config) {
    draftOf(project).reset(configOf(config));
}

int gride_options_available(const char* id, const char* arch) {
    std::string why;
    return id && arch && editor::options::available(id, arch, why) ? 1 : 0;
}

const char* gride_options_why(const char* id, const char* arch) {
    std::string why;
    if (id && arch) editor::options::available(id, arch, why);
    scratch() = why;
    return scratch().c_str();
}

const char* gride_options_preview(GRIDEProject* project, int config, int tab, const char* arch) {
    const editor::ToolchainKind kinds[] = { editor::ToolCxx1, editor::ToolCc1, editor::ToolCxx1, editor::ToolShc };
    const char* const names[] = { "cpp11", "c90", "cpp11", "shalimar" };
    std::string a = arch ? arch : "";
    int t = tab >= 0 && tab < 4 ? tab : 0;
    std::string line = std::string(names[t]) + (kinds[t] == editor::ToolShc ? " --target=" : " -arch ") + a;
    line += editor::options::flags(draftOf(project), kinds[t], configOf(config), a);
    scratch() = line + " <sources>";
    return scratch().c_str();
}

int gride_options_commit(GRIDEProject* project) {
    if (!project || !project->project.loaded())
        return editor::settings::rememberCompilerOptions(draftOf(project).toJson()) ? 1 : 0;
    // A CCS project's options are CCS's to write; the dialog shows them and changes nothing.
    if (project->project.isCcs()) return 0;
    project->project.setCompilerOptions(project->draft);
    project->last = editor::saveProject(project->project);
    return project->last.ok ? 1 : 0;
}
