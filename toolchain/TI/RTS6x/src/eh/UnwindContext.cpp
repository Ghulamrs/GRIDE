// Spec: SPRAB89B 11.5 - landing: the registers the unwind arrived at, A4 the exception, then the pad.

#include "UnwindContext.h"

namespace rts6x {

void UnwindContext::install(unsigned target, void *exception) const
{
    __rts6x_unwind_install(this, exception, target);
}

}  // namespace rts6x
