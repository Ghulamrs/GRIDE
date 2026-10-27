// Spec: the Microsoft C++ ABI's two exception entry points, here only as far as a program that throws
// nothing needs them:
//   _CxxThrowException(object, throwInfo)    a throw-expression. grt does not unwind yet: the program
//                                            ends, saying which type was thrown and, for a class
//                                            derived from std::exception, its what() - as terminate()
//                                            would for an exception nothing catches - with abort's 3.
//   __CxxFrameHandler3                       the language handler cpp11 names in the unwind data of
//                                            every function with a destructor to run or a try block;
//                                            with nothing thrown it is reached only by a structured
//                                            exception (an access violation) passing through, which
//                                            it leaves to the next frame: ExceptionContinueSearch.
//
// What handling them takes (the next step), all of it in the layout cpp11 already emits:
//   - _CxxThrowException: copy nothing, raise a structured exception - RaiseException with code
//     0xE06D7363 ("msc"), the magic 0x19930520, the object, the ThrowInfo and the image base;
//   - __CxxFrameHandler3: on the search pass, read the function's FuncInfo (version 0x19930522: the
//     unwind map, the try map with its catch handlers, the ip-to-state map), find the state of the
//     faulting ip, and for each try block covering it match the thrown CatchableTypes (by type
//     descriptor, const/volatile/reference rules, the base-class displacement) against each handler;
//     on a match call RtlUnwindEx to the catch's frame, whose unwind pass calls back here to run the
//     destructors of the unwind map from the state down to the try's, then copy-construct (or copy
//     bits) the object into the catch parameter, call the catch funclet, and continue at the address
//     it answers; rethrow (_CxxThrowException(0, 0)) and std::uncaught_exception keep a per-thread
//     record of the exception being handled, and the object is destroyed when its last catch ends;
//   - kernel32: RaiseException and ntdll's RtlUnwindEx, RtlLookupFunctionEntry, RtlVirtualUnwind
//     and RtlPcToFileHeader, added to kernel32.def (a second .def for ntdll.dll) - nothing else.

#include <stdlib.h>
#include <string.h>
#include "grt.h"

extern "C" char *__grt_undecorate(const char *decorated);
extern "C" char __ImageBase;

namespace {

struct ThrowInfo { unsigned attributes; int unwind, forwardCompat, catchables; };
struct Catchables { int count; int types[1]; };
struct Catchable { unsigned properties; int type; int mdisp, pdisp, vdisp; int size; int copy; };
struct TypeDescriptor { const void *vftable; char *undecorated; char name[1]; };

}  // namespace

extern "C" void __grt_cxx_unhandled(const char *type, const char *what)
{
    __grt_say("terminate: a C++ exception of type ");
    __grt_say(type);
    if (what && *what) { __grt_say(" (\""); __grt_say(what); __grt_say("\")"); }
    __grt_say(" was thrown, and GRIDE's runtime does not handle exceptions yet\n");
    abort();
}

extern "C" void _CxxThrowException(void *object, const ThrowInfo *info)
{
    if (!info) __grt_cxx_unhandled("(rethrown)", 0);
    const char *image = &__ImageBase;
    const Catchables *list = reinterpret_cast<const Catchables *>(image + info->catchables);
    const char *type = "(unknown)";
    const char *what = 0;
    for (int i = 0; i < list->count; i++) {
        const Catchable *c = reinterpret_cast<const Catchable *>(image + list->types[i]);
        const TypeDescriptor *t = reinterpret_cast<const TypeDescriptor *>(image + c->type);
        if (i == 0) type = __grt_undecorate(t->name);
        // std::exception among the bases: its what(), the second slot of its vftable
        if (object && strcmp(t->name, ".?AVexception@std@@") == 0 && c->pdisp < 0) {
            char *base = static_cast<char *>(object) + c->mdisp;
            typedef const char *(*What)(void *);
            What f = (*reinterpret_cast<What *const *>(base))[1];
            what = f(base);
        }
    }
    __grt_cxx_unhandled(type, what);
}

extern "C" int __CxxFrameHandler3(void *record, void *frame, void *context, void *dispatch)
{
    (void)record; (void)frame; (void)context; (void)dispatch;
    return 1;   // ExceptionContinueSearch
}
