// exceptions.cpp - C++ exceptions on x64 Windows (docs\Runtime_Design.md 8.4), over the shapes cpp11 and cl emit:
// the FuncInfo of each function with a try or an object to destroy (magic 0x19930522; the unwind map, the try map
// and its handlers, the ip-to-state map, the unwind-help slot), and the ThrowInfo of each type thrown.
//
// Spec: the Microsoft C++ ABI's __CxxFrameHandler3, as vcruntime's frame.cpp behaves (read, not copied):
// - _CxxThrowException raises 0xE06D7363 with the object, its ThrowInfo and the image base; with neither, a rethrow
//   of the exception whose catch is running.
// - The handler, on the search pass: the state of the faulting place (the parent's state when it is inside a catch
//   funclet, which the funclet's frame records in the parent's unwind-help slot so that a rethrow is not caught by
//   the same try), then the try blocks holding it - only those inside the running catch, when there is one - and
//   their handlers against the thrown object's catchable types (the same descriptor or name, const, volatile and
//   reference rules, by reference only where the type says so). The catch's object is built (a reference adjusted,
//   a simple type copied, a class copy-constructed), and the stack unwound with frame consolidation to the frame
//   that catches, whose callback calls the catch funclet and continues where it answers.
// - On the unwind pass: each frame's objects destroyed state by state down to its try's (the frame that catches)
//   or to the state outside the catch it is in; a destructor that throws while unwinding calls terminate.
// - After a catch: the object destroyed unless rethrown, still held by an outer catch, or an exception_ptr's; the
//   frame's state reset.
// - exception_ptr, as cl's layout and libcpmt's __ExceptionPtr functions have it (cpp11's <exception>): its first word
//   is a block holding a copy of the exception object, made by its exact type's copy constructor, with a count of the
//   exception_ptrs and rethrows using it; rethrow_exception raises that object again, and the catch that ends it
//   leaves it to the block.
// - noexcept: an exception leaving a function so marked, not from inside one of its catches, calls terminate.
// - An exception nothing catches reaches the unhandled-exception filter set at start, which calls terminate:
//   abort, the fast fail 0xC0000409, as a Release program on the Microsoft runtime ends.
// Per-thread state (the exception being handled, the catches running, the handlers) is __grt_thread's.
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "grt.h"

namespace {

const unsigned CxxCode = 0xE06D7363u, Magic1 = 0x19930520u, Magic2 = 0x19930521u, Magic3 = 0x19930522u,
               PureMagic = 0x01994000u, Consolidate = 0x80000029u, LongJump = 0x80000026u;
const unsigned NonContinuable = 1, UnwindFlags = 0x66, TargetUnwind = 0x20;
const int ContinueSearch = 1;
const unsigned EHs = 1, NoExcept = 4;
const unsigned HtConst = 1, HtVolatile = 2, HtUnaligned = 4, HtReference = 8, HtStdDotDot = 0x40,
               HtBadAllocCompat = 0x80, HtComplus = 0x80000000u;
const unsigned CtSimple = 1, CtByReference = 2, CtVirtualBase = 4, CtStdBadAlloc = 0x10;
const unsigned TiConst = 1, TiVolatile = 2, TiUnaligned = 4, TiPure = 8;

struct Record {
    unsigned code, flags;
    Record *next;
    void *address;
    unsigned count;
    unsigned long long info[15];
};
struct RuntimeFunction { unsigned begin, end, unwind; };
struct Dispatcher {
    unsigned long long controlPc, imageBase;
    const RuntimeFunction *function;
    unsigned long long establisherFrame, targetIp;
    void *context, *languageHandler, *handlerData, *historyTable;
    unsigned scopeIndex, fill;
};
struct FuncInfo {
    unsigned magic;
    int maxState, unwindMap;
    unsigned tryBlocks;
    int tryMap;
    unsigned ipEntries;
    int ipMap, unwindHelp, esTypeList;
    unsigned flags;
};
struct UnwindEntry { int toState, action; };
struct TryBlock { int tryLow, tryHigh, catchHigh, catches, handlers; };
struct Handler { unsigned adjectives; int type, catchObject, handler, frame; };
struct IpState { int ip, state; };
struct ThrowInfo { unsigned attributes; int unwind, forwardCompat, catchables; };
struct CatchableArray { int count; int types[1]; };
struct Pmd { int mdisp, pdisp, vdisp; };
struct CatchableType { unsigned properties; int type; Pmd thisDisp; int size, copy; };
struct TypeDescriptor { const void *vftable; void *spare; char name[1]; };

// a catch whose funclet is running: the object it holds, for the test whether an object is still caught
struct CatchFrame { void *object; CatchFrame *next; };
// what __grt_call_catch's guard and the callback share
struct CatchGuard {
    Record *exception;
    void *context, *savedException, *savedContext;
    CatchFrame frame;
    int rethrow, finished;
};

typedef unsigned long long Frame;

bool isCxx(const Record *r)
{
    return r && r->code == CxxCode && r->count == 4 &&
           (r->info[0] == Magic1 || r->info[0] == Magic2 || r->info[0] == Magic3 || r->info[0] == PureMagic);
}
void *objectOf(const Record *r) { return reinterpret_cast<void *>(r->info[1]); }
const ThrowInfo *throwOf(const Record *r) { return reinterpret_cast<const ThrowInfo *>(r->info[2]); }
const char *throwImage(const Record *r) { return reinterpret_cast<const char *>(r->info[3]); }

const TryBlock *tryAt(const FuncInfo *fi, const Dispatcher *dc, unsigned i)
{
    return reinterpret_cast<const TryBlock *>(dc->imageBase + fi->tryMap) + i;
}

int &unwindState(Frame f, const FuncInfo *fi) { return *reinterpret_cast<int *>(f + fi->unwindHelp); }
int &unwindTry(Frame f, const FuncInfo *fi) { return *reinterpret_cast<int *>(f + fi->unwindHelp + 4); }

int stateFromIp(const FuncInfo *fi, const Dispatcher *dc, unsigned long long ip)
{
    const IpState *map = reinterpret_cast<const IpState *>(dc->imageBase + fi->ipMap);
    unsigned i;
    for (i = 0; i < fi->ipEntries; i++)
        if (ip < dc->imageBase + static_cast<unsigned>(map[i].ip)) break;
    return i == 0 ? -1 : map[i - 1].state;
}

int stateFromPc(const FuncInfo *fi, const Dispatcher *dc) { return stateFromIp(fi, dc, dc->controlPc); }

// the frame of the function itself: for a catch funclet, the parent's frame, which the funclet keeps at the
// handler's frame offset in its own
Frame establisherOf(Frame f, const Dispatcher *dc, const FuncInfo *fi)
{
    int state = stateFromPc(fi, dc);
    for (unsigned index = fi->tryBlocks; index > 0; index--) {
        const TryBlock *t = tryAt(fi, dc, index - 1);
        if (state > t->tryHigh && state <= t->catchHigh) {
            unsigned long long base = 0;
            const RuntimeFunction *rf = static_cast<const RuntimeFunction *>(__grt_function_of(dc->controlPc, &base));
            unsigned begin = rf ? rf->begin : 0;
            const Handler *h = reinterpret_cast<const Handler *>(base + t->handlers);
            for (int i = 0; i < t->catches; i++)
                if (static_cast<unsigned>(h[i].handler) == begin) return *reinterpret_cast<Frame *>(f + h[i].frame);
        }
    }
    return f;
}

// the try block whose catch holds state, or a null pointer
const TryBlock *catchTryBlock(const FuncInfo *fi, const Dispatcher *dc, int state)
{
    for (unsigned index = fi->tryBlocks; index > 0; index--) {
        const TryBlock *t = tryAt(fi, dc, index - 1);
        if (state > t->tryHigh && state <= t->catchHigh) return t;
    }
    return 0;
}

int currentState(Frame f, const Dispatcher *dc, const FuncInfo *fi)
{
    int s = unwindState(f, fi);
    return s == -2 ? stateFromPc(fi, dc) : s;
}

// the state to search from: the place's own, or a deeper one a catch funclet of this frame recorded
int searchState(Frame f, const Dispatcher *dc, const FuncInfo *fi)
{
    int state = stateFromPc(fi, dc);
    Frame parent = establisherOf(f, dc, fi);
    if (state > unwindTry(parent, fi)) {
        unwindState(parent, fi) = state;
        unwindTry(parent, fi) = state;
        return state;
    }
    return unwindTry(parent, fi);
}

}  // namespace

extern "C" {
void *__grt_call_catch(void *funclet, Frame parent, CatchGuard *guard);
void __grt_call_or_terminate(void *function, void *a, void *b);
void __grt_call_or_abort(void *function, void *a, void *b, void *c);
void *__grt_catch_callback(Record *unwind);
void terminate();
}

namespace {

void unwindToState(Frame f, const Dispatcher *dc, const FuncInfo *fi, int target)
{
    struct __grt_thread *t = __grt_thread();
    const UnwindEntry *map = reinterpret_cast<const UnwindEntry *>(dc->imageBase + fi->unwindMap);
    int state = currentState(f, dc, fi);
    t->throwing++;
    while (state != -1 && state > target) {
        int next = map[state].toState;
        if (map[state].action) {
            unwindState(f, fi) = next;
            __grt_call_or_terminate(reinterpret_cast<void *>(dc->imageBase + map[state].action), 0,
                                    reinterpret_cast<void *>(f));
        }
        state = next;
    }
    if (t->throwing > 0) t->throwing--;
    unwindState(f, fi) = state;
}

void unwindToEmptyState(Frame f, const Dispatcher *dc, const FuncInfo *fi)
{
    Frame parent = establisherOf(f, dc, fi);
    const TryBlock *t = catchTryBlock(fi, dc, stateFromPc(fi, dc));
    unwindToState(parent, dc, fi, t ? t->tryHigh : -1);
}

void *adjust(void *object, const Pmd &pmd)
{
    char *p = static_cast<char *>(object) + pmd.mdisp;
    if (pmd.pdisp >= 0) {
        const char *vbtable = *reinterpret_cast<char **>(static_cast<char *>(object) + pmd.pdisp);
        p += *reinterpret_cast<const int *>(vbtable + pmd.vdisp);
        p += pmd.pdisp;
    }
    return p;
}

const TypeDescriptor *handlerType(const Handler *h, unsigned long long image)
{
    return h->type ? reinterpret_cast<const TypeDescriptor *>(image + h->type) : 0;
}

bool ellipsis(const Handler *h, unsigned long long image)
{
    const TypeDescriptor *d = handlerType(h, image);
    return !d || !d->name[0];
}

bool typeMatch(const Handler *h, unsigned long long image, const CatchableType *c, const ThrowInfo *ti, const char *thrown)
{
    if (ellipsis(h, image)) return true;
    if ((h->adjectives & HtBadAllocCompat) && (c->properties & CtStdBadAlloc)) return true;
    const TypeDescriptor *a = handlerType(h, image);
    const TypeDescriptor *b = reinterpret_cast<const TypeDescriptor *>(thrown + c->type);
    if (a != b && strcmp(a->name, b->name) != 0) return false;
    return (!(c->properties & CtByReference) || (h->adjectives & HtReference)) &&
           (!(ti->attributes & TiConst) || (h->adjectives & HtConst)) &&
           (!(ti->attributes & TiUnaligned) || (h->adjectives & HtUnaligned)) &&
           (!(ti->attributes & TiVolatile) || (h->adjectives & HtVolatile));
}

void buildCatchObject(const Record *r, Frame parent, const Handler *h, unsigned long long image, const CatchableType *c)
{
    if (ellipsis(h, image) || (!h->catchObject && !(h->adjectives & HtComplus))) return;
    void **buffer = reinterpret_cast<void **>(parent + h->catchObject);
    void *object = objectOf(r);
    const char *thrown = throwImage(r);
    if (h->adjectives & HtReference) {
        *buffer = adjust(object, c->thisDisp);
    } else if (c->properties & CtSimple) {
        memmove(buffer, object, static_cast<size_t>(c->size));
        if (c->size == 8 && *buffer) *buffer = adjust(*buffer, c->thisDisp);
    } else if (!c->copy) {
        memmove(buffer, adjust(object, c->thisDisp), static_cast<size_t>(c->size));
    } else {
        void *copy = const_cast<char *>(thrown + c->copy);
        __grt_call_or_abort(copy, buffer, adjust(object, c->thisDisp), (c->properties & CtVirtualBase) ? reinterpret_cast<void *>(1) : 0);
    }
}

bool toBeDestroyed(void *object)
{
    for (CatchFrame *f = static_cast<CatchFrame *>(__grt_thread()->catches); f; f = f->next)
        if (f->object == object) return false;
    return true;
}

void destroyObject(Record *r)
{
    if (!isCxx(r) || !throwOf(r) || !throwOf(r)->unwind) return;
    __grt_call_or_terminate(const_cast<char *>(throwImage(r) + throwOf(r)->unwind), objectOf(r), 0);
}

void catchIt(Record *r, Frame f, void *context, Dispatcher *dc, const FuncInfo *fi, const Handler *h,
             const CatchableType *c, const TryBlock *t)
{
    Frame parent = establisherOf(f, dc, fi);
    if (c) buildCatchObject(r, parent, h, dc->imageBase, c);
    Record u;
    memset(&u, 0, sizeof u);
    u.code = Consolidate;
    u.flags = NonContinuable;
    u.count = 15;
    u.info[0] = reinterpret_cast<unsigned long long>(&__grt_catch_callback);
    u.info[1] = reinterpret_cast<unsigned long long>(&parent);
    u.info[2] = dc->imageBase + static_cast<unsigned>(h->handler);
    u.info[3] = static_cast<unsigned long long>(static_cast<long long>(t->tryLow));
    u.info[4] = reinterpret_cast<unsigned long long>(context);
    u.info[5] = reinterpret_cast<unsigned long long>(fi);
    u.info[6] = reinterpret_cast<unsigned long long>(r);
    u.info[8] = Magic1;
    __grt_unwind(reinterpret_cast<void *>(f), reinterpret_cast<void *>(dc->controlPc), &u, 0, dc->context, dc->historyTable);
}

// the try blocks to look in: those holding the state, only inside the running catch when there is one; [start, end)
void tryRange(const FuncInfo *fi, const Dispatcher *dc, int state, unsigned &start, unsigned &end)
{
    int ip = stateFromPc(fi, dc);
    const TryBlock *inCatch = catchTryBlock(fi, dc, ip);
    int first = -1, last = -1;
    for (unsigned i = 0; i < fi->tryBlocks; i++) {
        const TryBlock *t = tryAt(fi, dc, i);
        if (inCatch && (t->tryLow <= inCatch->tryHigh || t->tryHigh > inCatch->catchHigh)) continue;
        if (state >= t->tryLow && state <= t->tryHigh) {
            if (first == -1) first = static_cast<int>(i);
            last = static_cast<int>(i);
        }
    }
    if (first == -1) { start = end = 0; return; }
    start = static_cast<unsigned>(first);
    end = static_cast<unsigned>(last) + 1;
}

void findHandler(Record *r, Frame f, void *context, Dispatcher *dc, const FuncInfo *fi)
{
    struct __grt_thread *thread = __grt_thread();
    int state = searchState(f, dc, fi);
    if (isCxx(r) && !throwOf(r)) {
        if (!thread->exception) return;
        r = static_cast<Record *>(thread->exception);
        context = thread->exceptionContext;
    }
    unsigned start, end;
    if (isCxx(r)) {
        if (fi->tryBlocks > 0) {
            tryRange(fi, dc, state, start, end);
            const CatchableArray *list = reinterpret_cast<const CatchableArray *>(throwImage(r) + throwOf(r)->catchables);
            for (unsigned i = start; i < end; i++) {
                const TryBlock *t = tryAt(fi, dc, i);
                if (t->tryLow > state || state > t->tryHigh) continue;
                const Handler *handlers = reinterpret_cast<const Handler *>(dc->imageBase + t->handlers);
                bool caught = false;
                for (int k = 0; k < t->catches && !caught; k++)
                    for (int j = 0; j < list->count; j++) {
                        const CatchableType *c = reinterpret_cast<const CatchableType *>(throwImage(r) + list->types[j]);
                        if (!typeMatch(&handlers[k], dc->imageBase, c, throwOf(r), throwImage(r))) continue;
                        catchIt(r, f, context, dc, fi, &handlers[k], c, t);
                        caught = true;
                        break;
                    }
            }
        }
        if (fi->magic >= Magic2 && (fi->flags & NoExcept) && !catchTryBlock(fi, dc, stateFromPc(fi, dc))) {
            thread->exception = r;
            thread->exceptionContext = context;
            terminate();
        }
    } else if (fi->tryBlocks > 0) {
        // a structured exception, in a function not built for synchronous exceptions only: catch (...) takes it
        tryRange(fi, dc, state, start, end);
        for (unsigned i = start; i < end; i++) {
            const TryBlock *t = tryAt(fi, dc, i);
            if (t->tryLow > state || state > t->tryHigh) continue;
            const Handler *handlers = reinterpret_cast<const Handler *>(dc->imageBase + t->handlers);
            for (int k = 0; k < t->catches; k++)
                if (ellipsis(&handlers[k], dc->imageBase) && !(handlers[k].adjectives & HtStdDotDot)) {
                    catchIt(r, f, context, dc, fi, &handlers[k], 0, t);
                    break;
                }
        }
    }
}

// an exception_ptr's block: the object follows it, at heldOffset
struct Held {
    Held *next;
    long refs;
    unsigned long long magic;
    const ThrowInfo *info;
    const char *image;
};
const size_t heldOffset = 48;
void *heldLock;     // an SRWLOCK, all zero to start: the list and the counts
Held *heldList;

void *objectIn(Held *h) { return reinterpret_cast<char *>(h) + heldOffset; }

void lockHeld() { if (__grt_threads) __grt_lock(&heldLock); }
void unlockHeld() { if (__grt_threads) __grt_unlock(&heldLock); }

void retain(Held *h)
{
    lockHeld();
    h->refs++;
    unlockHeld();
}

void release(Held *h)
{
    if (!h) return;
    lockHeld();
    bool last = --h->refs == 0;
    if (last) {
        Held **p = &heldList;
        while (*p != h) p = &(*p)->next;
        *p = h->next;
    }
    unlockHeld();
    if (!last) return;
    if (h->info && h->info->unwind) __grt_call_or_terminate(const_cast<char *>(h->image + h->info->unwind), objectIn(h), 0);
    free(h);
}

// the block whose object this is, or a null pointer
Held *heldWith(void *object)
{
    lockHeld();
    Held *h = heldList;
    while (h && objectIn(h) != object) h = h->next;
    unlockHeld();
    return h;
}

// the exception whose catch is running, as a block: the one it was rethrown from, counted once more, or a new copy
Held *capture(Record *r)
{
    if (!isCxx(r) || !throwOf(r)) return 0;
    Held *h = heldWith(objectOf(r));
    if (h) {
        retain(h);
        return h;
    }
    const ThrowInfo *ti = throwOf(r);
    const char *image = throwImage(r);
    const CatchableArray *types = reinterpret_cast<const CatchableArray *>(image + ti->catchables);
    const CatchableType *c = reinterpret_cast<const CatchableType *>(image + types->types[0]);
    h = static_cast<Held *>(malloc(heldOffset + static_cast<size_t>(c->size)));
    if (!h) return 0;
    h->refs = 1;
    h->magic = r->info[0];
    h->info = ti;
    h->image = image;
    if ((c->properties & CtSimple) || !c->copy)
        memcpy(objectIn(h), objectOf(r), static_cast<size_t>(c->size));
    else
        __grt_call_or_abort(const_cast<char *>(image + c->copy), objectIn(h), objectOf(r),
                            (c->properties & CtVirtualBase) ? reinterpret_cast<void *>(1) : 0);
    lockHeld();
    h->next = heldList;
    heldList = h;
    unlockHeld();
    return h;
}

void finishCatch(CatchGuard *g)
{
    if (g->finished) return;
    g->finished = 1;
    struct __grt_thread *t = __grt_thread();
    CatchFrame **p = reinterpret_cast<CatchFrame **>(&t->catches);
    while (*p && *p != &g->frame) p = &(*p)->next;
    if (*p) *p = g->frame.next;
    if (!g->rethrow && isCxx(g->exception)) {
        // an exception_ptr's object stays its block's; the count rethrow_exception took is given back
        Held *h = heldWith(objectOf(g->exception));
        if (h) release(h);
        else if (toBeDestroyed(objectOf(g->exception))) destroyObject(g->exception);
    }
    t->exception = g->savedException;
    t->exceptionContext = g->savedContext;
}

void *previousFilter;

}  // namespace

extern "C" int __CxxFrameHandler3(Record *r, Frame frame, void *context, Dispatcher *dc)
{
    const FuncInfo *fi = reinterpret_cast<const FuncInfo *>(dc->imageBase + *static_cast<unsigned *>(dc->handlerData));
    bool consolidating = r->code == Consolidate && r->count == 15 && r->info[8] == Magic1;
    if (r->code != CxxCode && !consolidating && r->code != LongJump && fi->magic >= Magic3 && (fi->flags & EHs))
        return ContinueSearch;
    if (r->flags & UnwindFlags) {
        if (fi->maxState != 0) {
            if ((r->flags & TargetUnwind) && r->code == LongJump) {
                unwindToState(frame, dc, fi, stateFromIp(fi, dc, dc->targetIp));
            } else if ((r->flags & TargetUnwind) && r->code == Consolidate) {
                unwindToState(*reinterpret_cast<Frame *>(r->info[1]), dc, fi, static_cast<int>(r->info[3]));
            } else {
                unwindToEmptyState(frame, dc, fi);
            }
        }
        return ContinueSearch;
    }
    if (fi->tryBlocks != 0 || (fi->magic >= Magic2 && fi->esTypeList) || (fi->magic >= Magic3 && (fi->flags & NoExcept)))
        findHandler(r, frame, context, dc, fi);
    return ContinueSearch;
}

extern "C" void *__grt_catch_callback(Record *unwind)
{
    struct __grt_thread *t = __grt_thread();
    CatchGuard g;
    g.exception = reinterpret_cast<Record *>(unwind->info[6]);
    g.context = reinterpret_cast<void *>(unwind->info[4]);
    g.savedException = t->exception;
    g.savedContext = t->exceptionContext;
    t->exception = g.exception;
    t->exceptionContext = g.context;
    CatchFrame *chain = static_cast<CatchFrame *>(t->catches);
    g.frame.object = objectOf(g.exception);
    g.frame.next = &g.frame < chain ? chain : 0;
    t->catches = &g.frame;
    g.rethrow = 0;
    g.finished = 0;
    Frame parent = *reinterpret_cast<Frame *>(unwind->info[1]);
    const FuncInfo *fi = reinterpret_cast<const FuncInfo *>(unwind->info[5]);
    void *next = __grt_call_catch(reinterpret_cast<void *>(unwind->info[2]), parent, &g);
    finishCatch(&g);
    *reinterpret_cast<long long *>(parent + fi->unwindHelp) = -2;
    return next;
}

// __grt_call_catch's handler: a rethrow out of the catch unwinds to the helper, which raises the caught exception
// again; any other way out of the catch ends it (vcruntime's __except with ExFilterRethrow, and its __finally)
extern "C" int __grt_catch_guard(Record *r, Frame frame, void *context, Dispatcher *dc)
{
    (void)context;
    CatchGuard *g = *reinterpret_cast<CatchGuard **>(frame + 32);
    if (r->flags & UnwindFlags) {
        if (!(r->flags & TargetUnwind)) finishCatch(g);
        return ContinueSearch;
    }
    g->rethrow = 0;
    if (isCxx(r) && objectOf(r) == objectOf(g->exception)) g->rethrow = 1;
    if (isCxx(r) && !throwOf(r)) {
        g->rethrow = 1;
        __grt_unwind(reinterpret_cast<void *>(frame),
                     reinterpret_cast<void *>(dc->imageBase + *static_cast<unsigned *>(dc->handlerData)), r, 0,
                     dc->context, dc->historyTable);
    }
    return ContinueSearch;
}

extern "C" void __grt_rethrow_caught(CatchGuard *g)
{
    Record *r = g->exception;
    __grt_raise(r->code, r->flags, r->count, r->info);
}

extern "C" int __grt_terminate_guard(Record *r, Frame frame, void *context, Dispatcher *dc)
{
    (void)frame;
    (void)dc;
    if (!(r->flags & UnwindFlags) && isCxx(r)) {
        struct __grt_thread *t = __grt_thread();
        t->exception = r;
        t->exceptionContext = context;
        terminate();
    }
    return ContinueSearch;
}

extern "C" int __grt_abort_guard(Record *r, Frame frame, void *context, Dispatcher *dc)
{
    (void)context;
    if (!(r->flags & UnwindFlags))
        __grt_unwind(reinterpret_cast<void *>(frame),
                     reinterpret_cast<void *>(dc->imageBase + *static_cast<unsigned *>(dc->handlerData)), r, 0,
                     dc->context, dc->historyTable);
    return ContinueSearch;
}

extern "C" void _CxxThrowException(void *object, const ThrowInfo *info)
{
    unsigned long long arguments[4];
    void *image = info ? __grt_image_of(info) : 0;
    arguments[0] = info && ((info->attributes & TiPure) || !image) ? PureMagic : Magic1;
    arguments[1] = reinterpret_cast<unsigned long long>(object);
    arguments[2] = reinterpret_cast<unsigned long long>(info);
    arguments[3] = reinterpret_cast<unsigned long long>(image);
    __grt_raise(CxxCode, NonContinuable, 4, arguments);
}

typedef void (*Handler0)();

extern "C" void terminate()
{
    struct __grt_thread *t = __grt_thread();
    if (t->terminateHandler) __grt_call_or_abort(reinterpret_cast<void *>(t->terminateHandler), 0, 0, 0);
    abort();
}

extern "C" void __std_terminate() { terminate(); }

// the handlers a thread starts with, as the Microsoft runtime answers them: terminate's ends the program by abort,
// and unexpected's is terminate; a thread's slot holds a null pointer until one is set
namespace {
void defaultTerminate() { abort(); }
}

extern "C" Handler0 set_terminate(Handler0 f)
{
    struct __grt_thread *t = __grt_thread();
    Handler0 old = t->terminateHandler;
    t->terminateHandler = f;
    return old ? old : defaultTerminate;
}

extern "C" Handler0 _get_terminate()
{
    Handler0 h = __grt_thread()->terminateHandler;
    return h ? h : defaultTerminate;
}

extern "C" void unexpected()
{
    Handler0 h = __grt_thread()->unexpectedHandler;
    if (h) h();
    terminate();
}

extern "C" Handler0 set_unexpected(Handler0 f)
{
    struct __grt_thread *t = __grt_thread();
    Handler0 old = t->unexpectedHandler;
    t->unexpectedHandler = f;
    return old ? old : terminate;
}

extern "C" Handler0 _get_unexpected()
{
    Handler0 h = __grt_thread()->unexpectedHandler;
    return h ? h : terminate;
}

extern "C" bool __uncaught_exception() { return __grt_thread()->throwing != 0; }
extern "C" int __uncaught_exceptions() { return __grt_thread()->throwing; }

namespace std {
bool uncaught_exception() throw() { return __grt_thread()->throwing != 0; }
}

namespace {
struct PointerData { Held *held; void *unused; };
PointerData *dataOf(void *p) { return static_cast<PointerData *>(p); }
const PointerData *dataOf(const void *p) { return static_cast<const PointerData *>(p); }
}

void __ExceptionPtrCreate(void *p)
{
    dataOf(p)->held = 0;
    dataOf(p)->unused = 0;
}

void __ExceptionPtrDestroy(void *p)
{
    release(dataOf(p)->held);
}

void __ExceptionPtrCopy(void *p, const void *from)
{
    Held *h = dataOf(from)->held;
    if (h) retain(h);
    dataOf(p)->held = h;
    dataOf(p)->unused = 0;
}

void __ExceptionPtrAssign(void *p, const void *from)
{
    Held *h = dataOf(from)->held, *old = dataOf(p)->held;
    if (h == old) return;
    if (h) retain(h);
    dataOf(p)->held = h;
    release(old);
}

bool __ExceptionPtrCompare(const void *a, const void *b)
{
    return dataOf(a)->held == dataOf(b)->held;
}

bool __ExceptionPtrToBool(const void *p)
{
    return dataOf(p)->held != 0;
}

void __ExceptionPtrSwap(void *a, void *b)
{
    Held *h = dataOf(a)->held;
    dataOf(a)->held = dataOf(b)->held;
    dataOf(b)->held = h;
}

void __ExceptionPtrCurrentException(void *p)
{
    Record *r = static_cast<Record *>(__grt_thread()->exception);
    Held *old = dataOf(p)->held;
    dataOf(p)->held = r ? capture(r) : 0;
    dataOf(p)->unused = 0;
    release(old);
}

void __ExceptionPtrRethrow(const void *p)
{
    Held *h = dataOf(p)->held;
    if (!h) terminate();
    retain(h);
    unsigned long long arguments[4];
    arguments[0] = h->magic;
    arguments[1] = reinterpret_cast<unsigned long long>(objectIn(h));
    arguments[2] = reinterpret_cast<unsigned long long>(h->info);
    arguments[3] = reinterpret_cast<unsigned long long>(h->image);
    __grt_raise(CxxCode, NonContinuable, 4, arguments);
}

// the process's unhandled-exception filter: an uncaught C++ exception ends in terminate; anything else goes on to
// the filter before (Windows' own)
extern "C" long __grt_unhandled(void **pointers)
{
    Record *r = static_cast<Record *>(pointers[0]);
    if (isCxx(r)) {
        struct __grt_thread *t = __grt_thread();
        t->exception = r;
        t->exceptionContext = pointers[1];
        terminate();
    }
    if (previousFilter) return reinterpret_cast<long (*)(void **)>(previousFilter)(pointers);
    return 0;
}

extern "C" int __grt_eh_start()
{
    previousFilter = __grt_set_unhandled_filter(reinterpret_cast<void *>(&__grt_unhandled));
    return 0;
}
