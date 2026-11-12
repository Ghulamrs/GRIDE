/* second.c - a call through an import library of its own; second.def names what kernel32.def does not */
#include <stdio.h>

unsigned GetConsoleCP(void);

int main(void)
{
    printf("GetConsoleCP %s\n", GetConsoleCP() == 0xFFFFFFFFu ? "failed" : "answered");
    return 0;
}
