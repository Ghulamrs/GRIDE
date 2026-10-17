#pragma once

// The window's reach into the processes it started, below the bridge. Plain Win32 in the managed
// unit - no template of ours is instantiated, so it is not the hazard bridge.cpp stays native for.

#include <windows.h>
#include <tlhelp32.h>

#include <cwchar>

namespace ridegui {

// A process is ours only if it started after this one: a parent id can be reused once its owner is gone.
inline bool StartedAfter(HANDLE process, const FILETIME& ours) {
    FILETIME made, gone, kernel, user;
    if (!GetProcessTimes(process, &made, &gone, &kernel, &user)) return false;
    return CompareFileTime(&made, &ours) >= 0;
}

// Ends every process this one started, and theirs, but the hidden console's conhost; how many.
// Breadth first, a snapshot per generation, so one started while this runs is caught next pass.
inline int StopChildren() {
    FILETIME ours, gone, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &ours, &gone, &kernel, &user)) return 0;

    DWORD parents[256];
    int known = 0;
    parents[known++] = GetCurrentProcessId();
    int stopped = 0;

    for (int from = 0; from < known;) {
        HANDLE all = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (all == INVALID_HANDLE_VALUE) break;
        int upTo = known;
        PROCESSENTRY32W entry;
        entry.dwSize = sizeof entry;
        for (BOOL more = Process32FirstW(all, &entry); more; more = Process32NextW(all, &entry)) {
            bool below = false;
            for (int i = from; i < upTo; ++i)
                if (entry.th32ParentProcessID == parents[i]) below = true;
            if (!below || _wcsicmp(entry.szExeFile, L"conhost.exe") == 0) continue;

            HANDLE child = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                       entry.th32ProcessID);
            if (child == NULL) continue;
            if (StartedAfter(child, ours)) {
                if (known < 256) parents[known++] = entry.th32ProcessID;
                if (TerminateProcess(child, 1)) ++stopped;
            }
            CloseHandle(child);
        }
        CloseHandle(all);
        from = upTo;
    }
    return stopped;
}

}
