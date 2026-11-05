// shm_c_strerror.cpp - strerror for Shalimar (docs\Runtime_Design.md 14.3), written by Toolchain.targets'
// ShalimarLibrary from toolchain\Windows\RT\catalogue.json in shc's build: change the catalogue, not this file.
#include "../Borrow.h"

#include <string.h>

extern "C" void shm_c_strerror(int32_t p0, ShmArray *out) {
    const char *const r = strerror(p0);
    shm::copyText(out, r);
}
