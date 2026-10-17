#ifndef EDITOR_PATH_H
#define EDITOR_PATH_H

#include <string>
#include <vector>

namespace editor {

namespace path {

std::string withSlashes(const std::string& path);

std::string absolute(const std::string& path);

std::string relativeTo(const std::string& path, const std::string& base);

std::string oneName(const std::string& path);

bool same(const std::string& one, const std::string& other);

std::string parent(const std::string& path);
std::string filename(const std::string& path);
std::string join(const std::string& directory, const std::string& leaf);

bool exists(const std::string& path);
bool isDirectory(const std::string& path);

bool makeDirectories(const std::string& path);

bool rename(const std::string& from, const std::string& to);

bool remove(const std::string& path);

bool removeTree(const std::string& path);

std::string tempDir();

std::string programDirectory();
// The running program's own file, by its full name; empty where the system will not say.
std::string programFile();

std::string besideProgram(const std::string& name);

// A bare program name as PATH finds it, absolute; never the current directory. Empty if not found.
std::string onPath(const std::string& name);

std::string homeDir();

struct Entry {
    std::string name;
    bool directory;

    Entry() : directory(false) {}
};

std::vector<Entry> entries(const std::string& directory, bool* ok = 0);

}
}

#endif
