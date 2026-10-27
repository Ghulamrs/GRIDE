/* start.c - mainCRTStartup, where a console program begins (c90), in grtstart.
 *
 * Spec: ISO C 5.1.2.2 (main called with argc, argv - argv[argc] a null pointer - and its return given
 * to exit), C++ 3.6.2 (the objects with static storage constructed before main), and the Microsoft
 * runtime's tables the compilers fill for that: .CRT$XI* (C initialisers, int (*)(void), a non-zero
 * answer a failure) and .CRT$XC* (C++ dynamic initialisers, void (*)(void)), each between the markers
 * of sections.asm, null entries skipped. A destructor of such an object is registered with atexit by
 * its initialiser, so exit runs them, last constructed first, with the program's own atexit functions.
 * exit is RTS6x's; it writes every stream's waiting bytes, then ends in __rts6x_halt (win\halt.asm). */
#include <stdlib.h>
#include "grt.h"

typedef int (*CInitialiser)(void);
typedef void (*Initialiser)(void);

extern CInitialiser __xi_a[], __xi_z[];
extern Initialiser __xc_a[], __xc_z[];

int main(int argc, char **argv, char **envp);

/* the word cl's objects ask for when they use floating point; nothing here reads it */
int _fltused = 0x9875;

void mainCRTStartup(void)
{
    int argc;
    char **argv;
    CInitialiser *c;
    Initialiser *i;
    __grt_memory_start();
    __grt_clock_start();
    __grt_files_start();
    __grt_streams_start();
    for (c = __xi_a; c < __xi_z; c++)
        if (*c && (*c)() != 0) {
            __grt_say("grt: a C initialiser failed\n");
            __rts6x_halt(255);
        }
    for (i = __xc_a; i < __xc_z; i++)
        if (*i) (*i)();
    __grt_arguments(&argc, &argv);
    exit(main(argc, argv, __grt_environment()));
}
