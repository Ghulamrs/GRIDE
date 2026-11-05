// shm_c_mblen.cpp - mblen for Shalimar (docs\Runtime_Design.md 14.3), written by Toolchain.targets'
// ShalimarLibrary from toolchain\Windows\RT\catalogue.json in shc's build: change the catalogue, not this file.
#include "../Borrow.h"

#include <stdlib.h>

extern "C" int32_t shm_c_mblen(const ShmArray *p0, int32_t p1) {
    const shm::CText a0(p0);
    const int r = mblen(a0, shm::toSize("mblen", p1));
    return r;
}
