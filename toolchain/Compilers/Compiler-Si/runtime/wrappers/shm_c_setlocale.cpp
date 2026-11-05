// shm_c_setlocale.cpp - setlocale for Shalimar (docs\Runtime_Design.md 14.3), written by Toolchain.targets'
// ShalimarLibrary from toolchain\Windows\RT\catalogue.json in shc's build: change the catalogue, not this file.
#include "../Borrow.h"

#include <errno.h>
#include <locale.h>

extern "C" void shm_c_setlocale(int32_t p0, const ShmArray *p1, ShmArray *out) {
    const shm::CText a1(p1);
    errno = 0;
    const char *const r = setlocale(p0, a1);
    if (!r) shm::failed("setlocale");
    shm::copyText(out, r);
}
