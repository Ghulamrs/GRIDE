/*  link.cpp - what is in the image, and where.
 *
 *  The order of a link, in the order this file does it:
 *
 *    read        every object named on the command line
 *    resolve     the global names, so a reference can be followed across objects
 *    eliminate   from the entry point, follow every relocation and keep what it reaches.
 *                This is not an optimisation to be switched on: lnk6x does it by default for
 *                EABI, and q02 is the proof - three sections of data the command file names
 *                and nothing refers to are not in the image at all
 *    sections    the output list is the SECTIONS block in its own order, and then whichever
 *                of the sixteen standard sections it did not name, in theirs
 *    allocate    .bss first, because it is what the static base points at, then the rest in
 *                SECTIONS order, each from the memory range its entry asks for
 *    offsets     file offsets in address order, not in section-table order
 */
#include "lnk.h"

#include <algorithm>
#include <set>
#include <cstdio>
#include <cstring>

/*  The sixteen sections lnk6x writes whether or not anything asked for them, in the order it
 *  appends the ones the command file left out - q08 leaves out .fardata and .rodata and gets
 *  them back in exactly this order - with the flags it gives each when it is empty. */
static const struct { const char *name; u32 flags; u32 entsize; } standard[] = {
    { ".text",          SHF_ALLOC | SHF_EXECINSTR,  0 },
    { ".const",         0,                          0 },
    { ".data",          SHF_ALLOC | SHF_WRITE,      0 },
    { ".bss",           SHF_ALLOC | SHF_WRITE,      0 },
    { ".far",           SHF_ALLOC | SHF_WRITE,      0 },
    { ".fardata",       SHF_ALLOC | SHF_WRITE,      0 },
    { ".neardata",      SHF_ALLOC | SHF_WRITE,      0 },
    { ".rodata",        SHF_ALLOC,                  0 },
    { ".cinit",         SHF_ALLOC,                  0 },
    { ".init_array",    SHF_ALLOC | SHF_WRITE,      0 },
    { ".switch",        0,                          0 },
    { ".cio",           0,                          0 },
    { ".stack",         0,                          0 },
    { ".sysmem",        0,                          0 },
    { ".c6xabi.exidx",  SHF_ALLOC | 0x80,           8 },
    { ".c6xabi.extab",  SHF_ALLOC,                  0 }
};
static const int nstandard = (int)(sizeof standard / sizeof standard[0]);

static bool standard_flags(const std::string &n, u32 &flags, u32 &entsize)
{
    for (int i = 0; i < nstandard; i++)
        if (n == standard[i].name) { flags = standard[i].flags; entsize = standard[i].entsize; return true; }
    flags = 0; entsize = 0;
    return false;
}

static bool slurp(const std::string &path, std::vector<u8> &out, std::string &err)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) { err = path + ": cannot open"; return false; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    out.resize((size_t)(n > 0 ? n : 0));
    if (n > 0 && fread(&out[0], 1, (size_t)n, f) != (size_t)n) { fclose(f); err = path + ": short read"; return false; }
    fclose(f);
    return true;
}

/* the name the map and the symbol table use: the file's own, without its directory */
static std::string basename_of(const std::string &p)
{
    size_t s = p.find_last_of("/\\");
    return (s == std::string::npos) ? p : p.substr(s + 1);
}

/*  What one module brings: its sections get their module number, and its global definitions
 *  go into the table. A strong definition wins over a weak one wherever it is met - q14 links
 *  the weak object first and lnk6x still gives `wk` the strong object's address - and two
 *  weak definitions keep the first. */
/*  Whether a definition is one the image will carry: a symbol in a section that is not
 *  allocated - debug information, the attribute records - is not. */
bool Link::in_image(int mi, int sym) const
{
    const Sym &y = mods[mi].syms[sym];
    if (y.shndx == SHN_ABS || is_common(y.shndx)) return true;
    if (y.shndx >= mods[mi].secs.size()) return false;
    return (mods[mi].secs[y.shndx].flags & SHF_ALLOC) != 0;
}

/*  An alias stands for its target: the target of the same object where that defines it, and
 *  the table's definition where it does not (remove.obj's unlink is lowlev.obj's). */
void Link::resolve_alias(int &mod, int &sym) const
{
    for (int hops = 0; hops < 8; hops++) {
        const Sym &y = mods[mod].syms[sym];
        if (y.alias < 0) return;
        const Sym &t = mods[mod].syms[y.alias];
        std::map<std::string, std::pair<int, int> >::const_iterator d = defined.find(t.name);
        if (t.shndx == SHN_UNDEF && d != defined.end()) { mod = d->second.first; sym = d->second.second; }
        else if (t.shndx != SHN_UNDEF) sym = y.alias;
        else return;
    }
}

bool Link::take_module(int mi)
{
    for (size_t si = 0; si < mods[mi].secs.size(); si++) mods[mi].secs[si].module = mi;
    /*  A section marked SHF_GROUP is C++'s vague linkage: the same definition compiled into
     *  every object that needed it, for the linker to keep once. The runtime's `_ZTSv` is in
     *  `.const:.typeinfo:_ZTSv`, flags 0x202, in both tdeh_cpp_abi.obj and typeinfo_.obj, and
     *  lnk6x links them without a word - so the first copy of a group section name is kept and
     *  every later one is dropped, with the symbols in it. */
    for (size_t si = 1; si < mods[mi].secs.size(); si++) {
        InSec &c = mods[mi].secs[si];
        if (!(c.flags & SHF_GROUP) || c.name.empty()) continue;
        if (groups[c.name]) c.dropped = true;
        else groups[c.name] = true;
    }
    for (size_t k = 0; k < mods[mi].syms.size(); k++) {
        const Sym &y = mods[mi].syms[k];
        if ((y.info >> 4) == STB_LOCAL || y.name.empty()) continue;
        if (y.shndx == SHN_UNDEF) continue;
        if (y.shndx < mods[mi].secs.size() && mods[mi].secs[y.shndx].dropped) continue;
        std::map<std::string, std::pair<int, int> >::iterator d = defined.find(y.name);
        if (d == defined.end()) { defined[y.name] = std::make_pair(mi, (int)k); continue; }
        const Sym &had = mods[d->second.first].syms[d->second.second];
        /*  **COMMON is a request for storage, not a definition**, so a real one replaces
         *  it and two of them are not a redefinition. in_image counts SHN_COMMON as a
         *  definition - which is what puts it in this table at all - and without this the
         *  linker's own .bss allocation of parmbuf read as a second definition of it, and
         *  take_module gave up there. Every symbol after it was then never recorded, so
         *  __TI_STACK_END and the rest of the linker's own names went missing. */
        if (is_common(had.shndx) && !is_common(y.shndx)) {
            d->second = std::make_pair(mi, (int)k);
            continue;
        }
        if (is_common(y.shndx)) continue;
        if ((had.info >> 4) == STB_WEAK && (y.info >> 4) == STB_GLOBAL) {
            d->second = std::make_pair(mi, (int)k);
            continue;
        }
        if ((had.info >> 4) == STB_GLOBAL && (y.info >> 4) == STB_GLOBAL &&
            in_image(d->second.first, d->second.second) && in_image(mi, (int)k)) {
            /*  Two strong definitions of one name, both of them in the image. lnk6x says
             *  "symbol redefined" and stops; this linker kept the first and said nothing, so
             *  two objects that both defined _c_int00 linked quietly (the review's N16).
             *
             *  Only definitions that are in the image count. The runtime's objects share
             *  debug-information symbols by design - boot.obj and args_main.obj both define
             *  __TI_DW.debug_info.$base_types.<hash>, in a section that is not allocated and
             *  never reaches an image - and lnk6x links them without a word. */
            err = "symbol redefined: " + y.name + ", in " +
                  mods[d->second.first].name + " and in " + mods[mi].name;
            return false;
        }
    }
    return true;
}

bool Link::read_inputs()
{
    /*  The command line's objects, in its order - that order is the layout order - and its
     *  archives kept aside. A `-l name` is looked for through the `-i` directories; a name
     *  given with a directory, or an object, is opened as it stands. */
    libs.clear();
    for (size_t i = 0; i < opt.inputs.size(); i++) {
        std::string path = opt.inputs[i];
        std::vector<u8> b;
        if (!slurp(path, b, err)) {
            std::string found = find_library(opt, opt.inputs[i]);
            if (found.empty()) { err = opt.inputs[i] + ": cannot open"; return false; }
            path = found;
            if (!slurp(path, b, err)) return false;
        }
        if (b.size() >= 8 && memcmp(&b[0], "!<arch>\n", 8) == 0) {
            Archive a;
            if (!a.load(path, err)) return false;
            libs.push_back(a);
            continue;
        }
        Module m;
        if (!elf_read(b.empty() ? (const u8 *)"" : &b[0], b.size(), basename_of(path), m, err)) return false;
        mods.push_back(m);
    }
    cmdMods = mods.size();
    for (size_t mi = 0; mi < mods.size(); mi++) if (!take_module((int)mi)) return false;
    if (libs.empty()) { add_linker_symbols(); return true; }

    /*  **An archive is scanned in passes, each pass a walk over its members in the archive's
     *  order, and a member is taken the moment the walk reaches it with a name that is still
     *  undefined** - so a member taken early in a pass can make a later one wanted in the
     *  same pass. That is lnk6x's order, read off its 7.4.4 image of the cbs C++ program:
     *  its STT_FILE names come as two alphabetical runs, boot ... vars and then _lock ...,
     *  with exit (boot's), newhandler (new_'s) and tdeh_unwinder (tdeh_pr_c6000's) in the
     *  first run though nothing on the command line names them - one pass, not one round of
     *  direct references. It matters because a size tie between two contributions of one
     *  name goes to the one loaded first (q07's two 4-byte `.const:.string`). The entry point
     *  is asked for first, since with a runtime it is the library that has it, and the
     *  decompressors the cinit table names with it. A weak undefined name does not pull a
     *  member - that is what weak means - which is why the runtime's optional hooks do not
     *  drag their implementations in. Not __TI_zero_init: a pulled member's symbols reach the
     *  image even when its code is eliminated, and lnk6x has none of that one's unless it is
     *  used - so it is pulled by eliminate(), once a zero-fill record is known to need it. */
    std::set<std::string> undef;                   /* strong undefined names, as of now */
    size_t scanned = 0;                            /* modules whose undefineds are in `undef` */
    std::vector<std::string> extra;
    if (defined.find(opt.entry) == defined.end()) extra.push_back(opt.entry);
    if (opt.rom_model) { extra.push_back("__TI_decompress_rle24"); extra.push_back("__TI_decompress_none"); }
    for (;;) {
        bool took = false;
        for (size_t a = 0; a < libs.size(); a++) {
            Archive &ar = libs[a];
            /*  the members in archive order, each with the names the index gives it */
            std::map<u32, std::vector<std::string> > members;
            for (size_t k = 0; k < ar.index.size(); k++) members[ar.index[k].second].push_back(ar.index[k].first);
            for (std::map<u32, std::vector<std::string> >::iterator m = members.begin(); m != members.end(); ++m) {
                if (ar.taken.count(m->first)) continue;
                for (; scanned < mods.size(); scanned++)
                    for (size_t k = 0; k < mods[scanned].syms.size(); k++) {
                        const Sym &y = mods[scanned].syms[k];
                        if (y.shndx == SHN_UNDEF && !y.name.empty() && (y.info >> 4) == STB_GLOBAL) undef.insert(y.name);
                    }
                bool wanted = false;
                for (size_t k = 0; k < m->second.size() && !wanted; k++) {
                    const std::string &n = m->second[k];
                    if (defined.find(n) != defined.end()) continue;
                    if (undef.count(n) || std::find(extra.begin(), extra.end(), n) != extra.end()) wanted = true;
                }
                if (!wanted) continue;
                Module md;
                if (!ar.member(m->first, md, err)) return false;
                ar.taken.insert(m->first);
                mods.push_back(md);
                if (!take_module((int)mods.size() - 1)) return false;
                took = true;
            }
        }
        if (!took) break;
    }
    add_linker_symbols();
    return true;
}

/*  The names lnk6x defines itself. It defines each one only when something asks for it - a
 *  link with no runtime has none of them, and q09's map lists only the six it invents whatever
 *  happens - and it writes them against an output section rather than as absolutes, except the
 *  three that are sizes. All of this is read off q07's image and map (the review's N5).
 *
 *  __TI_INITARRAY_Base and _Limit bracket .init_array, the constructors _c_int00 runs before
 *  main. They are defined only where an object brings an .init_array: q07 has none and lnk6x
 *  leaves them weak undefined there, where a C++ static constructor gets them (cbs, 03-10).
 */
void Link::add_linker_symbols()
{
    static const struct { const char *name; const char *sec; int which; } table[] = {
        { "__TI_STACK_END",           ".stack",         1 },   /* the end of the section */
        { "__TI_STACK_SIZE",          "",               2 },   /* --stack_size, absolute */
        { "__TI_SYSMEM_SIZE",         "",               3 },   /* --heap_size, absolute */
        { "__TI_UNWIND_TABLE_START",  ".c6xabi.exidx",  0 },
        { "__TI_UNWIND_TABLE_END",    ".c6xabi.exidx",  1 },
        { "__TI_INITARRAY_Base",      ".init_array",    0 },
        { "__TI_INITARRAY_Limit",     ".init_array",    1 },
        { "__TI_CINIT_Base",          ".cinit",         0 },
        { "__TI_CINIT_Limit",         ".cinit",         1 },
        { "__TI_Handler_Table_Base",  ".cinit",         0 },
        { "__TI_Handler_Table_Limit", ".cinit",         1 },
        { "__TI_STATIC_BASE",         ".bss",           0 },   /* last: lnk6x writes it last */
        { 0, 0, 0 }
    };
    /*  The six lnk6x invents whether or not anything wants them: they are absolute and
     *  0xFFFFFFFF, image.cpp writes them at the head of the globals, and they are registered
     *  here as well so that a runtime's reference to `__binit__` resolves. */
    static const char *const always[] = {
        "binit", "__binit__", "__c_args__",
        "__TI_pprof_out_hndl", "__TI_prof_data_start", "__TI_prof_data_size", 0
    };
    std::map<std::string, bool> wanted;
    wanted["__TI_STATIC_BASE"] = true;      /* this one is in every image the bed has */
    for (size_t mi = 0; mi < mods.size(); mi++)
        for (size_t k = 0; k < mods[mi].syms.size(); k++) {
            const Sym &y = mods[mi].syms[k];
            if (y.shndx == SHN_UNDEF && !y.name.empty() && defined.find(y.name) == defined.end())
                wanted[y.name] = true;
        }
    bool init_array = false;
    for (size_t mi = 0; mi < mods.size() && !init_array; mi++)
        for (size_t si = 0; si < mods[mi].secs.size() && !init_array; si++)
            if (base_section(mods[mi].secs[si].name) == ".init_array" && mods[mi].secs[si].size) init_array = true;
    if (!init_array) wanted["__TI_INITARRAY_Base"] = wanted["__TI_INITARRAY_Limit"] = false;

    /*  COMMON: a symbol with no section and a size, which is what `st_shndx` 0xFFF2 means.
     *  The runtime has four - parmbuf, __TI_tmpnams, _ZSt16__dummy_typeinfo, __dso_handle -
     *  and sym_addr used to refuse them as "names no section". The largest size wins, the
     *  alignment is the symbol's value, and they are allocated together in .bss (N7). Where
     *  lnk6x puts that run inside .bss is not something the bed shows. */
    std::map<std::string, std::pair<u32, u32> > common;      /* name -> size, alignment */
    for (size_t mi = 0; mi < mods.size(); mi++)
        for (size_t k = 0; k < mods[mi].syms.size(); k++) {
            const Sym &y = mods[mi].syms[k];
            if (!is_common(y.shndx) || y.name.empty()) continue;
            /*  **A real definition takes precedence, and COMMON is not one.** in_image
             *  counts SHN_COMMON as a definition, so take_module has already put every
             *  one of these in `defined` - skipping on that alone left the runtime's
             *  parmbuf with no storage, and sym_addr then refused it as "names no
             *  section". Only a definition that has a section wins here. */
            std::map<std::string, std::pair<int, int> >::const_iterator d =
                defined.find(y.name);
            if (d != defined.end() &&
                !is_common(mods[d->second.first].syms[d->second.second].shndx))
                continue;
            std::map<std::string, std::pair<u32, u32> >::iterator it = common.find(y.name);
            u32 al = common_align(y.shndx, y.value);
            if (it == common.end()) common[y.name] = std::make_pair(y.size, al);
            else {
                if (y.size > it->second.first)  it->second.first = y.size;
                if (al > it->second.second) it->second.second = al;
            }
        }

    Module m;
    m.name = "<linker>";
    m.file_sym = -1;
    m.syms.push_back(Sym());                /* the null symbol every module's table starts with */
    {
        InSec null_sec;
        null_sec.name = ""; null_sec.type = SHT_NULL; null_sec.flags = 0; null_sec.size = 0;
        null_sec.align = 1; null_sec.entsize = 0; null_sec.module = -1; null_sec.index = 0;
        null_sec.live = false; null_sec.dropped = false;
        null_sec.out = -1; null_sec.addr = 0; null_sec.load = 0;
        m.secs.push_back(null_sec);
        InSec bss;
        bss.name = ".bss"; bss.type = SHT_NOBITS; bss.flags = SHF_ALLOC | SHF_WRITE;
        bss.size = 0; bss.align = 1; bss.entsize = 0; bss.module = -1; bss.index = 1;
        bss.live = false; bss.dropped = false;
        bss.out = -1; bss.addr = 0; bss.load = 0;
        for (std::map<std::string, std::pair<u32, u32> >::iterator it = common.begin();
             it != common.end(); ++it) {
            u32 al = it->second.second;
            if (al > bss.align) bss.align = al;
            bss.size = align_up(bss.size, al);
            Sym y;
            y.name = it->first; y.value = bss.size; y.size = it->second.first;
            y.info = (u8)((STB_GLOBAL << 4) | STT_OBJECT); y.other = 2; y.shndx = 1;
            m.syms.push_back(y);
            bss.size += it->second.first;
        }
        m.secs.push_back(bss);
        /*  --args=N: lnk6x lays a PROGBITS .args of N zero bytes, writable, aligned 4, with
         *  __c_args__ at its start for the loader to fill (iop.map, 7.4.4); without the option
         *  __c_args__ stays the absolute -1 args_main.c reads as "no arguments". */
        if (cmd.args_size > 0) {
            InSec args = bss;
            args.name = ".args"; args.type = SHT_PROGBITS; args.size = cmd.args_size;
            args.align = 4; args.index = 2; args.data.assign(cmd.args_size, 0);
            Sym y;
            y.name = "__c_args__"; y.value = 0; y.size = 0;
            y.info = (u8)((STB_GLOBAL << 4) | STT_NOTYPE); y.other = 2; y.shndx = 2;
            m.syms.push_back(y);
            m.secs.push_back(args);
        }
    }
    for (int i = 0; always[i]; i++) {
        if (defined.find(always[i]) != defined.end()) continue;   /* an object got there first */
        if (cmd.args_size > 0 && !strcmp(always[i], "__c_args__")) continue;
        Sym y;
        y.name = always[i]; y.value = 0xFFFFFFFFu;
        y.info = (u8)((STB_GLOBAL << 4) | STT_NOTYPE); y.other = 2; y.shndx = SHN_ABS;
        m.syms.push_back(y);
    }
    for (int i = 0; table[i].name; i++) {
        if (opt.verbose)
            fprintf(stderr, "linker symbol %s: wanted=%d defined=%d\n", table[i].name,
                    (int)wanted[table[i].name],
                    (int)(defined.find(table[i].name) != defined.end()));
        if (!wanted[table[i].name]) continue;
        Sym y;
        y.name = table[i].name;
        y.info = (u8)((STB_GLOBAL << 4) | STT_NOTYPE);
        y.other = 2;
        y.shndx = SHN_ABS;              /* an address while it is resolved; see lnk_out */
        y.lnk_out = -1;
        m.syms.push_back(y);
    }
    mods.push_back(m);
    lnk_mod = (int)mods.size() - 1;
    /*  **Its result is checked.** It was not, and a `return false` inside it - which is
     *  how it reports a redefinition - stopped it partway through this module's symbols
     *  and left the rest unrecorded, with no message at all. */
    /*  A redefinition here would be the linker's own name against an object's, which
     *  the `always` and `wanted` guards above already rule out - but if it ever happens
     *  it is reported, not swallowed: the ignored result left every symbol after the
     *  first collision unrecorded, and said nothing. */
    if (!take_module(lnk_mod)) fprintf(stderr, "lnk6x: %s\n", err.c_str());
}

/*  Their values, once the sections have addresses. `.stack` and `.sysmem` are the linker's
 *  own too: q07's are 0x4000 and 0x1000, which are --stack_size and --heap_size, and no input
 *  section contributes to either. A link that asks for neither leaves both at zero, which is
 *  what q01 through q14 show (the review's N10). */
void Link::set_linker_symbols()
{
    if (lnk_mod < 0) return;
    Module &m = mods[lnk_mod];
    for (size_t k = 0; k < m.syms.size(); k++) {
        Sym &y = m.syms[k];
        if (y.name == "__TI_STACK_SIZE")  { y.value = cmd.stack_size; continue; }
        if (y.name == "__TI_SYSMEM_SIZE") { y.value = cmd.heap_size;  continue; }
        if (y.name == "__TI_STATIC_BASE") {
            int b = out_index(".bss");
            y.value = static_base;
            y.lnk_out = b;                  /* .bss when there is one, absolute otherwise */
            continue;
        }
        std::string sec = ".cinit";
        int end = 1;
        if (y.name == "__TI_STACK_END")                 sec = ".stack";
        else if (y.name.compare(0, 17, "__TI_UNWIND_TABLE") == 0) sec = ".c6xabi.exidx";
        else if (y.name.compare(0, 15, "__TI_INITARRAY_") == 0) sec = ".init_array";
        /*  **Only the .cinit table's own names default to .cinit.** The rest - binit and
         *  __binit__ among them - keep the value add_linker_symbols gave them: 0xFFFFFFFF,
         *  which _auto_init_elf reads as "no boot copy table". Moved to .cinit's end, it
         *  called copy_in on whatever lay there, and hello never reached main. */
        else if (y.name != "__TI_CINIT_Base" && y.name != "__TI_CINIT_Limit" &&
                 y.name != "__TI_Handler_Table_Base" && y.name != "__TI_Handler_Table_Limit") continue;
        if (y.name == "__TI_UNWIND_TABLE_START" || y.name == "__TI_CINIT_Base" ||
            y.name == "__TI_Handler_Table_Base" || y.name == "__TI_INITARRAY_Base") end = 0;
        int oi = out_index(sec);
        if (oi < 0) continue;
        y.value = outs[oi].addr + (end ? outs[oi].size : 0);
        y.lnk_out = oi;
        /*  **The .cinit table's names point inside it, and must before fix_up runs.** They
         *  were given .cinit's start and end here and corrected only by place_cinit, after
         *  relocation - so _auto_init_elf was patched with the start of .cinit for both
         *  bases and read the compressed image as its record table. */
        if (sec == ".cinit" && cinit_in >= 0) {
            u32 base = all[cinit_in]->addr;
            if (y.name == "__TI_Handler_Table_Base")       y.value = base + cinit_table_off;
            else if (y.name == "__TI_Handler_Table_Limit") y.value = base + cinit_table_off + 4 * (u32)cinit_handlers.size();
            else if (y.name == "__TI_CINIT_Base")          y.value = base + cinit_recs_off;
            else if (y.name == "__TI_CINIT_Limit")         y.value = base + cinit_recs_off + 8 * (u32)cinit_recs.size();
        }
    }
}

/* ------------------------------------------------- unused section elimination */

bool Link::eliminate()
{
    std::map<std::string, std::pair<int, int> >::iterator e = defined.find(opt.entry);
    if (e == defined.end()) { err = "entry point not found: " + opt.entry; return false; }

    std::vector<std::pair<int, int> > &work = pending;   /* module, section */
    work.clear();
    Module &em = mods[e->second.first];
    Sym &es = em.syms[e->second.second];
    if (es.shndx >= em.secs.size()) { err = opt.entry + ": defined in no section"; return false; }
    em.secs[es.shndx].live = true;
    work.push_back(std::make_pair(e->second.first, (int)es.shndx));

    /*  The decompressors are roots of their own under --rom_model: the handler table the
     *  linker writes is the only thing that reaches them, and elimination cannot see it. */
    if (opt.rom_model) {
        static const char *const kHandlers[] = { "__TI_decompress_rle24",
                                                 "__TI_decompress_none", 0 };
        for (int h = 0; kHandlers[h]; h++) {
            std::map<std::string, std::pair<int, int> >::iterator d = defined.find(kHandlers[h]);
            if (d == defined.end()) continue;
            Module &hm = mods[d->second.first];
            u16 hx = hm.syms[d->second.second].shndx;
            if (hx == 0 || hx >= hm.secs.size() || hm.secs[hx].live) continue;
            hm.secs[hx].live = true;
            work.push_back(std::make_pair(d->second.first, (int)hx));
        }
    }

    /*  **.init_array is a root**: nothing names a constructor's entry but the startup's walk
     *  between __TI_INITARRAY_Base and _Limit, which elimination cannot see. lnk6x keeps it -
     *  the section of that exact name: a `.init_array:late` subsection is dropped by both
     *  linkers like any other section nothing names (q36). */
    for (size_t mi = 0; mi < mods.size(); mi++)
        for (size_t si = 0; si < mods[mi].secs.size(); si++) {
            InSec &x = mods[mi].secs[si];
            if (x.live || x.dropped || x.name != ".init_array") continue;
            x.live = true;
            work.push_back(std::make_pair((int)mi, (int)si));
        }

    /*  **An unwind index entry lives as long as the code it describes.** `.c6xabi.exidx`
     *  is SHF_LINK_ORDER with sh_link naming its function's section; nothing refers to it,
     *  so following references alone drops every one - and with them the personality
     *  routine they name and the whole unwinder behind it, which is what lnk6x keeps. */
    for (;;) {
        while (!work.empty()) {
            std::pair<int, int> at = work.back(); work.pop_back();
            if (!follow(at)) return false;
        }
        for (size_t mi = 0; mi < mods.size(); mi++) {
            std::vector<InSec> &ss = mods[mi].secs;
            for (size_t si = 0; si < ss.size(); si++) {
                InSec &x = ss[si];
                if (x.live || x.dropped || x.name.compare(0, 13, ".c6xabi.exidx") != 0) continue;
                if (x.link == 0 || x.link >= ss.size() || !ss[x.link].live) continue;
                x.live = true;
                work.push_back(std::make_pair((int)mi, (int)si));
            }
        }
        if (work.empty()) {
            /*  **__TI_zero_init is a root only under 7.4.4, and only when something will be
             *  zero-filled**: a live, uninitialised .bss or .far piece under --rom_model. 7.4.4
             *  zeroes one with the handler and an 8-byte record (hello's .far, q31-q33-744);
             *  8.2.2 never pulls the handler and writes an rle image of zeros instead (q31-q33,
             *  ti74's isort and sieve) - which is what q30's first version met (docs/known.md). */
            if (opt.rom_model && opt.cgt744 && !zero_root) {
                bool need = false;
                for (size_t mi = 0; mi < mods.size() && !need; mi++)
                    for (size_t si = 0; si < mods[mi].secs.size() && !need; si++) {
                        const InSec &x = mods[mi].secs[si];
                        if (x.live && x.type == SHT_NOBITS && x.size && (x.flags & SHF_WRITE) &&
                            (base_section(x.name) == ".far" || base_section(x.name) == ".bss")) need = true;
                    }
                if (need && defined.find("__TI_zero_init") == defined.end() && !pull_symbol("__TI_zero_init"))
                    return false;
                std::map<std::string, std::pair<int, int> >::iterator d = defined.find("__TI_zero_init");
                if (need && d != defined.end()) {
                    zero_root = true;
                    Module &hm = mods[d->second.first];
                    u16 hx = hm.syms[d->second.second].shndx;
                    if (hx && hx < hm.secs.size() && !hm.secs[hx].live) {
                        hm.secs[hx].live = true;
                        work.push_back(std::make_pair(d->second.first, (int)hx));
                        continue;
                    }
                }
            }
            break;
        }
    }
    return true;
}

/*  The archive member that defines one name, taken as read_inputs takes members - for the
 *  one handler that is only wanted once elimination has seen what it would zero. */
bool Link::pull_symbol(const std::string &name)
{
    for (size_t a = 0; a < libs.size(); a++) {
        Archive &ar = libs[a];
        const u32 *hit = ar.find(name);
        if (!hit) continue;
        if (ar.taken.count(*hit)) return true;
        Module m;
        if (!ar.member(*hit, m, err)) return false;
        ar.taken.insert(*hit);
        mods.push_back(m);
        return take_module((int)mods.size() - 1);
    }
    return true;
}

/*  One kept section's references, each target made live and queued. */
bool Link::follow(std::pair<int, int> at)
{
    std::vector<std::pair<int, int> > &work = pending;
    {
        InSec &c = mods[at.first].secs[at.second];
        for (size_t r = 0; r < c.relocs.size(); r++) {
            const Rel &rl = c.relocs[r];
            if (rl.sym >= mods[at.first].syms.size()) { err = c.name + ": a relocation names no symbol"; return false; }
            const Sym &y = mods[at.first].syms[rl.sym];
            int tm = at.first, ts = (int)rl.sym; u16 tx = y.shndx;
            /*  A name that is not the object's own goes through the table of definitions,
             *  even when this object defines it too: q14's weak `wk` is defined in the
             *  referring object and lnk6x still reaches the strong one in the other. */
            if ((y.info >> 4) != STB_LOCAL && !y.name.empty()) {
                std::map<std::string, std::pair<int, int> >::iterator d = defined.find(y.name);
                if (d != defined.end()) {
                    tm = d->second.first; ts = d->second.second;
                    resolve_alias(tm, ts);
                    tx = mods[tm].syms[ts].shndx;
                } else if (y.shndx == SHN_UNDEF) {
                    /*  An undefined weak reference is not an error: it is zero, and it pulls
                     *  nothing in with it (q14 - lnk6x keeps it as UNDEF WEAK). */
                    if ((y.info >> 4) == STB_WEAK) continue;
                    err = "unresolved symbol: " + y.name; return false;
                }
            } else if (y.shndx == SHN_UNDEF) {
                err = "unresolved symbol: " + y.name; return false;
            }
            if (tx == SHN_ABS || tx == SHN_UNDEF || tx >= mods[tm].secs.size()) continue;
            InSec &t = mods[tm].secs[tx];
            if (t.live || t.dropped) continue;
            t.live = true;
            work.push_back(std::make_pair(tm, (int)tx));
        }
    }
    return true;
}

/* --------------------------------------------------------- output sections */

int Link::out_index(const std::string &name) const
{
    if (out_by_name_n != outs.size()) {
        for (size_t i = out_by_name_n; i < outs.size(); i++) out_by_name.emplace(outs[i].name, (int)i);
        out_by_name_n = outs.size();
    }
    std::unordered_map<std::string, int>::const_iterator i = out_by_name.find(name);
    return i == out_by_name.end() ? -1 : i->second;
}

bool Link::build_sections()
{
    for (size_t i = 0; i < cmd.secs.size(); i++) {
        OutSec o;
        o.name = cmd.secs[i].name;
        o.load = cmd.secs[i].load;
        o.run  = cmd.secs[i].run.empty() ? cmd.secs[i].load : cmd.secs[i].run;
        o.progbits = cmd.secs[i].has_fill;
        o.type = SHT_NOBITS; o.addr = 0; o.size = 0; o.align = 1; o.offset = 0; o.pflags = 0;
        standard_flags(o.name, o.flags, o.entsize);
        outs.push_back(o);
    }
    /*  **Only two standard sections are added that the command file does not name**: .fardata
     *  beside a named .far and .rodata beside a named .const, in that order after the named
     *  ones. q08 and q09 name .far and .const and get both back; q16 (ride.cmd) leaves out the
     *  unwind sections and does not; q26 and q27 name only their code sections and get
     *  nothing else at all - no .bss, and __TI_STATIC_BASE absolute. The table's order and
     *  flags still hold for what is added. */
    const bool named_far = out_index(".far") >= 0, named_const = out_index(".const") >= 0;
    for (int i = 0; i < nstandard; i++) {
        if (out_index(standard[i].name) >= 0) continue;
        std::string sn = standard[i].name;
        /*  7.4.4 adds an empty .cinit as well: q26 and q27, linked by it, have one no file named. */
        if (!((sn == ".fardata" && named_far) || (sn == ".rodata" && named_const) ||
              (sn == ".cinit" && opt.cgt744))) continue;
        OutSec o;
        o.name = standard[i].name;
        o.flags = standard[i].flags; o.entsize = standard[i].entsize;
        o.type = SHT_NOBITS; o.addr = 0; o.size = 0; o.align = 1; o.offset = 0; o.pflags = 0;
        o.progbits = false;
        outs.push_back(o);
    }

    for (size_t mi = 0; mi < mods.size(); mi++) {
        for (size_t si = 1; si < mods[mi].secs.size(); si++) {
            InSec &c = mods[mi].secs[si];
            if (!c.live || c.dropped || !(c.flags & SHF_ALLOC)) continue;
            /*  A subsection joins its base section: the runtime's 2,731 `.text:name` sections
             *  and the corpus's `.c6xabi.extab:*` are part of `.text` and `.c6xabi.extab`,
             *  not sections of their own - unless the command file names the full name, which
             *  is what is asked first (the review's N6). */
            int oi = out_index(c.name);
            if (oi < 0 && base_section(c.name) != c.name) oi = out_index(base_section(c.name));
            if (oi < 0) {
                OutSec o;
                o.name = base_section(c.name); o.flags = 0; o.entsize = 0;
                o.type = SHT_NOBITS; o.addr = 0; o.size = 0; o.align = 1; o.offset = 0;
                o.pflags = 0; o.progbits = false;
                outs.push_back(o);
                oi = (int)outs.size() - 1;
            }
            c.out = oi;
            outs[oi].parts.push_back((int)all.size());
            all.push_back(&c);
        }
    }

    /*  **.debug_line is carried, not allocated** - as lnk6x carries every input's into the image
     *  (t.out, 7.4.4: 13,968 bytes, boot.c's unit first), concatenated in input order with the
     *  DW_LNE_set_address relocations applied against the final addresses, so a debugger on the
     *  image (sim6747 --run --debug) stops on a source line. It takes no memory, no segment and
     *  no address: its bytes follow the loaded sections in the file. Elimination is not asked -
     *  the units of a dropped section relocate to 0, and no row of theirs is ever reached. */
    /*  **Only where an object on the command line carries one.** lnk6x carries the runtime's
     *  DWARF into every image, debug build or not, and keeps or drops its compile units by a
     *  reachability over .debug_info that this linker does not model - so an image built from
     *  objects without a line table stays exactly as it was (the bed's references, the referee's
     *  images), and a -g build carries every unit whose own code is in the image, a grouped
     *  (header-file) unit once per group signature. docs/known.md names the residue. */
    bool anyLines = false;
    for (size_t mi = 0; mi < cmdMods && mi < mods.size() && !anyLines; mi++)
        for (size_t si = 1; si < mods[mi].secs.size() && !anyLines; si++) {
            const InSec &c = mods[mi].secs[si];
            anyLines = !(c.flags & SHF_ALLOC) && c.type == SHT_PROGBITS && c.name == ".debug_line" && c.size != 0;
        }
    std::map<std::string, bool> dwGroups;
    for (size_t mi = 0; mi < mods.size() && anyLines; mi++) {
        std::map<u32, std::string> signature;         /* a group member's section index -> the group's name */
        for (size_t si = 1; si < mods[mi].secs.size(); si++) {
            const InSec &g = mods[mi].secs[si];
            if (g.type != 17 || g.data.size() < 8) continue;        /* SHT_GROUP: flags, then the members */
            for (size_t k = 4; k + 4 <= g.data.size(); k += 4)
                signature[rd32(&g.data[k])] = g.name;
        }
        for (size_t si = 1; si < mods[mi].secs.size(); si++) {
            InSec &c = mods[mi].secs[si];
            if ((c.flags & SHF_ALLOC) || c.type != SHT_PROGBITS || c.name != ".debug_line" || c.size == 0) continue;
            if (c.flags & SHF_GROUP) {
                std::map<u32, std::string>::const_iterator sg = signature.find((u32)si);
                const std::string key = sg == signature.end() ? c.name : sg->second;
                if (dwGroups[key]) continue;
                dwGroups[key] = true;
            }
            bool dead = false;                            /* its code was never pulled, or was eliminated */
            for (size_t r = 0; r < c.relocs.size() && !dead; r++) {
                const Sym &y = mods[mi].syms[c.relocs[r].sym];
                if (y.shndx == SHN_ABS || y.shndx == SHN_UNDEF) continue;
                dead = y.shndx >= mods[mi].secs.size() || !mods[mi].secs[y.shndx].live || mods[mi].secs[y.shndx].out < 0;
            }
            if (dead) continue;
            int oi = out_index(".debug_line");
            if (oi < 0) {
                OutSec o;
                o.name = ".debug_line"; o.flags = 0; o.entsize = 0;
                o.type = SHT_PROGBITS; o.addr = 0; o.size = 0; o.align = 1; o.offset = 0;
                o.pflags = 0; o.progbits = true; o.debug = true;
                outs.push_back(o);
                oi = (int)outs.size() - 1;
            }
            c.out = oi;
            c.live = true;
            outs[oi].parts.push_back((int)all.size());
            all.push_back(&c);
        }
    }

    /*  **.gride.dwtag is carried the same way** - the variables c90, cpp11 and shalimar name with
     *  .dwtag (-g), kept by asm6x as text: no relocations, the parts one after another, for
     *  sim6747 --run --debug to show a function's locals. Only the objects on the command line
     *  carry one; an image built without -g stays as it was. */
    for (size_t mi = 0; mi < cmdMods && mi < mods.size(); mi++)
        for (size_t si = 1; si < mods[mi].secs.size(); si++) {
            InSec &c = mods[mi].secs[si];
            if ((c.flags & SHF_ALLOC) || c.type != SHT_PROGBITS || c.name != ".gride.dwtag" || c.size == 0) continue;
            int oi = out_index(".gride.dwtag");
            if (oi < 0) {
                OutSec o;
                o.name = ".gride.dwtag"; o.flags = 0; o.entsize = 0;
                o.type = SHT_PROGBITS; o.addr = 0; o.size = 0; o.align = 1; o.offset = 0;
                o.pflags = 0; o.progbits = true; o.debug = true;
                outs.push_back(o);
                oi = (int)outs.size() - 1;
            }
            c.out = oi;
            c.live = true;
            outs[oi].parts.push_back((int)all.size());
            all.push_back(&c);
        }

    /*  A `{ *(.text:early) *(.text) *(.text:*) }` list is an order, not decoration: the parts
     *  that a pattern names come first, in the order the patterns were written, and whatever
     *  the list does not name keeps the order it was read in after them. */
    for (size_t i = 0; i < cmd.secs.size() && i < outs.size(); i++) {
        const SecSpec &sp = cmd.secs[i];
        if (sp.inputs.empty() || outs[i].name != sp.name) continue;
        std::vector<int> &ps = outs[i].parts;
        std::vector<std::pair<size_t, int> > keyed;
        for (size_t k = 0; k < ps.size(); k++) {
            size_t rank = sp.inputs.size();
            for (size_t q = 0; q < sp.inputs.size(); q++)
                if (sec_matches(sp.inputs[q], all[ps[k]]->name)) { rank = q; break; }
            keyed.push_back(std::make_pair(rank, ps[k]));
        }
        std::stable_sort(keyed.begin(), keyed.end());
        for (size_t k = 0; k < ps.size(); k++) ps[k] = keyed[k].second;
    }

    for (size_t i = 0; i < outs.size(); i++) {
        OutSec &o = outs[i];
        if (o.debug) continue;
        for (size_t k = 0; k < o.parts.size(); k++) {
            InSec *c = all[o.parts[k]];
            if (c->align > o.align) o.align = c->align;
            /*  Bytes are anything not NOBITS: .c6xabi.exidx is SHT_C6000_UNWIND in every
             *  object and PROGBITS in lnk6x's image - written NOBITS, the index was never
             *  loaded and the unwinder had nothing to search. */
            if (c->type != SHT_NOBITS) o.progbits = true;
            /*  The flags of a section that has input come from that input, not from the table
             *  above: the table was read off *empty* sections, where lnk6x writes 0 for
             *  .const, .switch, .cio, .stack and .sysmem, and q13 shows all three of .const,
             *  .rodata and .switch with ALLOC once they hold bytes. A name the table does not
             *  know had 0 and was therefore never laid out at all - its bytes went to file
             *  offset 0, over the ELF header (the review's N3). */
            o.flags |= c->flags & (u32)(SHF_WRITE | SHF_ALLOC | SHF_EXECINSTR);
            o.pflags |= PF_R;
            if (c->flags & SHF_WRITE) o.pflags |= PF_W;
            if (c->flags & SHF_EXECINSTR) o.pflags |= PF_X;
        }
        o.type = o.progbits ? SHT_PROGBITS : SHT_NOBITS;
        o.unnamed = o.run.empty();
        /*  **A section whose name begins `.far` is writable**, code or not - lnk6x takes it for
         *  far data, like .far and .fardata. q15's and q26's .fartext hold nothing but a
         *  function and are flags 7, where q26's .midtext and q20's .mycode, code in sections
         *  just as unknown to it, are 6 and q20's .myconst is 2. */
        if (!o.parts.empty() && o.name.compare(0, 4, ".far") == 0) o.flags |= SHF_WRITE;
    }
    return true;
}

namespace {

/*  **Descending size, and a tie goes to the name.** The size is q07's map, plain enough.
 *  The tie-break was not - it is neither the archive's order (46% of 171 ties, which is
 *  chance) nor the module summary's - and it turned out to be the name after the colon,
 *  ascending, and for a bare `.text` or `.fardata` the *object's* name in its place: q07's
 *  tdeh_uwentry_c6000.obj (.text) follows fseek.obj (.text:fseek) and fib's fib.obj (.text)
 *  precedes memory.obj (.text:malloc), both at 0x180. 116 of 116 tie groups across six maps
 *  of lnk6x 7.4.4 and 8.2.2 agree; the one exclusion is `.c6xabi.exidx`, which lnk6x sorts
 *  by function address instead and this linker does not sort at all yet (docs/known.md). */
struct NameKey {
    std::string key, object;
    NameKey(const std::string &n, const std::string &obj)
        : key(n.find(':') == std::string::npos ? obj : n.substr(n.find(':') + 1)), object(obj) {}
    /*  Two contributions of one name from two objects go by the object's name, not by the
     *  order the objects came: q37 links q37-tie-input before q37-tie-aaa and lnk6x lays
     *  aaa's `.const:yy` before input's, and q07's two 4-byte `.const:.string` are
     *  exception_.obj's then typeinfo_.obj's (both linkers). */
    bool operator<(const NameKey &o) const { return key != o.key ? key < o.key : object < o.object; }
};

/*  The same question for whole output sections: bigger first, a tie by name. */
struct BiggerOut {
    const std::vector<OutSec> &outs;
    explicit BiggerOut(const std::vector<OutSec> &o) : outs(o) {}
    bool operator()(const std::pair<u32, int> &x, const std::pair<u32, int> &y) const {
        if (x.first != y.first) return x.first < y.first;    /* ~size: smaller ~ is bigger */
        return outs[x.second].name < outs[y.second].name;
    }
};

struct BiggerPart {
    const std::vector<InSec *> &all;
    const std::vector<Module> &mods;
    BiggerPart(const std::vector<InSec *> &a, const std::vector<Module> &m) : all(a), mods(m) {}
    bool operator()(int x, int y) const {
        if (all[x]->size != all[y]->size) return all[x]->size > all[y]->size;
        return NameKey(all[x]->name, mods[all[x]->module].name) <
               NameKey(all[y]->name, mods[all[y]->module].name);
    }
};
} // namespace

/* ------------------------------------------------------------- allocation */

bool Link::allocate()
{
    /*  **Every range starts empty.** allocate() is run twice under --rom_model - once to
     *  learn the sizes compose_cinit needs, and again once .cinit has its own - and `used`
     *  accumulates, so without this the second pass would lay everything after the first. */
    for (size_t i = 0; i < cmd.mem.size(); i++) cmd.mem[i].used = 0;
    /*  .stack and .sysmem are the linker's: nothing contributes to them, and their size is
     *  --stack_size and --heap_size - but only in a link that asked for the names, which is
     *  the runtime's doing. Without one both stay at zero, as every bare probe shows. */
    if (lnk_mod >= 0) {
        for (size_t k = 0; k < mods[lnk_mod].syms.size(); k++) {
            const std::string &n = mods[lnk_mod].syms[k].name;
            int oi = -1;
            if (n == "__TI_STACK_END" || n == "__TI_STACK_SIZE") oi = out_index(".stack");
            else if (n == "__TI_SYSMEM_SIZE") oi = out_index(".sysmem");
            if (oi < 0) continue;
            outs[oi].reserve = (n == "__TI_SYSMEM_SIZE") ? cmd.heap_size : cmd.stack_size;
        }
    }
    /*  **The heap is sized when the allocator is linked**, whoever names what: TI's
     *  memory.obj brings an input .sysmem of its own and never asks for __TI_SYSMEM_SIZE,
     *  and lnk6x makes the section --heap_size all the same (q07: 0x1000). */
    int hs = out_index(".sysmem");
    if (hs >= 0 && !outs[hs].parts.empty() && outs[hs].reserve == 0) outs[hs].reserve = cmd.heap_size;

    /*  **The order output sections are allocated in is descending size**, not the order
     *  the command file names them. q07's run is .stack 0x4000, .text 0x3FC0, .sysmem
     *  0x1000, .const 0x26A, .c6xabi.extab 0x7C, .fardata 0x20, .switch 0x14, and q18's
     *  is the same with .data 0x80 in its place.
     *
     *  Two are held back to the end whatever their size, in the order the file names
     *  them: `.cinit` and `.c6xabi.exidx`. Both are tables that describe the rest of the
     *  image - the load images carry run addresses, the index is sorted by function
     *  address - so a linker that placed them among the others would be deciding their
     *  contents from their own position. q07 puts .cinit at 0x30 and .exidx at 0x148
     *  after .switch at 0x14, which no size rule explains and this one does.
     *
     *  `.bss` is not a case of its own: q03 and q12 lay it first because it is the largest
     *  section there, and sieve's 0x4E21 .bss follows its 0x9180 .text (lnk6x 7.4.4). */
    std::map<Range *, std::vector<std::pair<u32, u32> > > gaps;   /* per range, [start, end) */
    std::vector<int> order;
    std::vector<std::pair<u32, int> > rest;      /* -size, so a sort puts the big first */
    std::vector<int> held, unnamed;
    for (size_t i = 0; i < outs.size(); i++) {
        if (outs[i].debug) {                      /* carried, not allocated: its parts one after another from 0 */
            u32 at = 0;
            for (size_t p = 0; p < outs[i].parts.size(); p++) { InSec *c = all[outs[i].parts[p]]; c->addr = c->load = at; at += c->size; }
            outs[i].addr = 0; outs[i].size = at; outs[i].rank = 1 << 30;
            continue;
        }
        /*  A section the command file never names comes after every one it does, whatever
         *  its size - q12's `.mybss` is 0x20 and still follows `.neardata` at 4 (N12). */
        if (outs[i].unnamed) { unnamed.push_back((int)i); continue; }
        if (outs[i].name == ".cinit" || outs[i].name == ".c6xabi.exidx") { held.push_back((int)i); continue; }
        /*  The reservation is a floor under the parts, not a term added to them: .sysmem is
         *  0x800 with memory.obj's 8 bytes inside it, and lnk6x lays .stack, a tie by name, first. */
        u32 want = 0;
        for (size_t p = 0; p < outs[i].parts.size(); p++) {
            InSec *c = all[outs[i].parts[p]];
            want = align_up(want, c->align) + c->size;
        }
        if (want < outs[i].reserve) want = outs[i].reserve;
        rest.push_back(std::make_pair(~want, (int)i));   /* ~ rather than -, for unsigned */
    }
    /*  **A tie between two output sections goes to the name, ascending** - q19 has .const
     *  and .text both 0x40 and lnk6x puts .const first. It cannot be the command file's
     *  order: q19 is linked twice, from flat.cmd and from order.cmd, which name the six
     *  sections differently, and lnk6x lays them out identically both times. */
    std::stable_sort(rest.begin(), rest.end(), BiggerOut(outs));
    for (size_t i = 0; i < rest.size(); i++) order.push_back(rest[i].second);
    /*  The sections the file never names go by size among themselves as well: q20's .mycode
     *  (0x20) is laid before its .myconst (4), though the object has .myconst first. */
    {
        std::vector<std::pair<u32, int> > un;
        for (size_t i = 0; i < unnamed.size(); i++) {
            u32 want = 0;
            const OutSec &uo = outs[unnamed[i]];
            for (size_t p = 0; p < uo.parts.size(); p++) {
                InSec *c = all[uo.parts[p]];
                want = align_up(want, c->align) + c->size;
            }
            if (want < uo.reserve) want = uo.reserve;
            un.push_back(std::make_pair(~want, unnamed[i]));
        }
        std::stable_sort(un.begin(), un.end(), BiggerOut(outs));
        for (size_t i = 0; i < un.size(); i++) unnamed[i] = un[i].second;
    }
    for (size_t i = 0; i < held.size(); i++) order.push_back(held[i]);
    for (size_t i = 0; i < unnamed.size(); i++) order.push_back(unnamed[i]);
    for (size_t k = 0; k < order.size(); k++) outs[order[k]].rank = (int)k;

    for (size_t k = 0; k < order.size(); k++) {
        OutSec &o = outs[order[k]];
        Range *r = o.run.empty() ? 0 : cmd.range(o.run);
        if (o.parts.empty() && !o.reserve) {
            /*  An empty section still has an address when it holds bytes - a fill makes it
             *  initialised - and none at all when it does not. */
            o.addr = (o.progbits && r) ? r->origin : 0;
            continue;
        }
        if (o.parts.empty()) {                       /* .stack, .sysmem: size without input */
            if (!r) r = cmd.mem.empty() ? 0 : &cmd.mem[0];
            if (!r) { err = o.name + ": no memory range for it"; return false; }
            /*  A section the linker alone sizes is writable, allocated and 8-aligned in its
             *  header - q07's .stack, f3/al8 - where an empty one keeps the table's 0. */
            o.flags = SHF_ALLOC | SHF_WRITE; o.pflags = PF_R | PF_W;
            if (o.align < 8) o.align = 8;
            u32 at = align_up(r->origin + r->used, o.align > 8 ? o.align : 8);
            o.addr = at; o.size = o.reserve;
            at += o.reserve;
            if (at - r->origin > r->length) { err = o.name + ": does not fit in " + r->name; return false; }
            r->used = at - r->origin;
            continue;
        }
        /*  **lnk6x places a section's contributions in descending size**, not in the order
         *  they were read - q07's `.text` runs 0x640, 0x580, 0x4C0, 0x440, ... for the
         *  whole of the run, and its map is the evidence. A tie goes to the name - see
         *  BiggerPart, which is where the reading of it is written down. The unwind index
         *  is the one exception: its order is its functions' addresses, and it is
         *  composed here, once the code it describes has been placed. */
        if (o.name == ".c6xabi.exidx") { if (!compose_exidx(o)) return false; }
        /*  **8.2.2 keeps .init_array in input order** - q35 links a (one entry) then b (two)
         *  and gets a, b; reversed with a third it gets b, a, 0 - where 7.4.4 lays it by size
         *  like everything else: 0, b, a both times. The order is the order the
         *  constructors run in, which 8.2.2 leaves to the objects' order on the line. */
        else if (!(o.name == ".init_array" && !opt.cgt744))
            std::stable_sort(o.parts.begin(), o.parts.end(), BiggerPart(all, mods));
        /*  **Trampolines go at the end of their caller's output section**, after every input
         *  section of it, in the order they were made: q21's follows .text:small, not the
         *  .text:caller that called for it, and q24's follows the second object's .text. */
        if (!tramps.empty()) {
            std::vector<int> ps;
            for (size_t p = 0; p < o.parts.size(); p++)
                if (all[o.parts[p]]->tramp < 0) ps.push_back(o.parts[p]);
            for (size_t t = 0; t < tramps.size(); t++)
                if (all[tramps[t].in]->out == order[k]) ps.push_back(tramps[t].in);
            o.parts.swap(ps);
        }
        /*  **A piece goes into the first gap alignment left behind it, if it fits** - lnk6x
         *  fills its holes. q07's .const puts two 4-byte strings at 0x905c and 0x9084, in
         *  front of 8-aligned typeinfo names, where appending left them at the end and made
         *  the section 8 bytes longer than the oracle's. Gaps are tried lowest first.
         *
         *  **And where the parts land is asked before the section is put anywhere**, of every
         *  place it might go: their sum in input order can be less than they take laid out. q39's
         *  .const fitted a 28-byte gap by its input order (26), then took 32 laid by size, and its
         *  last word went over .text's first instruction - which lnk6x lays its .const after. */
        auto lay_out = [&](u32 base, bool commit) -> u32 {
            u32 end = base;
            std::vector<std::pair<u32, u32> > holes;        /* [start, end) */
            for (size_t p = 0; p < o.parts.size(); p++) {
                InSec *c = all[o.parts[p]];
                bool placed = false;
                /*  Never into a hole: trampolines stay at the section's end, as every probe has them. */
                for (size_t h = 0; h < holes.size() && !placed && c->tramp < 0; h++) {
                    u32 s = align_up(holes[h].first, c->align);
                    if (s + c->size > holes[h].second) continue;
                    std::pair<u32, u32> was = holes[h];
                    holes.erase(holes.begin() + (long)h);
                    if (s + c->size < was.second) holes.insert(holes.begin() + (long)h, std::make_pair(s + c->size, was.second));
                    if (was.first < s) holes.insert(holes.begin() + (long)h, std::make_pair(was.first, s));
                    if (commit) c->addr = s;
                    placed = true;
                }
                if (!placed) {
                    u32 s = align_up(end, c->align);
                    if (s > end) holes.push_back(std::make_pair(end, s));
                    if (commit) c->addr = s;
                    end = s + c->size;
                }
                if (commit) c->load = c->addr;
            }
            /*  A reservation is a floor, not an addition: .sysmem holds memory.obj's own 8 bytes
             *  and is still exactly --heap_size in q07's reference image. */
            if (end - base < o.reserve) end = base + o.reserve;
            return end;
        };
        if (!r) {
            /*  A section this command file never names - q12's `.mybss`, from a `.usect` -
             *  is allocated after the named ones, in the first range that still has room for
             *  it. q12 puts it at 0xC0000064, after `.neardata`, in the only range there is
             *  (the review's N12). */
            for (size_t m = 0; m < cmd.mem.size() && !r; m++) {
                u32 at = align_up(cmd.mem[m].origin + cmd.mem[m].used, o.align);
                if (lay_out(at, false) - cmd.mem[m].origin <= cmd.mem[m].length) r = &cmd.mem[m];
            }
            if (!r) { err = o.name + ": no memory range for it"; return false; }
            o.run = o.load = r->name;
        }
        /*  **An output section goes into the first gap alignment left between the ones
         *  before it, if it fits** - lnk6x's throw probe puts its 4-byte .bss at 8000aab4,
         *  between .const's end at 8000aab2 and .fardata's 8-aligned start at 8000aab8. It
         *  fits by the size its parts take laid out there, not by their sum in input order. */
        u32 al = o.align;
        for (size_t i = 0; i < cmd.secs.size(); i++)
            if (cmd.secs[i].name == o.name && cmd.secs[i].has_align && cmd.secs[i].align > al) al = cmd.secs[i].align;
        std::vector<std::pair<u32, u32> > &gap = gaps[r];
        bool in_gap = false;
        u32 at = 0;
        for (size_t h = 0; h < gap.size() && !in_gap; h++) {
            u32 s = align_up(gap[h].first, al);
            if (s > gap[h].second) continue;
            u32 e = lay_out(s, false);
            if (e > gap[h].second) continue;
            std::pair<u32, u32> was = gap[h];
            gap.erase(gap.begin() + (long)h);
            if (e < was.second) gap.insert(gap.begin() + (long)h, std::make_pair(e, was.second));
            if (was.first < s) gap.insert(gap.begin() + (long)h, std::make_pair(was.first, s));
            at = s;
            in_gap = true;
        }
        if (!in_gap) {
            u32 cur = r->origin + r->used;
            at = align_up(cur, al);
            if (at > cur) gap.push_back(std::make_pair(cur, at));
        }
        o.addr = at;
        at = lay_out(o.addr, true);
        o.size = at - o.addr;
        if (at - r->origin > r->length) { err = o.name + ": does not fit in " + o.run; return false; }
        if (!in_gap) r->used = at - r->origin;
    }

    int sb = out_index(".bss");
    static_base = (sb >= 0) ? outs[sb].addr : 0;
    set_linker_symbols();

    std::map<std::string, std::pair<int, int> >::iterator e = defined.find(opt.entry);
    if (!sym_addr(e->second.first, e->second.second, entry_addr)) return false;
    return true;
}

/* ------------------------------------------------------------- the unwind index */

/*  **The index is one table the linker composes**, not the objects' sections laid end to
 *  end - read off lnk6x 7.4.4's fib, whose 58 entries are 34 of the objects' and 24 of the
 *  linker's own. Each entry is two words: a PREL31 to its function, and either a PREL31 to
 *  its .c6xabi.extab record, EXIDX_CANTUNWIND, or the unwind instructions themselves with
 *  the top bit set. lnk6x sorts them by function address - the unwinder finds a PC's entry
 *  by binary search - gives every run of code with no entry one that says cantunwind, at
 *  the run's first byte, and folds an entry into the one before it when both carry the
 *  same word: `__c6xabi_unwind_cpp_pr1` through `_pr4` sit behind `_pr0` under its
 *  83000207, and the objects' own cantunwind entries vanish into the runs. An entry
 *  covers the code up to the next entry's function, so the folding changes nothing the
 *  unwinder reads. The objects' entries were followed by eliminate() already, which is
 *  what keeps the personality routines and the extab records they name; here they are
 *  taken apart and their sections leave the image. Run once per allocation pass. */
namespace {
struct ExidxEntry {
    u32  fn;          /* the function's address */
    bool prel;        /* the second word is a PREL31 to `target`; else `word` as it stands */
    u32  target, word;
    /*  What fn and target were read from - kept, because an address taken here can still move:
     *  a map that names neither unwind section lays .c6xabi.extab after this table is composed. */
    int  fmod, tmod;
    u32  fsym, tsym;
    i32  fadd, tadd;
    bool alone;       /* the only entry of its section - the ones lnk6x folds */
    u32  unit, seq;   /* its section's code address, and its place in that section */
    bool operator<(const ExidxEntry &o) const { return unit != o.unit ? unit < o.unit : seq < o.seq; }
};
} // namespace

bool Link::compose_exidx(OutSec &o)
{
    const int oi = (int)(&o - &outs[0]);
    if (exidx_input.empty() && exidx_in < 0) exidx_input = o.parts;

    /*  **An input index section is one unit, kept in its own order, and the units go by
     *  the address of the code each describes** - not every entry by its function. cpp11
     *  writes one `.c6xabi.exidx:.text` for all of a file's functions, and lnk6x 7.4.4 lays
     *  its 98 entries first and whole, four of them naming the runtime's own `std::exception`
     *  members at the far end of .text (their weak definitions in the file's .text lost to
     *  the library's), where sorting every entry by its function put those four last and
     *  the cbs C++ image 465 bytes of index away from lnk6x's. The runtime's one-entry
     *  sections sort the same either way. */
    std::vector<ExidxEntry> entries;
    std::map<std::pair<int, int>, bool> covered;    /* (module, code section) with an entry */
    for (size_t k = 0; k < exidx_input.size(); k++) {
        InSec *c = all[exidx_input[k]];
        c->out = -1;                                /* its bytes go into the composed table */
        if (c->link) covered[std::make_pair(c->module, (int)c->link)] = true;
        u32 unit = 0;
        if (c->link && c->link < mods[c->module].secs.size()) unit = mods[c->module].secs[c->link].addr;
        for (size_t at = 0; at + 8 <= c->data.size(); at += 8) {
            ExidxEntry e;
            e.fn = 0; e.prel = false; e.target = 0; e.word = rd32(&c->data[at + 4]);
            e.fmod = e.tmod = -1; e.fsym = e.tsym = 0; e.fadd = e.tadd = 0;
            e.alone = (c->data.size() == 8);
            e.unit = unit; e.seq = (u32)(at / 8);
            bool has_fn = false;
            for (size_t r = 0; r < c->relocs.size(); r++) {
                const Rel &rl = c->relocs[r];
                /*  Only the PREL31s: an entry also carries an R_C6000_NONE against its
                 *  personality routine, at offset 0, which is there to be followed by
                 *  eliminate() and writes nothing. */
                if ((rl.offset != at && rl.offset != at + 4) || rl.type != R_C6000_PREL31) continue;
                u32 S;
                if (!sym_addr(c->module, rl.sym, S)) return false;
                if (rl.offset == at) { e.fn = S + (u32)rl.addend; has_fn = true; e.fmod = c->module; e.fsym = rl.sym; e.fadd = rl.addend; }
                else { e.prel = true; e.target = S + (u32)rl.addend; e.tmod = c->module; e.tsym = rl.sym; e.tadd = rl.addend; }
            }
            if (!has_fn) { err = c->name + ": an unwind index entry names no function"; return false; }
            if (!c->link) e.unit = e.fn;
            entries.push_back(e);
        }
    }

    /* every placed piece of code, in address order, and a cantunwind for each run without one */
    std::vector<InSec *> code;
    for (size_t i = 0; i < outs.size(); i++) {
        if (!(outs[i].flags & SHF_EXECINSTR) || (int)i == oi) continue;
        for (size_t p = 0; p < outs[i].parts.size(); p++)
            if (all[outs[i].parts[p]]->size) code.push_back(all[outs[i].parts[p]]);
    }
    struct ByAddr { bool operator()(const InSec *a, const InSec *b) const { return a->addr < b->addr; } };
    std::stable_sort(code.begin(), code.end(), ByAddr());
    bool was = true;
    for (size_t k = 0; k < code.size(); k++) {
        bool has = covered.find(std::make_pair(code[k]->module, code[k]->index)) != covered.end();
        if (!has && was) {
            ExidxEntry e;
            e.fn = code[k]->addr; e.prel = false; e.target = 0; e.word = EXIDX_CANTUNWIND; e.alone = true;
            e.fmod = e.tmod = -1; e.fsym = e.tsym = 0; e.fadd = e.tadd = 0;
            e.unit = e.fn; e.seq = 0;
            entries.push_back(e);
        }
        was = has;
    }
    std::stable_sort(entries.begin(), entries.end());

    /*  The same word as the entry before, and not a pointer: one entry serves both. Only
     *  an entry that is its section's whole index is folded - cpp11 writes one section for
     *  all of a file's functions, and lnk6x keeps `main`'s entry behind `fib`'s identical
     *  one where it folds tdeh_cpp_abi.obj's one-entry sections into each other. */
    std::vector<ExidxEntry> kept;
    for (size_t k = 0; k < entries.size(); k++) {
        const ExidxEntry &e = entries[k];
        if (!kept.empty() && e.alone && !e.prel && !kept.back().prel && e.word == kept.back().word) continue;
        kept.push_back(e);
    }

    InSec *c;
    if (exidx_in < 0) {
        c = new InSec();
        c->name = ".c6xabi.exidx"; c->type = SHT_PROGBITS; c->flags = SHF_ALLOC | 0x80;
        c->align = 4; c->entsize = 8; c->module = lnk_mod; c->index = 0;
        c->live = true; c->dropped = false; c->exidx_synth = true;
        all.push_back(c);
        exidx_in = (int)all.size() - 1;
    } else c = all[exidx_in];
    c->out = oi;
    c->addr = c->load = 0;
    c->size = 8 * (u32)kept.size();
    c->data.assign(c->size, 0);
    c->relocs.clear();
    c->exidx_src.clear();
    for (size_t k = 0; k < kept.size(); k++) {
        /*  Carried as relocations of the linker's own, so fix_up writes them like any
         *  others once the table has an address: PREL31 in halfwords, the flag bit kept. Each
         *  names the symbol it was read from, resolved then and not now (exidx_src). */
        const ExidxEntry &e = kept[k];
        Rel r; r.offset = (u32)(8 * k); r.type = R_C6000_PREL31;
        r.sym = e.fsym; r.addend = e.fmod >= 0 ? e.fadd : (i32)e.fn;
        c->relocs.push_back(r); c->exidx_src.push_back(std::make_pair(e.fmod, e.fsym));
        if (e.prel) {
            r.offset += 4; r.sym = e.tsym; r.addend = e.tmod >= 0 ? e.tadd : (i32)e.target;
            c->relocs.push_back(r); c->exidx_src.push_back(std::make_pair(e.tmod, e.tsym));
        } else wr32(&c->data[8 * k + 4], e.word);
    }
    o.parts.clear();
    if (!kept.empty()) o.parts.push_back(exidx_in);
    if (o.align < 4) o.align = 4;
    return true;
}

bool Link::sym_addr(int mod, int sym, u32 &a)
{
    const Sym &y = mods[mod].syms[sym];
    /* the same rule as in eliminate: a name that is not the object's own is the table's */
    if ((y.info >> 4) != STB_LOCAL && !y.name.empty()) {
        std::map<std::string, std::pair<int, int> >::iterator d = defined.find(y.name);
        if (d != defined.end() &&
            (d->second.first != mod || d->second.second != sym))
            return sym_addr(d->second.first, d->second.second, a);
    }
    if (y.alias >= 0) {
        int am = mod, as = sym;
        resolve_alias(am, as);
        if (am != mod || as != sym) return sym_addr(am, as, a);
    }
    if (y.shndx == SHN_ABS) { a = y.value; return true; }
    if (y.shndx == SHN_UNDEF) {
        if ((y.info >> 4) == STB_WEAK) { a = 0; return true; }
        err = "unresolved symbol: " + y.name; return false;
    }
    if (y.shndx >= mods[mod].secs.size()) { err = y.name + ": names no section"; return false; }
    const InSec &c = mods[mod].secs[y.shndx];
    if (!c.live) { err = y.name + ": in a section that was eliminated"; return false; }
    a = c.addr + y.value;
    return true;
}

/* ------------------------------------------------------------- far-call trampolines */

/*  A trampoline is to a place - an input section and an offset - not to an address, which
 *  moves with every pass of the layout. */
int Link::tramp_for(int tm, int tsec, u32 toff, u32 P) const
{
    for (size_t t = 0; t < tramps.size(); t++) {
        const Tramp &r = tramps[t];
        const InSec *c = all[r.in];
        if (r.tmod == tm && r.tsec == tsec && r.toff == toff && c->out >= 0 && pcr_s21_reaches(P, c->addr))
            return (int)t;
    }
    return -1;
}

void Link::retarget_tramps()
{
    for (size_t t = 0; t < tramps.size(); t++) {
        Tramp &r = tramps[t];
        const Module &m = mods[r.tmod];
        r.target = (r.tsec > 0 && r.tsec < (int)m.secs.size()) ? m.secs[r.tsec].addr + r.toff : r.toff;
    }
}

/*  The symbol a relocation names, followed to its definition as sym_addr follows it. */
void Link::callee_of(int mod, u32 sym, int &tm, int &ts) const
{
    tm = mod; ts = (int)sym;
    const Sym &y = mods[mod].syms[sym];
    if ((y.info >> 4) != STB_LOCAL && !y.name.empty()) {
        std::map<std::string, std::pair<int, int> >::const_iterator d = defined.find(y.name);
        if (d != defined.end()) { tm = d->second.first; ts = d->second.second; }
    }
    resolve_alias(tm, ts);
}

/*  **lnk6x decides a call as it lays the caller's section out**, so a callee in an output
 *  section laid later has no address yet and gets a trampoline however near it lands: q20's
 *  call to .mycode, unnamed and so laid after .text, goes through $Tramp$S$$near, 0x38
 *  bytes from its target - and q29's to a *named* section laid after .text by size, so it is
 *  the order of allocation. */
bool Link::wants_tramp(const InSec *c, u32 P, u32 V, int tm, int ts) const
{
    if (!pcr_s21_reaches(P, V)) return true;
    const Sym &t = mods[tm].syms[ts];
    if (t.shndx == SHN_UNDEF || t.shndx >= mods[tm].secs.size()) return false;
    int co = mods[tm].secs[t.shndx].out;
    if (co < 0 || co == c->out) return false;
    return outs[co].rank > outs[c->out].rank;
}

/*  One pass over every PCR_S21 in the placed code. A branch that needs no trampoline, or
 *  reaches one to its callee, is left; for the rest, one trampoline per callee per pass - the
 *  next call to the same callee may well reach it once it has an address, and the next pass
 *  asks. **Made in descending order of each callee's lowest call** and **named by that call's
 *  symbol**: q28's faraway2 (called at 0x4) goes before faraway (0x0 and 0x8), and q23's
 *  shared one is $Tramp$S$$faralias, the name only its call at 0x0 used. */
bool Link::trampolines(bool &added)
{
    added = false;
    retarget_tramps();
    struct Far { u32 P; int in; u32 V; u32 sym; int tm, ts; };
    std::vector<Far> far;
    for (size_t i = 0; i < all.size(); i++) {
        InSec *c = all[i];
        if (c->data.empty() || c->out < 0 || c->tramp >= 0 || c->exidx_synth) continue;
        for (size_t r = 0; r < c->relocs.size(); r++) {
            const Rel &rl = c->relocs[r];
            if (rl.type != R_C6000_PCR_S21) continue;
            u32 S;
            if (!sym_addr(c->module, rl.sym, S)) return false;
            u32 V = S + (u32)rl.addend, P = c->addr + rl.offset;
            Far f; f.P = P; f.in = (int)i; f.V = V; f.sym = rl.sym;
            callee_of(c->module, rl.sym, f.tm, f.ts);
            if (!wants_tramp(c, P, V, f.tm, f.ts) ||
                tramp_for(f.tm, mods[f.tm].syms[f.ts].shndx, mods[f.tm].syms[f.ts].value, P) >= 0) continue;
            far.push_back(f);
        }
    }
    struct ByP { bool operator()(const Far &a, const Far &b) const { return a.P < b.P; } };
    std::stable_sort(far.begin(), far.end(), ByP());
    /* per callee: the lowest call names and orders it; the highest is the caller it follows */
    std::map<u32, size_t> lowest, highest;
    for (size_t k = 0; k < far.size(); k++) {
        if (!lowest.count(far[k].V)) lowest[far[k].V] = k;
        highest[far[k].V] = k;
    }
    for (size_t kk = far.size(); kk-- > 0; ) {
        if (lowest[far[kk].V] != kk) continue;
        const Far &f = far[kk];
        const Far &h = far[highest[far[kk].V]];
        /*  Its own section has one already and still cannot reach it - a caller past 4 MB.
         *  Another would be no nearer: fix_up refuses it by name, and the loop ends. */
        bool own = false;
        for (size_t t = 0; t < tramps.size() && !own; t++)
            own = tramps[t].tmod == f.tm && tramps[t].tsec == (int)mods[f.tm].syms[f.ts].shndx &&
                  tramps[t].toff == mods[f.tm].syms[f.ts].value && tramps[t].caller == h.in;
        if (own) continue;
        InSec *caller = all[h.in];
        const Module &m = mods[all[f.in]->module];
        Tramp t;
        t.target = f.V;
        /*  Named by the symbol the call was written against: q15's CALLP names `faraway`,
         *  q25's a local label, `localfar`. */
        const Sym &y = m.syms[f.sym];
        std::string nm = y.name;
        if (nm.empty() && y.shndx < m.secs.size()) nm = m.secs[y.shndx].name;
        t.name = "$Tramp$S$$" + nm;
        t.caller = h.in;
        /*  The map names the callee by its input section and offset, `$.fartext:q15-far.obj$0x0`. */
        t.tmod = f.tm; t.tsec = mods[f.tm].syms[f.ts].shndx; t.toff = mods[f.tm].syms[f.ts].value;
        InSec *c = new InSec();
        c->name = t.name; c->type = SHT_PROGBITS; c->flags = SHF_ALLOC | SHF_EXECINSTR;
        c->size = 32; c->align = 32; c->entsize = 0;
        c->data.assign(32, 0);
        tramp_code(&c->data[0], f.V, 0);
        /*  In the module of the section it follows - q24's in q24-two-b.obj, whose .text is
         *  the last of .text, though q24-two-a.obj's call is the lower. */
        c->module = caller->module; c->index = -1;
        c->live = true; c->out = caller->out; c->addr = c->load = 0;
        c->tramp = (int)tramps.size();
        all.push_back(c);
        t.in = (int)all.size() - 1;
        outs[caller->out].parts.push_back(t.in);
        tramps.push_back(t);
        added = true;
    }
    return true;
}

bool Link::layout()
{
    if (!allocate()) return false;
    for (;;) {
        bool added = false;
        if (!trampolines(added)) return false;
        if (!added) return true;
        if (!allocate()) return false;
    }
}

bool Link::fix_up(bool check_range)
{
    /*  Every relocation out of range is named before the link stops, not only the first. */
    int bad = 0;
    err.clear();
    retarget_tramps();
    for (size_t t = 0; t < tramps.size(); t++) {
        tramps[t].calls.clear();
        tramp_code(&all[tramps[t].in]->data[0], tramps[t].target, all[tramps[t].in]->addr);
    }
    for (size_t i = 0; i < all.size(); i++) {
        InSec *c = all[i];
        if (c->data.empty() || c->out < 0) continue;
        for (size_t r = 0; r < c->relocs.size(); r++) {
            const Rel &rl = c->relocs[r];
            if (rl.offset + reloc_width(rl.type) > c->data.size()) { err = c->name + ": a relocation falls past its section"; return false; }
            /*  A relocation of the linker's own - the unwind index it composed - is against the
             *  symbol exidx_src names, or, where that is none, its addend is the address itself. */
            u32 S = 0;
            i32 A = rl.addend;
            if (!c->exidx_synth && !sym_addr(c->module, rl.sym, S)) {
                if (!outs[c->out].debug) return false;
                S = 0; err.clear();                  /* a line-table row of a section that was eliminated */
            }
            if (c->exidx_synth && r < c->exidx_src.size() && c->exidx_src[r].first >= 0 &&
                !sym_addr(c->exidx_src[r].first, c->exidx_src[r].second, S)) return false;
            /*  A far call goes to its trampoline instead. */
            int ctm, cts;
            if (rl.type == R_C6000_PCR_S21 && !c->exidx_synth && c->tramp < 0 &&
                (callee_of(c->module, rl.sym, ctm, cts),
                 wants_tramp(c, c->addr + rl.offset, S + (u32)A, ctm, cts))) {
                int t = tramp_for(ctm, mods[ctm].syms[cts].shndx, mods[ctm].syms[cts].value, c->addr + rl.offset);
                /*  Only a branch out of reach is sent there: q20's call to .mycode made a
                 *  trampoline and the map lists it, but the CALLP goes straight to `near`. */
                if (t >= 0) {
                    tramps[t].calls.push_back(std::make_pair((int)i, rl.offset));
                    if (!pcr_s21_reaches(c->addr + rl.offset, S + (u32)A)) { S = all[tramps[t].in]->addr; A = 0; }
                }
            }
            std::string e2; bool range = false;
            if (!apply_reloc(rl.type, &c->data[rl.offset], c->addr + rl.offset, S, A, static_base, e2, range)) {
                if (!range) { err = c->name + ": " + e2; return false; }
                if (!check_range) {                  /* write it masked; the next pass decides */
                    apply_reloc_unchecked(rl.type, &c->data[rl.offset], c->addr + rl.offset, S, A, static_base);
                    continue;
                }
                /*  Named the way lnk6x's own diagnostics name a place: the symbol, the
                 *  relocation, the object and section and offset, then what did not fit. A
                 *  far call wants a trampoline, which this linker does not write yet
                 *  (docs/known.md has lnk6x's). */
                const Module &m = mods[c->module];
                std::string who;
                if (c->exidx_synth) who = "the unwind index";
                else {
                    const Sym &y = m.syms[rl.sym];
                    if (!y.name.empty()) who = "\"" + y.name + "\"";
                    else if (y.shndx < m.secs.size()) who = "section " + m.secs[y.shndx].name;
                    else who = "symbol " + std::to_string(rl.sym);
                }
                char b[160];
                snprintf(b, sizeof b, " (0x%08x) at %s(%s)+0x%x, address 0x%08x: ",
                         S + (u32)A, m.name.c_str(), c->name.c_str(),
                         (unsigned)rl.offset, (unsigned)(c->addr + rl.offset));
                if (!err.empty()) err += "\nlnk6x: ";
                err += std::string("relocation ") + reloc_name(rl.type) + " to " + who + b + e2;
                if (rl.type == R_C6000_PCR_S21) err += " - and its trampoline is out of reach too";
                ++bad;
            }
        }
    }
    return bad == 0;
}

/* ------------------------------------------------------------- .cinit, --rom_model */

namespace {

/*  **lnk6x's escape byte is the least frequent value in the data, the smallest on a tie**
 *  - which is the smallest absent value whenever one is absent: 0x00 for q05 (44 33 22 11),
 *  0x40 for q18. The review's probe holds every value 0..255 and 7.4.4 picks 0x0A, the
 *  smallest of those that occur once. */
u8 escape_for(const std::vector<u8> &d)
{
    size_t n[256];
    for (int i = 0; i < 256; i++) n[i] = 0;
    for (size_t i = 0; i < d.size(); i++) n[d[i]]++;
    int best = 0;
    for (int i = 1; i < 256; i++) if (n[i] < n[best]) best = i;
    return (u8)best;
}

/*  The rle24 stream `__TI_decompress_rle_core` reads (rts6740 7.4.4, read off dis6x): the
 *  escape byte, then literals, and the escape introducing a count. A count of 1 to 3 is
 *  that many copies of the escape itself and no value follows; 4 to 255 is a run of the
 *  value after it; 0 is the long form - a 16-bit big-endian length, a run of 256 to 65535,
 *  or, when that is below 256, the top of a 24-bit one with two more bytes - then the
 *  value. A long length of 0 ends the stream: `E 00 00 00`, four bytes (q18).
 *
 *  A run costs three bytes and saves one per byte beyond that, so four identical bytes are
 *  where it starts to pay; below that literals are shorter (q18, and the review's probe:
 *  `04 04 00` for four zeros, three zeros left as literals). */
void rle24_encode(const std::vector<u8> &d, u8 E, std::vector<u8> &out)
{
    out.push_back(E);
    size_t i = 0;
    while (i < d.size()) {
        size_t j = i;
        while (j < d.size() && d[j] == d[i] && j - i < 0xFFFFFF) j++;
        size_t run = j - i;
        const u8 v = d[i];
        if (v == E && run < 4) {                      /* the escape itself, 1 to 3 times */
            out.push_back(E); out.push_back((u8)run);
        } else if (run < 4) {
            for (size_t k = 0; k < run; k++) out.push_back(v);
        } else if (run < 256) {
            out.push_back(E); out.push_back((u8)run); out.push_back(v);
        } else if (run < 65536) {                     /* 300 x 7 is 0a 00 01 2c 07 */
            out.push_back(E); out.push_back(0);
            out.push_back((u8)(run >> 8)); out.push_back((u8)run); out.push_back(v);
        } else {                                      /* 70,000 x 0 is 0a 00 00 01 11 70 00 */
            out.push_back(E); out.push_back(0);
            out.push_back(0); out.push_back((u8)(run >> 16));
            out.push_back((u8)(run >> 8)); out.push_back((u8)run); out.push_back(v);
        }
        i = j;
    }
    out.push_back(E);
    out.push_back(0);
    out.push_back(0);
    out.push_back(0);
}

} // namespace

/*  **The load images, and room for the table that drives them.** Under --rom_model an
 *  initialised writable section is not in the image at its run address: its bytes go into
 *  .cinit as a load image and the section itself becomes SHT_NOBITS, which is what q05's
 *  `.data` is. This runs before allocation because .cinit's size moves everything after it.
 */
bool Link::compose_cinit()
{
    cinit_recs.clear();
    cinit_handlers.clear();
    std::vector<u8> image;
    /*  What .cinit is made of before it is laid out: a load image, the handler table
     *  (out -1) or a zero-fill record. */
    struct Piece { std::vector<u8> bytes; u32 align; int out; };
    std::vector<Piece> pieces;

    /*  **An uninitialised .bss or .far is zeroed through .cinit too** - .cio, also
     *  uninitialised, is not. 7.4.4 zeroes it with __TI_zero_init and an 8-byte record;
     *  8.2.2 writes an rle image of zeros, 9 bytes for a word and 0xD for 0x20000 (q31-q33,
     *  both linkers). A zero section is one that the zero handler was pulled for (7.4.4,
     *  zero_root), or any such section at all (8.2.2). */
    std::vector<int> zero_outs;
    if (zero_root || !opt.cgt744)
        for (size_t oi = 0; oi < outs.size(); oi++) {
            const OutSec &o = outs[oi];
            if ((o.flags & SHF_ALLOC) && (o.flags & SHF_WRITE) && o.type == SHT_NOBITS && o.size &&
                (o.name == ".far" || o.name == ".bss")) zero_outs.push_back((int)oi);
        }
    const bool zero_records = opt.cgt744 && !zero_outs.empty();

    /*  **The handler table is in the command file's order of the sections that need
     *  each handler**: the first that wants an rle image enters rle24 and none as a pair,
     *  the first that wants a zero record enters __TI_zero_init. 7.4.4's kernels name .far
     *  before .fardata in C6747.cmd and put zero_init at 0; hello.c and the Compiler++
     *  harness have a .data, named before .far, and put rle24 at 0, none 1, zero_init 2;
     *  q31-q33-744 under flat.cmd (.bss and .far before .fardata) put zero_init at 0. 8.2.2's
     *  table is rle24 then none in every image. */
    for (size_t oi = 0; oi < outs.size() && zero_records; oi++) {
        const OutSec &o = outs[oi];
        if (!(o.flags & SHF_ALLOC) || !(o.flags & SHF_WRITE) || o.size == 0) continue;
        if (o.name == ".cinit" || o.name == ".args" || o.name == ".init_array") continue;
        const bool zero = std::find(zero_outs.begin(), zero_outs.end(), (int)oi) != zero_outs.end();
        if (o.type == SHT_PROGBITS && cinit_handlers.empty()) {
            cinit_handlers.push_back("__TI_decompress_rle24");
            cinit_handlers.push_back("__TI_decompress_none");
        } else if (zero && std::find(cinit_handlers.begin(), cinit_handlers.end(), "__TI_zero_init") == cinit_handlers.end())
            cinit_handlers.push_back("__TI_zero_init");
    }
    if (cinit_handlers.empty() || cinit_handlers[0] == "__TI_zero_init") {
        cinit_handlers.push_back("__TI_decompress_rle24");
        cinit_handlers.push_back("__TI_decompress_none");
    }
    u8 rle_index = 0, zero_index = 0;
    for (size_t h = 0; h < cinit_handlers.size(); h++) {
        if (cinit_handlers[h] == "__TI_decompress_rle24") rle_index = (u8)h;
        if (cinit_handlers[h] == "__TI_zero_init") zero_index = (u8)h;
    }

    /*  **The records go in descending size of the section**, not in output-section order:
     *  7.4.4's isort puts .fardata (0x320) before .neardata (4), though .neardata is the
     *  lower, and the Compiler++ harness runs .fardata 0x6F0, .data 0x74, .neardata 0x1C. */
    std::vector<std::pair<u32, size_t> > by_size;
    for (size_t oi = 0; oi < outs.size(); oi++) by_size.push_back(std::make_pair(~outs[oi].size, oi));
    std::stable_sort(by_size.begin(), by_size.end());
    for (size_t bi = 0; bi < by_size.size(); bi++) {
        const size_t oi = by_size[bi].second;
        OutSec &o = outs[oi];
        if (!(o.flags & SHF_ALLOC) || !(o.flags & SHF_WRITE) || o.size == 0) continue;
        const bool zero = std::find(zero_outs.begin(), zero_outs.end(), (int)oi) != zero_outs.end();
        if (o.type != SHT_PROGBITS && !(zero && !zero_records)) continue;
        /*  .args is the loader's to fill: 7.4.4 keeps it PROGBITS under --rom_model (iop.map), and
         *  .init_array is loaded as it is, the startup reading it before .cinit is copied (cbs). */
        if (o.name == ".cinit" || o.name == ".args" || o.name == ".init_array") continue;

        /*  The section's bytes, laid out as they will be at run time. A hole between two
         *  contributions is zero, as it is in the image - and a zero section under 8.2.2 is
         *  all hole, its image the rle stream of that many zeros (q31: 00 01 01 04 00 01 00 00 00). */
        std::vector<u8> d(o.size, 0);
        for (size_t k = 0; k < o.parts.size() && o.type == SHT_PROGBITS; k++) {
            InSec *c = all[o.parts[k]];
            if (c->data.empty()) continue;
            u32 at = c->addr - o.addr;
            if (at + c->data.size() > d.size()) { err = o.name + ": a part lands outside it"; return false; }
            memcpy(&d[at], &c->data[0], c->data.size());
        }

        Piece pc;
        pc.out = (int)oi;
        pc.align = 1;                             /* q18's second image begins at 0x49 */
        pc.bytes.push_back(rle_index);            /* the handler index, rle24 */
        rle24_encode(d, escape_for(d), pc.bytes);
        pieces.push_back(pc);

        o.type = SHT_NOBITS;                      /* its bytes live in .cinit now */
        o.progbits = false;
    }
    if (pieces.empty() && !zero_records) { cinit_handlers.clear(); return true; }

    {
        Piece h;
        h.out = -1;                               /* the handler table: no record of its own */
        h.align = 4;
        h.bytes.resize(4 * cinit_handlers.size(), 0);
        pieces.push_back(h);
    }
    /*  A zero-fill record (7.4.4): the handler's index, three bytes of padding, and the
     *  section's size - hello's `.far` is 00 00 00 00 48 01 00 00, hello.c's, with rle24 and
     *  none in front of the handler, 02 00 00 00 48 01 00 00. */
    for (size_t z = 0; z < zero_outs.size() && zero_records; z++) {
        Piece pc;
        pc.out = zero_outs[z];
        pc.align = 4;
        u32 sz = outs[zero_outs[z]].size;
        const u8 w[8] = { zero_index, 0, 0, 0, (u8)sz, (u8)(sz >> 8), (u8)(sz >> 16), (u8)(sz >> 24) };
        pc.bytes.assign(w, w + 8);
        pieces.push_back(pc);
    }

    /*  **Every piece but the record table goes in descending size, the handler table
     *  among them**, each at its own alignment - and the record table last, at eight.
     *  7.4.4's isort is `.fardata` image 0x72, a hole of 2, the handler table 0xC, the
     *  `.neardata` image 0xA, a hole of 2, `.bss` and `.far` zero records, the table: 0xBC,
     *  where laying the images together and the handler table after them made 0xB8 and
     *  moved every address after .cinit by 4. The other 7.4.4 maps of the review, the
     *  harness (0x412, 0x25, 0x21, 0xC, 8) and 8.2.2's q05, q18 and isort (0x37, 0xB,
     *  0xB, 0xA, 8) all read the same way. A tie keeps the order the pieces were made in.
     *  The records follow the pieces' order, which is the table 7.4.4 writes. */
    std::stable_sort(pieces.begin(), pieces.end(),
                     [](const Piece &x, const Piece &y) { return x.bytes.size() > y.bytes.size(); });
    for (size_t k = 0; k < pieces.size(); k++) {
        while (image.size() % pieces[k].align) image.push_back(0);
        if (pieces[k].out < 0) cinit_table_off = (u32)image.size();
        else {
            CinitRec r;
            r.image = (u32)image.size();
            r.out = pieces[k].out;
            cinit_recs.push_back(r);
        }
        image.insert(image.end(), pieces[k].bytes.begin(), pieces[k].bytes.end());
    }
    /*  **The record table is 8-aligned by 8.2.2 and 4-aligned by 7.4.4**, and so is .cinit
     *  itself: q05 and q18 (8.2.2) pad the table to eight, 7.4.4's isort puts it at 0x9C.
     *  The bed's oracle is 8.2.2, so that is the default; --cgt=7.4.4 asks for the other. */
    const u32 cinit_align = opt.cgt744 ? 4 : 8;
    while (image.size() % cinit_align) image.push_back(0);
    cinit_recs_off = (u32)image.size();
    image.resize(image.size() + 8 * cinit_recs.size(), 0);

    /*  A contribution of the linker's own, so write_image copies it like any other. */
    InSec *c = new InSec();
    c->name = ".cinit";
    c->type = SHT_PROGBITS;
    c->flags = SHF_ALLOC;
    c->size = (u32)image.size();
    c->align = cinit_align;
    c->entsize = 0;
    c->module = lnk_mod;
    c->index = 0;
    c->live = true;
    c->dropped = false;
    c->out = -1;
    c->addr = 0;
    c->load = 0;
    c->data.swap(image);
    all.push_back(c);
    cinit_in = (int)all.size() - 1;

    /*  It has to join the output section like any other contribution, or allocate() will
     *  not place it and write_image() will not copy it. `.cinit` is in the section table
     *  build_sections works from, so it exists; it is simply empty until now. */
    int oi = out_index(".cinit");
    if (oi < 0) {
        OutSec o;
        o.name = ".cinit";
        o.run = outs.empty() ? std::string() : outs[0].run;
        outs.push_back(o);
        oi = (int)outs.size() - 1;
    }
    OutSec &o = outs[oi];
    o.parts.push_back(cinit_in);
    c->out = oi;                  /* write_image copies by this, and skips a -1 */
    o.progbits = true;
    o.type = SHT_PROGBITS;
    o.flags |= SHF_ALLOC;
    o.pflags |= PF_R;
    if (o.align < cinit_align) o.align = cinit_align;
    return true;
}

/*  **The addresses, once there are any.** The handler table's pointers, each record's
 *  {load, run}, and the four names that bracket the two - which are not the section's own
 *  bounds, so set_linker_symbols' rule for them is overridden here. */
void Link::place_cinit()
{
    if (cinit_in < 0) return;
    InSec *c = all[cinit_in];
    std::vector<u8> &d = c->data;
    const u32 base = c->addr;

    for (size_t i = 0; i < cinit_handlers.size(); i++) {
        u32 a = 0;
        std::map<std::string, std::pair<int, int> >::const_iterator it =
            defined.find(cinit_handlers[i]);
        if (it != defined.end()) sym_addr(it->second.first, it->second.second, a);
        wr32(&d[cinit_table_off + 4 * i], a);
    }
    for (size_t i = 0; i < cinit_recs.size(); i++) {
        wr32(&d[cinit_recs_off + 8 * i],     base + cinit_recs[i].image);
        wr32(&d[cinit_recs_off + 8 * i + 4], outs[cinit_recs[i].out].addr);
    }
    if (lnk_mod < 0) return;
    Module &m = mods[lnk_mod];
    const int oi = out_index(".cinit");
    for (size_t k = 0; k < m.syms.size(); k++) {
        Sym &y = m.syms[k];
        u32 v = 0;
        if (y.name == "__TI_Handler_Table_Base")       v = base + cinit_table_off;
        else if (y.name == "__TI_Handler_Table_Limit") v = base + cinit_table_off + 4 * (u32)cinit_handlers.size();
        else if (y.name == "__TI_CINIT_Base")          v = base + cinit_recs_off;
        else if (y.name == "__TI_CINIT_Limit")         v = base + cinit_recs_off + 8 * (u32)cinit_recs.size();
        else continue;
        y.value = v;
        y.lnk_out = oi;
    }
}
