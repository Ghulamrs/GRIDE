// The census recorder ("shim"). It stands in for one tool of a GRIDE folder: the real c90.exe is renamed
// c90.real.exe IN THE SAME FOLDER and a copy of this program is put there as c90.exe. Whatever starts c90.exe
// then starts this, which
//   1. appends one line to the file named by the environment variable GRIDE_CENSUS_LOG:
//      {"ev":"start","seq":N,"tool":"c90","pid":..,"ppid":..,"t":"..","cwd":"..","cmd":"..","env":{..}}
//   2. runs c90.real.exe with CreateProcessW(lpApplicationName = the real file, lpCommandLine = the command line
//      exactly as it was received), with its own standard handles, and waits;
//   3. appends {"ev":"end","seq":N,"tool":"c90","child":pid of the real tool,"exit":code,"ms":elapsed} and leaves
//      with the tool's exit code.
// With GRIDE_CENSUS_LOG unset nothing is recorded and the tool is simply run.
// The file is UTF-8, one JSON object per line; writers are serialised by a named mutex. "seq" is 1 for the first
// start record of a log file, counted from the file itself, so nested calls (a compiler that starts masm) are
// numbered in the order they began.
// "cmd" is GetCommandLineW() unchanged. The real tool sees the same command line, so its argv[0] is what the
// caller wrote; its module file name ends in .real.exe, in the same folder, which is all the GRIDE tools ask for.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <string>

static std::string utf8(const std::wstring &w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// a JSON string, quotes included
static std::string js(const std::wstring &w) {
    std::string in = utf8(w), out = "\"";
    for (unsigned char c : in) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) { char b[8]; wsprintfA(b, "\\u%04x", c); out += b; }
        else out += (char)c;
    }
    return out + "\"";
}

static std::wstring envOf(const wchar_t *name) {
    DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0) return std::wstring();
    std::wstring v(n, L'\0');
    DWORD got = GetEnvironmentVariableW(name, &v[0], n);
    v.resize(got);
    return v;
}

static bool startsWith(const std::wstring &s, const wchar_t *prefix) {
    size_t n = lstrlenW(prefix);
    return s.size() >= n && CompareStringOrdinal(s.c_str(), (int)n, prefix, (int)n, TRUE) == CSTR_EQUAL;
}

static bool same(const std::wstring &s, const wchar_t *name) {
    return CompareStringOrdinal(s.c_str(), (int)s.size(), name, -1, TRUE) == CSTR_EQUAL;
}

// the variables a build can be steered by: recorded when set
static bool wanted(const std::wstring &name) {
    static const wchar_t *prefixes[] = { L"C90_", L"CPP11_", L"SHALIMAR_", L"GRIDE_" };
    static const wchar_t *names[] = { L"INCLUDE", L"LIB", L"C90", L"CPP11", L"SHALIMAR", L"CL", L"CXX", L"ASM6X",
                                      L"LNK6X", L"VM6747", L"SIM6747", L"RTS6X", L"GRT", L"SHMRT6747", L"SHMRT6747D" };
    for (const wchar_t *p : prefixes) if (startsWith(name, p)) return true;
    for (const wchar_t *n : names) if (same(name, n)) return true;
    return false;
}

static std::string environmentJson() {
    std::string out = "{";
    LPWCH block = GetEnvironmentStringsW();
    if (block) {
        bool first = true;
        for (const wchar_t *p = block; *p; p += lstrlenW(p) + 1) {
            std::wstring entry(p);
            size_t eq = entry.find(L'=', 1);            // entries such as "=C:=C:\dir" begin with '='
            if (entry[0] == L'=' || eq == std::wstring::npos) continue;
            std::wstring name = entry.substr(0, eq);
            if (!wanted(name)) continue;
            if (!first) out += ",";
            first = false;
            out += js(name) + ":" + js(entry.substr(eq + 1));
        }
        FreeEnvironmentStringsW(block);
    }
    return out + "}";
}

static DWORD parentOf(DWORD pid) {
    DWORD parent = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W e;
    e.dwSize = sizeof e;
    if (Process32FirstW(snap, &e)) {
        do { if (e.th32ProcessID == pid) { parent = e.th32ParentProcessID; break; } } while (Process32NextW(snap, &e));
    }
    CloseHandle(snap);
    return parent;
}

// how many start records the log holds already
static unsigned long countStarts(HANDLE file) {
    static const char mark[] = "{\"ev\":\"start\"";
    const size_t markLen = sizeof mark - 1;
    unsigned long count = 0;
    size_t matched = 0;
    bool lineStart = true;
    char buf[65536];
    DWORD got = 0;
    SetFilePointer(file, 0, nullptr, FILE_BEGIN);
    while (ReadFile(file, buf, sizeof buf, &got, nullptr) && got > 0) {
        for (DWORD i = 0; i < got; i++) {
            char c = buf[i];
            if (lineStart) { matched = 0; lineStart = false; if (c == mark[0]) matched = 1; else matched = (size_t)-1; }
            else if (matched != (size_t)-1 && matched < markLen) {
                if (c == mark[matched]) { if (++matched == markLen) count++; } else matched = (size_t)-1;
            }
            if (c == '\n') lineStart = true;
        }
    }
    return count;
}

// one line appended under the mutex; *seq is given out here when it is 0
static void append(const std::wstring &log, unsigned long *seq, const std::string &head, const std::string &tail) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\GrideCensusLog");
    if (mutex) WaitForSingleObject(mutex, 30000);
    HANDLE file = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 50 && file == INVALID_HANDLE_VALUE; attempt++) {
        file = CreateFileW(log.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) Sleep(20);
    }
    if (file != INVALID_HANDLE_VALUE) {
        if (*seq == 0) *seq = countStarts(file) + 1;
        char number[32];
        wsprintfA(number, "%lu", *seq);
        std::string line = head + number + tail + "\n";
        SetFilePointer(file, 0, nullptr, FILE_END);
        DWORD wrote = 0;
        WriteFile(file, line.data(), (DWORD)line.size(), &wrote, nullptr);
        CloseHandle(file);
    }
    if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); }
}

static BOOL WINAPI ignoreBreak(DWORD) { return TRUE; }   // Ctrl+C goes to the tool, which decides

int wmain() {
    std::wstring self(32768, L'\0');
    self.resize(GetModuleFileNameW(nullptr, &self[0], (DWORD)self.size()));
    size_t slash = self.find_last_of(L"\\/");
    std::wstring dir = slash == std::wstring::npos ? std::wstring(L".") : self.substr(0, slash);
    std::wstring tool = slash == std::wstring::npos ? self : self.substr(slash + 1);
    if (tool.size() > 4 && same(tool.substr(tool.size() - 4), L".exe")) tool.resize(tool.size() - 4);
    std::wstring real = dir + L"\\" + tool + L".real.exe";

    const wchar_t *raw = GetCommandLineW();
    std::wstring commandLine(raw);
    std::wstring log = envOf(L"GRIDE_CENSUS_LOG");
    unsigned long seq = 0;

    if (!log.empty()) {
        std::wstring cwd(32768, L'\0');
        cwd.resize(GetCurrentDirectoryW((DWORD)cwd.size(), &cwd[0]));
        SYSTEMTIME now;
        GetLocalTime(&now);
        char stamp[48], ids[64];
        wsprintfA(stamp, "%04d-%02d-%02dT%02d:%02d:%02d.%03d", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                  now.wSecond, now.wMilliseconds);
        DWORD pid = GetCurrentProcessId();
        wsprintfA(ids, ",\"pid\":%lu,\"ppid\":%lu", pid, parentOf(pid));
        std::string tail = ",\"tool\":" + js(tool) + ids + ",\"t\":\"" + stamp + "\",\"cwd\":" + js(cwd) + ",\"cmd\":" +
                           js(commandLine) + ",\"env\":" + environmentJson() + "}";
        append(log, &seq, "{\"ev\":\"start\",\"seq\":", tail);
    }

    SetConsoleCtrlHandler(ignoreBreak, TRUE);
    // the tool ends with this program, however this program ends
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
        ZeroMemory(&limits, sizeof limits);
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
    }

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof pi);
    std::wstring mutableLine = commandLine;     // CreateProcessW may write into it
    mutableLine.push_back(L'\0');
    ULONGLONG t0 = GetTickCount64();
    DWORD code = 0, launchError = 0, child = 0;
    if (CreateProcessW(real.c_str(), &mutableLine[0], nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
        child = pi.dwProcessId;
        if (job) AssignProcessToJobObject(job, pi.hProcess);
        ResumeThread(pi.hThread);
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        launchError = GetLastError();
        code = 127;
        HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
        std::string say = "census shim: cannot start " + utf8(real) + "\r\n";
        DWORD wrote = 0;
        if (err && err != INVALID_HANDLE_VALUE) WriteFile(err, say.data(), (DWORD)say.size(), &wrote, nullptr);
    }
    ULONGLONG ms = GetTickCount64() - t0;

    if (!log.empty()) {
        char rest[128];
        wsprintfA(rest, ",\"child\":%lu,\"exit\":%lu,\"ms\":%lu", (unsigned long)child, (unsigned long)code, (unsigned long)ms);
        std::string tail = ",\"tool\":" + js(tool) + rest;
        if (launchError) { char e[48]; wsprintfA(e, ",\"launchError\":%lu", launchError); tail += e; }
        append(log, &seq, "{\"ev\":\"end\",\"seq\":", tail + "}");
    }
    ExitProcess(code);
}
