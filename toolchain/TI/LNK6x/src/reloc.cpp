/*  reloc.cpp - the C6000 fixups, as the bed spells them.
 *
 *  Three of them carry every reference in tests/ref, and each was checked against the
 *  reference images word by word (docs/elf-observed.md shows the arithmetic):
 *
 *    ABS_L16   MVKL: the low sixteen bits of the address, in bits 7..22
 *    ABS_H16   MVKH: the high sixteen, the same field - no rounding, the pair is MVKL/MVKH
 *    PCR_S21   a branch: (target - the fetch packet the branch is in) >> 2, in bits 7..27
 *
 *  The fetch packet is the instruction's address with its low five bits cleared. Every branch
 *  in the bed happens to sit at the start of one, so the bed cannot tell that rule from
 *  "relative to the instruction"; docs/known.md says so, and a probe would settle it.
 */
#include "lnk.h"
#include <cstdio>

static void field(u8 *p, u32 value, int lsb, int bits)
{
    u32 mask = (bits >= 32) ? 0xFFFFFFFFu : (((1u << bits) - 1u) << lsb);
    u32 w = rd32(p);
    wr32(p, (w & ~mask) | ((value << lsb) & mask));
}

const char *reloc_name(u32 type)
{
    switch (type) {
    case R_C6000_NONE:    return "R_C6000_NONE";
    case R_C6000_ABS32:   return "R_C6000_ABS32";
    case R_C6000_ABS16:   return "R_C6000_ABS16";
    case R_C6000_ABS8:    return "R_C6000_ABS8";
    case R_C6000_PCR_S21: return "R_C6000_PCR_S21";
    case R_C6000_PCR_S12: return "R_C6000_PCR_S12";
    case R_C6000_PCR_S10: return "R_C6000_PCR_S10";
    case R_C6000_PCR_S7:  return "R_C6000_PCR_S7";
    case R_C6000_ABS_S16: return "R_C6000_ABS_S16";
    case R_C6000_ABS_L16: return "R_C6000_ABS_L16";
    case R_C6000_ABS_H16: return "R_C6000_ABS_H16";
    case R_C6000_PREL31:  return "R_C6000_PREL31";
    case R_C6000_EHTYPE:  return "R_C6000_EHTYPE";
    case R_C6000_PCR_H16: return "R_C6000_PCR_H16";
    case R_C6000_PCR_L16: return "R_C6000_PCR_L16";
    default:              return 0;
    }
}

u32 reloc_width(u32 type)
{
    switch (type) {
    case R_C6000_NONE:  return 0;
    case R_C6000_ABS16: return 2;
    case R_C6000_ABS8:  return 1;
    default:            return 4;
    }
}

/*  **A bounded field is checked before it is written** (the review's L-A5): `field()` masks,
 *  so a value that does not fit used to land as its low bits - a branch 16 MB away became a
 *  branch somewhere else, rc 0. The ranges are the C6000 ELF ABI's: a PC-relative field is
 *  signed and counts words from the fetch packet, so the byte distance must be a multiple of
 *  four; ABS_S16 feeds MVK, which sign-extends; ABS16 and ABS8 are data and take either a
 *  signed or an unsigned value of their width. */
static bool check_range(u32 type, u32 P, u32 V, std::string &err)
{
    int bits = 0; bool pcr = false; i64 lo = 0, hi = 0;
    switch (type) {
    case R_C6000_PCR_S21: bits = 21; pcr = true; break;
    case R_C6000_PCR_S12: bits = 12; pcr = true; break;
    case R_C6000_PCR_S10: bits = 10; pcr = true; break;
    case R_C6000_PCR_S7:  bits = 7;  pcr = true; break;
    case R_C6000_ABS_S16: lo = -0x8000; hi = 0x7FFF; break;
    case R_C6000_ABS16:   lo = -0x8000; hi = 0xFFFF; break;
    case R_C6000_ABS8:    lo = -0x80;   hi = 0xFF;   break;
    default: return true;
    }
    char b[200];
    if (pcr) {
        i32 d = (i32)(V - (P & ~0x1Fu));
        lo = -((i64)1 << (bits - 1)); hi = ((i64)1 << (bits - 1)) - 1;
        if (d & 3) {
            snprintf(b, sizeof b, "the displacement %+d bytes from the fetch packet at 0x%08x is not "
                     "a whole number of words", (int)d, (unsigned)(P & ~0x1Fu));
            err = b; return false;
        }
        i32 w = d >> 2;
        if (w < lo || w > hi) {
            snprintf(b, sizeof b, "the displacement %+d words (%+d bytes) from the fetch packet at "
                     "0x%08x does not fit a signed %d-bit field (%lld..%lld words)",
                     (int)w, (int)d, (unsigned)(P & ~0x1Fu), bits, (long long)lo, (long long)hi);
            err = b; return false;
        }
        return true;
    }
    i64 v = (i32)V;
    if (v < lo || v > hi) {
        snprintf(b, sizeof b, "the value 0x%08x (%lld) does not fit the field (%lld..%lld)",
                 (unsigned)V, (long long)v, (long long)lo, (long long)hi);
        err = b; return false;
    }
    return true;
}

static bool apply(u32 type, u8 *p, u32 P, u32 S, i32 A, u32 B, std::string &err);

bool pcr_s21_reaches(u32 P, u32 V)
{
    i32 d = (i32)(V - (P & ~0x1Fu));
    if (d & 3) return true;
    i32 w = d >> 2;
    return w >= -(1 << 20) && w <= (1 << 20) - 1;
}

/*  **The trampoline lnk6x writes for a far call**, from q15-far.out (8.2.2), word for word:
 *
 *      053c54f7   STW   .D2T2  B10,*B15--[2]     ; in parallel with the MVKL
 *      0500002a   MVKL  .S2    callee,B10        ; the low half, in bits 7..22
 *      0500006a   MVKH  .S2    callee,B10        ; the high half
 *      00280362   B     .S2    B10
 *      053c52e6   LDW   .D2T2  *++B15[2],B10     ; B10 back, in the branch's delay slots
 *      00006000   NOP   4
 *      00000000   NOP
 *      00000000   NOP
 *
 *  B3 is untouched, so a CALLP's return address and a B's lack of one both survive it. */
/*
 *  **And when the trampoline itself reaches the callee, it is a plain branch**: `B .S1 callee`
 *  (00000010 with the PCR_S21 field), `NOP 5` (00008000), six zero words - q20's to a callee
 *  0x20 on, q27-reach-fwd-out's to two 0x3FFFFC and 0x3FFFC0 on, the last words of reach from
 *  their own fetch packets. Same 32 bytes either way, so which it is never moves anything. */
void tramp_code(u8 *p, u32 V, u32 P)
{
    static const u32 lng[8] = { 0x053c54f7u, 0x0500002au, 0x0500006au, 0x00280362u,
                                0x053c52e6u, 0x00006000u, 0x00000000u, 0x00000000u };
    static const u32 sht[8] = { 0x00000010u, 0x00008000u, 0, 0, 0, 0, 0, 0 };
    bool near = P && pcr_s21_reaches(P, V) && !((V - (P & ~0x1Fu)) & 3);
    for (int i = 0; i < 8; i++) wr32(p + 4 * i, near ? sht[i] : lng[i]);
    if (near) { field(p, (V - (P & ~0x1Fu)) >> 2, 7, 21); return; }
    field(p + 4, V & 0xFFFFu, 7, 16);
    field(p + 8, (V >> 16) & 0xFFFFu, 7, 16);
}

bool apply_reloc(u32 type, u8 *p, u32 P, u32 S, i32 A, u32 B, std::string &err, bool &range)
{
    range = false;
    if (!check_range(type, P, S + (u32)A, err)) { range = true; return false; }
    return apply(type, p, P, S, A, B, err);
}

void apply_reloc_unchecked(u32 type, u8 *p, u32 P, u32 S, i32 A, u32 B)
{
    std::string err;
    apply(type, p, P, S, A, B, err);
}

static bool apply(u32 type, u8 *p, u32 P, u32 S, i32 A, u32 B, std::string &err)
{
    u32 V = S + (u32)A;
    switch (type) {
    case R_C6000_NONE:    break;
    case R_C6000_ABS32:   wr32(p, V); break;
    case R_C6000_ABS16:   wr16(p, (u16)V); break;
    case R_C6000_ABS8:    *p = (u8)V; break;
    case R_C6000_ABS_L16: field(p, V & 0xFFFFu, 7, 16); break;
    case R_C6000_ABS_H16: field(p, (V >> 16) & 0xFFFFu, 7, 16); break;
    case R_C6000_ABS_S16: field(p, V & 0xFFFFu, 7, 16); break;
    case R_C6000_PCR_S21: field(p, (V - (P & ~0x1Fu)) >> 2, 7, 21); break;
    case R_C6000_PCR_S12: field(p, (V - (P & ~0x1Fu)) >> 2, 16, 12); break;
    case R_C6000_PCR_S10: field(p, (V - (P & ~0x1Fu)) >> 2, 13, 10); break;
    case R_C6000_PCR_S7:  field(p, (V - (P & ~0x1Fu)) >> 2, 16, 7); break;
    /*  **The MVKL/MVKH pair of `$PCR_OFFSET(dest, base)`**, and the addend is what says
     *  which base. TI's assembler writes `A = (P & ~31) - base`, so the base label comes
     *  back as `(P & ~31) - A`, and the value is measured from *that label's* fetch
     *  packet - not from the instruction's. `r_addend` is therefore not added to S.
     *
     *  Read off the three sites in tdeh_uwentry_c6000.obj against q07-lib.out, where
     *  base_pcr (+0x08) serves two and cxa_base_pcr (+0xB4) the third: the wanted fields
     *  are 1260, 0720 and 1660, and this gives all three. Both `S + A - P` and
     *  `S + A - (P & ~31)` give none of them - see docs/known.md. */
    case R_C6000_PCR_L16:
        field(p, (S - (((P & ~0x1Fu) - (u32)A) & ~0x1Fu)) & 0xFFFFu, 7, 16); break;
    case R_C6000_PCR_H16:
        field(p, ((S - (((P & ~0x1Fu) - (u32)A) & ~0x1Fu)) >> 16) & 0xFFFFu, 7, 16); break;
    /*  **PREL31, the unwind tables' own, counts halfwords on the C6000**: a 31-bit offset
     *  from the word to its target, shifted right one where ARM's is in bytes, the word's
     *  top bit kept - it is a flag, not a digit. Read off q07's .c6xabi.exidx: 0x7fffd678
     *  at 0xc0009310 is process_unwind at 0xc0004000 only as (S - P) / 2. */
    /*  **EHTYPE is a catch clause's type_info from the static base**, S + A - B, a plain
     *  word in .c6xabi.extab - lnk6x 7.4.4 writes fffffd40 for a type_info at 8000a7f4
     *  with __TI_STATIC_BASE at 8000aab4; the runtime adds DP back. */
    case R_C6000_EHTYPE:  wr32(p, V - B); break;
    case R_C6000_PREL31: {
        u32 w = rd32(p);
        wr32(p, (w & 0x80000000u) | (((u32)((i32)(V - P) >> 1)) & 0x7FFFFFFFu));
        break;
    }
    default: {
        const char *nm = reloc_name(type);
        char b[96];
        if (nm) snprintf(b, sizeof b, "relocation %s (%u) is not handled", nm, type);
        else    snprintf(b, sizeof b, "relocation type %u is not handled", type);
        err = b; return false;
    }
    }
    return true;
}
