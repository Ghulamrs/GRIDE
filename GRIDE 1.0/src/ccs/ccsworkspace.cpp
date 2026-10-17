#include "ccsworkspace.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "../json.h"
#include "../path.h"
#include "ccsproject.h"
#include "ccsxml.h"

namespace editor {
namespace ccs {

namespace {

const char* const kResources = ".metadata/.plugins/org.eclipse.core.resources";
const char* const kRuntimeSettings = ".metadata/.plugins/org.eclipse.core.runtime/.settings";

bool slurp(const std::string& file, std::string& text) {
    FILE* in = std::fopen(file.c_str(), "rb");
    if (!in) return false;
    char chunk[4096];
    size_t got;
    text.clear();
    while ((got = std::fread(chunk, 1, sizeof chunk, in)) > 0) text.append(chunk, got);
    std::fclose(in);
    return true;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void appendUtf8(std::string& out, unsigned code) {
    if (code < 0x80) out += static_cast<char>(code);
    else if (code < 0x800) { out += static_cast<char>(0xC0 | (code >> 6)); out += static_cast<char>(0x80 | (code & 0x3F)); }
    else { out += static_cast<char>(0xE0 | (code >> 12)); out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F)); }
}

// A file: URI as Eclipse writes it - file:/C:/x%20y or file:/home/x - to a path; anything else as it is.
std::string pathOfUri(const std::string& uri) {
    if (uri.compare(0, 5, "file:") != 0) return uri;
    std::string text = uri.substr(5), out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && hexDigit(text[i + 1]) >= 0 && hexDigit(text[i + 2]) >= 0) {
            out += static_cast<char>(hexDigit(text[i + 1]) * 16 + hexDigit(text[i + 2]));
            i += 2;
        } else out += text[i];
    }
    while (out.size() > 1 && out[0] == '/' && out[1] == '/') out.erase(0, 1);
    if (out.size() > 2 && out[0] == '/' && out[2] == ':') out.erase(0, 1);
    return path::withSlashes(out);
}

// .location: a 16-byte chunk marker, then Java's writeUTF of the location - a big-endian length
// and the bytes - "URI//file:/C:/Users/GRA/ws47src/Sample" from CCS 7.4, a bare path from older Eclipse.
std::string locationRecord(const std::string& bytes) {
    if (bytes.size() < 18) return std::string();
    size_t length = (static_cast<unsigned char>(bytes[16]) << 8) | static_cast<unsigned char>(bytes[17]);
    if (length == 0 || 18 + length > bytes.size()) return std::string();
    std::string text = bytes.substr(18, length);
    if (text.compare(0, 5, "URI//") == 0) return pathOfUri(text.substr(5));
    return path::withSlashes(text);
}

// A Java properties file, as Eclipse's preference store writes one: key=value, the escapes undone.
std::map<std::string, std::string> properties(const std::string& text) {
    std::map<std::string, std::string> out;
    std::vector<std::string> lines;
    std::string line;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') {
            if (!line.empty() && line[line.size() - 1] == '\r') line.resize(line.size() - 1);
            // A line ending in an odd number of backslashes goes on to the next.
            size_t slashes = 0;
            while (slashes < line.size() && line[line.size() - 1 - slashes] == '\\') ++slashes;
            if (slashes % 2 == 1 && i < text.size()) { line.resize(line.size() - 1); continue; }
            lines.push_back(line);
            line.clear();
        } else line += text[i];
    }
    for (size_t n = 0; n < lines.size(); ++n) {
        const std::string& raw = lines[n];
        size_t start = raw.find_first_not_of(" \t");
        if (start == std::string::npos || raw[start] == '#' || raw[start] == '!') continue;
        std::string key, value;
        bool inKey = true;
        for (size_t i = start; i < raw.size(); ++i) {
            char c = raw[i];
            if (c == '\\' && i + 1 < raw.size()) {
                char e = raw[++i];
                std::string& into = inKey ? key : value;
                if (e == 'n') into += '\n';
                else if (e == 'r') into += '\r';
                else if (e == 't') into += '\t';
                else if (e == 'u' && i + 4 < raw.size()) {
                    unsigned code = 0;
                    for (int k = 1; k <= 4; ++k) code = code * 16 + static_cast<unsigned>(std::max(0, hexDigit(raw[i + k])));
                    appendUtf8(into, code);
                    i += 4;
                } else into += e;
                continue;
            }
            if (inKey && (c == '=' || c == ':')) { inKey = false; continue; }
            (inKey ? key : value) += c;
        }
        out[key] = value;
    }
    return out;
}

}

const Member* Workspace::project(const std::string& name) const {
    for (size_t i = 0; i < projects.size(); ++i) if (projects[i].name == name) return &projects[i];
    return 0;
}

bool isWorkspace(const std::string& dir) {
    return !dir.empty() && path::isDirectory(path::join(dir, kResources));
}

bool readWorkspace(const std::string& where, Workspace& out, std::string& error) {
    out = Workspace();
    error.clear();
    std::string dir = path::withSlashes(path::absolute(where));
    while (dir.size() > 1 && dir[dir.size() - 1] == '/') dir.resize(dir.size() - 1);
    if (!isWorkspace(dir)) { error = dir + " is not a CCS workspace (no .metadata)"; return false; }
    out.dir = dir;

    // The registry: one directory per project, a .location in it only for one left outside.
    std::string registry = path::join(path::join(dir, kResources), ".projects");
    std::vector<path::Entry> entries = path::entries(registry);
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].directory || entries[i].name.empty() || entries[i].name[0] == '.') continue;
        Member member;
        member.name = entries[i].name;
        std::string record;
        if (slurp(path::join(path::join(registry, member.name), ".location"), record)) {
            member.location = locationRecord(record);
            if (member.location.empty()) { out.notes.push_back(member.name + ": its .location could not be read"); continue; }
        } else {
            member.location = path::join(dir, member.name);
            member.inside = true;
        }
        if (!path::isDirectory(member.location)) { out.notes.push_back(member.name + " is registered at " + member.location + ", which is not on this machine"); continue; }
        if (!isProject(member.location)) continue;   // RemoteSystemsTempFiles and other non-CCS projects
        out.projects.push_back(member);
    }
    std::sort(out.projects.begin(), out.projects.end(), [](const Member& a, const Member& b) { return a.name < b.name; });

    std::string settingsDir = path::join(dir, kRuntimeSettings), text;
    if (slurp(path::join(settingsDir, "org.eclipse.core.resources.prefs"), text)) {
        std::map<std::string, std::string> prefs = properties(text);
        for (std::map<std::string, std::string>::const_iterator it = prefs.begin(); it != prefs.end(); ++it)
            if (it->first.compare(0, 13, "pathvariable.") == 0)
                out.pathVariables[it->first.substr(13)] = pathOfUri(it->second);
    }
    if (slurp(path::join(settingsDir, "org.eclipse.cdt.core.prefs"), text)) {
        std::map<std::string, std::string> prefs = properties(text);
        std::map<std::string, std::string>::const_iterator xml = prefs.find("macros/workspace");
        XmlNode root;
        std::string why;
        if (xml != prefs.end() && parseXml(xml->second, root, why)) {
            std::vector<const XmlNode*> macros;
            root.find("stringMacro", macros);
            for (size_t i = 0; i < macros.size(); ++i) out.macros[macros[i]->attribute("name")] = macros[i]->attribute("value");
        } else if (xml != prefs.end()) out.notes.push_back("the workspace's build macros could not be read: " + why);
    }
    return true;
}

std::string workspaceOf(const std::string& projectDir) {
    std::string dir = path::withSlashes(path::absolute(projectDir));
    std::string parent = path::parent(dir);
    if (!isWorkspace(parent)) return std::string();
    Workspace ws;
    std::string error;
    if (!readWorkspace(parent, ws, error)) return std::string();
    for (size_t i = 0; i < ws.projects.size(); ++i)
        if (path::same(ws.projects[i].location, dir)) return ws.dir;
    return std::string();
}

std::string proFor(const std::string& workspaceDir, const std::string& project) {
    return path::join(path::withSlashes(workspaceDir), project + ".pro");
}

bool writePro(const std::string& workspaceDir, const std::string& project, std::string& file, std::string& error) {
    error.clear();
    file = proFor(workspaceDir, project);
    if (path::exists(file)) return true;
    Json ccs = Json::object();
    ccs.set("workspace", Json::fromText("."));
    ccs.set("project", Json::fromText(project));
    Json root = Json::object();
    root.set("ccs", ccs);
    std::string text = root.write() + "\n";
    FILE* out = std::fopen(file.c_str(), "wb");
    if (!out) { error = "cannot write " + file; return false; }
    bool trouble = std::fwrite(text.data(), 1, text.size(), out) != text.size();
    if (std::fclose(out) != 0) trouble = true;
    if (trouble) { error = "cannot write " + file; return false; }
    return true;
}

}
}
