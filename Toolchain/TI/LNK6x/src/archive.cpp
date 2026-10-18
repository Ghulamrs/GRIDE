/*  archive.cpp - a `ar` library, and the members a link pulls out of it.
 *
 *  The format is SysV's, which is what CCS 7.4's ar6x writes: the magic, then a first member
 *  named `/` holding the symbol index - a big-endian count, that many big-endian member
 *  offsets, and then that many NUL-terminated names in the same order - then, when any member
 *  name is longer than fifteen characters, a member named `//` holding those names, which the
 *  short header then points into as `/offset`. rts6740_elf_eh.lib has 456 members and 4,415
 *  index entries; q10.lib has two members and two names, and is the bed's probe for all of it.
 */
#include "lnk.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static u32 be32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

/* a header's size field is ten characters of decimal, space-padded */
static u32 header_size(const u8 *h)
{
    char s[11];
    memcpy(s, h + 48, 10);
    s[10] = 0;
    return (u32)strtoul(s, 0, 10);
}

bool Archive::load(const std::string &path, std::string &err)
{
    name = path;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) { err = path + ": cannot open"; return false; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    bytes.resize((size_t)(n > 0 ? n : 0));
    if (n > 0 && fread(&bytes[0], 1, (size_t)n, f) != (size_t)n)
        { fclose(f); err = path + ": short read"; return false; }
    fclose(f);

    if (bytes.size() < 8 || memcmp(&bytes[0], "!<arch>\n", 8) != 0)
        { err = path + ": not an archive"; return false; }

    size_t p = 8;
    if (p + 60 > bytes.size()) { err = path + ": no symbol index"; return false; }
    const u8 *h = &bytes[p];
    if (h[0] != '/' || h[1] == '/') { err = path + ": no symbol index"; return false; }
    u32 isize = header_size(h);
    size_t body = p + 60;
    if (body + isize > bytes.size() || isize < 4) { err = path + ": broken symbol index"; return false; }

    u32 count = be32(&bytes[body]);
    if (4 + (u64)count * 4 > isize) { err = path + ": broken symbol index"; return false; }
    size_t at = body + 4 + (size_t)count * 4;
    for (u32 i = 0; i < count; i++) {
        size_t e = at;
        while (e < body + isize && bytes[e]) e++;
        if (e >= body + isize) { err = path + ": broken symbol index"; return false; }
        index.push_back(std::make_pair(std::string((const char *)&bytes[at], e - at),
                                       be32(&bytes[body + 4 + 4 * i])));
        first.emplace(index.back().first, index.back().second);
        at = e + 1;
    }

    /* the long-name member, `//`, when there is one: it follows the index */
    size_t q = body + isize; if (q & 1) q++;
    if (q + 60 <= bytes.size() && bytes[q] == '/' && bytes[q + 1] == '/') longnames_at = q + 60;
    return true;
}

bool Archive::member(u32 off, Module &m, std::string &err) const
{
    if ((size_t)off + 60 > bytes.size()) { err = name + ": a member lies past the end"; return false; }
    const u8 *h = &bytes[off];
    u32 msize = header_size(h);
    const u8 *d = h + 60;
    if ((size_t)(off + 60) + msize > bytes.size()) { err = name + ": a member runs past the end"; return false; }

    /*  The member's own name, for the map and for a diagnostic: `name/`, or `/offset` into
     *  the long-name member. */
    char raw[17]; memcpy(raw, h, 16); raw[16] = 0;
    std::string leaf;
    if (raw[0] == '/' && raw[1] >= '0' && raw[1] <= '9' && longnames_at) {
        size_t o = longnames_at + (size_t)strtoul(raw + 1, 0, 10);
        size_t k = 0;
        while (o + k < bytes.size() && bytes[o + k] && bytes[o + k] != '/' && bytes[o + k] != '\n') k++;
        leaf.assign((const char *)&bytes[o], k);
    }
    if (leaf.empty()) {
        char *sl = strchr(raw, '/'); if (sl) *sl = 0;
        for (int k = (int)strlen(raw) - 1; k >= 0 && raw[k] == ' '; k--) raw[k] = 0;
        leaf = raw;
    }
    return elf_read(d, msize, leaf, m, err);
}

/*  A `-l name` is opened as given first - RIDE gives none with a directory, but a command line
 *  may - and then looked for in each `-i` directory in the order they were written, which is
 *  what lnk6x does with the two RIDE passes it. The separator is the one the directory itself
 *  spells, and `/` when it spells neither; a Windows path takes `/` too. */
std::string find_library(const Options &o, const std::string &nm)
{
    FILE *f = fopen(nm.c_str(), "rb");
    if (f) { fclose(f); return nm; }
    if (nm.find_first_of("/\\") != std::string::npos) return std::string();
    for (size_t i = 0; i < o.libdirs.size(); i++) {
        std::string t = o.libdirs[i];
        if (t.empty()) continue;
        char last = t[t.size() - 1];
        if (last != '/' && last != '\\')
            t += (t.find('\\') != std::string::npos && t.find('/') == std::string::npos) ? '\\' : '/';
        t += nm;
        f = fopen(t.c_str(), "rb");
        if (f) { fclose(f); return t; }
    }
    return std::string();
}
