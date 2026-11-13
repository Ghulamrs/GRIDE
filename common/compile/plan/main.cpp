// gride-plan.exe - the block's command-line front end, and the first user of compile.dll. It prints the plan
// of a build as the build log lays it out before anything runs; -steps prints every step for a script; -build
// then runs the steps, one after another, as an outlook would. The block plans; this program, like an outlook,
// runs.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../gride_compile.h"

using namespace gride::compile;

namespace {

std::wstring wide(const std::string &s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), 0, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::string narrow(const std::wstring &w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), 0, 0, 0, 0);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, 0, 0);
    return s;
}

void out(const std::string &line) {
    fwrite(line.data(), 1, line.size(), stdout);
    fputc('\n', stdout);
}

std::string fullPath(const std::string &path) {
    wchar_t buffer[4096];
    DWORD n = GetFullPathNameW(wide(path).c_str(), 4096, buffer, 0);
    return n ? narrow(std::wstring(buffer, n)) : path;
}

std::string now() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char b[64];
    std::snprintf(b, sizeof b, "%02d-%02d-%04d %02d:%02d:%02d", t.wDay, t.wMonth, t.wYear, t.wHour, t.wMinute, t.wSecond);
    return b;
}

void emptyFolder(const std::string &folder) {
    WIN32_FIND_DATAW f;
    HANDLE h = FindFirstFileW(wide(folder + "\\*").c_str(), &f);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring name = f.cFileName;
        if (name == L"." || name == L"..") continue;
        std::string path = folder + "\\" + narrow(name);
        if (f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { emptyFolder(path); RemoveDirectoryW(wide(path).c_str()); }
        else DeleteFileW(wide(path).c_str());
    } while (FindNextFileW(h, &f));
    FindClose(h);
}

// one step run as an outlook runs it: its own folder, the environment of this process with the step's
// additions, everything it writes read back; -1 when it could not be started
int runStep(const Step &step, std::string &output, double &seconds) {
    std::wstring environment;
    {
        std::vector<std::wstring> lines;
        wchar_t *block = GetEnvironmentStringsW();
        for (wchar_t *p = block; p && *p; p += wcslen(p) + 1) lines.push_back(p);
        FreeEnvironmentStringsW(block);
        for (size_t i = 0; i < step.environment.size(); ++i) {
            std::wstring name = wide(step.environment[i].first) + L"=";
            for (size_t k = 0; k < lines.size();)
                if (_wcsnicmp(lines[k].c_str(), name.c_str(), name.size()) == 0) lines.erase(lines.begin() + k); else ++k;
            lines.push_back(name + wide(step.environment[i].second));
        }
        for (size_t i = 0; i < lines.size(); ++i) { environment += lines[i]; environment += L'\0'; }
        environment += L'\0';
    }
    SECURITY_ATTRIBUTES sa = { sizeof sa, 0, TRUE };
    HANDLE readEnd, writeEnd;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0)) return -1;
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, 0);
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = writeEnd;
    si.hStdError = writeEnd;
    PROCESS_INFORMATION pi;
    std::wstring line = wide(windowsCommandLine(step.program, step.argv));
    std::vector<wchar_t> buffer(line.begin(), line.end());
    buffer.push_back(L'\0');
    DWORD start = GetTickCount();
    BOOL ok = CreateProcessW(wide(step.program).c_str(), &buffer[0], 0, 0, TRUE, CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
                             &environment[0], wide(step.directory).c_str(), &si, &pi);
    CloseHandle(writeEnd);
    CloseHandle(nul);
    if (!ok) { CloseHandle(readEnd); output = "cannot start " + step.program; return -1; }
    char chunk[4096];
    DWORD got;
    while (ReadFile(readEnd, chunk, sizeof chunk, &got, 0) && got > 0) output.append(chunk, got);
    CloseHandle(readEnd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    seconds = (GetTickCount() - start) / 1000.0;
    return static_cast<int>(code);
}

int usage() {
    out("gride-plan " GRIDE_COMPILE_NUMBER " - prints GRIDE's build plan");
    out("");
    out("  gride-plan <project.pro> [-config Debug|Release] [-target <arch>] [-rebuild | -clean | -compile <file>]");
    out("             [-tools <folder>] [-settings <file> | -no-settings] [-outlook <name>] [-native]");
    out("             [-steps | -files | -build | -dry]");
    out("");
    out("  (nothing)  the build log as it is planned before anything runs");
    out("  -steps     every step for a script: kind, tool, folder, command line, environment, files to write");
    out("  -files     the project's groups, entry by entry, with the files each pattern gave");
    out("  -build     runs the steps in order and prints the log as it is released");
    out("  -dry       the dry run: every command of a full build, each written whole, and nothing run");
    out("  -tools     where toolchain.json and the tools are; default: the folder of this program");
    return 2;
}

}

int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(narrow(argv[i]));
    if (args.size() == 1 && args[0] == "--version") {
        // GRIDE's one shape: the program's own line, the owner's; then the block's line, whose plans these are
        out("gride-plan " GRIDE_COMPILE_NUMBER " - prints GRIDE's build plan");
        out("\xC2\xA9" "2026 G. R. Akhtar");
        out(version());
        return 0;
    }
    if (std::strcmp(version(), GRIDE_COMPILE_VERSION) != 0) {
        out(std::string("compile.dll is not the one this program was built with: it says \"") + version() + "\"");
        return 3;
    }
    Request request;
    request.configuration = "Debug";
    request.target = "x86_64-windows";
    std::string tools, settings, outlook = "gride-plan", mode;
    bool noSettings = false;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string &a = args[i];
        bool more = i + 1 < args.size();
        if (a == "-config" && more) request.configuration = args[++i];
        else if (a == "-target" && more) request.target = args[++i];
        else if (a == "-rebuild") request.action = Rebuild;
        else if (a == "-clean") request.action = Clean;
        else if (a == "-compile" && more) { request.action = CompileFile; request.file = fullPath(args[++i]); }
        else if (a == "-tools" && more) tools = fullPath(args[++i]);
        else if (a == "-settings" && more) settings = fullPath(args[++i]);
        else if (a == "-no-settings") noSettings = true;
        else if (a == "-outlook" && more) outlook = args[++i];
        else if (a == "-native") request.native = true;
        else if (a == "-steps" || a == "-files" || a == "-build" || a == "-dry") mode = a;
        else if (!a.empty() && a[0] != '-' && request.project.empty()) request.project = fullPath(a);
        else return usage();
    }
    if (request.project.empty()) return usage();

    Paths paths = defaultPaths(tools, outlook);
    if (!settings.empty()) paths.settings = settings;
    if (noSettings) { paths.settings.clear(); paths.userToolchain.clear(); }
    Workspace workspace = Workspace::load(paths, systemFileSystem(), systemEnvironment());

    if (mode == "-files") {
        std::vector<Listed> listed = workspace.files(request.project);
        for (size_t i = 0; i < listed.size(); ++i) {
            std::string line = listed[i].group + "\t" + listed[i].entry + "\t";
            for (size_t k = 0; k < listed[i].files.size(); ++k) line += (k ? " " : "") + listed[i].files[k];
            out(line);
        }
        return 0;
    }

    Plan plan = workspace.plan(request);
    if (mode == "-steps") {
        for (size_t i = 0; i < plan.problems.size(); ++i) out(plan.problems[i].severity + "\t" + plan.problems[i].text);
        for (size_t i = 0; i < plan.steps.size(); ++i) {
            const Step &s = plan.steps[i];
            char wave[16];
            std::snprintf(wave, sizeof wave, "%d", s.wave);
            out("step\t" + s.kind + "\t" + s.tool + "\t" + wave + "\t" + s.stamp + "\t" + s.directory);
            out("cmd\t" + windowsCommandLine(s.program, s.argv));
            for (size_t k = 0; k < s.environment.size(); ++k) out("env\t" + s.environment[k].first + "=" + s.environment[k].second);
            out("why\t" + s.why);
        }
        for (size_t i = 0; i < plan.files.size(); ++i) out("file\t" + plan.files[i].first);
        for (size_t i = 0; i < plan.cleans.size(); ++i) out("clean\t" + plan.cleans[i]);
        if (!plan.program.empty()) out("program\t" + plan.program);
        return plan.runnable() ? 0 : 1;
    }

    BuildLog log(workspace, request, plan, outlook, now());
    if (mode != "-build") {
        std::string text = mode == "-dry" ? log.dryRun() : log.planned();
        fwrite(text.data(), 1, text.size(), stdout);
        return plan.runnable() ? 0 : 1;
    }

    for (size_t i = 0; i < plan.cleans.size(); ++i) emptyFolder(plan.cleans[i]);
    std::vector<std::string> lines = log.begin();
    for (size_t i = 0; i < lines.size(); ++i) out(lines[i]);
    bool failed = !plan.runnable();
    if (!failed) {
        CreateDirectoryW(wide(plan.outputFolder).c_str(), 0);
        for (size_t i = 0; i < plan.files.size(); ++i) systemFileSystem().write(plan.files[i].first, plan.files[i].second);
        // the folders of what the steps write
        for (size_t i = 0; i < plan.steps.size(); ++i)
            for (size_t k = 0; k < plan.steps[i].outputs.size(); ++k) {
                std::string placeholder = plan.steps[i].outputs[k] + ".gride-plan";
                if (systemFileSystem().write(placeholder, "")) DeleteFileW(wide(placeholder).c_str());
            }
    }
    // a step is asked whether it is up to date just before it would run: an earlier step that ran has by then
    // rewritten what it reads
    Stamps stamps(workspace, request, plan);
    for (size_t i = 0; i < plan.steps.size() && !failed; ++i) {
        int step = static_cast<int>(i);
        if (request.action != Rebuild && stamps.upToDate(step)) {
            lines = log.upToDate(step);
            for (size_t k = 0; k < lines.size(); ++k) out(lines[k]);
            continue;
        }
        std::string output;
        double seconds = 0;
        int code = runStep(plan.steps[i], output, seconds);
        stamps.ran(step, code == 0);
        lines = log.finished(step, code, output, seconds);
        for (size_t k = 0; k < lines.size(); ++k) out(lines[k]);
        if (code != 0) failed = true;
    }
    stamps.save();
    lines = log.end();
    for (size_t i = 0; i < lines.size(); ++i) out(lines[i]);
    return failed ? 1 : 0;
}
