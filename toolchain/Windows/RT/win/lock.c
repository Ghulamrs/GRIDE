/* lock.c - a lock and a condition for the layers above, over Windows' slim reader/writer locks and condition
 * variables (c90). Each is one pointer-sized word that starts zero (SRWLOCK_INIT, CONDITION_VARIABLE_INIT),
 * so a static one needs no initialiser. Used by C++'s function-local statics (cxx\support.cpp), the streams
 * (c\StreamWin.h) and the atexit list (c\atexit.cpp); the descriptors (files.c) call Windows' directly. */
#include "grt.h"
#include "win32.h"

void __grt_lock(void **lock)
{
    AcquireSRWLockExclusive(lock);
}

void __grt_unlock(void **lock)
{
    ReleaseSRWLockExclusive(lock);
}

void __grt_wait(void **condition, void **lock)
{
    SleepConditionVariableSRW(condition, lock, 0xFFFFFFFFul, 0);
}

void __grt_wake_all(void **condition)
{
    WakeAllConditionVariable(condition);
}
