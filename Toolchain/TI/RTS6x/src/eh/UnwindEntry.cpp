// Spec: SPRAB89B 11.2 and the ARM EHABI 6 (the index of functions; PREL31 offsets, which
// the C6000 counts in halfwords) and 11.4 (the personality's words: PR3's compact one with the
// pop mask, PR2's byte codes - MV FP,SP; POP mask; RETURN - with a count of words that follow).

#include "UnwindTable.h"
#include "UnwindContext.h"
#include "UnwindIndex.h"

namespace rts6x {

namespace {

unsigned word(unsigned at) { return *reinterpret_cast<const unsigned *>(at); }

}  // namespace

bool UnwindEntry::find(unsigned pc, UnwindEntry &entry)
{
    unsigned function;
    unsigned at = UnwindIndex::lookup(pc - 1, function);
    if (at == 0) return false;
    unsigned second = word(at + 4);
    if (second == 1) return false;
    entry.function_ = function;
    entry.table_ = (second & 0x80000000u) ? 0 : prel31(at + 4);
    entry.word_ = entry.table_ ? word(entry.table_) : second;
    entry.wide_ = (entry.word_ >> 24) == 0x82;
    entry.descriptors_ = 0;
    if (entry.table_) entry.descriptors_ = entry.table_ + 4 + (entry.wide_ ? 4 * (entry.word_ >> 16 & 0xff) : 0);
    return true;
}

unsigned UnwindEntry::frameMask() const
{
    if ((word_ >> 24) == 0x83) {
        if ((word_ >> 17 & 0x7f) != 0x7f) return 0;
        return word_ >> 4 & 0x1fff;
    }
    if (!wide_ || !table_) return 0;
    // PR2: the first two codes in the word, the rest in the words after it, high byte first.
    unsigned more = word_ >> 16 & 0xff;
    if (more == 0) return 0;
    unsigned op0 = word_ >> 8 & 0xff, op1 = word_ & 0xff, next = word(table_ + 4);
    unsigned op2 = next >> 24 & 0xff, op3 = next >> 16 & 0xff;
    if (op0 != 0xd0 || (op1 & 0xe0) != 0x80 || op3 != 0xe7) return 0;
    return (op1 & 0x1f) << 8 | op2;
}

bool UnwindEntry::unwind(UnwindContext &context, unsigned &pc) const
{
    const unsigned mask = frameMask();
    if ((mask & 1u << 12) == 0 || (mask & 1u << 5) == 0) return false;
    // The save area below the frame pointer, a word a register from bit 12 down: A15 at fp itself, then
    // bit 11 (no register), B14-B10, B3, A14-A10. Written out, one test a bit, since this runs per frame.
    const unsigned fp = context.frame();
    unsigned p = fp - 4;
    if (mask & 1u << 11) p -= 4;
    if (mask & 1u << 10) { context.set(UnwindContext::B14, word(p)); p -= 4; }
    if (mask & 1u << 9) { context.set(UnwindContext::B13, word(p)); p -= 4; }
    if (mask & 1u << 8) { context.set(UnwindContext::B12, word(p)); p -= 4; }
    if (mask & 1u << 7) { context.set(UnwindContext::B11, word(p)); p -= 4; }
    if (mask & 1u << 6) { context.set(UnwindContext::B10, word(p)); p -= 4; }
    pc = word(p);
    p -= 4;
    if (mask & 1u << 4) { context.set(UnwindContext::A14, word(p)); p -= 4; }
    if (mask & 1u << 3) { context.set(UnwindContext::A13, word(p)); p -= 4; }
    if (mask & 1u << 2) { context.set(UnwindContext::A12, word(p)); p -= 4; }
    if (mask & 1u << 1) { context.set(UnwindContext::A11, word(p)); p -= 4; }
    if (mask & 1u << 0) context.set(UnwindContext::A10, word(p));
    context.set(UnwindContext::B15, fp);
    context.set(UnwindContext::A15, word(fp));
    return true;
}

bool ScopeDescriptor::read(unsigned at, const UnwindEntry &entry)
{
    if (at == 0 || word(at) == 0) return false;
    const bool wide = entry.wide();
    const unsigned w = wide ? 4 : 0;
    unsigned length, offset;
    if (wide) {
        length = word(at);
        offset = word(at + 4);
    } else {
        unsigned both = word(at);
        length = both & 0xffff;
        offset = both >> 16;
    }
    kind_ = (Kind)(((length & 1) << 1) | (offset & 1));
    begin_ = entry.function() + (offset & ~1u);
    end_ = begin_ + (length & ~1u);
    padAt_ = 0;
    type_ = 0;
    count_ = 0;
    if (kind_ == Specification) {
        count_ = word(at + 4 + w);
        next_ = at + 8 + w + 4 * (count_ & 0x7fffffffu) + ((count_ & 0x80000000u) ? 4 : 0);
        return true;
    }
    padAt_ = at + 4 + w;
    if (kind_ == Catch) {
        unsigned t = word(at + 8 + w);
        type_ = t;
        next_ = at + 12 + w;
    } else {
        next_ = at + 8 + w;
    }
    return true;
}

}  // namespace rts6x
