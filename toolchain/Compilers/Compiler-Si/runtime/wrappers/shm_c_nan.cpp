// shm_c_nan.cpp - nan for Shalimar (docs\Runtime_Design.md 14.3), written by Toolchain.targets'
// ShalimarLibrary from toolchain\Windows\RT\catalogue.json in shc's build: change the catalogue, not this file.
#include "../Borrow.h"

#include <math.h>

extern "C" double shm_c_nan(const ShmArray *p0) {
    const shm::CText a0(p0);
    const double r = nan(a0);
    return r;
}
