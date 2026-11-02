// quickexit.cpp - quick_exit (ISO C11 7.22.4.7): the at_quick_exit functions, last registered first, then the end with
// no atexit function, no static object's destructor and no stream written. Run by probe.ps1 against the UCRT's
// transcript (quickexit.expected).
#include <stdio.h>
#include <stdlib.h>

struct Static {
    ~Static() { fputs("static destroyed\n", stderr); }
};
Static object;

static void notRun() { fputs("atexit ran\n", stderr); }
static void one() { fputs("one\n", stderr); printf("one buffered\n"); }
static void two() { fputs("two\n", stderr); }

int main()
{
    atexit(notRun);
    at_quick_exit(one);
    at_quick_exit(two);
    printf("buffered, not flushed\n");
    fputs("leaving\n", stderr);
    quick_exit(3);
}
