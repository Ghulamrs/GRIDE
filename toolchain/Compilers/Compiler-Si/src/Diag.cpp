#include "Diag.h"

namespace shalimar {

std::string Message::formatted(const std::vector<std::string>& units) const {
    std::string out = severity == Severity::Error ? "Error: " : "Warning: ";
    if (line > 0) {

        if (unit > 0 && static_cast<size_t>(unit) < units.size()) {
            out += units[static_cast<size_t>(unit)];
            out += " ";
        }
        out += "line ";
        out += std::to_string(line);
        out += ": ";
    }
    out += text;
    return out;
}

void Diagnostics::error(int line, const std::string& text) {
    error(0, line, text);
}

void Diagnostics::warning(int line, const std::string& text) {
    warning(0, line, text);
}

void Diagnostics::error(int unit, int line, const std::string& text) {
    messages_.push_back(Message{Severity::Error, line, text, unit});
    ++errors_;
}

void Diagnostics::error(int unit, int line, int col, const std::string& text) {
    Message m{Severity::Error, line, text, unit};
    m.col = col;
    messages_.push_back(m);
    ++errors_;
}

namespace {

bool identChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// line n (1-based) of a text, without its line break
std::string lineOf(const std::string& text, int n) {
    size_t at = 0;
    for (int i = 1; i < n; ++i) {
        at = text.find('\n', at);
        if (at == std::string::npos) return std::string();
        ++at;
    }
    size_t end = text.find('\n', at);
    std::string line = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

// a message naming no column that quotes a name ("Undefined variable 'x'"): the column of that name on the line
int columnOfQuotedName(const std::string& text, const std::string& line) {
    size_t open = text.find('\'');
    if (open == std::string::npos) return 0;
    size_t close = text.find('\'', open + 1);
    if (close == std::string::npos || close == open + 1) return 0;
    std::string name = text.substr(open + 1, close - open - 1);
    for (char c : name) if (!identChar(c)) return 0;
    for (size_t at = line.find(name); at != std::string::npos; at = line.find(name, at + 1)) {
        bool before = at == 0 || !identChar(line[at - 1]);
        bool after = at + name.size() >= line.size() || !identChar(line[at + name.size()]);
        if (before && after) return static_cast<int>(at) + 1;
    }
    return 0;
}

}

std::string Diagnostics::gnu(const Message& m) const {
    size_t u = static_cast<size_t>(m.unit > 0 ? m.unit : 0);
    std::string path = u < paths_.size() ? paths_[u] : std::string();
    if (m.line <= 0 || path.empty()) return m.formatted(units_) + "\n";
    std::string line = u < sources_.size() ? lineOf(sources_[u], m.line) : std::string();
    int col = m.col > 0 ? m.col : columnOfQuotedName(m.text, line);
    if (col <= 0) col = 1;
    std::string out = path + ":" + std::to_string(m.line) + ":" + std::to_string(col) + ": " +
                      (m.severity == Severity::Error ? "error: " : "warning: ") + m.text + "\n";
    if (!line.empty()) {
        out += "    " + line + "\n    ";
        for (int i = 0; i + 1 < col && static_cast<size_t>(i) < line.size(); ++i) out += line[static_cast<size_t>(i)] == '\t' ? '\t' : ' ';
        out += "^\n";
    }
    return out;
}

void Diagnostics::warning(int unit, int line, const std::string& text) {
    messages_.push_back(Message{Severity::Warning, line, text, unit});
}

void Diagnostics::unsupported(int line, const std::string& text) {
    unsupported_.push_back(Message{Severity::Error, line, text});
}

void Diagnostics::writeTo(std::string& out) const {
    for (const Message& m : messages_) out += gnu(m);
}

}
