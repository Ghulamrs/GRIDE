/* msprocess.c - Microsoft's process names, _getpid and _putenv, and their old spellings getpid and putenv, as the
   UCRT answers them (docs\Runtime_Design.md 9.4). getenv sees what _putenv set, names compared as Windows' are. */
#define _POSIX_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <process.h>

static void show(const char *name)
{
    const char *v = getenv(name);
    printf("  getenv(%s): %s\n", name, v ? v : "(none)");
}

int main(void)
{
    int pid = _getpid();
    printf("_getpid: positive %d, steady %d, getpid the same %d\n", pid > 0, pid == _getpid(), pid == getpid());

    show("GRIDE_MSPROBE");
    printf("_putenv set: %d\n", _putenv("GRIDE_MSPROBE=first value"));
    show("GRIDE_MSPROBE");
    show("gride_msprobe");
    printf("_putenv replace, another case: %d\n", _putenv("Gride_MsProbe=second"));
    show("GRIDE_MSPROBE");
    printf("_putenv a second name: %d\n", _putenv("GRIDE_MSPROBE2=x=y"));
    show("GRIDE_MSPROBE2");
    printf("_putenv remove: %d\n", _putenv("GRIDE_MSPROBE="));
    show("GRIDE_MSPROBE");
    show("GRIDE_MSPROBE2");
    printf("_putenv remove what is not there: %d\n", _putenv("GRIDE_MSPROBE_NEVER="));
    printf("putenv: %d\n", putenv("GRIDE_MSPROBE3=old name"));
    show("GRIDE_MSPROBE3");
    return 0;
}
