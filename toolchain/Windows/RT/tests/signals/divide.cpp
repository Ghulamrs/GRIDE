// divide.cpp - an integer division by zero with a SIGFPE handler installed: the Microsoft runtime's start-up filter
// calls the handler with SIGFPE, which here ends the program itself (divide.expected).
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static void handler(int sig)
{
    fprintf(stderr, "handler %d\n", sig);
    _Exit(8);
}

int main(int argc, char **)
{
    signal(SIGFPE, handler);
    fputs("before\n", stderr);
    volatile int zero = argc - 1;
    int q = 100 / zero;
    fprintf(stderr, "after %d\n", q);
    return 0;
}
