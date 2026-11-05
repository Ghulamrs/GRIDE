// shm_c_strlen.cpp - strlen for Shalimar (docs\Runtime_Design.md 14.3), written by Toolchain.targets'
// ShalimarLibrary from toolchain\Windows\RT\catalogue.json in shc's build: change the catalogue, not this file.
#include "../Borrow.h"

#include <string.h>

extern "C" int32_t shm_c_strlen(const ShmArray *p0) {
    const shm::CText a0(p0);
    const long long r = static_cast<long long>(strlen(a0));
    return shm::toInt("strlen", r);
}
