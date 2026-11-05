// windows.h - the part of the Win32 API a program here has asked for.
//
// Not Microsoft's header, and not a copy of one - that is the Windows SDK's,
// and no part of this toolchain reads the SDK's headers. What a program wants
// from it is a few types, structs and prototypes, which are ordinary C. They
// must agree with Windows' own, because the program links against kernel32.lib
// and the system fills the structs: a member at the wrong offset is a wrong
// answer, a short struct a write past the object. Each struct below is laid
// out as the x64 SDK lays it out, with the offsets written beside it.
//
// One calling convention on x64, so WINAPI and CALLBACK are nothing. `long` is
// four bytes (LLP64), so DWORD and LONG are the SDK's four-byte types.
//
// **InterlockedIncrement and its family are not in any library on x64**:
// Microsoft's compiler makes each one an instruction with a `lock` prefix, and
// kernel32 exports none of them. This compiler has no such instruction to give,
// so here each is an inline function that takes a slim reader/writer lock
// around the read-modify-write. The lock is a static of the translation unit -
// zero, which is SRWLOCK_INIT, before any code runs, so no initialisation can
// race - which makes the operations atomic **against each other within one
// translation unit**. A counter several units update would need one lock for
// all of them; no program here has asked for that yet.
//
// For the Windows target only.
//
// Added 2026-10-20 for cppparse: its main thread's 512 MB stack (CreateThread,
// WaitForSingleObject) and its worker pool (GetSystemInfo, a critical section,
// WaitForMultipleObjects and an interlocked counter of the next job). WinMain,
// wWinMain and the types they take came with GRIDE's start-up for them.
#ifndef _CXX1_WINDOWS_H
#define _CXX1_WINDOWS_H

#ifndef _WIN32
#error "<windows.h> is here for the x86_64-windows target only"
#endif

#include <_gride.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WINAPI
#define APIENTRY
#define CALLBACK
#define TRUE 1
#define FALSE 0
#define INFINITE 0xFFFFFFFF
#define WAIT_OBJECT_0 0x00000000
#define WAIT_TIMEOUT 0x00000102
#define WAIT_FAILED 0xFFFFFFFF
#define CREATE_SUSPENDED 0x00000004
#define STACK_SIZE_PARAM_IS_A_RESERVATION 0x00010000
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_SHOWMINIMIZED 2
#define SW_SHOWMAXIMIZED 3
#define SW_SHOWDEFAULT 10

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;
typedef long LONG;
typedef unsigned long ULONG;
typedef unsigned int UINT;
typedef long long LONGLONG;
typedef unsigned long long ULONGLONG;
typedef unsigned long long ULONG_PTR;
typedef unsigned long long DWORD_PTR;
typedef unsigned long long SIZE_T;
typedef long long LONG_PTR;
typedef void *LPVOID;
typedef void *PVOID;
typedef const void *LPCVOID;
typedef void *HANDLE;
typedef DWORD *LPDWORD;
typedef char CHAR;
typedef char *LPSTR;
typedef const char *LPCSTR;
typedef wchar_t WCHAR;
typedef WCHAR *LPWSTR;
typedef WCHAR *PWSTR;
typedef const WCHAR *LPCWSTR;
struct HINSTANCE__ { int unused; };
typedef struct HINSTANCE__ *HINSTANCE;
typedef HINSTANCE HMODULE;

typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID parameter);

//  0 nLength  8 lpSecurityDescriptor  16 bInheritHandle  - 24 bytes
typedef struct _SECURITY_ATTRIBUTES {
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

//  0 dwOemId  4 dwPageSize  8 lpMinimumApplicationAddress  16 lpMaximumApplicationAddress
// 24 dwActiveProcessorMask  32 dwNumberOfProcessors  36 dwProcessorType
// 40 dwAllocationGranularity  44 wProcessorLevel  46 wProcessorRevision  - 48 bytes.
// dwOemId is a union in the SDK with two WORDs, wProcessorArchitecture and
// wReserved; the four bytes are the same either way.
typedef struct _SYSTEM_INFO {
    DWORD dwOemId;
    DWORD dwPageSize;
    LPVOID lpMinimumApplicationAddress;
    LPVOID lpMaximumApplicationAddress;
    DWORD_PTR dwActiveProcessorMask;
    DWORD dwNumberOfProcessors;
    DWORD dwProcessorType;
    DWORD dwAllocationGranularity;
    WORD wProcessorLevel;
    WORD wProcessorRevision;
} SYSTEM_INFO, *LPSYSTEM_INFO;

//  0 DebugInfo  8 LockCount  12 RecursionCount  16 OwningThread  24 LockSemaphore
// 32 SpinCount  - 40 bytes, the system's to use between Initialize and Delete.
typedef struct _RTL_CRITICAL_SECTION {
    void *DebugInfo;
    LONG LockCount;
    LONG RecursionCount;
    HANDLE OwningThread;
    HANDLE LockSemaphore;
    ULONG_PTR SpinCount;
} CRITICAL_SECTION, *LPCRITICAL_SECTION;

// One pointer, zero when free; SRWLOCK_INIT is that zero.
typedef struct _RTL_SRWLOCK {
    PVOID Ptr;
} SRWLOCK, *PSRWLOCK;
#define SRWLOCK_INIT {0}

// The program's own, which the runtime's start-up calls: declared so that a definition in C++ has its C name.
// Microsoft's start-up passes the image, a null pointer, the command line past the program's name, and the
// show command of STARTUPINFO, or SW_SHOWDEFAULT.
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR commandLine, int show);
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR commandLine, int show);

LPSTR GetCommandLineA(void);
LPWSTR GetCommandLineW(void);
HANDLE CreateThread(LPSECURITY_ATTRIBUTES attributes, SIZE_T stackSize, LPTHREAD_START_ROUTINE start,
                    LPVOID parameter, DWORD flags, LPDWORD threadId);
DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds);
DWORD WaitForMultipleObjects(DWORD count, const HANDLE *handles, BOOL all, DWORD milliseconds);
BOOL CloseHandle(HANDLE handle);
void GetSystemInfo(LPSYSTEM_INFO info);
DWORD GetTickCount(void);
ULONGLONG GetTickCount64(void);
DWORD GetLastError(void);
DWORD GetCurrentThreadId(void);
void Sleep(DWORD milliseconds);
void InitializeCriticalSection(LPCRITICAL_SECTION section);
void EnterCriticalSection(LPCRITICAL_SECTION section);
void LeaveCriticalSection(LPCRITICAL_SECTION section);
void DeleteCriticalSection(LPCRITICAL_SECTION section);
void AcquireSRWLockExclusive(PSRWLOCK lock);
void ReleaseSRWLockExclusive(PSRWLOCK lock);

// The interlocked operations: each answers what the standard SDK's does - the
// new value for Increment and Decrement, the old one for the other three. The
// parameter is the SDK's `LONG volatile *`, written `volatile LONG *` (a `LONG *`
// converts to it), and the lock, a call the compiler cannot see through, keeps
// the value out of a register across it.
static SRWLOCK __cxx1_interlocked;
_GRIDE_STATIC_INLINE LONG InterlockedIncrement(volatile LONG *p) {
    LONG now;
    AcquireSRWLockExclusive(&__cxx1_interlocked);
    now = ++*p;
    ReleaseSRWLockExclusive(&__cxx1_interlocked);
    return now;
}
_GRIDE_STATIC_INLINE LONG InterlockedDecrement(volatile LONG *p) {
    LONG now;
    AcquireSRWLockExclusive(&__cxx1_interlocked);
    now = --*p;
    ReleaseSRWLockExclusive(&__cxx1_interlocked);
    return now;
}
_GRIDE_STATIC_INLINE LONG InterlockedExchange(volatile LONG *p, LONG value) {
    LONG was;
    AcquireSRWLockExclusive(&__cxx1_interlocked);
    was = *p;
    *p = value;
    ReleaseSRWLockExclusive(&__cxx1_interlocked);
    return was;
}
_GRIDE_STATIC_INLINE LONG InterlockedExchangeAdd(volatile LONG *p, LONG value) {
    LONG was;
    AcquireSRWLockExclusive(&__cxx1_interlocked);
    was = *p;
    *p = was + value;
    ReleaseSRWLockExclusive(&__cxx1_interlocked);
    return was;
}
_GRIDE_STATIC_INLINE LONG InterlockedCompareExchange(volatile LONG *p, LONG value, LONG comparand) {
    LONG was;
    AcquireSRWLockExclusive(&__cxx1_interlocked);
    was = *p;
    if (was == comparand) *p = value;
    ReleaseSRWLockExclusive(&__cxx1_interlocked);
    return was;
}

#ifdef __cplusplus
}
#endif

#endif
