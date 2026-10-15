// =============================================================================
//  NativeMethods.cpp - Win32 helpers (compiled WITHOUT the precompiled header)
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
// richedit.h is deliberately NOT included: with WIN32_LEAN_AND_MEAN its newer
// sections reference COM types (IStream ...) that ole2.h would normally declare.
// Only these two message ids are needed.
#ifndef EM_GETSCROLLPOS
#define EM_GETSCROLLPOS (WM_USER + 221)
#endif
#ifndef EM_SETSCROLLPOS
#define EM_SETSCROLLPOS (WM_USER + 222)
#endif

#using <System.dll>
#using <System.Drawing.dll>

#include "NativeMethods.h"

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
}}
