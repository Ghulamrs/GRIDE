/* temporary.c - the names tmpnam and tmpfile give (c90).
 *
 * Spec: the Microsoft runtime's form: the temporary folder (GetTempPathW), a letter - s for tmpnam, t for tmpfile -
 * the process's id in base 36, a dot and a count in base 36 kept for each letter ("C:\Users\me\AppData\Local\Temp\
 * sfw.0", ".1" ...), in the ANSI code page. A name some file already has is passed over. */
#include <errno.h>
#include "grt.h"
#include "win32.h"

static void *countLock;     /* an SRWLOCK, all zero to start */
static unsigned long counts[2];

/* value in base 36, lower case, at out; the end of what was written */
static char *grt_base36(char *out, unsigned long value)
{
    char digits[16];
    int n = 0;
    do {
        int d = (int)(value % 36);
        digits[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        value /= 36;
    } while (value);
    while (n) *out++ = digits[--n];
    return out;
}

int __grt_temporary_name(char letter, char *out, unsigned size)
{
    WCHAR folder[262];
    char name[300], *end;
    unsigned long *count = &counts[letter == 't'];
    DWORD n = GetTempPathW(262, folder);
    int bytes, held;
    unsigned long tries;
    if (n == 0 || n >= 262) { __grt_set_errno_from(GetLastError()); return -1; }
    bytes = WideCharToMultiByte(GRT_CP_ACP, 0, folder, -1, name, 260, 0, 0);
    if (bytes <= 0) { errno = ENOENT; return -1; }
    for (tries = 0; tries < 0x7FFFFFFFul; tries++) {
        unsigned long k;
        WCHAR *wide;
        GRT_FILE_ATTRIBUTE_DATA data;
        int taken;
        held = __grt_threads;
        if (held) __grt_lock(&countLock);
        k = (*count)++;
        if (held) __grt_unlock(&countLock);
        end = name + bytes - 1;
        *end++ = letter;
        end = grt_base36(end, GetCurrentProcessId());
        *end++ = '.';
        end = grt_base36(end, k);
        *end = 0;
        if ((unsigned)(end - name) + 1 > size) { errno = ERANGE; return -1; }
        wide = __grt_widen(name);
        if (!wide) return -1;
        taken = GetFileAttributesExW(wide, 0, &data);
        HeapFree(GetProcessHeap(), 0, wide);
        if (!taken) {
            unsigned i = 0;
            do out[i] = name[i]; while (name[i++]);
            return 0;
        }
    }
    errno = EEXIST;
    return -1;
}
