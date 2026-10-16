#ifndef ASM_H
#define ASM_H

/* asm6x: an assembler for the TMS320C6000 - the C674x of the C6747 - taking the assembly the
   c90, cpp11 and shalimar compilers write for their tms6747 target and writing the ELF object
   TI's own asm6x writes, which lnk6x links against TI's runtime. asm6x is the oracle: every
   encoding here was read back from it, one form per line, and the tests hold it to that.

   C-style ISO C++14, one thread per input file, no mutable globals. */

#include <map>
#include <unordered_map>
#include <string>
#include <vector>

enum TokKind { T_NAME, T_NUM, T_STR, T_PUNCT };

struct Token {
    TokKind kind;
    std::string text;
    long long value;
};

/* the C6000 EABI relocation types, numbered as the ABI numbers them */
enum RelKind {
    R_NONE = 0, R_ABS32 = 1, R_ABS16 = 2, R_ABS8 = 3, R_PCR_S21 = 4, R_PCR_S12 = 5, R_PCR_S10 = 6,
    R_PCR_S7 = 7, R_ABS_S16 = 8, R_ABS_L16 = 9, R_ABS_H16 = 10, R_PREL31 = 25, R_EHTYPE = 28,
    R_NOCMP = 255       /* asm6x's marker on the last data word a code section ends with */
};

struct Reloc {
    unsigned long offset;
    int symbol;         /* a symbol, or -1 with section below: against that section's own symbol */
    int section;
    RelKind kind;
    long long addend;   /* written as a RELA entry for the kinds asm6x writes that way, else in place */
    unsigned long order;    /* its place in the source, by which asm6x orders the relocation sections */
};

struct Section {
    std::string name;
    bool code;          /* executable: asm6x's rule is the content, not the name - an instruction
                           makes any section code, and .text starts so */
    bool hasCode;       /* an instruction landed: 32-aligned and padded to 32 */
    bool bss;           /* .bss / .usect: size only */
    bool writable;      /* .data, .bss, .usect, and a .sect with RW */
    bool alloc;         /* loaded: every section the compilers write is */
    int kind;           /* SEC_PROGBITS, SEC_NOBITS, SEC_UNWIND */
    int align;
    bool dataLast;      /* the last thing emitted was data, for the NOCMP marker */
    unsigned long dataEnd;
    std::vector<unsigned char> bytes;
    std::vector<Reloc> relocs;
    std::vector<unsigned long> insns;   /* where each instruction word went, in order: the compressor's input */
    unsigned long codeEnd;              /* the end of the last instruction: where a line-program sequence ends, before the padding */
    unsigned long dataBytes;            /* bytes of data emitted here: a code section holding any is not compressed */
    bool compressed;                    /* laid out in header-based fetch packets (--compress) */
};

enum { SEC_PROGBITS = 1, SEC_NOBITS = 8, SEC_UNWIND = 0x70000001 };

enum SymBind { B_LOCAL, B_GLOBAL, B_WEAK, B_EXTERN };

enum { SYM_NONE = 0, SYM_OBJECT = 1, SYM_FUNC = 2, SYM_SECTION = 3 };

struct Symbol {
    std::string name;
    SymBind bind;
    bool defined;
    int type;           /* SYM_FUNC for a label in code, SYM_OBJECT for one in data, as asm6x types them */
    int section;
    long long value;
    int line;
    int pass;           /* the pass that defined it */
    int prev_section;   /* where the previous pass put it: the layout has settled when nothing moved */
    long long prev_value;
    bool referenced;    /* named in an expression, a .global or a .ref */
    long long size;     /* a .bss or .usect symbol's, as asm6x records it; 0 otherwise */
    bool sized;         /* the size is written even for a local: a .usect symbol's is, a .bss one's not */
    bool mustDefine;    /* .def: the file must define it */
    int alias;          /* al .set fwd+4: the label, with aliasAdd; a reference to al is one to fwd, and the
                           symbol table has al as an absolute of the offset, as asm6x writes it */
    long long aliasAdd;
    unsigned long order;    /* its place among the definitions: asm6x writes the locals in that order */
};

struct Fixup {
    int section;
    unsigned long at;
    int symbol;         /* or -1: against the section symbol of secref ($) */
    int secref;
    int sub;            /* a label subtracted: the difference is a constant once both are placed */
    RelKind kind;
    long long addend;
    int width;          /* of the field: 1, 2 or 4 bytes */
    int line;
    bool half;          /* a BNOP in a header-based fetch packet: its displacement is in halfwords */
    bool settled;       /* the compressor wrote it in place (a compact branch): nothing left to do */
};

/* the value of an expression: a constant, or a symbol plus a constant, or a difference of two
   labels; the relocation operators - $EXIDX_FUNC(f), $EXIDX_EXTAB("t") and their kin - name
   the relocation the writer must emit for it */
struct Value {
    long long v;
    int sym;
    int sub;
    int secref;         /* $: the current section, at v */
    RelKind op;         /* R_NONE for a plain expression, else the operator's relocation */
    bool operate;       /* an operator was written */
    bool forward;       /* a .set constant defined later in the file was folded in; the directives that
                           size a section refuse that, as asm6x refuses it */
};

/* a .dwpsn: the source position the next instruction of a section carries (-g), as cl6x writes it;
   `address` names a function, which starts a new line-program sequence */
struct DwRow {
    int section;
    size_t insn;        /* the index of the next instruction in the section when the directive was read */
    std::string file;
    int line, column;
    bool stmt;
    bool newSeq;
};

class Unit {
public:
    Unit();

    int line;
    int pass;
    int current;
    std::string source;                     /* the input's base name, the file symbol */
    std::vector<unsigned long> prev_sizes;
    std::vector<Section> sections;
    std::vector<Symbol> symbols;
    std::vector<Fixup> fixups;
    std::vector<DwRow> dwRows;                  /* the .dwpsn rows of this pass, in source order */
    std::string dwTags;                         /* the .dwtag / .dwendtag lines of this pass, a line each, as written */
    std::vector<std::string> errors;
    std::vector<std::string> warnings;          /* asm6x mends these with a word; so does this */
    std::vector<std::pair<int, std::string> > depends;  /* .symdepend: (symbol, section name) pairs, R_NONE entries */
    std::vector<std::string> pendingLabels;     /* labels alone on their lines, placed by the next emission - the
                                                   last first, as asm6x defines them */
    unsigned long defined;                      /* definitions so far this pass, for Symbol::order */
    bool compressing;                           /* the compressor's pass: a difference of two code labels
                                                   is left to resolve(), as the labels will move */
    std::unordered_map<std::string, int> symIndex;   /* name -> symbols[] index: find() in one step */

    void begin_pass(int n);
    bool moved() const;
    void error(const std::string &msg);
    void warn(const std::string &msg);
    int section(const std::string &name, bool writable);
    Section *cur();
    unsigned long here();

    int find(const std::string &name) const;
    int ref(const std::string &name);
    bool define(const std::string &name, int type);
    bool constant(const std::string &name, long long v, int alias = -1, long long aliasAdd = 0);

    void align(int bytes);
    void placeLabel();
    void emit8(unsigned v);
    void emit16(unsigned v);
    void emit32(unsigned long v);
    void emitData(int width, unsigned long long v);     /* bytes, and the section remembers it was data */
    void emitWord(unsigned long v);                     /* an instruction word */
    void fixup(unsigned long at, int sym, RelKind kind, long long addend = 0, int sub = -1, int secref = -1, int width = 4);

    void resolve();
};

bool split_line(const std::string &src, std::vector<Token> &out, std::string &err);
std::string upper(const std::string &s);
bool is_punct(const std::vector<Token> &t, size_t i, char c);
bool eval(Unit &u, const std::vector<Token> &t, size_t from, size_t to, Value &v, std::string &err);
bool eval_const(Unit &u, const std::vector<Token> &t, size_t from, size_t to, long long &v, std::string &err, bool settled = false);

/* c6x.cpp's: an operand, a unit named in the source, what an instruction encodes to, and an
   instruction of the open execute packet, kept so that one can be moved to another unit */
enum OpKind { O_REG, O_PAIR, O_CST, O_MEM, O_EXPR };

struct Operand {
    OpKind kind;
    int reg;            /* O_REG, O_PAIR (the even, low register), O_MEM the base */
    int side;           /* 0 for A, 1 for B */
    long long v;        /* O_CST; O_MEM the offset as written; O_EXPR the constant part */
    bool minus;         /* O_MEM: *-R... */
    bool bracket;       /* O_MEM: the offset was [n], in units of the access */
    int mode;           /* O_MEM: 0 plain, 1 pre-increment, 2 pre-decrement, 3 post-increment, 4 post-decrement */
    int offReg;         /* O_MEM: a register offset, or -1 */
    Value val;          /* O_EXPR */
};

/* a named unit: .L1 .S2X .D1T2 .M1 - the letter, its side, the cross path, the data side */
struct UnitName {
    char letter;        /* 0 when none was named */
    int side;
    bool cross;
    int dataSide;       /* .D1T2: 1; -1 when not named */
    bool loose;         /* only the letter is asked for: a packet looking for another unit */
};

/* what an instruction encodes to: the word, the unit it takes, and a relocation if it needs one */
struct Encoded {
    unsigned long w;
    char unit;          /* 0 for NOP, SWE, IDLE */
    int side;
    int fixSym;
    RelKind fixKind;
    long long fixAdd;
};

struct Packet {
    std::string mnemonic;
    std::vector<Operand> operands;
    unsigned creg, z;
    unsigned long at;
    Encoded e;
    bool named;         /* the source named its unit: not moved */
};


/* the target: one statement at a time, over the source until the layout settles */
class C6xTarget {
public:
    C6xTarget();
    void begin_pass(int pass);
    void statement(Unit &u, std::vector<Token> &t);
    bool finished() const { return done; }
    void end_of_file(Unit &u);

private:
    bool done;
    int pass;
    unsigned long last_at;
    int last_section;   /* where the last instruction went: || joins it only there */
    unsigned units;     /* the units the open packet has taken, a bit per unit and side */
    std::map<std::string, std::vector<Token> > aliases;     /* .asg: a name for a piece of text */
    std::vector<Packet> packet;     /* the instructions of the open packet, for moving one to another unit */

    bool directive(Unit &u, const std::vector<Token> &t, size_t i);
    void instruction(Unit &u, const std::vector<Token> &t, size_t i);
    bool retarget(Unit &u, Packet &p, unsigned taken, bool keepUnit);
};

bool write_elf(const Unit &u, const std::string &path, std::string &err);

/* compact.cpp: TI's compaction - 16-bit instructions in header-based fetch packets - over the
   code sections of a pass that has not been resolved yet; moves the labels and the fixups */
void compress_sections(Unit &u);
/* the 16-bit form of a 32-bit word, if it has one under the header's expansion bits; for the tests */
bool compact_form(unsigned long w, unsigned dsz, unsigned br, unsigned &half, bool &branch);

class Assembler {
public:
    Assembler(const std::string &input, const std::string &output, bool compress = false);
    bool run();
    const std::vector<std::string> &errors() const { return unit.errors; }
    const std::vector<std::string> &warnings() const { return unit.warnings; }
    void fail(const std::string &why) { unit.errors.push_back(why); }

private:
    std::string input;
    std::string output;
    bool compress;
    Unit unit;

    Assembler(const Assembler &);
    Assembler &operator=(const Assembler &);
};

#endif

/* dwarf.cpp: the .debug_line section built from the .dwpsn rows, once the layout is final */
void build_debug_line(Unit &u);
