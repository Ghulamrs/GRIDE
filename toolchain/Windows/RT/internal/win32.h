/* win32.h - the kernel32 functions grt calls, declared in the types c90 and cpp11 both read.
 *
 * Not the SDK's <windows.h>: grt is built where there is no SDK, and kernel32.def (beside src\)
 * names exactly these functions for the import library link /LIB /DEF writes. A function added here
 * is added there too, or the link of every program says which one is missing.
 *
 * x86-64 Windows has one calling convention, so nothing marks these as WINAPI; DWORD is unsigned long,
 * thirty-two bits under LLP64, and a HANDLE a pointer. */
#ifndef GRT_WIN32_H
#define GRT_WIN32_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void *HANDLE;
typedef unsigned long DWORD;
typedef int BOOL;
typedef unsigned short WCHAR;
typedef unsigned int UINT;

typedef struct { DWORD low, high; } GRT_FILETIME;
typedef struct {
    unsigned short wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} GRT_SYSTEMTIME;
typedef struct {
    long Bias;
    WCHAR StandardName[32];
    GRT_SYSTEMTIME StandardDate;
    long StandardBias;
    WCHAR DaylightName[32];
    GRT_SYSTEMTIME DaylightDate;
    long DaylightBias;
} GRT_TIME_ZONE_INFORMATION;
typedef struct {                        /* WIN32_FIND_DATAW, 592 bytes */
    DWORD dwFileAttributes;
    GRT_FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow;
    DWORD dwReserved0, dwReserved1;
    WCHAR cFileName[260];
    WCHAR cAlternateFileName[14];
} GRT_FIND_DATA;
typedef struct {                        /* BY_HANDLE_FILE_INFORMATION */
    DWORD dwFileAttributes;
    GRT_FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD dwVolumeSerialNumber, nFileSizeHigh, nFileSizeLow, nNumberOfLinks, nFileIndexHigh, nFileIndexLow;
} GRT_FILE_INFORMATION;
typedef struct {                        /* STARTUPINFOW, 104 bytes */
    DWORD cb;
    WCHAR *lpReserved, *lpDesktop, *lpTitle;
    DWORD dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
    unsigned short wShowWindow, cbReserved2;
    unsigned char *lpReserved2;
    HANDLE hStdInput, hStdOutput, hStdError;
} GRT_STARTUPINFO;
typedef struct {
    HANDLE hProcess, hThread;
    DWORD dwProcessId, dwThreadId;
} GRT_PROCESS_INFORMATION;

#define GRT_STD_INPUT    ((DWORD)-10)
#define GRT_STD_OUTPUT   ((DWORD)-11)
#define GRT_STD_ERROR    ((DWORD)-12)
#define GRT_INVALID_HANDLE ((HANDLE)(long long)-1)
#define GRT_FILE_TYPE_DISK 1
#define GRT_FILE_TYPE_CHAR 2
#define GRT_FILE_TYPE_PIPE 3
#define GRT_GENERIC_READ  0x80000000ul
#define GRT_GENERIC_WRITE 0x40000000ul
#define GRT_SHARE_READ_WRITE 3ul
#define GRT_CREATE_ALWAYS 2ul
#define GRT_OPEN_EXISTING 3ul
#define GRT_OPEN_ALWAYS 4ul
#define GRT_TRUNCATE_EXISTING 5ul
#define GRT_FILE_ATTRIBUTE_NORMAL 0x80ul
#define GRT_HEAP_ZERO_MEMORY 8ul
#define GRT_CP_ACP 0u
#define GRT_ERROR_FILE_NOT_FOUND 2ul
#define GRT_ERROR_PATH_NOT_FOUND 3ul
#define GRT_ERROR_TOO_MANY_OPEN_FILES 4ul
#define GRT_ERROR_ACCESS_DENIED 5ul
#define GRT_ERROR_INVALID_HANDLE 6ul
#define GRT_ERROR_NOT_ENOUGH_MEMORY 8ul
#define GRT_ERROR_INVALID_DRIVE 15ul
#define GRT_ERROR_NOT_SAME_DEVICE 17ul
#define GRT_ERROR_SHARING_VIOLATION 32ul
#define GRT_ERROR_LOCK_VIOLATION 33ul
#define GRT_ERROR_FILE_EXISTS 80ul
#define GRT_ERROR_INVALID_PARAMETER 87ul
#define GRT_ERROR_BROKEN_PIPE 109ul
#define GRT_ERROR_DISK_FULL 112ul
#define GRT_ERROR_INVALID_NAME 123ul
#define GRT_ERROR_DIR_NOT_EMPTY 145ul
#define GRT_ERROR_ALREADY_EXISTS 183ul
#define GRT_ERROR_FILENAME_EXCED_RANGE 206ul
#define GRT_MOVEFILE_COPY_ALLOWED 2ul
#define GRT_TIME_ZONE_ID_INVALID 0xFFFFFFFFul
#define GRT_ERROR_NO_MORE_FILES 18ul
#define GRT_ERROR_NOT_READY 21ul
#define GRT_INVALID_FILE_ATTRIBUTES 0xFFFFFFFFul
#define GRT_FILE_ATTRIBUTE_READONLY 0x01ul
#define GRT_FILE_ATTRIBUTE_DIRECTORY 0x10ul
#define GRT_FILE_READ_ATTRIBUTES 0x80ul
#define GRT_SHARE_ALL 7ul                           /* read, write and delete */
#define GRT_FILE_FLAG_BACKUP_SEMANTICS 0x02000000ul /* lets CreateFileW open a directory */
#define GRT_FILE_TYPE_UNKNOWN 0
#define GRT_FILE_TYPE_REMOTE 0x8000
#define GRT_FIND_EX_INFO_STANDARD 0
#define GRT_FIND_EX_SEARCH_NAME_MATCH 0
#define GRT_DRIVE_NO_ROOT_DIR 1u
#define GRT_INFINITE 0xFFFFFFFFul

HANDLE GetStdHandle(DWORD which);
DWORD GetFileType(HANDLE file);
BOOL GetConsoleMode(HANDLE console, DWORD *mode);
BOOL WriteFile(HANDLE file, const void *bytes, DWORD count, DWORD *written, void *overlapped);
BOOL ReadFile(HANDLE file, void *bytes, DWORD count, DWORD *read, void *overlapped);
HANDLE CreateFileW(const WCHAR *path, DWORD access, DWORD share, void *security, DWORD disposition,
                   DWORD attributes, HANDLE templ);
BOOL CloseHandle(HANDLE handle);
BOOL SetFilePointerEx(HANDLE file, long long distance, long long *position, DWORD method);
BOOL DeleteFileW(const WCHAR *path);
BOOL MoveFileExW(const WCHAR *from, const WCHAR *to, DWORD flags);
DWORD GetLastError(void);
void SetLastError(DWORD code);
DWORD GetFileAttributesW(const WCHAR *path);
BOOL GetFileInformationByHandle(HANDLE file, GRT_FILE_INFORMATION *information);
BOOL PeekNamedPipe(HANDLE pipe, void *bytes, DWORD size, DWORD *read, DWORD *available, DWORD *left);
UINT GetDriveTypeW(const WCHAR *root);
HANDLE FindFirstFileExW(const WCHAR *pattern, int level, GRT_FIND_DATA *found, int search, void *filter, DWORD flags);
BOOL FindNextFileW(HANDLE find, GRT_FIND_DATA *found);
BOOL FindClose(HANDLE find);
DWORD GetCurrentDirectoryW(DWORD capacity, WCHAR *path);
DWORD GetFullPathNameW(const WCHAR *path, DWORD capacity, WCHAR *full, WCHAR **name);

BOOL CreateProcessW(const WCHAR *program, WCHAR *line, void *processSecurity, void *threadSecurity, BOOL inherit,
                    DWORD flags, void *environment, const WCHAR *directory, GRT_STARTUPINFO *startup,
                    GRT_PROCESS_INFORMATION *process);
DWORD WaitForSingleObject(HANDLE object, DWORD milliseconds);
BOOL GetExitCodeProcess(HANDLE process, DWORD *code);

HANDLE GetProcessHeap(void);
void *HeapAlloc(HANDLE heap, DWORD flags, unsigned long long size);
void *HeapReAlloc(HANDLE heap, DWORD flags, void *block, unsigned long long size);
BOOL HeapFree(HANDLE heap, DWORD flags, void *block);

WCHAR *GetCommandLineW(void);
WCHAR *GetEnvironmentStringsW(void);
BOOL FreeEnvironmentStringsW(WCHAR *block);
int WideCharToMultiByte(UINT codePage, DWORD flags, const WCHAR *wide, int wideCount, char *bytes, int byteCount,
                        const char *defaultChar, BOOL *usedDefault);
int MultiByteToWideChar(UINT codePage, DWORD flags, const char *bytes, int byteCount, WCHAR *wide, int wideCount);

void GetSystemTimeAsFileTime(GRT_FILETIME *now);
BOOL QueryPerformanceCounter(long long *count);
BOOL QueryPerformanceFrequency(long long *frequency);
BOOL FileTimeToSystemTime(const GRT_FILETIME *file, GRT_SYSTEMTIME *system);
BOOL SystemTimeToFileTime(const GRT_SYSTEMTIME *system, GRT_FILETIME *file);
BOOL SystemTimeToTzSpecificLocalTime(const GRT_TIME_ZONE_INFORMATION *zone, const GRT_SYSTEMTIME *universal,
                                     GRT_SYSTEMTIME *local);
BOOL TzSpecificLocalTimeToSystemTime(const GRT_TIME_ZONE_INFORMATION *zone, const GRT_SYSTEMTIME *local,
                                     GRT_SYSTEMTIME *universal);
DWORD GetTimeZoneInformation(GRT_TIME_ZONE_INFORMATION *zone);

void AcquireSRWLockExclusive(void **lock);
void ReleaseSRWLockExclusive(void **lock);
BOOL SleepConditionVariableSRW(void **condition, void **lock, DWORD milliseconds, DWORD flags);
void WakeAllConditionVariable(void **condition);

void ExitProcess(UINT status);

#ifdef __cplusplus
}
#endif

#endif
