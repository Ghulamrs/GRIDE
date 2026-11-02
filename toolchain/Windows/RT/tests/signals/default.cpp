// default.cpp - raise with SIG_DFL installed: the Microsoft runtime's default action, whose exit code and output are
// the transcript (default.expected). Buffered output not flushed shows whether the end writes the streams.
#include <signal.h>
#include <stdio.h>

int main()
{
    printf("buffered\n");
    fputs("raising SIGTERM\n", stderr);
    raise(SIGTERM);
    fputs("after\n", stderr);
    return 0;
}
