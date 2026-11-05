#ifndef EDITOR_PATH_H
#define EDITOR_PATH_H

#include <string>
#include <utility>
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

// **The names a project's lists, the shared lists and the defines may use**: $(NAME), from the .pro's
// "paths", settings.json's "paths", then GRIDE's own (the bridge's namesFor), a name in a value filled in
// too, and %VAR% after them. Names match without regard to case, and the first added stands.
class Names {
public:
    void add(const std::string& name, const std::string& value);
    // A folder: its value ends in a separator, so $(NAME)file reads as it does in Visual Studio.
    void addFolder(const std::string& name, const std::string& dir);
    std::string expand(const std::string& text) const;
    std::vector<std::string> expandAll(const std::vector<std::string>& list) const;
    // The first name nothing knows and the entry it is in; empty while every one was known.
    const std::string& unknown() const { return unknown_; }
    const std::string& unknownIn() const { return unknownIn_; }

private:
    const std::string* find(const std::string& name) const;
    std::string fill(const std::string& text, const std::string& entry, int depth) const;

    std::vector<std::pair<std::string, std::string> > table_;
    mutable std::string unknown_;
    mutable std::string unknownIn_;
};

}
}

#endif
