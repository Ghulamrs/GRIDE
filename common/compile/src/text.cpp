#include "text.h"

#include <cstdio>

namespace gride { namespace compile {

std::string lower(const std::string &s) {
    std::string r = s;
    for (size_t i = 0; i < r.size(); ++i) if (r[i] >= 'A' && r[i] <= 'Z') r[i] = static_cast<char>(r[i] - 'A' + 'a');
    return r;
}

bool same(const std::string &a, const std::string &b) { return a.size() == b.size() && lower(a) == lower(b); }

bool startsWith(const std::string &s, const std::string &head) {
    return s.size() >= head.size() && s.compare(0, head.size(), head) == 0;
}

bool endsWith(const std::string &s, const std::string &tail) {
    return s.size() >= tail.size() && same(s.substr(s.size() - tail.size()), tail);
}

std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

Words split(const std::string &s, const std::string &separators, bool keepEmpty) {
    Words r;
    size_t at = 0;
    for (;;) {
        size_t next = s.find_first_of(separators, at);
        std::string part = s.substr(at, next == std::string::npos ? std::string::npos : next - at);
        if (keepEmpty || !trim(part).empty()) r.push_back(keepEmpty ? part : trim(part));
        if (next == std::string::npos) break;
        at = next + 1;
    }
    return r;
}

std::string join(const Words &w, const std::string &between) {
    std::string r;
    for (size_t i = 0; i < w.size(); ++i) { if (i) r += between; r += w[i]; }
    return r;
}

std::string replaceAll(std::string s, const std::string &what, const std::string &with) {
    if (what.empty()) return s;
    for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + with.size())) s.replace(at, what.size(), with);
    return s;
}

bool contains(const Words &w, const std::string &s) {
    for (size_t i = 0; i < w.size(); ++i) if (same(w[i], s)) return true;
    return false;
}

void addOnce(Words &w, const std::string &s) { if (!contains(w, s)) w.push_back(s); }

// ---- paths ----

bool absolute(const std::string &p) {
    if (p.size() >= 2 && p[1] == ':') return true;
    return !p.empty() && (p[0] == '\\' || p[0] == '/');
}

std::string slashes(const std::string &path) {
    std::string r = path;
    for (size_t i = 0; i < r.size(); ++i) if (r[i] == '/') r[i] = '\\';
    return r;
}

std::string forward(const std::string &path) {
    std::string r = path;
    for (size_t i = 0; i < r.size(); ++i) if (r[i] == '\\') r[i] = '/';
    return r;
}

std::string tidy(const std::string &path) {
    std::string p = slashes(path);
    std::string head;
    if (p.size() >= 2 && p[1] == ':') { head = p.substr(0, 2); p = p.substr(2); }
    else if (startsWith(p, "\\\\")) { head = "\\"; p = p.substr(1); }
    bool rooted = !p.empty() && p[0] == '\\';
    Words parts = split(p, "\\", true), kept;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].empty() || parts[i] == ".") continue;
        if (parts[i] == ".." && !kept.empty() && kept.back() != "..") { kept.pop_back(); continue; }
        if (parts[i] == ".." && rooted) continue;
        kept.push_back(parts[i]);
    }
    std::string r = head + (rooted ? "\\" : "") + join(kept, "\\");
    return r.empty() ? "." : r;
}

std::string combine(const std::string &folder, const std::string &path) {
    if (path.empty()) return tidy(folder);
    if (absolute(path) || folder.empty()) return tidy(path);
    return tidy(folder + "\\" + path);
}

std::string parent(const std::string &path) {
    std::string p = slashes(path);
    size_t at = p.find_last_of('\\');
    if (at == std::string::npos) return std::string();
    if (at == 2 && p[1] == ':') return p.substr(0, 3);
    return p.substr(0, at);
}

std::string leaf(const std::string &path) {
    size_t at = path.find_last_of("\\/");
    return at == std::string::npos ? path : path.substr(at + 1);
}

std::string stem(const std::string &path) {
    std::string l = leaf(path);
    size_t dot = l.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? l : l.substr(0, dot);
}

std::string extension(const std::string &path) {
    std::string l = leaf(path);
    size_t dot = l.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? std::string() : lower(l.substr(dot));
}

bool under(const std::string &folder, const std::string &path) {
    std::string f = lower(tidy(folder)), p = lower(tidy(path));
    if (f == p) return true;
    if (!f.empty() && f[f.size() - 1] != '\\') f += '\\';
    return startsWith(p, f);
}

std::string relative(const std::string &folder, const std::string &path) {
    std::string f = tidy(folder), p = tidy(path);
    if (!under(f, p)) return p;
    if (p.size() == f.size()) return ".";
    size_t from = f.size();
    if (from < p.size() && p[from] == '\\') ++from;
    return p.substr(from);
}

bool samePath(const std::string &a, const std::string &b) { return lower(tidy(a)) == lower(tidy(b)); }

bool wildcard(const std::string &pattern) { return pattern.find_first_of("*?") != std::string::npos; }

static bool matchAt(const std::string &p, size_t pi, const std::string &n, size_t ni) {
    while (pi < p.size()) {
        if (p[pi] == '*') {
            while (pi < p.size() && p[pi] == '*') ++pi;
            if (pi == p.size()) return true;
            for (size_t k = ni; k <= n.size(); ++k) if (matchAt(p, pi, n, k)) return true;
            return false;
        }
        if (ni >= n.size()) return false;
        if (p[pi] != '?' && p[pi] != n[ni]) return false;
        ++pi; ++ni;
    }
    return ni == n.size();
}

bool matches(const std::string &pattern, const std::string &name) { return matchAt(lower(pattern), 0, lower(name), 0); }

// ---- command lines ----
// Windows' rule (CommandLineToArgvW, the C runtime): 2n backslashes before a quote are n backslashes and the
// quote opens or closes a quoted stretch; 2n+1 are n backslashes and a literal quote; other backslashes are
// themselves.
Words cutWords(const std::string &line) {
    Words r;
    std::string word;
    bool inWord = false, quoted = false;
    for (size_t i = 0; i < line.size();) {
        char c = line[i];
        if (c == '\\') {
            size_t n = 0;
            while (i < line.size() && line[i] == '\\') { ++n; ++i; }
            if (i < line.size() && line[i] == '"') {
                word.append(n / 2, '\\');
                if (n % 2) { word += '"'; ++i; }
            } else word.append(n, '\\');
            inWord = true;
            continue;
        }
        if (c == '"') { quoted = !quoted; inWord = true; ++i; continue; }
        if (!quoted && (c == ' ' || c == '\t' || c == '\r' || c == '\n')) {
            if (inWord) { r.push_back(word); word.clear(); inWord = false; }
            ++i;
            continue;
        }
        word += c; inWord = true; ++i;
    }
    if (inWord) r.push_back(word);
    return r;
}

static std::string quotedWord(const std::string &a) {
    if (!a.empty() && a.find_first_of(" \t\"") == std::string::npos) return a;
    std::string r = "\"";
    for (size_t i = 0; i < a.size();) {
        size_t n = 0;
        while (i < a.size() && a[i] == '\\') { ++n; ++i; }
        if (i == a.size()) { r.append(n * 2, '\\'); break; }
        if (a[i] == '"') { r.append(n * 2 + 1, '\\'); r += '"'; }
        else { r.append(n, '\\'); r += a[i]; }
        ++i;
    }
    return r + "\"";
}

std::string commandLine(const std::string &program, const Words &argv) {
    std::string r = quotedWord(program);
    for (size_t i = 0; i < argv.size(); ++i) { r += ' '; r += quotedWord(argv[i]); }
    return r;
}

Words namesIn(const std::string &word) {
    Words r;
    for (size_t at = word.find("$("); at != std::string::npos; at = word.find("$(", at + 2)) {
        size_t end = word.find(')', at);
        if (end == std::string::npos) break;
        r.push_back(word.substr(at + 2, end - at - 2));
    }
    return r;
}

// A template is cut into words while its quotes are still the template's own, so a value never changes where
// a word ends - a path with a blank, or one ending in a backslash before a quote, stays one word.
Words fill(const std::string &templ, const Table &table, Words *unknown) {
    Words r, words = cutWords(templ);
    for (size_t w = 0; w < words.size(); ++w) {
        const std::string &word = words[w];
        Words names = namesIn(word);
        if (names.size() == 1 && word == "$(" + names[0] + ")") {
            Table::const_iterator it = table.find(names[0]);
            if (it == table.end()) { if (unknown) addOnce(*unknown, names[0]); r.push_back(word); continue; }
            for (size_t i = 0; i < it->second.size(); ++i) r.push_back(it->second[i]);
            continue;
        }
        std::string out = word;
        for (size_t n = 0; n < names.size(); ++n) {
            Table::const_iterator it = table.find(names[n]);
            if (it == table.end()) { if (unknown) addOnce(*unknown, names[n]); continue; }
            out = replaceAll(out, "$(" + names[n] + ")", join(it->second, " "));
        }
        r.push_back(out);
    }
    return r;
}

std::string stampHash(const std::string &text) {
    unsigned long long h = 14695981039346656037ULL;
    for (size_t i = 0; i < text.size(); ++i) { h ^= static_cast<unsigned char>(text[i]); h *= 1099511628211ULL; }
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", h);
    return b;
}

}}
