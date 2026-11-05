// shm_c_scalbln.cpp - scalbln for Shalimar (docs\Runtime_Design.md 14.3), written by Toolchain.targets'
// ShalimarLibrary from toolchain\Windows\RT\catalogue.json in shc's build: change the catalogue, not this file.
#include "../Borrow.h"

#include <math.h>

extern "C" double shm_c_scalbln(double p0, int32_t p1) {
    const double r = scalbln(p0, static_cast<long>(p1));
    return r;
}
