#include "asm.h"

#include <algorithm>

/* Compaction: the C64x+ 16-bit instructions in header-based fetch packets, as TI's asm6x
   writes them by default (--no_compress turns it off there; --compress turns it on here).

   A header-based fetch packet is 32 bytes whose last word is a header, 0xE in its top nibble:
   bits 27-21 say which of the other seven words hold two 16-bit instructions (the low half
   first), 20 PROT (every load in the packet is followed by 4 NOP cycles the hardware adds),
   19 RS (the high register set - not used here), 18-16 DSZ (what the compact loads and stores
   move), 15 BR (compact .S instructions are branches), 14 SAT, and 13-0 the p-bits of the
   fourteen halves. A 32-bit word in such a packet keeps its own p-bit.

   Every rule here was read from asm6x 8.2.2's compressed objects set beside its
   --no_compress ones, instruction for instruction: 347 corpus files, the Compiler++ harness
   at -O1 and -O2 and bench-c6x - 300,000 compact instructions, every one of them the 16-bit
   form this file makes for the same 32-bit word (the RS=1 packets aside). The layout follows
   asm6x's too, where it could be read: one fetch packet at a time from the first
   instruction, the header's context chosen to take the most instructions, instructions
   paired greedily, a label a 32-bit branch reaches kept on a word, and the whole redone
   until nothing moves. Where it could not be read, the rule here is the safe one, and a
   layout that breaks any rule of the machine is thrown away: the section stays 32-bit. */

namespace {

struct Form16 {
    unsigned h;
    unsigned char dsz;      /* the DSZ values it means what it should under, a bit each */
    signed char br;         /* the BR it needs: 0, 1, or -1 for either */
    bool branch;            /* a compact branch: the displacement is filled in by the layout */
};

unsigned char dsz_mask(const char *pat)
{
    unsigned char m = 0;
    for (unsigned v = 0; v < 8; v++) {
        bool ok = true;
        for (int i = 0; i < 3; i++)
            if (pat[i] != 'x' && (unsigned)(pat[i] - '0') != ((v >> (2 - i)) & 1)) ok = false;
        if (ok) m = (unsigned char)(m | (1u << v));
    }
    return m;
}

void add(std::vector<Form16> &out, unsigned h, unsigned char dsz = 0xFF, int br = -1, bool branch = false)
{
    Form16 f;
    f.h = h & 0xFFFF; f.dsz = dsz; f.br = (signed char)br; f.branch = branch;
    out.push_back(f);
}

void mv_forms(std::vector<Form16> &out, unsigned s, unsigned x, unsigned src2, unsigned dst)
{
    if (src2 < 8)
        add(out, 0x0046 | s | (2u << 3) | (src2 << 7) | (((dst >> 3) & 3) << 10) | (x << 12) | ((dst & 7) << 13));
    else if (dst < 8)
        add(out, 0x0006 | s | (2u << 3) | ((src2 & 7) << 7) | (((src2 >> 3) & 3) << 10) | (x << 12) | (dst << 13));
}

/* the loads and stores with a 5-bit constant offset: kMemory's forms in c6x.cpp, bit 7 (the
   unit's side) left out */
struct MemRow { unsigned form; const char *name; bool load; int width; };
const MemRow kMem[] = {
    { 0x009, "LDB", true, 1 }, { 0x005, "LDBU", true, 1 }, { 0x011, "LDH", true, 2 }, { 0x001, "LDHU", true, 2 },
    { 0x019, "LDW", true, 4 }, { 0x059, "LDDW", true, 8 },
    { 0x00D, "STB", false, 1 }, { 0x015, "STH", false, 2 }, { 0x01D, "STW", false, 4 }, { 0x051, "STDW", false, 8 },
};

/* Doff4's size bit and the DSZ values under which it means this access */
struct Doff4Row { const char *name; unsigned sz; const char *dsz; };
const Doff4Row kDoff4[] = {
    { "LDW", 0, "0xx" }, { "LDW", 1, "100" }, { "STW", 0, "0xx" }, { "STW", 1, "100" },
    { "LDB", 1, "x01" }, { "STB", 1, "000" }, { "STB", 1, "x01" }, { "LDBU", 1, "000" },
    { "LDH", 1, "x11" }, { "LDHU", 1, "010" }, { "STH", 1, "01x" }, { "STH", 1, "111" },
    { "LDDW", 0, "1xx" }, { "STDW", 0, "1xx" },
};

}

/* the 16-bit forms of a 32-bit word, as asm6x writes them; the unit, the side and the cross
   path never change, so a packet takes the same units either way */
static void forms16(unsigned long w, std::vector<Form16> &out)
{
    out.clear();
    unsigned creg = (unsigned)(w >> 29) & 7, z = (unsigned)(w >> 28) & 1, dst = (unsigned)(w >> 23) & 31,
             src2 = (unsigned)(w >> 18) & 31, src1 = (unsigned)(w >> 13) & 31, x = (unsigned)(w >> 12) & 1,
             s = (unsigned)(w >> 1) & 1;
    if ((w & ~0x1E001ul) == 0) {                        /* NOP n */
        unsigned n = (unsigned)((w >> 13) & 15) + 1;
        if (n <= 8) add(out, 0x0C6E | ((n - 1) << 13));
        return;
    }
    if (creg || z) return;                              /* predicated: stays 32-bit */
    if (((w >> 2) & 3) == 1) {                          /* a load or store */
        unsigned form = (unsigned)(w >> 2) & 0x5F, mode = (unsigned)(w >> 9) & 15, y = (unsigned)(w >> 7) & 1;
        const MemRow *m = 0;
        for (size_t k = 0; k < sizeof kMem / sizeof *kMem; k++) if (kMem[k].form == form) m = &kMem[k];
        if (!m || mode != 1) return;                    /* *+R[ucst5] only */
        unsigned data = dst, base = src2, off = src1, op = m->load ? 1 : 0;
        std::string name = m->name;
        if (base == 15 && y == 1 && (name == "LDW" || name == "STW") && data < 8)     /* Dstk: *+B15[ucst5] */
            add(out, 0x8C05 | (op << 3) | (data << 4) | (s << 12) | (((off >> 2) & 7) << 7) | ((off & 3) << 13));
        if (base >= 4 && base <= 7 && off < 16) {       /* Doff4: *+A4..A7/B4..B7[ucst4] */
            unsigned sd;
            if (m->width == 8) { if ((data & 1) || data > 7) return; sd = (data >> 1) << 5; }
            else { if (data > 7) return; sd = data << 4; }
            for (size_t k = 0; k < sizeof kDoff4 / sizeof *kDoff4; k++) {
                if (name != kDoff4[k].name) continue;
                unsigned h = 0x0004 | y | (op << 3) | ((base - 4) << 7) | (kDoff4[k].sz << 9) | (s << 12) |
                             ((off & 7) << 13) | (((off >> 3) & 1) << 11) | sd;
                add(out, h, dsz_mask(kDoff4[k].dsz));
            }
        }
        return;
    }
    unsigned form = (unsigned)(w >> 2) & 0x3FF;
    if (form == 0x250 || form == 0x270) {               /* ADD/SUB .D src2, ucst5, dst: no cross path */
        if (x) return;
        unsigned c = src1;
        bool sub = form == 0x270;
        if (s == 1 && src2 == 15 && c % 4 == 0) {
            unsigned k = c / 4;
            if (dst == 15) add(out, 0x0C76 | 1 | ((sub ? 1u : 0u) << 7) | (((k >> 3) & 3) << 8) | ((k & 7) << 13));   /* Dx5p */
            else if (dst < 8 && !sub) add(out, 0x0436 | 1 | (dst << 7) | (((k >> 3) & 3) << 11) | ((k & 7) << 13));   /* Dx5 */
        }
        if (c == 1 && dst == src2 && dst < 8) add(out, 0x1876 | s | (dst << 7) | ((sub ? 3u : 5u) << 13));      /* Dx1 */
        if (c == 0 && !sub) mv_forms(out, s, x, src2, dst);
        return;
    }
    if (form == 0x23C && src1 == 0) { mv_forms(out, s, x, src2, dst); return; }        /* OR .D 0, x, y: MV */
    if (form == 0x2FC && src1 == 1 && dst == src2 && dst < 8 && !x) { add(out, 0x1876 | s | (dst << 7) | (7u << 13)); return; }   /* XOR 1 */
    if ((form == 0x210 || form == 0x230) && !x && dst == src2 && dst < 8 && src1 < 8) {   /* ADD/SUB .D rrr: Dx2op */
        add(out, 0x0036 | s | (src1 << 7) | ((form == 0x230 ? 1u : 0u) << 11) | (dst << 13));
        return;
    }
    if (form == 0x210 && !x && dst == src1 && dst < 8 && src2 < 8) { add(out, 0x0036 | s | (src2 << 7) | (dst << 13)); return; }
    if (form == 0xD6 && dst < 8 && !x) {                      /* MVK .L scst5: Lx5 */
        unsigned c = src2;
        add(out, 0x0426 | s | (dst << 7) | (((c >> 3) & 3) << 11) | ((c & 7) << 13));
        return;
    }
    if ((w & 0x7C) == 0x28 && dst < 8) {                /* MVK/MVKL .S, the constant 0..255: Smvk8 */
        unsigned c = (unsigned)(w >> 7) & 0xFFFF;
        if (c < 256) add(out, 0x0012 | s | (dst << 7) | (((c >> 7) & 1) << 10) | (((c >> 5) & 3) << 5) | (((c >> 3) & 3) << 11) | ((c & 7) << 13));
        return;
    }
    if ((w & 0x0F83EFFCul) == 0x360 || (w & 0x0F830FFCul) == 0x800360) {   /* B reg, BNOP reg,n: Sx1b */
        unsigned n = (w & 0x800000) ? (unsigned)(w >> 13) & 7 : 0;
        if (s == 1 && !x && src2 < 16) add(out, 0x006E | 1 | (src2 << 7) | (n << 13));
        return;
    }
    if ((w & 0x7C) == 0x10 && !(w & 0x60000000)) {     /* B label (not CALLP): Sbs7 BNOP label,0 */
        add(out, 0x000A | s, 0xFF, 1, true);
        return;
    }
    if ((w & 0x1FFC) == 0x120) {                        /* BNOP label,n: Sbs7 */
        unsigned n = (unsigned)(w >> 13) & 7;
        add(out, 0x000A | s | (n << 13), 0xFF, 1, true);
        return;
    }
    if (form == 0x168 && src1 == 0 && dst == src2 && dst < 8 && !x) { add(out, 0x186E | s | (dst << 7) | (2u << 13)); return; }   /* NEG: Sx1 */
    if ((w & 0x3C) == 0x08) {                           /* EXT/EXTU a,16,16 / a,24,24: S2ext */
        unsigned op = (unsigned)(w >> 6) & 3, csta = src1, cstb = (unsigned)(w >> 8) & 31;
        if (op < 2 && csta == cstb && (csta == 16 || csta == 24) && dst < 8 && src2 < 8) {
            unsigned o = op == 1 ? (csta == 16 ? 0 : 1) : (csta == 16 ? 2 : 3);
            add(out, 0x0062 | s | (src2 << 7) | (o << 11) | (dst << 13));
        }
        return;
    }
    if ((form == 0x328 || form == 0x368 || form == 0x268) && dst == src2 && dst < 8 && !x) {   /* SHL/SHR/SHRU ucst5: Ssh5 */
        unsigned op = form == 0x328 ? 0 : form == 0x368 ? 1 : 2, c = src1;
        add(out, 0x0402 | s | (op << 5) | (dst << 7) | (((c >> 3) & 3) << 11) | ((c & 7) << 13));
        return;
    }
    if ((form == 0x328 || form == 0x368) && dst < 8 && src2 < 8) {   /* SHL/SHR by 1..6, 8, 16 elsewhere: S3i */
        int c = src1 >= 1 && src1 <= 6 ? (int)src1 : src1 == 8 ? 7 : src1 == 16 ? 0 : -1;
        if (c >= 0) add(out, 0x040A | s | (dst << 4) | (src2 << 7) | ((form == 0x368 ? 1u : 0u) << 11) | (x << 12) | ((unsigned)c << 13), 0xFF, 0);
        return;
    }
    if ((form == 0x338 || form == 0x378 || form == 0x278) && !x && dst == src2 && dst < 8 && src1 < 8) {   /* by a register: S2sh */
        unsigned op = form == 0x338 ? 0 : form == 0x378 ? 1 : 2;
        add(out, 0x0462 | s | (dst << 7) | (op << 11) | (src1 << 13));
        return;
    }
    if (form == 0x296 && src1 < 8 && dst < 2 && src2 < 8 && !x) {   /* CMPEQ .L ucst3, x, A0/A1: Lx3c */
        add(out, 0x0026 | s | (src2 << 7) | (dst << 11) | (src1 << 13));
        return;
    }
    if ((form == 0x1E || form == 0x3E) && dst < 8 && src1 < 8 && src2 < 8) {   /* ADD/SUB .L src1, x src2: L3 */
        add(out, s | (dst << 4) | (src2 << 7) | ((form == 0x3E ? 1u : 0u) << 11) | (x << 12) | (src1 << 13));
        return;
    }
    static const unsigned l2c[][2] = { { 0x29E, 3 }, { 0x2BE, 4 }, { 0x23E, 5 }, { 0x2FE, 6 }, { 0x27E, 7 } };
    for (size_t k = 0; k < sizeof l2c / sizeof *l2c; k++)      /* the compares into A0/A1: L2c */
        if (form == l2c[k][0] && dst < 2 && src1 < 8 && src2 < 8) {
            unsigned op = l2c[k][1];
            add(out, 0x0408 | s | (dst << 4) | ((op & 3) << 5) | (src2 << 7) | ((op >> 2) << 11) | (x << 12) | (src1 << 13));
            return;
        }
}

bool compact_form(unsigned long w, unsigned dsz, unsigned br, unsigned &half, bool &branch)
{
    std::vector<Form16> f;
    forms16(w, f);
    for (size_t k = 0; k < f.size(); k++) {
        if (!((f[k].dsz >> dsz) & 1)) continue;
        if (f[k].br >= 0 && (unsigned)f[k].br != br) continue;
        half = f[k].h; branch = f[k].branch;
        return true;
    }
    return false;
}

namespace {

/* one instruction of a code section, as the compressor sees it */
struct CI {
    unsigned long w;
    unsigned long off;      /* where the uncompressed layout put it */
    bool p;                 /* its p-bit: the next instruction is in its execute packet */
    bool load;
    bool nop4;
    bool label;             /* a symbol stands here */
    bool pinned;            /* must start a word whatever else: a global, or a relocation's target */
    bool fixed;             /* has a fixup that stays a relocation or a 32-bit field: never 16-bit */
    bool bnopRel;           /* a 32-bit BNOP to another section: only in a packet without a header */
    bool bnopLocal;         /* a 32-bit BNOP to a label here: halfwords in a header-based packet */
    bool branch32;          /* a B or CALLP to a label here: its target starts a word */
    bool droppable;         /* a load whose NOP 4 a PROT header may stand for */
    int target;             /* the instruction a branch to a label here goes to, or -1 */
    int fix;                /* its branch fixup, or -1 */
    std::vector<Form16> forms;
};

/* one word of a fetch packet: one instruction, or two halves (b >= 0) */
struct Word { int a, b; };

struct FP {
    long addr;
    bool header;
    unsigned prot, br, dsz;
    int nw;
    Word words[8];
};

bool is_load(unsigned long w)
{
    if (((w >> 2) & 3) == 1) {
        unsigned f = (unsigned)(w >> 2) & 0x5F;
        for (size_t k = 0; k < sizeof kMem / sizeof *kMem; k++) if (kMem[k].form == f) return kMem[k].load;
        return (f == 0x4D || f == 0x49);            /* LDNW, LDNDW */
    }
    if ((w & 0xC) == 0xC) return ((w >> 4) & 7) != 3 && ((w >> 4) & 7) != 5 && ((w >> 4) & 7) != 7;   /* 15-bit offset: LD* */
    return false;
}

bool is_branch(unsigned long w)
{
    return (w & 0x7C) == 0x10 || (w & 0x1FFC) == 0x120 || (w & 0x0F83EFFCul) == 0x360 || (w & 0x0F830FFCul) == 0x800360 ||
           (w & 0x7FEFFC) == 0x1800E0 || (w & 0x7FEFFC) == 0x1C00E0;
}

/* the cycles an execute packet takes, for the delay slots of a branch */
unsigned cycles_of(unsigned long w)
{
    if ((w & ~0x1E001ul) == 0) return (unsigned)((w >> 13) & 15) + 1;
    return 1;
}

class Layout {
public:
    Layout(std::vector<CI> &ins) : in(ins), n((int)ins.size()) {}

    std::vector<CI> &in;
    int n;
    std::vector<long> prevAddr, curAddr;
    std::vector<char> aligned, curSmall, placed;
    std::vector<FP> fps;

    bool fits7(int k, long fp) const
    {
        int t = in[(size_t)k].target;
        if (t < 0) return false;
        /* a target placed in this pass is where it is; one ahead, where the pass before put it */
        long ta = placed[(size_t)t] ? curAddr[(size_t)t] : prevAddr[(size_t)t];
        long d = (ta - fp) / 2;
        return (ta & 1) == 0 && d >= -64 && d < 64;
    }

    bool ok16(int k, unsigned prot, unsigned br, unsigned dsz, long fp) const
    {
        (void)prot;
        const CI &c = in[(size_t)k];
        if (c.fixed) return false;
        for (size_t f = 0; f < c.forms.size(); f++) {
            const Form16 &x = c.forms[f];
            if (!((x.dsz >> dsz) & 1)) continue;
            if (x.br >= 0 && (unsigned)x.br != br) continue;
            if (x.branch && !fits7(k, fp)) continue;
            return true;
        }
        return false;
    }

    bool dropHere(int k, bool prot, bool first, bool prevProt) const
    {
        if (k <= 0 || k >= n || !in[(size_t)k].nop4) return false;
        if (!in[(size_t)(k - 1)].droppable) return false;
        return first ? prevProt : prot;
    }

    /* one fetch packet under one header from instruction i: the instruction it stops before,
       or -1 when the header cannot be used here */
    int greedy(int i, long fp, unsigned prot, unsigned br, unsigned dsz, bool prevProt, Word *words, int &nw) const
    {
        nw = 0;
        int k = i;
        while (nw < 7 && k < n) {
            if (dropHere(k, prot != 0, k == i, prevProt)) { k++; continue; }
            const CI &c = in[(size_t)k];
            if (c.bnopRel) return -1;
            if (prot && c.load && !c.droppable) return -1;
            int k2 = k + 1;
            if (prot && c.load) k2++;
            Word wd = { k, -1 };
            if (k2 < n && !in[(size_t)k2].bnopRel && ok16(k, prot, br, dsz, fp) && ok16(k2, prot, br, dsz, fp) && !aligned[(size_t)k2] &&
                !(prot && in[(size_t)k2].load && !in[(size_t)k2].droppable)) {
                wd.b = k2;
                k = k2 + 1;
            } else k++;
            words[nw++] = wd;
        }
        while (prot && k < n && dropHere(k, true, false, prevProt)) k++;
        return k;
    }

    /* one pass over the section, packet by packet */
    void pass()
    {
        fps.clear();
        curAddr.assign((size_t)n, -1);
        placed.assign((size_t)n, 0);
        curSmall.assign((size_t)n, 0);
        int i = 0;
        long fp = 0;
        bool prevProt = false;
        Word words[8], best[8];
        int nw = 0, bestNw = 0;
        while (i < n) {
            if (prevProt && dropHere(i, true, true, true)) i++;     /* the NOP 4 the PROT packet before stood for */
            if (i >= n) break;
            int plainEnd = std::min(i + 8, n);
            FP p;
            p.addr = fp; p.header = false; p.prot = p.br = p.dsz = 0;
            int bestEnd = -1;
            unsigned bp = 0, bb = 0, bd = 0;
            if (plainEnd - i == 8) {     /* fewer than eight left: a plain packet takes them all */
                /* a header's bit that no instruction within reach cares about changes nothing:
                   only the first of the contexts that are the same is tried */
                bool anyDsz = false, anyBr = false, anyLoad = false;
                for (int k = i; k < n && k < i + 24; k++) {
                    const CI &c = in[(size_t)k];
                    if (c.load) anyLoad = true;
                    for (size_t f = 0; f < c.forms.size(); f++) {
                        if (c.forms[f].dsz != 0xFF) anyDsz = true;
                        if (c.forms[f].br >= 0) anyBr = true;
                    }
                }
                /* asm6x's order, read from its objects: PROT last, BR before it, DSZ counting up;
                   the first of the contexts that take the most instructions is the one */
                for (unsigned prot = 0; prot < (anyLoad ? 2u : 1u); prot++)
                    for (unsigned br = 0; br < (anyBr ? 2u : 1u); br++)
                        for (unsigned dsz = 0; dsz < (anyDsz ? 8u : 1u); dsz++) {
                            int e = greedy(i, fp, prot, br, dsz, prevProt, words, nw);
                            if (e > bestEnd) { bestEnd = e; std::copy(words, words + nw, best); bestNw = nw; bp = prot; bb = br; bd = dsz; }
                        }
            }
            if (bestEnd >= plainEnd) {
                p.header = true; p.prot = bp; p.br = bb; p.dsz = bd; p.nw = bestNw; std::copy(best, best + bestNw, p.words);
                i = bestEnd; prevProt = bp != 0;
            } else {
                p.nw = 0;
                for (int k = i; k < plainEnd; k++) { Word wd = { k, -1 }; p.words[p.nw++] = wd; }
                /* a plain packet still takes a NOP 4 that a PROT packet before it stood for */
                i = plainEnd; prevProt = false;
            }
            long a = fp;
            for (int j = 0; j < p.nw; j++) {
                const Word &wd = p.words[j];
                curAddr[(size_t)wd.a] = a; placed[(size_t)wd.a] = 1; curSmall[(size_t)wd.a] = wd.b >= 0;
                if (wd.b >= 0) { curAddr[(size_t)wd.b] = a + 2; placed[(size_t)wd.b] = 1; curSmall[(size_t)wd.b] = 1; }
                a += 4;
            }
            fps.push_back(p);
            fp += 32;
        }
    }
};

}

/* the compressor, over one code section of an unresolved pass */
static bool compress_section(Unit &u, int si)
{
    Section &sec = u.sections[(size_t)si];
    if (!sec.hasCode || sec.dataBytes || sec.insns.empty()) return false;
    /* the instructions must be the section: no .align padding among them */
    for (size_t k = 0; k < sec.insns.size(); k++) if (sec.insns[k] != 4 * k) return false;
    unsigned long codeEnd = 4 * (unsigned long)sec.insns.size();
    int n = (int)sec.insns.size();
    std::vector<CI> in((size_t)n);
    for (int k = 0; k < n; k++) {
        CI &c = in[(size_t)k];
        unsigned long at = 4 * (unsigned long)k;
        c.w = (unsigned long)sec.bytes[at] | ((unsigned long)sec.bytes[at + 1] << 8) | ((unsigned long)sec.bytes[at + 2] << 16) |
              ((unsigned long)sec.bytes[at + 3] << 24);
        c.off = at; c.p = c.w & 1;
        c.load = is_load(c.w); c.nop4 = c.w == 0x6000;
        c.label = false; c.pinned = false; c.fixed = false; c.bnopRel = false; c.bnopLocal = false; c.branch32 = false;
        c.droppable = false; c.target = -1; c.fix = -1;
        forms16(c.w, c.forms);
    }
    /* labels: a symbol at an instruction; anything else and the section is left alone */
    for (size_t i = 0; i < u.symbols.size(); i++) {
        const Symbol &s = u.symbols[i];
        if (s.alias >= 0 && s.alias < (int)u.symbols.size() && u.symbols[(size_t)s.alias].section == si) return false;
        if (!s.defined || s.section != si) continue;
        if (s.value < 0 || (unsigned long)s.value > codeEnd) return false;
        if ((unsigned long)s.value == codeEnd) continue;
        if (s.value % 4) return false;
        CI &c = in[(size_t)(s.value / 4)];
        c.label = true;
        if (s.bind != B_LOCAL) c.pinned = true;
    }
    /* the fixups: a branch to a label of this section may be settled in place; anything else
       keeps its instruction 32-bit, and its target (in a section of ours) starts a word */
    for (size_t i = 0; i < u.fixups.size(); i++) {
        const Fixup &f = u.fixups[i];
        bool pcr = f.kind == R_PCR_S21 || f.kind == R_PCR_S12 || f.kind == R_PCR_S10 || f.kind == R_PCR_S7;
        if (f.symbol >= 0 && f.sub < 0 && pcr) {
            const Symbol &t = u.symbols[(size_t)f.symbol];
            bool local = t.defined && t.section == si && t.bind != B_WEAK;
            if (t.defined && t.section == si && !local && t.value >= 0 && (unsigned long)t.value < codeEnd) in[(size_t)(t.value / 4)].pinned = true;
            if (f.section != si && t.defined && t.section == si && t.value >= 0 && (unsigned long)t.value < codeEnd) in[(size_t)(t.value / 4)].pinned = true;
        }
        if (f.section == si && f.sub >= 0) return false;        /* a label difference inside code */
        if (f.symbol >= 0 && f.sub >= 0) continue;
        if (f.section != si) continue;
        if (f.symbol < 0) return false;                         /* $ in code */
        if (f.at % 4 || f.at >= codeEnd) return false;
        CI &c = in[(size_t)(f.at / 4)];
        if (c.fix >= 0) return false;
        c.fix = (int)i;
        c.fixed = true;
        const Symbol &t = u.symbols[(size_t)f.symbol];
        bool local = t.defined && (t.section == si) && t.bind != B_WEAK && f.addend == 0 && t.value >= 0 && t.value % 4 == 0 &&
                     (unsigned long)t.value < codeEnd && pcr;
        bool bnop = f.kind == R_PCR_S12;
        if (local) {
            c.target = (int)(t.value / 4);
            if (bnop) c.bnopLocal = true; else c.branch32 = true;
            if (f.kind == R_PCR_S21 && (c.w & 0xF000007Cul) == 0x10) c.fixed = false;   /* an unconditional B label */
            if (bnop) c.fixed = false;
        } else {
            if (bnop) c.bnopRel = true;
            if (pcr && t.defined && t.section == si && t.value >= 0 && (unsigned long)t.value < codeEnd) in[(size_t)(t.value / 4)].pinned = true;
        }
    }
    /* a CALLP stays 32-bit: its target starts a word */
    for (int k = 0; k < n; k++) {
        CI &c = in[(size_t)k];
        if (c.target >= 0 && (c.w & 0xF000007Cul) == 0x10000010ul) { c.fixed = true; in[(size_t)c.target].pinned = true; }
    }
    /* PROT may stand for a NOP 4 only after a load alone in its packet, followed by a NOP 4
       alone in its own, with no label on the NOP and no branch still in flight */
    for (int k = 0; k + 1 < n; k++) {
        CI &c = in[(size_t)k];
        if (!c.load || c.p || (k > 0 && in[(size_t)(k - 1)].p)) continue;
        const CI &nx = in[(size_t)(k + 1)];
        if (!nx.nop4 || nx.p || nx.label) continue;
        /* back over the packets before the load: fewer than 5 cycles since a branch, and the load is in its delay slots */
        unsigned cyc = 0;
        bool flight = false;
        int j = k - 1;
        while (j >= 0 && cyc < 6) {
            int start = j;
            while (start > 0 && in[(size_t)(start - 1)].p) start--;
            bool br = false;
            unsigned c1 = 1;
            for (int q = start; q <= j; q++) {
                if (is_branch(in[(size_t)q].w)) br = true;
                if ((in[(size_t)q].w & 0x1FFC) == 0x120) c1 = std::max(c1, (unsigned)((in[(size_t)q].w >> 13) & 7) + 1);
                c1 = std::max(c1, cycles_of(in[(size_t)q].w));
            }
            if (br && cyc + c1 - 1 < 5) { flight = true; break; }
            cyc += c1;
            j = start - 1;
        }
        if (!flight) c.droppable = true;
    }

    /* a 32-bit BNOP counts halfwords in a header-based packet, so reaches half as far: one
       too far for that stays in a packet without a header, found before the layout from the
       plain distance and after it from the real one */
    for (int k = 0; k < n; k++) {
        CI &c = in[(size_t)k];
        if (c.bnopLocal) {
            long d = ((long)in[(size_t)c.target].off - (long)(c.off & ~31ul)) / 2;
            if (d < -2048 || d >= 2048) c.bnopRel = true;
        }
    }
    Layout L(in);
    std::vector<int> fpOf;
    for (int round = 0; ; round++) {
        if (round == 8) return false;
        L.prevAddr.assign((size_t)n, 0);
        for (int k = 0; k < n; k++) L.prevAddr[(size_t)k] = (long)in[(size_t)k].off;
        L.aligned.assign((size_t)n, 0);
        for (int k = 0; k < n; k++) L.aligned[(size_t)k] = in[(size_t)k].pinned;
        bool sticky = false, settled = false;
        for (int it = 0; it < 80 && !settled; it++) {
            if (it == 40) sticky = true;
            L.pass();
            std::vector<char> al((size_t)n, 0);
            for (int k = 0; k < n; k++) al[(size_t)k] = in[(size_t)k].pinned || (sticky && L.aligned[(size_t)k]);
            for (int k = 0; k < n; k++)
                if (in[(size_t)k].target >= 0 && !L.curSmall[(size_t)k]) al[(size_t)in[(size_t)k].target] = 1;
            bool same = al == L.aligned;
            for (int k = 0; k < n && same; k++) if (L.curAddr[(size_t)k] >= 0 && L.curAddr[(size_t)k] != L.prevAddr[(size_t)k]) same = false;
            for (int k = 0; k < n; k++) if (L.curAddr[(size_t)k] >= 0) L.prevAddr[(size_t)k] = L.curAddr[(size_t)k];
            L.aligned = al;
            settled = same;
        }
        if (!settled) return false;

        /* the layout, checked against every rule of the machine before anything is written */
        const std::vector<long> &A = L.curAddr;
        fpOf.assign((size_t)n, -1);
        for (size_t f = 0; f < L.fps.size(); f++)
            for (int j = 0; j < L.fps[f].nw; j++) {
                fpOf[(size_t)L.fps[f].words[j].a] = (int)f;
                if (L.fps[f].words[j].b >= 0) fpOf[(size_t)L.fps[f].words[j].b] = (int)f;
            }
        bool far = false;
        for (int k = 0; k < n; k++) {
            CI &c = in[(size_t)k];
            if (A[(size_t)k] < 0) {
                /* dropped: a NOP 4 after a load in a PROT packet, nothing else */
                if (!c.nop4 || k == 0 || !in[(size_t)(k - 1)].droppable || fpOf[(size_t)(k - 1)] < 0 || !L.fps[(size_t)fpOf[(size_t)(k - 1)]].prot) return false;
                continue;
            }
            const FP &p = L.fps[(size_t)fpOf[(size_t)k]];
            bool small = L.curSmall[(size_t)k] != 0;
            if (c.pinned && (A[(size_t)k] & 3)) return false;
            if (p.prot && c.load && (k + 1 >= n || A[(size_t)(k + 1)] >= 0)) return false;
            if (c.target >= 0) {
                long ta = A[(size_t)c.target];
                if (ta < 0) return false;
                long fp = A[(size_t)k] & ~31L;
                if (small) { long d = (ta - fp) / 2; if ((ta & 1) || d < -64 || d >= 64) return false; }
                else if (c.bnopLocal && p.header) { long d = (ta - fp) / 2; if (d < -2048 || d >= 2048) { c.bnopRel = true; far = true; } }
                else { if (ta & 3) return false; }
            }
            if (c.bnopRel && p.header && !far) return false;
            if (small) {
                bool ok = false;
                for (size_t f = 0; f < c.forms.size() && !ok; f++) {
                    const Form16 &x = c.forms[f];
                    if ((x.dsz >> p.dsz) & 1 && (x.br < 0 || (unsigned)x.br == p.br)) ok = true;
                }
                if (!ok || c.fixed) return false;
            }
        }
        if (!far) break;
    }
    const std::vector<long> &A = L.curAddr;

    /* the new section: packet by packet */
    std::vector<unsigned char> out;
    for (size_t f = 0; f < L.fps.size(); f++) {
        const FP &p = L.fps[f];
        unsigned long hdr = 0xE0000000ul | ((unsigned long)p.prot << 20) | ((unsigned long)p.dsz << 16) | ((unsigned long)p.br << 15);
        size_t nw = p.header ? 7 : 8;
        for (size_t j = 0; j < nw; j++) {
            unsigned long w = 0;
            if ((int)j < p.nw) {
                const Word &wd = p.words[j];
                if (wd.b < 0) w = in[(size_t)wd.a].w;
                else {
                    unsigned h[2];
                    for (int q = 0; q < 2; q++) {
                        const CI &c = in[(size_t)(q ? wd.b : wd.a)];
                        unsigned half = 0;
                        bool br = false;
                        for (size_t fi = 0; fi < c.forms.size(); fi++) {
                            const Form16 &x = c.forms[fi];
                            if ((x.dsz >> p.dsz) & 1 && (x.br < 0 || (unsigned)x.br == p.br)) { half = x.h; br = x.branch; break; }
                        }
                        if (br) {
                            long d = (A[(size_t)c.target] - (long)p.addr) / 2;
                            half |= ((unsigned)d & 0x7F) << 6;
                        }
                        h[q] = half;
                        if (c.p) hdr |= 1ul << (2 * j + (size_t)q);
                    }
                    w = (unsigned long)h[0] | ((unsigned long)h[1] << 16);
                    hdr |= 1ul << (21 + j);
                }
            }
            for (int b = 0; b < 4; b++) out.push_back((unsigned char)(w >> (8 * b)));
        }
        if (p.header) for (int b = 0; b < 4; b++) out.push_back((unsigned char)(hdr >> (8 * b)));
    }
    long newEnd = 0;
    for (int k = n - 1; k >= 0; k--) if (A[(size_t)k] >= 0) { newEnd = A[(size_t)k] + (L.curSmall[(size_t)k] ? 2 : 4); break; }

    /* move what pointed into the old layout */
    for (size_t i = 0; i < u.fixups.size(); i++) {
        Fixup &f = u.fixups[i];
        if (f.section != si) continue;
        int k = (int)(f.at / 4);
        f.at = (unsigned long)A[(size_t)k];
        if (L.curSmall[(size_t)k]) f.settled = true;
        else if (in[(size_t)k].bnopLocal && L.fps[(size_t)fpOf[(size_t)k]].header) f.half = true;
    }
    for (size_t i = 0; i < u.symbols.size(); i++) {
        Symbol &s = u.symbols[i];
        if (!s.defined || s.section != si) continue;
        if ((unsigned long)s.value >= codeEnd) s.value = newEnd;
        else s.value = A[(size_t)(s.value / 4)];
    }
    /* where each instruction went, for the line table: an instruction folded away (a NOP a header absorbed)
       is where the next one is, or the section's end */
    for (int k = n - 1; k >= 0; k--)
        sec.insns[(size_t)k] = A[(size_t)k] >= 0 ? (unsigned long)A[(size_t)k]
                             : (k + 1 < n ? sec.insns[(size_t)k + 1] : (unsigned long)newEnd);
    sec.codeEnd = (unsigned long)newEnd;
    sec.bytes.swap(out);
    sec.compressed = true;
    return true;
}

void compress_sections(Unit &u)
{
    for (size_t i = 0; i < u.sections.size(); i++)
        if (u.sections[i].code) compress_section(u, (int)i);
}
