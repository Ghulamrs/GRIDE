// windows.h - cppparse's declarations of the Win32 API that its own sources use (x64, so the
// calling-convention keywords are not needed). The real kernel32 import library resolves them.
#ifndef CPPPARSE_WINDOWS_H
#define CPPPARSE_WINDOWS_H

#ifndef _WIN32
#error "windows.h declares the Win32 API: only for a 64-bit Windows target"
#endif

#include <cstddef>

typedef int                BOOL;
typedef unsigned char      BYTE;
typedef unsigned short     WORD;
typedef unsigned long      DWORD;
typedef long               LONG;
typedef unsigned long      ULONG;
typedef unsigned int       UINT;
typedef int                INT;
typedef long long          LONGLONG;
typedef unsigned long long ULONGLONG;
typedef unsigned long long SIZE_T;
typedef void*              LPVOID;
typedef const void*        LPCVOID;
typedef void*              HANDLE;
typedef HANDLE             HMODULE;
typedef HANDLE             HWND;
typedef char*              LPSTR;
typedef const char*        LPCSTR;
typedef wchar_t*           LPWSTR;
typedef const wchar_t*     LPCWSTR;
typedef DWORD*             LPDWORD;
typedef long long          LPARAM;
typedef unsigned long long WPARAM;
typedef long long          LRESULT;
typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID parameter);

struct SECURITY_ATTRIBUTES { DWORD nLength; LPVOID lpSecurityDescriptor; BOOL bInheritHandle; };
typedef SECURITY_ATTRIBUTES* LPSECURITY_ATTRIBUTES;

struct CRITICAL_SECTION { void* DebugInfo; LONG LockCount; LONG RecursionCount; HANDLE OwningThread; HANDLE LockSemaphore; SIZE_T SpinCount; };
typedef CRITICAL_SECTION* LPCRITICAL_SECTION;

struct SYSTEM_INFO {
    WORD wProcessorArchitecture; WORD wReserved; DWORD dwPageSize; LPVOID lpMinimumApplicationAddress; LPVOID lpMaximumApplicationAddress;
    ULONGLONG dwActiveProcessorMask; DWORD dwNumberOfProcessors; DWORD dwProcessorType; DWORD dwAllocationGranularity; WORD wProcessorLevel; WORD wProcessorRevision;
};
typedef SYSTEM_INFO* LPSYSTEM_INFO;

union LARGE_INTEGER { struct { DWORD LowPart; LONG HighPart; } u; LONGLONG QuadPart; };

struct FILETIME { DWORD dwLowDateTime; DWORD dwHighDateTime; };
struct WIN32_FIND_DATAA {
    DWORD dwFileAttributes; FILETIME ftCreationTime; FILETIME ftLastAccessTime; FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh; DWORD nFileSizeLow; DWORD dwReserved0; DWORD dwReserved1; char cFileName[260]; char cAlternateFileName[14];
};
typedef WIN32_FIND_DATAA* LPWIN32_FIND_DATAA;

extern "C" {
    HANDLE CreateThread(LPSECURITY_ATTRIBUTES attributes, SIZE_T stackSize, LPTHREAD_START_ROUTINE start, LPVOID parameter, DWORD flags, LPDWORD threadId);
    DWORD  WaitForSingleObject(HANDLE handle, DWORD milliseconds);
    DWORD  WaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD milliseconds);
    BOOL   CloseHandle(HANDLE handle);
    HANDLE CreateEventA(LPSECURITY_ATTRIBUTES attributes, BOOL manualReset, BOOL initialState, LPCSTR name);
    BOOL   SetEvent(HANDLE event);
    BOOL   ResetEvent(HANDLE event);
    HANDLE CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES attributes, DWORD disposition, DWORD flags, HANDLE templateFile);
    HANDLE CreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES attributes, DWORD disposition, DWORD flags, HANDLE templateFile);
    HANDLE FindFirstFileA(LPCSTR pattern, LPWIN32_FIND_DATAA data);
    BOOL   FindNextFileA(HANDLE find, LPWIN32_FIND_DATAA data);
    BOOL   FindClose(HANDLE find);
    DWORD  GetFullPathNameA(LPCSTR name, DWORD size, LPSTR buffer, LPSTR* filePart);
    DWORD  GetModuleFileNameA(HMODULE module, LPSTR buffer, DWORD size);
    DWORD  GetLastError(void);
    DWORD  GetCurrentThreadId(void);
    DWORD  GetTickCount(void);
    void   Sleep(DWORD milliseconds);
    void   GetSystemInfo(LPSYSTEM_INFO info);
    void   InitializeCriticalSection(LPCRITICAL_SECTION section);
    void   EnterCriticalSection(LPCRITICAL_SECTION section);
    void   LeaveCriticalSection(LPCRITICAL_SECTION section);
    void   DeleteCriticalSection(LPCRITICAL_SECTION section);
    LONG   cppparse_interlocked_increment(LONG volatile* value);   // compiler intrinsics in MSVC: supplied by cppparse's runtime
    LONG   cppparse_interlocked_decrement(LONG volatile* value);
    BOOL   QueryPerformanceCounter(LARGE_INTEGER* counter);
    BOOL   QueryPerformanceFrequency(LARGE_INTEGER* frequency);
}

#define TRUE  1
#define FALSE 0
#define INFINITE 0xFFFFFFFF
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 258
#define WAIT_FAILED 0xFFFFFFFF
#define INVALID_HANDLE_VALUE ((HANDLE)(long long)-1)
#define MAX_PATH 260
#define GENERIC_READ 0x80000000
#define GENERIC_WRITE 0x40000000
#define FILE_SHARE_READ 1
#define OPEN_EXISTING 3
#define CREATE_ALWAYS 2
#define FILE_ATTRIBUTE_NORMAL 0x80
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define CreateFile  CreateFileA
#define CreateEvent CreateEventA
#define InterlockedIncrement cppparse_interlocked_increment
#define InterlockedDecrement cppparse_interlocked_decrement
#define WINAPI
#define CALLBACK
#define APIENTRY

#endif
