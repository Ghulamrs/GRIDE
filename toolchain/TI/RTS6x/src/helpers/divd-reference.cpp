// Spec: SPRAB89B 8.2 Table 8-4 - __c6xabi_divd, the C++ reference of divd.s: every operand through
// FloatArithmetic::divideBinary64, the exact path divd.s keeps for what is not normal. Built only
// with RTS6X_REFERENCE defined (make reference), which leaves divd.s out; otherwise nothing.
#ifdef RTS6X_REFERENCE
#include "SoftFloat.h"

extern "C" double __c6xabi_divd(double x, double y)
{
    return rts6x::FloatArithmetic::divideBinary64(x, y);
}

#endif
