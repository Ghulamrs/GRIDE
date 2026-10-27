/* messages.c - the runtime's own words: strerror's, and a failed assertion's (c90).
 *
 * Spec: ISO C 7.21.6.2 (strerror) and 7.2.1.1 (assert writes the expression, the file and the line,
 * then calls abort); the words are the Microsoft runtime's, so a program's perror and assert say on
 * Windows what they say built with cl: its _sys_errlist for 0 to 42, "Unknown error" past them, and
 * "Assertion failed: %s, file %s, line %d" for a console program. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const words[] = {
    "No error", "Operation not permitted", "No such file or directory", "No such process",
    "Interrupted function call", "Input/output error", "No such device or address", "Arg list too long",
    "Exec format error", "Bad file descriptor", "No child processes", "Resource temporarily unavailable",
    "Not enough space", "Permission denied", "Bad address", "Unknown error", "Resource device",
    "File exists", "Improper link", "No such device", "Not a directory", "Is a directory",
    "Invalid argument", "Too many open files in system", "Too many open files",
    "Inappropriate I/O control operation", "Unknown error", "File too large", "No space left on device",
    "Invalid seek", "Read-only file system", "Too many links", "Broken pipe", "Domain error",
    "Result too large", "Unknown error", "Resource deadlock avoided", "Unknown error", "Filename too long",
    "No locks available", "Function not implemented", "Directory not empty", "Illegal byte sequence"
};

char *strerror(int code)
{
    if (code < 0 || code >= (int)(sizeof words / sizeof words[0])) return (char *)"Unknown error";
    return (char *)words[code];
}

void _assert(const char *expression, const char *file, unsigned line)
{
    fprintf(stderr, "Assertion failed: %s, file %s, line %d\n", expression, file, (int)line);
    abort();
}
