// Spec: ISO C 7.19.10.4 - perror writes s, a colon and a space (when s is not null and not empty),
// then strerror(errno) and a new-line, to stderr.

#include <stdio.h>
#include <string.h>
#include <errno.h>

extern "C" void perror(const char *s)
{
    const char *message = strerror(errno);
    if (s && *s) {
        fputs(s, stderr);
        fputs(": ", stderr);
    }
    fputs(message, stderr);
    fputc('\n', stderr);
}
