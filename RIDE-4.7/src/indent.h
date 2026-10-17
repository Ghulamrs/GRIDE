#ifndef EDITOR_INDENT_H
#define EDITOR_INDENT_H

#include <cstddef>
#include <string>
#include <vector>

namespace editor {

enum IndentDialect {
    DialectC = 0,
    DialectShalimar
};

struct IndentStyle {
    size_t width = 4;
    bool tabs = false;

    size_t caseIndent = 0;
    IndentDialect dialect = DialectC;
};

struct IndentState {
    int depth = 0;
    bool comment = false;
    bool continued = false;
    int hanging = 0;
    std::vector<bool> inSwitch;
};

struct LineFacts {
    bool blank = false;
    bool preprocessor = false;
    bool caseLabel = false;
    bool gotoLabel = false;
    bool controlHead = false;
    bool switchHead = false;
    bool startsInComment = false;
    bool startsContinued = false;
    bool endsInComment = false;
    bool endsContinued = false;
    size_t leadingCloses = 0;
    int opens = 0;
    int closes = 0;
    bool endsStatement = false;
    std::string code;
};

LineFacts examine(const std::string& line, const IndentState& in,
                  IndentDialect dialect = DialectC);
IndentState advance(const IndentState& in, const LineFacts& f);

const int kKeep = -1;
int levelFor(const IndentState& in, const LineFacts& f, const IndentStyle& style);

std::string indentString(int level, const IndentStyle& style);
std::string withoutLeadingSpace(const std::string& line);

IndentState stateBefore(const std::vector<std::string>& lines, size_t row,
                        IndentDialect dialect = DialectC);

std::string indentFor(const std::vector<std::string>& lines, size_t row,
                      const IndentStyle& style);

std::vector<std::string> reindent(const std::vector<std::string>& lines,
                                  const IndentStyle& style);

std::string indentAfterNewline(const std::vector<std::string>& lines, size_t row,
                               size_t col, const IndentStyle& style);

}

#endif
