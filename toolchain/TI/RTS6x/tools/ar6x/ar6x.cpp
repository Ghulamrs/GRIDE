// ar6x - the archiver RTS6x is packed with: TI ELF objects in, a SysV `ar` library out,
// in the form LNK6x's archive.cpp reads. Our own, so that no host `ar` is in the build.
// Spec: the SysV ar format (magic, 60-byte headers, `/` index, `//` long names); ELF32 (gABI).

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

typedef unsigned char u8;
typedef unsigned int u32;

struct Member {
    std::string name;               // the object's file name, without its directory
    std::vector<u8> bytes;
    std::vector<std::string> defs;  // the global and weak symbols it defines
    std::vector<bool> weak;         // beside each, whether it is weak - an inline function, say
};

bool readFile(const std::string &path, std::vector<u8> &out)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? size_t(n) : 0);
    bool ok = n <= 0 || std::fread(&out[0], 1, size_t(n), f) == size_t(n);
    std::fclose(f);
    return ok;
}

u32 le32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (u32(p[3]) << 24); }
u32 le16(const u8 *p) { return p[0] | (p[1] << 8); }

// The length of s up to its NUL or to most bytes, whichever comes first (strnlen is POSIX, not ISO C++).
size_t boundedLength(const char *s, size_t most)
{
    size_t n = 0;
    while (n < most && s[n]) n++;
    return n;
}

// The symbols an object defines for others: every GLOBAL or WEAK symbol that is not undefined,
// in symbol-table order. A symbol of type FILE or SECTION is never one of them.
bool definedSymbols(const std::vector<u8> &d, std::vector<std::string> &out, std::vector<bool> &weak, std::string &why)
{
    if (d.size() < 52 || std::memcmp(&d[0], "\x7f" "ELF", 4) != 0) { why = "not an ELF object"; return false; }
    if (d[4] != 1 || d[5] != 1) { why = "not ELF32 little-endian"; return false; }
    u32 shoff = le32(&d[0x20]), shentsize = le16(&d[0x2e]), shnum = le16(&d[0x30]);
    if (shentsize < 40 || u32(shoff) + shnum * shentsize > d.size()) { why = "section headers past the end"; return false; }
    for (u32 i = 0; i < shnum; i++) {
        const u8 *s = &d[shoff + i * shentsize];
        if (le32(s + 4) != 2) continue;                      // SHT_SYMTAB
        u32 off = le32(s + 16), size = le32(s + 20), link = le32(s + 24), ent = le32(s + 36);
        if (ent < 16 || link >= shnum || off + size > d.size()) { why = "a broken symbol table"; return false; }
        const u8 *str = &d[shoff + link * shentsize];
        u32 stroff = le32(str + 16), strsize = le32(str + 20);
        if (stroff + strsize > d.size()) { why = "a broken string table"; return false; }
        for (u32 k = 1; k < size / ent; k++) {
            const u8 *y = &d[off + k * ent];
            u32 name = le32(y), bind = y[12] >> 4, type = y[12] & 15, shndx = le16(y + 14);
            if (shndx == 0 || (bind != 1 && bind != 2) || type == 3 || type == 4) continue;
            if (name >= strsize) { why = "a symbol name past its table"; return false; }
            const char *p = reinterpret_cast<const char *>(&d[stroff + name]);
            out.push_back(std::string(p, boundedLength(p, strsize - name)));
            weak.push_back(bind == 2);
        }
    }
    return true;
}

std::string folded(std::string s)
{
    for (size_t i = 0; i < s.size(); i++)
        if (s[i] >= 'A' && s[i] <= 'Z') s[i] = char(s[i] - 'A' + 'a');
    return s;
}

void put(std::vector<u8> &v, const char *s, size_t width)
{
    size_t n = std::strlen(s);
    for (size_t i = 0; i < width; i++) v.push_back(u8(i < n ? s[i] : ' '));
}

// A 60-byte member header. Date, owner, group are zero and the mode 644, so that the same objects
// always make the same library.
void header(std::vector<u8> &v, const std::string &name, size_t size)
{
    char num[16];
    put(v, name.c_str(), 16);
    put(v, "0", 12);
    put(v, "0", 6);
    put(v, "0", 6);
    put(v, "100644", 8);
    std::snprintf(num, sizeof num, "%zu", size);
    put(v, num, 10);
    v.push_back('`');
    v.push_back('\n');
}

void be32(std::vector<u8> &v, size_t at, u32 x)
{
    v[at] = u8(x >> 24); v[at + 1] = u8(x >> 16); v[at + 2] = u8(x >> 8); v[at + 3] = u8(x);
}

int create(const std::string &lib, const std::vector<std::string> &objs)
{
    std::vector<Member> ms;
    std::map<std::string, std::string> owner;   // the member the index names for each symbol
    std::map<std::string, bool> weakOwner;      // whether that member's definition is weak
    for (size_t i = 0; i < objs.size(); i++) {
        Member m;
        size_t slash = objs[i].find_last_of("/\\");
        m.name = slash == std::string::npos ? objs[i] : objs[i].substr(slash + 1);
        std::string why;
        if (!readFile(objs[i], m.bytes)) { std::fprintf(stderr, "ar6x: %s: cannot read\n", objs[i].c_str()); return 1; }
        if (!definedSymbols(m.bytes, m.defs, m.weak, why)) { std::fprintf(stderr, "ar6x: %s: %s\n", objs[i].c_str(), why.c_str()); return 1; }
        // Names equal but for case collide on the Mac's and Windows' file systems, so they are refused too.
        for (size_t k = 0; k < ms.size(); k++)
            if (folded(ms[k].name) == folded(m.name)) {
                std::fprintf(stderr, "ar6x: two members named %s and %s\n", ms[k].name.c_str(), m.name.c_str());
                return 1;
            }
        // Two strong definitions of one name would make a link's choice depend on the order; weak
        // ones - an inline function in each object that uses it - are one definition, indexed once.
        for (size_t k = 0; k < m.defs.size(); k++) {
            std::map<std::string, std::string>::iterator o = owner.find(m.defs[k]);
            if (o != owner.end() && !m.weak[k] && !weakOwner[m.defs[k]]) {
                std::fprintf(stderr, "ar6x: %s is defined in %s and in %s\n", m.defs[k].c_str(), o->second.c_str(), m.name.c_str());
                return 1;
            }
            if (o == owner.end() || (weakOwner[m.defs[k]] && !m.weak[k])) {
                owner[m.defs[k]] = m.name;
                weakOwner[m.defs[k]] = m.weak[k];
            }
        }
        ms.push_back(m);
    }

    // By name, whatever order the host's shell listed them in: the same objects, the same library.
    std::sort(ms.begin(), ms.end(), [](const Member &x, const Member &y) { return x.name < y.name; });

    // The long names, `name/\n` each, for a name past fifteen characters.
    std::string longs;
    std::vector<std::string> field(ms.size());
    for (size_t i = 0; i < ms.size(); i++) {
        if (ms[i].name.size() <= 15) { field[i] = ms[i].name + "/"; continue; }
        field[i] = "/" + std::to_string(longs.size());
        longs += ms[i].name + "/\n";
    }

    // The index: count, one member offset per name, then the names - offsets known only after the
    // layout, so the size is computed first and the offsets filled in last.
    size_t names = 0, count = 0;
    for (size_t i = 0; i < ms.size(); i++)
        for (size_t k = 0; k < ms[i].defs.size(); k++)
            if (owner[ms[i].defs[k]] == ms[i].name) { names += ms[i].defs[k].size() + 1; count++; }
    size_t isize = 4 + 4 * count + names;
    std::vector<u8> out(8);
    std::memcpy(&out[0], "!<arch>\n", 8);
    header(out, "/", isize);
    size_t idx = out.size();
    out.resize(out.size() + isize, 0);
    if (out.size() & 1) out.push_back('\n');
    if (!longs.empty()) {
        header(out, "//", longs.size());
        out.insert(out.end(), longs.begin(), longs.end());
        if (out.size() & 1) out.push_back('\n');
    }
    std::vector<size_t> at(ms.size());
    for (size_t i = 0; i < ms.size(); i++) {
        at[i] = out.size();
        header(out, field[i], ms[i].bytes.size());
        out.insert(out.end(), ms[i].bytes.begin(), ms[i].bytes.end());
        if (out.size() & 1) out.push_back('\n');
    }
    be32(out, idx, u32(count));
    size_t slot = idx + 4, text = idx + 4 + 4 * count;
    for (size_t i = 0; i < ms.size(); i++)
        for (size_t k = 0; k < ms[i].defs.size(); k++) {
            if (owner[ms[i].defs[k]] != ms[i].name) continue;
            be32(out, slot, u32(at[i]));
            slot += 4;
            std::memcpy(&out[text], ms[i].defs[k].c_str(), ms[i].defs[k].size() + 1);
            text += ms[i].defs[k].size() + 1;
        }

    FILE *f = std::fopen(lib.c_str(), "wb");
    if (!f || std::fwrite(out.data(), 1, out.size(), f) != out.size()) {
        std::fprintf(stderr, "ar6x: %s: cannot write\n", lib.c_str());
        if (f) std::fclose(f);
        return 1;
    }
    std::fclose(f);
    return 0;
}

u32 rd32be(const u8 *p) { return (u32(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

// -t: each member's name and size, then the index as `symbol member`, from the file as written.
int list(const std::string &lib)
{
    std::vector<u8> d;
    if (!readFile(lib, d) || d.size() < 68 || std::memcmp(&d[0], "!<arch>\n", 8) != 0) {
        std::fprintf(stderr, "ar6x: %s: not an archive\n", lib.c_str());
        return 1;
    }
    std::map<size_t, std::string> names;
    size_t longsAt = 0, p = 8;
    std::vector<std::pair<std::string, size_t> > index;
    while (p + 60 <= d.size()) {
        std::string raw(reinterpret_cast<const char *>(&d[p]), 16);
        size_t size = std::strtoul(std::string(reinterpret_cast<const char *>(&d[p + 48]), 10).c_str(), 0, 10);
        const u8 *b = &d[p + 60];
        if (raw.compare(0, 2, "/ ") == 0) {
            u32 n = rd32be(b);
            const char *s = reinterpret_cast<const char *>(b + 4 + 4 * n);
            for (u32 i = 0; i < n; i++) { index.push_back(std::make_pair(std::string(s), size_t(rd32be(b + 4 + 4 * i)))); s += std::strlen(s) + 1; }
        } else if (raw.compare(0, 2, "//") == 0) {
            longsAt = p + 60;
        } else {
            std::string nm = raw.substr(0, raw.find('/'));
            if (raw[0] == '/' && longsAt) {
                const char *s = reinterpret_cast<const char *>(&d[longsAt + std::strtoul(raw.c_str() + 1, 0, 10)]);
                nm = std::string(s, std::strcspn(s, "/\n"));
            }
            names[p] = nm;
            std::printf("member %s %zu\n", nm.c_str(), size);
        }
        p += 60 + size + (size & 1);
    }
    for (size_t i = 0; i < index.size(); i++)
        std::printf("symbol %s %s\n", index[i].first.c_str(), names.count(index[i].second) ? names[index[i].second].c_str() : "?");
    return 0;
}

// An argument @list stands for the objects named in that file, one a line - cmd's line is 8191.
std::vector<std::string> expand(char **first, char **last)
{
    std::vector<std::string> out;
    for (; first != last; ++first) {
        if ((*first)[0] != '@') { out.push_back(*first); continue; }
        std::vector<u8> text;
        if (!readFile(*first + 1, text)) { std::fprintf(stderr, "ar6x: cannot read %s\n", *first + 1); std::exit(1); }
        std::string line;
        for (size_t i = 0; i <= text.size(); i++) {
            char c = i < text.size() ? char(text[i]) : '\n';
            if (c == '\n' || c == '\r') { if (!line.empty()) out.push_back(line); line.clear(); }
            else if (c != '"') line += c;
        }
    }
    return out;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc >= 2 && std::strcmp(argv[1], "--version") == 0) {
        std::printf("\xc2\xa9" "2026 G. R. Akhtar - ar6x 1.0, a TMS320C6000 archiver\n");
        return 0;
    }
    if (argc == 3 && std::strcmp(argv[1], "-t") == 0) return list(argv[2]);
    if (argc >= 3 && std::strcmp(argv[1], "-r") == 0)
        return create(argv[2], expand(argv + 3, argv + argc));
    std::fprintf(stderr, "usage: ar6x -r library.lib object.obj ...   (make the library from these objects, @file for a list)\n"
                         "       ar6x -t library.lib                  (list its members and its index)\n");
    return 2;
}
