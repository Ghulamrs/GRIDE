// shm_c_atof.cpp - atof for Shalimar (docs\Runtime_Design.md 14.3), written by Toolchain.targets'
// ShalimarLibrary from toolchain\Windows\RT\catalogue.json in shc's build: change the catalogue, not this file.
#include "../Borrow.h"

#include <stdlib.h>

extern "C" double shm_c_atof(const ShmArray *p0) {
    const shm::CText a0(p0);
    const double r = atof(a0);
    return r;
}
