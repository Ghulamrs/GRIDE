#include "asm.h"

#include <cstdio>
#include <cstring>

/* The object as asm6x 8.2.2 writes it: ELF32, little-endian, EM_TI_C6000 (140), one
   section header per section the source opened in the order it opened them with .text
   first, then .c6xabi.attributes, .symtab, the exidx tables (asm6x adds those after the
   symbol table, each linked to the code section its name ends with), a .rela.<sec> for the
   MVK/MVKL/MVKH halves and a .rel.<sec> for every other relocation kind, the two TI
   bookkeeping sections, .strtab and .shstrtab. Every symbol is STV_HIDDEN, as asm6x marks
   them; the build attributes are the bytes asm6x writes for --abi=eabi -mv6740, which is
   what lnk6x checks a file against. */

namespace {

struct Out {
    std::vector<unsigned char> b;
    void u8(unsigned v) { b.push_back((unsigned char)v); }
    void u16(unsigned v) { u8(v); u8(v >> 8); }
    void u32(unsigned long v) { u16((unsigned)(v & 0xFFFF)); u16((unsigned)(v >> 16)); }
    void bytes(const std::vector<unsigned char> &v) { b.insert(b.end(), v.begin(), v.end()); }
    void pad4() { while (b.size() % 4) u8(0); }
    size_t size() const { return b.size(); }
};

struct Strtab {
    std::vector<unsigned char> b;
    Strtab() { b.push_back(0); }
    unsigned long add(const std::string &s) {
        unsigned long at = (unsigned long)b.size();
        b.insert(b.end(), s.begin(), s.end());
        b.push_back(0);
        return at;
    }
};

struct Shdr {
    unsigned long name, type, flags, addr, offset, size, link, info, align, entsize;
};

/* the build attributes asm6x 8.2.2 writes: "A", the TI vendor block (producer "Assembler"
   9.3.3, ISA C6740, EABI, no long in the interface, any bit-field layout) and the c6xabi
   block (ISA C6740) */
const unsigned char kAttributes[0x3c] = {
    0x41, 0x29, 0x00, 0x00, 0x00, 0x54, 0x49, 0x00, 0x01, 0x22, 0x00, 0x00, 0x00, 0x05, 0x41, 0x73,
    0x73, 0x65, 0x6d, 0x62, 0x6c, 0x65, 0x72, 0x00, 0x08, 0x09, 0x0a, 0x03, 0x0c, 0x03, 0x80, 0x02,
    0x08, 0x82, 0x02, 0x02, 0x84, 0x02, 0x03, 0x8e, 0x02, 0x02, 0x12, 0x00, 0x00, 0x00, 0x63, 0x36,
    0x78, 0x61, 0x62, 0x69, 0x00, 0x01, 0x07, 0x00, 0x00, 0x00, 0x04, 0x08
};
const unsigned char kSymbolAlias[9] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x54, 0x49, 0x00 };
const unsigned char kSectionFlags[0x1a] = { 0x01, 0 };

bool rela_kind(RelKind k) { return k == R_ABS_L16 || k == R_ABS_H16 || k == R_ABS_S16; }

}

bool write_elf(const Unit &u, const std::string &path, std::string &err)
{
    /* where each section's header lands: the source's sections in order with the exidx
       tables held back until after .symtab, as asm6x places them */
    size_t nsec = u.sections.size();
    std::vector<int> shndx(nsec, 0);
    std::vector<size_t> exidx;
    {
        int next = 1;
        for (size_t i = 0; i < nsec; i++) {
            if (u.sections[i].kind == SEC_UNWIND) exidx.push_back(i);
            else shndx[i] = next++;
        }
        next += 2;   /* .c6xabi.attributes, .symtab */
        for (size_t k = 0; k < exidx.size(); k++) shndx[exidx[k]] = next++;
    }

    /* the symbols asm6x writes: the file, every local that is defined, one per section, then
       the globals, weak and undefined - a .global of a symbol neither defined nor used is
       omitted, as asm6x omits it - with our index for each */
    std::vector<int> index(u.symbols.size(), -1);
    std::vector<int> secsym(nsec, -1);
    Strtab strtab;
    Out sym;
    int count = 0;
    /* null */
    for (int k = 0; k < 4; k++) sym.u32(0);
    count++;
    /* the file */
    sym.u32(strtab.add(u.source)); sym.u32(0); sym.u32(0); sym.u8(4); sym.u8(2); sym.u16(0xFFF1);
    count++;
    std::vector<size_t> locals_by_order;
    for (size_t i = 0; i < u.symbols.size(); i++)
        if (u.symbols[i].bind == B_LOCAL && u.symbols[i].defined) locals_by_order.push_back(i);
    for (size_t a = 1; a < locals_by_order.size(); a++)     /* by definition, as asm6x lists them */
        for (size_t b = a; b > 0 && u.symbols[locals_by_order[b - 1]].order > u.symbols[locals_by_order[b]].order; b--) {
            size_t t = locals_by_order[b - 1]; locals_by_order[b - 1] = locals_by_order[b]; locals_by_order[b] = t;
        }
    for (size_t k = 0; k < locals_by_order.size(); k++) {
        size_t i = locals_by_order[k];
        const Symbol &s = u.symbols[i];
        index[i] = count++;
        sym.u32(strtab.add(s.name)); sym.u32((unsigned long)s.value); sym.u32(s.sized ? (unsigned long)s.size : 0);
        /* a .set constant is an absolute symbol of no type, as asm6x writes it */
        sym.u8(s.section < 0 ? 0 : (unsigned)s.type); sym.u8(2); sym.u16(s.section < 0 ? 0xFFF1 : (unsigned)shndx[s.section]);
    }
    for (size_t i = 0; i < nsec; i++) {
        secsym[i] = count++;
        sym.u32(strtab.add(u.sections[i].name)); sym.u32(0); sym.u32(0);
        sym.u8(SYM_SECTION); sym.u8(2); sym.u16((unsigned)shndx[i]);
    }
    int locals = count;
    for (size_t i = 0; i < u.symbols.size(); i++) {
        const Symbol &s = u.symbols[i];
        if (s.bind == B_LOCAL && (s.defined || !s.referenced)) continue;
        if (s.bind == B_LOCAL && !s.defined) { err = "'" + s.name + "' is not defined"; return false; }
        if (!s.defined && !s.referenced && s.bind == B_GLOBAL) continue;
        index[i] = count++;
        unsigned bind = s.bind == B_WEAK ? 2 : 1;
        unsigned type = s.defined && s.section >= 0 ? (unsigned)s.type : 0;
        sym.u32(strtab.add(s.name)); sym.u32(s.defined ? (unsigned long)s.value : 0); sym.u32((unsigned long)s.size);
        sym.u8((bind << 4) | type); sym.u8(2); sym.u16(!s.defined ? 0 : s.section < 0 ? 0xFFF1 : (unsigned)shndx[s.section]);
    }

    /* the section headers, in asm6x's order */
    std::vector<Shdr> hdrs;
    std::vector<std::vector<unsigned char> > bodies;
    Strtab shstr;
    Shdr null; memset(&null, 0, sizeof null);
    hdrs.push_back(null); bodies.push_back(std::vector<unsigned char>());
    for (size_t i = 0; i < nsec; i++) {
        const Section &s = u.sections[i];
        if (s.kind == SEC_UNWIND) continue;
        Shdr h; memset(&h, 0, sizeof h);
        h.name = shstr.add(s.name);
        h.type = (unsigned long)s.kind;
        h.flags = (s.alloc ? 2 : 0) | (s.writable && !s.code ? 1 : 0) | (s.code ? 4 : 0);
        h.size = (unsigned long)s.bytes.size();
        h.align = (unsigned long)s.align;
        if (shndx[i] != (int)hdrs.size()) { err = "internal: section order"; return false; }
        hdrs.push_back(h);
        bodies.push_back(s.bss ? std::vector<unsigned char>() : s.bytes);
    }
    {
        Shdr h; memset(&h, 0, sizeof h);
        h.name = shstr.add(".c6xabi.attributes"); h.type = 0x70000003; h.size = sizeof kAttributes;
        hdrs.push_back(h); bodies.push_back(std::vector<unsigned char>(kAttributes, kAttributes + sizeof kAttributes));
    }
    size_t symtab = hdrs.size();
    {
        Shdr h; memset(&h, 0, sizeof h);
        h.name = shstr.add(".symtab"); h.type = 2; h.size = (unsigned long)sym.size(); h.info = (unsigned long)locals; h.entsize = 16;
        hdrs.push_back(h); bodies.push_back(sym.b);
    }
    for (size_t k = 0; k < exidx.size(); k++) {
        const Section &s = u.sections[exidx[k]];
        Shdr h; memset(&h, 0, sizeof h);
        h.name = shstr.add(s.name);
        h.type = (unsigned long)s.kind;
        h.flags = 0x82;   /* SHF_ALLOC | SHF_LINK_ORDER */
        h.size = (unsigned long)s.bytes.size(); h.entsize = 8;     /* two words an entry */
        h.align = (unsigned long)s.align;
        /* linked to the code section its name ends with, wherever that was opened; a plain
           .c6xabi.exidx to .text */
        size_t colon = s.name.find(':');
        std::string code = colon == std::string::npos ? std::string(".text") : s.name.substr(colon + 1);
        int linked = -1;
        for (size_t i = 0; i < nsec; i++) if (u.sections[i].name == code && u.sections[i].kind != SEC_UNWIND) linked = (int)i;
        if (linked < 0) { err = "'" + s.name + "' describes '" + code + "', which this file does not have"; return false; }
        h.link = (unsigned long)shndx[linked];
        if (shndx[exidx[k]] != (int)hdrs.size()) { err = "internal: exidx order"; return false; }
        hdrs.push_back(h);
        bodies.push_back(s.bytes);
    }
    /* the relocation sections, in the order the source first needed one for a section, the
       entries of each by offset; the .symdepend entries are R_C6000_NONE at offset 0 */
    struct Group { size_t section; bool rela; unsigned long first; };
    std::vector<Group> groups;
    std::vector<std::pair<int, int> > depends;      /* .symdepend, its section found by name */
    for (size_t k = 0; k < u.depends.size(); k++) {
        int sec = -1;
        for (size_t i = 0; i < nsec; i++) if (u.sections[i].name == u.depends[k].second) sec = (int)i;
        if (sec < 0) { err = "'" + u.depends[k].second + "' is not a section of this file"; return false; }
        depends.push_back(std::make_pair(u.depends[k].first, sec));
    }
    for (size_t i = 0; i < nsec; i++) {
        const Section &s = u.sections[i];
        Group ga = { i, true, ~0ul }, gr = { i, false, ~0ul };
        for (size_t k = 0; k < s.relocs.size(); k++) {
            Group &g = rela_kind(s.relocs[k].kind) ? ga : gr;
            if (s.relocs[k].order < g.first) g.first = s.relocs[k].order;
        }
        for (size_t k = 0; k < depends.size(); k++)
            if (depends[k].second == (int)i && gr.first == ~0ul) gr.first = ~1ul;
        if (ga.first != ~0ul) groups.push_back(ga);
        if (gr.first != ~0ul) groups.push_back(gr);
    }
    for (size_t a = 1; a < groups.size(); a++)
        for (size_t b = a; b > 0 && groups[b - 1].first > groups[b].first; b--) {
            Group t = groups[b - 1]; groups[b - 1] = groups[b]; groups[b] = t;
        }
    for (size_t gi = 0; gi < groups.size(); gi++) {
        size_t i = groups[gi].section;
        const Section &s = u.sections[i];
        Out rela, rel;
        std::vector<Reloc> sorted(s.relocs);
        for (size_t k = 0; k < depends.size(); k++)
            if (depends[k].second == (int)i) {
                Reloc r; r.offset = 0; r.symbol = depends[k].first; r.section = -1; r.kind = R_NONE; r.addend = 0; r.order = ~1ul;
                sorted.push_back(r);
            }
        for (size_t a = 1; a < sorted.size(); a++)      /* a stable sort by offset */
            for (size_t b = a; b > 0 && sorted[b - 1].offset > sorted[b].offset; b--) {
                Reloc t = sorted[b - 1]; sorted[b - 1] = sorted[b]; sorted[b] = t;
            }
        for (size_t k = 0; k < sorted.size(); k++) {
            const Reloc &r = sorted[k];
            if (rela_kind(r.kind) != groups[gi].rela) continue;
            int si = r.symbol < 0 ? secsym[r.section] : index[r.symbol];
            if (si < 0) { err = "no symbol for a relocation against '" + u.symbols[r.symbol].name + "'"; return false; }
            Out &o = rela_kind(r.kind) ? rela : rel;
            o.u32(r.offset);
            o.u32(((unsigned long)si << 8) | ((unsigned long)r.kind & 0xFF));
            if (rela_kind(r.kind)) o.u32((unsigned long)r.addend);
        }
        if (rela.size()) {
            Shdr h; memset(&h, 0, sizeof h);
            h.name = shstr.add(".rela" + s.name); h.type = 4; h.size = (unsigned long)rela.size();
            h.link = (unsigned long)symtab; h.info = (unsigned long)shndx[i]; h.entsize = 12;
            hdrs.push_back(h); bodies.push_back(rela.b);
        }
        if (rel.size()) {
            Shdr h; memset(&h, 0, sizeof h);
            h.name = shstr.add(".rel" + s.name); h.type = 9; h.size = (unsigned long)rel.size();
            h.link = (unsigned long)symtab; h.info = (unsigned long)shndx[i]; h.entsize = 8;
            hdrs.push_back(h); bodies.push_back(rel.b);
        }
    }
    {
        Shdr h; memset(&h, 0, sizeof h);
        h.name = shstr.add(".TI.symbol.alias"); h.type = 0x7f000006; h.size = sizeof kSymbolAlias; h.entsize = 8;
        hdrs.push_back(h); bodies.push_back(std::vector<unsigned char>(kSymbolAlias, kSymbolAlias + sizeof kSymbolAlias));
    }
    {
        Shdr h; memset(&h, 0, sizeof h);
        h.name = shstr.add(".TI.section.flags"); h.type = 0x7f000005; h.size = sizeof kSectionFlags;
        hdrs.push_back(h); bodies.push_back(std::vector<unsigned char>(kSectionFlags, kSectionFlags + sizeof kSectionFlags));
    }
    size_t strtab_index = hdrs.size();
    {
        Shdr h; memset(&h, 0, sizeof h);
        h.name = shstr.add(".strtab"); h.type = 3; h.flags = 0x20; h.entsize = 1; h.size = (unsigned long)strtab.b.size();
        hdrs.push_back(h); bodies.push_back(strtab.b);
    }
    hdrs[symtab].link = (unsigned long)strtab_index;
    size_t shstrtab_index = hdrs.size();
    {
        Shdr h; memset(&h, 0, sizeof h);
        h.name = shstr.add(".shstrtab"); h.type = 3; h.flags = 0x20; h.entsize = 1;
        hdrs.push_back(h); bodies.push_back(std::vector<unsigned char>());
    }
    hdrs[shstrtab_index].size = (unsigned long)shstr.b.size();
    bodies[shstrtab_index] = shstr.b;

    /* lay the bodies out after the header, each at a 4-byte boundary, then the headers */
    Out f;
    static const unsigned char ident[16] = { 0x7f, 'E', 'L', 'F', 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    for (int k = 0; k < 16; k++) f.u8(ident[k]);
    f.u16(1); f.u16(140); f.u32(1); f.u32(0); f.u32(0);
    size_t shoff_at = f.size();
    f.u32(0);
    f.u32(0); f.u16(52); f.u16(32); f.u16(0); f.u16(40);   /* e_phentsize 32 with no program headers, as asm6x */ f.u16((unsigned)hdrs.size()); f.u16((unsigned)shstrtab_index);
    /* each body at its own alignment, not four: asm6x's .text (32) is at 64, not 52, and
       its .TI.section.flags (0) at an odd offset */
    for (size_t i = 1; i < hdrs.size(); i++) {
        while (hdrs[i].align > 1 && f.size() % hdrs[i].align) f.u8(0);
        hdrs[i].offset = (unsigned long)f.size();
        if (hdrs[i].type != 8) f.bytes(bodies[i]);
    }
    f.pad4();
    unsigned long shoff = (unsigned long)f.size();
    for (size_t i = 0; i < hdrs.size(); i++) {
        const Shdr &h = hdrs[i];
        f.u32(h.name); f.u32(h.type); f.u32(h.flags); f.u32(h.addr); f.u32(h.offset);
        f.u32(h.size); f.u32(h.link); f.u32(h.info); f.u32(h.align); f.u32(h.entsize);
    }
    for (int k = 0; k < 4; k++) f.b[shoff_at + (size_t)k] = (unsigned char)(shoff >> (8 * k));

    FILE *out = fopen(path.c_str(), "wb");
    if (!out) { err = path + ": cannot write"; return false; }
    bool ok = fwrite(f.b.data(), 1, f.b.size(), out) == f.b.size();
    if (fclose(out) != 0) ok = false;
    if (!ok) { err = path + ": write failed"; return false; }
    return true;
}
