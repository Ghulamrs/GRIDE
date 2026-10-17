#include "indent.h"

namespace editor {

namespace {

bool isIdentChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_';
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

std::string trimmed(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && isSpace(s[a])) ++a;
    size_t b = s.size();
    while (b > a && isSpace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

bool wordAt(const std::string& s, size_t at, const char* word) {
    size_t n = 0;
    while (word[n]) ++n;
    if (at + n > s.size()) return false;
    if (s.compare(at, n, word) != 0) return false;
    if (at > 0 && isIdentChar(s[at - 1])) return false;
    if (at + n < s.size() && isIdentChar(s[at + n])) return false;
    return true;
}

bool startsWithWord(const std::string& s, const char* word) {
    return wordAt(s, 0, word);
}

}

LineFacts examine(const std::string& line, const IndentState& in,
                  IndentDialect dialect) {
    const bool c = dialect == DialectC;

    LineFacts f;
    f.startsInComment = in.comment;
    f.startsContinued = in.continued;

    bool comment = in.comment;
    bool inString = false;
    bool inChar = false;
    bool atHead = true;

    for (size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];

        if (comment) {
            if (ch == '*' && i + 1 < line.size() && line[i + 1] == '/') {
                comment = false;
                ++i;
            }
            continue;
        }
        if (inString || inChar) {

            if (c && ch == '\\' && i + 1 < line.size()) {
                ++i;
                continue;
            }
            if (inString && ch == '"') inString = false;
            if (inChar && ch == '\'') inChar = false;
            continue;
        }

        if (c && ch == '/' && i + 1 < line.size() && line[i + 1] == '*') {
            comment = true;
            ++i;
            continue;
        }
        if (ch == '/' && i + 1 < line.size() && line[i + 1] == '/') break;
        if (ch == '"') {
            inString = true;
            atHead = false;
            f.code += ch;
            continue;
        }

        if (c && ch == '\'') {
            inChar = true;
            atHead = false;
            f.code += ch;
            continue;
        }

        if (ch == '{') {
            ++f.opens;
            atHead = false;
        } else if (ch == '}') {
            ++f.closes;
            if (atHead) ++f.leadingCloses;
            atHead = false;
        } else if (!isSpace(ch)) {
            atHead = false;
        }
        f.code += ch;
    }

    std::string t = trimmed(f.code);
    std::string raw = trimmed(line);

    f.blank = raw.empty();

    f.preprocessor = c && !f.startsInComment && !raw.empty() && raw[0] == '#';

    if (!t.empty()) {
        char last = t[t.size() - 1];

        f.endsStatement = c ? (last == ';' || last == '}' || last == '{') : true;
    }

    f.endsInComment = comment;

    f.endsContinued = c && !raw.empty() && raw[raw.size() - 1] == '\\';

    f.caseLabel = c && (startsWithWord(t, "case") || startsWithWord(t, "default"));

    if (c) {
        for (size_t i = 0; i < t.size(); ++i) {
            if (t[i] == '{') break;
            if (wordAt(t, i, "switch")) f.switchHead = true;
        }
    }

    bool control = startsWithWord(t, "if") || startsWithWord(t, "for") ||
                   startsWithWord(t, "while") || startsWithWord(t, "do") ||
                   startsWithWord(t, "else") || startsWithWord(t, "switch");

    if (c && control && !t.empty()) {
        char last = t[t.size() - 1];

        f.controlHead = (last != '{' && last != ';' && last != '}');
    }

    if (c && !f.caseLabel && !f.preprocessor && !t.empty() && !f.startsInComment) {
        size_t i = 0;
        while (i < t.size() && isIdentChar(t[i])) ++i;
        if (i > 0 && !(t[0] >= '0' && t[0] <= '9')) {
            size_t j = i;
            while (j < t.size() && isSpace(t[j])) ++j;
            if (j < t.size() && t[j] == ':' && (j + 1 >= t.size() || t[j + 1] != ':') &&
                t.find('?') == std::string::npos && !control)
                f.gotoLabel = true;
        }
    }

    return f;
}

IndentState advance(const IndentState& in, const LineFacts& f) {
    IndentState out = in;
    out.comment = f.endsInComment;
    out.continued = f.endsContinued;

    bool sawOpen = false;
    for (size_t i = 0; i < f.code.size(); ++i) {
        if (f.code[i] == '{') {
            out.inSwitch.push_back(f.switchHead && !sawOpen);
            sawOpen = true;
            ++out.depth;
        } else if (f.code[i] == '}') {
            if (!out.inSwitch.empty()) out.inSwitch.pop_back();
            if (out.depth > 0) --out.depth;
        }
    }

    if (f.blank) {

        return out;
    }

    if (f.controlHead) {
        ++out.hanging;
    } else if (f.endsStatement || f.opens > 0 || f.closes > 0 || f.caseLabel ||
               f.gotoLabel || f.preprocessor) {

        out.hanging = 0;
    }

    return out;
}

int levelFor(const IndentState& in, const LineFacts& f, const IndentStyle& style) {
    if (f.startsInComment || f.startsContinued) return kKeep;
    if (f.blank) return 0;
    if (f.preprocessor) return 0;

    bool switchBody = !in.inSwitch.empty() && in.inSwitch.back();

    int level = in.depth;

    if (f.leadingCloses > 0) {

        level -= static_cast<int>(f.leadingCloses);
    } else if (switchBody) {
        level += static_cast<int>(style.caseIndent);
        if (f.caseLabel) level -= 1;
    } else {
        level += in.hanging;
    }

    if (f.gotoLabel) level -= 1;

    return level < 0 ? 0 : level;
}

std::string indentString(int level, const IndentStyle& style) {
    if (level <= 0) return std::string();
    if (style.tabs) return std::string(static_cast<size_t>(level), '\t');
    return std::string(static_cast<size_t>(level) * style.width, ' ');
}

std::string withoutLeadingSpace(const std::string& line) {
    size_t a = 0;
    while (a < line.size() && isSpace(line[a])) ++a;
    return line.substr(a);
}

IndentState stateBefore(const std::vector<std::string>& lines, size_t row,
                        IndentDialect dialect) {
    IndentState state;
    for (size_t i = 0; i < row && i < lines.size(); ++i)
        state = advance(state, examine(lines[i], state, dialect));
    return state;
}

std::string indentFor(const std::vector<std::string>& lines, size_t row,
                      const IndentStyle& style) {
    if (row >= lines.size()) return std::string();
    IndentState before = stateBefore(lines, row, style.dialect);
    LineFacts f = examine(lines[row], before, style.dialect);
    int level = levelFor(before, f, style);
    if (level == kKeep) {
        std::string keep = lines[row];
        size_t a = 0;
        while (a < keep.size() && isSpace(keep[a])) ++a;
        return keep.substr(0, a);
    }
    return indentString(level, style);
}

std::vector<std::string> reindent(const std::vector<std::string>& lines,
                                  const IndentStyle& style) {
    std::vector<std::string> out;
    out.reserve(lines.size());

    IndentState state;
    for (size_t i = 0; i < lines.size(); ++i) {
        LineFacts f = examine(lines[i], state, style.dialect);
        int level = levelFor(state, f, style);

        if (level == kKeep) {
            out.push_back(lines[i]);
        } else if (f.blank) {

            out.push_back(std::string());
        } else {
            out.push_back(indentString(level, style) + withoutLeadingSpace(lines[i]));
        }

        state = advance(state, f);
    }

    return out;
}

std::string indentAfterNewline(const std::vector<std::string>& lines, size_t row,
                               size_t col, const IndentStyle& style) {
    if (row >= lines.size()) return std::string();

    std::vector<std::string> upto(lines.begin(), lines.begin() + static_cast<long>(row));
    std::string head = lines[row].substr(0, col > lines[row].size() ? lines[row].size() : col);
    upto.push_back(head);

    IndentState before = stateBefore(upto, upto.size() - 1, style.dialect);
    IndentState after = advance(before, examine(head, before, style.dialect));

    std::string tail = lines[row].substr(col > lines[row].size() ? lines[row].size() : col);
    std::string rest = withoutLeadingSpace(tail);

    LineFacts next = examine(rest, after, style.dialect);

    if (rest.empty()) next.blank = false;
    int level = levelFor(after, next, style);
    if (level == kKeep) return std::string();
    return indentString(level, style);
}

}
