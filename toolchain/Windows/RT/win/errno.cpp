// Spec: the Microsoft runtime's errno is *_errno(), one for each thread (docs\Runtime_Design.md 9.6). It is kept
// here, in the lowest layer, so the Windows layer can set errno without calling up into the C library; RTS6x's
// ErrorNumber (misc\ErrorNumber.h) reaches it through _errno too.
namespace {
thread_local int errnoValue;
}

extern "C" int *_errno(void)
{
    return &errnoValue;
}
