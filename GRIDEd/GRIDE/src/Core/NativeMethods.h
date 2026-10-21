// =============================================================================
//  NativeMethods.h - the few Win32 calls WinForms does not expose.
//  Implemented in NativeMethods.cpp, which includes <windows.h> on its own
//  (that file does NOT use the precompiled header, so Win32 names never clash
//  with .NET names such as IDataObject or MessageBox).
// =============================================================================
#pragma once

namespace Gride { namespace Native {

    public ref class NativeMethods abstract sealed
    {
    public:
        // stop / resume painting of a window (used while re-colouring the editor)
        static void SetRedraw(System::IntPtr hwnd, bool on);
        // handle of the control that has keyboard focus (also nested ones)
        static System::IntPtr FocusedHandle();
        // first visible line of an edit / rich edit control
        static int FirstVisibleLine(System::IntPtr hwnd);
        // rich edit scroll position, used to keep the view still while colouring.
        // The values are 16-bit inside RichEdit, so only X is reliable in long files.
        static System::Drawing::Point GetScrollPos(System::IntPtr hwnd);
        static void SetScrollPos(System::IntPtr hwnd, System::Drawing::Point p);
        // client position of a character of a rich edit control (EM_POSFROMCHAR)
        static System::Drawing::Point PosFromChar(System::IntPtr hwnd, int index);
        // scrolls an edit control vertically by whole lines
        static void LineScroll(System::IntPtr hwnd, int lines);
        // 0 switches the RichEdit undo buffer off (GRIDE keeps its own history)
        static void SetUndoLimit(System::IntPtr hwnd, int limit);
        // the part of the window WM_PAINT is about to repaint (empty if none)
        static System::Drawing::Rectangle GetUpdateRect(System::IntPtr hwnd);
        static void DestroyIcon(System::IntPtr hicon);

        // job objects: a tool and every process it starts can be killed together
        static System::IntPtr CreateJob();
        static bool AssignToJob(System::IntPtr job, System::IntPtr process);
        static void TerminateJob(System::IntPtr job);
        static void CloseHandle(System::IntPtr handle);

        // a child on a pseudo-console (RIDE's startOnPseudoConsole), for a debugger that flushes its
        // prompt only to a console: what it writes comes through `output`, what is typed at it goes in
        // through `input`, and it lives in a job of its own. environment is the child's whole block,
        // NAME=value a line. False, with the reason, where this Windows has no CreatePseudoConsole
        // (before 10 1809) or the start failed.
        static bool StartOnConsole(System::String^ commandLine, System::String^ workDir, array<System::String^>^ environment,
                                   int columns, int rows, System::IntPtr% process, System::IntPtr% console,
                                   System::IntPtr% input, System::IntPtr% output, System::IntPtr% job, System::String^% why);
        // a blocking read from a pipe: the bytes read, 0 or less at its end
        static int ReadFrom(System::IntPtr pipe, array<System::Byte>^ buffer);
        static bool WriteTo(System::IntPtr pipe, array<System::Byte>^ bytes);
        // ClosePseudoConsole: it waits for what the console wrote to be read, so a reader must still be reading
        static void CloseConsole(System::IntPtr console);
        // true when the process ended within the time (-1 waits for it), with its exit code
        static bool WaitForProcess(System::IntPtr process, int ms, int% exitCode);
        static void KillProcess(System::IntPtr process);
    };
}}
