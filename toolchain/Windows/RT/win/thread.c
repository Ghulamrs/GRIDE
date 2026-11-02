/* thread.c - CreateThread, over kernel32's, so that the runtime knows when a program has a second thread (c90).
 *
 * Spec: docs\Runtime_Design.md 9.6. kernel32.def exports CreateThread as DATA, so kernel32.lib defines only
 * __imp_CreateThread, the import table's word Windows' loader fills with kernel32's function, and this is the
 * CreateThread a program links. It sets __grt_threads in the creating thread, before the new one exists: no call
 * of the runtime's is under way unlocked when the new thread can start locking, because the only thread there was
 * is here. The flag is never cleared.
 * __grt_thread_attach is the TLS callback (win\tls.asm) that sets it for a thread the runtime did not start - the
 * thread pool's, Ctrl+C's handler, a cl-built object's call of __imp_CreateThread - before that thread runs. */
#include "grt.h"
#include "win32.h"

typedef HANDLE (*Creator)(void *security, unsigned long long stack, DWORD (*start)(void *), void *parameter,
                          DWORD flags, DWORD *id);
extern Creator __imp_CreateThread;

int __grt_threads;

HANDLE CreateThread(void *security, unsigned long long stack, DWORD (*start)(void *), void *parameter, DWORD flags,
                    DWORD *id)
{
    __grt_threads = 1;
    return __imp_CreateThread(security, stack, start, parameter, flags, id);
}

void __grt_thread_attach(void *module, DWORD reason, void *reserved)
{
    (void)module;
    (void)reserved;
    if (reason == 2)
        __grt_threads = 1;
}
