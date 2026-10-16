// Spec: ISO C 7.14 - the standard signals and their handlers, SIG_DFL to start with (7.14.1.1/6).
// Signals are numbered as <signal.h> has them for the C6000.
#ifndef RTS6X_SIGNAL_TABLE_H
#define RTS6X_SIGNAL_TABLE_H

namespace rts6x {

class SignalTable {
public:
    typedef void (*Handler)(int);
    static Handler install(int sig, Handler handler);
    static int deliver(int sig);

private:
    enum { Count = 16 };
    // Zero-filled at start, which is SIG_DFL for every signal.
    static Handler handlers_[Count];
};

}  // namespace rts6x

#endif
