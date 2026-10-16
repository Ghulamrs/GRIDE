// Spec: ARM EHABI 6 with SPRAB89B 11.2 - the index's entries, a PREL31 to each function, ordered by
// start once and found by halving: the last pair whose start is at or below the pc.

#include <stdlib.h>
#include "UnwindIndex.h"
#include "UnwindTable.h"

extern "C" {
extern const char __TI_UNWIND_TABLE_START[];
extern const char __TI_UNWIND_TABLE_END[];
}

namespace rts6x {

UnwindIndex::Pair *UnwindIndex::pairs_;
int UnwindIndex::count_;
int UnwindIndex::last_;
bool UnwindIndex::tried_;

unsigned UnwindIndex::lookup(unsigned pc, unsigned &start)
{
    if (!tried_) {
        tried_ = true;
        build();
    }
    const Pair *p = pairs_;
    if (!p) {
        unsigned at = scan(pc);
        if (at) start = UnwindEntry::prel31(at);
        return at;
    }
    const int n = count_;
    int k = last_;
    if (p[k].start > pc || (k + 1 < n && p[k + 1].start <= pc)) {
        // The first pair whose start lies above pc; the one before it is the answer.
        int lo = 0, hi = n;
        while (lo < hi) {
            int mid = (lo + hi) >> 1;
            if (p[mid].start <= pc) lo = mid + 1;
            else hi = mid;
        }
        if (lo == 0) return 0;
        k = lo - 1;
        last_ = k;
    }
    start = p[k].start;
    return p[k].at;
}

unsigned UnwindIndex::scan(unsigned pc)
{
    unsigned lo = reinterpret_cast<unsigned>(__TI_UNWIND_TABLE_START);
    unsigned hi = reinterpret_cast<unsigned>(__TI_UNWIND_TABLE_END);
    unsigned at = 0, best = 0;
    for (unsigned e = lo; e < hi; e += 8) {
        unsigned start = UnwindEntry::prel31(e);
        if (start <= pc && (at == 0 || start >= best)) {
            at = e;
            best = start;
        }
    }
    return at;
}

bool UnwindIndex::build()
{
    unsigned lo = reinterpret_cast<unsigned>(__TI_UNWIND_TABLE_START);
    unsigned hi = reinterpret_cast<unsigned>(__TI_UNWIND_TABLE_END);
    int n = (int)((hi - lo) / 8);
    if (n <= 0) return false;
    Pair *p = static_cast<Pair *>(malloc(n * sizeof(Pair)));
    if (!p) return false;
    bool sorted = true;
    for (int i = 0; i < n; i++) {
        p[i].at = lo + 8 * i;
        p[i].start = UnwindEntry::prel31(p[i].at);
        if (i && before(p[i], p[i - 1])) sorted = false;
    }
    // A heap sort where the index is out of order: no room beyond the pairs, n log n at worst.
    if (!sorted) {
        for (int root = n / 2 - 1; root >= 0; root--) sift(p, root, n);
        for (int last = n - 1; last > 0; last--) {
            Pair t = p[0];
            p[0] = p[last];
            p[last] = t;
            sift(p, 0, last);
        }
    }
    pairs_ = p;
    count_ = n;
    return true;
}

void UnwindIndex::sift(Pair *pairs, int root, int count)
{
    const Pair moving = pairs[root];
    for (;;) {
        int child = 2 * root + 1;
        if (child >= count) break;
        Pair c = pairs[child];
        if (child + 1 < count) {
            const Pair d = pairs[child + 1];
            if (c.start < d.start || (c.start == d.start && c.at < d.at)) {
                child++;
                c = d;
            }
        }
        if (moving.start > c.start || (moving.start == c.start && moving.at > c.at)) break;
        pairs[root] = c;
        root = child;
    }
    pairs[root] = moving;
}

}  // namespace rts6x
