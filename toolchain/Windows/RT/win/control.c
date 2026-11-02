/* control.c - the console control handler signal sets for Ctrl+C and Ctrl+Break (c90; c\signals.cpp).
 *
 * Spec: the Microsoft runtime installs one when SIGINT or SIGBREAK is first given a handler; Windows calls it on a
 * thread of its own (which win\tls.asm's callback marks as a thread), and when it answers FALSE the next handler -
 * in the end Windows' own, which ends the process with 0xC000013A - takes the event. */
#include "grt.h"
#include "win32.h"

int __grt_on_console_control(int (*routine)(unsigned long))
{
    return SetConsoleCtrlHandler((void *)routine, 1) ? 0 : -1;
}
