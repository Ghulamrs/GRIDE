// Spec: SPRAB89B 8.2 Table 8-4 - __c6xabi_divf, the C++ reference of divf.s: every operand through
// FloatArithmetic::divideBinary32, the exact path divf.s keeps for what is not normal. Built only
// with RTS6X_REFERENCE defined (make reference), which leaves divf.s out; otherwise nothing.
#ifdef RTS6X_REFERENCE
#include "SoftFloat.h"

extern "C" float __c6xabi_divf(float x, float y)
{
    return rts6x::FloatArithmetic::divideBinary32(x, y);
}

#endif
