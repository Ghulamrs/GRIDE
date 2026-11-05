// shm_c_strncmp.cpp - strncmp for Shalimar (docs\Runtime_Design.md 14.3), written by Toolchain.targets'
// ShalimarLibrary from toolchain\Windows\RT\catalogue.json in shc's build: change the catalogue, not this file.
#include "../Borrow.h"

#include <string.h>

extern "C" int32_t shm_c_strncmp(const ShmArray *p0, const ShmArray *p1, int32_t p2) {
    const shm::CText a0(p0);
    const shm::CText a1(p1);
    const int r = strncmp(a0, a1, shm::toSize("strncmp", p2));
    return r;
}
