#include "ccsxml.h"

#include <cstdio>
#include <cstdlib>

namespace editor {
namespace ccs {

std::string XmlNode::attribute(const std::string& key, const std::string& fallback) const {
    for (size_t i = 0; i < attributes.size(); ++i)
        if (attributes[i].first == key) return attributes[i].second;
    return fallback;
}

bool XmlNode::has(const std::string& key) const {
    for (size_t i = 0; i < attributes.size(); ++i)
        if (attributes[i].first == key) return true;
    return false;
}

const XmlNode* XmlNode::child(const std::string& wanted) const {
    for (size_t i = 0; i < children.size(); ++i)
        if (children[i].name == wanted) return &children[i];
    return 0;
}

std::vector<const XmlNode*> XmlNode::all(const std::string& wanted) const {
    std::vector<const XmlNode*> out;
    for (size_t i = 0; i < children.size(); ++i)
        if (children[i].name == wanted) out.push_back(&children[i]);
    return out;
}

void XmlNode::find(const std::string& wanted, std::vector<const XmlNode*>& into) const {
    for (size_t i = 0; i < children.size(); ++i) {
        if (children[i].name == wanted) into.push_back(&children[i]);
        children[i].find(wanted, into);
    }
}

namespace {

struct Reader {
    const std::string& text;
    size_t at;
    size_t line;
    std::string error;

    Reader(const std::string& t) : text(t), at(0), line(1) {}

    bool done() const { return at >= text.size(); }
    char peek(size_t ahead = 0) const { return at + ahead < text.size() ? text[at + ahead] : '\0'; }
    void take() { if (!done()) { if (text[at] == '\n') ++line; ++at; } }
    bool startsWith(const char* what) const { return text.compare(at, std::char_traits<char>::length(what), what) == 0; }
    void skipSpace() { while (!done() && (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n')) take(); }
    bool fail(const std::string& why) { if (error.empty()) error = "line " + std::to_string(line) + ": " + why; return false; }

    // Past `<?...?>`, `<!--...-->` and `<!...>`, which say nothing a reader here wants.
    bool skipNoise() {
        for (;;) {
            skipSpace();
            if (startsWith("<?")) { size_t end = text.find("?>", at); if (end == std::string::npos) return fail("unterminated <?"); while (at < end + 2) take(); }
            else if (startsWith("<!--")) { size_t end = text.find("-->", at); if (end == std::string::npos) return fail("unterminated comment"); while (at < end + 3) take(); }
            else if (startsWith("<!")) { size_t end = text.find('>', at); if (end == std::string::npos) return fail("unterminated <!"); while (at < end + 1) take(); }
            else return true;
        }
    }

    static bool nameChar(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '-' || c == '.' || c == ':';
    }

    std::string name() {
        std::string out;
        while (!done() && nameChar(peek())) { out += peek(); take(); }
        return out;
    }

    static std::string decoded(const std::string& raw) {
        std::string out;
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] != '&') { out += raw[i]; continue; }
            size_t semi = raw.find(';', i);
            std::string entity = semi == std::string::npos ? std::string() : raw.substr(i + 1, semi - i - 1);
            if (entity == "quot") out += '"';
            else if (entity == "amp") out += '&';
            else if (entity == "lt") out += '<';
            else if (entity == "gt") out += '>';
            else if (entity == "apos") out += '\'';
            else if (entity.size() > 1 && entity[0] == '#') {
                long code = entity[1] == 'x' ? std::strtol(entity.c_str() + 2, 0, 16) : std::strtol(entity.c_str() + 1, 0, 10);
                if (code > 0 && code < 128) out += static_cast<char>(code);
            } else { out += raw[i]; continue; }
            i = semi;
        }
        return out;
    }

    static std::string trimmed(const std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
        return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    }

    bool element(XmlNode& node) {
        if (peek() != '<') return fail("expected '<'");
        take();
        node.name = name();
        if (node.name.empty()) return fail("an element needs a name");
        for (;;) {
            skipSpace();
            if (startsWith("/>")) { take(); take(); return true; }
            if (peek() == '>') { take(); break; }
            std::string key = name();
            if (key.empty()) return fail("bad attribute in <" + node.name + ">");
            skipSpace();
            if (peek() != '=') return fail("attribute " + key + " needs a value");
            take();
            skipSpace();
            char quote = peek();
            if (quote != '"' && quote != '\'') return fail("attribute " + key + " needs a quoted value");
            take();
            size_t end = text.find(quote, at);
            if (end == std::string::npos) return fail("unterminated attribute " + key);
            std::string value = text.substr(at, end - at);
            while (at < end + 1) take();
            node.attributes.push_back(std::make_pair(key, decoded(value)));
        }
        std::string body;
        for (;;) {
            if (done()) return fail("<" + node.name + "> is never closed");
            if (startsWith("</")) {
                take(); take();
                std::string closing = name();
                skipSpace();
                if (peek() != '>') return fail("bad close of " + closing);
                take();
                if (closing != node.name) return fail("</" + closing + "> closes <" + node.name + ">");
                node.text = decoded(trimmed(body));
                return true;
            }
            if (startsWith("<!--")) { if (!skipNoise()) return false; continue; }
            if (startsWith("<![CDATA[")) return fail("CDATA is not read here");
            if (peek() == '<') {
                node.children.push_back(XmlNode());
                if (!element(node.children.back())) return false;
                continue;
            }
            body += peek();
            take();
        }
    }
};

}

bool parseXml(const std::string& text, XmlNode& root, std::string& error) {
    root = XmlNode();
    Reader in(text);
    if (!in.skipNoise()) { error = in.error; return false; }
    if (in.done()) { error = "the file is empty"; return false; }
    if (!in.element(root)) { error = in.error; return false; }
    if (!in.skipNoise()) { error = in.error; return false; }
    if (!in.done()) { error = "line " + std::to_string(in.line) + ": more than one root element"; return false; }
    return true;
}

bool readXmlFile(const std::string& file, XmlNode& root, std::string& error) {
    std::FILE* in = std::fopen(file.c_str(), "rb");
    if (!in) { error = "cannot read " + file; return false; }
    std::string text;
    char chunk[4096];
    size_t got;
    while ((got = std::fread(chunk, 1, sizeof chunk, in)) > 0) text.append(chunk, got);
    std::fclose(in);
    if (!parseXml(text, root, error)) { error = file + ": " + error; return false; }
    return true;
}

}
}
