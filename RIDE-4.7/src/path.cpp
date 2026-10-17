#include "path.h"

#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace editor {
namespace path {

namespace {

struct Split {
    std::string root;
    std::vector<std::string> parts;
};

bool isLetter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

std::string rootOf(const std::string& path) {
    if (path.size() >= 2 && path[0] == '/' && path[1] == '/') return "//";
    if (path.size() >= 2 && isLetter(path[0]) && path[1] == ':') return path.substr(0, 2) + "/";
    if (!path.empty() && path[0] == '/') return "/";
    return std::string();
}

Split split(const std::string& path) {
    Split out;
    out.root = rootOf(path);

    size_t at = out.root.size();
    if (out.root.size() == 3) at = 3;
    else if (out.root == "//") at = 2;
    else if (out.root == "/") at = 1;

    std::string name;
    for (; at <= path.size(); ++at) {
        char c = (at < path.size()) ? path[at] : '/';
        if (c != '/') { name += c; continue; }

        if (name.empty() || name == ".") { name.clear(); continue; }
        if (name == "..") {
            if (!out.parts.empty() && out.parts.back() != "..") out.parts.pop_back();
            else if (out.root.empty()) out.parts.push_back("..");
            name.clear();
            continue;
        }
        out.parts.push_back(name);
        name.clear();
    }
    return out;
}

std::string joined(const Split& s) {
    std::string out = s.root;
    for (size_t i = 0; i < s.parts.size(); ++i) {
        if (i) out += "/";
        out += s.parts[i];
    }

    if (!s.parts.empty() && !out.empty() && s.root.size() > 1 &&
        out.compare(0, s.root.size(), s.root) == 0) {
        return out;
    }
    return out.empty() ? std::string(".") : out;
}

std::string currentDirectory() {
    char buffer[4096];
#ifdef _WIN32
    DWORD got = GetCurrentDirectoryA(sizeof buffer, buffer);
    if (got == 0 || got >= sizeof buffer) return std::string(".");
    return withSlashes(buffer);
#else
    if (!getcwd(buffer, sizeof buffer)) return std::string(".");
    return withSlashes(buffer);
#endif
}

#ifndef _WIN32
bool statOf(const std::string& path, struct stat& out, bool followLinks) {
    return (followLinks ? ::stat(path.c_str(), &out) : ::lstat(path.c_str(), &out)) == 0;
}
#endif

bool makeOneDirectory(const std::string& path) {
#ifdef _WIN32
    return _mkdir(path.c_str()) == 0 || isDirectory(path);
#else
    return ::mkdir(path.c_str(), 0777) == 0 || isDirectory(path);
#endif
}

bool removeOneDirectory(const std::string& path) {
#ifdef _WIN32
    return _rmdir(path.c_str()) == 0;
#else
    return ::rmdir(path.c_str()) == 0;
#endif
}

bool isLink(const std::string& path) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    struct stat info;
    return statOf(path, info, false) && S_ISLNK(info.st_mode);
#endif
}

}

std::string withSlashes(const std::string& path) {
    std::string out = path;
    for (size_t i = 0; i < out.size(); ++i)
        if (out[i] == '\\') out[i] = '/';
    return out;
}

std::string absolute(const std::string& path) {
    if (path.empty()) return currentDirectory();

    std::string here = withSlashes(path);
    if (rootOf(here).empty()) here = join(currentDirectory(), here);

    std::string out = joined(split(here));
    return out == "." ? withSlashes(path) : out;
}

namespace {

// A path with its links resolved, where it exists; as given otherwise. On a Mac /tmp is a link to
// /private/tmp and the window spells a file one way while the project was opened the other, so a
// project's own file read as outside it: a comparison that fails as written is tried again this way.
std::string resolvedPath(const std::string& path) {
#ifdef _WIN32
    return absolute(path);
#else
    char buffer[PATH_MAX];
    if (::realpath(absolute(path).c_str(), buffer) == 0) return absolute(path);
    return withSlashes(buffer);
#endif
}

std::string relativeAsWritten(const std::string& path, const std::string& base) {
    Split here = split(absolute(path));
    Split from = split(absolute(base));
    if (here.root != from.root) return std::string();

    size_t same = 0;
    while (same < here.parts.size() && same < from.parts.size() &&
           here.parts[same] == from.parts[same])
        ++same;

    std::string out;
    for (size_t i = same; i < from.parts.size(); ++i) out += out.empty() ? ".." : "/..";
    for (size_t i = same; i < here.parts.size(); ++i) {
        if (!out.empty()) out += "/";
        out += here.parts[i];
    }
    return out.empty() ? std::string(".") : out;
}

}

std::string relativeTo(const std::string& path, const std::string& base) {
    std::string out = relativeAsWritten(path, base);
    if (!out.empty() && out.compare(0, 2, "..") != 0) return out;
    std::string resolved = relativeAsWritten(resolvedPath(path), resolvedPath(base));
    return (!resolved.empty() && resolved.compare(0, 2, "..") != 0) ? resolved : out;
}

std::string oneName(const std::string& path) {
    std::string name = absolute(path);
#ifdef _WIN32
    for (size_t i = 0; i < name.size(); ++i)
        name[i] = static_cast<char>(::tolower(static_cast<unsigned char>(name[i])));
#endif
    return name;
}

bool same(const std::string& one, const std::string& other) {
    if (oneName(one) == oneName(other)) return true;
    return oneName(resolvedPath(one)) == oneName(resolvedPath(other));
}

std::string parent(const std::string& path) {
    std::string here = withSlashes(path);
    while (here.size() > 1 && here[here.size() - 1] == '/') here.resize(here.size() - 1);

    size_t slash = here.rfind('/');
    if (slash == std::string::npos) return std::string();
    if (slash == 0) return "/";
    return here.substr(0, slash);
}

std::string filename(const std::string& path) {
    std::string here = withSlashes(path);
    size_t slash = here.rfind('/');
    return (slash == std::string::npos) ? here : here.substr(slash + 1);
}

std::string join(const std::string& directory, const std::string& leaf) {
    if (directory.empty()) return withSlashes(leaf);
    if (leaf.empty()) return withSlashes(directory);

    std::string out = withSlashes(directory);
    if (out[out.size() - 1] != '/') out += "/";
    return out + withSlashes(leaf);
}

bool exists(const std::string& path) {
#ifdef _WIN32
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat info;
    return statOf(path, info, true);
#endif
}

bool isDirectory(const std::string& path) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat info;
    return statOf(path, info, true) && S_ISDIR(info.st_mode);
#endif
}

bool makeDirectories(const std::string& path) {
    if (path.empty()) return false;

    Split here = split(absolute(path));
    std::string so_far = here.root.empty() ? std::string() : here.root;
    for (size_t i = 0; i < here.parts.size(); ++i) {
        so_far = so_far.empty() ? here.parts[i] : join(so_far, here.parts[i]);
        if (!isDirectory(so_far) && !makeOneDirectory(so_far)) return false;
    }
    return isDirectory(absolute(path));
}

bool rename(const std::string& from, const std::string& to) {
    return std::rename(from.c_str(), to.c_str()) == 0;
}

bool remove(const std::string& path) {
    if (isDirectory(path)) return removeOneDirectory(path);
    return std::remove(path.c_str()) == 0;
}

bool removeTree(const std::string& path) {
    if (path.empty()) return false;

    Split here = split(absolute(path));
    if (here.parts.empty()) return false;

    if (!exists(path)) return true;

    if (isDirectory(path) && !isLink(path)) {
        bool ok = true;
        std::vector<Entry> inside = entries(path);
        for (size_t i = 0; i < inside.size(); ++i)
            if (!removeTree(join(path, inside[i].name))) ok = false;
        return removeOneDirectory(path) && ok;
    }
    return std::remove(path.c_str()) == 0;
}

std::string tempDir() {
#ifdef _WIN32
    const char* named = std::getenv("TEMP");
    if (!named) named = std::getenv("TMP");
    std::string out = named ? named : ".";
#else
    const char* named = std::getenv("TMPDIR");
    std::string out = named ? named : "/tmp";
#endif
    out = withSlashes(out);
    while (out.size() > 1 && out[out.size() - 1] == '/') out.resize(out.size() - 1);
    return out;
}

std::string homeDir() {
#ifdef _WIN32
    const char* named = std::getenv("USERPROFILE");
    if (!named) named = std::getenv("HOMEPATH");
#else
    const char* named = std::getenv("HOME");
#endif
    if (!named || !*named) return std::string();

    std::string out = withSlashes(named);
    while (out.size() > 1 && out[out.size() - 1] == '/') out.resize(out.size() - 1);
    return out;
}

std::string programFile() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    DWORD wrote = GetModuleFileNameA(NULL, buffer, sizeof buffer);
    if (wrote == 0 || wrote >= sizeof buffer) return std::string();
    return withSlashes(std::string(buffer, wrote));
#elif defined(__APPLE__)
    char buffer[4096];
    uint32_t room = sizeof buffer;
    if (_NSGetExecutablePath(buffer, &room) != 0) return std::string();
    return absolute(withSlashes(std::string(buffer)));
#else
    char buffer[4096];
    ssize_t wrote = readlink("/proc/self/exe", buffer, sizeof buffer - 1);
    if (wrote <= 0) return std::string();
    buffer[wrote] = 0;
    return withSlashes(std::string(buffer));
#endif
}

std::string programDirectory() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    DWORD wrote = GetModuleFileNameA(NULL, buffer, sizeof buffer);
    if (wrote == 0 || wrote >= sizeof buffer) return std::string();
    return parent(withSlashes(std::string(buffer, wrote)));
#elif defined(__APPLE__)
    char buffer[4096];
    uint32_t room = sizeof buffer;
    if (_NSGetExecutablePath(buffer, &room) != 0) return std::string();

    return parent(absolute(withSlashes(std::string(buffer))));
#else
    char buffer[4096];
    ssize_t wrote = readlink("/proc/self/exe", buffer, sizeof buffer - 1);
    if (wrote <= 0) return std::string();
    buffer[wrote] = 0;
    return parent(withSlashes(std::string(buffer)));
#endif
}

std::string besideProgram(const std::string& name) {
    if (name.empty()) return std::string();

    std::string where = programDirectory();
    if (where.empty()) return std::string();

    // **The name as asked for first, and only then with `.exe`.** The programs are cc1.exe and so
    // on everywhere now, so appending on Windows turned "cc1.exe" into a search for cc1.exe.exe;
    // About reported all three compilers absent while standing beside them. The fallback stays for a bare name, which a caller that never thought about Windows writes.
    std::string full = join(where, name);
    if (exists(full) && !isDirectory(full)) return full;

#ifdef _WIN32
    full = join(where, name + ".exe");
    if (exists(full) && !isDirectory(full)) return full;
#endif

    return std::string();
}

std::string onPath(const std::string& name) {
    if (name.empty() || name.find_first_of("/\\") != std::string::npos) return std::string();
    const char* all = std::getenv("PATH");
    if (!all) return std::string();
#ifdef _WIN32
    const char split = ';';
#else
    const char split = ':';
#endif
    std::string list = all;
    size_t at = 0;
    while (at <= list.size()) {
        size_t cut = list.find(split, at);
        if (cut == std::string::npos) cut = list.size();
        std::string dir = list.substr(at, cut - at);
        at = cut + 1;
        // An empty entry, or ".", is the current directory: a tool is never found there.
        if (dir.empty() || dir == ".") continue;
        std::string full = join(dir, name);
        if (exists(full) && !isDirectory(full)) return full;
#ifdef _WIN32
        full = join(dir, name + ".exe");
        if (exists(full) && !isDirectory(full)) return full;
#endif
    }
    return std::string();
}

std::vector<Entry> entries(const std::string& directory, bool* ok) {
    std::vector<Entry> found;
    if (ok) *ok = false;

#ifdef _WIN32
    WIN32_FIND_DATAA data;
    HANDLE search = FindFirstFileA(join(directory, "*").c_str(), &data);
    if (search == INVALID_HANDLE_VALUE) return found;
    if (ok) *ok = true;

    do {
        Entry entry;
        entry.name = data.cFileName;
        if (entry.name == "." || entry.name == "..") continue;
        entry.directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        found.push_back(entry);
    } while (FindNextFileA(search, &data));
    FindClose(search);
#else
    DIR* open = opendir(directory.c_str());
    if (!open) return found;
    if (ok) *ok = true;

    while (struct dirent* it = readdir(open)) {
        Entry entry;
        entry.name = it->d_name;
        if (entry.name == "." || entry.name == "..") continue;
        entry.directory = isDirectory(join(directory, entry.name));
        found.push_back(entry);
    }
    closedir(open);
#endif
    return found;
}

}
}
