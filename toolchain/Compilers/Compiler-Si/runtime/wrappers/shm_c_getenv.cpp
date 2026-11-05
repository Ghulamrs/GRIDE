// shm_c_getenv.cpp - getenv for Shalimar (docs\Runtime_Design.md 14.3), written by Toolchain.targets'
// ShalimarLibrary from toolchain\Windows\RT\catalogue.json in shc's build: change the catalogue, not this file.
#include "../Borrow.h"

#include <stdlib.h>

extern "C" void shm_c_getenv(const ShmArray *p0, ShmArray *out) {
    const shm::CText a0(p0);
    const char *const r = getenv(a0);
    shm::copyText(out, r);
}
