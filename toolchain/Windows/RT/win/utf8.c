/* utf8.c - the program's narrow strings as UTF-8, once setlocale asks (c90; docs\Runtime_Design.md 6).
 *
 * Only setlocale calls in here, so a program that does not ask links none of it. A text console is then spoken to in
 * UTF-16, through WriteConsoleW and ReadConsoleW, so that its code page, which the console's other programs share,
 * stays as it is and every character shows: the Microsoft runtime goes through that code page, and writes '?' for
 * what is not in it. A file, a pipe and a console in binary mode take the bytes as they are (win\files.c).
 *
 * Not the Microsoft runtime's: argv and the environment are spelled again in UTF-8 here, where its argv stays in the
 * ANSI code page and so cannot name a file its fopen, now reading UTF-8, would find. The vectors main was given stay
 * the program's, holding the new strings; a string it has already taken stays as it was, as a pointer from getenv
 * stays valid. */
#include <errno.h>
#include <stdlib.h>
#include "grt.h"
#include "win32.h"

#define STAGED 1024             /* the bytes one WriteConsoleW takes at most */
#define CTRL_Z 0x1A

/* how many of the bytes at the end of a run begin a character that goes on past it: 0 to 3 */
static unsigned grt_cut_at_end(const char *bytes, unsigned count)
{
    unsigned back;
    for (back = 1; back <= 3 && back <= count; back++) {
        unsigned char b = (unsigned char)bytes[count - back];
        unsigned length = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
        if ((b & 0xC0) == 0x80) continue;
        return length > back ? back : 0;
    }
    return 0;
}

/* bytes as UTF-8 on a console, in runs that end between characters: a character cut in two by the end of a write, as
   a putchar loop cuts every one, waits in cut for the next. A byte that is not UTF-8 shows as U+FFFD. 1, or 0 with
   errno set. */
static int grt_console_put(void *handle, char *cut, unsigned *cutCount, const char *bytes, unsigned count)
{
    char staged[STAGED + 3];
    WCHAR wide[STAGED + 3];
    while (count > 0) {
        unsigned n = *cutCount, take = count < STAGED ? count : STAGED, i, end;
        int units, at = 0;
        for (i = 0; i < n; i++) staged[i] = cut[i];
        for (i = 0; i < take; i++) staged[n + i] = bytes[i];
        bytes += take;
        count -= take;
        n += take;
        end = grt_cut_at_end(staged, n);
        n -= end;
        for (i = 0; i < end; i++) cut[i] = staged[n + i];
        *cutCount = end;
        if (n == 0) continue;
        units = MultiByteToWideChar(GRT_CP_UTF8, 0, staged, (int)n, wide, (int)(sizeof wide / sizeof wide[0]));
        while (at < units) {
            DWORD wrote = 0;
            if (!WriteConsoleW(handle, wide + at, (DWORD)(units - at), &wrote, 0)) {
                __grt_set_errno_from(GetLastError());
                return 0;
            }
            if (wrote == 0) { errno = ENOSPC; return 0; }
            at += (int)wrote;
        }
    }
    return 1;
}

/* one read of a console into bytes (GRT_READ_BLOCK of them), as UTF-8: ReadFile would give what was typed in the
   console's code page, and nothing for a character outside it. A UTF-16 unit is three UTF-8 bytes at most, a pair
   four; a first surrogate that ends a read waits in high for its second. A line that begins with CTRL+Z is the end,
   as ReadFile answers it. The count, 0 at the end, -1 on an error. */
static int grt_console_get(void *handle, unsigned short *high, char *bytes)
{
    WCHAR wide[GRT_READ_BLOCK / 3];
    DWORD have, got;
    for (;;) {
        have = *high ? 1 : 0;
        got = 0;
        if (have) wide[0] = *high;
        if (!ReadConsoleW(handle, wide + have, GRT_READ_BLOCK / 3 - have, &got, 0)) {
            __grt_set_errno_from(GetLastError());
            return -1;
        }
        if (got == 0 || (!have && wide[0] == CTRL_Z)) return 0;
        *high = 0;
        have += got;
        if (wide[have - 1] >= 0xD800 && wide[have - 1] <= 0xDBFF) *high = wide[--have];
        if (have > 0) break;
    }
    return WideCharToMultiByte(GRT_CP_UTF8, 0, wide, (int)have, bytes, GRT_READ_BLOCK, 0, 0);
}

void __grt_set_utf8(int on)
{
    char **given = __argv, **again;
    int i;
    if (__grt_utf8 == on) return;
    __grt_utf8 = on;
    __grt_console_put = grt_console_put;
    __grt_console_get = grt_console_get;
    if (given) {
        __grt_arguments(0);
        again = __argv;
        for (i = 0; i < __argc; i++) given[i] = again[i];
        free(again);
        __argv = given;
    }
    if (__grt_envp_given) {
        given = __grt_envp_given;
        __grt_envp_given = 0;
        again = __grt_environment();
        for (i = 0; given[i] && again && again[i]; i++) given[i] = again[i];
    }
}
