// Spec: Itanium C++ ABI EH 2.2-2.5 (S3) - the header the runtime keeps in front of each thrown
// object: its type and destructor, the handlers that hold it, whether it was rethrown, the chain of
// exceptions caught and not finished; and what the two phases of a throw decide about it.
#ifndef RTS6X_EXCEPTION_H
#define RTS6X_EXCEPTION_H

namespace rts6x {

class UnwindContext;

class Exception {
public:
    // The header of the object __cxa_allocate_exception handed out, and back.
    static Exception *of(void *object) { return static_cast<Exception *>(object) - 1; }
    void *object() { return this + 1; }

    // ExceptionThrow.cpp: what __cxa_throw and __cxa_rethrow do, and a cleanup's end.
    void raise(UnwindContext &context);
    static void resume(UnwindContext &context);

    // ExceptionCatch.cpp: the handlers.
    void *begin();
    static void end();
    static Exception *caught() { return caught_; }
    static int uncaught() { return uncaught_; }

    const void *type;
    void (*destroy)(void *);
    // Phase one's answer: the frame and descriptor of the handler, and what it receives.
    unsigned barrierFrame, barrierDescriptor;
    void *adjusted;
    void *pointer;
    // The frame of the cleanup being run, which __cxa_end_cleanup goes on from.
    unsigned cleanupFrame;
    Exception *nextCleanup;
    Exception *nextCaught;
    int handlers;
    bool rethrown;
    // Padding to 48 bytes, so the object after the header is 8-aligned as malloc's own block.
    int reserved_;

    // The exceptions caught and not finished, the latest first; those being unwound, likewise.
    static Exception *caught_;
    static Exception *cleaning_;
    static int uncaught_;

private:
    // Phase one: the handler for this exception from (pc, context), or false.
    bool search(unsigned pc, const UnwindContext &context);
    // Phase two from (pc, context).
    void unwind(unsigned pc, UnwindContext &context);
    // Whether a handler of catchType takes this exception, and what it then receives.
    bool matches(unsigned catchType);
};

static_assert(sizeof(Exception) % 8 == 0, "the header keeps the thrown object 8-aligned");

}  // namespace rts6x

#endif
