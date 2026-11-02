// ctrlc.cpp - Ctrl+C sent to the program's own console: with a SIGINT handler installed the handler runs, on the thread
// Windows starts for the event, and is set back to SIG_DFL; with SIG_DFL the program ends as Windows ends it
// (ctrlc.expected). Run by probe.ps1 -OwnConsole, since the event reaches every process the console has.
#include <signal.h>
#include <stdio.h>

extern "C" int GenerateConsoleCtrlEvent(unsigned long, unsigned long);
extern "C" void Sleep(unsigned long);

static volatile int seen;

static void handler(int sig)
{
    seen = sig;
}

int main()
{
    signal(SIGINT, handler);
    fputs("sending\n", stderr);
    GenerateConsoleCtrlEvent(0, 0);
    for (int i = 0; i < 500 && !seen; i++) Sleep(10);
    void (*now)(int) = signal(SIGINT, SIG_DFL);
    fprintf(stderr, "seen %d, then %s\n", seen, now == SIG_DFL ? "SIG_DFL" : "handler");
    fputs("sending with SIG_DFL\n", stderr);
    GenerateConsoleCtrlEvent(0, 0);
    Sleep(3000);
    fputs("still here\n", stderr);
    return 0;
}
