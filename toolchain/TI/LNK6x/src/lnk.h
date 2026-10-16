/*  lnk.h - what the passes of this linker say to each other.
 *
 *  The shape is TI's, not the PE linker's: input sections keep their own names, the command
 *  file decides which output section each one joins and which memory range that section is
 *  cut from, and a section nothing refers to is not in the image at all. docs/elf-observed.md
 *  holds the numbers, and every rule here was read off tests/ref rather than out of a manual.
 */
#ifndef LNK_H
#define LNK_H

#include <map>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef signed int         i32;
typedef unsigned long long u64;
typedef signed long long   i64;

enum {
    SHT_NULL = 0, SHT_PROGBITS = 1, SHT_SYMTAB = 2, SHT_STRTAB = 3, SHT_RELA = 4,
    SHT_NOBITS = 8, SHT_REL = 9,
    SHT_TI_SYMALIAS = 0x7F000006,   /* .TI.symbol.alias: pairs of (alias, target) symbol */
    SHT_C6000_ATTRIBUTES = 0x70000003u,
    SHT_TI_SECTION_FLAGS = 0x7F000005u,
    SHT_TI_SYMBOL_ALIAS  = 0x7F000006u
};
enum { SHF_WRITE = 1, SHF_ALLOC = 2, SHF_EXECINSTR = 4, SHF_GROUP = 0x200 };
enum { EXIDX_CANTUNWIND = 1 };
enum { PF_X = 1, PF_W = 2, PF_R = 4 };
enum { STB_LOCAL = 0, STB_GLOBAL = 1, STB_WEAK = 2 };
enum { STT_NOTYPE = 0, STT_OBJECT = 1, STT_FUNC = 2, STT_SECTION = 3, STT_FILE = 4 };
enum { SHN_UNDEF = 0, SHN_ABS = 0xFFF1u, SHN_COMMON = 0xFFF2u };

/*  **The C6000 ABI has a small COMMON of its own**, in the processor's own range rather
 *  than SHN_COMMON: one index for an unstated alignment and four that state it. The
 *  runtime's `__dso_handle` is SHN_C6000_SCOMMON, and a linker that knows only
 *  SHN_COMMON leaves it with no storage and then refuses it as naming no section. */
enum {
    SHN_C6000_SCOMMON       = 0xFF00u,
    SHN_C6000_SCOMMON_BYTE  = 0xFF01u,
    SHN_C6000_SCOMMON_HALF  = 0xFF02u,
    SHN_C6000_SCOMMON_WORD  = 0xFF03u,
    SHN_C6000_SCOMMON_DWORD = 0xFF04u
};

/*  Every shape of COMMON: a request for storage the linker must place, not a definition. */
inline bool is_common(u16 shndx)
{
    return shndx == SHN_COMMON ||
           (shndx >= SHN_C6000_SCOMMON && shndx <= SHN_C6000_SCOMMON_DWORD);
}

/*  What it must be aligned to: the four sized indices say it, and the other two carry it
 *  in the symbol's value, which is what `st_value` means for a COMMON symbol. */
inline u32 common_align(u16 shndx, u32 value)
{
    switch (shndx) {
    case SHN_C6000_SCOMMON_BYTE:  return 1;
    case SHN_C6000_SCOMMON_HALF:  return 2;
    case SHN_C6000_SCOMMON_WORD:  return 4;
    case SHN_C6000_SCOMMON_DWORD: return 8;
    default:                      return value ? value : 1;
    }
}

/*  The C6000 relocations the bed produces. The numbering is the C6000 ELF ABI's; the three
 *  that appear in tests/ref are PCR_S21 for a branch, and ABS_L16 and ABS_H16 for the MVKL and
 *  MVKH that carry a 32-bit address in two halves. */
enum {
    R_C6000_NONE = 0, R_C6000_ABS32 = 1, R_C6000_ABS16 = 2, R_C6000_ABS8 = 3,
    R_C6000_PCR_S21 = 4, R_C6000_PCR_S12 = 5, R_C6000_PCR_S10 = 6, R_C6000_PCR_S7 = 7,
    R_C6000_ABS_S16 = 8, R_C6000_ABS_L16 = 9, R_C6000_ABS_H16 = 10,
    /*  Met in the runtime and in every C++ program, and not applied here: what each one
     *  computes has not been read off the oracle yet, and a relocation applied by guesswork
     *  is a wrong image written in silence. docs/known.md has the measurements so far. */
    R_C6000_PREL31 = 25, R_C6000_EHTYPE = 28, R_C6000_PCR_H16 = 29, R_C6000_PCR_L16 = 30
};

struct Rel { u32 offset; u32 sym; u32 type; i32 addend; };

struct Sym {
    std::string name;
    u32 value, size;
    u8  info, other;
    u16 shndx;
    int out;            /* index in the output symbol table, -1 while it has none */
    /*  A name the linker defines itself is absolute while it is being resolved - its value is
     *  the address - but lnk6x writes it against an output section: q07 has __TI_STACK_END in
     *  .stack and __TI_CINIT_Base in .cinit. This says which, or -1 for an ordinary symbol. */
    int lnk_out;
    /*  .TI.symbol.alias: this name stands for that symbol of the same object - the runtime's
     *  `remove` is `unlink`, and lnk6x lays no .text:remove - or -1 for an ordinary symbol. */
    int alias;
    Sym() : value(0), size(0), info(0), other(0), shndx(0), out(-1), lnk_out(-1), alias(-1) {}
};

/* one input section, from one object */
struct InSec {
    std::string name;
    u32 type, flags, size, align, entsize;
    u32 link = 0;                /* sh_link: for .c6xabi.exidx, the code section it describes */
    std::vector<u8> data;        /* empty when SHT_NOBITS */
    std::vector<Rel> relocs;
    int  module;
    int  index;                  /* its section number inside that object */
    bool live;                   /* survived unused-section elimination */
    /*  **Initialised, because nothing else initialises it.** The two sections
     *  add_linker_symbols builds set every field it could think of and not this one, so
     *  take_module read uninitialised memory and skipped the linker's own COMMON symbols
     *  when it happened to be non-zero - which failed one program and not the next. */
    bool dropped = false;        /* a second copy of a group section: another object had it */
    bool exidx_synth = false;    /* the unwind index the linker composed: its relocations name no symbol */
    /*  For the unwind index: the (module, symbol) each relocation is against, or (-1, 0) for an
     *  address that is final when composed. Resolved when written, after every section is placed. */
    std::vector<std::pair<int, u32> > exidx_src;
    int  tramp = -1;             /* a far-call trampoline the linker wrote: its index in Link::tramps */
    int  out;                    /* output section, -1 */
    u32  addr;                   /* run address, once allocated */
    u32  load;                   /* load address: the same unless the command file parts them */
};

struct Module {
    std::string name;            /* the file name, as the map and the symbol table show it */
    std::vector<InSec> secs;     /* secs[i] is section number i */
    std::vector<Sym>   syms;
    int file_sym;                /* the STT_FILE symbol, or -1 */
};

/* ------------------------------------------------------ the command file */

struct Range {
    std::string name;
    u32 origin, length, used;
};

struct SecSpec {
    std::string name;
    std::string load, run;       /* memory range names; run empty means the same as load */
    u32  align;  bool has_align;
    u32  fill;   bool has_fill;
    /*  The input-section list of a `.name : { *(.text:early) *(.text) } > RAM` entry, in the
     *  order it was written: that order is the order the parts are laid down in. A pattern
     *  may end in `*`. Empty when the entry has no list. */
    std::vector<std::string> inputs;
};

/*  `*(.text:early)` against `.text:early`, and `*(.text:*)` against any subsection of .text.
 *  A pattern is a section name, optionally ending in `*`. */
bool sec_matches(const std::string &pattern, const std::string &name);

/*  A subsection joins its base section: `.text:_outc` is part of `.text` unless the command
 *  file names `.text:_outc` itself. */
std::string base_section(const std::string &name);

struct Cmd {
    std::vector<Range>   mem;
    std::vector<SecSpec> secs;
    u32 stack_size, heap_size;
    u32 args_size;               /* --args=N: a .args of N bytes, __c_args__ at its start; 0 = none */
    /*  Options written inside the command file. RIDE's kTiLinkCmd puts --rom_model on its
     *  second line, and a linker that reads options only from its command line links RIDE's
     *  programs as --ram_model without saying so (the review's N4). 0 = the file said
     *  nothing, 1 = --ram_model, 2 = --rom_model. */
    int model;
    std::string entry;
    /*  lnk6x's own defaults, 1 kB each, where neither the line nor the file names a size. */
    Cmd() : stack_size(0x400), heap_size(0x400), args_size(0), model(0) {}
    bool parse(const std::string &path, std::string &err);
    Range *range(const std::string &name);
};

/* ----------------------------------------------------- the output image */

struct OutSec {
    std::string name;
    u32 type, flags, addr, size, align, entsize, offset;
    u32 reserve;                 /* bytes the linker itself asks for: .stack, .sysmem */
    u32 pflags;                  /* the segment attributes, taken from the input sections */
    std::vector<int> parts;      /* indices into Link::all, in allocation order */
    bool progbits;               /* a fill makes an empty section initialised */
    bool unnamed = false;        /* the command file does not name it: laid after all it does */
    bool debug = false;          /* .debug_line: not allocated, no address, carried into the file after the loaded sections */
    int  rank = -1;              /* its place in the last allocation order */
    std::string load, run;
    OutSec() : type(SHT_NOBITS), flags(0), addr(0), size(0), align(1), entsize(0), offset(0),
               reserve(0), pflags(0), progbits(false) {}
};

struct Seg { u32 offset, vaddr, paddr, filesz, memsz, flags, align; };

struct Options {
    std::string out, map, entry, cmdfile;
    std::vector<std::string> inputs;
    std::vector<std::string> libdirs;
    bool ram_model, rom_model, verbose;
    /*  --cgt=7.4.4: lay out what the two versions of lnk6x lay out differently as CCS 5.5's
     *  7.4.4 does, where the default is CCS 7.4's 8.2.2, the bed's oracle. Today that is only
     *  .cinit's alignment and its record table's: 4 against 8 (docs/known.md). */
    bool cgt744 = false;
    bool model_given, entry_given;       /* whether the command line said so itself */
    long stack_size, heap_size, args_size; /* -1 unless the command line gave one */
    Options() : entry("_c_int00"), ram_model(true), rom_model(false), verbose(false),
                model_given(false), entry_given(false), stack_size(-1), heap_size(-1), args_size(-1) {}
};

/* archive.cpp */
struct Archive {
    std::string name;
    std::vector<u8> bytes;
    std::vector<std::pair<std::string, u32> > index;   /* symbol -> the member's offset */
    /*  The index hashed, each name to its *first* entry - the member the linear walk it
     *  replaces would have stopped at (the review's C2). */
    std::unordered_map<std::string, u32> first;
    std::unordered_set<u32> taken;                     /* members already pulled */
    const u32 *find(const std::string &sym) const {
        std::unordered_map<std::string, u32>::const_iterator i = first.find(sym);
        return i == first.end() ? 0 : &i->second;
    }
    size_t longnames_at;                               /* the `//` member's data, 0 when none */
    Archive() : longnames_at(0) {}
    bool load(const std::string &path, std::string &err);
    bool member(u32 off, Module &m, std::string &err) const;
};

struct Link {
    Options opt;
    Cmd cmd;
    std::vector<Module>  mods;
    std::vector<InSec*>  all;
    std::vector<OutSec>  outs;
    std::vector<Seg>     segs;
    std::map<std::string, std::pair<int, int> > defined;   /* name -> module, symbol */
    std::map<std::string, bool> groups;   /* group section names already taken, by the first */
    size_t cmdMods = 0;                   /* the command line's objects: mods[0..cmdMods) - the rest came out of archives */
    u32 entry_addr, static_base;
    int lnk_mod;                 /* the module holding the names the linker defines, or -1 */
    std::string err;

    Link() : entry_addr(0), static_base(0), lnk_mod(-1),
             cinit_table_off(0), cinit_recs_off(0), cinit_in(-1) {}

    bool run();
    bool read_inputs();
    bool take_module(int mi);
    bool in_image(int mi, int sym) const;
    void resolve_alias(int &mod, int &sym) const;
    void add_linker_symbols();
    void set_linker_symbols();
    void write_map();
    std::vector<Archive> libs;                   /* the archives read_inputs opened */
    bool pull_symbol(const std::string &name);
    bool eliminate();
    bool follow(std::pair<int, int> at);
    std::vector<std::pair<int, int> > pending;   /* elimination's work list: module, section */
    
    bool build_sections();
    /*  --rom_model: the load images and the table that drives them. compose_cinit runs
     *  before allocation, because .cinit's size decides where everything after it goes;
     *  place_cinit runs after, when the addresses it must write down exist. */
    bool compose_cinit();
    void place_cinit();
    /*  Where each piece of .cinit sits, as offsets into it, and what the records say. */
    struct CinitRec { u32 image;      /* offset of the load image */
                      int out; };     /* the output section it initialises */
    std::vector<CinitRec> cinit_recs;
    std::vector<std::string> cinit_handlers;
    u32 cinit_table_off, cinit_recs_off;
    bool zero_root = false;      /* __TI_zero_init was made a root: a .bss or .far needs zeroing */
    int cinit_in;                     /* index into `all` of the synthetic contribution */
    bool allocate();
    /*  The unwind index, composed from the objects' entries once the code is placed: sorted
     *  by function address, an EXIDX_CANTUNWIND for every run of code with no entry, and
     *  entries carrying one word folded into the one before. */
    bool compose_exidx(OutSec &o);
    std::vector<int> exidx_input;                 /* the objects' entries, as build_sections found them */
    int exidx_in = -1;                            /* index into `all` of the composed table */
    bool fix_up(bool check_range = true);   /* false: a provisional pass, fields masked unchecked */
    /*  **Far-call trampolines**, lnk6x's `$Tramp$S$$callee` (docs/known.md): a PCR_S21 branch
     *  that cannot reach its target is sent to 32 bytes of code laid right after the calling
     *  input section, which load the target into B10 and branch there. `layout` is allocate()
     *  run to a fixed point with them: a pass that adds one moves everything after it, so the
     *  calls are asked again until no pass adds any. One is made per callee and reused by
     *  every later call that can reach it. */
    struct Tramp {
        u32 target;                      /* the callee's address, as last allocated */
        std::string name;                /* $Tramp$S$$faraway */
        int in;                          /* index into `all` of its section */
        int caller;                      /* index into `all` of the input section it follows */
        int tmod, tsec; u32 toff;        /* the callee's input section and offset: the map's name */
        std::vector<std::pair<int, u32> > calls;   /* (index into `all`, offset) sent through it */
    };
    std::vector<Tramp> tramps;
    bool layout();
    bool trampolines(bool &added);
    int  tramp_for(int tm, int tsec, u32 toff, u32 P) const;   /* one in reach of P, or -1 */
    void retarget_tramps();                      /* each one's target from its callee's place */
    /*  Whether a branch from `c` at P to V must go through a trampoline: out of reach, or its
     *  callee's output section laid after the caller's (q20), so not placed when lnk6x asked. */
    bool wants_tramp(const InSec *c, u32 P, u32 V, int tm, int ts) const;
    void callee_of(int mod, u32 sym, int &tm, int &ts) const;
    bool write_image();
    bool sym_addr(int mod, int sym, u32 &a);
    int  out_index(const std::string &name) const;
    /*  out_index's table, rebuilt when `outs` has grown: a section's name never changes once
     *  it is pushed, so the count alone says whether the table is current. */
    mutable std::unordered_map<std::string, int> out_by_name;
    mutable size_t out_by_name_n = 0;
};

/* elf.cpp */
bool elf_read(const u8 *p, size_t n, const std::string &name, Module &m, std::string &err);



/* where a `-l name` is looked for: as given, then each `-i` directory in order */
std::string find_library(const Options &o, const std::string &nm);

/* reloc.cpp */
/*  `range` is set when the refusal is a value that does not fit its field, so that the caller
 *  can name the symbol and the place; otherwise the message says everything. */
bool apply_reloc(u32 type, u8 *p, u32 P, u32 S, i32 A, u32 B, std::string &err, bool &range);
void apply_reloc_unchecked(u32 type, u8 *p, u32 P, u32 S, i32 A, u32 B);   /* a provisional pass */
const char *reloc_name(u32 type);       /* "R_C6000_PCR_S21", or 0 for a number not known here */
u32 reloc_width(u32 type);              /* the bytes the place occupies: 4, 2 for ABS16, 1 for ABS8 */
/*  A PCR_S21 branch at P reaches V: a whole number of words from P's fetch packet, and no
 *  more than 2^20 of them either way. Misaligned is "reaches" here - it is not a question a
 *  trampoline answers, and apply_reloc refuses it by name. */
bool pcr_s21_reaches(u32 P, u32 V);
/*  The 32 bytes of a trampoline at P to V: `B V` when it reaches, lnk6x's B10 form when not. */
void tramp_code(u8 *p, u32 V, u32 P);

inline u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
inline u32 rd32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
inline void wr16(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
inline void wr32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
inline u32 align_up(u32 v, u32 a) { return (a > 1) ? ((v + a - 1) / a) * a : v; }

#endif
