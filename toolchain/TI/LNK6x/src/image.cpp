/*  image.cpp - writing the TI ELF32 executable.
 *
 *  Every constant here was read off tests/ref. Two of them are blobs this linker copies rather
 *  than composes: the build attributes lnk6x writes with its own name in them, and the
 *  .TI.section.flags record. Both are byte-identical in all seven images that link no runtime
 *  library, and differ only in the two that do - so they are constants until a probe says
 *  otherwise, and docs/known.md says exactly that.
 */
#include "lnk.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>

static const u8 attributes[] = {
    0x41, 0x26, 0x00, 0x00, 0x00, 0x54, 0x49, 0x00, 0x01, 0x1F, 0x00, 0x00,
    0x00, 0x05, 0x4C, 0x69, 0x6E, 0x6B, 0x65, 0x72, 0x00, 0x08, 0x09, 0x0A,
    0x03, 0x0C, 0x03, 0x80, 0x02, 0x08, 0x82, 0x02, 0x02, 0x84, 0x02, 0x03,
    0x8E, 0x02, 0x02, 0x12, 0x00, 0x00, 0x00, 0x63, 0x36, 0x78, 0x61, 0x62,
    0x69, 0x00, 0x01, 0x07, 0x00, 0x00, 0x00, 0x04, 0x08
};
static const u8 ti_section_flags[26] = { 0x01 };

/*  The six names lnk6x defines whether or not anything wants them, all absolute and all
 *  0xFFFFFFFF: the startup code tests them rather than calls them. */
static const char *invented[] = {
    "binit", "__binit__", "__c_args__",
    "__TI_pprof_out_hndl", "__TI_prof_data_start", "__TI_prof_data_size"
};
static const int ninvented = 6;

namespace {

/*  **A string written once is found by hash, not by searching the table** (the review's C1).
 *  `s.find("\0" + t + "\0")` was a scan of the whole table per symbol - 85% of a 3 s link of
 *  the Compiler++ harness. A name can only ever match a whole earlier entry, so the first
 *  offset each name was given is exactly what the scan returned. */
struct Strtab {
    std::string s;
    std::unordered_map<std::string, u32> at;
    Strtab() { s.push_back('\0'); }
    u32 add(const std::string &t) {
        if (t.empty()) return 0;
        std::unordered_map<std::string, u32>::const_iterator i = at.find(t);
        if (i != at.end()) return i->second;
        u32 r = (u32)s.size();
        s += t; s.push_back('\0');
        at.emplace(t, r);
        return r;
    }
};

struct OutSym { u32 name, value, size; u8 info, other; u16 shndx; };

struct ByAddr {
    const std::vector<OutSec> *o;
    ByAddr(const std::vector<OutSec> *x) : o(x) {}
    bool operator()(int a, int b) const { return (*o)[a].addr < (*o)[b].addr; }
};

} /* namespace */

bool Link::write_image()
{
    const u32 nout = (u32)outs.size();

    /*  **7.4.4 writes an empty section without SHF_ALLOC** - .data 1, .rodata and .cinit 0,
     *  .c6xabi.exidx 0x80 - and so at file offset 0, where 8.2.2 keeps the flags (every q-probe
     *  linked by both). And its attributes blob says `08 08 0a 05 0c 05` where 8.2.2's says
     *  `08 09 0a 03 0c 03`: the linker's own tags, not the objects'. */
    u8 attr[sizeof attributes];
    memcpy(attr, attributes, sizeof attributes);
    if (opt.cgt744) {
        static const u8 v744[6] = { 0x08, 0x08, 0x0A, 0x05, 0x0C, 0x05 };
        memcpy(attr + 21, v744, 6);
        for (u32 i = 0; i < nout; i++)
            if (!outs[i].size && outs[i].parts.empty() && !outs[i].reserve) outs[i].flags &= ~(u32)SHF_ALLOC;
    }

    /*  File offsets. Allocated sections are laid down in address order, not in the order the
     *  section table lists them - q03's .bss is at a lower address than its .text and takes a
     *  lower offset, though the command file names .text first. A section with no address of
     *  its own lands at the end of them, and one that is not allocated has no offset at all. */
    std::vector<int> placed, empty;
    for (u32 i = 0; i < nout; i++) {
        if (!(outs[i].flags & SHF_ALLOC)) continue;
        if (outs[i].addr) placed.push_back((int)i); else empty.push_back((int)i);
    }
    std::stable_sort(placed.begin(), placed.end(), ByAddr(&outs));

    /*  A segment starts at the largest alignment of the sections it will hold, not its first's:
     *  q19's .neardata is aligned 1 and lnk6x puts it at 0x40, .text's 32. The groups are the
     *  segments' own, by the rule the segment loop below applies. */
    std::vector<u32> start_align(placed.size(), 0);    /* non-zero where a segment begins */
    /*  **The same run the segment loop makes**: one kind - file bytes or none - never write with
     *  execute, and across a gap smaller than the next section's alignment (or 8). Grouped by a
     *  stricter rule, q30's .extab took .switch's group past a NOBITS .fardata, was aligned by
     *  .cinit's 8, and its offset drifted 4 from its address - two segments where lnk6x has one. */
    bool lead_file = false;
    for (size_t k = 0, lead = 0, end = 0, flags = 0; k < placed.size(); k++) {
        const OutSec &o = outs[placed[k]];
        if (!o.size) continue;
        u32 f = (u32)flags | o.pflags;
        const bool file = (o.type != SHT_NOBITS);
        /*  8.2.2 runs across a small gap (q30's .const and .extab, 2 bytes); 7.4.4 never does */
        const bool near = !opt.cgt744 && o.addr >= end && o.addr - end < (o.align > 8 ? o.align : 8);
        const bool same_x = ((u32)flags & PF_X) == (o.pflags & PF_X);
        /*  a stricter alignment joins only a run whose start already has it: q32's .switch is at
         *  4 mod 8, and lnk6x opens a segment for .cinit rather than pad .switch to 8 */
        const bool fits = !start_align[lead] || o.align <= start_align[lead] ||
                          (o.align && outs[placed[lead]].addr % o.align == 0);
        /*  and never a writable section with a read-only one: q30 under --ram_model has .fardata
         *  (RW) between .extab and .switch, and lnk6x gives it a segment of its own */
        const bool same_w = ((u32)flags & PF_W) == (o.pflags & PF_W);
        if (start_align[lead] && file == lead_file && !((f & PF_W) && (f & PF_X)) && fits && same_w &&
            (o.addr == end || (near && same_x))) {
            if (o.align > start_align[lead]) start_align[lead] = o.align;
            flags = f;
        } else {
            lead = k; start_align[k] = o.align ? o.align : 1; flags = o.pflags; lead_file = file;
        }
        end = o.addr + o.size;
    }

    u32 pos = 52;
    for (size_t k = 0; k < placed.size(); k++) {
        OutSec &o = outs[placed[k]];
        pos = align_up(pos, start_align[k] > o.align ? start_align[k] : o.align);
        o.offset = pos;
        if (o.type == SHT_PROGBITS) pos += o.size;
    }
    for (size_t k = 0; k < empty.size(); k++) outs[empty[k]].offset = pos;
    for (u32 i = 0; i < nout; i++)                   /* .debug_line: after the loaded sections, no segment */
        if (outs[i].debug && outs[i].size) { outs[i].offset = pos; pos += outs[i].size; }

    /*  Nothing with bytes may be left without an offset. Before N3 was found, a section whose
     *  flags said it was not allocated kept offset 0 and its bytes were copied over the ELF
     *  header: a file that was not an ELF file at all, written without a word. */
    for (u32 i = 0; i < nout; i++)
        if (outs[i].type == SHT_PROGBITS && outs[i].size && !outs[i].offset) {
            err = outs[i].name + ": laid out at no file offset (flags " +
                  std::string(1, (char)('0' + (outs[i].flags & 7))) + ")";
            return false;
        }

    u32 attr_off = pos;                       pos += (u32)sizeof attributes;
    u32 sym_off  = align_up(pos, 4);          /* the symbol table is the one that is aligned */

    /* ------------------------------------------------------ the symbol table */
    Strtab str;
    std::vector<OutSym> syms;
    OutSym z; memset(&z, 0, sizeof z);
    syms.push_back(z);

    for (u32 i = 0; i < nout; i++) {
        OutSym y;
        y.name = str.add(outs[i].name);
        y.value = outs[i].addr; y.size = 0;
        y.info = (STB_LOCAL << 4) | STT_SECTION; y.other = 0; y.shndx = (u16)(i + 1);
        syms.push_back(y);
    }

    for (size_t mi = 0; mi < mods.size(); mi++) {
        Module &m = mods[mi];
        bool any = false;
        for (size_t si = 1; si < m.secs.size(); si++) if (m.secs[si].live && m.secs[si].out >= 0) any = true;
        if (!any) continue;
        for (size_t k = 1; k < m.syms.size(); k++) {
            Sym &y = m.syms[k];
            if ((y.info >> 4) != STB_LOCAL) continue;
            u32 type = y.info & 0xF;
            OutSym o;
            o.size = y.size; o.info = y.info; o.other = y.other;
            if (type == STT_FILE) {
                /* the object's own FILE symbol names the source it was assembled from, and
                   that is the name lnk6x carries into the image - not the object's */
                o.name = str.add(y.name); o.value = 0; o.shndx = SHN_ABS;
            } else if (y.shndx == SHN_ABS) {
                o.name = str.add(y.name); o.value = y.value; o.shndx = SHN_ABS;
            } else {
                if (y.shndx >= m.secs.size()) continue;
                InSec &c = m.secs[y.shndx];
                if (!c.live || c.out < 0) continue;
                /*  **A section symbol keeps its input section's name**: lnk6x writes
                 *  `.text:big` at q21's .text:big, and `.text:_c_int00` in every runtime image,
                 *  where this linker wrote the output section's `.text` for all of them. */
                o.name = str.add(y.name);
                o.value = c.addr + y.value;
                o.shndx = (u16)(c.out + 1);
            }
            y.out = (int)syms.size();
            syms.push_back(o);
        }
        /*  **A trampoline is a local function of the module whose call made it**, after that
         *  module's own locals, with `other` 0 where theirs is 2: q15's `$Tramp$S$$faraway`
         *  is symbol 21, straight after .fartext's section symbol. In address order when a
         *  module has several, which no probe has shown yet (tests/probes/q24). */
        std::vector<std::pair<u32, int> > mine;
        for (size_t t = 0; t < tramps.size(); t++) {
            const InSec *c = all[tramps[t].in];
            if (c->module == (int)mi && c->out >= 0) mine.push_back(std::make_pair(c->addr, (int)t));
        }
        std::sort(mine.begin(), mine.end());
        for (size_t k = 0; k < mine.size(); k++) {
            const InSec *c = all[tramps[mine[k].second].in];
            OutSym o;
            o.name = str.add(tramps[mine[k].second].name);
            o.value = c->addr; o.size = 0;
            o.info = (STB_LOCAL << 4) | STT_FUNC; o.other = 0;
            o.shndx = (u16)(c->out + 1);
            syms.push_back(o);
        }
    }

    u32 first_global = (u32)syms.size();
    for (int i = 0; i < ninvented; i++) {
        OutSym o;
        o.name = str.add(invented[i]);
        o.value = 0xFFFFFFFFu; o.size = 0;
        o.info = (STB_GLOBAL << 4) | STT_NOTYPE; o.other = 2; o.shndx = SHN_ABS;
        /*  Under --args, __c_args__ is the linker's .args section's start rather than -1. */
        if (cmd.args_size > 0 && lnk_mod >= 0 && !strcmp(invented[i], "__c_args__"))
            for (size_t si = 1; si < mods[lnk_mod].secs.size(); si++) {
                const InSec &c = mods[lnk_mod].secs[si];
                if (c.name == ".args" && c.out >= 0) { o.value = c.addr; o.shndx = (u16)(c.out + 1); }
            }
        syms.push_back(o);
    }
    for (size_t mi = 0; mi < mods.size(); mi++) {
        Module &m = mods[mi];
        if ((int)mi == lnk_mod) continue;      /* the linker's own names come after these */
        for (size_t k = 1; k < m.syms.size(); k++) {
            Sym &y = m.syms[k];
            if ((y.info >> 4) == STB_LOCAL || y.name.empty() || y.shndx == SHN_UNDEF) continue;
            /*  Only the definition that won: with a weak and a strong definition of one name
             *  (q14) both objects carry it, and lnk6x writes one symbol, the strong one. */
            std::map<std::string, std::pair<int, int> >::const_iterator d = defined.find(y.name);
            if (d != defined.end() && (d->second.first != (int)mi || d->second.second != (int)k)) continue;
            OutSym o;
            o.name = str.add(y.name); o.size = y.size; o.info = y.info; o.other = 2;
            if (y.lnk_out >= 0) { o.value = y.value; o.shndx = (u16)(y.lnk_out + 1); }
            else if (y.shndx == SHN_ABS) { o.value = y.value; o.shndx = SHN_ABS; }
            else {
                int am = (int)mi, as = (int)k;       /* an alias is written at its target */
                resolve_alias(am, as);
                const Sym &t = mods[am].syms[as];
                if (t.shndx >= mods[am].secs.size()) continue;
                InSec &c = mods[am].secs[t.shndx];
                if (!c.live || c.out < 0) continue;
                o.value = c.addr + t.value;
                o.shndx = (u16)(c.out + 1);
            }
            syms.push_back(o);
        }
    }
    /*  Then the names the linker defined itself, in the order add_linker_symbols made them -
     *  __TI_STATIC_BASE last, which is where lnk6x writes it. The six it invents are already
     *  at the head of the globals, so they are not written twice. */
    if (lnk_mod >= 0) {
        Module &m = mods[lnk_mod];
        for (size_t k = 1; k < m.syms.size(); k++) {
            const Sym &y = m.syms[k];
            bool already = false;
            for (int i = 0; i < ninvented && !already; i++) already = (y.name == invented[i]);
            if (already) continue;
            OutSym o;
            o.name = str.add(y.name); o.size = 0; o.info = y.info; o.other = 2;
            o.value = y.value;
            o.shndx = (u16)(y.lnk_out >= 0 ? y.lnk_out + 1 : (int)SHN_ABS);
            syms.push_back(o);
        }
    }

    /*  The weak names nothing defined. lnk6x keeps them: UNDEF, WEAK, value 0, and after
     *  __TI_STATIC_BASE in the table - q14's `undefined_weak` is the last symbol in its
     *  image. A weak reference resolves to zero rather than stopping the link. */
    {
        std::map<std::string, bool> seen;
        for (size_t mi = 0; mi < mods.size(); mi++) {
            Module &m = mods[mi];
            for (size_t k = 1; k < m.syms.size(); k++) {
                const Sym &y = m.syms[k];
                if ((y.info >> 4) != STB_WEAK || y.shndx != SHN_UNDEF || y.name.empty()) continue;
                if (defined.find(y.name) != defined.end() || seen[y.name]) continue;
                seen[y.name] = true;
                OutSym o;
                o.name = str.add(y.name); o.value = 0; o.size = 0;
                o.info = y.info; o.other = 2; o.shndx = SHN_UNDEF;
                syms.push_back(o);
            }
        }
    }

    pos = sym_off + (u32)syms.size() * 16;
    u32 tif_off = pos;  pos += (u32)sizeof ti_section_flags;
    u32 str_off = pos;  pos += (u32)str.s.size();

    /* ------------------------------------------------------ the section names */
    Strtab shstr;
    std::vector<u32> shname(nout);
    for (u32 i = 0; i < nout; i++) shname[i] = shstr.add(outs[i].name);
    u32 n_attr = shstr.add(".c6xabi.attributes");
    u32 n_sym  = shstr.add(".symtab");
    u32 n_tif  = shstr.add(".TI.section.flags");
    u32 n_str  = shstr.add(".strtab");
    u32 n_shs  = shstr.add(".shstrtab");

    u32 shstr_off = pos; pos += (u32)shstr.s.size();

    /* ------------------------------------------------------- the segments */
    segs.clear();
    /*  **A segment is a run of one kind of section - bytes from the file, or none -** that
     *  never mixes write with execute (q04 keeps .text and .data apart), and it may cross an
     *  alignment gap when the file has the same gap: q07's .const and .extab are two bytes
     *  apart and one segment. Mixing the kinds put hello's .switch, .cinit and unwind tables
     *  past filesz, where the loader zero-filled them and _c_int00 ran off a blank table. */
    std::vector<bool> seg_file;
    for (size_t k = 0; k < placed.size(); k++) {
        OutSec &o = outs[placed[k]];
        if (!o.size) continue;
        bool file = (o.type != SHT_NOBITS);     /* .c6xabi.exidx is SHT_C6000_UNWIND: file */
        /*  The runs are the ones the offsets were laid out by: a section that began one there
         *  begins a segment here, and every other joins the segment before it. */
        if (!segs.empty() && seg_file.back() == file && !start_align[k]) {
            Seg &g = segs.back();
            u32 flags = g.flags | o.pflags;
            {
                g.memsz = (o.addr + o.size) - g.vaddr;
                if (file) g.filesz = (o.offset + o.size) - g.offset;
                g.flags = flags;
                if (o.align > g.align) g.align = o.align;
                continue;
            }
        }
        Seg g;
        g.offset = o.offset; g.vaddr = o.addr; g.paddr = o.addr;
        g.filesz = file ? o.size : 0;
        g.memsz = o.size; g.flags = o.pflags; g.align = o.align;
        segs.push_back(g);
        seg_file.push_back(file);
    }

    u32 ph_off = align_up(pos, 4);
    u32 sh_off = ph_off + (u32)segs.size() * 32;
    u32 total  = sh_off + (nout + 6) * 40;

    /* ---------------------------------------------------------- the bytes */
    std::vector<u8> f(total, 0);
    memcpy(&f[0], "\177ELF\1\1\1", 7);
    wr16(&f[16], 2);                 /* ET_EXEC */
    wr16(&f[18], 140);               /* EM_TI_C6000 */
    wr32(&f[20], 1);
    wr32(&f[24], entry_addr);
    wr32(&f[28], ph_off);
    wr32(&f[32], sh_off);
    wr32(&f[36], 0);
    wr16(&f[40], 52);
    wr16(&f[42], 32); wr16(&f[44], (u16)segs.size());
    wr16(&f[46], 40); wr16(&f[48], (u16)(nout + 6));
    wr16(&f[50], (u16)(nout + 5));

    for (size_t i = 0; i < all.size(); i++) {
        InSec *c = all[i];
        if (c->data.empty() || c->out < 0) continue;
        /*  **A section whose bytes moved into .cinit carries none of its own.** Its
         *  contributions still hold the data - fix_up needs them, and so does the image
         *  composed from them - but the section is SHT_NOBITS now and owns no file space,
         *  so writing them would land on whatever took that offset instead. */
        if (outs[c->out].type == SHT_NOBITS) continue;
        u32 at = outs[c->out].offset + (c->addr - outs[c->out].addr);
        if (at + c->data.size() > f.size()) { err = "a section lands past the end of the file"; return false; }
        memcpy(&f[at], &c->data[0], c->data.size());
    }
    memcpy(&f[attr_off], attr, sizeof attr);
    memcpy(&f[tif_off], ti_section_flags, sizeof ti_section_flags);
    memcpy(&f[str_off], str.s.data(), str.s.size());
    memcpy(&f[shstr_off], shstr.s.data(), shstr.s.size());
    for (size_t i = 0; i < syms.size(); i++) {
        u8 *p = &f[sym_off + i * 16];
        wr32(p, syms[i].name); wr32(p + 4, syms[i].value); wr32(p + 8, syms[i].size);
        p[12] = syms[i].info; p[13] = syms[i].other; wr16(p + 14, syms[i].shndx);
    }
    for (size_t i = 0; i < segs.size(); i++) {
        u8 *p = &f[ph_off + i * 32];
        wr32(p, 1); wr32(p + 4, segs[i].offset); wr32(p + 8, segs[i].vaddr);
        wr32(p + 12, segs[i].paddr); wr32(p + 16, segs[i].filesz); wr32(p + 20, segs[i].memsz);
        wr32(p + 24, segs[i].flags); wr32(p + 28, segs[i].align);
    }

    u8 *sh = &f[sh_off];
    memset(sh, 0, 40);
    for (u32 i = 0; i < nout; i++) {
        u8 *p = sh + (size_t)(i + 1) * 40;
        wr32(p, shname[i]); wr32(p + 4, outs[i].type); wr32(p + 8, outs[i].flags);
        wr32(p + 12, outs[i].addr); wr32(p + 16, outs[i].offset); wr32(p + 20, outs[i].size);
        wr32(p + 24, 0); wr32(p + 28, 0);
        wr32(p + 32, (outs[i].parts.empty() && !outs[i].reserve) ? 1u : outs[i].align);
        wr32(p + 36, outs[i].entsize);
    }
    u8 *p = sh + (size_t)(nout + 1) * 40;
    wr32(p, n_attr); wr32(p + 4, SHT_C6000_ATTRIBUTES); wr32(p + 16, attr_off);
    wr32(p + 20, (u32)sizeof attributes);
    p += 40;
    wr32(p, n_sym); wr32(p + 4, SHT_SYMTAB); wr32(p + 16, sym_off);
    wr32(p + 20, (u32)syms.size() * 16); wr32(p + 24, nout + 4); wr32(p + 28, first_global);
    wr32(p + 36, 16);
    p += 40;
    wr32(p, n_tif); wr32(p + 4, SHT_TI_SECTION_FLAGS); wr32(p + 16, tif_off);
    wr32(p + 20, (u32)sizeof ti_section_flags);
    p += 40;
    wr32(p, n_str); wr32(p + 4, SHT_STRTAB); wr32(p + 8, 0x20); wr32(p + 16, str_off);
    wr32(p + 20, (u32)str.s.size()); wr32(p + 36, 1);
    p += 40;
    wr32(p, n_shs); wr32(p + 4, SHT_STRTAB); wr32(p + 8, 0x20); wr32(p + 16, shstr_off);
    wr32(p + 20, (u32)shstr.s.size()); wr32(p + 36, 1);

    FILE *o = fopen(opt.out.c_str(), "wb");
    if (!o) { err = opt.out + ": cannot create"; return false; }
    if (fwrite(&f[0], 1, f.size(), o) != f.size()) { fclose(o); err = opt.out + ": short write"; return false; }
    fclose(o);
    return true;
}
