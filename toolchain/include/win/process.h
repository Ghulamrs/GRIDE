// process.h - the Microsoft runtime's process calls, as much of them as a program here has asked for
// (docs\Runtime_Design.md 9.4, the policy of 2026-11-10).
//
// Not Microsoft's header, and not a copy of one: the prototypes, which agree with the UCRT's. Both of GRIDE's
// runtimes define them: grt on Windows, RTS6x on the C6747, where the one program there is answers 1.
#ifndef _GRIDE_PROCESS_H
#define _GRIDE_PROCESS_H

#ifdef __cplusplus
extern "C" {
#endif

int _getpid(void);
#if defined(__cplusplus) || defined(_POSIX_C_SOURCE) || defined(_POSIX_SOURCE)
int getpid(void);
#endif

#ifdef __cplusplus
}
#endif

#endif