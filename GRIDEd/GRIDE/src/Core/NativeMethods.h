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
        // rich edit scroll position, used to keep the view still while colouring
        static System::Drawing::Point GetScrollPos(System::IntPtr hwnd);
        static void SetScrollPos(System::IntPtr hwnd, System::Drawing::Point p);
    };
}}
