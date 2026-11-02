// badsignal.cpp - signal with a number the Microsoft runtime does not know: an invalid parameter, which in a Release
// program is the fast fail (badsignal.expected).
#include <signal.h>
#include <stdio.h>

static void handler(int) {}

int main()
{
    fputs("asking\n", stderr);
    signal(3, handler);
    fputs("after\n", stderr);
    return 0;
}
