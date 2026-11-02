// fault.cpp - an access violation with a SIGSEGV handler installed: the Microsoft runtime's start-up filter calls the
// handler, which here ends the program itself (its stderr line and exit code are the transcript, fault.expected).
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static void handler(int sig)
{
    void (*now)(int) = signal(SIGSEGV, handler);
    fprintf(stderr, "handler %d, then %s\n", sig, now == SIG_DFL ? "SIG_DFL" : "kept");
    _Exit(7);
}

int main(int argc, char **)
{
    signal(SIGSEGV, handler);
    fputs("before\n", stderr);
    volatile int *nowhere = (int *)(long long)(argc - 1);
    *nowhere = 1;
    fputs("after\n", stderr);
    return 0;
}
