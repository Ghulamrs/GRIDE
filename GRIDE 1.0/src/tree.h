#ifndef EDITOR_TREE_H
#define EDITOR_TREE_H

#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "project.h"

namespace editor {

struct TreeEntry {
    std::string path;
    std::string name;
    bool directory = false;
    bool group = false;

    bool session = false;
    bool open = false;
    int depth = 0;
};

class Tree {
public:
    Tree();

    void setRoot(const std::string& path);

    void showProject(const Project& project);

    void showOpenFiles(const std::vector<std::string>& paths);

    void clear();

    const std::string& root() const { return root_; }
    const std::string& error() const { return error_; }

    const std::vector<TreeEntry>& entries() const { return entries_; }
    size_t size() const { return entries_.size(); }

    void toggle(size_t index);
    void reread();

    size_t find(const std::string& path) const;

private:
    void gather(const std::string& dir, int depth);

    enum Showing { ShowingDirectory, ShowingProject, ShowingOpenFiles, ShowingNothing };
    Showing showing_;

    std::string root_;
    std::string error_;
    std::vector<TreeEntry> entries_;
    std::set<std::string> opened_;
};

}

#endif
