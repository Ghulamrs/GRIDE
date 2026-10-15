// =============================================================================
//  NativeMethods.cpp - Win32 helpers (compiled WITHOUT the precompiled header)
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
// richedit.h is deliberately NOT included: with WIN32_LEAN_AND_MEAN its newer
// sections reference COM types (IStream ...) that ole2.h would normally declare.
// Only these message ids are needed.
#ifndef EM_GETSCROLLPOS
#define EM_GETSCROLLPOS (WM_USER + 221)
#endif
#ifndef EM_SETSCROLLPOS
#define EM_SETSCROLLPOS (WM_USER + 222)
#endif
#ifndef EM_SETUNDOLIMIT
#define EM_SETUNDOLIMIT (WM_USER + 82)
#endif

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif
#include <vector>
#include <vcclr.h>

#using <System.dll>
#using <System.Drawing.dll>

#include "NativeMethods.h"

// The pseudo-console half is native code (no metadata for Win32's opaque types): the calls go
// through GetProcAddress, as RIDE reaches them, since a Windows before 10 1809 has none.
#pragma managed(push, off)
namespace {
    typedef HRESULT (WINAPI *MakeConsoleFn)(COORD, HANDLE, HANDLE, DWORD, void**);
    typedef void (WINAPI *DropConsoleFn)(void*);

    MakeConsoleFn MakeConsole()
    {
        HMODULE kernel = ::GetModuleHandleW(L"kernel32.dll");
        return kernel ? (MakeConsoleFn)::GetProcAddress(kernel, "CreatePseudoConsole") : NULL;
    }
    DropConsoleFn DropConsole()
    {
        HMODULE kernel = ::GetModuleHandleW(L"kernel32.dll");
        return kernel ? (DropConsoleFn)::GetProcAddress(kernel, "ClosePseudoConsole") : NULL;
    }

    // RIDE's startOnPseudoConsole: the console over two pipes, the child suspended on it with the window's
    // own standard handles kept from it; 0 on success, else a code the caller turns into words
    int StartChildOnConsole(wchar_t* commandLine, const wchar_t* workDir, wchar_t* environment, int columns, int rows,
                            HANDLE& process, HANDLE& thread, void*& console, HANDLE& input, HANDLE& output, DWORD& error)
    {
        process = NULL; thread = NULL; console = NULL; input = NULL; output = NULL; error = 0;
        MakeConsoleFn make = MakeConsole();
        DropConsoleFn drop = DropConsole();
        if (!make || !drop) return 1;

        HANDLE consoleReads = NULL, weWrite = NULL, weRead = NULL, consoleWrites = NULL;
        if (!::CreatePipe(&consoleReads, &weWrite, NULL, 0)) return 2;
        if (!::CreatePipe(&weRead, &consoleWrites, NULL, 0))
        {
            ::CloseHandle(consoleReads); ::CloseHandle(weWrite);
            return 2;
        }
        COORD size;
        size.X = (SHORT)(columns > 0 ? columns : 500);
        size.Y = (SHORT)(rows > 0 ? rows : 50);
        void* made = NULL;
        HRESULT hr = make(size, consoleReads, consoleWrites, 0, &made);
        ::CloseHandle(consoleReads);
        ::CloseHandle(consoleWrites);
        if (FAILED(hr))
        {
            ::CloseHandle(weWrite); ::CloseHandle(weRead);
            error = (DWORD)hr;
            return 3;
        }

        SIZE_T room = 0;
        ::InitializeProcThreadAttributeList(NULL, 1, 0, &room);
        std::vector<char> attributes(room ? room : 1);
        LPPROC_THREAD_ATTRIBUTE_LIST list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(&attributes[0]);
        bool listed = room != 0 && ::InitializeProcThreadAttributeList(list, 1, 0, &room) &&
                      ::UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, made, sizeof made, NULL, NULL);
        STARTUPINFOEXW startup;
        ::ZeroMemory(&startup, sizeof startup);
        startup.StartupInfo.cb = sizeof startup;
        startup.lpAttributeList = list;
        PROCESS_INFORMATION child;
        ::ZeroMemory(&child, sizeof child);
        BOOL ok = FALSE;
        if (listed)
        {
            HANDLE keepIn = ::GetStdHandle(STD_INPUT_HANDLE), keepOut = ::GetStdHandle(STD_OUTPUT_HANDLE), keepErr = ::GetStdHandle(STD_ERROR_HANDLE);
            ::SetStdHandle(STD_INPUT_HANDLE, NULL);
            ::SetStdHandle(STD_OUTPUT_HANDLE, NULL);
            ::SetStdHandle(STD_ERROR_HANDLE, NULL);
            ok = ::CreateProcessW(NULL, commandLine, NULL, NULL, FALSE,
                                  EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                                  environment, workDir, &startup.StartupInfo, &child);
            error = ::GetLastError();
            ::SetStdHandle(STD_INPUT_HANDLE, keepIn);
            ::SetStdHandle(STD_OUTPUT_HANDLE, keepOut);
            ::SetStdHandle(STD_ERROR_HANDLE, keepErr);
            ::DeleteProcThreadAttributeList(list);
        }
        if (!ok)
        {
            drop(made);
            ::CloseHandle(weWrite); ::CloseHandle(weRead);
            return listed ? 4 : 5;
        }
        process = child.hProcess;
        thread = child.hThread;
        console = made;
        input = weWrite;
        output = weRead;
        return 0;
    }

    void DropChildConsole(void* console)
    {
        DropConsoleFn drop = DropConsole();
        if (console && drop) drop(console);
    }
}
#pragma managed(pop)

namespace {
    void AppendWide(std::vector<wchar_t>& into, System::String^ s)
    {
        pin_ptr<const wchar_t> chars = PtrToStringChars(s);
        for (int i = 0; i < s->Length; i++) into.push_back(chars[i]);
        into.push_back(0);
    }
}

namespace Gride { namespace Native {

    void NativeMethods::SetRedraw(System::IntPtr hwnd, bool on)
    {
        ::SendMessageW((HWND)hwnd.ToPointer(), WM_SETREDRAW, on ? TRUE : FALSE, 0);
        if (on) ::InvalidateRect((HWND)hwnd.ToPointer(), NULL, TRUE);
    }

    System::IntPtr NativeMethods::FocusedHandle()
    {
        return System::IntPtr((void*)::GetFocus());
    }

    int NativeMethods::FirstVisibleLine(System::IntPtr hwnd)
    {
        return (int)::SendMessageW((HWND)hwnd.ToPointer(), EM_GETFIRSTVISIBLELINE, 0, 0);
    }

    System::Drawing::Point NativeMethods::GetScrollPos(System::IntPtr hwnd)
    {
        POINT pt = { 0, 0 };
        ::SendMessageW((HWND)hwnd.ToPointer(), EM_GETSCROLLPOS, 0, (LPARAM)&pt);
        return System::Drawing::Point(pt.x, pt.y);
    }

    void NativeMethods::SetScrollPos(System::IntPtr hwnd, System::Drawing::Point p)
    {
        POINT pt = { p.X, p.Y };
        ::SendMessageW((HWND)hwnd.ToPointer(), EM_SETSCROLLPOS, 0, (LPARAM)&pt);
    }

    System::Drawing::Point NativeMethods::PosFromChar(System::IntPtr hwnd, int index)
    {
        POINTL pt = { 0, 0 };
        ::SendMessageW((HWND)hwnd.ToPointer(), EM_POSFROMCHAR, (WPARAM)&pt, (LPARAM)index);
        return System::Drawing::Point(pt.x, pt.y);
    }

    void NativeMethods::LineScroll(System::IntPtr hwnd, int lines)
    {
        if (lines != 0) ::SendMessageW((HWND)hwnd.ToPointer(), EM_LINESCROLL, 0, (LPARAM)lines);
    }

    void NativeMethods::SetUndoLimit(System::IntPtr hwnd, int limit)
    {
        ::SendMessageW((HWND)hwnd.ToPointer(), EM_SETUNDOLIMIT, (WPARAM)limit, 0);
    }

    System::Drawing::Rectangle NativeMethods::GetUpdateRect(System::IntPtr hwnd)
    {
        RECT r = { 0, 0, 0, 0 };
        if (!::GetUpdateRect((HWND)hwnd.ToPointer(), &r, FALSE)) return System::Drawing::Rectangle::Empty;
        return System::Drawing::Rectangle::FromLTRB(r.left, r.top, r.right, r.bottom);
    }

    void NativeMethods::DestroyIcon(System::IntPtr hicon)
    {
        if (hicon != System::IntPtr::Zero) ::DestroyIcon((HICON)hicon.ToPointer());
    }

    System::IntPtr NativeMethods::CreateJob()
    {
        HANDLE job = ::CreateJobObjectW(NULL, NULL);
        if (job == NULL) return System::IntPtr::Zero;
        // children may still leave the job on purpose (e.g. long-lived helper servers)
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
        ::ZeroMemory(&info, sizeof(info));
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_BREAKAWAY_OK;
        ::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
        return System::IntPtr(job);
    }

    bool NativeMethods::AssignToJob(System::IntPtr job, System::IntPtr process)
    {
        if (job == System::IntPtr::Zero || process == System::IntPtr::Zero) return false;
        return ::AssignProcessToJobObject((HANDLE)job.ToPointer(), (HANDLE)process.ToPointer()) != FALSE;
    }

    void NativeMethods::TerminateJob(System::IntPtr job)
    {
        if (job != System::IntPtr::Zero) ::TerminateJobObject((HANDLE)job.ToPointer(), 1);
    }

    void NativeMethods::CloseHandle(System::IntPtr handle)
    {
        if (handle != System::IntPtr::Zero) ::CloseHandle((HANDLE)handle.ToPointer());
    }

    bool NativeMethods::StartOnConsole(System::String^ commandLine, System::String^ workDir, array<System::String^>^ environment,
                                       int columns, int rows, System::IntPtr% process, System::IntPtr% console,
                                       System::IntPtr% input, System::IntPtr% output, System::IntPtr% job, System::String^% why)
    {
        process = System::IntPtr::Zero; console = System::IntPtr::Zero; input = System::IntPtr::Zero;
        output = System::IntPtr::Zero; job = System::IntPtr::Zero; why = System::String::Empty;
        std::vector<wchar_t> block;
        if (environment != nullptr) for each (System::String^ pair in environment) if (!System::String::IsNullOrEmpty(pair)) AppendWide(block, pair);
        block.push_back(0);
        std::vector<wchar_t> writable;
        AppendWide(writable, commandLine);
        std::vector<wchar_t> dir;
        if (!System::String::IsNullOrEmpty(workDir)) AppendWide(dir, workDir);

        HANDLE child = NULL, thread = NULL, in = NULL, out = NULL;
        void* made = NULL;
        DWORD error = 0;
        int failed = StartChildOnConsole(&writable[0], dir.empty() ? NULL : &dir[0], &block[0], columns, rows, child, thread, made, in, out, error);
        if (failed != 0)
        {
            switch (failed)
            {
            case 1: why = "this Windows has no pseudo-console (Windows 10 1809 or later has one)"; break;
            case 2: why = "CreatePipe failed"; break;
            case 3: why = System::String::Format("CreatePseudoConsole failed (0x{0:X8})", (unsigned)error); break;
            case 4: why = System::String::Format("CreateProcess failed (error {0})", (int)error); break;
            default: why = "the pseudo-console attribute could not be set"; break;
            }
            return false;
        }
        System::IntPtr j = CreateJob();
        if (j != System::IntPtr::Zero && !AssignToJob(j, System::IntPtr(child))) { ::CloseHandle((HANDLE)j.ToPointer()); j = System::IntPtr::Zero; }
        ::ResumeThread(thread);
        ::CloseHandle(thread);
        process = System::IntPtr(child);
        console = System::IntPtr(made);
        input = System::IntPtr(in);
        output = System::IntPtr(out);
        job = j;
        return true;
    }

    int NativeMethods::ReadFrom(System::IntPtr pipe, array<System::Byte>^ buffer)
    {
        if (pipe == System::IntPtr::Zero || buffer == nullptr || buffer->Length == 0) return -1;
        pin_ptr<System::Byte> at = &buffer[0];
        DWORD got = 0;
        if (!::ReadFile((HANDLE)pipe.ToPointer(), at, (DWORD)buffer->Length, &got, NULL)) return -1;
        return (int)got;
    }

    bool NativeMethods::WriteTo(System::IntPtr pipe, array<System::Byte>^ bytes)
    {
        if (pipe == System::IntPtr::Zero || bytes == nullptr) return false;
        if (bytes->Length == 0) return true;
        pin_ptr<System::Byte> at = &bytes[0];
        DWORD written = 0;
        while (written < (DWORD)bytes->Length)
        {
            DWORD went = 0;
            if (!::WriteFile((HANDLE)pipe.ToPointer(), at + written, (DWORD)bytes->Length - written, &went, NULL) || went == 0) return false;
            written += went;
        }
        return true;
    }

    void NativeMethods::CloseConsole(System::IntPtr console)
    {
        if (console != System::IntPtr::Zero) DropChildConsole(console.ToPointer());
    }

    bool NativeMethods::WaitForProcess(System::IntPtr process, int ms, int% exitCode)
    {
        exitCode = 0;
        if (process == System::IntPtr::Zero) return true;
        if (::WaitForSingleObject((HANDLE)process.ToPointer(), ms < 0 ? INFINITE : (DWORD)ms) != WAIT_OBJECT_0) return false;
        DWORD code = 0;
        ::GetExitCodeProcess((HANDLE)process.ToPointer(), &code);
        exitCode = (int)code;
        return true;
    }

    void NativeMethods::KillProcess(System::IntPtr process)
    {
        if (process != System::IntPtr::Zero) ::TerminateProcess((HANDLE)process.ToPointer(), 1);
    }
}}
