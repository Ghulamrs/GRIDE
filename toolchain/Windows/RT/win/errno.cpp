// Spec: the Microsoft runtime's errno is *_errno(). Its storage is RTS6x's ErrorNumber (misc\ErrorNumber.h),
// defined here, in the lowest layer, so the Windows layer can set errno without calling up into the C library.
#include "misc/ErrorNumber.h"

int rts6x::ErrorNumber::value_;

extern "C" int *_errno(void)
{
    return rts6x::ErrorNumber::address();
}
