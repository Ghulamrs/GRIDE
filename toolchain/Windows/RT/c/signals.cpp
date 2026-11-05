// Spec: ISO C 7.14 - signal and raise, as the Microsoft runtime's (tests\signals):
//   - the signals are SIGINT 2, SIGILL 4, SIGFPE 8, SIGSEGV 11, SIGTERM 15, SIGBREAK 21 and SIGABRT 22, with 6 a
//     second name for SIGABRT. signal answers 3 and 5 with SIG_ERR and EINVAL; any other number is an invalid
//     parameter, the fast fail of a Release program, for signal and raise alike;
//   - SIGINT, SIGBREAK, SIGTERM and SIGABRT have one handler for the program; SIGFPE, SIGILL and SIGSEGV one for each
//     thread;
//   - a handler is set back before it runs: to SIG_DFL, except that raise leaves SIGILL's and SIGSEGV's at a value
//     that is neither SIG_DFL nor the handler (a third value, which acts as SIG_DFL), as the UCRT's does;
//   - raise with SIG_DFL ends the program with exit code 3 and writes no stream; with SIG_IGN it does nothing.
//     SIGFPE's handler is called with a second argument, _FPE_EXPLICITGEN (0x8C) from raise;
//   - Ctrl+C and Ctrl+Break reach SIGINT's and SIGBREAK's handlers through a console control handler, set when
//     either is first given one (win\control.c); with SIG_DFL, Windows' own end follows;
//   - an access violation, an illegal instruction or a floating-point exception in the program's first thread
//     reaches SIGSEGV's, SIGILL's or SIGFPE's handler through main's language handler (start\entryguard.asm), and the
//     program goes on where it stopped (__grt_signal_exception); an integer division by zero is not a signal.
// abort reaches SIGABRT's handler through RTS6x's SignalTable::deliver, defined here: it runs a handler, if there is
// one, and returns, abort going on to its end.
#include <errno.h>
#include <signal.h>
#include "misc/SignalTable.h"
#include "grt.h"

namespace {

typedef void (*Handler)(int);

const int SIGBREAK_ = 21, SIGABRT_COMPAT_ = 6;

// raise's mark on SIGILL and SIGSEGV once it has run their handler
void setBack(int) {}
const Handler raised = setBack;

Handler interrupt, interruptBreak, terminate_, abort_;
thread_local Handler floatingPoint, illegal, segment;
void *tableLock;        // an SRWLOCK, all zero to start
bool consoleHooked;

Handler *slotOf(int sig)
{
    switch (sig) {
    case SIGINT: return &interrupt;
    case SIGILL: return &illegal;
    case SIGFPE: return &floatingPoint;
    case SIGSEGV: return &segment;
    case SIGTERM: return &terminate_;
    case SIGBREAK_: return &interruptBreak;
    case SIGABRT:
    case SIGABRT_COMPAT_: return &abort_;
    }
    return 0;
}

// the handler that is to run, set back first; SIG_DFL or SIG_IGN when none is to run
Handler take(int sig, Handler back)
{
    bool held = __grt_threads != 0;
    if (held) __grt_lock(&tableLock);
    Handler *slot = slotOf(sig), h = *slot;
    if (h == raised) h = SIG_DFL;
    if (h != SIG_DFL && h != SIG_IGN) *slot = back;
    if (held) __grt_unlock(&tableLock);
    return h;
}

void run(Handler h, int sig, int code)
{
    if (sig == SIGFPE) reinterpret_cast<void (*)(int, int)>(h)(sig, code);
    else h(sig);
}

// Ctrl+C (0) and Ctrl+Break (1): TRUE when the program's handler took the event
int controlEvent(unsigned long type)
{
    int sig = type == 0 ? SIGINT : type == 1 ? SIGBREAK_ : 0;
    if (!sig) return 0;
    Handler h = take(sig, SIG_DFL);
    if (h == SIG_DFL) return 0;
    if (h != SIG_IGN) h(sig);
    return 1;
}

}  // namespace

extern "C" Handler signal(int sig, Handler handler)
{
    if (sig == 3 || sig == 5) {
        errno = EINVAL;
        return SIG_ERR;
    }
    if (!slotOf(sig)) __grt_fail_fast();
    bool held = __grt_threads != 0;
    if (held) __grt_lock(&tableLock);
    if ((sig == SIGINT || sig == SIGBREAK_) && !consoleHooked) {
        __grt_on_console_control(controlEvent);
        consoleHooked = true;
    }
    Handler *slot = slotOf(sig), old = *slot;
    *slot = handler;
    if (held) __grt_unlock(&tableLock);
    return old;
}

extern "C" int raise(int sig)
{
    if (!slotOf(sig)) __grt_fail_fast();
    Handler h = take(sig, sig == SIGILL || sig == SIGSEGV ? raised : SIG_DFL);
    if (h == SIG_IGN) return 0;
    if (h == SIG_DFL) __rts6x_halt(3);
    run(h, sig, 0x8C);
    return 0;
}

extern "C" int __grt_signal_exception(unsigned long code)
{
    int sig, fpe = 0;
    switch (code) {
    case 0xC0000005ul: sig = SIGSEGV; break;
    case 0xC000001Dul:
    case 0xC0000096ul: sig = SIGILL; break;
    case 0xC000008Dul: sig = SIGFPE; fpe = 0x82; break;    // denormal operand
    case 0xC000008Eul: sig = SIGFPE; fpe = 0x83; break;    // division by zero
    case 0xC000008Ful: sig = SIGFPE; fpe = 0x86; break;    // inexact result
    case 0xC0000090ul: sig = SIGFPE; fpe = 0x81; break;    // invalid operation
    case 0xC0000091ul: sig = SIGFPE; fpe = 0x84; break;    // overflow
    case 0xC0000092ul: sig = SIGFPE; fpe = 0x8A; break;    // stack check
    case 0xC0000093ul: sig = SIGFPE; fpe = 0x85; break;    // underflow
    case 0xC00002B4ul: sig = SIGFPE; fpe = 0x8E; break;    // multiple faults
    case 0xC00002B5ul: sig = SIGFPE; fpe = 0x8D; break;    // multiple traps
    default: return 0;
    }
    Handler h = take(sig, SIG_DFL);
    if (h == SIG_DFL) return 0;
    if (h != SIG_IGN) run(h, sig, fpe);
    return 1;
}

namespace rts6x {

SignalTable::Handler SignalTable::install(int sig, Handler handler)
{
    return signal(sig, handler);
}

int SignalTable::deliver(int sig)
{
    if (!slotOf(sig)) return -1;
    Handler h = take(sig, SIG_DFL);
    if (h != SIG_DFL && h != SIG_IGN) h(sig);
    return 0;
}

}  // namespace rts6x
