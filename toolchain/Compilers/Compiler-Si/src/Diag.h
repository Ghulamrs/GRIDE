
#pragma once

#include <string>
#include <vector>

namespace shalimar {

enum class Severity { Error, Warning };

struct Message {
    Severity severity;
    int line;
    std::string text;

    int unit = 0;
    int col = 0;        // 1-based; 0 when the message names no column

    std::string formatted(const std::vector<std::string> &units) const;
};

class Diagnostics {
public:
    void error(int line, const std::string& text);
    void warning(int line, const std::string& text);
    void error(int unit, int line, const std::string& text);
    void warning(int unit, int line, const std::string& text);
    void error(int unit, int line, int col, const std::string& text);

    void nameUnits(const std::vector<std::string>& names) { units_ = names; }
    // the units' paths as given and their text: a message then reads "path:line:col: error: text", with the line and
    // a caret under the column, as c90's and cpp11's do
    void pathUnits(const std::vector<std::string>& paths) { paths_ = paths; }
    void sourceOf(int unit, const std::string& text) {
        if (unit < 0) return;
        if (sources_.size() <= static_cast<size_t>(unit)) sources_.resize(static_cast<size_t>(unit) + 1);
        sources_[static_cast<size_t>(unit)] = text;
    }

    void unsupported(int line, const std::string& what);

    bool hasUnsupported() const { return !unsupported_.empty(); }
    const std::vector<Message>& unsupportedItems() const { return unsupported_; }

    bool hasErrors() const { return errors_ > 0; }
    const std::vector<Message>& messages() const { return messages_; }
    void writeTo(std::string& out) const;

private:
    std::vector<Message> messages_;
    std::vector<Message> unsupported_;
    std::vector<std::string> units_;
    std::vector<std::string> paths_;
    std::vector<std::string> sources_;
    int errors_ = 0;

    std::string gnu(const Message& m) const;
};

}
