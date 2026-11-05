// shm_c_system.cpp - system for Shalimar (docs\Runtime_Design.md 14.3), written by Toolchain.targets'
// ShalimarLibrary from toolchain\Windows\RT\catalogue.json in shc's build: change the catalogue, not this file.
#include "../Borrow.h"

#include <stdlib.h>

#ifndef __TMS320C6X__
extern "C" int32_t shm_c_system(const ShmArray *p0) {
    const shm::CText a0(p0);
    const int r = system(a0);
    return r;
}
#endif
