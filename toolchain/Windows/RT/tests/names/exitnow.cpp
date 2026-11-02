// exitnow.cpp - _Exit (ISO C99 7.20.4.4): the end at once, with no atexit or at_quick_exit function, no static
// object's destructor, no thread_local's, and no stream written. Run by probe.ps1 against the UCRT's transcript
// (exitnow.expected).
#include <stdio.h>
#include <stdlib.h>

struct Noisy {
    const char *name;
    explicit Noisy(const char *n) : name(n) {}
    ~Noisy() { fprintf(stderr, "%s destroyed\n", name); }
};
Noisy object("static");
thread_local Noisy perThread("thread_local");

static void notRun() { fputs("atexit ran\n", stderr); }

int main()
{
    atexit(notRun);
    at_quick_exit(notRun);
    printf("buffered, not flushed %s\n", perThread.name);
    fputs("leaving\n", stderr);
    _Exit(4);
}
