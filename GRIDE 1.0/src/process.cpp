#include "process.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace editor {

#ifdef _WIN32

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

typedef HRESULT (WINAPI *MakeConsole)(COORD, HANDLE, HANDLE, DWORD, void**);
typedef void (WINAPI *DropConsole)(void*);

MakeConsole makeConsole() {
    HMODULE kernel = GetModuleHandleA("kernel32.dll");
    return kernel ? (MakeConsole)GetProcAddress(kernel, "CreatePseudoConsole") : 0;
}

DropConsole dropConsole() {
    HMODULE kernel = GetModuleHandleA("kernel32.dll");
    return kernel ? (DropConsole)GetProcAddress(kernel, "ClosePseudoConsole") : 0;
}

struct Process::Held {
    HANDLE toChild;
    HANDLE fromChild;
    HANDLE child;
    void* console;
    // startCaptured and startInteractive only: the error stream, the job, what ended.
    HANDLE errFromChild;
    HANDLE job;
    bool grouped;
    bool outOpen, errOpen;
    std::atomic<bool> reaped;
    int status;
    // An interactive child on a pseudo-console: its input half a line, and where the reading of
    // the console's escape sequences has got to (vtState, the parameters so far, the row, the column).
    bool midLine;
    int vtState;
    std::string vtParams;
    int row;
    int column;

    Held() : toChild(NULL), fromChild(NULL), child(NULL), console(NULL), errFromChild(NULL),
             job(NULL), grouped(false), outOpen(false), errOpen(false), reaped(false), status(0),
             midLine(false), vtState(0), row(1), column(1) {}
};

#else

struct Process::Held {
    int toChild;
    int fromChild;
    pid_t child;
    // startCaptured and startInteractive only: the error stream, what ended, and - the input
    // being a terminal - the character that ends a line of it and whether one is half written.
    int errFromChild;
    bool grouped;
    bool outOpen, errOpen;
    std::atomic<bool> reaped;
    int status;
    bool terminal;
    char endOfInput;
    bool midLine;

    Held() : toChild(-1), fromChild(-1), child(-1), errFromChild(-1), grouped(false),
             outOpen(false), errOpen(false), reaped(false), status(0), terminal(false),
             endOfInput(4), midLine(false) {}
};

#endif

Process::Process() : held_(new Held()), running_(false), killed_(false) {}

Process::~Process() {
    if (held_->grouped) {
        if (!held_->reaped) kill();
        finish();
    } else {
        stop();
    }
    delete held_;
}

#ifdef _WIN32

bool Process::start(const std::string& command) {
    if (running_) return false;

    SECURITY_ATTRIBUTES inherit;
    inherit.nLength = sizeof inherit;
    inherit.lpSecurityDescriptor = NULL;
    inherit.bInheritHandle = TRUE;

    HANDLE childReads = NULL, weWrite = NULL, weRead = NULL, childWrites = NULL;
    if (!CreatePipe(&childReads, &weWrite, &inherit, 0)) return false;
    if (!CreatePipe(&weRead, &childWrites, &inherit, 0)) {
        CloseHandle(childReads);
        CloseHandle(weWrite);
        return false;
    }
    SetHandleInformation(weWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(weRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startup;
    std::memset(&startup, 0, sizeof startup);
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = childReads;
    startup.hStdOutput = childWrites;
    startup.hStdError = childWrites;

    std::string line = "cmd /c \"" + command + "\"";
    std::vector<char> writable(line.begin(), line.end());
    writable.push_back('\0');

    PROCESS_INFORMATION made;
    std::memset(&made, 0, sizeof made);
    BOOL ok = CreateProcessA(NULL, &writable[0], NULL, NULL, TRUE, CREATE_NO_WINDOW,
                             NULL, NULL, &startup, &made);

    CloseHandle(childReads);
    CloseHandle(childWrites);
    if (!ok) {
        CloseHandle(weWrite);
        CloseHandle(weRead);
        return false;
    }
    CloseHandle(made.hThread);

    held_->toChild = weWrite;
    held_->fromChild = weRead;
    held_->child = made.hProcess;
    running_ = true;
    return true;
}

bool Process::startOnConsole(const std::string& command) {
    if (running_) return false;

    MakeConsole make = makeConsole();
    if (!make) return false;

    HANDLE consoleReads = NULL, weWrite = NULL, weRead = NULL, consoleWrites = NULL;
    if (!CreatePipe(&consoleReads, &weWrite, NULL, 0)) return false;
    if (!CreatePipe(&weRead, &consoleWrites, NULL, 0)) {
        CloseHandle(consoleReads);
        CloseHandle(weWrite);
        return false;
    }

    COORD size;
    size.X = 500;
    size.Y = 50;
    void* console = NULL;
    if (FAILED(make(size, consoleReads, consoleWrites, 0, &console))) {
        CloseHandle(consoleReads);
        CloseHandle(consoleWrites);
        CloseHandle(weWrite);
        CloseHandle(weRead);
        return false;
    }

    STARTUPINFOEXA startup;
    std::memset(&startup, 0, sizeof startup);
    startup.StartupInfo.cb = sizeof startup;

    SIZE_T room = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &room);
    std::vector<char> attributes(room);
    startup.lpAttributeList =
        reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(room ? &attributes[0] : 0);
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &room) ||
        !UpdateProcThreadAttribute(startup.lpAttributeList, 0,
                                   PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   console, sizeof console, NULL, NULL)) {
        DropConsole drop = dropConsole();
        if (drop) drop(console);
        CloseHandle(consoleReads);
        CloseHandle(consoleWrites);
        CloseHandle(weWrite);
        CloseHandle(weRead);
        return false;
    }

    std::vector<char> writable(command.begin(), command.end());
    writable.push_back('\0');

    HANDLE keepIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE keepOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE keepErr = GetStdHandle(STD_ERROR_HANDLE);
    SetStdHandle(STD_INPUT_HANDLE, NULL);
    SetStdHandle(STD_OUTPUT_HANDLE, NULL);
    SetStdHandle(STD_ERROR_HANDLE, NULL);

    PROCESS_INFORMATION made;
    std::memset(&made, 0, sizeof made);

    BOOL ok = CreateProcessA(NULL, &writable[0], NULL, NULL, FALSE,
                             EXTENDED_STARTUPINFO_PRESENT,
                             NULL, NULL, &startup.StartupInfo, &made);

    SetStdHandle(STD_INPUT_HANDLE, keepIn);
    SetStdHandle(STD_OUTPUT_HANDLE, keepOut);
    SetStdHandle(STD_ERROR_HANDLE, keepErr);

    DeleteProcThreadAttributeList(startup.lpAttributeList);
    CloseHandle(consoleReads);
    CloseHandle(consoleWrites);

    if (!ok) {
        DropConsole drop = dropConsole();
        if (drop) drop(console);
        CloseHandle(weWrite);
        CloseHandle(weRead);
        return false;
    }
    CloseHandle(made.hThread);

    held_->toChild = weWrite;
    held_->fromChild = weRead;
    held_->child = made.hProcess;
    held_->console = console;
    running_ = true;
    return true;
}

bool Process::say(const std::string& line) {
    if (!running_) return false;

    std::string out = line + (held_->console ? "\r\n" : "\n");
    DWORD written = 0;
    if (!WriteFile(held_->toChild, out.data(), static_cast<DWORD>(out.size()), &written, NULL))
        return false;
    return written == out.size();
}

void Process::stop() {
    if (!running_) return;
    running_ = false;

    if (held_->toChild) { CloseHandle(held_->toChild); held_->toChild = NULL; }

    if (WaitForSingleObject(held_->child, 2000) == WAIT_TIMEOUT)
        TerminateProcess(held_->child, 1);

    if (held_->console) {
        DropConsole drop = dropConsole();
        if (drop) drop(held_->console);
        held_->console = NULL;
    }

    if (held_->fromChild) { CloseHandle(held_->fromChild); held_->fromChild = NULL; }
    CloseHandle(held_->child);
    held_->child = NULL;
}

namespace {

// A child in a job of its own, killed with everything it started when the job is ended, and handed
// only the handles named - so a pipe made on another thread for another child is not inherited
// and cannot hold this one's output open. Suspended until it is in the job, so nothing escapes it.
bool launchInJob(const std::string& line, HANDLE input, HANDLE output, HANDLE errors,
                 HANDLE& child, HANDLE& job) {
    job = CreateJobObjectA(NULL, NULL);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
        std::memset(&limits, 0, sizeof limits);
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
    }

    STARTUPINFOEXA startup;
    std::memset(&startup, 0, sizeof startup);
    startup.StartupInfo.cb = sizeof startup;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input;
    startup.StartupInfo.hStdOutput = output;
    startup.StartupInfo.hStdError = errors;

    HANDLE handed[3];
    DWORD count = 0;
    handed[count++] = input;
    handed[count++] = output;
    if (errors != output) handed[count++] = errors;

    SIZE_T room = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &room);
    std::vector<char> attributes(room ? room : 1);
    LPPROC_THREAD_ATTRIBUTE_LIST list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(&attributes[0]);
    bool listed = room != 0 && InitializeProcThreadAttributeList(list, 1, 0, &room) &&
                  UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handed,
                                            count * sizeof(HANDLE), NULL, NULL);
    DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED;
    if (listed) {
        startup.lpAttributeList = list;
        flags |= EXTENDED_STARTUPINFO_PRESENT;
    }

    std::vector<char> writable(line.begin(), line.end());
    writable.push_back('\0');
    PROCESS_INFORMATION made;
    std::memset(&made, 0, sizeof made);
    BOOL ok = CreateProcessA(NULL, &writable[0], NULL, NULL, TRUE, flags, NULL, NULL,
                             &startup.StartupInfo, &made);
    if (listed) DeleteProcThreadAttributeList(list);
    if (!ok) {
        if (job) CloseHandle(job);
        job = NULL;
        return false;
    }
    if (job && !AssignProcessToJobObject(job, made.hProcess)) {
        CloseHandle(job);
        job = NULL;
    }
    ResumeThread(made.hThread);
    CloseHandle(made.hThread);
    child = made.hProcess;
    return true;
}

bool inheritablePipe(HANDLE& reads, HANDLE& writes, bool childReads) {
    SECURITY_ATTRIBUTES inherit;
    inherit.nLength = sizeof inherit;
    inherit.lpSecurityDescriptor = NULL;
    inherit.bInheritHandle = TRUE;
    if (!CreatePipe(&reads, &writes, &inherit, 0)) return false;
    SetHandleInformation(childReads ? writes : reads, HANDLE_FLAG_INHERIT, 0);
    return true;
}

}

// Through cmd, as popen ran it: a build's command is a shell line. The input is NUL - nothing
// run from here may read the editor's own - and both streams go to one pipe, as 2>&1 put them.
bool Process::startCaptured(const std::string& command) {
    if (running_) return false;

    SECURITY_ATTRIBUTES inherit;
    inherit.nLength = sizeof inherit;
    inherit.lpSecurityDescriptor = NULL;
    inherit.bInheritHandle = TRUE;
    HANDLE nothing = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 &inherit, OPEN_EXISTING, 0, NULL);
    if (nothing == INVALID_HANDLE_VALUE) return false;
    HANDLE weRead = NULL, childWrites = NULL;
    if (!inheritablePipe(weRead, childWrites, false)) { CloseHandle(nothing); return false; }

    HANDLE child = NULL, job = NULL;
    bool ok = launchInJob("cmd /c \"" + command + "\"", nothing, childWrites, childWrites,
                          child, job);
    CloseHandle(nothing);
    CloseHandle(childWrites);
    if (!ok) { CloseHandle(weRead); return false; }

    held_->fromChild = weRead;
    held_->child = child;
    held_->job = job;
    held_->grouped = true;
    held_->outOpen = true;
    held_->errOpen = false;
    held_->reaped = false;
    killed_ = false;
    running_ = true;
    return true;
}

// Not through cmd: the line is the program and its quoted arguments, and cmd would read a '%' or
// a '&' in a path. On a pseudo-console where there is one (Windows 10 1809 on), so the C library
// flushes a prompt before it reads; on pipes before that, where a prompt shows late. README.md.
bool Process::startInteractive(const std::string& command) {
    if (running_) return false;
    if (startOnPseudoConsole(command)) return true;

    HANDLE childReads = NULL, weWrite = NULL, weRead = NULL, childWrites = NULL;
    HANDLE errRead = NULL, errWrites = NULL;
    if (!inheritablePipe(childReads, weWrite, true)) return false;
    if (!inheritablePipe(weRead, childWrites, false)) {
        CloseHandle(childReads); CloseHandle(weWrite);
        return false;
    }
    if (!inheritablePipe(errRead, errWrites, false)) {
        CloseHandle(childReads); CloseHandle(weWrite);
        CloseHandle(weRead); CloseHandle(childWrites);
        return false;
    }

    HANDLE child = NULL, job = NULL;
    bool ok = launchInJob(command, childReads, childWrites, errWrites, child, job);
    CloseHandle(childReads);
    CloseHandle(childWrites);
    CloseHandle(errWrites);
    if (!ok) {
        CloseHandle(weWrite); CloseHandle(weRead); CloseHandle(errRead);
        return false;
    }

    held_->toChild = weWrite;
    held_->fromChild = weRead;
    held_->errFromChild = errRead;
    held_->child = child;
    held_->job = job;
    held_->grouped = true;
    held_->outOpen = true;
    held_->errOpen = true;
    held_->reaped = false;
    held_->midLine = false;
    killed_ = false;
    running_ = true;
    return true;
}

// The pseudo-console's half: input and output - both streams, a console has one - through a
// console the system keeps for the program, which is in a job as the pipes' child is. Wide, so a
// long line is not wrapped; the console's escape sequences are taken out as it is read.
bool Process::startOnPseudoConsole(const std::string& command) {
    MakeConsole make = makeConsole();
    if (!make) return false;

    HANDLE consoleReads = NULL, weWrite = NULL, weRead = NULL, consoleWrites = NULL;
    if (!CreatePipe(&consoleReads, &weWrite, NULL, 0)) return false;
    if (!CreatePipe(&weRead, &consoleWrites, NULL, 0)) {
        CloseHandle(consoleReads);
        CloseHandle(weWrite);
        return false;
    }
    COORD size;
    size.X = 2000;
    size.Y = 30;
    void* console = NULL;
    HRESULT made = make(size, consoleReads, consoleWrites, 0, &console);
    CloseHandle(consoleReads);
    CloseHandle(consoleWrites);
    if (FAILED(made)) {
        CloseHandle(weWrite);
        CloseHandle(weRead);
        return false;
    }

    HANDLE job = CreateJobObjectA(NULL, NULL);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
        std::memset(&limits, 0, sizeof limits);
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
    }

    STARTUPINFOEXA startup;
    std::memset(&startup, 0, sizeof startup);
    startup.StartupInfo.cb = sizeof startup;
    SIZE_T room = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &room);
    std::vector<char> attributes(room ? room : 1);
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(&attributes[0]);
    bool listed = room != 0 &&
                  InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &room) &&
                  UpdateProcThreadAttribute(startup.lpAttributeList, 0,
                                            PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, console,
                                            sizeof console, NULL, NULL);
    PROCESS_INFORMATION child;
    std::memset(&child, 0, sizeof child);
    BOOL ok = FALSE;
    if (listed) {
        std::vector<char> writable(command.begin(), command.end());
        writable.push_back('\0');
        // As startOnConsole does: the window's own standard handles are not the child's.
        HANDLE keepIn = GetStdHandle(STD_INPUT_HANDLE);
        HANDLE keepOut = GetStdHandle(STD_OUTPUT_HANDLE);
        HANDLE keepErr = GetStdHandle(STD_ERROR_HANDLE);
        SetStdHandle(STD_INPUT_HANDLE, NULL);
        SetStdHandle(STD_OUTPUT_HANDLE, NULL);
        SetStdHandle(STD_ERROR_HANDLE, NULL);
        ok = CreateProcessA(NULL, &writable[0], NULL, NULL, FALSE,
                            EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED, NULL, NULL,
                            &startup.StartupInfo, &child);
        SetStdHandle(STD_INPUT_HANDLE, keepIn);
        SetStdHandle(STD_OUTPUT_HANDLE, keepOut);
        SetStdHandle(STD_ERROR_HANDLE, keepErr);
    }
    if (listed) DeleteProcThreadAttributeList(startup.lpAttributeList);
    if (!ok) {
        DropConsole drop = dropConsole();
        if (drop) drop(console);
        if (job) CloseHandle(job);
        CloseHandle(weWrite);
        CloseHandle(weRead);
        return false;
    }
    if (job && !AssignProcessToJobObject(job, child.hProcess)) {
        CloseHandle(job);
        job = NULL;
    }
    ResumeThread(child.hThread);
    CloseHandle(child.hThread);

    held_->toChild = weWrite;
    held_->fromChild = weRead;
    held_->errFromChild = NULL;
    held_->child = child.hProcess;
    held_->job = job;
    held_->console = console;
    held_->grouped = true;
    held_->outOpen = true;
    held_->errOpen = false;
    held_->reaped = false;
    held_->midLine = false;
    held_->vtState = 0;
    held_->vtParams.clear();
    held_->row = 1;
    held_->column = 1;
    killed_ = false;
    running_ = true;
    return true;
}

namespace {

// What a pseudo-console writes, as text: its escape sequences out - CSI, OSC such as the title,
// the short ones - "\r" dropped, the cursor sent to a lower row as that many newlines, and sent
// right as that many spaces: the console writes a trailing space as a move. One state across reads.
std::string plainText(const char* bytes, size_t size, int& state, std::string& params, int& row,
                      int& column) {
    std::string out;
    for (size_t i = 0; i < size; ++i) {
        unsigned char c = static_cast<unsigned char>(bytes[i]);
        switch (state) {
        case 0:
            if (c == 0x1b) { state = 1; continue; }
            if (c == '\r' || c == 0x07) continue;
            if (c == '\n') { ++row; column = 1; }
            else if ((c & 0xc0) != 0x80) ++column;   // a UTF-8 continuation byte is no column
            out += static_cast<char>(c);
            continue;
        case 1:  // after ESC
            if (c == '[') { state = 2; params.clear(); }
            else if (c == ']') state = 3;
            else if (c == '(' || c == ')' || c == '#' || c == '%') state = 5;
            else state = 0;
            continue;
        case 2:  // CSI: parameters, then a final byte
            if (c >= 0x40 && c <= 0x7e) {
                if (c == 'H' || c == 'f') {
                    int wanted = std::atoi(params.c_str());
                    if (wanted < 1) wanted = 1;
                    size_t semi = params.find(';');
                    int col = semi == std::string::npos ? 1 : std::atoi(params.c_str() + semi + 1);
                    if (col < 1) col = 1;
                    if (row < wanted) column = 1;
                    for (; row < wanted; ++row) out += '\n';
                    for (; row == wanted && column < col; ++column) out += ' ';
                } else if (c == 'C') {
                    int n = std::atoi(params.c_str());
                    for (n = n < 1 ? 1 : n; n > 0; --n, ++column) out += ' ';
                } else if (c == 'G') {
                    int col = std::atoi(params.c_str());
                    for (; column < col; ++column) out += ' ';
                }
                state = 0;
            } else {
                params += static_cast<char>(c);
            }
            continue;
        case 3:  // OSC: to BEL, or to ESC backslash
            if (c == 0x07) state = 0;
            else if (c == 0x1b) state = 4;
            continue;
        case 4:
            state = c == '\\' ? 0 : 3;
            continue;
        default:  // the one character a charset designation takes
            state = 0;
            continue;
        }
    }
    return out;
}

}

namespace {

bool writeAll(HANDLE to, const char* bytes, size_t size) {
    size_t written = 0;
    while (written < size) {
        DWORD went = 0;
        if (!WriteFile(to, bytes + written, static_cast<DWORD>(size - written), &went, NULL) ||
            went == 0)
            return false;
        written += went;
    }
    return true;
}

}

// A console is typed at: Enter is "\r", and the console hands the program "\r\n" for it.
bool Process::send(const char* bytes, size_t size) {
    if (!running_ || !held_->toChild) return false;
    if (size > 0) held_->midLine = bytes[size - 1] != '\n';
    if (!held_->console) return writeAll(held_->toChild, bytes, size);
    std::string typed;
    for (size_t i = 0; i < size; ++i)
        if (bytes[i] != '\r') typed += bytes[i] == '\n' ? '\r' : bytes[i];
    return writeAll(held_->toChild, typed.data(), typed.size());
}

// A console's input has no end to close: Ctrl-Z at the start of a line is end of file to the C
// library, so a half line is finished first; the pipe is kept, for the console's sake.
void Process::closeInput() {
    if (!running_ || !held_->toChild) return;
    if (held_->console) {
        const char* end = held_->midLine ? "\r\x1a\r" : "\x1a\r";
        writeAll(held_->toChild, end, std::strlen(end));
        held_->midLine = false;
        return;
    }
    CloseHandle(held_->toChild);
    held_->toChild = NULL;
}

int Process::readAny(std::string& into, bool* isStderr, int timeoutMs) {
    if (isStderr) *isStderr = false;
    for (int waited = 0;; waited += 5) {
        HANDLE pipes[2] = {held_->outOpen ? held_->fromChild : NULL,
                           held_->errOpen ? held_->errFromChild : NULL};
        if (!pipes[0] && !pipes[1]) return -1;
        for (int i = 0; i < 2; ++i) {
            if (!pipes[i]) continue;
            DWORD ready = 0;
            if (!PeekNamedPipe(pipes[i], NULL, 0, NULL, &ready, NULL)) {
                if (i == 0) held_->outOpen = false;
                else held_->errOpen = false;
                continue;
            }
            if (ready == 0) continue;
            char chunk[4096];
            DWORD got = 0;
            DWORD want = ready < sizeof chunk ? ready : static_cast<DWORD>(sizeof chunk);
            if (!ReadFile(pipes[i], chunk, want, &got, NULL) || got == 0) {
                if (i == 0) held_->outOpen = false;
                else held_->errOpen = false;
                continue;
            }
            if (held_->console && i == 0)
                into += plainText(chunk, got, held_->vtState, held_->vtParams, held_->row, held_->column);
            else
                into.append(chunk, got);
            if (isStderr) *isStderr = i == 1;
            return 1;
        }
        if (!held_->outOpen && !held_->errOpen) return -1;
        if (waited >= timeoutMs) return 0;
        Sleep(5);
    }
}

bool Process::ended(int* status) {
    if (!held_->child) return true;
    if (!held_->reaped && WaitForSingleObject(held_->child, 0) == WAIT_OBJECT_0) {
        DWORD code = 0;
        GetExitCodeProcess(held_->child, &code);
        held_->status = static_cast<int>(code);
        held_->reaped = true;
    }
    if (held_->reaped && status) *status = held_->status;
    return held_->reaped;
}

int Process::finish() {
    if (held_->child) {
        WaitForSingleObject(held_->child, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(held_->child, &code);
        held_->status = static_cast<int>(code);
        held_->reaped = true;
        CloseHandle(held_->child);
        held_->child = NULL;
    }
    if (held_->job) { CloseHandle(held_->job); held_->job = NULL; }
    // The reading end before the console: closing a console waits for what it has written to be
    // read, and nothing here reads any more.
    if (held_->fromChild) { CloseHandle(held_->fromChild); held_->fromChild = NULL; }
    if (held_->console) {
        DropConsole drop = dropConsole();
        if (drop) drop(held_->console);
        held_->console = NULL;
    }
    if (held_->toChild) { CloseHandle(held_->toChild); held_->toChild = NULL; }
    if (held_->errFromChild) { CloseHandle(held_->errFromChild); held_->errFromChild = NULL; }
    held_->outOpen = held_->errOpen = false;
    running_ = false;
    return held_->status;
}

void Process::kill() {
    if (!held_->child || held_->reaped) return;
    killed_ = true;
    if (held_->job) TerminateJobObject(held_->job, 1);
    else TerminateProcess(held_->child, 1);
}

namespace {

bool waitingToBeRead(HANDLE from) {
    DWORD ready = 0;
    if (!PeekNamedPipe(from, NULL, 0, NULL, &ready, NULL)) return true;
    return ready > 0;
}

int readSome(void* from, char* into, size_t room, size_t& got, int timeoutMs) {
    HANDLE pipe = static_cast<HANDLE>(from);
    for (int waited = 0; waited < timeoutMs; waited += 10) {
        if (waitingToBeRead(pipe)) break;
        Sleep(10);
        if (waited + 10 >= timeoutMs) return 0;
    }

    DWORD read = 0;
    if (!ReadFile(pipe, into, static_cast<DWORD>(room), &read, NULL)) return -1;
    got = read;
    return read > 0 ? 1 : -1;
}

}

#else

bool Process::startOnConsole(const std::string&) { return false; }

bool Process::start(const std::string& command) {
    if (running_) return false;

    int toChild[2], fromChild[2];
    if (pipe(toChild) != 0) return false;
    if (pipe(fromChild) != 0) {
        close(toChild[0]);
        close(toChild[1]);
        return false;
    }

    pid_t child = fork();
    if (child < 0) {
        close(toChild[0]); close(toChild[1]);
        close(fromChild[0]); close(fromChild[1]);
        return false;
    }

    if (child == 0) {

        dup2(toChild[0], STDIN_FILENO);
        dup2(fromChild[1], STDOUT_FILENO);
        dup2(fromChild[1], STDERR_FILENO);
        close(toChild[0]); close(toChild[1]);
        close(fromChild[0]); close(fromChild[1]);

        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(0));
        _exit(127);
    }

    close(toChild[0]);
    close(fromChild[1]);

    held_->toChild = toChild[1];
    held_->fromChild = fromChild[0];
    held_->child = child;
    running_ = true;
    return true;
}

bool Process::say(const std::string& line) {
    if (!running_) return false;

    std::string out = line + "\n";
    size_t written = 0;
    while (written < out.size()) {
        ssize_t went = write(held_->toChild, out.data() + written, out.size() - written);
        if (went <= 0) {
            if (went < 0 && errno == EINTR) continue;
            return false;
        }
        written += static_cast<size_t>(went);
    }
    return true;
}

void Process::stop() {
    if (!running_) return;
    running_ = false;

    if (held_->toChild >= 0) { close(held_->toChild); held_->toChild = -1; }

    for (int waited = 0; waited < 200; ++waited) {
        int status = 0;
        pid_t done = waitpid(held_->child, &status, WNOHANG);
        if (done == held_->child || done < 0) break;
        usleep(10 * 1000);
        if (waited == 199) {
            ::kill(held_->child, SIGKILL);
            waitpid(held_->child, &status, 0);
        }
    }

    if (held_->fromChild >= 0) { close(held_->fromChild); held_->fromChild = -1; }
    held_->child = -1;
}

namespace {

int readSome(int from, char* into, size_t room, size_t& got, int timeoutMs) {
    for (;;) {
        struct pollfd waiting;
        waiting.fd = from;
        waiting.events = POLLIN;
        waiting.revents = 0;

        int ready = poll(&waiting, 1, timeoutMs);
        if (ready == 0) return 0;
        if (ready < 0) {
            if (errno == EINTR) continue;
            return -1;
        }

        ssize_t read = ::read(from, into, room);
        if (read > 0) { got = static_cast<size_t>(read); return 1; }
        if (read == 0) return -1;
        if (errno == EINTR) continue;
        return -1;
    }
}

}


namespace {

void closeOnExec(int fd) {
    int flags = fcntl(fd, F_GETFD);
    if (flags >= 0) fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

int statusOf(int raw) {
    if (WIFEXITED(raw)) return WEXITSTATUS(raw);
    if (WIFSIGNALED(raw)) return 128 + WTERMSIG(raw);
    return raw;
}

}

bool Process::startCaptured(const std::string& command) {
    if (running_) return false;

    int nothing = open("/dev/null", O_RDONLY);
    if (nothing < 0) return false;
    int fromChild[2];
    if (pipe(fromChild) != 0) { close(nothing); return false; }
    closeOnExec(fromChild[0]);

    pid_t child = fork();
    if (child < 0) {
        close(nothing); close(fromChild[0]); close(fromChild[1]);
        return false;
    }
    if (child == 0) {
        setpgid(0, 0);
        dup2(nothing, STDIN_FILENO);
        dup2(fromChild[1], STDOUT_FILENO);
        dup2(fromChild[1], STDERR_FILENO);
        close(nothing); close(fromChild[0]); close(fromChild[1]);
        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(0));
        _exit(127);
    }
    setpgid(child, child);
    close(nothing);
    close(fromChild[1]);

    held_->fromChild = fromChild[0];
    held_->child = child;
    held_->grouped = true;
    held_->outOpen = true;
    held_->errOpen = false;
    held_->reaped = false;
    killed_ = false;
    running_ = true;
    return true;
}

// The program's input and output are a terminal - a pseudo-terminal this end holds - so its C
// library flushes a prompt before it reads, as at a shell. The terminal echoes what it is sent,
// as a Windows console does, so the window does not; its errors come apart, on a pipe.
bool Process::startInteractive(const std::string& command) {
    if (running_) return false;

    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) return false;
    if (grantpt(master) != 0 || unlockpt(master) != 0) { close(master); return false; }
    const char* name = ptsname(master);
    if (name == 0) { close(master); return false; }
    int slave = open(name, O_RDWR | O_NOCTTY);
    if (slave < 0) { close(master); return false; }

    struct termios settings;
    char endOfInput = 4;
    if (tcgetattr(slave, &settings) == 0) {
        settings.c_lflag |= ECHO;
        settings.c_lflag &= ~static_cast<tcflag_t>(ECHOCTL);
        settings.c_oflag &= ~static_cast<tcflag_t>(OPOST);
        tcsetattr(slave, TCSANOW, &settings);
        if (settings.c_cc[VEOF] != 0) endOfInput = static_cast<char>(settings.c_cc[VEOF]);
    }

    int errors[2];
    if (pipe(errors) != 0) { close(master); close(slave); return false; }
    closeOnExec(master);
    closeOnExec(errors[0]);

    pid_t child = fork();
    if (child < 0) {
        close(master); close(slave); close(errors[0]); close(errors[1]);
        return false;
    }
    if (child == 0) {
        setsid();
#ifdef TIOCSCTTY
        ioctl(slave, TIOCSCTTY, 0);
#endif
        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(errors[1], STDERR_FILENO);
        close(slave); close(errors[0]); close(errors[1]);
        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(0));
        _exit(127);
    }
    close(slave);
    close(errors[1]);

    held_->toChild = master;
    held_->fromChild = master;
    held_->errFromChild = errors[0];
    held_->child = child;
    held_->grouped = true;
    held_->outOpen = true;
    held_->errOpen = true;
    held_->reaped = false;
    held_->terminal = true;
    held_->endOfInput = endOfInput;
    held_->midLine = false;
    killed_ = false;
    running_ = true;
    return true;
}

bool Process::send(const char* bytes, size_t size) {
    if (!running_ || held_->toChild < 0) return false;
    size_t written = 0;
    while (written < size) {
        ssize_t went = write(held_->toChild, bytes + written, size - written);
        if (went <= 0) {
            if (went < 0 && errno == EINTR) continue;
            return false;
        }
        written += static_cast<size_t>(went);
    }
    if (size > 0) held_->midLine = bytes[size - 1] != '\n';
    return true;
}

// A terminal's input has no end to close: the end-of-file character says it, once at the start of
// a line and twice after half of one - the first hands the half line over, the second ends it.
void Process::closeInput() {
    if (!running_ || held_->toChild < 0) return;
    if (held_->terminal) {
        char end[2] = {held_->endOfInput, held_->endOfInput};
        ssize_t went = write(held_->toChild, end, held_->midLine ? 2 : 1);
        (void)went;
        held_->midLine = false;
        held_->toChild = -1;
        return;
    }
    close(held_->toChild);
    held_->toChild = -1;
}

int Process::readAny(std::string& into, bool* isStderr, int timeoutMs) {
    if (isStderr) *isStderr = false;
    for (;;) {
        struct pollfd waiting[2];
        int streams[2];
        int count = 0;
        if (held_->outOpen) { waiting[count].fd = held_->fromChild; streams[count++] = 0; }
        if (held_->errOpen) { waiting[count].fd = held_->errFromChild; streams[count++] = 1; }
        if (count == 0) return -1;
        for (int i = 0; i < count; ++i) { waiting[i].events = POLLIN; waiting[i].revents = 0; }

        int ready = poll(waiting, static_cast<nfds_t>(count), timeoutMs);
        if (ready == 0) return 0;
        if (ready < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        for (int i = 0; i < count; ++i) {
            if (waiting[i].revents == 0) continue;
            char chunk[4096];
            ssize_t got = ::read(waiting[i].fd, chunk, sizeof chunk);
            if (got > 0) {
                into.append(chunk, static_cast<size_t>(got));
                if (isStderr) *isStderr = streams[i] == 1;
                return 1;
            }
            if (got < 0 && errno == EINTR) return 0;
            // Nothing, or EIO: a terminal's reading end says that once every writer has gone.
            if (streams[i] == 0) held_->outOpen = false;
            else held_->errOpen = false;
        }
    }
}

bool Process::ended(int* status) {
    if (held_->child <= 0) return true;
    if (!held_->reaped) {
        int raw = 0;
        pid_t done = waitpid(held_->child, &raw, WNOHANG);
        if (done == held_->child) {
            held_->reaped = true;
            held_->status = statusOf(raw);
        } else if (done < 0 && errno != EINTR) {
            held_->reaped = true;
        }
    }
    if (held_->reaped && status) *status = held_->status;
    return held_->reaped;
}

int Process::finish() {
    if (held_->child > 0 && !held_->reaped) {
        int raw = 0;
        pid_t done;
        do done = waitpid(held_->child, &raw, 0); while (done < 0 && errno == EINTR);
        held_->reaped = true;
        held_->status = done == held_->child ? statusOf(raw) : -1;
    }
    if (held_->fromChild >= 0) close(held_->fromChild);
    if (held_->errFromChild >= 0) close(held_->errFromChild);
    if (held_->toChild >= 0 && held_->toChild != held_->fromChild) close(held_->toChild);
    held_->fromChild = held_->errFromChild = held_->toChild = -1;
    held_->outOpen = held_->errOpen = false;
    running_ = false;
    return held_->status;
}

void Process::kill() {
    pid_t child = held_->child;
    if (child <= 0 || held_->reaped) return;
    killed_ = true;
    ::kill(-child, SIGKILL);
    ::kill(child, SIGKILL);
}

#endif

std::string Process::readUntil(const std::string& marker, bool* found, int timeoutMs) {
    if (found) *found = false;
    if (marker.empty() || timeoutMs <= 0) return std::string();

    for (;;) {
        size_t at = pending_.find(marker);
        if (at != std::string::npos) {
            std::string answer = pending_.substr(0, at);
            pending_.erase(0, at + marker.size());
            if (found) *found = true;
            return answer;
        }
        if (!running_) break;

        char chunk[1024];
        size_t got = 0;
        int said = readSome(held_->fromChild, chunk, sizeof chunk, got, timeoutMs);
        if (said < 0) {
            running_ = false;
            break;
        }
        if (said == 0) break;
        pending_.append(chunk, got);
    }

    std::string rest = pending_;
    pending_.clear();
    return rest;
}

}
