#include "tree.h"

#include <algorithm>

#include "path.h"

namespace editor {

namespace {

bool skip(const std::string& name) {
    if (name.empty()) return true;
    if (name[0] == '.') return true;
    return name == "obj" || name == "build" || name == "x64" || name == "Debug" ||
           name == "Release" || name == "node_modules";
}

}

Tree::Tree() : showing_(ShowingDirectory) {}

void Tree::setRoot(const std::string& where) {
    root_ = path::absolute(where);
    showing_ = ShowingDirectory;
    opened_.clear();
    reread();
}

void Tree::showProject(const Project& project) {
    showing_ = ShowingProject;
    root_ = project.root();
    error_.clear();
    entries_.clear();

    for (size_t i = 0; i < project.groups().size(); ++i) {
        const Group& group = project.groups()[i];

        TreeEntry head;
        head.name = group.name;

        head.path = "group:" + group.name;
        head.directory = true;
        head.group = true;
        head.depth = 0;
        head.open = opened_.count(head.path) > 0 || opened_.empty();
        entries_.push_back(head);

        if (!head.open) continue;

        for (size_t j = 0; j < group.files.size(); ++j) {
            TreeEntry file;
            file.name = path::filename(group.files[j]);
            file.path = project.absolute(group.files[j]);
            file.depth = 1;
            entries_.push_back(file);
        }
    }
}

void Tree::showOpenFiles(const std::vector<std::string>& paths) {
    showing_ = ShowingOpenFiles;
    error_.clear();
    entries_.clear();

    for (size_t i = 0; i < paths.size(); ++i) {
        TreeEntry file;
        file.path = paths[i];

        file.name = paths[i].empty() ? std::string("untitled") : path::filename(paths[i]);
        file.depth = 0;
        entries_.push_back(file);
    }
}

void Tree::clear() {
    showing_ = ShowingNothing;
    error_.clear();
    entries_.clear();
}

void Tree::reread() {

    if (showing_ != ShowingDirectory) return;
    entries_.clear();
    error_.clear();
    if (root_.empty()) return;
    gather(root_, 0);
}

void Tree::gather(const std::string& dir, int depth) {
    bool readable = false;
    std::vector<path::Entry> found = path::entries(dir, &readable);
    if (!readable) {
        if (depth == 0) error_ = "cannot read " + dir;
        return;
    }

    std::sort(found.begin(), found.end(),
              [](const path::Entry& a, const path::Entry& b) {
                  if (a.directory != b.directory) return a.directory;
                  return a.name < b.name;
              });

    for (size_t i = 0; i < found.size(); ++i) {
        std::string name = found[i].name;
        if (skip(name)) continue;

        bool isDir = found[i].directory;

        TreeEntry entry;
        entry.path = path::join(dir, name);
        entry.name = name;
        entry.directory = isDir;
        entry.depth = depth;
        entry.open = isDir && opened_.count(entry.path) > 0;
        entries_.push_back(entry);

        if (entry.open) gather(entry.path, depth + 1);
    }
}

void Tree::toggle(size_t index) {
    if (index >= entries_.size()) return;
    TreeEntry entry = entries_[index];
    if (!entry.directory) return;

    if (entry.group) {

        if (opened_.empty())
            for (size_t i = 0; i < entries_.size(); ++i)
                if (entries_[i].group) opened_.insert(entries_[i].path);
        if (opened_.count(entry.path))
            opened_.erase(entry.path);
        else
            opened_.insert(entry.path);
        return;
    }

    if (opened_.count(entry.path))
        opened_.erase(entry.path);
    else
        opened_.insert(entry.path);
    reread();
}

size_t Tree::find(const std::string& where) const {
    std::string want = path::absolute(where);
    for (size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].path == want) return i;
    return entries_.size();
}

}
