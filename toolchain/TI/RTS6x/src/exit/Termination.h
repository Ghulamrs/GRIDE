// Spec: ISO C 7.20.4 (atexit: at least 32 functions, called in reverse order of registration by
// exit, then every open stream closed; abort raises SIGABRT and calls none of them) and the Itanium
// C++ ABI 3.3.5 (__cxa_atexit: a destructor and its object, in atexit's order). One class owns the ending.
#ifndef RTS6X_TERMINATION_H
#define RTS6X_TERMINATION_H

extern "C" void __rts6x_halt(int status);

namespace rts6x {

class Termination {
public:
    // A function for exit to call; 0, or non-zero when there is no memory for it.
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
#if defined(_WIN32)
    // GRIDE's runtime for Windows (toolchain\Windows\RT): abort ends a program as the Microsoft runtime's
    // Release build does, by the fast fail, whose exit code 0xC0000409 is the status __rts6x_halt is given
    // (win\halt.asm takes it as the sign to fail fast).
    enum { AbortStatus = -1073740791 };
#else
    enum { AbortStatus = 134 };
#endif
    struct Entry {
        void (*plain)(void);
        void (*withObject)(void *);
        void *object;
    };
    // The list grows: a static first chunk, then a chunk from malloc each time the last is full, so every
    // static object's destructor a C++ program registers has a place, however many there are. A full list
    // was once a fixed 64 (4,096 on Windows) and refused the rest without a word, which cpp11's
    // initialisers never check.
    enum { ChunkSize = 64 };
    struct Chunk {
        Entry entries[ChunkSize];
        Chunk *older;
        int count;
    };
    static Entry *slot();
    static Chunk first_;
    static Chunk *last_;
    static void (*streams_)(void);
    // Set while SIGABRT is delivered, so an abort from its handler (SIG_DFL included) stops at once.
    static bool aborting_;
};

}  // namespace rts6x

#endif
