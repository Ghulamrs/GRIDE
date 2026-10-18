// Spec: ARM EHABI 5 and 6 with SPRAB89B 11.2 - the index between __TI_UNWIND_TABLE_START and _END,
// an entry per function counted in PREL31 halfwords, found for a pc by its function's start: the
// entry with the greatest start at or below the pc, the later in the index where two starts agree.
#ifndef RTS6X_UNWIND_INDEX_H
#define RTS6X_UNWIND_INDEX_H

namespace rts6x {

// The index read once into (start, entry) pairs ordered by start, then searched by halving.
// lnk6x keeps an input unit's entries in their own order, so an entry naming a weak function kept
// elsewhere sits out of order: the pairs are sorted, never assumed. Without room, a scan of the index.
class UnwindIndex {
public:
    // The address of pc's entry and its function's start, or 0 when no function starts at or below pc.
    static unsigned lookup(unsigned pc, unsigned &start);

private:
    struct Pair {
        unsigned start;   // the function's first byte
        unsigned at;      // the entry's address in the index, which also breaks a tie of starts
    };

    // The pairs, built on the first lookup; false when no room could be had for them.
    static bool build();
    static unsigned scan(unsigned pc);
    static bool before(const Pair &a, const Pair &b) { return a.start != b.start ? a.start < b.start : a.at < b.at; }
    static void sift(Pair *pairs, int root, int count);

    static Pair *pairs_;
    static int count_;
    // The pair last answered: a throw asks of the same function again and again, phase two above all.
    static int last_;
    static bool tried_;
};

}  // namespace rts6x

#endif
