// Spec: Itanium C++ ABI EH 2.5.3 (S3) - a handler begins by taking the exception off the uncaught
// count and onto the caught chain, answering what the handler is to receive; it ends by coming off
// the chain once its last handler is done, the object then destroyed and freed unless rethrown.

#include <stdlib.h>
#include "Exception.h"

namespace rts6x {

void *Exception::begin()
{
    if (handlers++ == 0) {
        nextCaught = caught_;
        caught_ = this;
    }
    rethrown = false;
    if (uncaught_ > 0) uncaught_--;
    return adjusted;
}

void Exception::end()
{
    Exception *e = caught_;
    if (!e || --e->handlers > 0) return;
    caught_ = e->nextCaught;
    if (e->rethrown) return;
    if (e->destroy) e->destroy(e->object());
    free(e);
}

}  // namespace rts6x
