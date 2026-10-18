// Spec: SPRAB89B 11.2-11.5 with the ARM EHABI 6-7: the index of (function, word) pairs between
// __TI_UNWIND_TABLE_START and _END, each a PREL31 counted in halfwords; the word is EXIDX_CANTUNWIND
// (1), the frame's unwind word itself (bit 31 set), or a PREL31 to the function's table.
#ifndef RTS6X_UNWIND_TABLE_H
#define RTS6X_UNWIND_TABLE_H

namespace rts6x {

class UnwindContext;

// A function's unwind information: where it starts, its frame, and its scope descriptors.
class UnwindEntry {
public:
    // The entry whose function holds pc - a return address, so pc - 1 is what is looked up.
    static bool find(unsigned pc, UnwindEntry &entry);

    unsigned function() const { return function_; }
    // The registers the frame saved, in SPRAB89B 11.5's pop mask (bit 12 A15 ... bit 0 A10); 0 if unknown.
    unsigned frameMask() const;
    // The first descriptor, or 0; and whether its ranges are 32-bit (PR2) rather than 16-bit (PR3).
    unsigned descriptors() const { return descriptors_; }
    bool wide() const { return wide_; }

    // To the caller: its frame, the return address into it, the saved registers back; false if it cannot.
    bool unwind(UnwindContext &context, unsigned &pc) const;

    // The address a PREL31 at place names: 31 bits, signed, in halfwords - so the word doubled, bit 31 dropped.
    static unsigned prel31(unsigned place)
    {
        return place + (*reinterpret_cast<const unsigned *>(place) << 1);
    }

private:
    unsigned function_;
    unsigned word_;           // the unwind word: inline, or the table's first
    unsigned table_;          // the table's address, or 0 for an inline entry
    unsigned descriptors_;
    bool wide_;
};

// One scope descriptor of a table: a cleanup, a catch, or an exception specification.
class ScopeDescriptor {
public:
    enum Kind { Cleanup = 0, Specification = 1, Catch = 2 };
    enum { CatchAll = 0xffffffffu, Terminate = 0xfffffffeu };

    // The descriptor at address at in the table of entry; false at the list's end.
    bool read(unsigned at, const UnwindEntry &entry);

    Kind kind() const { return kind_; }
    // pc, a return address, is in the scope when it lies in [begin, end).
    bool holds(unsigned pc) const { return pc >= begin_ && pc < end_; }
    // The landing pad, read from the descriptor only when asked: most are passed over.
    unsigned pad() const { return UnwindEntry::prel31(padAt_); }
    // A catch's type: CatchAll, Terminate, or the type_info's address.
    unsigned type() const { return type_; }
    // A specification's count of types allowed.
    unsigned allowed() const { return count_; }
    unsigned next() const { return next_; }

private:
    Kind kind_;
    unsigned begin_, end_, padAt_, type_, count_, next_;
};

}  // namespace rts6x

#endif
