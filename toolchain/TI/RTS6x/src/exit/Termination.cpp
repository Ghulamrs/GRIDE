// Spec: ISO C 7.20.4.1-3 and Itanium C++ ABI 3.3.5: one list for atexit and __cxa_atexit,
// emptied from the end, so each function runs after everything registered later than it; the
// streams closed after the list (7.20.4.3/4); abort raising SIGABRT before it stops (7.20.4.1/2).

#include <signal.h>
#include <stdlib.h>
#include "Termination.h"
#include "../misc/SignalTable.h"

namespace rts6x {

Termination::Chunk Termination::first_;
Termination::Chunk *Termination::last_;
void (*Termination::streams_)(void);
bool Termination::aborting_;

// The next free entry, in a new chunk when the last one is full; null when malloc has none.
Termination::Entry *Termination::slot()
{
    if (!last_) last_ = &first_;
    if (last_->count == ChunkSize) {
        Chunk *c = static_cast<Chunk *>(malloc(sizeof(Chunk)));
        if (!c) return 0;
        c->older = last_;
        c->count = 0;
        last_ = c;
    }
    return &last_->entries[last_->count++];
}

int Termination::add(void (*function)(void))
{
    Entry *e = slot();
    if (!e) return -1;
    e->plain = function;
    e->withObject = 0;
    e->object = 0;
    return 0;
}

int Termination::add(void (*destructor)(void *), void *object)
{
    Entry *e = slot();
    if (!e) return -1;
    e->plain = 0;
    e->withObject = destructor;
    e->object = object;
    return 0;
}

void Termination::exit(int status)
{
    // A function the list calls may register another; it is called too, before those below it.
    while (last_) {
        if (last_->count == 0) {
            last_ = last_->older;
            continue;
        }
        Entry e = last_->entries[--last_->count];
        if (e.plain) e.plain();
        else e.withObject(e.object);
    }
    if (streams_) streams_();
    __rts6x_halt(status);
}

void Termination::abort()
{
    if (!aborting_) {
        aborting_ = true;
        SignalTable::deliver(SIGABRT);
    }
    __rts6x_halt(AbortStatus);
}

}  // namespace rts6x
