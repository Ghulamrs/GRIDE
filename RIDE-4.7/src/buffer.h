#ifndef EDITOR_BUFFER_H
#define EDITOR_BUFFER_H

#include <cstddef>
#include <string>
#include <vector>

namespace editor {

struct Range {
    size_t fromRow = 0;
    size_t fromCol = 0;
    size_t toRow = 0;
    size_t toCol = 0;

    bool empty() const { return fromRow == toRow && fromCol == toCol; }
};

Range ordered(size_t rowA, size_t colA, size_t rowB, size_t colB);

enum EditKind {
    EditNone = 0,
    EditTyping,
    EditErasing,
    EditOther
};

class Buffer {
public:
    enum LoadResult {
        Opened,
        NewFile,
        Failed
    };

    Buffer();

    LoadResult load(const std::string& path, std::string& error);
    bool save(std::string& error);

    size_t lineCount() const { return lines_.size(); }
    const std::string& line(size_t row) const { return lines_[row]; }
    const std::vector<std::string>& lines() const { return lines_; }

    void replaceLine(size_t row, const std::string& text);
    void replaceAll(const std::vector<std::string>& lines);

    const std::string& path() const { return path_; }
    void setPath(const std::string& path) { path_ = path; }

    bool dirty() const { return dirty_; }

    void beginEdit(EditKind kind, size_t cx, size_t cy);

    void breakRun() { lastKind_ = EditNone; }

    bool undo(size_t& cx, size_t& cy);
    bool redo(size_t& cx, size_t& cy);
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    size_t undoDepth() const { return undo_.size(); }

    std::string textIn(const Range& range) const;
    void eraseRange(const Range& range);
    void insertText(size_t row, size_t col, const std::string& text,
                    size_t& endRow, size_t& endCol);

    void insertChar(size_t row, size_t col, char c);
    void eraseChar(size_t row, size_t col);
    void splitLine(size_t row, size_t col);
    void joinLine(size_t row);

private:

    struct Snapshot {
        std::vector<std::string> lines;
        size_t cx;
        size_t cy;
    };

    static const size_t kMaxSteps = 100;

    std::vector<std::string> lines_;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    EditKind lastKind_;

    long savedAt_;
    std::string path_;
    bool dirty_;

    bool finalNewline_;
};

}

#endif
