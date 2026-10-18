// Spec: ISO C 7.20.2 - rand's sequence in 0..RAND_MAX, srand restarting it (rand before any srand
// as after srand(1)). RAND_MAX is 32767, C's least and the C6000 ABI's; the generator is C's own
// example (7.20.2.2/5), so a sequence is the same wherever that example is used.
#ifndef RTS6X_RANDOM_H
#define RTS6X_RANDOM_H

namespace rts6x {

class Random {
public:
    enum { Max = 32767 };
    static int next() { state_ = state_ * 1103515245u + 12345u; return (int)((state_ >> 16) & 0x7FFF); }
    static void seed(unsigned s) { state_ = s; }

private:
    static unsigned state_;
};

}  // namespace rts6x

#endif
