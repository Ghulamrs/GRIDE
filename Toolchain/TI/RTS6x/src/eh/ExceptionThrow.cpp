// Spec: ARM EHABI 7-8 with SPRAB89B 11 (S4/S1) - a throw in two phases. Phase one walks the frames
// without changing them for the first catch descriptor whose range holds the return address and
// whose type takes the exception, or a specification that allows nothing; none means terminate.

#include "Exception.h"
#include "Handlers.h"
#include "UnwindContext.h"
#include "UnwindTable.h"

extern "C" void __cxa_call_unexpected(void *exception);

namespace rts6x {

Exception *Exception::caught_;
Exception *Exception::cleaning_;
int Exception::uncaught_;

bool Exception::search(unsigned pc, const UnwindContext &context)
{
    UnwindContext walk = context;
    unsigned p = pc;
    for (;;) {
        UnwindEntry entry;
        if (!UnwindEntry::find(p, entry)) return false;
        ScopeDescriptor d;
        for (unsigned at = entry.descriptors(); d.read(at, entry); at = d.next()) {
            if (!d.holds(p)) continue;
            bool barrier = false;
            if (d.kind() == ScopeDescriptor::Specification) {
                // Only a row allowing nothing - noexcept, throw() - exists: cpp11 refuses throw(T) (ANALYSIS 4).
                barrier = (d.allowed() & 0x7fffffffu) == 0;
            } else if (d.kind() == ScopeDescriptor::Catch) {
                if (d.type() == ScopeDescriptor::Terminate) Handlers::terminate();
                adjusted = object();
                barrier = d.type() == ScopeDescriptor::CatchAll || matches(d.type());
            }
            if (barrier) {
                barrierFrame = walk.frame();
                barrierDescriptor = at;
                return true;
            }
        }
        if (!entry.unwind(walk, p)) return false;
    }
}

// Phase two: the cleanups between the throw and the handler, each landed on in turn, then the handler's
// pad. A pad ends in __cxa_end_cleanup, which resumes from its own call: a pad lies in the regions round it.
void Exception::unwind(unsigned pc, UnwindContext &context)
{
    unsigned p = pc;
    for (;;) {
        UnwindEntry entry;
        if (!UnwindEntry::find(p, entry)) Handlers::terminate();
        ScopeDescriptor d;
        for (unsigned at = entry.descriptors(); d.read(at, entry); at = d.next()) {
            if (d.kind() == ScopeDescriptor::Cleanup && d.holds(p)) {
                cleanupFrame = context.frame();
                nextCleanup = cleaning_;
                cleaning_ = this;
                context.install(d.pad(), this);
            }
            if (context.frame() == barrierFrame && at == barrierDescriptor) {
                if (d.kind() == ScopeDescriptor::Specification) __cxa_call_unexpected(object());
                context.install(d.pad(), this);
            }
        }
        if (context.frame() == barrierFrame || !entry.unwind(context, p)) Handlers::terminate();
    }
}

void Exception::raise(UnwindContext &context)
{
    if (!search(context.pc(), context)) Handlers::terminate();
    unwind(context.pc(), context);
}

void Exception::resume(UnwindContext &context)
{
    Exception *e = cleaning_;
    if (!e) Handlers::terminate();
    cleaning_ = e->nextCleanup;
    context.set(UnwindContext::A15, e->cleanupFrame);
    e->unwind(context.pc(), context);
}

}  // namespace rts6x

extern "C" void __rts6x_throw(void *object, const void *type, void (*destroy)(void *), rts6x::UnwindContext *context)
{
    rts6x::Exception *e = rts6x::Exception::of(object);
    e->type = type;
    e->destroy = destroy;
    e->handlers = 0;
    e->rethrown = false;
    rts6x::Exception::uncaught_++;
    e->raise(*context);
}

extern "C" void __rts6x_rethrow(rts6x::UnwindContext *context)
{
    rts6x::Exception *e = rts6x::Exception::caught_;
    if (!e) rts6x::Handlers::terminate();
    e->rethrown = true;
    rts6x::Exception::uncaught_++;
    e->raise(*context);
}

extern "C" void __rts6x_end_cleanup(rts6x::UnwindContext *context)
{
    rts6x::Exception::resume(*context);
}
