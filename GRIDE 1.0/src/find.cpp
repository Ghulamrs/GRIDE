#include "find.h"
#include "path.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace editor {

Match findNext(const std::vector<std::string>& lines, const std::string& needle,
               size_t row, size_t col) {
    Match match;
    if (needle.empty() || lines.empty()) return match;
    if (row >= lines.size()) row = lines.size() - 1;

    for (size_t step = 0; step <= lines.size(); ++step) {
        size_t at = (row + step) % lines.size();
        size_t from = (step == 0) ? col : 0;
        if (from > lines[at].size()) continue;

        size_t found = lines[at].find(needle, from);

        if (step == lines.size() && found != std::string::npos && found >= col)
            return match;

        if (found != std::string::npos) {
            match.found = true;
            match.row = at;
            match.col = found;
            return match;
        }
    }
    return match;
}

Match findPrevious(const std::vector<std::string>& lines, const std::string& needle,
                   size_t row, size_t col) {
    Match match;
    if (needle.empty() || lines.empty()) return match;
    if (row >= lines.size()) row = lines.size() - 1;

    for (size_t step = 0; step <= lines.size(); ++step) {
        size_t at = (row + lines.size() - (step % lines.size())) % lines.size();

        size_t upTo;
        if (step == 0) {
            if (col == 0) continue;
            upTo = col - 1;
        } else {
            upTo = lines[at].size();
        }

        size_t found = lines[at].rfind(needle, upTo);
        if (found == std::string::npos) continue;
        if (step == lines.size() && found < col) return match;

        match.found = true;
        match.row = at;
        match.col = found;
        return match;
    }
    return match;
}

size_t replaceAll(std::vector<std::string>& lines, const std::string& needle,
                  const std::string& with) {
    if (needle.empty()) return 0;

    size_t count = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string& line = lines[i];
        size_t at = 0;
        for (;;) {
            size_t found = line.find(needle, at);
            if (found == std::string::npos) break;
            line.replace(found, needle.size(), with);

            at = found + with.size();
            ++count;
        }
    }
    return count;
}

}

namespace editor {

namespace {

std::string lowered(const std::string& text) {
    std::string out = text;
    for (size_t i = 0; i < out.size(); ++i) out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
    return out;
}

// '*' any run, '?' any one character, letters compared without case: what a file dialog's filter means.
bool wildMatch(const std::string& pattern, const std::string& name) {
    size_t p = 0, n = 0, star = std::string::npos, back = 0;
    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == '?' ||
                                   std::tolower(static_cast<unsigned char>(pattern[p])) ==
                                       std::tolower(static_cast<unsigned char>(name[n])))) { ++p; ++n; }
        else if (p < pattern.size() && pattern[p] == '*') { star = p++; back = n; }
        else if (star != std::string::npos) { p = star + 1; n = ++back; }
        else return false;
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

bool fits(const std::vector<std::string>& patterns, const std::string& name) {
    if (patterns.empty()) return true;
    for (size_t i = 0; i < patterns.size(); ++i)
        if (wildMatch(patterns[i], name)) return true;
    return false;
}

// What a build or a tool leaves, which no one searching their sources means to search.
bool passedOver(const std::string& folder) {
    static const char* const skip[] = {".git", ".svn", "obj", "x64", "Debug", "Release", "build", ".vs", "node_modules"};
    for (size_t i = 0; i < sizeof skip / sizeof skip[0]; ++i)
        if (folder == skip[i]) return true;
    return folder.size() > 5 && folder.compare(folder.size() - 5, 5, ".dSYM") == 0;
}

bool wordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

void walk(const FindInFiles& q, const std::string& dir, const std::vector<std::string>& patterns,
          const std::string& needle, size_t limit, std::vector<FileHit>& out, size_t& files, bool& cut) {
    bool ok = false;
    std::vector<path::Entry> all = path::entries(dir, &ok);
    if (!ok) return;
    // By name: NTFS and APFS list a folder sorted and Linux does not, and the hits should read alike everywhere.
    std::sort(all.begin(), all.end(), [](const path::Entry& a, const path::Entry& b) { return a.name < b.name; });
    for (size_t i = 0; i < all.size() && !cut; ++i) {
        if (q.stop && q.stop->load()) { cut = true; return; }
        const std::string full = path::join(dir, all[i].name);
        if (all[i].directory) {
            if (q.subfolders && !passedOver(all[i].name)) walk(q, full, patterns, needle, limit, out, files, cut);
            continue;
        }
        if (!fits(patterns, all[i].name)) continue;
        ++files;
        if (q.namesOnly) {
            const std::string name = q.matchCase ? all[i].name : lowered(all[i].name);
            if (needle.empty() || name.find(needle) != std::string::npos) {
                FileHit hit;
                hit.file = full;
                hit.text = all[i].name;
                out.push_back(hit);
                if (out.size() >= limit) cut = true;
            }
            continue;
        }
        // FILE* and not <fstream>: linked into the C++/CLI window, the iostream library's globals corrupt the heap
        // before main - the trap settings.cpp names, and every other source here reads files this way for it.
        std::FILE* in = std::fopen(full.c_str(), "rb");
        if (!in) continue;
        std::string body;
        char chunk[65536];
        size_t got = 0;
        while ((got = std::fread(chunk, 1, sizeof chunk, in)) > 0) {
            body.append(chunk, got);
            if (body.size() > 8 * 1024 * 1024) break;
        }
        std::fclose(in);
        if (body.size() > 8 * 1024 * 1024 || body.find('\0') != std::string::npos) continue;  // too big, or not text
        size_t lineNo = 0, start = 0;
        while (start <= body.size() && !cut) {
            size_t end = body.find('\n', start);
            if (end == std::string::npos) end = body.size();
            std::string line = body.substr(start, end - start);
            if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
            ++lineNo;
            const std::string hay = q.matchCase ? line : lowered(line);
            for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1)) {
                if (q.wholeWord && ((at > 0 && wordChar(hay[at - 1])) ||
                                    (at + needle.size() < hay.size() && wordChar(hay[at + needle.size()]))))
                    continue;
                FileHit hit;
                hit.file = full;
                hit.line = lineNo;
                hit.col = at + 1;
                hit.text = line;
                out.push_back(hit);
                if (out.size() >= limit) cut = true;
                break;  // one hit a line: the line is what is shown
            }
            if (end == body.size()) break;
            start = end + 1;
        }
    }
}

}

std::vector<FileHit> findInFiles(const FindInFiles& query, size_t limit, size_t& files, bool& cut) {
    std::vector<FileHit> out;
    files = 0;
    cut = false;
    if (query.folder.empty() || !path::isDirectory(query.folder)) return out;
    if (query.text.empty() && !query.namesOnly) return out;
    std::vector<std::string> patterns;
    std::string one;
    for (size_t i = 0; i <= query.patterns.size(); ++i) {
        const char c = i < query.patterns.size() ? query.patterns[i] : ';';
        if (c == ';' || c == ',' || c == ' ') {
            if (!one.empty()) patterns.push_back(one);
            one.clear();
        } else {
            one += c;
        }
    }
    const std::string needle = query.matchCase ? query.text : lowered(query.text);
    walk(query, path::absolute(query.folder), patterns, needle, limit == 0 ? 1 : limit, out, files, cut);
    return out;
}

}
