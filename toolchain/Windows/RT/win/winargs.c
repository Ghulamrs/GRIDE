/* winargs.c - what WinMain and wWinMain are given besides the image (c90), a member of its own, so that a program with
 * main does not import what only they use.
 *
 * Spec: Microsoft C runtime - the line is the command line past the program's name, by the rule of the UCRT's
 * _get_narrow_winmain_command_line and not argv's: any character up to the space ends the name outside quotes, and any
 * such characters after it are skipped; the show command is STARTUPINFO's where STARTF_USESHOWWINDOW is set, else
 * SW_SHOWDEFAULT. */
#include "grt.h"
#include "win32.h"

char *__grt_winmain_line(void)
{
    unsigned char *p = (unsigned char *)GetCommandLineA();
    int quoted = 0;
    while (*p > ' ' || (*p && quoted)) {
        if (*p == '"') quoted = !quoted;
        p++;
    }
    while (*p && *p <= ' ') p++;
    return (char *)p;
}

WCHAR *__grt_wide_winmain_line(void)
{
    WCHAR *p = GetCommandLineW();
    int quoted = 0;
    while (*p > ' ' || (*p && quoted)) {
        if (*p == '"') quoted = !quoted;
        p++;
    }
    while (*p && *p <= ' ') p++;
    return p;
}

int __grt_show_command(void)
{
    GRT_STARTUPINFO info;
    GetStartupInfoW(&info);
    return info.dwFlags & GRT_STARTF_USESHOWWINDOW ? info.wShowWindow : GRT_SW_SHOWDEFAULT;
}
