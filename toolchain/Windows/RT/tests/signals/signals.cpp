// signals.cpp - signal and raise as the Microsoft runtime's (ISO C 7.14, docs\Runtime_Design.md 16): what signal
// answers for each number, whether a handler stays installed once it has run, raise's answers, and errno. Run by
// probe.ps1 against the UCRT's transcript (signals.expected). The SEH signals and the default actions end the
// program, and are the probes beside this one (fault.cpp, divide.cpp, default.cpp), as does a number signal does
// not know (badsignal.cpp: an invalid parameter, the fast fail).
#include <errno.h>
#include <signal.h>
#include <stdio.h>

static int seen;
static void handler(int sig)
{
    seen = sig;
    printf("  handler %d\n", sig);
}

static const char *name(void (*h)(int))
{
    if (h == SIG_DFL) return "SIG_DFL";
    if (h == SIG_IGN) return "SIG_IGN";
    if (h == SIG_ERR) return "SIG_ERR";
    if (h == handler) return "handler";
    return "other";
}

int main()
{
    int numbers[] = { SIGINT, SIGILL, SIGFPE, SIGSEGV, SIGTERM, 21, SIGABRT, 6 };
    for (int i = 0; i < 8; i++) {
        int sig = numbers[i];
        errno = 0;
        void (*was)(int) = signal(sig, handler);
        int e = errno;
        void (*now)(int) = signal(sig, SIG_DFL);
        printf("signal %d: %s errno %d, then %s\n", sig, name(was), e, name(now));
    }
    int sigs[] = { SIGINT, SIGILL, SIGFPE, SIGSEGV, SIGTERM, SIGABRT, 21 };
    for (int i = 0; i < 7; i++) {
        int sig = sigs[i];
        signal(sig, handler);
        seen = 0;
        errno = 0;
        int r = raise(sig);
        int e = errno;
        void (*after)(int) = signal(sig, SIG_IGN);
        printf("raise %d: %d errno %d, seen %d, then %s\n", sig, r, e, seen, name(after));
        errno = 0;
        r = raise(sig);
        printf("raise %d ignored: %d errno %d\n", sig, r, errno);
        signal(sig, SIG_DFL);
    }
    return 0;
}
