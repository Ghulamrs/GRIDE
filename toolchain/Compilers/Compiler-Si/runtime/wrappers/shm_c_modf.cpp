// shm_c_modf.cpp - modf for Shalimar (docs\Runtime_Design.md 14.3), written by Toolchain.targets'
// ShalimarLibrary from toolchain\Windows\RT\catalogue.json in shc's build: change the catalogue, not this file.
// Two outputs come back through pointers after the inputs, as a Shalimar function's do.
#include "../Borrow.h"

#include <math.h>

extern "C" void shm_c_modf(double p0, double *o0, double *o1) {
    double w1 = 0;
    const double r = modf(p0, &w1);
    *o0 = r;
    *o1 = w1;
}
