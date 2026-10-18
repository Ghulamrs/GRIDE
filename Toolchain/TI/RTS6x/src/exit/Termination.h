// Spec: ISO C 7.20.4 (atexit: at least 32 functions, called in reverse order of registration by
// exit, then every open stream closed; abort raises SIGABRT and calls none of them) and the Itanium
// C++ ABI 3.3.5 (__cxa_atexit: a destructor and its object, in atexit's order). One class owns the ending.
#ifndef RTS6X_TERMINATION_H
#define RTS6X_TERMINATION_H

extern "C" void __rts6x_halt(int status);

namespace rts6x {

class Termination {
public:
    // A function for exit to call; 0, or non-zero when the list is full.
    static int add(void (*function)(void));
    // A destructor and its object, for exit to call in the same list (C++).
    static int add(void (*destructor)(void *), void *object);
    // Calls the list, last first, then the stream closer, then stops with status - which sim6747 reports.
    static void exit(int status);
    // What closes the open streams at exit (7.20.4.3/4); stdio names it when it first opens one.
    static void closeStreamsWith(void (*closer)(void)) { streams_ = closer; }
    // SIGABRT delivered once, then a stop, the list not called: 134, a host shell's status for an abort.
    static void abort();

private:
    enum { Capacity = 64, AbortStatus = 134 };
    struct Entry {
        void (*plain)(void);
        void (*withObject)(void *);
        void *object;
    };
    static Entry entries_[Capacity];
    static int count_;
    static void (*streams_)(void);
    // Set while SIGABRT is delivered, so an abort from its handler (SIG_DFL included) stops at once.
    static bool aborting_;
};

}  // namespace rts6x

#endif
