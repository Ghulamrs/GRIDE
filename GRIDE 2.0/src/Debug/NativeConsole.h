#pragma once
// ============================================================================
//  GRIDE 2.0 - a child on a pseudo-console (R09; RIDE's startOnPseudoConsole,
//  GRIDEd's NativeMethods.cpp in platform-invoke form, since GRIDE is one
//  managed translation unit and includes no <windows.h>). For a debugger that
//  flushes its prompt only to a console (cdb): what it writes comes through
//  `output`, what is typed at it goes in through `input`, and it lives in a job
//  of its own. CreatePseudoConsole is looked up at run time, as RIDE does, since
//  a Windows before 10 1809 has none.
// ============================================================================
#include "../Build/ProgramRunner.h"

namespace Gride
{
    public ref class NativeConsole abstract sealed
    {
    public:
        // the child on a console of `columns` x `rows` over two pipes, started suspended, put in a job, then
        // resumed; environment is the child's whole block, NAME=value a line. False, with the reason, where
        // this Windows has no pseudo-console or the start failed.
        static bool StartOnConsole(String^ commandLine, String^ workDir, array<String^>^ environment, int columns, int rows,
                                   IntPtr% process, IntPtr% console, IntPtr% input, IntPtr% output, IntPtr% job, String^% why)
        {
            process = IntPtr::Zero; console = IntPtr::Zero; input = IntPtr::Zero; output = IntPtr::Zero; job = IntPtr::Zero; why = String::Empty;
            IntPtr kernel = GetModuleHandleW("kernel32.dll");
            if (kernel == IntPtr::Zero || GetProcAddress(kernel, "CreatePseudoConsole") == IntPtr::Zero)
            {
                why = "this Windows has no pseudo-console (Windows 10 1809 or later has one)";
                return false;
            }
            IntPtr consoleReads, weWrite, weRead, consoleWrites;
            if (!CreatePipe(consoleReads, weWrite, IntPtr::Zero, 0)) { why = "CreatePipe failed"; return false; }
            if (!CreatePipe(weRead, consoleWrites, IntPtr::Zero, 0))
            {
                CloseHandle(consoleReads); CloseHandle(weWrite);
                why = "CreatePipe failed";
                return false;
            }
            Coord size;
            size.X = (short)(columns > 0 ? columns : 500);
            size.Y = (short)(rows > 0 ? rows : 50);
            IntPtr made;
            int hr = CreatePseudoConsole(size, consoleReads, consoleWrites, 0, made);
            CloseHandle(consoleReads);
            CloseHandle(consoleWrites);
            if (hr < 0)
            {
                CloseHandle(weWrite); CloseHandle(weRead);
                why = String::Format("CreatePseudoConsole failed (0x{0:X8})", (unsigned)hr);
                return false;
            }

            // the attribute list naming the console
            IntPtr room = IntPtr::Zero;
            InitializeProcThreadAttributeList(IntPtr::Zero, 1, 0, room);
            IntPtr list = room == IntPtr::Zero ? IntPtr::Zero : Marshal::AllocHGlobal(room);
            bool listed = list != IntPtr::Zero && InitializeProcThreadAttributeList(list, 1, 0, room) &&
                          UpdateProcThreadAttribute(list, 0, IntPtr(PseudoConsoleAttribute), made, IntPtr(IntPtr::Size), IntPtr::Zero, IntPtr::Zero);
            // the environment block: NAME=value\0 ... \0
            StringBuilder^ block = gcnew StringBuilder();
            if (environment != nullptr) for each (String^ pair in environment) if (!String::IsNullOrEmpty(pair)) block->Append(pair)->Append(L'\0');
            block->Append(L'\0');
            IntPtr env = Marshal::StringToHGlobalUni(block->ToString());
            StartupInfoEx si;
            si.cb = Marshal::SizeOf(StartupInfoEx::typeid);
            si.lpAttributeList = list;
            ProcessInformation child;
            bool ok = false;
            int error = 0;
            if (listed)
            {
                // the window's own standard handles kept from the child (RIDE's)
                IntPtr keepIn = GetStdHandle(StdInput), keepOut = GetStdHandle(StdOutput), keepErr = GetStdHandle(StdError);
                SetStdHandle(StdInput, IntPtr::Zero);
                SetStdHandle(StdOutput, IntPtr::Zero);
                SetStdHandle(StdError, IntPtr::Zero);
                StringBuilder^ writable = gcnew StringBuilder(commandLine);
                ok = CreateProcessW(nullptr, writable, IntPtr::Zero, IntPtr::Zero, false, ExtendedStartupInfoPresent | CreateSuspended | CreateUnicodeEnvironment,
                                    env, String::IsNullOrEmpty(workDir) ? nullptr : workDir, si, child);
                error = Marshal::GetLastWin32Error();
                SetStdHandle(StdInput, keepIn);
                SetStdHandle(StdOutput, keepOut);
                SetStdHandle(StdError, keepErr);
                DeleteProcThreadAttributeList(list);
            }
            if (list != IntPtr::Zero) Marshal::FreeHGlobal(list);
            Marshal::FreeHGlobal(env);
            if (!ok)
            {
                ClosePseudoConsole(made);
                CloseHandle(weWrite); CloseHandle(weRead);
                why = listed ? String::Format("CreateProcess failed (error {0})", error) : Str("the pseudo-console attribute could not be set");
                return false;
            }
            IntPtr j = CreateJob();
            if (j != IntPtr::Zero && !AssignProcessToJobObject(j, child.hProcess)) { CloseHandle(j); j = IntPtr::Zero; }
            ResumeThread(child.hThread);
            CloseHandle(child.hThread);
            process = child.hProcess;
            console = made;
            input = weWrite;
            output = weRead;
            job = j;
            return true;
        }

        // a blocking read from a pipe: the bytes read, 0 or less at its end
        static int ReadFrom(IntPtr pipe, array<Byte>^ buffer)
        {
            if (pipe == IntPtr::Zero || buffer == nullptr || buffer->Length == 0) return -1;
            int got = 0;
            if (!ReadFile(pipe, buffer, buffer->Length, got, IntPtr::Zero)) return -1;
            return got;
        }

        static bool WriteTo(IntPtr pipe, array<Byte>^ bytes)
        {
            if (pipe == IntPtr::Zero || bytes == nullptr) return false;
            int written = 0;
            while (written < bytes->Length)
            {
                array<Byte>^ rest = written == 0 ? bytes : SubArray(bytes, written);
                int went = 0;
                if (!WriteFile(pipe, rest, rest->Length, went, IntPtr::Zero) || went == 0) return false;
                written += went;
            }
            return true;
        }

        // ClosePseudoConsole waits for what the console wrote to be read, so a reader must still be reading
        static void CloseConsole(IntPtr console) { if (console != IntPtr::Zero) ClosePseudoConsole(console); }

        // true when the process ended within the time (-1 waits for it), with its exit code
        static bool WaitForProcess(IntPtr process, int ms, int% exitCode)
        {
            exitCode = 0;
            if (process == IntPtr::Zero) return true;
            if (WaitForSingleObject(process, ms < 0 ? 0xFFFFFFFFu : (unsigned)ms) != 0) return false;
            unsigned code = 0;
            GetExitCodeProcess(process, code);
            exitCode = (int)code;
            return true;
        }

        static void KillProcess(IntPtr process) { if (process != IntPtr::Zero) TerminateProcess(process, 1); }

        // a job a child and whatever it starts can be ended with (GRIDEd's: breaking away allowed)
        static IntPtr CreateJob() { return NativeMethods::CreateJobObject(IntPtr::Zero, nullptr); }
        static bool AssignToJob(IntPtr job, IntPtr process) { return job != IntPtr::Zero && process != IntPtr::Zero && NativeMethods::AssignProcessToJobObject(job, process); }
        static void TerminateJob(IntPtr job) { if (job != IntPtr::Zero) NativeMethods::TerminateJobObject(job, 1); }
        static void Close(IntPtr handle) { if (handle != IntPtr::Zero) CloseHandle(handle); }

    private:
        literal int PseudoConsoleAttribute = 0x00020016;       // PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
        literal unsigned ExtendedStartupInfoPresent = 0x00080000;
        literal unsigned CreateSuspended = 0x00000004;
        literal unsigned CreateUnicodeEnvironment = 0x00000400;
        literal int StdInput = -10, StdOutput = -11, StdError = -12;

        [StructLayout(LayoutKind::Sequential)]
        value struct Coord { short X; short Y; };

        [StructLayout(LayoutKind::Sequential, CharSet = CharSet::Unicode)]
        value struct StartupInfoEx
        {
            int cb;
            IntPtr lpReserved, lpDesktop, lpTitle;
            int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
            short wShowWindow, cbReserved2;
            IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
            IntPtr lpAttributeList;
        };

        [StructLayout(LayoutKind::Sequential)]
        value struct ProcessInformation { IntPtr hProcess; IntPtr hThread; int dwProcessId; int dwThreadId; };

        static array<Byte>^ SubArray(array<Byte>^ a, int from)
        {
            array<Byte>^ r = gcnew array<Byte>(a->Length - from);
            Array::Copy(a, from, r, 0, r->Length);
            return r;
        }

        [DllImport("kernel32.dll", CharSet = CharSet::Unicode)] static IntPtr GetModuleHandleW(String^ name);
        [DllImport("kernel32.dll", CharSet = CharSet::Ansi, BestFitMapping = false)] static IntPtr GetProcAddress(IntPtr module, String^ name);
        [DllImport("kernel32.dll", SetLastError = true)] [returnvalue: MarshalAs(UnmanagedType::Bool)]
        static bool CreatePipe([Out] IntPtr% read, [Out] IntPtr% write, IntPtr attributes, int size);
        [DllImport("kernel32.dll")] static int CreatePseudoConsole(Coord size, IntPtr input, IntPtr output, unsigned flags, [Out] IntPtr% console);
        [DllImport("kernel32.dll")] static void ClosePseudoConsole(IntPtr console);
        [DllImport("kernel32.dll", SetLastError = true)] [returnvalue: MarshalAs(UnmanagedType::Bool)]
        static bool InitializeProcThreadAttributeList(IntPtr list, int count, int flags, IntPtr% size);
        [DllImport("kernel32.dll", SetLastError = true)] [returnvalue: MarshalAs(UnmanagedType::Bool)]
        static bool UpdateProcThreadAttribute(IntPtr list, unsigned flags, IntPtr attribute, IntPtr value, IntPtr size, IntPtr previous, IntPtr returnSize);
        [DllImport("kernel32.dll")] static void DeleteProcThreadAttributeList(IntPtr list);
        [DllImport("kernel32.dll", CharSet = CharSet::Unicode, SetLastError = true)] [returnvalue: MarshalAs(UnmanagedType::Bool)]
        static bool CreateProcessW(String^ application, StringBuilder^ commandLine, IntPtr processAttributes, IntPtr threadAttributes,
                                   [MarshalAs(UnmanagedType::Bool)] bool inherit, unsigned flags, IntPtr environment, String^ directory,
                                   StartupInfoEx% startup, [Out] ProcessInformation% information);
        [DllImport("kernel32.dll")] static IntPtr GetStdHandle(int which);
        [DllImport("kernel32.dll")] [returnvalue: MarshalAs(UnmanagedType::Bool)] static bool SetStdHandle(int which, IntPtr handle);
        [DllImport("kernel32.dll")] static unsigned ResumeThread(IntPtr thread);
        [DllImport("kernel32.dll", SetLastError = true)] [returnvalue: MarshalAs(UnmanagedType::Bool)]
        static bool ReadFile(IntPtr file, [Out] array<Byte>^ buffer, int toRead, [Out] int% read, IntPtr overlapped);
        [DllImport("kernel32.dll", SetLastError = true)] [returnvalue: MarshalAs(UnmanagedType::Bool)]
        static bool WriteFile(IntPtr file, array<Byte>^ buffer, int toWrite, [Out] int% written, IntPtr overlapped);
        [DllImport("kernel32.dll")] static unsigned WaitForSingleObject(IntPtr handle, unsigned ms);
        [DllImport("kernel32.dll")] [returnvalue: MarshalAs(UnmanagedType::Bool)] static bool GetExitCodeProcess(IntPtr process, [Out] unsigned% code);
        [DllImport("kernel32.dll")] [returnvalue: MarshalAs(UnmanagedType::Bool)] static bool TerminateProcess(IntPtr process, unsigned code);
        [DllImport("kernel32.dll")] [returnvalue: MarshalAs(UnmanagedType::Bool)] static bool CloseHandle(IntPtr handle);
        [DllImport("kernel32.dll", SetLastError = true)] [returnvalue: MarshalAs(UnmanagedType::Bool)]
        static bool AssignProcessToJobObject(IntPtr job, IntPtr process);
    };
}
