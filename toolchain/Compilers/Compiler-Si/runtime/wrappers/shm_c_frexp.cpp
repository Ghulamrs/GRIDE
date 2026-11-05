// shm_c_frexp.cpp - frexp for Shalimar (docs\Runtime_Design.md 14.3), written by Toolchain.targets'
// ShalimarLibrary from toolchain\Windows\RT\catalogue.json in shc's build: change the catalogue, not this file.
// Two outputs come back through pointers after the inputs, as a Shalimar function's do.
#include "../Borrow.h"

#include <math.h>

extern "C" void shm_c_frexp(double p0, double *o0, int32_t *o1) {
    int w1 = 0;
    const double r = frexp(p0, &w1);
    *o0 = r;
    *o1 = w1;
}
