#ifndef EDITOR_FIND_H
#define EDITOR_FIND_H

#include <cstddef>
#include <atomic>
#include <string>
#include <vector>

namespace editor {

struct Match {
    bool found = false;
    size_t row = 0;
    size_t col = 0;
};

Match findNext(const std::vector<std::string>& lines, const std::string& needle,
               size_t row, size_t col);

Match findPrevious(const std::vector<std::string>& lines, const std::string& needle,
                   size_t row, size_t col);

size_t replaceAll(std::vector<std::string>& lines, const std::string& needle,
                  const std::string& with);

// Edit > Find in Files: a text in every file under a folder whose name fits one of the patterns ("*.cpp;*.h",
// empty for all), or - namesOnly - the files whose names hold it. Build folders and binary files are passed over.
struct FileHit {
    std::string file;   // its full name
    size_t line = 0;    // 1-based; 0 for a name found
    size_t col = 0;     // 1-based
    std::string text;   // the line it is on
};

struct FindInFiles {
    std::string text;
    std::string folder;
    std::string patterns;
    bool matchCase = false;
    bool wholeWord = false;
    bool subfolders = true;
    bool namesOnly = false;
    const std::atomic<bool>* stop = nullptr;   // set from another thread, the walk ends at the next file
};

// The hits, at most `limit` of them; `files` says how many files were searched, `cut` whether the limit was met.
std::vector<FileHit> findInFiles(const FindInFiles& query, size_t limit, size_t& files, bool& cut);

}

#endif
