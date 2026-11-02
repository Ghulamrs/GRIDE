/* system.c - system, a command handed to the command processor (c90).
 *
 * Spec: Microsoft C runtime, system (its _spawnve and _spawnvpe beneath). The processor is the program COMSPEC
 * names, run as "<COMSPEC> /c <command>" - the parts joined by single spaces, nothing quoted. Where COMSPEC is not
 * set, or its program cannot be found or opened (ENOENT, EACCES), "cmd.exe /c <command>" is run, cmd.exe found as
 * _spawnvp finds a program: a bare name in the working directory first, then in each folder of PATH. A name with no
 * extension is tried with .com, .exe, .bat and .cmd. The child inherits the handles that can be inherited and the
 * environment; system waits for it and answers its exit code, or -1 with errno set when it could not start.
 * system(NULL) answers 1 when COMSPEC names something that exists, else 0. An empty COMSPEC fails fast, as the
 * runtime's check of _spawnve's parameters does.
 *
 * There is no command processor of GRIDE's own (docs\Runtime_Design.md 3, general use): a command means what the
 * processor makes of it, so a program does here what it does on Microsoft's runtime - where cmd.exe is refused by
 * policy, it prints the refusal and system answers its exit code. Nothing here calls the C library (this is the
 * lowest layer): the strings are measured and copied by hand. */
#include <errno.h>
#include <stdlib.h>
#include "grt.h"
#include "win32.h"

#define MAX_PATH 260

static unsigned grt_length(const char *s)
{
    unsigned n = 0;
    while (s[n]) n++;
    return n;
}

static const char *grt_last(const char *s, char c)
{
    const char *at = 0;
    for (; *s; s++) if (*s == c) at = s;
    return at;
}

static char *grt_append_text(char *to, const char *from)
{
    while (*from) *to++ = *from++;
    *to = 0;
    return to;
}

/* 0 when path names something (the runtime's _access_s(path, 0)); else -1, errno and *error set */
static int grt_exists(const char *path, DWORD *error)
{
    WCHAR *wide = __grt_widen(path);
    GRT_FILE_ATTRIBUTE_DATA data;
    int ok;
    if (!wide) return -1;
    ok = GetFileAttributesExW(wide, 0, &data);
    if (!ok) *error = GetLastError();
    HeapFree(GetProcessHeap(), 0, wide);
    if (ok) return 0;
    __grt_set_errno_from(*error);
    return -1;
}

/* program started with the command line line, waited for: its exit code, or -1 */
static int grt_run(const char *program, const char *line, DWORD *error)
{
    WCHAR *wideProgram = __grt_widen(program), *wideLine;
    GRT_STARTUPINFO startup;
    GRT_PROCESS_INFORMATION child;
    unsigned char *b;
    unsigned i;
    DWORD code;
    int started, result = -1;
    if (!wideProgram) return -1;
    wideLine = __grt_widen(line);
    if (!wideLine) { HeapFree(GetProcessHeap(), 0, wideProgram); return -1; }
    for (b = (unsigned char *)&startup, i = 0; i < sizeof startup; i++) b[i] = 0;
    startup.cb = sizeof startup;
    started = CreateProcessW(wideProgram, wideLine, 0, 0, 1, 0, 0, 0, &startup, &child);
    if (!started) *error = GetLastError();
    HeapFree(GetProcessHeap(), 0, wideProgram);
    HeapFree(GetProcessHeap(), 0, wideLine);
    if (!started) { __grt_set_errno_from(*error); return -1; }
    WaitForSingleObject(child.hProcess, GRT_INFINITE);
    if (GetExitCodeProcess(child.hProcess, &code)) result = (int)code;
    else { *error = GetLastError(); __grt_set_errno_from(*error); }
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    return result;
}

/* _spawnve: file as it is when its name has an extension, else with each executable one in turn; a bare name in
   the working directory */
static int grt_spawn(const char *file, const char *line, DWORD *error)
{
    static const char kinds[4][5] = { ".com", ".exe", ".bat", ".cmd" };
    const char *back = grt_last(file, '\\'), *fore = grt_last(file, '/'), *end = back, *path = file;
    char *made = 0, *tried;
    unsigned n;
    int k, saved, result = -1;
    if (!fore) {
        if (!back) {
            end = grt_last(file, ':');
            if (!end) {
                made = (char *)malloc(grt_length(file) + 3);
                if (!made) return -1;
                made[0] = '.';
                made[1] = '\\';
                grt_append_text(made + 2, file);
                path = made;
                end = made + 2;
            }
        }
    } else if (!back || fore > back)
        end = fore;
    if (grt_last(end, '.')) {
        if (grt_exists(path, error) == 0) result = grt_run(path, line, error);
    } else {
        n = grt_length(path);
        tried = (char *)malloc(n + 5);
        if (tried) {
            grt_append_text(tried, path);
            saved = errno;
            for (k = 0; k < 4; k++) {
                grt_append_text(tried + n, kinds[k]);
                if (grt_exists(tried, error) == 0) {
                    errno = saved;
                    result = grt_run(tried, line, error);
                    break;
                }
            }
            free(tried);
        }
    }
    free(made);
    return result;
}

/* _getpath: the next folder of a PATH list into folder, its quotes dropped and the semicolons around it skipped,
   at most capacity - 2 characters; what is left of the list, or 0 when nothing was left or the folder too long */
static const char *grt_next_folder(const char *list, char *folder, unsigned capacity)
{
    const char *at = list, *first;
    char *out = folder, *end = folder + capacity - 1;
    folder[0] = 0;
    while (*at == ';') at++;
    first = at;
    while (*at && *at != ';') {
        int quoted = *at == '"';
        if (quoted) at++;
        while (*at && (quoted ? *at != '"' : *at != ';' && *at != '"')) {
            *out++ = *at++;
            if (out == end) { *out = 0; errno = ERANGE; return 0; }
        }
        if (quoted && *at) at++;
    }
    while (*at == ';') at++;
    *out = 0;
    return at == first ? 0 : at;
}

/* _spawnvpe: spawn, then, for a bare name not found, each folder of PATH; errno as it was when nothing set it */
static int grt_spawn_searching(const char *file, const char *line)
{
    DWORD error = 0;
    int saved = errno, result;
    const char *list;
    char *folder;
    unsigned n;
    errno = 0;
    result = grt_spawn(file, line, &error);
    if (result == -1 && errno == ENOENT && !grt_last(file, '\\') && !grt_last(file, '/') && file[1] != ':' &&
        (list = getenv("PATH")) != 0 && (folder = (char *)malloc(MAX_PATH)) != 0) {
        while ((list = grt_next_folder(list, folder, MAX_PATH - 1)) != 0 && folder[0]) {
            n = grt_length(folder);
            if (folder[n - 1] != '\\' && folder[n - 1] != '/') { folder[n++] = '\\'; folder[n] = 0; }
            if (n + grt_length(file) >= MAX_PATH) break;
            grt_append_text(folder + n, file);
            errno = 0;
            result = grt_spawn(folder, line, &error);
            if (result != -1) break;
            if (errno == ENOENT || error == GRT_ERROR_NOT_READY) continue;
            if ((folder[0] == '/' && folder[1] == '/') || (folder[0] == '\\' && folder[1] == '\\')) continue;
            break;
        }
        free(folder);
    }
    if (errno == 0 && saved != 0) errno = saved;
    return result;
}

/* "<program> /c <command>", from malloc */
static char *grt_command_line(const char *program, const char *command)
{
    char *line = (char *)malloc(grt_length(program) + grt_length(command) + 5);
    if (line) grt_append_text(grt_append_text(grt_append_text(line, program), " /c "), command);
    return line;
}

int system(const char *command)
{
    const char *comspec = getenv("COMSPEC");
    char *line;
    DWORD error = 0;
    int saved, result;
    if (!command) return comspec != 0 && grt_exists(comspec, &error) == 0;
    if (comspec) {
        if (!comspec[0]) __grt_fail_fast();
        line = grt_command_line(comspec, command);
        if (!line) return -1;
        saved = errno;
        errno = 0;
        result = grt_spawn(comspec, line, &error);
        free(line);
        if (result != -1) { errno = saved; return result; }
        if (errno != ENOENT && errno != EACCES) return result;
        errno = saved;
    }
    line = grt_command_line("cmd.exe", command);
    if (!line) return -1;
    result = grt_spawn_searching("cmd.exe", line);
    free(line);
    return result;
}
