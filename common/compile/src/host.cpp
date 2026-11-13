// host.cpp - this machine: the only file of the block that calls the operating system. The planner never
// includes it; it reaches files and the environment through the two interfaces these objects implement.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>

#include "../gride_compile.h"
#include "text.h"

namespace gride { namespace compile {

namespace {

std::wstring wide(const std::string &s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), 0, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::string narrow(const std::wstring &w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), 0, 0, 0, 0);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, 0, 0);
    return s;
}

void makeFolders(const std::string &folder) {
    if (folder.empty()) return;
    DWORD a = GetFileAttributesW(wide(folder).c_str());
    if (a != INVALID_FILE_ATTRIBUTES) return;
    makeFolders(parent(folder));
    CreateDirectoryW(wide(folder).c_str(), 0);
}

struct SystemFiles : FileSystem {
    bool isFile(const std::string &path) {
        DWORD a = GetFileAttributesW(wide(path).c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    }
    bool isDirectory(const std::string &path) {
        DWORD a = GetFileAttributesW(wide(path).c_str());
        return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
    }
    bool read(const std::string &path, std::string &bytes) {
        FILE *in = _wfopen(wide(path).c_str(), L"rb");
        if (!in) return false;
        bytes.clear();
        char buffer[65536];
        for (size_t n; (n = fread(buffer, 1, sizeof buffer, in)) > 0;) bytes.append(buffer, n);
        fclose(in);
        return true;
    }
    bool write(const std::string &path, const std::string &bytes) {
        makeFolders(parent(path));
        FILE *out = _wfopen(wide(path).c_str(), L"wb");
        if (!out) return false;
        bool ok = bytes.empty() || fwrite(bytes.data(), 1, bytes.size(), out) == bytes.size();
        return fclose(out) == 0 && ok;
    }
    long long modified(const std::string &path) {
        WIN32_FILE_ATTRIBUTE_DATA d;
        if (!GetFileAttributesExW(wide(path).c_str(), GetFileExInfoStandard, &d)) return 0;
        return (static_cast<long long>(d.ftLastWriteTime.dwHighDateTime) << 32) | d.ftLastWriteTime.dwLowDateTime;
    }
    std::vector<std::string> list(const std::string &folder) {
        std::vector<std::string> r;
        WIN32_FIND_DATAW f;
        HANDLE h = FindFirstFileW(wide(folder + "\\*").c_str(), &f);
        if (h == INVALID_HANDLE_VALUE) return r;
        do {
            std::string name = narrow(f.cFileName);
            if (name == "." || name == "..") continue;
            r.push_back(f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? name + "\\" : name);
        } while (FindNextFileW(h, &f));
        FindClose(h);
        return r;
    }
};

struct SystemEnvironment : Environment {
    std::string variable(const std::string &name) {
        std::wstring n = wide(name);
        DWORD size = GetEnvironmentVariableW(n.c_str(), 0, 0);
        if (size == 0) return std::string();
        std::wstring value(size, L'\0');
        DWORD got = GetEnvironmentVariableW(n.c_str(), &value[0], size);
        value.resize(got);
        return narrow(value);
    }
    std::string registry(const std::string &key, const std::string &value) {
        wchar_t buffer[1024];
        DWORD size = sizeof buffer;
        if (RegGetValueW(HKEY_LOCAL_MACHINE, wide(key).c_str(), wide(value).c_str(), RRF_RT_REG_SZ, 0, buffer, &size) != ERROR_SUCCESS)
            return std::string();
        return narrow(buffer);
    }
};

}

// neither has a member, so neither has anything to destroy when the DLL is unloaded
FileSystem &systemFileSystem() {
    static SystemFiles *files = new SystemFiles();
    return *files;
}

Environment &systemEnvironment() {
    static SystemEnvironment *environment = new SystemEnvironment();
    return *environment;
}

Paths defaultPaths(const std::string &programFolder, const std::string &outlook) {
    Paths p;
    p.programFolder = programFolder;
    if (p.programFolder.empty()) {
        wchar_t file[MAX_PATH * 4];
        DWORD n = GetModuleFileNameW(0, file, MAX_PATH * 4);
        p.programFolder = parent(narrow(std::wstring(file, n)));
    }
    p.programFolder = tidy(p.programFolder);
    p.outlook = outlook;
    p.shippedToolchain = combine(p.programFolder, "toolchain.json");
    std::string appData = systemEnvironment().variable("APPDATA");
    if (!appData.empty()) {
        p.userToolchain = combine(appData, "GRIDE\\toolchain.json");
        p.settings = combine(appData, "GRIDE\\settings.json");
    }
    return p;
}

}}
