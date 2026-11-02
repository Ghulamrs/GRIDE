/* seh.c - what C++ exceptions (cxx\exceptions.cpp) ask of Windows, and the runtime's block for each thread (c90).
 *
 * grtwin is the one layer that calls kernel32, so the exception functions grtcxx needs are passed through here
 * unchanged: RaiseException, RtlUnwindEx, RtlPcToFileHeader, RtlLookupFunctionEntry and SetUnhandledExceptionFilter
 * (x64 Windows exports them from kernel32, so no ntdll import is needed).
 *
 * Each thread's block (struct __grt_thread, grt.h) lives in a fiber-local slot taken at the first use in the
 * process, made zero at its thread's first use and freed when the thread ends. Windows' last error is kept across
 * the lookup, as the Microsoft runtime keeps it across its own, so a program's GetLastError is not disturbed. */
#include "grt.h"
#include "win32.h"

void __grt_raise(unsigned long code, unsigned long flags, unsigned long count, const unsigned long long *arguments)
{
    RaiseException(code, flags, count, arguments);
}

void __grt_unwind(void *targetFrame, void *targetIp, void *record, void *returnValue, void *context, void *history)
{
    RtlUnwindEx(targetFrame, targetIp, record, returnValue, context, history);
}

void *__grt_image_of(const void *address)
{
    void *base = 0;
    return RtlPcToFileHeader((void *)address, &base);
}

void *__grt_function_of(unsigned long long pc, unsigned long long *imageBase)
{
    return RtlLookupFunctionEntry(pc, imageBase, 0);
}

void *__grt_set_unhandled_filter(void *filter)
{
    return SetUnhandledExceptionFilter(filter);
}

static DWORD slot = GRT_FLS_OUT_OF_INDEXES;
static void *slotLock;

static void grt_release(void *block)
{
    if (block) HeapFree(GetProcessHeap(), 0, block);
}

struct __grt_thread *__grt_thread(void)
{
    DWORD error = GetLastError();
    struct __grt_thread *t;
    if (slot == GRT_FLS_OUT_OF_INDEXES) {
        __grt_lock(&slotLock);
        if (slot == GRT_FLS_OUT_OF_INDEXES) slot = FlsAlloc(grt_release);
        __grt_unlock(&slotLock);
        if (slot == GRT_FLS_OUT_OF_INDEXES) __grt_fail_fast();
    }
    t = (struct __grt_thread *)FlsGetValue(slot);
    if (!t) {
        t = (struct __grt_thread *)HeapAlloc(GetProcessHeap(), GRT_HEAP_ZERO_MEMORY, sizeof *t);
        if (!t || !FlsSetValue(slot, t)) __grt_fail_fast();
    }
    SetLastError(error);
    return t;
}
