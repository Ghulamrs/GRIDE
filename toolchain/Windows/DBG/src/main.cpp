// =============================================================================
//  gride-dbg - GRIDE's own console debugger for x86_64-windows programs
//
//    gride-dbg [-y <symbol dir>] <program> [arguments...]
//
//  The program runs under the Win32 debug API; its PDB (GRIDE's link /DEBUG, or
//  link.exe's) is read through dbghelp. Commands come one per line on stdin and
//  are answered on stdout in cdb's words, so GRIDE's engine drives it as it
//  drives cdb (toolchain.json's session, dialect "cdb"):
//
//    bp [/1] `file:line`   a breakpoint at every address of the line (/1: once)
//    bc *                  every breakpoint cleared
//    g                     run to a breakpoint or the end
//    p  t  gu              step over a line, into a call, out of the function
//    .lastevent  ln        what stopped it, and where (file(line), module!function)
//    k  .frame N           the call stack, a frame chosen for dv and ??
//    dv [/t /V]            the frame's locals and parameters with types and values
//    ?? expr  ?? a = v     a value (names, . -> [] * &), an assignment
//    r  db <addr> L<n>     registers, memory
//    .step_filter "pat"    functions a step into does not stop in (module!name, *)
//    .input text           a line for the program's own input, taken at once, even
//                          while the program runs (answered with nothing)
//    .printf  .echo  q
//
//  A step into stops only in a function whose source is on this machine; a
//  step out of the program's own code into code with no source runs on. The
//  program has a console of its own (a pseudo-console); what it prints is read
//  from that console's screen at every stop, and while it runs, so it comes in
//  order with the stops. A line it has not finished is told as far as it goes;
//  the rest of it comes later after the mark <<more>>, never the same text twice.
//  Values are shown as their language shows them: a Shalimar array's elements,
//  not its runtime descriptor; cpp11's vector, string and initializer_list by
//  what they hold. GRIDE_DBG_TRACE=1 traces every instruction a step takes on stderr.
// =============================================================================
#include <windows.h>
#include <dbghelp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
#pragma comment(lib, "dbghelp.lib")

#define GRIDE_DBG_VERSION "1.1"

// the mark before the rest of a line whose start was told unfinished: GRIDE joins it to that start
#define CONTINUED "<<more>>"

// ---- the conversation with GRIDE ---------------------------------------------------------------
static HANDLE out = INVALID_HANDLE_VALUE, in = INVALID_HANDLE_VALUE;

static void say(const char* format, ...)
{
    char buffer[8192];
    va_list a;
    va_start(a, format);
    int n = vsnprintf(buffer, sizeof buffer, format, a);
    va_end(a);
    if (n < 0) return;
    if (n >= static_cast<int>(sizeof buffer)) n = static_cast<int>(sizeof buffer) - 1;
    DWORD written = 0;
    WriteFile(out, buffer, static_cast<DWORD>(n), &written, NULL);
}

static void sayLine(const std::string& s) { say("%s\n", s.c_str()); }

// GRIDE's lines, heard on a thread of their own (hearGride, below): the commands wait here for the loop
static CRITICAL_SECTION heardLock;
static HANDLE heardEvent = NULL;
static std::vector<std::string> heard;
static bool heardAll = false;

static bool readLine(std::string& line)
{
    for (;;) {
        EnterCriticalSection(&heardLock);
        bool any = !heard.empty(), end = heardAll;
        if (any) { line = heard.front(); heard.erase(heard.begin()); }
        LeaveCriticalSection(&heardLock);
        if (any) return true;
        if (end) return false;
        WaitForSingleObject(heardEvent, INFINITE);
    }
}

static std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return std::string();
    size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

static std::string hex64(DWORD64 v)
{
    char b[32];
    snprintf(b, sizeof b, "%08lx`%08lx", static_cast<unsigned long>(v >> 32), static_cast<unsigned long>(v & 0xffffffffu));
    return b;
}

// ---- the program ---------------------------------------------------------------------------------
static HANDLE process = NULL, thread = NULL;
static DWORD pid = 0, tid = 0;
static std::map<DWORD, HANDLE> threads;
static std::string moduleName = "program";
static DWORD64 exeBase = 0;
static bool exited = false;
static DWORD exitCode = 0;
static std::string lastEvent;

// the program's console: a pseudo-console whose screen is read while attached to it
static HPCON pcon = NULL;
static HANDLE pcIn = NULL, pcOut = NULL, screen = INVALID_HANDLE_VALUE;
static bool attached = false;

static DWORD WINAPI drainConsole(LPVOID)
{
    char buffer[4096];
    DWORD got = 0;
    while (ReadFile(pcOut, buffer, sizeof buffer, &got, NULL) && got > 0) { }
    return 0;
}

static std::string utf8(const std::wstring& w)
{
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), NULL, 0, NULL, NULL);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, NULL, NULL);
    return s;
}

// a row of the screen from column `from` to `upTo` (-1: its end), trailing blanks dropped unless `keepBlanks`
// (an unfinished line's "Enter n: " keeps its space, which the answer is typed after)
static std::wstring screenRow(SHORT y, SHORT width, SHORT upTo, SHORT from = 0, bool keepBlanks = false)
{
    std::wstring row(static_cast<size_t>(width), L' ');
    DWORD got = 0;
    COORD at = { 0, y };
    ReadConsoleOutputCharacterW(screen, &row[0], static_cast<DWORD>(width), at, &got);
    row.resize(upTo >= 0 && upTo < width ? static_cast<size_t>(upTo) : static_cast<size_t>(got));
    row = from > 0 ? (static_cast<size_t>(from) < row.size() ? row.substr(static_cast<size_t>(from)) : std::wstring()) : row;
    if (keepBlanks) return row;
    size_t end = row.find_last_not_of(L' ');
    return end == std::wstring::npos ? std::wstring() : row.substr(0, end + 1);
}

// how far the program's output has been told: a row of its screen and a column in it (the part of an
// unfinished line already told), and where its cursor stood when last looked at while it ran
static SHORT toldRow = 0, toldColumn = 0;
static COORD lastSeen = { -1, -1 };

// the program's threads held (or let go) while its screen is emptied as it runs
static void suspendProgram(bool suspend)
{
    for (const auto& t : threads) { if (suspend) SuspendThread(t.second); else ResumeThread(t.second); }
}

// what the program printed since it was last told: the lines it finished (one whose start was told
// unfinished, by its rest after CONTINUED), then the line it has not finished - at a stop, at the end
// (told as finished), and while it runs once its cursor has stood still since the last look (a prompt
// waiting for its input). At a stop the screen is emptied, the unfinished line moved to its top; while
// it runs the screen is left as it is until half of it is used, then the program is held while it is emptied.
static void programOutput(bool end, bool running = false)
{
    if (!attached) return;
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(screen, &info)) return;
    SHORT width = info.dwSize.X, cy = info.dwCursorPosition.Y, cx = info.dwCursorPosition.X;
    if (running && cy > info.dwSize.Y / 2) {
        suspendProgram(true);
        programOutput(false, false);
        suspendProgram(false);
        return;
    }
    if (cy < toldRow || (cy == toldRow && cx < toldColumn)) { toldRow = cy; toldColumn = 0; }     // the program cleared its screen
    for (SHORT y = toldRow; y < cy; y++) {
        if (y == toldRow && toldColumn > 0) {
            std::wstring rest = screenRow(y, width, -1, toldColumn);
            if (!rest.empty()) say("%s%s\n", CONTINUED, utf8(rest).c_str());
        }
        else sayLine(utf8(screenRow(y, width, -1)));
        toldColumn = 0;
    }
    toldRow = cy;
    bool still = cx == lastSeen.X && cy == lastSeen.Y;
    lastSeen = info.dwCursorPosition;
    if (cx > toldColumn && (!running || still)) {
        std::wstring piece = screenRow(cy, width, cx, toldColumn, !end);
        if (!piece.empty()) say("%s%s\n", toldColumn > 0 ? CONTINUED : "", utf8(piece).c_str());
        toldColumn = cx;
    }
    if (end || running || cy == 0) return;
    std::wstring partial = screenRow(cy, width, cx);
    COORD top = { 0, 0 };
    DWORD n = 0;
    FillConsoleOutputCharacterW(screen, L' ', static_cast<DWORD>(width) * static_cast<DWORD>(cy + 1), top, &n);
    if (!partial.empty()) WriteConsoleOutputCharacterW(screen, partial.c_str(), static_cast<DWORD>(partial.size()), top, &n);
    COORD cursor = { cx, 0 };
    SetConsoleCursorPosition(screen, cursor);
    toldRow = 0;
    lastSeen = cursor;
}

// a line typed for the program, given to its console as typed there: the console echoes it on the
// screen, and the program reads it when it reads its input
static void feedProgram(const std::string& text)
{
    if (!pcIn) return;
    std::string keys = text + "\r";
    DWORD put = 0;
    WriteFile(pcIn, keys.data(), static_cast<DWORD>(keys.size()), &put, NULL);
}

// GRIDE's lines as they come: ".input text" goes to the program at once, even while it runs (a program
// waiting on its input is not stopped to be given it); every other line waits for the loop (readLine)
static DWORD WINAPI hearGride(LPVOID)
{
    std::string pending;
    for (;;) {
        char buffer[4096];
        DWORD got = 0;
        bool more = ReadFile(in, buffer, sizeof buffer, &got, NULL) && got > 0;
        if (more) pending.append(buffer, got);
        for (;;) {
            size_t nl = pending.find('\n');
            if (nl == std::string::npos && (more || pending.empty())) break;
            std::string line = nl == std::string::npos ? pending : pending.substr(0, nl);
            pending.erase(0, nl == std::string::npos ? std::string::npos : nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.compare(0, 6, ".input") == 0 && (line.size() == 6 || line[6] == ' ')) { feedProgram(line.size() > 7 ? line.substr(7) : std::string()); continue; }
            EnterCriticalSection(&heardLock);
            heard.push_back(line);
            LeaveCriticalSection(&heardLock);
            SetEvent(heardEvent);
        }
        if (!more) break;
    }
    EnterCriticalSection(&heardLock);
    heardAll = true;
    LeaveCriticalSection(&heardLock);
    SetEvent(heardEvent);
    return 0;
}

// ---- symbols and lines ----------------------------------------------------------------------------
struct Sym { std::string name; DWORD64 start = 0, size = 0; bool ok = false; };

static Sym symbolAt(DWORD64 a)
{
    Sym s;
    char buffer[sizeof(SYMBOL_INFO) + 1024];
    SYMBOL_INFO* info = reinterpret_cast<SYMBOL_INFO*>(buffer);
    memset(buffer, 0, sizeof buffer);
    info->SizeOfStruct = sizeof(SYMBOL_INFO);
    info->MaxNameLen = 1023;
    DWORD64 off = 0;
    if (!SymFromAddr(process, a, &off, info)) return s;
    s.name = info->Name;
    s.start = info->Address;
    s.size = info->Size;
    s.ok = true;
    return s;
}

static std::string moduleOf(DWORD64 a)
{
    IMAGEHLP_MODULE64 m;
    memset(&m, 0, sizeof m);
    m.SizeOfStruct = sizeof m;
    if (!SymGetModuleInfo64(process, a, &m)) return "?";
    return m.ModuleName;
}

// "module!function+0x1a", or "module!function" at its first instruction
static std::string where(DWORD64 a)
{
    Sym s = symbolAt(a);
    if (!s.ok) return moduleOf(a) + "+" + hex64(a);
    char b[64] = "";
    if (a > s.start) snprintf(b, sizeof b, "+0x%llx", static_cast<unsigned long long>(a - s.start));
    return moduleOf(a) + "!" + s.name + b;
}

struct Line { std::string file; DWORD number = 0; DWORD disp = 0; DWORD64 address = 0; bool ok = false; };

static Line lineAt(DWORD64 a)
{
    Line l;
    IMAGEHLP_LINE64 line;
    memset(&line, 0, sizeof line);
    line.SizeOfStruct = sizeof line;
    if (!SymGetLineFromAddr64(process, a, &l.disp, &line)) return l;
    l.file = line.FileName;
    l.number = line.LineNumber;
    l.address = line.Address;
    l.ok = l.number != 0 && l.number < 0xf00000;      // 0xfeefee and friends: hidden lines
    return l;
}

static bool fileThere(const std::string& f) { return !f.empty() && GetFileAttributesA(f.c_str()) != INVALID_FILE_ATTRIBUTES; }

// ---- the step filter (cdb's .step_filter): module!name patterns with * -------------------------------
static std::vector<std::string> stepFilter;

static bool wildMatch(const char* p, const char* s)
{
    if (*p == '\0') return *s == '\0';
    if (*p == '*') return wildMatch(p + 1, s) || (*s != '\0' && wildMatch(p, s + 1));
    return *s != '\0' && (tolower(static_cast<unsigned char>(*p)) == tolower(static_cast<unsigned char>(*s))) && wildMatch(p + 1, s + 1);
}

static bool filtered(DWORD64 a)
{
    Sym s = symbolAt(a);
    std::string full = moduleOf(a) + "!" + (s.ok ? s.name : std::string());
    for (const std::string& f : stepFilter) if (wildMatch(f.c_str(), full.c_str())) return true;
    return false;
}

// a place a step may stop in: a function of the program whose source is here, not filtered out
static bool hasSource(DWORD64 a)
{
    Line l = lineAt(a);
    return l.ok && fileThere(l.file) && !filtered(a);
}

// ---- registers and memory --------------------------------------------------------------------------
static CONTEXT context()
{
    CONTEXT c;
    memset(&c, 0, sizeof c);
    c.ContextFlags = CONTEXT_FULL;
    GetThreadContext(thread, &c);
    return c;
}

static void setContext(CONTEXT& c) { SetThreadContext(thread, &c); }

static bool readMemory(DWORD64 a, void* to, size_t n)
{
    SIZE_T got = 0;
    return ReadProcessMemory(process, reinterpret_cast<LPCVOID>(a), to, n, &got) && got == n;
}

static bool writeMemory(DWORD64 a, const void* from, size_t n)
{
    SIZE_T put = 0;
    bool ok = WriteProcessMemory(process, reinterpret_cast<LPVOID>(a), from, n, &put) && put == n;
    FlushInstructionCache(process, reinterpret_cast<LPCVOID>(a), n);
    return ok;
}

static DWORD64 peek64(DWORD64 a)
{
    DWORD64 v = 0;
    readMemory(a, &v, 8);
    return v;
}

// CodeView's AMD64 register numbers
static DWORD64 cvRegister(const CONTEXT& c, ULONG r)
{
    switch (r) {
    case 328: return c.Rax; case 329: return c.Rbx; case 330: return c.Rcx; case 331: return c.Rdx;
    case 332: return c.Rsi; case 333: return c.Rdi; case 334: return c.Rbp; case 335: return c.Rsp;
    case 336: return c.R8;  case 337: return c.R9;  case 338: return c.R10; case 339: return c.R11;
    case 340: return c.R12; case 341: return c.R13; case 342: return c.R14; case 343: return c.R15;
    case 33: return c.Rip;
    default: return 0;
    }
}

static const char* cvRegisterName(ULONG r)
{
    static const char* names[] = { "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" };
    return r >= 328 && r <= 343 ? names[r - 328] : "reg";
}

// ---- breakpoints -----------------------------------------------------------------------------------
struct Patch { BYTE saved = 0; int users = 0; };
static std::map<DWORD64, Patch> patched;

struct Breakpoint { int id = 0; std::string spec; std::vector<DWORD64> at; bool once = false; };
static std::vector<Breakpoint> breakpoints;
static int nextBreakpoint = 0;

static void patch(DWORD64 a)
{
    Patch& p = patched[a];
    if (p.users++ == 0) {
        readMemory(a, &p.saved, 1);
        BYTE cc = 0xCC;
        writeMemory(a, &cc, 1);
    }
}

static void unpatch(DWORD64 a)
{
    auto i = patched.find(a);
    if (i == patched.end()) return;
    if (--i->second.users <= 0) {
        writeMemory(a, &i->second.saved, 1);
        patched.erase(i);
    }
}

static int breakpointAt(DWORD64 a)
{
    for (size_t i = 0; i < breakpoints.size(); i++)
        for (DWORD64 x : breakpoints[i].at) if (x == a) return static_cast<int>(i);
    return -1;
}

struct LineHits {
    std::vector<DWORD64>* to; DWORD line;
    DWORD next = 0, prev = 0;               // the nearest lines after and before it that have code
    DWORD64 nextAt = 0, prevAt = 0;
    std::string source;                     // the file as the program's lines name it
};

// the addresses of the line; and the nearest lines around it that have code, for a line that has none
static BOOL CALLBACK onSourceLine(PSRCCODEINFO info, PVOID context)
{
    LineHits* h = static_cast<LineHits*>(context);
    DWORD n = info->LineNumber;
    if (n == 0 || n >= 0xf00000) return TRUE;
    if (n == h->line) {
        bool known = false;
        for (DWORD64 a : *h->to) if (a == info->Address) known = true;
        if (!known) h->to->push_back(info->Address);
    }
    else if (n > h->line && (h->next == 0 || n < h->next)) { h->next = n; h->nextAt = info->Address; }
    else if (n < h->line && n > h->prev) { h->prev = n; h->prevAt = info->Address; }
    if (h->source.empty()) h->source = info->FileName;
    return TRUE;
}

// whether a line of a source holds code that is no statement of its own: the rest of one begun above
// ("? ... :" on two lines), not a blank line or a comment
static bool continuesCode(const std::string& source, DWORD line)
{
    FILE* f = fopen(source.c_str(), "rb");
    if (!f) return false;
    std::string text;
    DWORD n = 1;
    for (int ch; (ch = fgetc(f)) != EOF && n <= line;) {
        if (ch == '\n') { n++; continue; }
        if (n == line) text += static_cast<char>(ch);
    }
    fclose(f);
    std::string t = trim(text);
    if (!t.empty() && t.back() == '\r') t = trim(t.substr(0, t.size() - 1));
    return !t.empty() && t.compare(0, 2, "//") != 0 && t.compare(0, 2, "/*") != 0 && t[0] != '*';
}

static DWORD64 bodyStart(DWORD64 at, DWORD64 end);

// every address the line has (a for-statement's line has two or three). A line with no code: the line
// above's, when it goes on a statement begun there in the same function; else the next line's that
// has code (as Visual Studio moves a breakpoint off a blank line, and "int main(void)" is main's "{").
// A one-line function's line has its entry and, where the compiler wrote it, its body's start: the
// body's is kept, where the parameters are stored, so a stop there reads them
static bool resolve(const std::string& file, DWORD line, std::vector<DWORD64>& at)
{
    LineHits h;
    h.to = &at; h.line = line;
    SymEnumSourceLines(process, exeBase, NULL, file.c_str(), 0, 0, onSourceLine, &h);
    if (at.empty() && (h.next != 0 || h.prev != 0)) {
        bool sameFunction = h.next != 0 && h.prev != 0 && symbolAt(h.prevAt).start == symbolAt(h.nextAt).start;
        LineHits n;
        n.to = &at;
        n.line = h.prev != 0 && (h.next == 0 || (sameFunction && continuesCode(h.source, line))) ? h.prev : h.next;
        SymEnumSourceLines(process, exeBase, NULL, file.c_str(), 0, 0, onSourceLine, &n);
    }
    for (size_t i = 0; i < at.size(); i++) {
        Sym fn = symbolAt(at[i]);
        if (!fn.ok || fn.start != at[i]) continue;
        DWORD64 body = bodyStart(at[i], fn.start + (fn.size ? fn.size : 1));
        bool bodyToo = false;
        for (DWORD64 a : at) if (body && a == body) bodyToo = true;
        if (bodyToo) { at.erase(at.begin() + static_cast<std::ptrdiff_t>(i)); i--; }
    }
    if (!at.empty()) return true;
    LONG disp = 0;
    IMAGEHLP_LINE64 l;
    memset(&l, 0, sizeof l);
    l.SizeOfStruct = sizeof l;
    if (!SymGetLineFromName64(process, NULL, file.c_str(), line, &disp, &l)) return false;
    at.push_back(l.Address);
    return true;
}

// ---- the debug events ------------------------------------------------------------------------------
enum class Event { Breakpoint, Step, Exit, Crash };
static DWORD64 eventAddress = 0;
static DWORD crashCode = 0;

static std::string exceptionName(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "Access violation";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "Integer divide-by-zero";
    case EXCEPTION_STACK_OVERFLOW: return "Stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "Illegal instruction";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "Array bounds exceeded";
    case 0xE06D7363: return "C++ EH exception";
    case 0xC0000409: return "Security check failure or stack buffer overrun";
    default: return "Unknown exception";
    }
}

static std::string programPath;

// a module by its image's path, so it is named as cdb names it ("shapes!main", "KERNEL32!...")
static void loadModule(HANDLE file, const char* name, DWORD64 base)
{
    char path[MAX_PATH] = "";
    if (!name && file && GetFinalPathNameByHandleA(file, path, MAX_PATH, FILE_NAME_NORMALIZED) > 0) name = strncmp(path, "\\\\?\\", 4) == 0 ? path + 4 : path;
    SymLoadModuleEx(process, file, name, NULL, base, 0, NULL, 0);
}

static Event waitEvent()
{
    for (;;) {
        DEBUG_EVENT e;
        // the program running free: what it prints is told as it prints it, a tenth of a second at a time
        if (!WaitForDebugEvent(&e, attached ? 100 : INFINITE)) {
            DWORD why = GetLastError();
            if (attached && (why == ERROR_SEM_TIMEOUT || why == WAIT_TIMEOUT)) { programOutput(false, true); continue; }
            exited = true;
            return Event::Exit;
        }
        DWORD status = DBG_CONTINUE;
        switch (e.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            threads[e.dwThreadId] = e.u.CreateProcessInfo.hThread;
            exeBase = reinterpret_cast<DWORD64>(e.u.CreateProcessInfo.lpBaseOfImage);
            loadModule(e.u.CreateProcessInfo.hFile, programPath.c_str(), exeBase);
            if (e.u.CreateProcessInfo.hFile) CloseHandle(e.u.CreateProcessInfo.hFile);
            break;
        case CREATE_THREAD_DEBUG_EVENT:
            threads[e.dwThreadId] = e.u.CreateThread.hThread;
            break;
        case EXIT_THREAD_DEBUG_EVENT:
            threads.erase(e.dwThreadId);
            break;
        case LOAD_DLL_DEBUG_EVENT:
            loadModule(e.u.LoadDll.hFile, NULL, reinterpret_cast<DWORD64>(e.u.LoadDll.lpBaseOfDll));
            if (e.u.LoadDll.hFile) CloseHandle(e.u.LoadDll.hFile);
            break;
        case EXIT_PROCESS_DEBUG_EVENT: {
            exited = true;
            exitCode = e.u.ExitProcess.dwExitCode;
            char b[160];
            snprintf(b, sizeof b, "%lx.%lx: Exit process 0:%lx, code %lx", pid, e.dwThreadId, pid, exitCode);
            lastEvent = b;
            return Event::Exit;     // left uncontinued: the console is read first, then q ends it
        }
        case EXCEPTION_DEBUG_EVENT: {
            const EXCEPTION_RECORD& x = e.u.Exception.ExceptionRecord;
            DWORD code = x.ExceptionCode;
            if (code == EXCEPTION_BREAKPOINT || code == EXCEPTION_SINGLE_STEP || code == 0x4000001F || code == 0x4000001E) {
                tid = e.dwThreadId;
                thread = threads[tid];
                eventAddress = reinterpret_cast<DWORD64>(x.ExceptionAddress);
                bool isBreak = code == EXCEPTION_BREAKPOINT || code == 0x4000001F;
                char b[160];
                snprintf(b, sizeof b, "%lx.%lx: %s - code %08lx (first chance)", pid, tid, isBreak ? "Break instruction exception" : "Single step exception",
                         static_cast<unsigned long>(isBreak ? EXCEPTION_BREAKPOINT : EXCEPTION_SINGLE_STEP));
                lastEvent = b;
                return isBreak ? Event::Breakpoint : Event::Step;
            }
            if (!e.u.Exception.dwFirstChance) {
                tid = e.dwThreadId;
                thread = threads[tid];
                eventAddress = reinterpret_cast<DWORD64>(x.ExceptionAddress);
                crashCode = code;
                char b[200];
                snprintf(b, sizeof b, "%lx.%lx: %s - code %08lx (!!! second chance !!!)", pid, tid, exceptionName(code).c_str(), code);
                lastEvent = b;
                return Event::Crash;
            }
            status = DBG_EXCEPTION_NOT_HANDLED;     // the program's own handlers see it first
            break;
        }
        default:
            break;
        }
        ContinueDebugEvent(e.dwProcessId, e.dwThreadId, status);
    }
}

static void resume() { ContinueDebugEvent(pid, tid, DBG_CONTINUE); }

// one instruction with the trap flag, a breakpoint under RIP lifted for it
static Event singleStep()
{
    CONTEXT c = context();
    DWORD64 at = c.Rip;
    bool lifted = patched.count(at) != 0;
    BYTE cc = 0xCC;
    if (lifted) writeMemory(at, &patched[at].saved, 1);
    c.EFlags |= 0x100;
    setContext(c);
    resume();
    Event e = waitEvent();
    if (lifted && !exited) writeMemory(at, &cc, 1);
    if (e == Event::Breakpoint && patched.count(eventAddress)) {       // a breakpoint byte stepped onto: back to it
        CONTEXT k = context();
        k.Rip = eventAddress;
        setContext(k);
        return Event::Step;
    }
    return e;
}

// a breakpoint hit: RIP back on its byte; a once-breakpoint is gone
static int arrivedAtBreakpoint()
{
    CONTEXT c = context();
    c.Rip = eventAddress;
    setContext(c);
    int b = breakpointAt(eventAddress);
    return b;
}

static void dropOnce(int b)
{
    if (b < 0 || !breakpoints[static_cast<size_t>(b)].once) return;
    for (DWORD64 a : breakpoints[static_cast<size_t>(b)].at) unpatch(a);
    breakpoints.erase(breakpoints.begin() + b);
}

// ---- the moves -------------------------------------------------------------------------------------
enum class Stop { Line, Breakpoint, Entry, Exit, Crash, NoSource };
static int hitBreakpoint = -1;

// on from where it stands until a breakpoint, the end or a crash
static Stop runOn()
{
    if (patched.count(context().Rip)) {
        Event e = singleStep();
        if (e == Event::Exit) return Stop::Exit;
        if (e == Event::Crash) return Stop::Crash;
        if (patched.count(context().Rip) && breakpointAt(context().Rip) >= 0) { hitBreakpoint = breakpointAt(context().Rip); return Stop::Breakpoint; }
    }
    for (;;) {
        resume();
        Event e = waitEvent();
        if (e == Event::Exit) return Stop::Exit;
        if (e == Event::Crash) return Stop::Crash;
        if (e == Event::Breakpoint && patched.count(eventAddress)) {
            int b = arrivedAtBreakpoint();
            if (b >= 0) { hitBreakpoint = b; return Stop::Breakpoint; }
            continue;
        }
        if (e == Event::Breakpoint) return Stop::NoSource;      // a DebugBreak of the program's own
    }
}

// to `target` with a temporary breakpoint, once RSP is above `rsp` (a recursive call passes it); a breakpoint
// of the user's on the way stops there
static Stop runTo(DWORD64 target, DWORD64 rsp)
{
    patch(target);
    Stop result = Stop::Line;
    bool first = true;
    for (;;) {
        Event e;
        if (first && patched.count(context().Rip) && context().Rip != target) { e = singleStep(); first = false; if (e == Event::Step) continue; }
        else { first = false; resume(); e = waitEvent(); }
        if (e == Event::Exit) { result = Stop::Exit; break; }
        if (e == Event::Crash) { result = Stop::Crash; break; }
        if (e != Event::Breakpoint || !patched.count(eventAddress)) { if (e == Event::Breakpoint) { result = Stop::NoSource; break; } continue; }
        CONTEXT c = context();
        c.Rip = eventAddress;
        setContext(c);
        int b = breakpointAt(eventAddress);
        if (eventAddress == target && c.Rsp > rsp) { result = Stop::Line; break; }
        if (b >= 0) { hitBreakpoint = b; result = Stop::Breakpoint; break; }
        // the target in a deeper call, or another temporary: over it and on
        Event s = singleStep();
        if (s == Event::Exit) { result = Stop::Exit; break; }
        if (s == Event::Crash) { result = Stop::Crash; break; }
    }
    if (!exited) unpatch(target);
    return result;
}

// whether the instruction just stepped was a call: RSP down by 8 with a return address just after the old RIP
static bool wasCall(const CONTEXT& before, const CONTEXT& after)
{
    if (after.Rsp != before.Rsp - 8) return false;
    DWORD64 ret = peek64(after.Rsp);
    return ret > before.Rip && ret <= before.Rip + 16 && after.Rip != ret;
}

// the frame above: its return address and the RSP it returns with
static bool callerOf(DWORD64& ret, DWORD64& rsp)
{
    CONTEXT c = context();
    STACKFRAME64 f;
    memset(&f, 0, sizeof f);
    f.AddrPC.Offset = c.Rip; f.AddrPC.Mode = AddrModeFlat;
    f.AddrFrame.Offset = c.Rbp; f.AddrFrame.Mode = AddrModeFlat;
    f.AddrStack.Offset = c.Rsp; f.AddrStack.Mode = AddrModeFlat;
    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &f, &c, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) return false;
    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &f, &c, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) return false;
    ret = f.AddrPC.Offset;
    rsp = c.Rsp;
    return ret != 0;
}

static Stop stepOut();

// whether the function from `at` to `end` has a line other than the one at `at` (a one-line function has not)
static bool otherLine(DWORD64 at, DWORD64 end)
{
    IMAGEHLP_LINE64 l;
    memset(&l, 0, sizeof l);
    l.SizeOfStruct = sizeof l;
    DWORD disp = 0;
    if (!SymGetLineFromAddr64(process, at, &disp, &l)) return false;
    DWORD first = l.LineNumber;
    for (int n = 0; n < 4096 && SymGetLineNext64(process, &l); n++) {
        if (l.Address >= end) return false;
        if (l.LineNumber != first && l.LineNumber < 0xf00000) return true;
    }
    return false;
}

// a function's body past its prologue: the second line record from its entry, when the compiler wrote one for the
// entry's own line (a one-line function: "{ return new Circle(r); }"), so its parameters are read where they are
// stored. The line's records are enumerated: SymGetLineNext64 passes over a record that repeats the line before it.
static DWORD64 bodyStart(DWORD64 at, DWORD64 end)
{
    Line entry = lineAt(at);
    if (!entry.ok || entry.disp != 0) return 0;
    std::vector<DWORD64> all;
    LineHits h;
    h.to = &all; h.line = entry.number;
    SymEnumSourceLines(process, SymGetModuleBase64(process, at), NULL, entry.file.c_str(), 0, 0, onSourceLine, &h);
    DWORD64 body = 0;
    for (DWORD64 a : all) if (a > at && a < end && (body == 0 || a < body) && lineAt(a).file == entry.file) body = a;
    return body;
}

// cdb's p and t: on to the start of another line of this function; a call is run through (p), or stopped in
// at its entry when its source is here (t); a return stops in the caller
static Stop step(bool into)
{
    CONTEXT c = context();
    Line line = lineAt(c.Rip);
    Sym fn = symbolAt(c.Rip);
    if (!line.ok || !fn.ok) return stepOut();
    DWORD64 start = fn.start, end = fn.start + (fn.size ? fn.size : 1);
    DWORD64 frame = c.Rsp;
    DWORD64 body = 0;       // a function stepped into: where its body starts, past the prologue
    for (int n = 0; n < 5000000; n++) {
        CONTEXT before = context();
        Event e = singleStep();
        if (e == Event::Exit) return Stop::Exit;
        if (e == Event::Crash) return Stop::Crash;
        CONTEXT k = context();
        if (getenv("GRIDE_DBG_TRACE")) fprintf(stderr, "[step %s rip %llx rsp %llx -> rip %llx rsp %llx call %d]\n", into ? "t" : "p", before.Rip, before.Rsp, k.Rip, k.Rsp, wasCall(before, k) ? 1 : 0);
        if (wasCall(before, k)) {
            if (getenv("GRIDE_DBG_TRACE")) fprintf(stderr, "[call %s source %d line %d]\n", where(k.Rip).c_str(), hasSource(k.Rip) ? 1 : 0, lineAt(k.Rip).ok ? 1 : 0);
            if (breakpointAt(k.Rip) >= 0) { hitBreakpoint = breakpointAt(k.Rip); return Stop::Breakpoint; }
            if (into && hasSource(k.Rip)) {
                // into the function: on through its prologue to its first statement, as an IDE shows a step into;
                // a function all on one line stays on that line
                Sym callee = symbolAt(k.Rip);
                DWORD64 calleeEnd = callee.start + (callee.size ? callee.size : 1);
                body = callee.ok ? bodyStart(k.Rip, calleeEnd) : 0;
                if (!callee.ok || (!body && !otherLine(k.Rip, calleeEnd))) return Stop::Entry;
                start = callee.start; end = calleeEnd;
                line = lineAt(k.Rip);
                frame = k.Rsp;
                into = false;
                continue;
            }
            Stop s = runTo(peek64(k.Rsp), k.Rsp);
            if (s != Stop::Line) return s;
            k = context();
        }
        if (k.Rsp > frame && (k.Rip < start || k.Rip >= end)) {    // returned to the caller
            if (!hasSource(k.Rip)) return runOn();                  // out of the program's own code: on to the next stop
            return Stop::Line;
        }
        if (k.Rip >= start && k.Rip < end) {
            // another line, wherever in it (a loop's condition sits part way into the for-statement's line)
            Line l = lineAt(k.Rip);
            if (l.ok && (l.number != line.number || (body && k.Rip == body))) return Stop::Line;
        }
        else if (k.Rsp == frame) {                                  // a jump into another function (a tail call)
            Sym other = symbolAt(k.Rip);
            if (!other.ok) continue;
            start = other.start; end = other.start + (other.size ? other.size : 1);
            if (into && hasSource(k.Rip)) return Stop::Entry;
        }
    }
    return Stop::Line;
}

// cdb's gu: on to the caller, just after the call
static Stop stepOut()
{
    DWORD64 ret = 0, rsp = 0;
    if (!callerOf(ret, rsp)) return runOn();
    Stop s = runTo(ret, context().Rsp);
    if (s == Stop::Line && !hasSource(context().Rip)) return runOn();
    return s;
}

// ---- types and values (dbghelp's type information) --------------------------------------------------
enum { TagUDT = 11, TagEnum = 12, TagFunctionType = 13, TagPointer = 14, TagArray = 15, TagBase = 16, TagTypedef = 17, TagBaseClass = 18, TagData = 7 };
static DWORD64 typeModule = 0;

static DWORD tagOf(ULONG t) { DWORD tag = 0; SymGetTypeInfo(process, typeModule, t, TI_GET_SYMTAG, &tag); return tag; }
static ULONG64 lengthOf(ULONG t) { ULONG64 n = 0; SymGetTypeInfo(process, typeModule, t, TI_GET_LENGTH, &n); return n; }
static ULONG targetOf(ULONG t) { DWORD to = 0; SymGetTypeInfo(process, typeModule, t, TI_GET_TYPEID, &to); return to; }

static std::string symName(ULONG t)
{
    WCHAR* name = NULL;
    if (!SymGetTypeInfo(process, typeModule, t, TI_GET_SYMNAME, &name) || !name) return std::string();
    std::string s = utf8(name);
    LocalFree(name);
    return s;
}

static ULONG plain(ULONG t)
{
    for (int i = 0; i < 16 && tagOf(t) == TagTypedef; i++) t = targetOf(t);
    return t;
}

static std::string baseName(ULONG t)
{
    DWORD bt = 0;
    SymGetTypeInfo(process, typeModule, t, TI_GET_BASETYPE, &bt);
    ULONG64 n = lengthOf(t);
    switch (bt) {
    case 1: return "void";
    case 2: return n == 1 ? "char" : "char" + std::to_string(n * 8);
    case 3: return "wchar_t";
    case 6: return n == 1 ? "signed char" : n == 2 ? "short" : n == 4 ? "int" : "int64";
    case 7: return n == 1 ? "unsigned char" : n == 2 ? "unsigned short" : n == 4 ? "unsigned int" : "unsigned int64";
    case 8: return n == 4 ? "float" : n == 8 ? "double" : "long double";
    case 10: return "bool";
    case 13: return n == 8 ? "int64" : "long";
    case 14: return n == 8 ? "unsigned int64" : "unsigned long";
    case 31: return "HRESULT";
    case 32: return "char16_t";
    case 33: return "char32_t";
    case 34: return "char8_t";
    default: return "base" + std::to_string(n * 8);
    }
}

static bool memberOf(ULONG udt, const std::string& name, ULONG& type, DWORD& offset);
static bool shalimarArray(ULONG t, ULONG& element, DWORD& countAt, DWORD& dataAt);

// a C++ reference, or a Shalimar parameter passed by reference: shown and used as what it refers to
static bool isReference(ULONG t) { BOOL r = FALSE; return tagOf(t) == TagPointer && SymGetTypeInfo(process, typeModule, t, TI_GET_IS_REFERENCE, &r) && r; }

static std::string typeName(ULONG t)
{
    switch (tagOf(t)) {
    case TagPointer: {
        // a Shalimar array variable holds a pointer to the runtime's descriptor: its type is the array's ("int[]")
        ULONG el; DWORD countAt, dataAt;
        if (shalimarArray(targetOf(t), el, countAt, dataAt)) return symName(plain(targetOf(t)));
        return typeName(targetOf(t)) + (isReference(t) ? " &" : " *");
    }
    case TagArray: {
        DWORD count = 0;
        SymGetTypeInfo(process, typeModule, t, TI_GET_COUNT, &count);
        return typeName(targetOf(t)) + " [" + std::to_string(count) + "]";
    }
    case TagBase: return baseName(t);
    case TagUDT: { DWORD kind = 0; SymGetTypeInfo(process, typeModule, t, TI_GET_UDTKIND, &kind); return std::string(kind == 1 ? "class " : kind == 2 ? "union " : "struct ") + symName(t); }
    case TagEnum: return "enum " + symName(t);
    case TagTypedef: return symName(t);
    case TagFunctionType: return "<function>";
    default: return "?";
    }
}

static std::vector<ULONG> childrenOf(ULONG t)
{
    std::vector<ULONG> kids;
    DWORD count = 0;
    if (!SymGetTypeInfo(process, typeModule, t, TI_GET_CHILDRENCOUNT, &count) || count == 0) return kids;
    std::vector<BYTE> buffer(sizeof(TI_FINDCHILDREN_PARAMS) + count * sizeof(ULONG));
    TI_FINDCHILDREN_PARAMS* p = reinterpret_cast<TI_FINDCHILDREN_PARAMS*>(buffer.data());
    p->Count = count;
    p->Start = 0;
    if (!SymGetTypeInfo(process, typeModule, t, TI_FINDCHILDREN, p)) return kids;
    for (DWORD i = 0; i < count; i++) kids.push_back(p->ChildId[i]);
    return kids;
}

// a Shalimar array: the runtime's descriptor {count, element, data}, which Compiler-Si's CodeView names by
// the array's own type ("int[]", "real[][]"); where its count is, and what its data points at
static bool shalimarArray(ULONG t, ULONG& element, DWORD& countAt, DWORD& dataAt)
{
    t = plain(t);
    if (tagOf(t) != TagUDT) return false;
    std::string name = symName(t);
    if (name.size() < 3 || name.compare(name.size() - 2, 2, "[]") != 0) return false;
    ULONG countType = 0, kindType = 0, dataType = 0;
    DWORD kindAt = 0;
    if (!memberOf(t, "count", countType, countAt) || !memberOf(t, "element", kindType, kindAt) || !memberOf(t, "data", dataType, dataAt)) return false;
    if (tagOf(plain(dataType)) != TagPointer) return false;
    element = targetOf(plain(dataType));
    return true;
}

static unsigned long long unsignedAt(DWORD64 at, ULONG type)
{
    size_t n = static_cast<size_t>(lengthOf(plain(type)));
    unsigned long long v = 0;
    if (n == 0 || n > 8 || !readMemory(at, &v, n)) return 0;
    return v;
}

// cpp11's containers, by the names of its include folder (in std, or bare as cpp11 1.29 names them) and the
// members they keep: vector<T> {items_, size_}, initializer_list<T> {first_, count_}, basic_string<C>
// {heap_, len_, small_} - where the elements are, how many, and of what type
enum class Holds { Nothing, Elements, Text };

static Holds container(ULONG t, DWORD64 at, ULONG& element, DWORD64& first, unsigned long long& count)
{
    t = plain(t);
    BYTE probe = 0;
    if (tagOf(t) != TagUDT || !readMemory(at, &probe, 1)) return Holds::Nothing;     // not there: shown member by member, unreadable
    std::string name = symName(t);
    if (name.compare(0, 5, "std::") == 0) name = name.substr(5);
    ULONG a = 0, b = 0, c = 0;
    DWORD ao = 0, bo = 0, co = 0;
    if (name.compare(0, 7, "vector<") == 0 && memberOf(t, "items_", a, ao) && memberOf(t, "size_", b, bo) && tagOf(plain(a)) == TagPointer) {
        element = targetOf(plain(a)); first = peek64(at + ao); count = unsignedAt(at + bo, b);
        return Holds::Elements;
    }
    if (name.compare(0, 17, "initializer_list<") == 0 && memberOf(t, "first_", a, ao) && memberOf(t, "count_", b, bo) && tagOf(plain(a)) == TagPointer) {
        element = targetOf(plain(a)); first = peek64(at + ao); count = unsignedAt(at + bo, b);
        return Holds::Elements;
    }
    if (name.compare(0, 13, "basic_string<") == 0 && memberOf(t, "heap_", a, ao) && memberOf(t, "len_", b, bo) && memberOf(t, "small_", c, co) && tagOf(plain(a)) == TagPointer) {
        element = targetOf(plain(a));
        DWORD64 heap = peek64(at + ao);
        first = heap ? heap : at + co;
        count = unsignedAt(at + bo, b);
        return Holds::Text;
    }
    return Holds::Nothing;
}

static long long signedOf(const BYTE* b, size_t n)
{
    switch (n) {
    case 1: return static_cast<signed char>(b[0]);
    case 2: { short v; memcpy(&v, b, 2); return v; }
    case 4: { int v; memcpy(&v, b, 4); return v; }
    default: { long long v; memcpy(&v, b, 8); return v; }
    }
}

static unsigned long long unsignedOf(const BYTE* b, size_t n)
{
    unsigned long long v = 0;
    memcpy(&v, b, n < 8 ? n : 8);
    return v;
}

static std::string formatValue(ULONG t, DWORD64 at, int depth, bool compact);

static std::string pointerText(DWORD64 p) { return "0x" + hex64(p); }

static std::string quoted(DWORD64 at, size_t most)
{
    std::string s;
    for (size_t i = 0; i < most; i++) {
        char ch = 0;
        if (!readMemory(at + i, &ch, 1)) return s.empty() ? std::string() : "\"" + s + "\"";
        if (ch == 0) break;
        if (ch == '\n') s += "\\n"; else if (ch == '"') s += "\\\""; else if (static_cast<unsigned char>(ch) < 32) s += '.'; else s += ch;
        if (i + 1 == most) s += "...";
    }
    return "\"" + s + "\"";
}

// `count` units of text at `at`, one byte (UTF-8 as it is) or two (UTF-16) each, quoted as quoted() quotes
static std::string textAt(DWORD64 at, unsigned long long count, size_t unit)
{
    unsigned long long most = count < 256 ? count : 256;
    std::string s;
    for (unsigned long long i = 0; i < most; i++) {
        unsigned v = 0;
        if (!readMemory(at + i * unit, &v, unit)) return "<memory not readable>";
        if (v == '\n') s += "\\n"; else if (v == '"') s += "\\\""; else if (v < 32) s += '.';
        else if (unit == 1 || v < 128) s += static_cast<char>(v);
        else s += utf8(std::wstring(1, static_cast<wchar_t>(v)));
    }
    if (count > most) s += "...";
    return "\"" + s + "\"";
}

// `count` elements of type `element` from `at`, as an array shows them: {5, 7, 13}, a char array as text
static std::string elements(ULONG element, DWORD64 at, unsigned long long count, int depth, bool compact)
{
    ULONG el = plain(element);
    ULONG64 size = lengthOf(el);
    if (tagOf(el) == TagBase && size == 1) { std::string q = quoted(at, static_cast<size_t>(count < 256 ? count : 256)); return q.empty() ? "<memory not readable>" : q; }
    if (depth > 2) return "{...}";
    const char* sep = compact ? "," : ", ";
    std::string s = "{";
    for (unsigned long long i = 0; i < count && i < 8; i++) { if (i) s += sep; s += formatValue(el, at + i * size, depth + 1, compact); }
    if (count > 8) s += std::string(sep) + "...";
    return s + "}";
}

static std::string formatValue(ULONG t, DWORD64 at, int depth, bool compact)
{
    const char* sep = compact ? "," : ", ";
    ULONG p = plain(t);
    DWORD tag = tagOf(p);
    if (isReference(p)) { DWORD64 to = peek64(at); return to ? formatValue(targetOf(p), to, depth, compact) : pointerText(0); }
    // what a language's own types hold, as it shows them: a Shalimar array (the variable, a pointer to the
    // runtime's descriptor, or the descriptor itself) by its elements; cpp11's containers by what they hold
    {
        ULONG el = 0;
        DWORD countAt = 0, dataAt = 0;
        DWORD64 descriptor = 0;
        if (tag == TagPointer && shalimarArray(targetOf(p), el, countAt, dataAt)) descriptor = peek64(at);
        else if (tag == TagUDT && shalimarArray(p, el, countAt, dataAt)) descriptor = at;
        if (descriptor) {
            int count = 0;
            if (!readMemory(descriptor + countAt, &count, 4)) return "<memory not readable>";
            return elements(el, peek64(descriptor + dataAt), count < 0 ? 0 : static_cast<unsigned long long>(count), depth, compact);
        }
        DWORD64 first = 0;
        unsigned long long count = 0;
        Holds holds = tag == TagUDT ? container(p, at, el, first, count) : Holds::Nothing;
        if (holds == Holds::Text) return textAt(first, count, static_cast<size_t>(lengthOf(plain(el)) == 2 ? 2 : 1));
        if (holds == Holds::Elements) return elements(el, first, count, depth, compact);
    }
    ULONG64 n = lengthOf(p);
    BYTE b[16] = { 0 };
    if (tag == TagBase || tag == TagPointer || tag == TagEnum) {
        if (!readMemory(at, b, n < 16 ? static_cast<size_t>(n) : 16)) return "<memory not readable>";
    }
    if (tag == TagBase) {
        DWORD bt = 0;
        SymGetTypeInfo(process, typeModule, p, TI_GET_BASETYPE, &bt);
        char text[96];
        if (bt == 8) {
            if (n == 4) { float f; memcpy(&f, b, 4); snprintf(text, sizeof text, "%.9g", f); }
            else { double d; memcpy(&d, b, 8); snprintf(text, sizeof text, "%.17g", d); }
            return text;
        }
        if (bt == 10) return b[0] ? "true" : "false";
        if ((bt == 2 || (bt == 6 && n == 1)) && n == 1) {
            int v = static_cast<signed char>(b[0]);
            if (v >= 32 && v < 127) snprintf(text, sizeof text, compact ? "%d('%c')" : "%d '%c'", v, v);
            else snprintf(text, sizeof text, "%d", v);
            return text;
        }
        if (bt == 7 || bt == 14 || bt == 3 || bt == 32 || bt == 33 || bt == 34) { snprintf(text, sizeof text, "%llu", unsignedOf(b, static_cast<size_t>(n))); return text; }
        snprintf(text, sizeof text, "%lld", signedOf(b, static_cast<size_t>(n)));
        return text;
    }
    if (tag == TagPointer) {
        DWORD64 v = unsignedOf(b, 8);
        std::string s = pointerText(v);
        ULONG to = plain(targetOf(p));
        if (!compact && tagOf(to) == TagBase && lengthOf(to) == 1 && v) { std::string q = quoted(v, 64); if (!q.empty()) s += " " + q; }
        if (!compact && tagOf(to) == TagFunctionType && v) s += " " + where(v);
        if (!compact && tagOf(to) == TagUDT && v && depth < 2) s += " " + formatValue(to, v, depth + 1, false);
        return s;
    }
    if (tag == TagEnum) {
        long long v = signedOf(b, static_cast<size_t>(n));
        for (ULONG kid : childrenOf(p)) {
            VARIANT var;
            memset(&var, 0, sizeof var);
            if (!SymGetTypeInfo(process, typeModule, kid, TI_GET_VALUE, &var)) continue;
            long long k = var.vt == VT_I8 ? var.llVal : var.vt == VT_UI8 ? static_cast<long long>(var.ullVal) : var.vt == VT_I2 ? var.iVal : var.vt == VT_UI2 ? var.uiVal :
                          var.vt == VT_I1 ? var.cVal : var.vt == VT_UI1 ? var.bVal : var.vt == VT_UI4 ? static_cast<long long>(var.ulVal) : var.lVal;
            if (k == v) return compact ? symName(kid) + "(" + std::to_string(v) + ")" : symName(kid) + " (" + std::to_string(v) + ")";
        }
        return std::to_string(v);
    }
    if (tag == TagArray) {
        DWORD count = 0;
        SymGetTypeInfo(process, typeModule, p, TI_GET_COUNT, &count);
        ULONG el = plain(targetOf(p));
        ULONG64 size = lengthOf(el);
        if (tagOf(el) == TagBase && size == 1) { std::string q = quoted(at, count < 64 ? count : 64); return q.empty() ? "<memory not readable>" : q; }
        if (depth > 2) return "{...}";
        std::string s = "{";
        for (DWORD i = 0; i < count && i < 8; i++) { if (i) s += sep; s += formatValue(el, at + i * size, depth + 1, compact); }
        if (count > 8) s += std::string(sep) + "...";
        return s + "}";
    }
    if (tag == TagUDT) {
        if (depth > 2) return "{...}";
        std::string s = "{";
        int shown = 0;
        for (ULONG kid : childrenOf(p)) {
            DWORD kt = tagOf(kid);
            if (kt != TagData && kt != TagBaseClass) continue;
            DWORD kind = 0;
            if (kt == TagData && (!SymGetTypeInfo(process, typeModule, kid, TI_GET_DATAKIND, &kind) || kind != 7)) continue;     // members only
            DWORD off = 0;
            SymGetTypeInfo(process, typeModule, kid, TI_GET_OFFSET, &off);
            if (shown == 8) { s += std::string(sep) + "..."; break; }
            if (shown++) s += sep;
            ULONG kidType = targetOf(kid) ? targetOf(kid) : kid;
            s += symName(kt == TagBaseClass ? kidType : kid) + "=" + formatValue(kidType, at + off, depth + 1, compact);
        }
        return s + "}";
    }
    return "?";
}

// ---- frames ---------------------------------------------------------------------------------------
struct Frame { CONTEXT c; DWORD64 pc = 0; };
static std::vector<Frame> frames;
static size_t currentFrame = 0;
static bool framesRead = false;

static void readFrames()
{
    if (framesRead) return;
    framesRead = true;
    frames.clear();
    CONTEXT c = context();
    STACKFRAME64 f;
    memset(&f, 0, sizeof f);
    f.AddrPC.Offset = c.Rip; f.AddrPC.Mode = AddrModeFlat;
    f.AddrFrame.Offset = c.Rbp; f.AddrFrame.Mode = AddrModeFlat;
    f.AddrStack.Offset = c.Rsp; f.AddrStack.Mode = AddrModeFlat;
    for (int n = 0; n < 64; n++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &f, &c, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) break;
        if (f.AddrPC.Offset == 0) break;
        Frame fr;
        fr.c = c;
        fr.pc = f.AddrPC.Offset;
        frames.push_back(fr);
    }
    if (frames.empty()) { Frame fr; fr.c = c; fr.pc = c.Rip; frames.push_back(fr); }
}

static void forgetFrames() { framesRead = false; currentFrame = 0; }

static const Frame& frameNow()
{
    readFrames();
    if (currentFrame >= frames.size()) currentFrame = 0;
    return frames[currentFrame];
}

// the address to look scopes up at: a caller's return address can be past its call's line
static DWORD64 scopeAddress(size_t i) { readFrames(); return i == 0 ? frames[0].pc : frames[i].pc - 1; }

static std::string frameLine(size_t i)
{
    readFrames();
    const Frame& f = frames[i];
    DWORD64 ret = i + 1 < frames.size() ? frames[i + 1].pc : 0;
    std::string s = hex64(f.c.Rsp) + " " + hex64(ret) + "     " + where(f.pc);
    Line l = lineAt(scopeAddress(i));
    if (l.ok) s += " [" + l.file + " @ " + std::to_string(l.number) + "]";
    return s;
}

// ---- locals ---------------------------------------------------------------------------------------
struct Variable { std::string name; ULONG type = 0; DWORD64 mod = 0; DWORD64 at = 0; bool inRegister = false; ULONG reg = 0; std::string place; bool param = false; };

struct Gather { std::vector<Variable>* to; const CONTEXT* c; const char* only; };

static BOOL CALLBACK onLocal(PSYMBOL_INFO s, ULONG, PVOID ctx)
{
    Gather* g = static_cast<Gather*>(ctx);
    if (g->only && strcmp(g->only, s->Name) != 0) return TRUE;
    if (!(s->Flags & (SYMFLAG_LOCAL | SYMFLAG_PARAMETER)) && s->Tag != 7) return TRUE;
    if (!g->only && (s->Name[0] == '.' || s->Name[0] == '$')) return TRUE;     // the compiler's own temporaries (cpp11's .ilbacking0)
    Variable v;
    v.name = s->Name;
    v.type = s->TypeIndex;
    v.mod = s->ModBase;
    v.param = (s->Flags & SYMFLAG_PARAMETER) != 0;
    char b[64];
    if (s->Flags & SYMFLAG_REGISTER) { v.inRegister = true; v.reg = s->Register; v.place = std::string("@") + cvRegisterName(s->Register); }
    else if (s->Flags & SYMFLAG_REGREL) {
        v.at = cvRegister(*g->c, s->Register) + s->Address;
        long long off = static_cast<long long>(static_cast<LONG>(s->Address));
        snprintf(b, sizeof b, "@%s%s0x%llx", cvRegisterName(s->Register), off < 0 ? "-" : "+", static_cast<unsigned long long>(off < 0 ? -off : off));
        v.place = b;
    }
    else if (s->Flags & SYMFLAG_FRAMEREL) {
        v.at = g->c->Rbp + s->Address;
        long long off = static_cast<long long>(static_cast<LONG>(s->Address));
        snprintf(b, sizeof b, "@rbp%s0x%llx", off < 0 ? "-" : "+", static_cast<unsigned long long>(off < 0 ? -off : off));
        v.place = b;
    }
    else { v.at = s->Address; v.place = "static"; }
    g->to->push_back(v);
    return TRUE;
}

static std::vector<Variable> localsOf(size_t frame, const char* only)
{
    std::vector<Variable> found;
    readFrames();
    if (frame >= frames.size()) return found;
    const CONTEXT& c = frames[frame].c;
    IMAGEHLP_STACK_FRAME f;
    memset(&f, 0, sizeof f);
    f.InstructionOffset = scopeAddress(frame);
    f.FrameOffset = c.Rbp;
    f.StackOffset = c.Rsp;
    SymSetContext(process, &f, NULL);
    Gather g = { &found, &c, only };
    SymEnumSymbols(process, 0, only ? only : NULL, onLocal, &g);
    return found;
}

static std::string valueOf(const Variable& v, bool compact)
{
    typeModule = v.mod;
    if (v.inRegister) return std::to_string(static_cast<long long>(cvRegister(frameNow().c, v.reg)));
    return formatValue(v.type, v.at, 0, compact);
}

// ---- ?? expressions: names, . -> [] * & ( ), numbers --------------------------------------------------
struct Value { DWORD64 mod = 0; ULONG type = 0; DWORD64 at = 0; bool addressOf = false; bool isNumber = false; long long number = 0; bool ok = false; std::string error; };

struct Parser {
    std::string s;
    size_t i = 0;
    void space() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++; }
    bool eat(const char* t) { space(); size_t n = strlen(t); if (s.compare(i, n, t) == 0) { i += n; return true; } return false; }
    std::string ident() {
        space();
        size_t a = i;
        while (i < s.size() && (isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_' || s[i] == ':')) i++;
        return s.substr(a, i - a);
    }
};

static Value fail(const std::string& why) { Value v; v.error = why; return v; }

static bool memberOf(ULONG udt, const std::string& name, ULONG& type, DWORD& offset)
{
    for (ULONG kid : childrenOf(plain(udt))) {
        DWORD kt = tagOf(kid);
        DWORD off = 0;
        SymGetTypeInfo(process, typeModule, kid, TI_GET_OFFSET, &off);
        if (kt == TagData && symName(kid) == name) { type = targetOf(kid); offset = off; return true; }
        if (kt == TagBaseClass) {
            ULONG t2; DWORD o2;
            if (memberOf(targetOf(kid) ? targetOf(kid) : kid, name, t2, o2)) { type = t2; offset = off + o2; return true; }
        }
    }
    return false;
}

// a reference's value is what it refers to: x of "x[]: real" passed by reference is the array
static Value referent(Value v)
{
    if (!v.ok || v.isNumber || v.addressOf) return v;
    typeModule = v.mod;
    ULONG t = plain(v.type);
    if (isReference(t)) { v.at = peek64(v.at); v.type = targetOf(t); }
    return v;
}

static Value lookup(const std::string& name)
{
    std::vector<Variable> vs = localsOf(currentFrame, name.c_str());
    if (!vs.empty()) {
        Value v; v.mod = vs[0].mod; v.type = vs[0].type; v.at = vs[0].at; v.ok = !vs[0].inRegister;
        if (vs[0].inRegister) { v.isNumber = true; v.number = static_cast<long long>(cvRegister(frameNow().c, vs[0].reg)); v.ok = true; }
        return referent(v);
    }
    char buffer[sizeof(SYMBOL_INFO) + 1024];
    SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
    memset(buffer, 0, sizeof buffer);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 1023;
    if (SymFromName(process, name.c_str(), sym) && sym->TypeIndex) { Value v; v.mod = sym->ModBase; v.type = sym->TypeIndex; v.at = sym->Address; v.ok = true; return v; }
    // a member of *this, as a member function sees it
    std::vector<Variable> self = localsOf(currentFrame, "this");
    if (!self.empty()) {
        typeModule = self[0].mod;
        DWORD64 obj = peek64(self[0].at);
        ULONG t; DWORD off;
        if (memberOf(targetOf(plain(self[0].type)), name, t, off)) { Value v; v.mod = self[0].mod; v.type = t; v.at = obj + off; v.ok = true; return v; }
    }
    // no local, parameter, global or member of this here: what a watch of another function's variable says
    return fail("Couldn't resolve error: not in scope here");
}

static Value unary(Parser& p);

// an index's number: a literal, or a variable's value
static long long integerOf(const Value& v)
{
    if (v.isNumber) return v.number;
    typeModule = v.mod;
    size_t n = static_cast<size_t>(lengthOf(plain(v.type)));
    if (n == 0 || n > 8) return 0;
    BYTE b[8] = { 0 };
    readMemory(v.at, b, n);
    return signedOf(b, n);
}

static Value postfix(Parser& p, Value v)
{
    for (;;) {
        if (!v.ok) return v;
        typeModule = v.mod;
        if (p.eat("->")) {
            ULONG t = plain(v.type);
            if (tagOf(t) != TagPointer) return fail("Couldn't resolve error: '->' on a value that is not a pointer");
            DWORD64 obj = peek64(v.at);
            std::string m = p.ident();
            ULONG mt; DWORD off;
            if (!memberOf(targetOf(t), m, mt, off)) return fail("Couldn't resolve error: " + typeName(plain(targetOf(t))) + " has no member " + m);
            v.type = mt; v.at = obj + off;
            v = referent(v);
        }
        else if (p.eat(".")) {
            std::string m = p.ident();
            ULONG mt; DWORD off;
            if (!memberOf(v.type, m, mt, off)) return fail("Couldn't resolve error: " + typeName(plain(v.type)) + " has no member " + m);
            v.type = mt; v.at += off;
            v = referent(v);
        }
        else if (p.eat("[")) {
            Value index = unary(p);
            if (!p.eat("]")) return fail("Syntax error: ']' expected");
            if (!index.ok) return index;
            long long k = integerOf(index);
            typeModule = v.mod;
            ULONG t = plain(v.type);
            // a Shalimar array's element, and a cpp11 vector's, string's or initializer_list's, as the language indexes them
            {
                ULONG el = 0;
                DWORD countAt = 0, dataAt = 0;
                DWORD64 first = 0;
                unsigned long long count = 0;
                bool shalimar = tagOf(t) == TagPointer && shalimarArray(targetOf(t), el, countAt, dataAt);
                if (shalimar) first = peek64(peek64(v.at) + dataAt);
                if (shalimar || container(t, v.at, el, first, count) != Holds::Nothing) {
                    v.type = el; v.at = first + static_cast<DWORD64>(k * static_cast<long long>(lengthOf(plain(el))));
                    continue;
                }
            }
            ULONG el = targetOf(t);
            DWORD64 base = tagOf(t) == TagPointer ? peek64(v.at) : v.at;
            if (tagOf(t) != TagPointer && tagOf(t) != TagArray) return fail("Couldn't resolve error: '[]' on a value that is not an array");
            v.type = el; v.at = base + static_cast<DWORD64>(k * static_cast<long long>(lengthOf(plain(el))));
        }
        else return v;
    }
}

static Value primary(Parser& p)
{
    p.space();
    if (p.eat("(")) {
        Value v = unary(p);
        if (!p.eat(")")) return fail("Syntax error: ')' expected");
        return postfix(p, v);
    }
    if (p.i < p.s.size() && (isdigit(static_cast<unsigned char>(p.s[p.i])) || p.s[p.i] == '-')) {
        char* end = NULL;
        const char* from = p.s.c_str() + p.i;
        long long n = (strncmp(from, "0n", 2) == 0) ? strtoll(from + 2, &end, 10) : strtoll(from, &end, 0);
        p.i = static_cast<size_t>(end - p.s.c_str());
        Value v; v.isNumber = true; v.number = n; v.ok = true;
        return v;
    }
    std::string name = p.ident();
    if (name.empty()) return fail("Syntax error in '" + p.s + "'");
    return postfix(p, lookup(name));
}

static Value unary(Parser& p)
{
    if (p.eat("*")) {
        Value v = unary(p);
        if (!v.ok) return v;
        typeModule = v.mod;
        ULONG t = plain(v.type);
        if (tagOf(t) == TagPointer) { v.at = peek64(v.at); v.type = targetOf(t); return v; }
        if (tagOf(t) == TagArray) { v.type = targetOf(t); return v; }
        return fail("Couldn't resolve error: '*' on a value that is not a pointer");
    }
    if (p.eat("&")) {
        Value v = unary(p);
        if (!v.ok || v.isNumber) return v.ok ? fail("Couldn't resolve error: '&' of a number") : v;
        v.addressOf = true;
        return v;
    }
    return primary(p);
}

static Value evaluate(const std::string& text)
{
    Parser p;
    p.s = text;
    Value v = unary(p);
    p.space();
    if (v.ok && p.i < p.s.size()) return fail("Syntax error in '" + text + "'");
    return v;
}

static std::string shown(const Value& v)
{
    if (v.isNumber) return "int64 " + std::to_string(v.number);
    typeModule = v.mod;
    if (v.addressOf) return typeName(v.type) + " * " + pointerText(v.at);
    return typeName(v.type) + " " + formatValue(v.type, v.at, 0, true);
}

static std::string assign(const std::string& left, const std::string& right)
{
    Value l = evaluate(left);
    if (!l.ok) return l.error;
    if (l.isNumber || l.addressOf) return "Couldn't resolve error: not an lvalue";
    typeModule = l.mod;
    ULONG t = plain(l.type);
    DWORD tag = tagOf(t);
    size_t n = static_cast<size_t>(lengthOf(t));
    std::string r = trim(right);
    BYTE b[8] = { 0 };
    if (tag == TagBase) {
        DWORD bt = 0;
        SymGetTypeInfo(process, typeModule, t, TI_GET_BASETYPE, &bt);
        if (bt == 8) {
            double d = strtod(r.c_str(), NULL);
            if (n == 4) { float f = static_cast<float>(d); memcpy(b, &f, 4); } else memcpy(b, &d, 8);
        }
        else if (bt == 10) { b[0] = (r == "true" || (r != "false" && strtoll(r.c_str(), NULL, 0) != 0)) ? 1 : 0; }
        else if (r.size() == 3 && r[0] == '\'' && r[2] == '\'') b[0] = static_cast<BYTE>(r[1]);
        else { Value rv = evaluate(r); long long k = rv.isNumber ? rv.number : 0; if (!rv.isNumber) return rv.ok ? "Couldn't resolve error: a number is expected" : rv.error; memcpy(b, &k, n < 8 ? n : 8); }
    }
    else if (tag == TagPointer || tag == TagEnum) { Value rv = evaluate(r); if (!rv.isNumber) return "Couldn't resolve error: a number is expected"; memcpy(b, &rv.number, n < 8 ? n : 8); }
    else return "Couldn't resolve error: only a number, a pointer or an enum can be set here";
    if (n == 0 || n > 8 || !writeMemory(l.at, b, n)) return "Couldn't resolve error: memory not writable";
    return shown(l);
}

// ---- the answers ------------------------------------------------------------------------------------
static void sayStop(Stop s)
{
    forgetFrames();
    programOutput(s == Stop::Exit);
    if (s == Stop::Exit) {
        sayLine("ntdll!NtTerminateProcess+0x14:");
        return;
    }
    DWORD64 rip = context().Rip;
    if (s == Stop::Crash) say("(%lx.%lx): %s - code %08lx (!!! second chance !!!)\n", pid, tid, exceptionName(crashCode).c_str(), crashCode);
    if (s == Stop::Breakpoint) {
        say("Breakpoint %d hit\n", hitBreakpoint >= 0 && static_cast<size_t>(hitBreakpoint) < breakpoints.size() ? breakpoints[static_cast<size_t>(hitBreakpoint)].id : 0);
        dropOnce(hitBreakpoint);
        hitBreakpoint = -1;
    }
    // a one-line function stepped into is shown at its line: "+0x0" says the entry is where to stay (cdb's bare
    // "module!function:" would have GRIDE step on to a first statement that is not there)
    say("%s%s:\n", where(rip).c_str(), s == Stop::Entry ? "+0x0" : "");
    BYTE code[8] = { 0 };
    readMemory(rip, code, sizeof code);
    for (size_t i = 0; i < sizeof code; i++) { auto at = patched.find(rip + i); if (at != patched.end()) code[i] = at->second.saved; }
    say("%s %02x%02x%02x%02x\n", hex64(rip).c_str(), code[0], code[1], code[2], code[3]);
}

static void sayWhere()
{
    if (exited) { sayLine("No runnable debuggees"); return; }
    DWORD64 a = scopeAddress(currentFrame);
    Line l = lineAt(a);
    if (l.ok) say("%s(%lu)\n", l.file.c_str(), l.number);
    Sym s = symbolAt(frameNow().pc);
    if (s.ok) say("(%s)   %s\n", hex64(s.start).c_str(), where(frameNow().pc).c_str());
}

static void sayStack()
{
    readFrames();
    sayLine("Child-SP          RetAddr               Call Site");
    for (size_t i = 0; i < frames.size(); i++) sayLine(frameLine(i));
}

static void sayLocals()
{
    std::vector<Variable> vs = localsOf(currentFrame, NULL);
    if (vs.empty()) { sayLine("Unable to enumerate locals"); return; }
    for (const Variable& v : vs) {
        typeModule = v.mod;
        std::string at = v.inRegister ? std::string("@") + cvRegisterName(v.reg) : hex64(v.at);
        say("%s %s %s %s = %s\n", at.c_str(), v.place.c_str(), typeName(v.type).c_str(), v.name.c_str(), valueOf(v, false).c_str());
    }
}

static void sayRegisters()
{
    const CONTEXT& c = frameNow().c;
    say("rax=%016llx rbx=%016llx rcx=%016llx\n", c.Rax, c.Rbx, c.Rcx);
    say("rdx=%016llx rsi=%016llx rdi=%016llx\n", c.Rdx, c.Rsi, c.Rdi);
    say("rip=%016llx rsp=%016llx rbp=%016llx\n", c.Rip, c.Rsp, c.Rbp);
    say(" r8=%016llx  r9=%016llx r10=%016llx\n", c.R8, c.R9, c.R10);
    say("r11=%016llx r12=%016llx r13=%016llx\n", c.R11, c.R12, c.R13);
    say("r14=%016llx r15=%016llx\n", c.R14, c.R15);
    say("efl=%08lx\n", c.EFlags);
}

static void sayMemory(const std::string& args)
{
    std::string a = trim(args);
    size_t l = a.find(" L");
    unsigned long long count = 128;
    if (l != std::string::npos) { count = strtoull(a.c_str() + l + 2, NULL, 0); a = trim(a.substr(0, l)); }
    std::string digits;
    for (char ch : a) if (ch != '`') digits += ch;
    DWORD64 at = strtoull(digits.c_str(), NULL, 16);
    for (unsigned long long row = 0; row < count; row += 16) {
        BYTE b[16];
        size_t n = static_cast<size_t>(count - row < 16 ? count - row : 16);
        if (!readMemory(at + row, b, n)) { say("%s  ?? ?? ?? ??\n", hex64(at + row).c_str()); return; }
        std::string hex, text;
        for (size_t i = 0; i < n; i++) {
            char x[8];
            snprintf(x, sizeof x, i == 8 ? "-%02x" : (i == 0 ? "%02x" : " %02x"), b[i]);
            hex += x;
            text += b[i] >= 32 && b[i] < 127 ? static_cast<char>(b[i]) : '.';
        }
        say("%s  %-48s  %s\n", hex64(at + row).c_str(), hex.c_str(), text.c_str());
    }
}

static void sayPrintf(const std::string& rest)
{
    size_t q1 = rest.find('"'), q2 = rest.rfind('"');
    if (q1 == std::string::npos || q2 <= q1) return;
    std::string format = rest.substr(q1 + 1, q2 - q1 - 1);
    std::vector<long long> args;
    std::string tail = rest.substr(q2 + 1);
    for (size_t c = tail.find(','); c != std::string::npos; c = tail.find(',', c + 1)) args.push_back(strtoll(trim(tail.substr(c + 1)).c_str(), NULL, 0));
    std::string text;
    size_t next = 0;
    for (size_t i = 0; i < format.size(); i++) {
        if (format[i] == '\\' && i + 1 < format.size()) { char e = format[++i]; text += e == 'n' ? '\n' : e == 't' ? '\t' : e; continue; }
        if (format[i] == '%' && i + 1 < format.size()) {
            char f = format[++i];
            long long v = next < args.size() ? args[next++] : 0;
            if (f == 'c') text += static_cast<char>(v);
            else if (f == 'd') text += std::to_string(v);
            else if (f == 'x') { char b[32]; snprintf(b, sizeof b, "%llx", static_cast<unsigned long long>(v)); text += b; }
            else if (f == '%') { text += '%'; next--; }
            continue;
        }
        text += format[i];
    }
    say("%s", text.c_str());
}

static void setBreakpoint(const std::string& args)
{
    std::string a = trim(args);
    bool once = false;
    if (a.compare(0, 2, "/1") == 0) { once = true; a = trim(a.substr(2)); }
    std::string spec;
    for (char ch : a) if (ch != '`') spec += ch;
    size_t colon = spec.rfind(':');
    if (colon == std::string::npos || colon == 0) { say("Couldn't resolve error at '%s'\n", spec.c_str()); return; }
    std::string file = spec.substr(0, colon);
    DWORD line = static_cast<DWORD>(strtoul(spec.c_str() + colon + 1, NULL, 10));
    Breakpoint b;
    b.id = nextBreakpoint++;
    b.spec = spec;
    b.once = once;
    if (!resolve(file, line, b.at)) { say("Couldn't resolve error at '`%s`'\n", spec.c_str()); return; }
    for (DWORD64 x : b.at) patch(x);
    breakpoints.push_back(b);
}

static void clearBreakpoints()
{
    for (const Breakpoint& b : breakpoints) for (DWORD64 a : b.at) unpatch(a);
    breakpoints.clear();
}

static void quit()
{
    if (!exited) TerminateProcess(process, 0);
    resume();
    if (pcon) ClosePseudoConsole(pcon);
    ExitProcess(0);
}

// ---- starting the program ---------------------------------------------------------------------------
static bool start(const std::string& program, const std::string& commandLine, const std::string& symbols)
{
    COORD size = { 400, 9000 };
    HANDLE inRead = NULL, outWrite = NULL;
    bool console = CreatePipe(&inRead, &pcIn, NULL, 0) && CreatePipe(&pcOut, &outWrite, NULL, 0) &&
                   SUCCEEDED(CreatePseudoConsole(size, inRead, outWrite, 0, &pcon));
    STARTUPINFOEXA si;
    memset(&si, 0, sizeof si);
    si.StartupInfo.cb = sizeof si;
    // the program's standard handles are its console's, not this debugger's pipes from GRIDE
    if (console) si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    std::vector<BYTE> attributes;
    DWORD flags = DEBUG_ONLY_THIS_PROCESS;
    if (console) {
        SIZE_T n = 0;
        InitializeProcThreadAttributeList(NULL, 1, 0, &n);
        attributes.resize(n);
        si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        if (InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &n) &&
            UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pcon, sizeof pcon, NULL, NULL))
            flags |= EXTENDED_STARTUPINFO_PRESENT;
        else console = false;
    }
    std::string cmd = commandLine;
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(program.c_str(), &cmd[0], NULL, NULL, FALSE, flags, NULL, NULL, &si.StartupInfo, &pi)) {
        say("cannot start %s (error %lu)\n", program.c_str(), GetLastError());
        return false;
    }
    if (inRead) CloseHandle(inRead);
    if (outWrite) CloseHandle(outWrite);
    process = pi.hProcess;
    pid = pi.dwProcessId;
    if (console) CreateThread(NULL, 0, drainConsole, NULL, 0, NULL);
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
    SymInitialize(process, symbols.empty() ? NULL : symbols.c_str(), FALSE);
    const char* slash = strrchr(program.c_str(), '\\');
    std::string leaf = slash ? slash + 1 : program;
    size_t dot = leaf.rfind('.');
    moduleName = dot == std::string::npos ? leaf : leaf.substr(0, dot);
    if (waitEvent() != Event::Breakpoint) { sayLine("the program did not reach its first instruction"); return false; }   // the loader's breakpoint
    if (console) {
        FreeConsole();
        if (AttachConsole(pid)) {
            screen = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            attached = screen != INVALID_HANDLE_VALUE;
            if (attached) SetConsoleOutputCP(CP_UTF8);
        }
    }
    return true;
}

int main(int argc, char** argv)
{
    out = GetStdHandle(STD_OUTPUT_HANDLE);
    in = GetStdHandle(STD_INPUT_HANDLE);
    std::string symbols, program, commandLine;
    int a = 1;
    for (; a < argc; a++) {
        std::string w = argv[a];
        if (w == "--version" || w == "-version") { say("gride-dbg %s - GRIDE's debugger for x86_64-windows programs (Win32 debug API, dbghelp)\n", GRIDE_DBG_VERSION); return 0; }
        if (w == "-y" && a + 1 < argc) { symbols = argv[++a]; continue; }
        break;
    }
    if (a >= argc) { say("usage: gride-dbg [-y <symbol dir>] <program> [arguments...]\n"); return 2; }
    char full[MAX_PATH];
    program = GetFullPathNameA(argv[a], MAX_PATH, full, NULL) ? full : argv[a];
    programPath = program;
    commandLine = "\"" + program + "\"";
    for (int i = a + 1; i < argc; i++) commandLine += std::string(" \"") + argv[i] + "\"";
    InitializeCriticalSection(&heardLock);
    heardEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    CreateThread(NULL, 0, hearGride, NULL, 0, NULL);
    if (!start(program, commandLine, symbols)) return 1;

    std::string line;
    while (readLine(line)) {
        std::string cmd = trim(line);
        if (cmd.empty()) continue;
        std::string word = cmd.substr(0, cmd.find_first_of(" \t"));
        std::string rest = cmd.size() > word.size() ? trim(cmd.substr(word.size())) : std::string();
        if (cmd == "q" || cmd == "qq") quit();
        else if (word == ".printf") sayPrintf(rest);
        else if (word == ".echo") sayLine(rest);
        else if (cmd == ".lines -e" || cmd == "l+t" || cmd == "l+s" || cmd == "l-t" || cmd == "l-s" || word == "n" || word == ".symfix" || word == ".reload") { }
        else if (word == ".step_filter") {
            stepFilter.clear();
            size_t q1 = rest.find('"'), q2 = rest.rfind('"');
            std::string list = q1 != std::string::npos && q2 > q1 ? rest.substr(q1 + 1, q2 - q1 - 1) : rest;
            for (size_t from = 0; from <= list.size();) {
                size_t semi = list.find(';', from);
                std::string one = trim(list.substr(from, semi == std::string::npos ? std::string::npos : semi - from));
                if (!one.empty()) stepFilter.push_back(one);
                if (semi == std::string::npos) break;
                from = semi + 1;
            }
        }
        else if (exited && (cmd == "g" || cmd == "p" || cmd == "t" || cmd == "gu")) sayLine("No runnable debuggees");
        else if (word == "bp" || word == "bu" || word == "bm") setBreakpoint(rest);
        else if (word == "bc") clearBreakpoints();
        else if (word == "bl") { for (const Breakpoint& b : breakpoints) say(" %d e %s%s\n", b.id, b.spec.c_str(), b.once ? " (once)" : ""); }
        else if (cmd == "g") sayStop(runOn());
        else if (cmd == "p") sayStop(step(false));
        else if (cmd == "t") sayStop(step(true));
        else if (cmd == "gu") sayStop(stepOut());
        else if (cmd == ".lastevent") { say("Last event: %s\n", lastEvent.c_str()); }
        else if (cmd == "ln") sayWhere();
        else if (word == "k" || word == "kb" || word == "kn") { if (exited) sayLine("No runnable debuggees"); else sayStack(); }
        else if (word == ".frame") {
            readFrames();
            size_t n = static_cast<size_t>(strtoul(rest.c_str(), NULL, 16));
            if (exited || n >= frames.size()) sayLine("Invalid frame number");
            else { currentFrame = n; char b[8]; snprintf(b, sizeof b, "%02zx ", n); say("%s%s\n", b, frameLine(n).c_str()); }
        }
        else if (word == "dv") { if (exited) sayLine("No runnable debuggees"); else sayLocals(); }
        else if (word == "??") {
            if (exited) { sayLine("No runnable debuggees"); continue; }
            size_t eq = rest.find('=');
            bool isAssign = eq != std::string::npos && (eq + 1 >= rest.size() || rest[eq + 1] != '=') && (eq == 0 || (rest[eq - 1] != '!' && rest[eq - 1] != '<' && rest[eq - 1] != '>'));
            if (isAssign) sayLine(assign(trim(rest.substr(0, eq)), rest.substr(eq + 1)));
            else { Value v = evaluate(rest); sayLine(v.ok ? shown(v) : v.error); }
        }
        else if (cmd == "r") { if (exited) sayLine("No runnable debuggees"); else sayRegisters(); }
        else if (word == "db") sayMemory(rest);
        else say("                ^ Syntax error in '%s'\n", cmd.c_str());
    }
    quit();
    return 0;
}
