// Spec: SPRAB89B 3.1 and 11.5 - what survives a call and what unwinding restores: A10-A15 (A15 the
// frame pointer), B10-B14 (B14 the data page), B15 the stack pointer, and the return address; the
// virtual register set the unwinder carries from frame to frame and installs at a landing pad.
#ifndef RTS6X_UNWIND_CONTEXT_H
#define RTS6X_UNWIND_CONTEXT_H

namespace rts6x {

class UnwindContext {
public:
    // The words, as context.s stores and installs them: A10-A15, B10-B15, the address to go on at.
    enum Slot { A10, A11, A12, A13, A14, A15, B10, B11, B12, B13, B14, B15, Pc, Count };

    unsigned get(Slot s) const { return words_[s]; }
    void set(Slot s, unsigned v) { words_[s] = v; }
    unsigned pc() const { return words_[Pc]; }
    unsigned frame() const { return words_[A15]; }
    unsigned stack() const { return words_[B15]; }

    // The registers set, then a branch to target with A4 = exception; never returns.
    void install(unsigned target, void *exception) const;

private:
    unsigned words_[Count];
};

}  // namespace rts6x

extern "C" void __rts6x_unwind_install(const rts6x::UnwindContext *context, void *exception, unsigned target);

#endif
