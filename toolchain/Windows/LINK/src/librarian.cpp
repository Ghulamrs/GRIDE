/*  librarian.cpp - `link /LIB`: archives, the other half of what lib.exe is.
 *
 *  Two things are written here, both in the layout link.exe and lib.exe read (and archive.cpp
 *  reads back):
 *
 *    link /LIB /OUT:x.lib a.obj b.obj ...     a COFF archive of the objects, its two linker
 *                                             members indexing every external each defines
 *    link /LIB /DEF:x.def /OUT:x.lib          an import library for the DLL the .def names:
 *                                             the three descriptor members and one short
 *                                             import member per export, as the SDK's are made
 *
 *  `link /LIB /LIST x.lib` prints an archive's members and what each defines, which is what the
 *  Setup test reads to check a shipped library.
 *
 *  The layout: "!<arch>\n"; the first linker member (`/`: a big-endian count, the big-endian
 *  offset of the member defining each symbol, the names, in member order); the second linker
 *  member (`/`: a little-endian member count and offsets, a symbol count, each symbol's 1-based
 *  member number as 16 bits, the names in byte order - what link.exe searches by halving); the
 *  longnames member (`//`) when a member's name will not fit its header's sixteen bytes; then
 *  the members, each on an even offset. Every header's date is -1 and its mode 0, as the SDK's
 *  libraries are written, so the same objects make the same library.
 */
#include "link.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct Member {
    std::string name;                 /* what the member header names: the file's leaf */
    std::vector<u8> data;
    std::vector<std::string> syms;    /* the externals it defines, which the linker members index */
    std::vector<bool> comdat;         /* each in a COMDAT section: a second definition is expected, not warned of */
};

bool slurp(const std::string &path, std::vector<u8> &out, std::string &err)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) { err = path + ": cannot open"; return false; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    out.resize((size_t)(n > 0 ? n : 0));
    if (n > 0 && fread(&out[0], 1, (size_t)n, f) != (size_t)n) { fclose(f); err = path + ": short read"; return false; }
    fclose(f);
    return true;
}

std::string leaf(const std::string &path)
{
    size_t sl = path.find_last_of("/\\");
    return sl == std::string::npos ? path : path.substr(sl + 1);
}

std::string upper(const std::string &s)
{
    std::string o = s;
    for (size_t i = 0; i < o.size(); i++) if (o[i] >= 'a' && o[i] <= 'z') o[i] = (char)(o[i] - 'a' + 'A');
    return o;
}

/*  The externals an object defines: storage class EXTERNAL in a section, absolute, or common (a
 *  size in place of a section). An undefined one is a reference, and a weak external is not
 *  indexed - lib.exe's rule, and the one that keeps a member from being pulled for a name it only
 *  falls back on. */
bool object_symbols(const u8 *p, size_t n, const std::string &name, std::vector<std::string> &out,
                    std::vector<bool> &comdat, std::string &err)
{
    if (n < 20) { err = name + ": not a COFF object"; return false; }
    u16 machine = rd16(p), nsec = rd16(p + 2);
    if (machine == 0 && nsec == 0xFFFF) { err = name + ": an import member or a /bigobj object - not taken by /LIB"; return false; }
    if (!coff_is_object(p, n)) { err = name + ": not a COFF object"; return false; }
    u16 opt = rd16(p + 16);
    if ((u64)20 + opt + 40ull * nsec > n) { err = name + ": section headers run past the file"; return false; }
    u32 symptr = rd32(p + 8), nsym = rd32(p + 12);
    if (symptr == 0 || nsym == 0) return true;
    if ((u64)symptr + 18ull * nsym > n) { err = name + ": symbol table runs past the file"; return false; }
    const u8 *strtab = p + symptr + 18 * nsym;
    size_t strsize = (size_t)(n - (symptr + 18 * nsym));
    for (u32 i = 0; i < nsym; i++) {
        const u8 *e = p + symptr + 18 * i;
        u32 value = rd32(e + 8);
        short section = (short)rd16(e + 12);
        u8 storage = e[16], naux = e[17];
        if (storage == SYM_EXTERNAL && (section != 0 || value != 0)) {
            std::string s;
            if (rd32(e) == 0) {
                u32 off = rd32(e + 4);
                if (off >= strsize) { err = name + ": a symbol name past the string table"; return false; }
                const char *c = (const char *)strtab + off;
                s.assign(c, strnlen(c, strsize - off));
            } else {
                s.assign((const char *)e, strnlen((const char *)e, 8));
            }
            out.push_back(s);
            comdat.push_back(section > 0 && section <= (short)nsec && (rd32(p + 20 + opt + 40 * (section - 1) + 36) & SCN_LNK_COMDAT) != 0);
        }
        i += naux;
    }
    return true;
}

/* ---------------------------------------------------------------- the import library's members */

/*  A section header, a relocation and a symbol, appended in COFF's little-endian layout */
void put_section(std::vector<u8> &o, const char *name, u32 rawsize, u32 rawptr, u32 relptr, u16 nrel, u32 flags)
{
    size_t at = o.size();
    o.resize(at + 40, 0);
    memcpy(&o[at], name, strlen(name) > 8 ? 8 : strlen(name));
    wr32(&o[at + 16], rawsize);
    wr32(&o[at + 20], rawptr);
    wr32(&o[at + 24], relptr);
    wr16(&o[at + 32], nrel);
    wr32(&o[at + 36], flags);
}

void put_reloc(std::vector<u8> &o, u32 offset, u32 sym, u16 type)
{
    size_t at = o.size();
    o.resize(at + 10, 0);
    wr32(&o[at], offset); wr32(&o[at + 4], sym); wr16(&o[at + 8], type);
}

/*  A symbol: a name of eight bytes or fewer in place, a longer one in the string table */
void put_symbol(std::vector<u8> &o, std::vector<u8> &strtab, const std::string &name, u32 value, short section, u8 storage)
{
    size_t at = o.size();
    o.resize(at + 18, 0);
    if (name.size() <= 8) memcpy(&o[at], name.data(), name.size());
    else {
        wr32(&o[at + 4], (u32)(4 + strtab.size()));
        strtab.insert(strtab.end(), name.begin(), name.end());
        strtab.push_back(0);
    }
    wr32(&o[at + 8], value);
    wr16(&o[at + 12], (u16)section);
    o[at + 16] = storage;
}

void finish_object(std::vector<u8> &o, u32 symptr, u32 nsym, const std::vector<u8> &strtab)
{
    wr32(&o[8], symptr);
    wr32(&o[12], nsym);
    u8 sz[4]; wr32(sz, (u32)(4 + strtab.size()));
    o.insert(o.end(), sz, sz + 4);
    o.insert(o.end(), strtab.begin(), strtab.end());
}

void file_header(std::vector<u8> &o, u16 nsec)
{
    o.assign(20, 0);
    wr16(&o[0], 0x8664);
    wr16(&o[2], nsec);
}

/* u32 constants rather than enumerators: to cl an or of enumerators that fits in an int is an int (C4245) */
const u32 SECTION_VALUE = (u32)SCN_CNT_INITDATA | (u32)SCN_MEM_READ | (u32)SCN_MEM_WRITE;   /* a section symbol's value, as the SDK's */
const u32 IDATA_4BYTE = SECTION_VALUE | 0x00300000u;   /* .idata$2, $3 */
const u32 IDATA_2BYTE = SECTION_VALUE | 0x00200000u;   /* .idata$6 */
const u32 IDATA_8BYTE = SECTION_VALUE | 0x00400000u;   /* .idata$4, $5 */

/*  __IMPORT_DESCRIPTOR_<dll>: the directory entry (.idata$2, twenty bytes whose ILT, name and IAT
 *  words are relocations), the DLL's name (.idata$6), and references to the two members that end
 *  the directory and the DLL's thunks - the SDK's kernel32.lib, member by member. */
Member descriptor(const std::string &dll, const std::string &stem)
{
    Member m; m.name = dll;
    std::vector<u8> &o = m.data, strtab;
    file_header(o, 2);
    u32 name_size = (u32)dll.size() + 1; if (name_size & 1) name_size++;
    u32 sec2 = 20 + 2 * 40, rel2 = sec2 + 20, sec6 = rel2 + 3 * 10, symptr = sec6 + name_size;
    put_section(o, ".idata$2", 20, sec2, rel2, 3, IDATA_4BYTE);
    put_section(o, ".idata$6", name_size, sec6, 0, 0, IDATA_2BYTE);
    o.resize(o.size() + 20, 0);
    put_reloc(o, 12, 2, REL_ADDR32NB);        /* the name: .idata$6 */
    put_reloc(o, 0, 3, REL_ADDR32NB);         /* the import lookup table: .idata$4 */
    put_reloc(o, 16, 4, REL_ADDR32NB);        /* the address table: .idata$5 */
    size_t at = o.size();
    o.resize(at + name_size, 0);
    memcpy(&o[at], dll.data(), dll.size());
    put_symbol(o, strtab, "__IMPORT_DESCRIPTOR_" + stem, 0, 1, SYM_EXTERNAL);
    put_symbol(o, strtab, ".idata$2", SECTION_VALUE, 1, SYM_SECTION);
    put_symbol(o, strtab, ".idata$6", 0, 2, SYM_STATIC);
    put_symbol(o, strtab, ".idata$4", SECTION_VALUE, 0, SYM_SECTION);
    put_symbol(o, strtab, ".idata$5", SECTION_VALUE, 0, SYM_SECTION);
    put_symbol(o, strtab, "__NULL_IMPORT_DESCRIPTOR", 0, 0, SYM_EXTERNAL);
    put_symbol(o, strtab, std::string("\x7f") + stem + "_NULL_THUNK_DATA", 0, 0, SYM_EXTERNAL);
    finish_object(o, symptr, 7, strtab);
    m.syms.push_back("__IMPORT_DESCRIPTOR_" + stem);
    return m;
}

/* __NULL_IMPORT_DESCRIPTOR: the twenty zero bytes that end the import directory */
Member null_descriptor(const std::string &dll)
{
    Member m; m.name = dll;
    std::vector<u8> &o = m.data, strtab;
    file_header(o, 1);
    u32 sec = 20 + 40, symptr = sec + 20;
    put_section(o, ".idata$3", 20, sec, 0, 0, IDATA_4BYTE);
    o.resize(o.size() + 20, 0);
    put_symbol(o, strtab, "__NULL_IMPORT_DESCRIPTOR", 0, 1, SYM_EXTERNAL);
    finish_object(o, symptr, 1, strtab);
    m.syms.push_back("__NULL_IMPORT_DESCRIPTOR");
    return m;
}

/* \x7f<dll>_NULL_THUNK_DATA: the zero words that end this DLL's address table and lookup table */
Member null_thunk(const std::string &dll, const std::string &stem)
{
    Member m; m.name = dll;
    std::vector<u8> &o = m.data, strtab;
    file_header(o, 2);
    u32 sec5 = 20 + 2 * 40, sec4 = sec5 + 8, symptr = sec4 + 8;
    put_section(o, ".idata$5", 8, sec5, 0, 0, IDATA_8BYTE);
    put_section(o, ".idata$4", 8, sec4, 0, 0, IDATA_8BYTE);
    o.resize(o.size() + 16, 0);
    std::string thunk = std::string("\x7f") + stem + "_NULL_THUNK_DATA";
    put_symbol(o, strtab, thunk, 0, 1, SYM_EXTERNAL);
    finish_object(o, symptr, 1, strtab);
    m.syms.push_back(thunk);
    return m;
}

/*  One export: the short import member - twenty bytes of header, then the name and the DLL's.
 *  Imported by name (name type 1, the name as the symbol has it); code by default, so the linker
 *  makes the jump thunk under the plain name, data when the .def says DATA (only __imp_ then). */
Member short_member(const std::string &dll, const std::string &sym, u16 hint, bool data)
{
    Member m; m.name = dll;
    std::vector<u8> &o = m.data;
    u32 size = (u32)(sym.size() + 1 + dll.size() + 1);
    o.assign(20, 0);
    wr16(&o[0], 0); wr16(&o[2], 0xFFFF); wr16(&o[4], 0); wr16(&o[6], 0x8664);
    wr32(&o[12], size);
    wr16(&o[16], hint);
    wr16(&o[18], (u16)((1 << 2) | (data ? 1 : 0)));
    o.insert(o.end(), sym.begin(), sym.end()); o.push_back(0);
    o.insert(o.end(), dll.begin(), dll.end()); o.push_back(0);
    m.syms.push_back("__imp_" + sym);
    if (!data) m.syms.push_back(sym);
    return m;
}

/*  The .def file, as much of it as an import library needs: LIBRARY (or NAME) and the EXPORTS -
 *  `name[=internal] [@ordinal [NONAME]] [PRIVATE] [DATA]`, `;` to the end of a line a comment. A
 *  PRIVATE export is left out of the import library, as lib.exe leaves it. */
struct Export { std::string name; bool data; };

bool read_def(const std::string &path, std::string &dll, std::vector<Export> &exports, std::string &err)
{
    std::vector<u8> bytes;
    if (!slurp(path, bytes, err)) return false;
    std::string text(bytes.begin(), bytes.end());
    bool in_exports = false;
    size_t at = 0;
    int lineno = 0;
    while (at < text.size()) {
        size_t nl = text.find('\n', at);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(at, nl - at);
        at = nl + 1;
        lineno++;
        size_t semi = line.find(';');
        if (semi != std::string::npos) line.erase(semi);
        std::vector<std::string> w;
        size_t i = 0;
        while (i < line.size()) {
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) i++;
            if (i >= line.size()) break;
            size_t j = i;
            if (line[i] == '"') {
                j = line.find('"', i + 1);
                if (j == std::string::npos) j = line.size();
                w.push_back(line.substr(i + 1, j - i - 1));
                i = j + 1;
                continue;
            }
            while (j < line.size() && line[j] != ' ' && line[j] != '\t' && line[j] != '\r') j++;
            w.push_back(line.substr(i, j - i));
            i = j;
        }
        if (w.empty()) continue;
        std::string key = upper(w[0]);
        if (key == "LIBRARY" || key == "NAME") {
            in_exports = false;
            if (w.size() > 1) dll = w[1];
            continue;
        }
        if (key == "EXPORTS") { in_exports = true; w.erase(w.begin()); if (w.empty()) continue; }
        else if (key == "DESCRIPTION" || key == "HEAPSIZE" || key == "STACKSIZE" || key == "SECTIONS" || key == "VERSION") {
            in_exports = false;
            continue;
        }
        if (!in_exports) {
            char n[16]; snprintf(n, sizeof n, "%d", lineno);
            err = path + "(" + n + "): '" + w[0] + "' is not a statement this librarian knows";
            return false;
        }
        Export e;
        e.name = w[0];
        size_t eq = e.name.find('=');
        if (eq != std::string::npos) e.name.erase(eq);
        e.data = false;
        bool priv = false;
        for (size_t k = 1; k < w.size(); k++) {
            std::string u = upper(w[k]);
            if (u == "DATA" || u == "CONSTANT") e.data = true;
            else if (u == "PRIVATE") priv = true;
        }
        if (!priv && !e.name.empty()) exports.push_back(e);
    }
    if (dll.empty()) { err = path + ": no LIBRARY statement names the DLL"; return false; }
    if (dll.find('.') == std::string::npos) dll += ".dll";
    return true;
}

/* ---------------------------------------------------------------- the archive */

void header(std::vector<u8> &o, const std::string &name, size_t size)
{
    char h[61];
    snprintf(h, sizeof h, "%-16s%-12s%-6s%-6s%-8s%-10u`\n", name.c_str(), "-1", "", "", "0", (unsigned)size);
    o.insert(o.end(), h, h + 60);
}

void be32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

bool write_archive(const std::string &out, std::vector<Member> &members, std::string &err)
{
    /*  The index: each name once, from the first member that defines it. A name two members define is
     *  said, unless both are COMDATs - an inline function each translation unit emits - which the linker
     *  settles, whichever member it pulls. */
    std::vector<std::pair<std::string, size_t> > index;
    std::map<std::string, std::pair<size_t, bool> > seen;
    for (size_t i = 0; i < members.size(); i++)
        for (size_t k = 0; k < members[i].syms.size(); k++) {
            const std::string &s = members[i].syms[k];
            bool comdat = k < members[i].comdat.size() && members[i].comdat[k];
            std::map<std::string, std::pair<size_t, bool> >::iterator f = seen.find(s);
            if (f != seen.end()) {
                if (f->second.first != i && !(f->second.second && comdat))
                    fprintf(stderr, "link: warning: %s defined in %s and again in %s; the second is not indexed\n",
                            s.c_str(), members[f->second.first].name.c_str(), members[i].name.c_str());
                continue;
            }
            seen[s] = std::make_pair(i, comdat);
            index.push_back(std::make_pair(s, i));
        }

    /* the longnames member, and each member's header name */
    std::vector<u8> longnames;
    std::vector<std::string> names(members.size());
    for (size_t i = 0; i < members.size(); i++) {
        const std::string &n = members[i].name;
        if (n.size() < 16) { names[i] = n + "/"; continue; }
        char b[16]; snprintf(b, sizeof b, "/%u", (unsigned)longnames.size());
        names[i] = b;
        longnames.insert(longnames.end(), n.begin(), n.end());
        longnames.push_back(0);
    }

    size_t names_size = 0;
    for (size_t k = 0; k < index.size(); k++) names_size += index[k].first.size() + 1;
    size_t first_size = 4 + 4 * index.size() + names_size;
    size_t second_size = 4 + 4 * members.size() + 4 + 2 * index.size() + names_size;
    size_t at = 8 + 60 + first_size; at += at & 1;
    at += 60 + second_size; at += at & 1;
    if (!longnames.empty()) { at += 60 + longnames.size(); at += at & 1; }
    std::vector<u32> offsets(members.size());
    for (size_t i = 0; i < members.size(); i++) {
        offsets[i] = (u32)at;
        at += 60 + members[i].data.size(); at += at & 1;
    }
    if (at > 0xFFFFFFFFull) { err = out + ": the archive would pass 4 GB"; return false; }

    std::vector<u8> o;
    o.reserve(at);
    const char *magic = "!<arch>\n";
    o.insert(o.end(), magic, magic + 8);

    /* first linker member: member order */
    header(o, "/", first_size);
    size_t p = o.size();
    o.resize(p + 4 + 4 * index.size());
    be32(&o[p], (u32)index.size());
    for (size_t k = 0; k < index.size(); k++) be32(&o[p + 4 + 4 * k], offsets[index[k].second]);
    for (size_t k = 0; k < index.size(); k++) { o.insert(o.end(), index[k].first.begin(), index[k].first.end()); o.push_back(0); }
    if (o.size() & 1) o.push_back('\n');

    /* second linker member: the names in byte order */
    std::vector<std::pair<std::string, size_t> > sorted(index);
    std::sort(sorted.begin(), sorted.end());
    header(o, "/", second_size);
    p = o.size();
    o.resize(p + 4 + 4 * members.size() + 4 + 2 * sorted.size());
    wr32(&o[p], (u32)members.size());
    for (size_t i = 0; i < members.size(); i++) wr32(&o[p + 4 + 4 * i], offsets[i]);
    size_t q = p + 4 + 4 * members.size();
    wr32(&o[q], (u32)sorted.size());
    for (size_t k = 0; k < sorted.size(); k++) wr16(&o[q + 4 + 2 * k], (u16)(sorted[k].second + 1));
    for (size_t k = 0; k < sorted.size(); k++) { o.insert(o.end(), sorted[k].first.begin(), sorted[k].first.end()); o.push_back(0); }
    if (o.size() & 1) o.push_back('\n');

    if (!longnames.empty()) {
        header(o, "//", longnames.size());
        o.insert(o.end(), longnames.begin(), longnames.end());
        if (o.size() & 1) o.push_back('\n');
    }
    for (size_t i = 0; i < members.size(); i++) {
        if (o.size() != offsets[i]) { err = out + ": member offsets went wrong"; return false; }
        header(o, names[i], members[i].data.size());
        o.insert(o.end(), members[i].data.begin(), members[i].data.end());
        if (o.size() & 1) o.push_back('\n');
    }
    if (members.size() > 0xFFFF) { err = out + ": more than 65,535 members"; return false; }

    FILE *f = fopen(out.c_str(), "wb");
    if (!f) { err = out + ": cannot write"; return false; }
    bool ok = fwrite(&o[0], 1, o.size(), f) == o.size();
    ok = fclose(f) == 0 && ok;
    if (!ok) { err = out + ": write failed"; remove(out.c_str()); return false; }
    return true;
}

/*  /LIST: each member and what it defines, from the first linker member - for the Setup test and
 *  for a person asking what a library holds. */
int list_archive(const std::string &path)
{
    Archive ar;
    std::string err;
    if (!ar.load(path, err)) { fprintf(stderr, "link: %s\n", err.c_str()); return 1; }
    std::map<u32, std::vector<std::string> > by;
    for (size_t k = 0; k < ar.index.size(); k++) by[ar.index[k].second].push_back(ar.index[k].first);
    for (std::map<u32, std::vector<std::string> >::iterator it = by.begin(); it != by.end(); ++it) {
        Module m;
        std::string name;
        if (ar.member(it->first, path, m, err)) name = m.name; else name = "?";
        printf("%s\n", name.c_str());
        for (size_t k = 0; k < it->second.size(); k++) {
            std::string s = printable(it->second[k]);
            printf("    %s\n", s.c_str());
        }
    }
    return 0;
}

bool eat(const std::string &a, const char *sw, std::string &val)
{
    size_t n = strlen(sw);
    if (a.size() < n + 1 || (a[0] != '/' && a[0] != '-')) return false;
    for (size_t i = 0; i < n; i++) {
        char c = a[i + 1];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != sw[i]) return false;
    }
    val = a.substr(n + 1);
    return true;
}

} /* namespace */

int librarian(const std::vector<std::string> &args)
{
    std::string out, def, name, v;
    std::vector<std::string> inputs;
    bool list = false;
    for (size_t i = 0; i < args.size(); i++) {
        const std::string &a = args[i];
        if (eat(a, "out:", v))           { out = v; continue; }
        if (eat(a, "def:", v))           { def = v; continue; }
        if (eat(a, "name:", v))          { name = v; continue; }
        if (eat(a, "list", v) && v.empty()) { list = true; continue; }
        if (eat(a, "nologo", v) || eat(a, "machine:", v) || eat(a, "ignore:", v) || eat(a, "verbose", v) ||
            eat(a, "wx", v) || eat(a, "ltcg", v) || eat(a, "errorreport:", v) || eat(a, "brepro", v)) continue;
        if (!a.empty() && (a[0] == '/' || a[0] == '-')) {
            FILE *f = a[0] == '/' ? fopen(a.c_str(), "rb") : 0;
            if (!f) { fprintf(stderr, "link /LIB: unknown switch %s\n", a.c_str()); return 2; }
            fclose(f);
        }
        inputs.push_back(a);
    }
    if (list) {
        if (inputs.size() != 1) { fprintf(stderr, "usage: link /LIB /LIST library.lib\n"); return 2; }
        return list_archive(inputs[0]);
    }
    std::string err;
    std::vector<Member> members;
    if (!def.empty()) {
        if (!inputs.empty()) { fprintf(stderr, "link /LIB: /DEF: makes an import library and takes no objects\n"); return 2; }
        std::string dll;
        std::vector<Export> exports;
        if (!read_def(def, dll, exports, err)) { fprintf(stderr, "link: %s\n", err.c_str()); return 1; }
        if (!name.empty()) dll = name;
        if (out.empty()) {
            out = def;
            size_t d = out.rfind('.'), sl = out.find_last_of("/\\");
            if (d != std::string::npos && (sl == std::string::npos || d > sl)) out.erase(d);
            out += ".lib";
        }
        std::string stem = dll.substr(0, dll.find('.'));
        members.push_back(descriptor(dll, stem));
        members.push_back(null_descriptor(dll));
        members.push_back(null_thunk(dll, stem));
        /* the hint is each name's place among the exports in byte order, which is lib.exe's guess */
        std::vector<std::string> byname;
        for (size_t i = 0; i < exports.size(); i++) byname.push_back(exports[i].name);
        std::sort(byname.begin(), byname.end());
        for (size_t i = 0; i < exports.size(); i++) {
            u16 hint = (u16)(std::lower_bound(byname.begin(), byname.end(), exports[i].name) - byname.begin());
            members.push_back(short_member(dll, exports[i].name, hint, exports[i].data));
        }
    } else {
        if (inputs.empty()) {
            fprintf(stderr, "usage: link /LIB /OUT:library.lib object.obj...\n"
                            "       link /LIB /DEF:dll.def [/NAME:dll] /OUT:import.lib\n"
                            "       link /LIB /LIST library.lib\n");
            return 2;
        }
        if (out.empty()) {
            out = inputs[0];
            size_t d = out.rfind('.'), sl = out.find_last_of("/\\");
            if (d != std::string::npos && (sl == std::string::npos || d > sl)) out.erase(d);
            out += ".lib";
        }
        for (size_t i = 0; i < inputs.size(); i++) {
            Member m;
            m.name = leaf(inputs[i]);
            if (!slurp(inputs[i], m.data, err) || !object_symbols(m.data.empty() ? 0 : &m.data[0], m.data.size(), inputs[i], m.syms, m.comdat, err)) {
                fprintf(stderr, "link: %s\n", err.c_str());
                return 1;
            }
            members.push_back(m);
        }
    }
    if (!write_archive(out, members, err)) { fprintf(stderr, "link: %s\n", err.c_str()); return 1; }
    return 0;
}
