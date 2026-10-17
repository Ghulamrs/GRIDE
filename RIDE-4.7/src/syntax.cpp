#include "syntax.h"

#include <cstring>

namespace editor {

namespace {

bool identChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_';
}

bool digit(char c) { return c >= '0' && c <= '9'; }

const char* const kCKeywords[] = {
    "auto", "break", "case", "continue", "default", "do", "else", "enum", "extern",
    "for", "goto", "if", "inline", "register", "restrict", "return", "sizeof",
    "static", "struct", "switch", "typedef", "union", "volatile", "while",
    "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic",
    "_Noreturn", "_Static_assert", "_Thread_local", 0};

const char* const kCTypes[] = {
    "char", "const", "double", "float", "int", "long", "short", "signed",
    "unsigned", "void", "size_t", "ptrdiff_t", "wchar_t", "FILE", 0};

const char* const kCppKeywords[] = {
    "alignas", "alignof", "and", "and_eq", "asm", "bitand", "bitor", "catch",
    "class", "compl", "constexpr", "const_cast", "decltype", "delete",
    "dynamic_cast", "explicit", "export", "false", "final", "friend", "mutable",
    "namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or",
    "or_eq", "override", "private", "protected", "public", "reinterpret_cast",
    "static_assert", "static_cast", "template", "this", "thread_local", "throw",
    "true", "try", "typeid", "typename", "using", "virtual", "xor", "xor_eq", 0};

const char* const kCppTypes[] = {
    "auto", "bool", "char16_t", "char32_t", "nullptr_t",
    "std", "string", "wstring", "vector", "array", "deque", "list", "forward_list",
    "map", "multimap", "set", "multiset", "unordered_map", "unordered_set",
    "pair", "tuple", "bitset", "initializer_list",
    "unique_ptr", "shared_ptr", "weak_ptr",
    "istream", "ostream", "iostream", "ifstream", "ofstream", "fstream",
    "stringstream", "istringstream", "ostringstream",
    "exception", "runtime_error", "logic_error", "bad_alloc",
    "function", "thread", "mutex", "atomic", "chrono", 0};

// No `elseif`: the language dropped it and did not keep it reserved, so it is an ordinary
// identifier now and colouring it as a keyword would be the exact lie this list exists to
// avoid. A branch is `else` then `if`, and both are already here.
const char* const kShalimarKeywords[] = {
    "if", "else", "while", "for", "to", "step", "fun", "return",
    "break", "continue", "uses", 0};

const char* const kShalimarTypes[] = {
    "int", "real", "char", 0};

const char* const kShalimarBuiltins[] = {
    "abs", "sqrt", "log", "exp", "hypot", "sin", "cos", "tan", "asin", "acos",
    "atan", "atan2", "pow", "round", "ceil", "floor", "trunc", "max", "min",
    "len", 0};

const char* const kShalimarConstants[] = { "pi", "e", 0 };

bool inList(const char* const* list, const std::string& word) {
    for (size_t i = 0; list[i]; ++i)
        if (word == list[i]) return true;
    return false;
}

bool endsWith(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    if (s.size() < n) return false;

    for (size_t i = 0; i < n; ++i) {
        char a = s[s.size() - n + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

void markAsm(const std::string& line, std::vector<unsigned char>& kind) {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;

    for (; i < line.size(); ++i) {
        char c = line[i];

        if (c == ';' || c == '#' || (c == '/' && i + 1 < line.size() && line[i + 1] == '/')) {
            for (size_t j = i; j < line.size(); ++j) kind[j] = KindComment;
            return;
        }

        if (c == '"') {
            size_t j = i;
            kind[j++] = KindString;
            while (j < line.size()) {
                kind[j] = KindString;
                if (line[j] == '\\' && j + 1 < line.size()) { kind[++j] = KindString; }
                else if (line[j] == '"') break;
                ++j;
            }
            i = j;
            continue;
        }

        if (c == '.' && (i == 0 || !identChar(line[i - 1]))) {
            size_t j = i + 1;
            while (j < line.size() && identChar(line[j])) ++j;

            size_t k = j;
            while (k < line.size() && (line[k] == '.' || identChar(line[k]))) ++k;
            unsigned char what = (k < line.size() && line[k] == ':') ? KindLabel : KindPreproc;
            for (size_t m = i; m < (what == KindLabel ? k : j); ++m) kind[m] = what;
            i = (what == KindLabel ? k : j) - 1;
            continue;
        }

        if (digit(c)) {
            size_t j = i;
            while (j < line.size() && (identChar(line[j]) || line[j] == '.')) ++j;
            for (size_t m = i; m < j; ++m) kind[m] = KindNumber;
            i = j - 1;
            continue;
        }

        if (identChar(c)) {
            size_t j = i;
            while (j < line.size() && identChar(line[j])) ++j;
            if (j < line.size() && line[j] == ':') {
                for (size_t m = i; m <= j; ++m) kind[m] = KindLabel;
                i = j;
            } else if (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t') {

                bool first = true;
                for (size_t m = 0; m < i; ++m)
                    if (line[m] != ' ' && line[m] != '\t') first = false;
                if (first) for (size_t m = i; m < j; ++m) kind[m] = KindKeyword;
                i = j - 1;
            } else {
                i = j - 1;
            }
            continue;
        }
    }
}

void markJson(const std::string& line, std::vector<unsigned char>& kind) {
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        if (c == '"') {
            size_t j = i + 1;
            for (; j < line.size(); ++j) {
                if (line[j] == '\\') { ++j; continue; }
                if (line[j] == '"') break;
            }

            size_t end = (j < line.size()) ? j : line.size() - 1;

            size_t after = end + 1;
            while (after < line.size() && (line[after] == ' ' || line[after] == '\t')) ++after;
            unsigned char as = (after < line.size() && line[after] == ':') ? KindType : KindString;

            for (size_t k = i; k <= end; ++k) kind[k] = as;
            i = end;
            continue;
        }

        if ((c >= '0' && c <= '9') || (c == '-' && i + 1 < line.size() &&
                                       line[i + 1] >= '0' && line[i + 1] <= '9')) {
            size_t j = i;
            for (; j < line.size(); ++j) {
                char d = line[j];
                if (!((d >= '0' && d <= '9') || d == '-' || d == '+' || d == '.' ||
                      d == 'e' || d == 'E'))
                    break;
            }
            for (size_t k = i; k < j; ++k) kind[k] = KindNumber;
            i = j - 1;
            continue;
        }

        if (c == 't' || c == 'f' || c == 'n') {
            static const char* const words[3] = {"true", "false", "null"};
            for (size_t w = 0; w < 3; ++w) {
                size_t len = std::strlen(words[w]);
                if (line.compare(i, len, words[w]) != 0) continue;
                for (size_t k = i; k < i + len; ++k) kind[k] = KindKeyword;
                i += len - 1;
                break;
            }
        }
    }
}

void markShalimar(const std::string& line, std::vector<unsigned char>& kind) {
    size_t i = 0;

    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i < line.size() && line[i] == '?') {
        kind[i] = KindPreproc;
        if (i + 1 < line.size() && line[i + 1] == '?') kind[++i] = KindPreproc;
        ++i;
    }

    for (; i < line.size(); ++i) {
        char c = line[i];

        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
            for (size_t j = i; j < line.size(); ++j) kind[j] = KindComment;
            return;
        }

        if (c == '"') {
            size_t j = i;
            kind[j] = KindString;
            for (++j; j < line.size(); ++j) {
                kind[j] = KindString;
                if (line[j] == '"') break;
            }
            i = (j < line.size()) ? j : line.size() - 1;
            continue;
        }

        if (digit(c) && (i == 0 || !identChar(line[i - 1]))) {
            size_t j = i;
            while (j < line.size() && (identChar(line[j]) || line[j] == '.')) ++j;
            for (size_t m = i; m < j; ++m) kind[m] = KindNumber;
            i = j - 1;
            continue;
        }

        if (identChar(c) && !digit(c)) {
            const bool afterDot = i > 0 && line[i - 1] == '.';
            size_t j = i;
            while (j < line.size() && identChar(line[j])) ++j;
            std::string word = line.substr(i, j - i);

            unsigned char what = KindNormal;
            if (afterDot) {

                if (word == "row" || word == "col" || word == "dim") what = KindType;
            } else if (inList(kShalimarKeywords, word)) {
                what = KindKeyword;
            } else if (inList(kShalimarTypes, word)) {
                what = KindType;
            } else if (inList(kShalimarConstants, word)) {
                what = KindNumber;
            } else if (inList(kShalimarBuiltins, word)) {
                what = KindType;
            } else if (word == "prec") {

                size_t k = j;
                while (k < line.size() && line[k] == ' ') ++k;
                if (k < line.size() && line[k] == '(') what = KindPreproc;
            }

            if (what != KindNormal)
                for (size_t m = i; m < j; ++m) kind[m] = what;
            i = j - 1;
            continue;
        }
    }
}

}

// .shl is the editor's one spelling of a Shalimar file (decided 2026-08-23: .shm, the phone
// app's, is not taken everywhere a Shalimar file has to go, so the Language menu is what opens
// one of those); the audit of 2026-09-19 weighed reopening that and kept it.
Language sourceLanguageFor(const std::string& path) {
    if (endsWith(path, ".shl")) return LangShalimar;
    if (endsWith(path, ".c")) return LangC;
    if (endsWith(path, ".cpp") || endsWith(path, ".cc") || endsWith(path, ".cxx")) return LangCpp;
    return LangPlain;
}

Language languageFor(const std::string& path) {
    Language source = sourceLanguageFor(path);
    if (source != LangPlain) return source;
    if (endsWith(path, ".h")) return LangC;
    if (endsWith(path, ".hpp") || endsWith(path, ".hh") || endsWith(path, ".hxx") ||
        endsWith(path, ".ipp"))
        return LangCpp;
    if (endsWith(path, ".s") || endsWith(path, ".asm")) return LangAsm;

    if (endsWith(path, ".json") || endsWith(path, ".pro")) return LangJson;
    return LangPlain;
}

const char* languageName(Language lang) {
    switch (lang) {
        case LangC:        return "C";
        case LangCpp:      return "C++";
        case LangShalimar: return "Shalimar";
        case LangAsm:      return "asm";
        case LangJson:     return "JSON";
        default:           return "text";
    }
}

const char* languageSuffix(Language lang) {
    switch (lang) {
        case LangC:        return "c";
        case LangCpp:      return "cpp";
        case LangShalimar: return "shl";
        case LangAsm:      return "s";
        case LangJson:     return "json";
        default:           return "txt";
    }
}

const char* colourFor(unsigned char kind) {
    switch (kind) {
        case KindKeyword: return "94";
        case KindType:    return "36";
        case KindString:  return "32";
        case KindChar:    return "32";
        case KindComment: return "90";
        case KindPreproc: return "35";
        case KindNumber:  return "33";
        case KindLabel:   return "93";
        default:          return "39";
    }
}

void advanceState(const std::string& line, Language lang, SyntaxState& state) {

    if (lang == LangPlain || lang == LangAsm || lang == LangShalimar ||
        lang == LangJson) {
        state.comment = false;
        return;
    }
    for (size_t i = 0; i < line.size(); ++i) {
        if (state.comment) {
            if (line[i] == '*' && i + 1 < line.size() && line[i + 1] == '/') {
                state.comment = false;
                ++i;
            }
            continue;
        }
        char c = line[i];
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') return;
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
            state.comment = true;
            ++i;
            continue;
        }
        if (c == '"' || c == '\'') {
            char closer = c;
            ++i;
            while (i < line.size()) {
                if (line[i] == '\\' && i + 1 < line.size()) ++i;
                else if (line[i] == closer) break;
                ++i;
            }
        }
    }
}

std::vector<unsigned char> highlight(const std::string& line, Language lang,
                                     SyntaxState& state) {
    std::vector<unsigned char> kind(line.size(), KindNormal);
    if (lang == LangPlain || line.empty()) {
        state.string = false;
        return kind;
    }
    if (lang == LangAsm) {
        markAsm(line, kind);
        return kind;
    }
    if (lang == LangShalimar) {
        markShalimar(line, kind);
        return kind;
    }
    if (lang == LangJson) {
        markJson(line, kind);
        return kind;
    }

    size_t i = 0;

    if (state.comment) {
        while (i < line.size()) {
            kind[i] = KindComment;
            if (line[i] == '*' && i + 1 < line.size() && line[i + 1] == '/') {
                kind[++i] = KindComment;
                ++i;
                state.comment = false;
                break;
            }
            ++i;
        }
        if (state.comment) return kind;
    }

    size_t head = i;
    while (head < line.size() && (line[head] == ' ' || line[head] == '\t')) ++head;
    bool include = false;
    if (head < line.size() && line[head] == '#') {
        size_t j = head + 1;
        while (j < line.size() && (line[j] == ' ' || line[j] == '\t')) ++j;
        size_t wordStart = j;
        while (j < line.size() && identChar(line[j])) ++j;
        for (size_t m = head; m < j; ++m) kind[m] = KindPreproc;
        include = line.compare(wordStart, j - wordStart, "include") == 0;
        i = j;
    }

    for (; i < line.size(); ++i) {
        char c = line[i];

        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
            for (size_t j = i; j < line.size(); ++j) kind[j] = KindComment;
            return kind;
        }

        if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
            kind[i] = kind[i + 1] = KindComment;
            i += 2;
            state.comment = true;
            while (i < line.size()) {
                kind[i] = KindComment;
                if (line[i] == '*' && i + 1 < line.size() && line[i + 1] == '/') {
                    kind[++i] = KindComment;
                    state.comment = false;
                    break;
                }
                ++i;
            }
            if (state.comment) return kind;
            continue;
        }

        if (c == '"' || (include && c == '<')) {
            char closer = (c == '<') ? '>' : '"';
            size_t j = i;
            kind[j] = KindString;
            ++j;
            while (j < line.size()) {
                kind[j] = KindString;
                if (closer == '"' && line[j] == '\\' && j + 1 < line.size()) {
                    kind[++j] = KindString;
                } else if (line[j] == closer) {
                    break;
                }
                ++j;
            }
            i = (j < line.size()) ? j : line.size() - 1;
            continue;
        }

        if (c == '\'') {
            size_t j = i;
            kind[j] = KindChar;
            ++j;
            while (j < line.size()) {
                kind[j] = KindChar;
                if (line[j] == '\\' && j + 1 < line.size()) kind[++j] = KindChar;
                else if (line[j] == '\'') break;
                ++j;
            }
            i = (j < line.size()) ? j : line.size() - 1;
            continue;
        }

        if (digit(c) && (i == 0 || !identChar(line[i - 1]))) {
            size_t j = i;
            while (j < line.size() && (identChar(line[j]) || line[j] == '.')) ++j;
            for (size_t m = i; m < j; ++m) kind[m] = KindNumber;
            i = j - 1;
            continue;
        }

        if (identChar(c) && !digit(c)) {
            size_t j = i;
            while (j < line.size() && identChar(line[j])) ++j;
            std::string word = line.substr(i, j - i);

            unsigned char what = KindNormal;
            if (inList(kCKeywords, word)) what = KindKeyword;
            else if (inList(kCTypes, word)) what = KindType;
            else if (lang == LangCpp && inList(kCppKeywords, word)) what = KindKeyword;
            else if (lang == LangCpp && inList(kCppTypes, word)) what = KindType;

            if (what != KindNormal)
                for (size_t m = i; m < j; ++m) kind[m] = what;
            i = j - 1;
            continue;
        }
    }

    return kind;
}

}
